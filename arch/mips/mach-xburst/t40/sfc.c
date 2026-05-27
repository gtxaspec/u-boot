// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 SPL SPI-NOR loader: read U-Boot proper from SFC NOR
 * flash into DRAM and jump to it.
 *
 * Faithful transliteration of the vendor known-good loader
 * common/spl/spl_sfc_nor.c (functions jz_sfc_readl/writel,
 * sfc_set_mode, sfc_dev_addr_dummy_bytes, sfc_set_read_reg,
 * sfc_read_data, sfc_read, sfc_init, sfc_nor_load) and the SSI branch
 * of the vendor clk_set_rate() (arch/mips/cpu/xburst/a1/clk.c).
 *
 * The register write order, bit fields and poll loops are reproduced
 * exactly from the vendor source: do not hand-roll, reorder or guess.
 *
 * U-Boot proper is stored LZMA-compressed inside a 64-byte legacy
 * mkimage header (Makefile u-boot-lzma.img). The SPL reads the
 * header, SFC-reads the exact compressed payload and LZMA-decompresses
 * it to CONFIG_TEXT_BASE, then jumps. Reading the exact size from the
 * header (not a fixed window) retires the old truncation landmine.
 * The decoder allocates only its ~32 KiB probability table (the
 * dictionary is the output buffer); t40_spl_load_uboot() points
 * malloc_simple at a DRAM window so that allocation always succeeds.
 *
 * DRAM is already up (sdram_init() ran) so 0x80100000 is valid.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <asm/global_data.h>
#include <lzma/LzmaTools.h>
#include <malloc.h>
#include <mach/t40.h>
#include <mach/t31-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Flash layout for the isvp_t40_sfcnor_ddr128M profile:
 *   flash 0           : SPL (boot header + SPL code)
 *   flash UBOOT_OFFSET : U-Boot proper raw image (run at 0x80100000)
 *
 * The headered SPL is ~17 KB and CONFIG_SPL_MAX_SIZE is 0x5800
 * (22.5 KB), so 0x8000 (32 KB) is a clean, comfortably-clear
 * boundary - no point wasting flash on a 0x10000 gap. This MUST
 * stay in sync with the U-Boot entry offset in the binman image
 * (arch/mips/dts/a1-isvp-u-boot.dtsi).
 */
#define T40_UBOOT_OFFSET	0x8000		/* must match binman u-boot @ */
#define T40_UBOOT_LOAD_ADDR	0x80100000	/* CONFIG_SYS_TEXT_BASE */
/*
 * U-Boot proper is a gzip payload wrapped in a 64-byte legacy mkimage
 * header at T40_UBOOT_OFFSET. Read the header for the exact compressed
 * size into a DRAM scratch, gunzip to the load address. Heap is in
 * DRAM (up by now); all regions are disjoint: U-Boot 0x80100000.., a
 * gunzip heap at 0x80a00000, compressed scratch at 0x81000000.
 */
#define T40_IH_MAGIC		0x27051956
#define T40_IH_HDR_LEN		64
#define T40_UBOOT_SCRATCH	0x81000000	/* compressed image in DRAM */
#define T40_SPL_HEAP_BASE	0x80a00000	/* LZMA decoder malloc pool */
#define T40_SPL_HEAP_SIZE	0x00200000	/* 2 MiB (1 MiB dict + state) */
#define T40_UBOOT_MAX		0x00800000	/* decompressed size bound */
#define T40_DRAM_BASE		0x80000000	/* KSEG0 cached DRAM */
#define T40_DRAM_MAX		0x4000000	/* 512 MB A1N */

/* SSI/SFC clock target, from vendor sfc_init(): clk_set_rate(SSI, 70M) */
#define T40_SFC_RATE		70000000U
/*
 * MPLL rate for this profile (1200 MHz; same MPLL the DDR clock uses,
 * see pll.c T40_MPLL_MHZ). Vendor cgu_clk_sel[SSI]
 * selects MPLL as the SSI source (non-CONFIG_BURNER path).
 */
#define T40_MPLL_RATE		1000000000U

/*
 * SFC CGU entry: T40 CPM_SFCCDR is at offset 0x60 (from mach/t40.h).
 * ce/busy/stop are at bits 29/28/27 (matching the XBurst2 CGU
 * convention shared with A1).
 */
