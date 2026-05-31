// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 SPL SFC SPI-NAND loader: read U-Boot proper from SPI-NAND
 * into DRAM and jump to it.
 *
 * Faithful port of the vendor known-good loader common/spl/spl_sfc_nand.c
 * (functions sfc_controler_init, spinand_probe_id, spinand_read_page
 * with bad-block check, sfc_nand_load) and the matching SFC_SEND_COMMAND
 * helper macro from arch/mips/include/asm/arch-t40/sfc.h.
 *
 * Vendor source-of-truth: ingenic-u-boot-xburst2 T40-1.3.1 (May 2026).
 *
 * Copyright (c) 2016 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <lzma/LzmaTools.h>
#include <mach/t40.h>
#include <mach/t31-sfc.h>
#include <mach/t40-sfc-nand.h>

DECLARE_GLOBAL_DATA_PTR;

void t40_spl_puts(const char *s);
void t40_spl_putc(char c);

static void puthex32(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 28; i >= 0; i -= 4)
		t40_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * SFC NAND device table - per-chip parameters from the vendor's
 * tools/ingenic-tools/nand_device/<vendor>_nand.c host-side files.
 *
 * Add a row per SPI-NAND chip we expect to see in the field. Each row
 * is small (~7 bytes packed). The probe walks this table and stops on
 * the first matching (manufacturer, device) ID pair.
 */
static const struct spl_nand_param nand_table[] = {
	/* Micron MT29F2G01ABAGD (2 Gbit, 2048-B page) */
	{ .pagesize = 2048, .id_manufactory = 0x2c, .device_id = 0x24,
	  .addrlen = 2, .ecc_bit = 4, .bit_counts = 3,
	  .eccstat_count = 1, .eccerrstatus = { 0x2 } },
	/* Winbond W25M02GV / W25N01GV - very common in T-series boards */
	{ .pagesize = 2048, .id_manufactory = 0xef, .device_id = 0xab,
	  .addrlen = 2, .ecc_bit = 4, .bit_counts = 2,
	  .eccstat_count = 1, .eccerrstatus = { 0x2 } },
	{ .pagesize = 2048, .id_manufactory = 0xef, .device_id = 0xaa,
	  .addrlen = 2, .ecc_bit = 4, .bit_counts = 2,
	  .eccstat_count = 1, .eccerrstatus = { 0x2 } },
	/* GigaDevice GD5F1GQ4UC (1 Gbit) */
	{ .pagesize = 2048, .id_manufactory = 0xc8, .device_id = 0xb1,
	  .addrlen = 2, .ecc_bit = 4, .bit_counts = 3,
	  .eccstat_count = 1, .eccerrstatus = { 0x7 } },
	/* GigaDevice GD5F2GQ4UC (2 Gbit) */
	{ .pagesize = 2048, .id_manufactory = 0xc8, .device_id = 0xb2,
	  .addrlen = 2, .ecc_bit = 4, .bit_counts = 3,
	  .eccstat_count = 1, .eccerrstatus = { 0x7 } },
};

static const struct spl_nand_param *curr_device;

/*
 * Pages-per-block. SPI-NAND 2-KiB-page parts are universally 64
 * pages/block (= 128 KiB blocks). If we ever support 4-KiB-page parts
 * we'll have to detect this from the device, but every chip in
 * nand_table[] above is 64 pages/block.
 */
#define SPI_NAND_PPB		64

#define THRESHOLD		31

/* Local register helpers, hardcoded for the T40 SFC at SFC_BASE. */
static inline void sfc_writel(u32 v, u32 off)
{
	writel(v, (void __iomem *)(SFC_BASE + off));
}

static inline u32 sfc_readl(u32 off)
{
	return readl((void __iomem *)(SFC_BASE + off));
}

static inline void sfc_transfer_direction(u32 v)
{
	u32 t = sfc_readl(SFC_GLB);

	t &= ~(1 << GLB_TRAN_DIR_OFFSET);
	t |= (v << GLB_TRAN_DIR_OFFSET);
	sfc_writel(t, SFC_GLB);
}

static inline void sfc_set_length(u32 v)
{
	sfc_writel(v, SFC_TRAN_LEN);
}

