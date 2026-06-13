// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T21 SPL SFC clock + controller bring-up, plus a minimal
 * polled NOR read.
 *
 * After PLL + DDR init, the T21 SPL hands U-Boot loading to the
 * standard SPL_SPI framework (NOR cold-boot, via board_init_r) or
 * returns to the mask ROM (USB-boot). Both need the SFC clock derived
 * off MPLL and the controller GLB/DEV_CONF programmed before the DM
 * SFC driver (drivers/spi/ingenic_sfc.c, compatible ingenic,t31-sfc)
 * touches the flash.
 *
 * T21 clocks the SFC off the SSI clock divider (CPM_SSICDR), exactly
 * like T31/T23 (same SFC block, same SSICDR). The proven vendor
 * ssi_clk_set_rate() is kept verbatim (it does not touch the SSIPCS
 * source-select or the T21 SSISCS bit - the bootrom leaves them
 * NOR-boot-usable, which is how every boot of this board has run).
 *
 * t21_spl_nor_read() is a pre-driver-model polled NOR read (vendor
 * sfc_read transliteration). The T21 SPL boots cache-as-RAM and must
 * make itself DRAM-resident once DDR is up; the bootrom-loaded cache
 * copy of the image is NOT trustworthy by then (cold clean lines may
 * already have been evicted - discarded - by pre-DDR stack/data cache
 * pressure), so soc.c re-reads the pristine image straight from NOR
 * into DRAM instead of copying it out of the cache.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/io.h>
#include <mach/t21.h>
#include <mach/t21-sfc.h>

/*
 * SSI cgu entry from the vendor cgu_clk_sel[] table (clk.c):
 *   [SSI] = {1, CPM_SSICDR, 30, MPLL, {APLL, MPLL, VPLL}, 28, 27, 26}
 * ce = bit 28, busy = bit 27, stop = bit 26.
 */
#define CPM_SSICDR	0x74
#define SSI_CGU_CE	28
#define SSI_CGU_BUSY	27
#define SSI_CGU_STOP	26

/*
 * SSI/SFC clock target (vendor sfc_init(): clk_set_rate(SSI, 70M)).
 * The divider is computed against the LARGEST per-SKU MPLL (HP's
 * 1000 MHz); on the 900 MHz T21N the same divider just comes out
 * slightly slower, never faster - safe.
 */
#define T21_MPLL_RATE	1000000000U
#define T21_SSI_RATE	70000000U

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
 * SSI branch of the vendor clk_set_rate() (clk.c). SSI is not MSC0/MSC1
 * and not DDR, so cdr = (pll_rate/rate - 1) & 0xff, with pll_rate
 * rounded to a multiple of rate exactly as the vendor does.
 */
