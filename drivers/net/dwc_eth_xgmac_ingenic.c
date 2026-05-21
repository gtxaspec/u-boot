// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic A1 glue for the Synopsys DWC_ether_xgmac driver.
 *
 * The A1 "XGMAC" is a Synopsys DWC_ether_xgmac core (v2.10). The
 * generic XGMAC bring-up - DMA reset, descriptors, MAC/MTL config,
 * MDIO - is done by dwc_eth_xgmac.c. The A1 only needs SoC glue:
 *
 *  - enable the GMAC CGU clock and set the RMII/RGMII reference rate;
 *  - program the SoC MAC PHY-interface mode register (RMII vs RGMII),
 *    whose address comes from the "ingenic,mode-reg" DT property;
 *  - pulse the external PHY out of reset via its reset GPIO.
 */

#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <net.h>
#include <reset.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm/device_compat.h>
#include "dwc_eth_xgmac.h"

/* CPM MACxPHYC interface-mode register: low 2 bits 1 = RGMII, 2 = RMII. */
#define A1_XGMAC_MODE_MASK	0x3
#define A1_XGMAC_MODE_RGMII	1
#define A1_XGMAC_MODE_RMII	2

/*
 * The GMAC needs three CGU dividers running, all sourced from EPLL,
 * or its register/MDIO block is unresponsive. clk-a1 drives MAC0CDR
 * via the DT clock; MAC0TXCDR and MAC0PTPCDR are poked here. RMII
 * rates: MAC0CDR 50 MHz refclk, MAC0TXCDR 25 MHz, MAC0PTPCDR 50 MHz.
 */
#define A1_XGMAC_RMII_REFCLK	50000000
#define A1_XGMAC_TX_CLK		25000000
#define A1_XGMAC_PTP_CLK	50000000

#define A1_CPM_BASE		0xb0000000
#define A1_CPM_CPEPCR		0x18		/* EPLL config */
#define A1_CPM_MAC0TXCDR	0xc4
#define A1_CPM_MAC0PTPCDR	0xcc
#define A1_CDR_SRC_EPLL		(3u << 30)	/* CDR source select [31:30] */
#define A1_CDR_CE		BIT(29)		/* clock-change enable */
#define A1_CDR_BUSY		BIT(28)
#define A1_CDR_STOP		BIT(27)

static ulong a1_epll_rate(void)
{
	u32 v = readl((void __iomem *)(A1_CPM_BASE + A1_CPM_CPEPCR));
	u32 m = (v >> 20) & 0xfff;
	u32 n = (v >> 14) & 0x3f;
	u32 od1 = (v >> 11) & 0x7;
	u32 od0 = (v >> 8) & 0x7;

	if (!n)
		n = 1;
	if (!od1)
		od1 = 1;
	if (!od0)
		od0 = 1;

	return (ulong)((u64)24000000 * m / n / od1 / od0);
}

/* Program one EPLL-sourced GMAC CGU divider for the requested rate. */
static void a1_xgmac_set_cdr(u32 cdr_off, ulong rate)
{
	void __iomem *cdr = (void __iomem *)(A1_CPM_BASE + cdr_off);
	u32 div = a1_epll_rate() / rate;
	u32 v;

	if (!div)
		div = 1;
	if (div > 256)
		div = 256;

	v = readl(cdr);
	v &= ~(A1_CDR_SRC_EPLL | A1_CDR_BUSY | A1_CDR_STOP | 0xff);
	v |= A1_CDR_SRC_EPLL | A1_CDR_CE | (div - 1);
	writel(v, cdr);

	while (readl(cdr) & A1_CDR_BUSY)
		;
}

static int xgmac_probe_resources_a1(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	phy_interface_t interface;
	u32 mode_reg, mode_val;
	int ret;

	interface = xgmac->config->interface(dev);
	switch (interface) {
	case PHY_INTERFACE_MODE_RMII:
		mode_val = A1_XGMAC_MODE_RMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		mode_val = A1_XGMAC_MODE_RGMII;
		break;
	default:
		dev_err(dev, "unsupported phy-mode %d\n", interface);
		return -EINVAL;
	}

	/* Program the SoC MAC PHY-interface mode register (CPM MACxPHYC). */
	ret = dev_read_u32(dev, "ingenic,mode-reg", &mode_reg);
	if (ret) {
		dev_err(dev, "missing ingenic,mode-reg\n");
		return ret;
	}
	clrsetbits_le32((void __iomem *)(uintptr_t)mode_reg,
			A1_XGMAC_MODE_MASK, mode_val);

	ret = clk_get_by_index(dev, 0, &xgmac->clk_common);
	if (ret) {
		dev_err(dev, "failed to get GMAC clock: %d\n", ret);
		return ret;
	}

	return 0;
}