#define SFC_CGU_CE		29
#define SFC_CGU_BUSY		28
#define SFC_CGU_STOP		27

static u32 cpm_readl(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_writel(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static u32 jz_sfc_readl(unsigned int offset)
{
	return readl((void __iomem *)(SFC_BASE + offset));
}

static void jz_sfc_writel(unsigned int value, unsigned int offset)
{
	writel(value, (void __iomem *)(SFC_BASE + offset));
}

/*
 * SSI branch of the vendor clk_set_rate() (clk.c). SSI is not
 * MSC0/MSC1 and not DDR, so:
 *   cdr = (pll_rate/rate - 1) & 0xff
 *   regval &= ~(3 << cgu->stop | 0xff)
 *   regval |= (1 << cgu->ce) | cdr
 *   write; poll while regval & (1 << cgu->busy)
 * pll_rate is rounded to a multiple of rate exactly as the vendor does.
 */
static void sfc_clk_set_rate(void)
{
	unsigned int pll_rate = T40_MPLL_RATE;
	unsigned int rate = T40_SFC_RATE * 4;
	unsigned int cdr;
	u32 regval;

	regval = cpm_readl(CPM_SFCCDR);

	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);

	cdr = (pll_rate / rate - 1) & 0xff;

	/* Set source to MPLL (sel=1 -> bits 31:30 = 01), clear stop+div, set CE+cdr */
	regval &= ~((3u << 30) | (3 << SFC_CGU_STOP) | 0xff);
	regval |= (1u << 30) | (1 << SFC_CGU_CE) | cdr;
	cpm_writel(regval, CPM_SFCCDR);
	while (cpm_readl(CPM_SFCCDR) & (1 << SFC_CGU_BUSY))
		;
}

static void sfc_set_mode(int channel, int value)
{
	unsigned int tmp;

	tmp = jz_sfc_readl(SFC_TRAN_CONF(channel));
	tmp &= ~(TRAN_MODE_MSK);
	tmp |= (value << TRAN_MODE_OFFSET);
	jz_sfc_writel(tmp, SFC_TRAN_CONF(channel));
}

static void sfc_dev_addr_dummy_bytes(int channel, unsigned int value)
{
	unsigned int tmp;

	tmp = jz_sfc_readl(SFC_TRAN_CONF(channel));
	tmp &= ~TRAN_CONF_DMYBITS_MSK;
	tmp |= (value << TRAN_CONF_DMYBITS_OFFSET);
	jz_sfc_writel(tmp, SFC_TRAN_CONF(channel));
}

static void sfc_set_read_reg(unsigned int cmd, unsigned int addr,
		unsigned int addr_plus, unsigned int addr_len,
		unsigned int data_en)
{
	unsigned int tmp;

	tmp = jz_sfc_readl(SFC_GLB);
	tmp &= ~PHASE_NUM_MSK;
	tmp |= (0x1 << PHASE_NUM_OFFSET);
	jz_sfc_writel(tmp, SFC_GLB);

	if (data_en) {
		tmp = (addr_len << ADDR_WIDTH_OFFSET) | CMD_EN |
			DATEEN | (cmd << CMD_OFFSET);
	} else {
		tmp = (addr_len << ADDR_WIDTH_OFFSET) | CMD_EN |
			(cmd << CMD_OFFSET);
	}

	jz_sfc_writel(tmp, SFC_TRAN_CONF(0));
	jz_sfc_writel(addr, SFC_DEV_ADDR(0));
	jz_sfc_writel(addr_plus, SFC_DEV_ADDR_PLUS(0));

	sfc_dev_addr_dummy_bytes(0, 0);
	sfc_set_mode(0, 0);

	jz_sfc_writel(START, SFC_TRIG);
}

static int sfc_read_data(unsigned int *data, unsigned int len)
{
	unsigned int tmp_len = 0;
	unsigned int fifo_num = 0;
	unsigned int i;

	while (1) {
		if (jz_sfc_readl(SFC_SR) & RECE_REQ) {
			jz_sfc_writel(CLR_RREQ, SFC_SCR);
			if ((len - tmp_len) > THRESHOLD)
				fifo_num = THRESHOLD;
			else
				fifo_num = len - tmp_len;

			for (i = 0; i < fifo_num; i++) {
				*data = jz_sfc_readl(SFC_DR);
				data++;
				tmp_len++;
			}
		}

		if (tmp_len == len)
			break;
	}

	while ((jz_sfc_readl(SFC_SR) & SFC_SR_END) != SFC_SR_END)
		;
	jz_sfc_writel(CLR_END, SFC_SCR);

	return 0;
}

static int sfc_read(unsigned int addr, unsigned int addr_plus,
		unsigned int addr_len, unsigned int *data, unsigned int len)
{
	unsigned int cmd, ret;

	jz_sfc_writel(STOP, SFC_TRIG);
	jz_sfc_writel(FLUSH, SFC_TRIG);

	cmd = CMD_READ;

	jz_sfc_writel((len * 4), SFC_TRAN_LEN);

	sfc_set_read_reg(cmd, addr, addr_plus, addr_len, 1);

	ret = sfc_read_data(data, len);
	if (ret)
		return ret;
	else
		return 0;
}

static void sfc_init(void)
{
	unsigned int tmp;

	/* Ungate SFC0 clock (CLKGR0 bit 24) */
	tmp = cpm_readl(CPM_CLKGR0);
	tmp &= ~CPM_CLKGR0_SFC;
	cpm_writel(tmp, CPM_CLKGR0);

	/*
	 * Re-program CPM_SFCCDR. The vendor CGU source mux for SFC is
	 * sel=0:APLL, sel=1:MPLL, sel=2:VPLL (per t40 clk.c
	 * cgu_clk_sel[SFC]). MPLL therefore lives at bits 31:30 = 01,
	 * NOT 10. div=80 with MPLL=1000 gives SFC ~12 MHz.
	 *
	 * Poll BUSY rather than spinning for a fixed cycle count: the CGU
	 * does not finalize the clock change until BUSY clears, and a
	 * stuck BUSY leaves the controller unclocked.
	 */
	{
		u32 reg = cpm_readl(CPM_SFCCDR);
		reg &= ~((3u << 30) | (3 << SFC_CGU_STOP) | 0xff);
		reg |= (1u << 30) | (1 << SFC_CGU_CE) | 80;
		cpm_writel(reg, CPM_SFCCDR);
		while (cpm_readl(CPM_SFCCDR) & (1 << SFC_CGU_BUSY))
			;
	}

	tmp = THRESHOLD << THRESHOLD_OFFSET;
	jz_sfc_writel(tmp, SFC_GLB);

	tmp = CEDL | HOLDDL | WPDL;
	jz_sfc_writel(tmp, SFC_DEV_CONF);

	/* low power consumption */
	jz_sfc_writel(0, SFC_CGE);
}

static void sfc_nor_load(unsigned int src_addr, unsigned int count,
		unsigned int dst_addr)
{
	unsigned int addr_len, words_of_spl;

	/* spi norflash addr len */
	addr_len = 3;

	/* count word align */
	words_of_spl = (count + 3) / 4;

	sfc_init();

	sfc_read(src_addr, 0x0, addr_len, (unsigned int *)(dst_addr),
		 words_of_spl);
}

/*
 * Bring up the SFC0 clock + controller without doing a NOR read. The
 * USB-boot SPL calls this so U-Boot proper, uploaded into DRAM after
 * the SPL returns, inherits a clocked SFC and can probe the SPI-NOR.
 * The NOR-boot SPL gets the same setup via sfc_nor_load() -> sfc_init().
 */
void t40_spl_sfc_clk_init(void)
{
	sfc_init();
}

extern void t40_spl_puts(const char *s);

/*
 * Enter U-Boot proper through its uncached KSEG1 alias.
 *
 * The bootrom runs the SPL from cache-as-SRAM at 0x80001000 and locks
 * those lines (XBurst2 cache 0x1C Fill+Lock); any Index cache op walks
 * every set/way, hits the locked lines and hangs the CPU. U-Boot proper
 * is copied into DRAM through the uncached KSEG1 alias, so it is fully
 * resident in DRAM - we enter it at its KSEG1 address and no cache
 * flush is needed; the locked SPL lines are never touched.
 */
static void __attribute__((noreturn))
t40_jump_to_uboot(unsigned long target)
{
	unsigned long kseg1 = (target & 0x1fffffff) | 0xa0000000;

	asm volatile(
		".set push\n"
		".set noreorder\n"
		"jr    %0\n"
		" nop\n"
		".set pop\n"
		: : "r"(kseg1)
		: "memory"
	);
	__builtin_unreachable();
}

static u32 hdr_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

static int sfc_nor_load_wrapper(u32 src, u32 cnt, u32 dst)
{
	sfc_nor_load(src, cnt, dst);
	return 0;
}

/*
 * Entry point used by board_init_f(): read the LZMA-compressed,
 * mkimage-wrapped U-Boot proper from SPI flash (NOR or NAND, per
 * read_fn), decompress to its load address and jump (no return).
 * DRAM is already initialised.
 */
void t40_spl_load_uboot_with(int (*read_fn)(u32 src, u32 cnt, u32 dst))
{
	u8 *scratch = (u8 *)T40_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	/* Legacy mkimage header (64 bytes, big-endian fields). */
	if (read_fn(T40_UBOOT_OFFSET, T40_IH_HDR_LEN, T40_UBOOT_SCRATCH)) {
		t40_spl_puts("SPL: U-Boot header read failed\n");
		return;
	}
	ih_magic = hdr_be32(scratch + 0);
	ih_size  = hdr_be32(scratch + 12);
	ih_load  = hdr_be32(scratch + 16);
	if (ih_magic != T40_IH_MAGIC) {
		t40_spl_puts("SPL: bad U-Boot image magic\n");
		return;
	}

	/*
	 * The header fields come off flash - bound them before they drive a
	 * read length / decompress destination / jump. ih_size caps the
	 * scratch write; ih_load must land in DRAM.
	 */
	if (ih_size == 0 || ih_size > T40_UBOOT_MAX) {
		t40_spl_puts("SPL: U-Boot image size out of range\n");
		return;
	}
	if (ih_load < T40_DRAM_BASE ||
	    ih_load >= T40_DRAM_BASE + T40_DRAM_MAX) {
		t40_spl_puts("SPL: U-Boot load addr out of range\n");
		return;
	}

	/* Header + LZMA payload into DRAM scratch. */
	if (read_fn(T40_UBOOT_OFFSET, T40_IH_HDR_LEN + ih_size,
		    T40_UBOOT_SCRATCH)) {
		t40_spl_puts("SPL: U-Boot payload read failed\n");
		return;
	}

	/*
	 * The lean SPL never runs the normal SPL malloc init. dlmalloc is
	 * unusable here - even after mem_malloc_init() sets a correct 2 MiB
	 * arena it returns NULL for every request - so route malloc() to
	 * the simple bump allocator instead: point it at the 2 MiB DRAM
	 * window and clear GD_FLG_FULL_MALLOC_INIT. malloc_simple is a
	 * trivial base+offset allocator and cannot fail for the single
	 * ~32 KiB probability-table allocation the LZMA decoder makes.
	 */
	gd->malloc_base = T40_SPL_HEAP_BASE;
	gd->malloc_limit = T40_SPL_HEAP_SIZE;
	gd->malloc_ptr = 0;
	gd->flags &= ~GD_FLG_FULL_MALLOC_INIT;

	/*
	 * Decompress straight to the uncached KSEG1 alias of the load
	 * address. The bootrom locks the SPL cache-as-SRAM lines, so the
	 * image must land directly in DRAM - a cached write would strand
	 * part of it in L1/L2 and the SPL cannot run an Index dcache
	 * flush without hanging. t40_jump_to_uboot() enters via KSEG1.
	 */
	out_len = T40_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress(
		(unsigned char *)(uintptr_t)((ih_load & 0x1fffffff) |
					     0xa0000000),
		&out_len, scratch + T40_IH_HDR_LEN, (SizeT)ih_size);
	if (ret) {
		char m[2] = { (char)('0' + (ret & 7)), 0 };

		t40_spl_puts("SPL: U-Boot LZMA decompress failed, SZ_ERROR=");
		t40_spl_puts(m);
		t40_spl_puts("\n");
		return;
	}

	t40_jump_to_uboot(ih_load);
}

void t40_spl_load_uboot(void)
{
	t40_spl_load_uboot_with(sfc_nor_load_wrapper);
}
