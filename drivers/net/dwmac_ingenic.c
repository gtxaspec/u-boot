// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 T-series GMAC glue for the DesignWare MAC.
 *
 * Faithful port of the vendor dwmac_ingenic.c: it selects the PHY
 * interface/speed in the CPM GMAC-PHY-control register, brings the
 * PHY out of reset via a board GPIO, sets the MAC-PHY clock and then
 * chains to the mainline DesignWare core driver.
 *
 * The vendor calls _clk_set_rate(MACPHY, rate); mainline has no such
 * API, so program CPM_MACCDR directly using the same CE/BUSY sequence
 * that the (hardware-proven) MSC clock setup uses - and, like that
 * one, never clear CE afterwards or the clock dies on real silicon.
 */

#include <asm/io.h>
#include <dm.h>
#include <phy.h>
#include <malloc.h>
#include "designware.h"
#include <dm/device_compat.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <wait_bit.h>
#include <asm-generic/gpio.h>

/* CPM GMAC-PHY-control (CPM_GMACPHYC, our DT ingenic,mode-reg):
 * the vendor T31 net driver only selects the PHY interface here
 * (clear [2:0], RMII = 0x4) - it does NOT program any speed bits
 * (those are T33-era; "T31 delete"). Keep it faithful. */
#define INGENIC_ETH_SEL_MASK		0x7
#define INGENIC_ETH_SEL_RMII		0x4

/* CPM (0xb0000000): GMAC gate in CLKGR1, MAC clock divider MACCDR. */
#define T31_CPM_BASE			0xb0000000
#define T31_CPM_CLKGR1			0x28
#define T31_CPM_CLKGR1_GMAC		BIT(4)
#define T31_CPM_MACCDR			0x54
#define MACCDR_SRC_MPLL			(1u << 30)	/* {APLL,MPLL,VPLL} idx 1 */
#define MACCDR_CE			BIT(29)
#define MACCDR_BUSY			BIT(28)
#define MACCDR_STOP_SHIFT		27
#define MACCDR_DIV_MASK			0xffu
#define T31_MPLL_HZ			1200000000u	/* see mach-xburst t31 pll.c */

/*
 * GMAC RMII pins on GPIO port B, device function 0 (vendor
 * gpio_set_func(GPIO_PORT_B, GPIO_FUNC_0, 0x1EFC0)). Without this the
 * RMII REFCLK/data pins are not routed, the GMAC MAC clock domain has
 * no clock and the first MAC register access bus-stalls. Same FUNC_0
 * sequence as the UART/MSC pinmux (clear INT/MSK/PAT1/PAT0 + pulls).
 */
#define T31_GPIO_PORTB_BASE		0xb0011000
#define G_PXINTC			0x18
#define G_PXMSKC			0x28
#define G_PXPAT1C			0x38
#define G_PXPAT0C			0x48
#define G_PXPUENC			0x118
#define G_PXPDENC			0x128
#define T31_GMAC_PB_PINS		0x1EFC0

struct dwmac_ingenic_plat {
	struct dw_eth_pdata dw_eth_pdata;
	unsigned int macphy_rate;
	unsigned int max_speed;
	void __iomem *cpm_phyc_reg;
	struct gpio_desc reset_gpio;
};

static int dwmac_ingenic_of_to_plat(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	u32 cpm_reg;

	pdata->macphy_rate = dev_read_u32_default(dev, "macphy-rate", 50000000);
	pdata->max_speed = dev_read_u32_default(dev, "max-speed", 100);
	cpm_reg = dev_read_u32_default(dev, "ingenic,mode-reg", 0);
	if (!cpm_reg)
		return -EINVAL;
	pdata->cpm_phyc_reg = map_physmem(cpm_reg, 4, MAP_NOCACHE);

	return designware_eth_of_to_plat(dev);
}

