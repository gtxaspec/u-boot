// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 SPL SPI-NOR loader: read U-Boot proper from SFC NOR
 * into DRAM, LZMA-decompress and jump.
 *
 * Faithful transliteration of the lean vendor NOR path (same code
 * base as t31/t23 sfc.c). The SFC block and the SSI cgu entry are
 * identical to T31/T23 (same SFC_BASE, same CPM_SSICDR); only the
 * MPLL source rate differs (T20N MPLL = 900 MHz). DRAM is already
 * up (sdram_init() ran) so the scratch/heap/load addresses are
 * valid.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <asm/global_data.h>
#include <lzma/LzmaTools.h>
#include <malloc.h>
#include <mach/t20.h>
#include <mach/t20-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Flash layout for the isvp_t20 NOR profile (same as T31/T23):
 *   flash 0            : SPL (boot header + SPL code)
 *   flash UBOOT_OFFSET : U-Boot proper image (run at 0x80100000)
 *
 * DRAM scratch/heap addresses fit the 64 MB T20N (load 0x80100000,
 * heap 0x80a00000, scratch 0x81000000).
 */
#define T20_UBOOT_OFFSET	0x10000		/* CONFIG_UBOOT_OFFSET */
#define T20_UBOOT_LOAD_ADDR	0x80100000	/* CONFIG_TEXT_BASE */
#define T20_IH_MAGIC		0x27051956
#define T20_IH_HDR_LEN		64
#define T20_UBOOT_SCRATCH	0x81000000	/* compressed image in DRAM */
#define T20_SPL_HEAP_BASE	0x80a00000	/* LZMA decoder malloc pool */
#define T20_SPL_HEAP_SIZE	0x00200000	/* 2 MiB (1 MiB dict + state) */
#define T20_UBOOT_MAX		0x00800000	/* decompressed size bound */
#define T20_DRAM_BASE		0x80000000	/* KSEG0 cached DRAM */
#define T20_DRAM_MAX		0x08000000	/* 128 MB (covers all variants) */

/* SSI/SFC clock target, from vendor sfc_init(): clk_set_rate(SSI, 70M) */
#define T20_SSI_RATE		70000000U
/* T20L MPLL is 1000 MHz (t20/pll.c), the SSI cgu source. */
#define T20_MPLL_RATE		1000000000U

/*
 * SSI cgu entry from the vendor T20 cgu_clk_sel[] table (clk.c) -
 * NOTE the T20 layout differs from T31/T23/T30:
 *   [SSI] = {1, CPM_SSICDR, 31, {APLL, MPLL, -1, -1}, 29, 28, 27}
 * off=CPM_SSICDR, PLL-select = single bit 31 (0=APLL, 1=MPLL),
 * ce=bit 29, busy=bit 28, stop=bit 27 (T31/T23 SSICDR is 28/27/26
 * with a [31:30] 2-bit select - do not reuse that here).
 */
#define CPM_SSICDR		0x74
#define SSI_CGU_CE		29
#define SSI_CGU_BUSY		28
#define SSI_CGU_STOP		27
#define SSI_SRC_MPLL		(1u << 31)	/* select MPLL (sel = 1) */

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
	unsigned int pll_rate = T20_MPLL_RATE;
	unsigned int rate = T20_SSI_RATE;
	unsigned int cdr;
	u32 regval;

	regval = cpm_readl(CPM_SSICDR);

	if (pll_rate % rate >= rate / 2)
		pll_rate += rate - (pll_rate % rate);
	else
		pll_rate -= (pll_rate % rate);

	cdr = (pll_rate / rate - 1) & 0xff;

	regval &= ~((3 << SSI_CGU_STOP) | 0xff);
	regval |= (SSI_SRC_MPLL | (1 << SSI_CGU_CE) | cdr);
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

extern void t20_spl_puts(const char *s);

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
void t20_spl_load_uboot(void)
{
	u8 *scratch = (u8 *)T20_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	/* Legacy mkimage header (64 bytes, big-endian fields). */
	sfc_nor_load(T20_UBOOT_OFFSET, T20_IH_HDR_LEN, T20_UBOOT_SCRATCH);
	ih_magic = hdr_be32(scratch + 0);
	ih_size  = hdr_be32(scratch + 12);
	ih_load  = hdr_be32(scratch + 16);
	if (ih_magic != T20_IH_MAGIC) {
		t20_spl_puts("SPL: bad U-Boot image magic\n");
		return;
	}

	/*
	 * The header fields come off NOR - bound them before they drive a
	 * read length / decompress destination / jump. ih_size caps the
	 * scratch write; ih_load must land in DRAM.
	 */
	if (ih_size == 0 || ih_size > T20_UBOOT_MAX) {
		t20_spl_puts("SPL: U-Boot image size out of range\n");
		return;
	}
	if (ih_load < T20_DRAM_BASE ||
	    ih_load >= T20_DRAM_BASE + T20_DRAM_MAX) {
		t20_spl_puts("SPL: U-Boot load addr out of range\n");
		return;
	}

	/* Header + compressed payload into DRAM scratch. */
	sfc_nor_load(T20_UBOOT_OFFSET, T20_IH_HDR_LEN + ih_size,
		     T20_UBOOT_SCRATCH);

	/*
	 * The lean SPL has no heap until here; DRAM is up. This custom
	 * SPL never calls spl_init(), so set GD_FLG_FULL_MALLOC_INIT
	 * ourselves or malloc() stays on the tiny malloc_simple arena
	 * and the LZMA dictionary allocation fails (SZ_ERROR_MEM).
	 */
	mem_malloc_init(T20_SPL_HEAP_BASE, T20_SPL_HEAP_SIZE);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;

	out_len = T20_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress((unsigned char *)(uintptr_t)ih_load,
				       &out_len, scratch + T20_IH_HDR_LEN,
				       (SizeT)ih_size);
	if (ret) {
		char m[2] = { (char)('0' + (ret & 7)), 0 };
		t20_spl_puts("SPL: U-Boot LZMA decompress failed, SZ_ERROR=");
		t20_spl_puts(m);
		t20_spl_puts("\n");
		return;
	}

	/* Jump to the load address (== entry for a -T standalone image). */
	((void (*)(void))(uintptr_t)ih_load)();
}
