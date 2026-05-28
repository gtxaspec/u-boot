// SPDX-License-Identifier: GPL-2.0+
/*
 * T41 SPL DRAM bring-up.
 *
 * Two paths, picked at Kconfig time:
 *
 *   - CONFIG_T41_SDRAM_INNOPHY=y selects the vendor-derived UCLASS_RAM
 *     driver in drivers/ram/ingenic/. Runs at the vendor target DDR
 *     rate with HW DQS calibration on. The chosen T41 variant struct
 *     (per CONFIG_T41_VARIANT_*) is passed through the non-DM
 *     ingenic_ddr_init() helper.
 *
 *   - CONFIG_T41_SDRAM_INNOPHY=n keeps the legacy hand-coded init
 *     below, transliterated from the cloner SPL's sdram_init function
 *     at 0x80002a54 (46 MMIO writes). Runs DDR at MPLL/3 ~= 466 MHz
 *     (cloner underclocks vs the vendor 700 MHz target). HW-validated
 *     on lab T41NQ.
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>

#ifdef CONFIG_T41_SDRAM_INNOPHY

#include "../../../../drivers/ram/ingenic/ddr_innophy.h"

/* Per-variant struct pointer, selected at compile time by the
 * CONFIG_T41_VARIANT_* Kconfig choice. SPL uses this to drive the
 * non-DM ingenic_ddr_init() helper - our T41 SPL has a custom
 * board_init_f that runs as the SPL "main" and never goes through
 * board_init_r, so DM is not initialized when sdram_init() fires.
 * U-Boot proper still uses the UCLASS_RAM driver via DT compatible
 * match (in ddr_innophy.c). */
static const struct ingenic_ddr_variant *t41_pick_variant(void)
{
#if defined(CONFIG_T41_VARIANT_T41A)
	return &ingenic_ddr_variant_t41a;
#elif defined(CONFIG_T41_VARIANT_T41L)
	return &ingenic_ddr_variant_t41l;
#elif defined(CONFIG_T41_VARIANT_T41LQ)
	return &ingenic_ddr_variant_t41lq;
#elif defined(CONFIG_T41_VARIANT_T41N)
	return &ingenic_ddr_variant_t41n;
#elif defined(CONFIG_T41_VARIANT_T41XQ)
	return &ingenic_ddr_variant_t41xq;
#elif defined(CONFIG_T41_VARIANT_T41ZG)
	return &ingenic_ddr_variant_t41zg;
#elif defined(CONFIG_T41_VARIANT_T41ZGC)
	return &ingenic_ddr_variant_t41zgc;
#elif defined(CONFIG_T41_VARIANT_T41ZL)
	return &ingenic_ddr_variant_t41zl;
#elif defined(CONFIG_T41_VARIANT_T41ZM)
	return &ingenic_ddr_variant_t41zm;
#elif defined(CONFIG_T41_VARIANT_T41ZMC)
	return &ingenic_ddr_variant_t41zmc;
#elif defined(CONFIG_T41_VARIANT_T41ZN)
	return &ingenic_ddr_variant_t41zn;
#elif defined(CONFIG_T41_VARIANT_T41ZX)
	return &ingenic_ddr_variant_t41zx;
#else /* default T41_VARIANT_T41NQ */
	return &ingenic_ddr_variant_t41nq;
#endif
}

void sdram_init(void)
{
	const struct ingenic_ddr_variant *cfg = t41_pick_variant();
	u32 cdr;

	/* DDR CGU: MPLL source + divider so the controller gets clock
	 * at the variant's ddr_hz. Matches vendor clk_set_rate(DDR,
	 * ddrfreq): cdr = mpll/ddr - 1. The Innophy PHY then has its
	 * own PLL programmed by ddr_phy_init() that takes this CGU
	 * output as input. */
	cdr = (cfg->mpll_hz / cfg->ddr_hz) - 1;

	{
		u32 r = readl((void __iomem *)(CPM_BASE + CPM_DDRCDR));
		r &= ~(3u << 30);
		r |= (2u << 30);		/* source = MPLL */
		writel(r, (void __iomem *)(CPM_BASE + CPM_DDRCDR));
		r = readl((void __iomem *)(CPM_BASE + CPM_DDRCDR));
		r &= ~(0xf | (0x3fu << 24));
		r |= (1u << 29) | (cdr & 0xf);	/* CE | divider */
		writel(r, (void __iomem *)(CPM_BASE + CPM_DDRCDR));
		while (readl((void __iomem *)(CPM_BASE + CPM_DDRCDR)) & (1u << 28))
			;
	}

	ingenic_ddr_init(cfg, (void __iomem *)0xb34f0000);
	/* SPL has no real error path. The serial helper in soc.c prints
	 * 'DDR verify FAILED' or 'DDR OK' after we return based on a
	 * stack-allocated DRAM verify. */
}