static int t31_macphy_clk_init(struct dwmac_ingenic_plat *pdata)
{
	void __iomem *cpm = (void __iomem *)T31_CPM_BASE;
	u32 div, v;

	/* Ungate the GMAC functional clock. */
	clrbits_le32(cpm + T31_CPM_CLKGR1, T31_CPM_CLKGR1_GMAC);

	/*
	 * MAC-PHY clock from MPLL. Vendor clk_set_rate (non-MSC):
	 * cdr = pll/rate - 1. RMII wants 50 MHz -> 1200/50 - 1 = 23.
	 * Same CE/BUSY dance as the MSC clock; leave CE set.
	 *
	 * The poll is bounded: on a board where the MAC-PHY clock can't
	 * lock, an unbounded wait would hang U-Boot forever at "Net:".
	 * Time out instead so the boot continues without ethernet.
	 */
	div = (T31_MPLL_HZ / pdata->macphy_rate) - 1;
	v = readl(cpm + T31_CPM_MACCDR);
	v &= ~((3u << 30) | (3u << MACCDR_STOP_SHIFT) | MACCDR_DIV_MASK);
	v |= MACCDR_SRC_MPLL | MACCDR_CE | (div & MACCDR_DIV_MASK);
	writel(v, cpm + T31_CPM_MACCDR);

	return wait_for_bit_le32(cpm + T31_CPM_MACCDR, MACCDR_BUSY,
				 false, 100, false);
}

/* Mux the GMAC RMII pins to device function 0 on GPIO port B. */
static void t31_gmac_pinmux(void)
{
	void __iomem *gpb = (void __iomem *)T31_GPIO_PORTB_BASE;

	writel(T31_GMAC_PB_PINS, gpb + G_PXINTC);
	writel(T31_GMAC_PB_PINS, gpb + G_PXMSKC);
	writel(T31_GMAC_PB_PINS, gpb + G_PXPAT1C);
	writel(T31_GMAC_PB_PINS, gpb + G_PXPAT0C);
	writel(T31_GMAC_PB_PINS, gpb + G_PXPUENC);
	writel(T31_GMAC_PB_PINS, gpb + G_PXPDENC);
}

static int ingenic_gmac_init(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	struct eth_pdata *edata = &pdata->dw_eth_pdata.eth_pdata;
	u32 v;
	int ret;

	if (edata->phy_interface != PHY_INTERFACE_MODE_RMII) {
		dev_err(dev, "only RMII supported on T31 (mode %d)\n",
			edata->phy_interface);
		return -EINVAL;
	}

	/* Vendor jz_net_initialize order: clk_set_rate(MACPHY,50M),
	 * 50 ms settle, mux the RMII pins, then select RMII in the CPM
	 * GMAC-PHY-control register. */
	ret = t31_macphy_clk_init(pdata);
	if (ret) {
		dev_err(dev, "MAC-PHY clock did not lock (%d)\n", ret);
		return ret;
	}
	udelay(50000);

	t31_gmac_pinmux();

	v = readl(pdata->cpm_phyc_reg);
	v &= ~INGENIC_ETH_SEL_MASK;
	v |= INGENIC_ETH_SEL_RMII;
	writel(v, pdata->cpm_phyc_reg);

	return 0;
}

static int ingenic_gmac_setphy(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	int ret;

	ret = gpio_request_by_name(dev, "ingenic,reset-gpio", 0,
				   &pdata->reset_gpio, GPIOD_IS_OUT);
	if (ret == -ENOENT)
		return 0;	/* no PHY reset wired on this board */
	if (ret) {
		dev_err(dev, "request reset gpio failed: %d\n", ret);
		return ret;
	}

	/*
	 * Vendor IP101G reset: deassert, then assert for 50 ms, then
	 * deassert and wait 10 ms. DT carries the line polarity, so
	 * value 1 = asserted (in reset), 0 = released.
	 */
	dm_gpio_set_value(&pdata->reset_gpio, 0);
	mdelay(10);
	dm_gpio_set_value(&pdata->reset_gpio, 1);
	mdelay(50);
	dm_gpio_set_value(&pdata->reset_gpio, 0);
	mdelay(10);
	return 0;
}

static int dwmac_ingenic_probe(struct udevice *dev)
{
	int ret;

	ret = ingenic_gmac_init(dev);
	if (ret)
		return ret;

	ret = ingenic_gmac_setphy(dev);
	if (ret)
		return ret;

	return designware_eth_probe(dev);
}

static const struct udevice_id dwmac_ingenic_ids[] = {
	{ .compatible = "ingenic,t31-gmac" },
	{ }
};

U_BOOT_DRIVER(dwmac_ingenic) = {
	.name		= "dwmac_ingenic",
	.id		= UCLASS_ETH,
	.of_match	= dwmac_ingenic_ids,
	.of_to_plat	= dwmac_ingenic_of_to_plat,
	.probe		= dwmac_ingenic_probe,
	.ops		= &designware_eth_ops,
	.priv_auto	= sizeof(struct dw_eth_dev),
	.plat_auto	= sizeof(struct dwmac_ingenic_plat),
	.flags		= DM_FLAG_ALLOC_PRIV_DMA,
};
