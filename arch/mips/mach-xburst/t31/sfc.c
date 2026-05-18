// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T31 SPL SPI-NOR loader: read U-Boot proper from SFC NOR
 * flash into DRAM and jump to it.
 *
 * Faithful transliteration of the vendor known-good loader
 * common/spl/spl_sfc_nor.c (functions jz_sfc_readl/writel,
 * sfc_set_mode, sfc_dev_addr_dummy_bytes, sfc_set_read_reg,
 * sfc_read_data, sfc_read, sfc_init, sfc_nor_load) and the SSI branch
 * of the vendor clk_set_rate() (arch/mips/cpu/xburst/t31/clk.c).
 *
 * The register write order, bit fields and poll loops are reproduced
 * exactly from the vendor source: do not hand-roll, reorder or guess.
 *
 * U-Boot proper is stored LZMA-compressed inside a 64-byte legacy
 * mkimage header (Makefile u-boot-lzma.img). The SPL reads the
 * header, SFC-reads the exact compressed payload and LZMA-decompresses
 * it to CONFIG_TEXT_BASE, then jumps. Reading the exact size from the
 * header (not a fixed window) retires the old truncation landmine.
 * The Makefile caps the LZMA dictionary at 1 MiB so the decoder's
 * dictionary malloc fits the SPL heap.
 *
 * DRAM is already up (sdram_init() ran) so 0x80100000 is valid.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <asm/global_data.h>
#include <lzma/LzmaTools.h>
#include <malloc.h>
#include <mach/t31.h>
#include <mach/t31-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Flash layout for the isvp_t31_sfcnor_ddr128M profile:
 *   flash 0           : SPL (boot header + SPL code)
 *   flash UBOOT_OFFSET : U-Boot proper raw image (run at 0x80100000)
 *
 * The headered SPL is ~17 KB and CONFIG_SPL_MAX_SIZE is 0x5800
 * (22.5 KB), so 0x8000 (32 KB) is a clean, comfortably-clear
 * boundary - no point wasting flash on a 0x10000 gap. This MUST
 * stay in sync with the U-Boot entry offset in the binman image
 * (arch/mips/dts/t31-isvp-u-boot.dtsi).
 */
#define T31_UBOOT_OFFSET	0x8000		/* must match binman u-boot @ */
#define T31_UBOOT_LOAD_ADDR	0x80100000	/* CONFIG_SYS_TEXT_BASE */
/*
 * U-Boot proper is a gzip payload wrapped in a 64-byte legacy mkimage
 * header at T31_UBOOT_OFFSET. Read the header for the exact compressed
 * size into a DRAM scratch, gunzip to the load address. Heap is in
 * DRAM (up by now); all regions are disjoint: U-Boot 0x80100000.., a
 * gunzip heap at 0x80a00000, compressed scratch at 0x81000000.
 */
#define T31_IH_MAGIC		0x27051956
#define T31_IH_HDR_LEN		64
#define T31_UBOOT_SCRATCH	0x81000000	/* compressed image in DRAM */
#define T31_SPL_HEAP_BASE	0x80a00000	/* LZMA decoder malloc pool */
#define T31_SPL_HEAP_SIZE	0x00200000	/* 2 MiB (1 MiB dict + state) */
#define T31_UBOOT_MAX		0x00800000	/* decompressed size bound */

/* SSI/SFC clock target, from vendor sfc_init(): clk_set_rate(SSI, 70M) */
#define T31_SSI_RATE		70000000U
/*
 * MPLL rate for this profile (1200 MHz; same MPLL the DDR clock uses,
 * see DDR_MPLL_RATE in <mach/t31-ddr.h>). Vendor cgu_clk_sel[SSI]
 * selects MPLL as the SSI source (non-CONFIG_BURNER path).
 */
#define T31_MPLL_RATE		1200000000U

/*
 * SSI cgu entry from the vendor cgu_clk_sel[] table (clk.c):
 *   [SSI] = {1, CPM_SSICDR, 30, MPLL, {APLL, MPLL, VPLL}, 28, 27, 26}
 * i.e. off=CPM_SSICDR, sel_src=MPLL, ce=bit 28, busy=bit 27,
 * stop=bit 26. These bits differ from the DDR entry; do not guess.
 */
#define CPM_SSICDR		0x74
#define SSI_CGU_CE		28
#define SSI_CGU_BUSY		27
#define SSI_CGU_STOP		26

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
static void ssi_clk_set_rate(void)
{
	unsigned int pll_rate = T31_MPLL_RATE;
	unsigned int rate = T31_SSI_RATE;
	unsigned int cdr;
	u32 regval;

	regval = cpm_readl(CPM_SSICDR);

	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);

	cdr = (pll_rate / rate - 1) & 0xff;

	regval &= ~((3 << SSI_CGU_STOP) | 0xff);
	regval |= ((1 << SSI_CGU_CE) | cdr);
	cpm_writel(regval, CPM_SSICDR);
	while (cpm_readl(CPM_SSICDR) & (1 << SSI_CGU_BUSY))
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

	ssi_clk_set_rate();		/* vendor: clk_set_rate(SSI, 70000000) */

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

extern void t31_spl_puts(const char *s);

static u32 hdr_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

/*
 * Entry point used by board_init_f(): read the LZMA-compressed,
 * mkimage-wrapped U-Boot proper from NOR, decompress to its load
 * address and jump (no return). DRAM is already initialised.
 */
void t31_spl_load_uboot(void)
{
	u8 *scratch = (u8 *)T31_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	/* Legacy mkimage header (64 bytes, big-endian fields). */
	sfc_nor_load(T31_UBOOT_OFFSET, T31_IH_HDR_LEN, T31_UBOOT_SCRATCH);
	ih_magic = hdr_be32(scratch + 0);
	ih_size  = hdr_be32(scratch + 12);
	ih_load  = hdr_be32(scratch + 16);
	if (ih_magic != T31_IH_MAGIC) {
		t31_spl_puts("SPL: bad U-Boot image magic\n");
		return;
	}

	/* Header + gzip payload into DRAM scratch. */
	sfc_nor_load(T31_UBOOT_OFFSET, T31_IH_HDR_LEN + ih_size,
		     T31_UBOOT_SCRATCH);

	/*
	 * The lean SPL has no heap until here; DRAM is up. This custom
	 * SPL never calls spl_init(), so set GD_FLG_FULL_MALLOC_INIT
	 * ourselves or malloc() stays on the tiny malloc_simple arena
	 * and the LZMA dictionary allocation fails (SZ_ERROR_MEM).
	 */
	mem_malloc_init(T31_SPL_HEAP_BASE, T31_SPL_HEAP_SIZE);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;

	out_len = T31_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress((unsigned char *)(uintptr_t)ih_load,
				       &out_len, scratch + T31_IH_HDR_LEN,
				       (SizeT)ih_size);
	if (ret) {
		char m[2] = { (char)('0' + (ret & 7)), 0 };
		t31_spl_puts("SPL: U-Boot LZMA decompress failed, SZ_ERROR=");
		t31_spl_puts(m);
		t31_spl_puts("\n");
		return;
	}

	/* Jump to the load address (== entry for a -T standalone image). */
	((void (*)(void))(uintptr_t)ih_load)();
}