static int xgmac_start_clks_a1(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = clk_enable(&xgmac->clk_common);
	if (ret && ret != -ENOSYS && ret != -ENOTSUPP)
		return ret;

	/* MAC0CDR (the RMII refclk) via the DT clock; the other two direct. */
	clk_set_rate(&xgmac->clk_common, A1_XGMAC_RMII_REFCLK);
	a1_xgmac_set_cdr(A1_CPM_MAC0TXCDR, A1_XGMAC_TX_CLK);
	a1_xgmac_set_cdr(A1_CPM_MAC0PTPCDR, A1_XGMAC_PTP_CLK);

	return 0;
}

static int xgmac_stop_clks_a1(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	clk_disable(&xgmac->clk_common);

	return 0;
}

static int xgmac_start_resets_a1(struct udevice *dev)
{
	struct gpio_desc phy_reset;
	int ret;

	/*
	 * Pulse the external PHY out of reset. The GPIO carries its own
	 * active-low polarity in the DT, so logical 1 = asserted (held in
	 * reset). The clock is already running (xgmac_start_clks_a1), so
	 * the PHY has its reference clock for the post-reset boot.
	 */
	ret = gpio_request_by_name(dev, "snps,reset-gpio", 0, &phy_reset,
				   GPIOD_IS_OUT);
	if (ret)
		return 0;	/* no PHY reset GPIO - nothing to do */

	dm_gpio_set_value(&phy_reset, 1);
	mdelay(15);
	dm_gpio_set_value(&phy_reset, 0);
	mdelay(80);

	dm_gpio_free(dev, &phy_reset);

	return 0;
}

static int xgmac_get_enetaddr_a1(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_plat(dev);
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 hi_addr, lo_addr;

	lo_addr = readl(&xgmac->mac_regs->address0_low);
	hi_addr = readl(&xgmac->mac_regs->address0_high);

	pdata->enetaddr[0] = lo_addr & 0xff;
	pdata->enetaddr[1] = (lo_addr >> 8) & 0xff;
	pdata->enetaddr[2] = (lo_addr >> 16) & 0xff;
	pdata->enetaddr[3] = (lo_addr >> 24) & 0xff;
	pdata->enetaddr[4] = hi_addr & 0xff;
	pdata->enetaddr[5] = (hi_addr >> 8) & 0xff;

	/* Non-zero return -> caller falls back to the env / random MAC. */
	return !is_valid_ethaddr(pdata->enetaddr);
}

static struct xgmac_ops xgmac_a1_ops = {
	.xgmac_inval_desc = xgmac_inval_desc_generic,
	.xgmac_flush_desc = xgmac_flush_desc_generic,
	.xgmac_inval_buffer = xgmac_inval_buffer_generic,
	.xgmac_flush_buffer = xgmac_flush_buffer_generic,
	.xgmac_probe_resources = xgmac_probe_resources_a1,
	.xgmac_remove_resources = xgmac_null_ops,
	.xgmac_stop_resets = xgmac_null_ops,
	.xgmac_start_resets = xgmac_start_resets_a1,
	.xgmac_stop_clks = xgmac_stop_clks_a1,
	.xgmac_start_clks = xgmac_start_clks_a1,
	.xgmac_calibrate_pads = xgmac_null_ops,
	.xgmac_disable_calibration = xgmac_null_ops,
	.xgmac_get_enetaddr = xgmac_get_enetaddr_a1,
};

struct xgmac_config xgmac_a1_config = {
	.reg_access_always_ok = false,
	.swr_wait = 50,
	.config_mac = XGMAC_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB,
	.config_mac_mdio = XGMAC_MAC_MDIO_ADDRESS_CR_400_500,
	.axi_bus_width = XGMAC_AXI_WIDTH_64,
	.interface = dev_read_phy_mode,
	.ops = &xgmac_a1_ops,
};