static inline void sfc_dev_addr(u32 ch, u32 v)
{
	sfc_writel(v, SFC_DEV_ADDR(ch));
}

static inline void sfc_tranconf_init(struct jz_sfc *sfc, u32 ch)
{
	sfc_writel(sfc->tranconf.d32, SFC_TRAN_CONF(ch));
}

static void sfc_set_transfer(struct jz_sfc *sfc, u32 dir)
{
	sfc_transfer_direction(dir);
	sfc_tranconf_init(sfc, 0);
	sfc_set_length(sfc->len);
	sfc_dev_addr(0, sfc->addr);
}

static void clear_end(void)
{
	while (!(sfc_readl(SFC_SR) & SFC_SR_END))
		;
	sfc_writel(CLR_END, SFC_SCR);
}

static void sfc_tranconf1_init(u32 tran_mode)
{
	u32 t = sfc_readl(SFC_TRAN_CONF1(0));

	t &= ~TRAN_CONF1_TRAN_MODE_MSK;
	t |= tran_mode << TRAN_CONF1_TRAN_MODE_OFFSET;
	sfc_writel(t, SFC_TRAN_CONF1(0));
}

static void sfc_send_cmd(struct jz_sfc *sfc, u8 dir)
{
	sfc_writel(1 << 1, SFC_TRIG);
	sfc_set_transfer(sfc, dir);
	sfc_writel(1 << 2, SFC_TRIG);
	sfc_writel(START, SFC_TRIG);
}

#define SFC_SEND_COMMAND(sfc, cmd_, len_, addr_, addr_width_, dmy_,	\
			 data_en_, dir_) do {				\
	(sfc)->tranconf.d32 = 0;					\
	(sfc)->tranconf.reg.cmd_en = 1;					\
	(sfc)->tranconf.reg.cmd = (cmd_);				\
	(sfc)->len = (len_);						\
	(sfc)->addr = (addr_);						\
	(sfc)->tranconf.reg.addr_width = (addr_width_);			\
	(sfc)->addr_plus = 0;						\
	(sfc)->tranconf.reg.dmy_bits = (dmy_);				\
	(sfc)->tranconf.reg.data_en = (data_en_);			\
	(sfc)->tranconf.reg.tran_mode = 0;				\
	if ((cmd_) == CMD_FR_CACHE_QUAD)				\
		sfc_tranconf1_init(TRAN_CONF1_SPI_QUAD);		\
	else								\
		sfc_tranconf1_init(TRAN_CONF1_SPI_STANDARD);		\
	sfc_send_cmd((sfc), (dir_));					\
} while (0)

static void sfc_write_one(u32 v)
{
	while (!(sfc_readl(SFC_SR) & (1 << 3)))		/* TRAN_REQ */
		;
	sfc_writel(1 << 3, SFC_SCR);			/* CLR_TREQ */
	sfc_writel(v, SFC_DR);
	clear_end();
}

static int sfc_read_buf(u32 *data, u32 byte_len)
{
	u32 word_len = (byte_len + 3) / 4;
	u32 read_so_far = 0;
	u32 fifo_num, sr;
	u32 i;

	while (1) {
		sr = sfc_readl(SFC_SR);
		if (sr & RECE_REQ) {
			sfc_writel(CLR_RREQ, SFC_SCR);
			fifo_num = word_len - read_so_far;
			if (fifo_num > THRESHOLD)
				fifo_num = THRESHOLD;

			for (i = 0; i < fifo_num; i++)
				data[read_so_far + i] = sfc_readl(SFC_DR);
			read_so_far += fifo_num;
		}
		if (read_so_far == word_len)
			break;
	}
	clear_end();

	return 0;
}

/*
 * SFC pinmux. In SFCNOR / SFCNAND cold boot the bootrom sets these up
 * before jumping to SPL; in USB-boot it does not. Mirror vendor T40
 * gpio_init() entry for SFC: PA23..PA28 -> FUNC_1.
 *
 * GPIO base 0xb0010000, port A at offset 0, port stride 0x1000.
 * FUNC_1: PAT0=set, PAT1=clear, MSK=clear, INT=clear, PUEN=clear,
 * PDEN=clear (per drivers/gpio/jz_gpio_common.c gpio_set_func()).
 */
