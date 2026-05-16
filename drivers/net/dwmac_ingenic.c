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

#define INGENIC_ETH_SEL_MASK		0x7
#define INGENIC_ETH_SEL_RMII		0x4
#define INGENIC_ETH_SPEED_MASK		(0x3 << 29)
#define INGENIC_ETH_SPEED_10M		(0x2 << 29)
#define INGENIC_ETH_SPEED_100M		(0x3 << 29)

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

static int ingenic_gmac_init(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	struct eth_pdata *edata = &pdata->dw_eth_pdata.eth_pdata;
	u32 v;
	int ret;

	v = readl(pdata->cpm_phyc_reg);
	v &= ~(INGENIC_ETH_SEL_MASK | INGENIC_ETH_SPEED_MASK);

	switch (edata->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_RGMII:
		break;
	case PHY_INTERFACE_MODE_RMII:
		v |= INGENIC_ETH_SEL_RMII;
		break;
	default:
		dev_err(dev, "unsupported PHY mode %d\n", edata->phy_interface);
		return -EINVAL;
	}

	switch (pdata->max_speed) {
	case 10:
		v |= INGENIC_ETH_SPEED_10M;
		break;
	case 100:
		v |= INGENIC_ETH_SPEED_100M;
		break;
	default:
		dev_err(dev, "unsupported speed %u\n", pdata->max_speed);
		return -EINVAL;
	}

	writel(v, pdata->cpm_phyc_reg);

	ret = t31_macphy_clk_init(pdata);
	if (ret) {
		dev_err(dev, "MAC-PHY clock did not lock (%d)\n", ret);
		return ret;
	}
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

	/* Pulse reset then release; DT carries the line polarity. */
	dm_gpio_set_value(&pdata->reset_gpio, 1);
	mdelay(10);
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