#else /* legacy hand-coded cloner sequence */

#define W(a, v) do { *(volatile u32*)(a) = (v); asm volatile("sync":::"memory"); } while(0)
#define R(a) ({ asm volatile("sync":::"memory"); *(volatile u32*)(a); })

static u32 cpm_readl(unsigned int off) { return R(CPM_BASE + off); }
static void cpm_writel(u32 v, unsigned int off) { W(CPM_BASE + off, v); }

void sdram_init(void)
{
	u32 val, regval;

	/* DDR CGU: MPLL source, div_field=2 -> divider=(field+1)=3,
	 * gives DDR = MPLL(1400) / 3 ~= 466 MHz. Cloner-derived timings
	 * are calibrated for 400 MHz; 466 MHz works because the cloner
	 * underclocks the chip vs the vendor 700 MHz target. */
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(3u << 30);
	regval |= (2u << 30);
	cpm_writel(regval, CPM_DDRCDR);
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(0xf | (0x3fu << 24));
	regval |= (1 << 29) | 2;
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28));

	/* #1-2: ddrc_reset_phy */
	W(0xb34f0010, 0x00f00000);
	mdelay(1);
	W(0xb34f0010, 0x00800000);
	mdelay(1);

	/* #3-13: ddr_phy_init */
	val = R(0xb3011140); val &= ~0xff; val |= 0x01;
	W(0xb3011140, val);					/* #3 PLL_FBDIVL */

	val = R(0xb3011144); val &= ~0xff; val |= 0x80;
	W(0xb3011144, val);					/* #4 PLL_FBDIVH */

	val = R(0xb301114c); val &= ~0xff; val |= 0x28;
	W(0xb301114c, val);					/* #5 PLL_CTRL (pre) */

	val = R(0xb3011148); val &= ~0x1f; val |= 0x01;
	W(0xb3011148, val);					/* #6 PLL_PDIV */

	W(0xb301114c, 0x20);					/* #7 PLL_CTRL (enable) */
	{ int i; for (i = 0; i < 500; i++)
		if (R(0xb3011180) & 4) break; }			/* poll PLL lock */

	val = R(0xb3011004); val &= ~0xff; val |= 0x0a;
	W(0xb3011004, val);					/* #8 MEM_CFG */

	W(0xb3011034, 0x03);					/* #9 DQ_WIDTH (16-bit) */

	val = R(0xb3011000); val &= ~0xff; val |= 0x0d;
	W(0xb3011000, val);					/* #10 PHY_RST */

	val = R(0xb301101c); val &= ~0xf; val |= 0x05;
	W(0xb301101c, val);					/* #11 CWL=5 */

	val = R(0xb3011014); val &= ~0xf; val |= 0x06;
	W(0xb3011014, val);					/* #12 CL=6 */

	W(0xb3011018, 0);					/* #13 AL=0 */

	/* #14-20: timing + MMAP (BEFORE DFI init - cloner order) */
	W(0xb34f0040, 0x040d0605);				/* #14 TIMING1 */
	W(0xb34f0048, 0x03070406);				/* #15 TIMING2 */
	W(0xb34f0050, 0x03060406);				/* #16 TIMING3 */
	W(0xb34f0058, 0x0e140e04);				/* #17 TIMING4 */
	W(0xb34f0060, 0x00040033);				/* #18 TIMING5 */
	/* MMAP encoding: bits[15:8] = chip base (addr >> 24), bits[7:0] = mask.
	 * MMAP0 base 0x20 (DDR_MEM_PHY_BASE >> 24), mask 0xf8 = 8 bits free
	 * = 8 x 16MiB = 128 MiB visible on CS0. The cloner runs at 64 MiB
	 * with mask 0xfc; W631GU6NG is a 1 Gbit (128 MiB) DDR3 chip and the
	 * full chip is visible with mask 0xf8. */
	W(0xb34f0078, 0x000020f8);				/* #19 MMAP0 (128 MiB CS0) */
	W(0xb34f0080, 0x00002800);				/* #20 MMAP1 (no CS1) */

	/* #21-25: DDR3 address remap (cloner blob values for T41NQ + W631GU6NG) */
	W(0xb3012008, 0x03020d0c);				/* #21 REMAP1 */
	W(0xb301200c, 0x07060504);				/* #22 REMAP2 */
	W(0xb3012010, 0x0b0a0908);				/* #23 REMAP3 */
	W(0xb3012014, 0x0f0e0100);				/* #24 REMAP4 */
	W(0xb3012018, 0x13121110);				/* #25 REMAP5 */

	/* #26-28: clear pre-init */
	W(0xb34f0028, 0x00000000);				/* #26 AUTOSR_EN=0 */
	W(0xb34f0030, 0x00000000);				/* #27 AUTOSR_CNT=0 */
	W(0xb34f0038, 0x00000000);				/* #28 REFCNT=0 */

	/* #29-33: DFI init + CFG + CKE */
	W(0xb3012000, 0x08);					/* #29 DFI_INIT_START */
	W(0xb3012000, 0x00);					/* #30 buswidth 16-bit */
	while (!(R(0xb3012004) & 1));				/* poll DFI_INIT_COMP */

	W(0xb34f0010, 0x00000000);				/* #31 CTRL=0 */
	udelay(5);
	W(0xb34f0008, 0x80002831);				/* #32 CFG */
	udelay(5);
	W(0xb34f0010, 0x00000002);				/* #33 CKE */
	udelay(5);

	/* #34-37: DDR3 LMR (cloner values, no dummies, no ZQCL) */
	mdelay(1);
	W(0xb34f0018, 0x00000481);				/* #34 MR2 (CWL=5) */
	udelay(10);
	W(0xb34f0018, 0x00000681);				/* #35 MR3 */
	udelay(10);
	W(0xb34f0018, 0x00060281);				/* #36 MR1 (DRV=40R, RTT=120R) */
	udelay(10);
	W(0xb34f0018, 0x00520081);				/* #37 MR0 (CL=6, WR=6) */
	udelay(10);

	/* #38-40: HW calibration + D-cache flush to avoid corrupting
	 * bootrom's cached state. The PHY does internal DDR reads
	 * during calibration which can evict bootrom D-cache lines. */
	W(0xb3011008, 0x00);
	W(0xb3011008, 0x01);
	{ int i; for (i = 0; i < 1000000; i++)
		if ((R(0xb3011184) & 0xf) == 0x3) break; }
	W(0xb3011008, 0x00);
	/* Hit-invalidate D-cache for bootrom scratchpad (0x80000000-0x200)
	 * WITHOUT writeback, to avoid flushing SRAM data to DDR.
	 * Also invalidate SPL code range to prevent stale writebacks. */
	{ unsigned long a;
	  for (a = 0x80000000; a < 0x80000200; a += 32)
		__asm__ volatile("cache 0x11, 0(%0)" : : "r"(a));
	  for (a = 0x80001000; a < 0x80008000; a += 32)
		__asm__ volatile("cache 0x11, 0(%0)" : : "r"(a));
	  __asm__ volatile("sync"); }

	/* #41-43: post-init */
	W(0xb34f0030, 0x16000101);				/* #41 AUTOSR_CNT */
	W(0xb34f0038, 0x40c30081);				/* #42 REFCNT */
	{							/* #43 CTRL merge */
		u32 ctrl = R(0xb34f0010);
		ctrl = (0x0000b092 & 0xf000) | (ctrl & ~0xf000);
		W(0xb34f0010, ctrl);
	}

	/* #44-46: final */
	W(0xb3012064, 0x11111111);				/* #44 CGUC0 */
	W(0xb3012068, 0x00000113);				/* #45 CGUC1 */
	W(0xb34f0028, 0x00000000);				/* #46 AUTOSR_EN */

}

#endif /* CONFIG_T41_SDRAM_INNOPHY */