#define PA_BASE		0xb0010000
#define PA_INTC		(PA_BASE + 0x18)
#define PA_MSKC		(PA_BASE + 0x28)
#define PA_PAT1C	(PA_BASE + 0x38)
#define PA_PAT0S	(PA_BASE + 0x44)
#define PA_PUENC	(PA_BASE + 0x118)
#define PA_PDENC	(PA_BASE + 0x128)
#define SFC_PA_PINS	(0x3fU << 23)	/* PA23..PA28 */

static void sfc_pinmux_init(void)
{
	writel(SFC_PA_PINS, (void __iomem *)PA_INTC);
	writel(SFC_PA_PINS, (void __iomem *)PA_MSKC);
	writel(SFC_PA_PINS, (void __iomem *)PA_PAT1C);
	writel(SFC_PA_PINS, (void __iomem *)PA_PAT0S);
	writel(SFC_PA_PINS, (void __iomem *)PA_PUENC);
	writel(SFC_PA_PINS, (void __iomem *)PA_PDENC);
}

/*
 * Mirror U-Boot proper drivers/spi/ingenic_sfc.c sfc_hw_init() so the
 * SFC is in the exact same state when SPL probes the SPI-NAND as it
 * is when U-Boot proper does the same RDID. SFC clock + ungate are
 * brought up earlier by t40_spl_sfc_clk_init() in arch/.../t40/sfc.c.
 */
static void sfc_controller_init(void)
{
	u32 t;
	int i;

	sfc_pinmux_init();

	/* SFC_GLB: wp_en=1, tran_dir=0, op_mode=0, threshold=31, phase_num=1.
	 * Bits: WP_EN=(1<<2), TRAN_DIR=(1<<13), OP_MODE=(1<<6),
	 *       THRESHOLD=(0x3f<<7), PHASE_NUM=(0x7<<3). */
	t = sfc_readl(SFC_GLB);
	t &= ~((1 << 13) | (1 << 6) | THRESHOLD_MSK | PHASE_NUM_MSK);
	t |= (1 << 2);				/* WP_EN */
	t |= (THRESHOLD << THRESHOLD_OFFSET);
	t |= (0x1 << PHASE_NUM_OFFSET);
	sfc_writel(t, SFC_GLB);

	/* SFC_DEV_CONF: cmd_type=0, cpol=0, cpha=0, smp_delay=0, thold=0,
	 * tsetup=0, tsh=0, ce_dl=1, hold_dl=1, wp_dl=1. */
	sfc_writel(0x7, SFC_DEV_CONF);

	/* Clear FMAT (TRAN_CONF0 bit 23) and TRAN_MODE (TRAN_CONF1 bits
	 * 4-7) for all 6 channels - leftover bits from previous SFC use
	 * (e.g. a prior NOR probe) break later transactions. */
	for (i = 0; i < 6; i++) {
		sfc_writel(sfc_readl(SFC_TRAN_CONF(i)) & ~(1 << 23),
			   SFC_TRAN_CONF(i));
		sfc_writel(sfc_readl(SFC_TRAN_CONF1(i)) &
			   ~TRAN_CONF1_TRAN_MODE_MSK,
			   SFC_TRAN_CONF1(i));
	}

	/* Clear all status flags and reset transaction state. */
	sfc_writel(CLR_END | (1 << 3) | CLR_RREQ |
		   (1 << 1) | (1 << 0), SFC_SCR);
	/* Mask all SFC interrupts: SFC_INTC bits 0-4 (END/TREQ/RREQ/OVER/UNDR). */
	sfc_writel(sfc_readl(0x70) | 0x1f, 0x70);
	sfc_writel(0, SFC_CGE);

	sfc_writel(1 << 1, SFC_TRIG);		/* STOP */
	sfc_writel(1 << 2, SFC_TRIG);		/* FLUSH */
	sfc_writel(0, SFC_TRAN_LEN);
}

static int probe_id_match(u8 vid, u8 pid)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(nand_table); i++) {
		if (nand_table[i].id_manufactory == vid &&
		    nand_table[i].device_id == pid) {
			curr_device = &nand_table[i];
			return 0;
		}
	}
	return -1;
}

