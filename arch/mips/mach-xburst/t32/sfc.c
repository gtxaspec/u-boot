// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SPL SPI-NOR loader: read U-Boot proper from the SFC2
 * NOR flash into DRAM and jump to it.
 *
 * Faithful transliteration of the vendor U-Boot 2022.10 SFC2 engine
 * (common/spl_ingenic/spl_sfc_common.c: sfc_init, sfc_set_reg,
 * sfc_read_data, sfc_execution) and the SFC0 branch of the vendor
 * _clk_set_rate() (arch/mips/mach-xburst/PRJ/clk.c). The vendor's
 * packed bitfield-union register writes are rendered as explicit
 * shifted-mask writes; values, order and poll loops are unchanged.
 *
 * The U-Boot-proper image format and the LZMA-decompress-and-jump
 * flow mirror the T31 loader exactly (64-byte legacy mkimage header
 * + LZMA payload at T32_UBOOT_OFFSET, decompress to ih_load, jump).
 * DRAM is already up (sdram_init() ran) so the load/scratch/heap
 * regions are valid.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <asm/global_data.h>
#include <lzma/LzmaTools.h>
#include <malloc.h>
#include <linux/string.h>
#include <mach/t32.h>
#include <mach/t32-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

u32 t32_pll_rate(unsigned int cpxpcr_off);
void t32_spl_puts(const char *s);

/*
 * Flash / DRAM layout (matches the binman NOR image and the T31
 * profile): SPL at flash 0, U-Boot proper (LZMA + 64-byte mkimage
 * header) at T32_UBOOT_OFFSET, decompressed to 0x80100000. 64 MB
 * DRAM at 0x80000000, so scratch/heap below are all disjoint.
 */
#define T32_UBOOT_OFFSET	0x10000
#define T32_IH_MAGIC		0x27051956
#define T32_IH_HDR_LEN		64
#define T32_UBOOT_SCRATCH	0x81000000
#define T32_SPL_HEAP_BASE	0x80a00000
#define T32_SPL_HEAP_SIZE	0x00200000
#define T32_UBOOT_MAX		0x00800000

static u32 cpm_r(unsigned int off)
{
	return readl((void __iomem *)(CPM_BASE + off));
}

static void cpm_w(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(CPM_BASE + off));
}

static u32 sfc_r(unsigned int off)
{
	return readl((void __iomem *)(SFC_BASE + off));
}

static void sfc_w(u32 val, unsigned int off)
{
	writel(val, (void __iomem *)(SFC_BASE + off));
}

/*
 * SFC0 branch of the vendor _clk_set_rate(): preserve the source
 * select [31:30] (left valid by the bootrom) and the [9:8] pre-
 * divide; reprogram only the divider + change-enable, then wait for
 * the busy bit to clear. src 0=APLL 1=MPLL 2=VPLL.
 */
static void sfc0_clk_set_rate(unsigned int rate)
{
	u32 regval = cpm_r(CPM_SFC0CDR);
	u32 src = regval >> 30;
	u32 pll = t32_pll_rate(src == 0 ? CPM_CPAPCR : CPM_CPMPCR);
	u32 ori_sel = 1u << ((regval >> 8) & 0x3);
	u32 cdr = (((pll + rate - 1) / rate) / ori_sel - 1) & 0xff;

	regval &= ~((3 << SFC0_CGU_STOP) | 0xff);
	regval |= (1 << SFC0_CGU_CE) | cdr;
	cpm_w(regval, CPM_SFC0CDR);
	while (cpm_r(CPM_SFC0CDR) & (1 << SFC0_CGU_BUSY))
		;
}

void sfc_init(void)
{
	u32 reg;
	int i;

	sfc0_clk_set_rate(SFC0_INIT_RATE);

	reg = sfc_r(SFC_GLB0);
	reg &= ~(GLB_TRAN_DIR | GLB_OP_MODE | GLB_THRESHOLD_MSK);
	reg |= GLB_WP_EN | (SFC_THRESHOLD << GLB_THRESHOLD_OFFSET);
	sfc_w(reg, SFC_GLB0);

	reg = sfc_r(SFC_DEV_CONF);
	reg |= DEV_CONF_CEDL | DEV_CONF_HOLDDL | DEV_CONF_WPDL;
	sfc_w(reg, SFC_DEV_CONF);

	for (i = 0; i < 6; i++) {
		sfc_w(sfc_r(SFC_TRAN_CONF0(i)) & ~TRAN_CONF0_FMAT,
		      SFC_TRAN_CONF0(i));
		sfc_w(sfc_r(SFC_TRAN_CONF1(i)) & ~TRAN_CONF1_TRAN_MODE_MSK,
		      SFC_TRAN_CONF1(i));
	}

	sfc_w(CLR_END | CLR_TREQ | CLR_RREQ | CLR_OVER | CLR_UNDER, SFC_SCR);
	sfc_w(sfc_r(SFC_INTC) | MASK_END | MASK_TREQ | MASK_RREQ |
		      MASK_OVER | MASK_UNDR, SFC_INTC);
	sfc_w(0, SFC_CGE);

	/* bootrom may leave the engine running - stop & flush the FIFO */
	sfc_w(TRIG_STOP, SFC_TRIG);
	sfc_w(TRIG_FLUSH, SFC_TRIG);
	sfc_w(0, SFC_TRAN_LEN);
}

static int sfc_wait_end(void)
{
	u32 timeout = 0xffff;

	while (!(sfc_r(SFC_SR) & SR_END)) {
		if (timeout-- == 0) {
			t32_spl_puts("SFC: wait end timeout\n");
			return -1;
		}
	}
	return 0;
}

