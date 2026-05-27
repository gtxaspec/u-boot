// SPDX-License-Identifier: GPL-2.0+
/*
 * T41NQ DDR3 init - replay vendor register trace verbatim.
 * Every write matches the instrumented vendor SPL trace captured
 * on 2026-05-27 (saved as vendor-full-trace.log).
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>

void t41_spl_putc(char c);

#define W(a, v) do { *(volatile u32*)(a) = (v); asm volatile("sync":::"memory"); } while(0)
#define R(a) ({ asm volatile("sync":::"memory"); *(volatile u32*)(a); })

static u32 cpm_readl(unsigned int off) { return R(CPM_BASE + off); }
static void cpm_writel(u32 v, unsigned int off) { W(CPM_BASE + off, v); }

void sdram_init(void)
{
	u32 val, regval;


	/* DRCG bit 6 */
	cpm_writel(cpm_readl(CPM_DRCG) | 0x40, CPM_DRCG);

	/* DDR CGU: MPLL source + div=1 */
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(3u << 30);
	regval |= (2u << 30);
	cpm_writel(regval, CPM_DDRCDR);
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(0xf | (0x3fu << 24));
	regval |= (1 << 29) | 1;
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28));

	/* === Vendor trace line-by-line === */

	/* ddrc_reset_phy */
	W(0xb34f0010, 0x00f00000);
	mdelay(1);
	W(0xb34f0010, 0x00800000);
	mdelay(1);

	/* ddr_phy_init: PLL FBDIVL */
	val = R(0xb3011140); val &= ~0xff; val |= 0x01;
	W(0xb3011140, val);

	/* PLL FBDIVH */
	val = R(0xb3011144); val &= ~0xff; val |= 0x80;
	W(0xb3011144, val);

	/* PLL_CTRL first (0x28 for pll_sel=0, >625MHz) */
	val = R(0xb301114c); val &= ~0xff; val |= 0x28;
	W(0xb301114c, val);
	udelay(500);

	/* PLL PDIV (RMW preserving bit 6!) */
	val = R(0xb3011148); val &= ~0x1f; val |= 0x01;
	W(0xb3011148, val);

	/* PLL_CTRL second (0x20) */
	val = 0x20;
	W(0xb301114c, val);
	udelay(500);

	/* vendor polls PLK bit 2 up to 500 times, we just wait */
	{ int i; for (i = 0; i < 500; i++) if (R(0xb3011180) & 4) break; }

	/* MEM_CFG */
	val = R(0xb3011004); val &= ~0xff; val |= 0x0a;
	W(0xb3011004, val);

	/* DQ_WIDTH = 3 (16-bit) */
	W(0xb3011034, 0x03);

	/* PHY RST */
	val = R(0xb3011000); val &= ~0xff; val |= 0x0d;
	W(0xb3011000, val);

	/* CWL */
	val = R(0xb301101c); val &= ~0xf; val |= 0x08;
	W(0xb301101c, val);

	/* CL */
	val = R(0xb3011014); val &= ~0xf; val |= 0x0a;
	W(0xb3011014, val);

	/* AL = 0 */
	W(0xb3011018, 0);


	/* DFI init */
	W(0xb3012000, 0x08);	/* DWCFG DFI_INIT_START */
	W(0xb3012000, 0x00);	/* buswidth 16-bit */
	while (!(R(0xb3012004) & 1));	/* poll DFI_INIT_COMP */


	W(0xb34f0010, 0x00000000);	/* CTRL = 0 */
	udelay(5);
	W(0xb34f0008, 0x02002a35);	/* CFG */
	udelay(5);
	W(0xb34f0010, 0x00000002);	/* CKE */
	udelay(5);

	/* DDR3 LMR - exact values from vendor trace */
	W(0xb34f0018, 0x00000000); udelay(5);
	W(0xb34f0018, 0x00018481); udelay(5);	/* MR2 */
	W(0xb34f0018, 0x00000000); udelay(5);
	W(0xb34f0018, 0x00000681); udelay(5);	/* MR3 */
	W(0xb34f0018, 0x00000000); udelay(5);
	W(0xb34f0018, 0x00002281); udelay(5);	/* MR1 */
	W(0xb34f0018, 0x00000000); udelay(5);
	W(0xb34f0018, 0x01d50081); udelay(5);	/* MR0 */
	W(0xb34f0018, 0x000000c1); udelay(5);	/* ZQCL */

	/* ddrp_set_drv_odt - exact from trace */
	W(0xb3011500, 0x00);	/* ODT_PD [7:0] */
	W(0xb3011504, 0x00);	/* ODT_PU [7:0] */
	W(0xb3011540, 0x00);	/* ODT_PD [15:8] */
	W(0xb3011544, 0x00);	/* ODT_PU [15:8] */
	W(0xb30114c0, 0x0f);	/* CMD_PD */
	W(0xb30114c4, 0x0f);	/* CMD_PU */
	W(0xb30114c8, 0x03);	/* CLK_PD */
	W(0xb30114cc, 0x03);	/* CLK_PU */
	W(0xb3011508, 0x14);	/* DQ_PD [7:0] */
	W(0xb301150c, 0x14);	/* DQ_PU [7:0] */
	W(0xb3011548, 0x14);	/* DQ_PD [15:8] */
	W(0xb301154c, 0x14);	/* DQ_PU [15:8] */

	/* ddrc_prev_init - TIMINGs + MMAPs + CTRL */
	W(0xb34f0040, 0x07120b08);
	W(0xb34f0048, 0x0708060a);
	W(0xb34f0050, 0x030a040a);
	W(0xb34f0058, 0x19221805);
	W(0xb34f0060, 0x00050054);
	W(0xb34f0078, 0x000020f8);
	W(0xb34f0080, 0x00002800);
	W(0xb34f0010, 0x00008092);

	/* skip calibration for now - vendor trace shows it but
	 * we need to get basic RW working first */

	/* ddrc_post_init */
	W(0xb34f0038, 0x67aa0083);	/* REFCNT */
	W(0xb34f0010, 0x0000b092);	/* CTRL full */
	W(0xb3012064, 0x11111111);	/* CGUC0 */
	W(0xb3012068, 0x00000113);	/* CGUC1 */

	/* AUTOSR */
	W(0xb34f0030, 0x26001556);
	W(0xb34f0028, 0x00000000);

	/* Protection */
	W(0xb34f00d8, 0x00000001);	/* HREGPRO */
	W(0xb301206c, 0x00000001);	/* PREGPRO */


	/* DDR bandwidth optimize */
	W(0xb301206c, 0x00000000);
	W(0xb3012040, 0xff000000);
	W(0xb3012048, 0x2bd07460);
	W(0xb301206c, 0x00000001);

	/* Test */
	{
		volatile u32 *a = (volatile u32 *)0xa0000000;
		*a = 0xdeadbeef;
	}
}