static int spinand_probe_id(struct jz_sfc *sfc)
{
	/*
	 * SPI-NAND RDID layout (Micron / Winbond / GigaDevice / most
	 * 2-KiB-page chips): 1-byte dummy + manufacturer + device ID.
	 * The vendor SPL drives this as addr_width=0 / dmy_bits=8, which
	 * matches what U-Boot proper's spi-mem framework lowers to.
	 *
	 * Older Winbond W25NxxGV / W25M02GV: dmy_bits=0 / addr_width=1
	 * (one address byte then ID). Try both shapes; first chip table
	 * match wins.
	 */
	static const struct { u8 addr_width; u8 dmy_bits; } variants[] = {
		{ 0, 8 },
		{ 1, 0 },
	};
	int i;

	/*
	 * Issue CMD_RESET (0xff) to put the chip in a known state. Required
	 * after cold-boot from a vendor SPL or any prior SPL that left
	 * QUAD/ECC/BUF feature bits set: a chip stuck in QUAD-IO mode does
	 * not respond to single-IO RDID and our probe sees 0xff/0xff and
	 * declares FAILED. The reset clears feature regs to chip defaults
	 * and is harmless on a freshly-powered chip.
	 */
	sfc_writel(CLR_END | (1 << 3) | CLR_RREQ |
		   (1 << 1) | (1 << 0), SFC_SCR);
	SFC_SEND_COMMAND(sfc, 0xff, 0, 0, 0, 0, 0, 0);
	/* tRST(max) = 250 us per Micron / Winbond datasheets. */
	udelay(500);

	for (i = 0; i < (int)ARRAY_SIZE(variants); i++) {
		u8 buf[8] = { 0 };

		/* Clear all SFC status flags before each transfer (matches
		 * drivers/spi/ingenic_sfc.c sfc_exec_op behaviour). */
		sfc_writel(CLR_END | (1 << 3) | CLR_RREQ |
			   (1 << 1) | (1 << 0), SFC_SCR);
		SFC_SEND_COMMAND(sfc, CMD_RDID, 4, 0,
				 variants[i].addr_width,
				 variants[i].dmy_bits, 1, 0);
		sfc_read_buf((u32 *)buf, 4);

		if (probe_id_match(buf[0], buf[1]) == 0)
			return 0;
		/* The +1-offset retry handles the case where the chip
		 * returned (dummy, mfr, did, ...) instead of (mfr, did, ...). */
		if (probe_id_match(buf[1], buf[2]) == 0)
			return 0;
	}
	return -1;
}

static int spinand_init(void)
{
	struct jz_sfc sfc;
	u32 x;

	if (spinand_probe_id(&sfc) != 0) {
		t40_spl_puts("SPL: NAND probe FAILED\n");
		return -1;
	}

	/* Disable on-chip write-protect (status1.BP0..2 := 0). */
	x = 0;
	SFC_SEND_COMMAND(&sfc, CMD_SET_FEATURE, 1, FEATURE_REG_PROTECT,
			 1, 0, 1, 1);
	sfc_write_one(x);

	/* Enable QUAD-IO read mode + ECC + Winbond BUF=1 (no-op on others). */
	x = BITS_QUAD_EN | BITS_ECC_EN | BITS_BUF_EN;
	SFC_SEND_COMMAND(&sfc, CMD_SET_FEATURE, 1, FEATURE_REG_FEATURE1,
			 1, 0, 1, 1);
	sfc_write_one(x);

	return 0;
}

static int spinand_bad_block_check(int len, const u8 *buf)
{
	int j;

	for (j = 0; j < len; j++)
		if (buf[j] != 0xff)
			return 1;
	return 0;
}

/*
 * Read one page (or part of one) from SPI-NAND, with optional bad-block
 * check on the first-page-of-block boundary.
 *
 * Returns:
 *   0  : data is good
 *   1  : block is bad - caller should skip the rest of this block
 *  -1  : ECC uncorrectable for THIS page (don't trust data)
 */