static void sfc_set_reg(u32 cmd, u32 addr, u32 addr_len, u32 dummy_bits,
			u32 data_en, u32 data_len, u32 dir)
{
	u32 reg;

	sfc_w(data_len, SFC_TRAN_LEN);

	if (sfc_r(SFC_SR) & SR_BUSY_MSK) {
		sfc_w(TRIG_STOP, SFC_TRIG);
		sfc_wait_end();
	}
	sfc_w(CLR_END | CLR_TREQ | CLR_RREQ | CLR_OVER | CLR_UNDER, SFC_SCR);

	reg = sfc_r(SFC_GLB0);
	reg &= ~(GLB_PHASE_NUM_MSK | GLB_TRAN_DIR);
	reg |= (0x1 << GLB_PHASE_NUM_OFFSET) | (dir ? GLB_TRAN_DIR : 0);
	sfc_w(reg, SFC_GLB0);

	reg = sfc_r(SFC_TRAN_CONF0(0));
	reg &= ~(TRAN_CONF0_ADDR_WIDTH_MSK | TRAN_CONF0_DMYBITS_MSK |
		 TRAN_CONF0_CMD_MSK | TRAN_CONF0_FMAT | TRAN_CONF0_DATEEN);
	reg |= (addr_len << TRAN_CONF0_ADDR_WIDTH_OFFSET) |
	       (dummy_bits << TRAN_CONF0_DMYBITS_OFFSET) |
	       (cmd << TRAN_CONF0_CMD_OFFSET) | TRAN_CONF0_CMDEN |
	       (data_en ? TRAN_CONF0_DATEEN : 0);
	sfc_w(reg, SFC_TRAN_CONF0(0));

	sfc_w(addr, SFC_DEV_ADDR(0));
	sfc_w(0, SFC_DEV_ADDR_PLUS(0));
	sfc_w(TRIG_START, SFC_TRIG);
}

static int sfc_read_data(u8 *data, u32 len)
{
	u32 fifo_len = SFC_THRESHOLD * 4;
	u32 tmp_buf[SFC_THRESHOLD];
	u32 timeout = 0xffff;
	u32 read_len = len;
	u32 i, dl, fifo_num;

	while (len > 0) {
		if (!(sfc_r(SFC_SR) & SR_RECE_REQ)) {
			if (timeout-- == 0) {
				t32_spl_puts("SFC: wait RECE_REQ timeout\n");
				break;
			}
			continue;
		}
		timeout = 0xffff;

		sfc_w(CLR_RREQ, SFC_SCR);
		dl = (len >= fifo_len) ? fifo_len : len;
		fifo_num = (dl + 3) / 4;

		for (i = 0; i < fifo_num; i++)
			tmp_buf[i] = sfc_r(SFC_RM_DR);
		memcpy(data, tmp_buf, dl);
		data += dl;
		len -= dl;
	}

	return read_len - len;
}

static int sfc_execution(u32 cmd, u32 addr, u32 addr_len, u32 dummy_bits,
			 u32 data_len, u8 *data, u32 dir)
{
	u32 data_en = data_len ? 1 : 0;
	int ret;

	sfc_set_reg(cmd, addr, addr_len, dummy_bits, data_en, data_len, dir);

	if (data_en && dir == 0) {
		ret = sfc_read_data(data, data_len);
		if ((u32)ret != data_len) {
			t32_spl_puts("SFC: read length error\n");
			return -1;
		}
	}
	return sfc_wait_end();
}

static int sfc_nor_load(u32 addr, u32 len, u8 *dst)
{
	return sfc_execution(SFC_NOR_CMD_READ, addr, SFC_NOR_ADDR_LEN, 0,
			     len, dst, 0);
}

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
void t32_spl_load_uboot(void)
{
	u8 *scratch = (u8 *)T32_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	sfc_init();

	/* Legacy mkimage header (64 bytes, big-endian fields). */
	if (sfc_nor_load(T32_UBOOT_OFFSET, T32_IH_HDR_LEN, scratch) < 0) {
		t32_spl_puts("SPL: SFC header read failed\n");
		return;
	}
	ih_magic = hdr_be32(scratch + 0);
	ih_size  = hdr_be32(scratch + 12);
	ih_load  = hdr_be32(scratch + 16);
	if (ih_magic != T32_IH_MAGIC) {
		t32_spl_puts("SPL: bad U-Boot image magic\n");
		return;
	}

	/* Header + LZMA payload into DRAM scratch. */
	if (sfc_nor_load(T32_UBOOT_OFFSET, T32_IH_HDR_LEN + ih_size,
			 scratch) < 0) {
		t32_spl_puts("SPL: SFC image read failed\n");
		return;
	}

	/*
	 * Lean SPL: no heap until DRAM is up and we never call
	 * spl_init(), so arm the full malloc arena ourselves or the
	 * LZMA dictionary allocation fails.
	 */
	mem_malloc_init(T32_SPL_HEAP_BASE, T32_SPL_HEAP_SIZE);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;

	out_len = T32_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress((unsigned char *)(uintptr_t)ih_load,
				       &out_len, scratch + T32_IH_HDR_LEN,
				       (SizeT)ih_size);
	if (ret) {
		char m[2] = { (char)('0' + (ret & 7)), 0 };

		t32_spl_puts("SPL: U-Boot LZMA decompress failed, SZ_ERROR=");
		t32_spl_puts(m);
		t32_spl_puts("\n");
		return;
	}

	((void (*)(void))(uintptr_t)ih_load)();
}
