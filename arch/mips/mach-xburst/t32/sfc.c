// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T32 SPL SFC clock + controller bring-up.
 *
 * After PLL + DDR init (UCLASS_RAM driver), the T32 SPL hands U-Boot
 * loading to the standard SPL_SPI framework (NOR cold-boot, via
 * board_init_r) or returns to the mask ROM (USB-boot). Both need the
 * SFC clock derived off MPLL and the controller GLB0/DEV_CONF
 * programmed before the DM SFC driver (or U-Boot proper, on USB-boot)
 * touches the flash.
 *
 * T32 (SFC2) clocks the SFC off the dedicated SFC0 divider
 * (CPM_SFC0CDR), not the shared SSI divider T31 uses. t32_spl_nor_read()
 * is the bare-metal SFC2 0x03 read the TPL uses to load the DRAM-resident
 * SPL off NOR; the SPL itself loads U-Boot proper through the DM SFC
 * driver (drivers/spi/ingenic_sfc.c), so the old in-SPL LZMA loader is
 * gone.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <linux/string.h>
#include <mach/t32.h>
#include <mach/t32-sfc.h>

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

/*
 * Bring up the SFC clock + controller. Called from the TPL before
 * t32_spl_nor_read() loads the DRAM-resident SPL off NOR, and from the
 * DRAM-resident SPL's board_init_f before board_init_r runs the SPL_SPI
 * load (NOR cold-boot) or before returning to the mask ROM (USB-boot).
 * Derives the clock, programs the GLB0 threshold / DEV_CONF line-enable
 * delays, then resets every channel to a plain single-lane transfer with
 * clean status so the bare-metal read starts from the vendor's known
 * state (the DM SFC driver re-inits anyway).
 */
void t32_spl_sfc_clk_init(void)
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

	/* low power consumption */
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

/*
 * Bare-metal SFC2 0x03 NOR read - the TPL's nor_read hook. Loads the
 * DRAM-resident SPL from NOR into DRAM; t32_spl_sfc_clk_init() must have
 * run first. One CMD-READ transfer - SFC_TRAN_LEN is wide enough that the
 * whole SPL fits in a single execution.
 */
void t32_spl_nor_read(unsigned int nor_off, unsigned int *dst,
		      unsigned int bytes)
{
	sfc_execution(SFC_NOR_CMD_READ, nor_off, SFC_NOR_ADDR_LEN, 0,
		      bytes, (u8 *)dst, 0);
}