static int spinand_read_page(u32 page, u32 column, u8 *dst,
			     u32 len, u32 pagesize)
{
	struct jz_sfc sfc;
	u32 read_buf;
	u8 read_id_oob = 0;
	u32 i;
	u8 checklen = 1;

read_oob:
	if (read_id_oob) {
		column = pagesize;
		len = 4;
		dst = (u8 *)&read_buf;
	}

	/* Page Read: transfer cell -> on-chip cache. Status1.OIP polled
	 * until 0 (transfer complete). */
	SFC_SEND_COMMAND(&sfc, CMD_PARD, 0, page, 3, 0, 0, 0);
	clear_end();
	do {
		SFC_SEND_COMMAND(&sfc, CMD_GET_FEATURE, 1,
				 FEATURE_REG_STATUS1, 1, 0, 1, 0);
		read_buf = 0;
		sfc_read_buf(&read_buf, 1);
	} while (read_buf & 0x1);

	/* ECC status check (vendor pattern: shift, mask by bit_counts,
	 * compare against eccerrstatus[]). */
	for (i = 0; i < curr_device->eccstat_count; i++) {
		if (((read_buf >> curr_device->ecc_bit) &
		     (~(0xff << curr_device->bit_counts))) ==
		    curr_device->eccerrstatus[i])
			return -1;
	}

	/*
	 * Parity-block plane select bit (column[12]) - required for the
	 * second 1024-page-half on certain Micron / XTX / ESMT chips
	 * with 2-plane geometry. Set the bit from page>>6 (page block index)
	 * only on these chips so we don't break others.
	 */
	if (curr_device->device_id == 0x20 ||
	    curr_device->device_id == 0x22 ||
	    curr_device->device_id == 0x24)
		column |= (((page >> 6) & 1) << 12);

	/* Quad-IO fast read from on-chip cache. 8 dummy cycles. */
	SFC_SEND_COMMAND(&sfc, CMD_FR_CACHE_QUAD, len, column,
			 curr_device->addrlen, 8, 1, 0);
	sfc_read_buf((u32 *)dst, len);

	/* Bad-block detection: on the first page of each block, also read
	 * the first OOB byte; if it's not 0xff, mark block bad. */
	if (!read_id_oob && !(page % SPI_NAND_PPB)) {
		read_id_oob = 1;
		goto read_oob;
	} else if (read_id_oob) {
		if (spinand_bad_block_check(checklen, (u8 *)&read_buf))
			return 1;
	}

	return 0;
}

/*
 * Public entry: read `count` bytes from NAND flash offset `src_addr`
 * (in bytes, page-aligned not required) into RAM at `dst_addr`.
 * Bad blocks are skipped (per vendor).
 */
int sfc_nand_load(u32 src_addr, u32 count, u32 dst_addr)
{
	u8 *buf = (u8 *)(uintptr_t)dst_addr;
	u32 pagesize;
	u32 pageaddr, columnaddr, rlen;
	int ret;

	if (!curr_device) {
		t40_spl_puts("SPL: NAND not probed\n");
		return -1;
	}
	pagesize = curr_device->pagesize;

	while (count) {
		pageaddr = src_addr / pagesize;
		columnaddr = src_addr % pagesize;
		rlen = (pagesize - columnaddr) < count ?
		       (pagesize - columnaddr) : count;
		ret = spinand_read_page(pageaddr, columnaddr, buf,
					rlen, pagesize);
		if (ret > 0) {
			t40_spl_puts("SPL: NAND bad block ");
			puthex32(pageaddr / SPI_NAND_PPB);
			t40_spl_puts(" - skipping\n");
			src_addr += SPI_NAND_PPB * pagesize;
			continue;
		}
		if (ret < 0)
			return -1;

		buf += rlen;
		src_addr += rlen;
		count -= rlen;
	}
	return 0;
}

/*
 * Public entry: bring up SFC + probe SPI-NAND. Returns 0 on success.
 * Must be called before sfc_nand_load().
 */
int sfc_nand_init(void)
{
	sfc_controller_init();
	return spinand_init();
}

/*
 * Custom U-Boot loader for the SPI-NAND cold-boot path. With DM-in-SPL,
 * DDR is up via UCLASS_RAM and the SPL framework's malloc heap is at
 * CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR (4 MiB in DRAM), but mainline
 * doesn't have a generic SPL SPI-NAND loader that goes through DM.
 * We open-code a small loader: read the legacy mkimage header from a
 * fixed NAND offset, LZMA-decompress the payload to its load address,
 * and jump.
 *
 * Flash layout (matches the binman dtsi):
 *   NAND  offset 0       : SPL (boot header + SPL code)
 *   NAND  offset T40_NAND_UBOOT_OFFSET (0x80000) : LZMA U-Boot
 *
 * Use a NAND-friendly offset that's well past the SPL region with
 * margin for the bootrom's hardware ECC + bad-block tolerance.
 */