static void ssi_clk_set_rate(void)
{
	unsigned int pll_rate = T21_MPLL_RATE;
	unsigned int rate = T21_SSI_RATE;
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

/*
 * Bring up the SFC clock + controller for the DM SPI driver. Called
 * from board_init_f before board_init_r runs the SPL_SPI load (NOR
 * cold-boot) or before returning to the mask ROM (USB-boot). The DM SFC
 * driver expects the clock derived and the GLB threshold / DEV_CONF
 * line-enable delays already programmed.
 */
void t21_spl_sfc_clk_init(void)
{
	ssi_clk_set_rate();

	jz_sfc_writel(THRESHOLD << THRESHOLD_OFFSET, SFC_GLB);
	jz_sfc_writel(CEDL | HOLDDL | WPDL, SFC_DEV_CONF);

	/* low power consumption */
	jz_sfc_writel(0, SFC_CGE);
}

/*
 * Minimal polled NOR read (vendor sfc_read transliteration): basic
 * 1-1-1 READ (0x03), 3-byte address, FIFO polling, no DMA/IRQ. Word
 * (u32) granularity.
 */
static void sfc_set_read_reg(unsigned int cmd, unsigned int addr,
			     unsigned int addr_len)
{
	unsigned int tmp;

	tmp = jz_sfc_readl(SFC_GLB);
	tmp &= ~PHASE_NUM_MSK;
	tmp |= (0x1 << PHASE_NUM_OFFSET);
	jz_sfc_writel(tmp, SFC_GLB);

	tmp = (addr_len << ADDR_WIDTH_OFFSET) | CMD_EN | DATEEN |
	      (cmd << CMD_OFFSET);
	jz_sfc_writel(tmp, SFC_TRAN_CONF(0));
	jz_sfc_writel(addr, SFC_DEV_ADDR(0));
	jz_sfc_writel(0, SFC_DEV_ADDR_PLUS(0));

	/* zero dummy bits, standard single-line SPI mode */
	tmp = jz_sfc_readl(SFC_TRAN_CONF(0));
	tmp &= ~TRAN_CONF_DMYBITS_MSK;
	jz_sfc_writel(tmp, SFC_TRAN_CONF(0));
	tmp = jz_sfc_readl(SFC_TRAN_CONF(0));
	tmp &= ~TRAN_MODE_MSK;
	jz_sfc_writel(tmp, SFC_TRAN_CONF(0));

	jz_sfc_writel(START, SFC_TRIG);
}

static void sfc_read_data(unsigned int *data, unsigned int words)
{
	unsigned int tmp_len = 0;
	unsigned int fifo_num;
	unsigned int i;

	while (tmp_len < words) {
		if (!(jz_sfc_readl(SFC_SR) & RECE_REQ))
			continue;

		jz_sfc_writel(CLR_RREQ, SFC_SCR);
		if ((words - tmp_len) > THRESHOLD)
			fifo_num = THRESHOLD;
		else
			fifo_num = words - tmp_len;

		for (i = 0; i < fifo_num; i++) {
			*data = jz_sfc_readl(SFC_DR);
			data++;
			tmp_len++;
		}
	}

	while ((jz_sfc_readl(SFC_SR) & SFC_SR_END) != SFC_SR_END)
		;
	jz_sfc_writel(CLR_END, SFC_SCR);
}

/*
 * Read `bytes` (rounded up to a whole word) from NOR offset `nor_off`
 * to `dst`. Self-contained: derives the SFC clock and programs the
 * controller, so it works before driver model is up. `dst` should be
 * a KSEG1 (uncached) pointer when the point is to populate DRAM
 * without disturbing the cache (the cache-as-RAM reload).
 */
void t21_spl_nor_read(unsigned int nor_off, unsigned int *dst,
		      unsigned int bytes)
{
	unsigned int words = (bytes + 3) / 4;

	t21_spl_sfc_clk_init();

	jz_sfc_writel(STOP, SFC_TRIG);
	jz_sfc_writel(FLUSH, SFC_TRIG);

	jz_sfc_writel(words * 4, SFC_TRAN_LEN);
	sfc_set_read_reg(CMD_READ, nor_off, 3);
	sfc_read_data(dst, words);
}

/*
 * The T20-generation mask ROM (which T21 carries, gen 1.5) loads only
 * the first 0x6800 bytes of the SPL payload from NOR - sized for the
 * L2-less T20's 32 KB cache - regardless of the header length (proven
 * on T21N silicon: image bytes beyond 0x80001000+0x6800 read back
 * zero). The old sub-26 KB imperative SPL fit; the DM-in-SPL image
 * (with the appended DTB at its very end) does not.
 *
 * Complete the load ourselves: read the missing tail from NOR into
 * CACHED memory at its link address. The bootrom enables the 64 KB L2
 * at reset on every boot path (Config7 |= 2 at 0xbfc00018), so the
 * full ~80 KB cache-as-RAM budget is live and the tail's write-
 * allocated lines fit alongside the resident head. The bootrom's own
 * SFC clock is still programmed (it just loaded the head with it), so
 * no clock change is needed - and must not be made, since pll_init()
 * has not run yet.
 *
 * Must run BEFORE anything touches the missing region (fdtdec_setup
 * reads the appended DTB). This function and everything it calls live
 * in the loaded head (mach objects link first; asserted at build time
 * in soc.c via T21_ROM_SPL_LOAD).
 */
void t21_spl_self_complete(unsigned int image_end)
{
	/*
	 * The 2 KiB boot header is part of the linked image (start.S
	 * emits it at SPL_TEXT_BASE; entry is at +0x800), so flash file
	 * offset k maps to memory SPL_TEXT_BASE + k. The mask ROM
	 * loaded T21_ROM_SPL_LOAD bytes total from file offset 0
	 * (header included) - proven by the on-target probe: the first
	 * zero byte is exactly SPL_TEXT_BASE + T21_ROM_SPL_LOAD.
	 */
	unsigned int loaded = CONFIG_SPL_TEXT_BASE + T21_ROM_SPL_LOAD;
	unsigned int words;

	if (image_end <= loaded)
		return;

	words = (image_end - loaded + 3) / 4;

	/*
	 * Do NOT reprogram GLB/DEV_CONF or the clock here: the bootrom
	 * just used this controller+clock to load the head, its GLB
	 * threshold matches the same 31-word drain protocol (verified
	 * in the bfc014e4 sfc_read disassembly), and reprogramming GLB
	 * mid-state wedges the next transfer. Just clear stale status
	 * (the bootrom does the same before each transfer) and go.
	 */
	jz_sfc_writel(0x1f, SFC_SCR);

	jz_sfc_writel(STOP, SFC_TRIG);
	jz_sfc_writel(FLUSH, SFC_TRIG);

	jz_sfc_writel(words * 4, SFC_TRAN_LEN);
	sfc_set_read_reg(CMD_READ, T21_ROM_SPL_LOAD, 3);
	sfc_read_data((unsigned int *)loaded, words);
}