#define T40_NAND_UBOOT_OFFSET	0x80000		/* 512 KiB into NAND */
#define T40_IH_MAGIC		0x27051956
#define T40_IH_HDR_LEN		64
#define T40_UBOOT_SCRATCH	0x81000000	/* DRAM compressed scratch */
#define T40_UBOOT_MAX		0x00800000	/* 8 MiB decompressed bound */
#define T40_DRAM_BASE		0x80000000
#define T40_DRAM_MAX		0x10000000	/* 256 MiB T40XP */
#define T40_SPL_HEAP_BASE	0x80a00000	/* LZMA decoder malloc pool */
#define T40_SPL_HEAP_SIZE	0x00200000	/* 2 MiB (1 MiB dict + state) */

static u32 hdr_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8)  | (u32)p[3];
}

/*
 * Enter U-Boot proper through KSEG1. The bootrom-locked SPL cache
 * lines are never touched because U-Boot proper landed in DRAM via
 * the SFC controller (which doesn't pass through cache).
 */
static void __attribute__((noreturn)) t40_jump_to_uboot(unsigned long target)
{
	unsigned long kseg1 = (target & 0x1fffffff) | 0xa0000000;

	asm volatile(
		".set push\n"
		".set noreorder\n"
		"jr    %0\n"
		" nop\n"
		".set pop\n"
		: : "r"(kseg1)
		: "memory");
	__builtin_unreachable();
}

void t40_spl_nand_load_uboot(void)
{
	u8 *scratch = (u8 *)T40_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	/* Bring up the SFC controller + probe the NAND chip. */
	if (sfc_nand_init() != 0) {
		t40_spl_puts("SPL: SPI-NAND init failed\n");
		return;
	}

	/* Read the 64-byte legacy mkimage header from NAND. */
	if (sfc_nand_load(T40_NAND_UBOOT_OFFSET, T40_IH_HDR_LEN,
			  T40_UBOOT_SCRATCH) != 0) {
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
	if (ih_size == 0 || ih_size > T40_UBOOT_MAX) {
		t40_spl_puts("SPL: U-Boot image size out of range\n");
		return;
	}
	if (ih_load < T40_DRAM_BASE ||
	    ih_load >= T40_DRAM_BASE + T40_DRAM_MAX) {
		t40_spl_puts("SPL: U-Boot load addr out of range\n");
		return;
	}

	/* Read header + LZMA payload into DRAM scratch. */
	if (sfc_nand_load(T40_NAND_UBOOT_OFFSET, T40_IH_HDR_LEN + ih_size,
			  T40_UBOOT_SCRATCH) != 0) {
		t40_spl_puts("SPL: U-Boot payload read failed\n");
		return;
	}

	/*
	 * Re-point malloc at a 2 MiB DRAM heap. The SPL framework's
	 * full dlmalloc heap (CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR) is
	 * activated only by board_init_r(); we never call that because
	 * we hand off via t40_jump_to_uboot() below. The LZMA decoder
	 * needs ~170 KB of allocations which won't fit in the SPL F
	 * malloc area (CONFIG_SPL_SYS_MALLOC_F_LEN, ~16 KB). Switch to
	 * malloc_simple in a DRAM window so the LZMA single ~32 KB
	 * probability-table allocation always succeeds.
	 */
	gd->malloc_base = T40_SPL_HEAP_BASE;
	gd->malloc_limit = T40_SPL_HEAP_SIZE;
	gd->malloc_ptr = 0;
	gd->flags &= ~GD_FLG_FULL_MALLOC_INIT;

	/*
	 * Decompress to the KSEG1 alias of the load address. The bootrom
	 * locks the SPL cache lines; writing through KSEG1 keeps the
	 * decompressed U-Boot in real DRAM, and t40_jump_to_uboot()
	 * enters via KSEG1 so we never touch the locked SPL cache.
	 */
	out_len = T40_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress(
		(unsigned char *)(uintptr_t)((ih_load & 0x1fffffff) | 0xa0000000),
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
