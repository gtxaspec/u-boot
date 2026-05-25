// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 T-series GMAC glue for the DesignWare MAC.
 *
 * Faithful port of the vendor net bring-up. Two PHY topologies:
 *
 *  - External RMII PHY (T31/T23, "ingenic,t31-gmac"): select RMII in
 *    the CPM GMAC-PHY-control register, mux the RMII pins, set the
 *    MAC-PHY clock to 50 MHz and pulse a board reset GPIO.
 *
 *  - Internal "OMNI" ePHY (T21, "ingenic,t21-gmac"): there is no
 *    external PHY chip and no reset GPIO. The embedded PHY (MDIO
 *    addr 0, ID 0x001c/0xc816) is powered and reset through the CPM
 *    GMAC-PHY-control register, runs the MAC<->ePHY link in MII mode
 *    and uses a 25 MHz MAC-PHY clock. This mirrors the vendor
 *    jz4775-9161.c PHY_TYPE_OMNI path (branch T21-1.0.33).
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
#define INGENIC_ETH_SEL_MII		0x0

/* Internal ("OMNI") ePHY control bits in the same CPM register. */
#define INGENIC_ETH_EPHY_BIT21		BIT(21)		/* vendor: set first */
#define INGENIC_ETH_EPHY_ENABLE		(0x3 << 22)	/* enable inner PHY */
#define INGENIC_ETH_EPHY_RESET		BIT(3)		/* soft-reset pulse */
#define INGENIC_ETH_EPHY_RSTSTAT	BIT(24)		/* reset-done status */

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

/*
 * Undocumented CPM word the vendor OMNI path writes before enabling
 * the embedded PHY (ePHY analog/clock seed). Not in any T21 CPM
 * header; replicated verbatim from vendor jz4775-9161.c.
 */
#define T21_CPM_EPHY_SEED_OFF		0x50
#define T21_CPM_EPHY_SEED_VAL		0xc8007016u

/* DesignWare MAC MDIO registers (struct eth_mac_regs offsets). */
#define MAC_MIIADDR			0x10
#define MAC_MIIDATA			0x14

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
#define G_PXPAT1S			0x34
#define G_PXPAT1C			0x38
#define G_PXPAT0C			0x48
#define G_PXPUENC			0x118
#define G_PXPDENC			0x128
#define T31_GMAC_PB_PINS		0x1EFC0
/* Inner-ePHY path: vendor OMNI muxes only PB7+PB15 to device FUNC_2
 * (PAT1=1,PAT0=0), not the external-RMII pin set. */
#define T21_EPHY_PB_PINS		(BIT(7) | BIT(15))

struct dwmac_ingenic_data {
	u32 mpll_hz;		/* MACCDR parent (MPLL) rate for this SoC */
	bool inner_phy;		/* T21 embedded ePHY (no ext PHY/reset) */
};

struct dwmac_ingenic_plat {
	struct dw_eth_pdata dw_eth_pdata;
	const struct dwmac_ingenic_data *socdata;
	unsigned int macphy_rate;
	unsigned int max_speed;
	void __iomem *cpm_phyc_reg;
	void __iomem *mac_base;
	struct gpio_desc reset_gpio;
};

static int dwmac_ingenic_of_to_plat(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	fdt_addr_t mac;
	u32 cpm_reg;

	pdata->socdata = (const struct dwmac_ingenic_data *)
			 dev_get_driver_data(dev);
	pdata->macphy_rate = dev_read_u32_default(dev, "macphy-rate", 50000000);
	pdata->max_speed = dev_read_u32_default(dev, "max-speed", 100);
	cpm_reg = dev_read_u32_default(dev, "ingenic,mode-reg", 0);
	if (!cpm_reg)
		return -EINVAL;
	pdata->cpm_phyc_reg = map_physmem(cpm_reg, 4, MAP_NOCACHE);

	mac = dev_read_addr(dev);
	if (mac == FDT_ADDR_T_NONE)
		return -EINVAL;
	pdata->mac_base = map_physmem(mac, 0x20, MAP_NOCACHE);

	return designware_eth_of_to_plat(dev);
}

static int macphy_clk_init(struct dwmac_ingenic_plat *pdata)
{
	void __iomem *cpm = (void __iomem *)T31_CPM_BASE;
	u32 div, v;

	/* Ungate the GMAC functional clock. */
	clrbits_le32(cpm + T31_CPM_CLKGR1, T31_CPM_CLKGR1_GMAC);

	/*
	 * MAC-PHY clock from MPLL. Vendor clk_set_rate (non-MSC):
	 * cdr = pll/rate - 1. External RMII wants 50 MHz; the T21
	 * inner ePHY wants 25 MHz. MPLL rate is per-SoC (T31 1200,
	 * T21 900). Same CE/BUSY dance as the MSC clock; leave CE set.
	 *
	 * The poll is bounded: on a board where the MAC-PHY clock can't
	 * lock, an unbounded wait would hang U-Boot forever at "Net:".
	 * Time out instead so the boot continues without ethernet.
	 */
	div = (pdata->socdata->mpll_hz / pdata->macphy_rate) - 1;
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

static int t31_gmac_rmii_init(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	struct eth_pdata *edata = &pdata->dw_eth_pdata.eth_pdata;
	u32 v;
	int ret;

	if (edata->phy_interface != PHY_INTERFACE_MODE_RMII) {
		dev_err(dev, "external PHY path needs RMII (mode %d)\n",
			edata->phy_interface);
		return -EINVAL;
	}

	/* Vendor jz_net_initialize order: clk_set_rate(MACPHY,50M),
	 * 50 ms settle, mux the RMII pins, then select RMII in the CPM
	 * GMAC-PHY-control register. */
	ret = macphy_clk_init(pdata);
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

static int t31_gmac_setphy(struct udevice *dev)
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

/*
 * Raw DesignWare MDIO (MAC GMII addr/data), used before the core
 * driver's MDIO bus exists: the vendor OMNI reset sequence pokes ePHY
 * register 0x18 between the two CPM reset pulses. Encoding mirrors
 * drivers/net/designware.c dw_mdio_{read,write}.
 */
static int t21_mdio_wait(void __iomem *mac)
{
	int timeout = 1000;

	while (timeout--) {
		if (!(readl(mac + MAC_MIIADDR) & MII_BUSY))
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

static int t21_mdio_read(void __iomem *mac, int addr, int reg)
{
	u32 a = ((addr << MIIADDRSHIFT) & MII_ADDRMSK) |
		((reg << MIIREGSHIFT) & MII_REGMSK);

	writel(a | MII_CLKRANGE_150_250M | MII_BUSY, mac + MAC_MIIADDR);
	if (t21_mdio_wait(mac))
		return -ETIMEDOUT;
	return readl(mac + MAC_MIIDATA) & 0xffff;
}

static int t21_mdio_write(void __iomem *mac, int addr, int reg, u16 val)
{
	u32 a = ((addr << MIIADDRSHIFT) & MII_ADDRMSK) |
		((reg << MIIREGSHIFT) & MII_REGMSK) | MII_WRITE;

	writel(val, mac + MAC_MIIDATA);
	writel(a | MII_CLKRANGE_150_250M | MII_BUSY, mac + MAC_MIIADDR);
	return t21_mdio_wait(mac);
}

/* One vendor ePHY soft-reset pulse: set then clear CPM reset bit. */
static void t21_ephy_reset_pulse(void __iomem *phyc)
{
	u32 v;

	v = readl(phyc);
	v |= INGENIC_ETH_EPHY_RESET;
	writel(v, phyc);
	mdelay(1);
	v = readl(phyc);
	v &= ~INGENIC_ETH_EPHY_RESET;
	writel(v, phyc);
}

/*
 * T21 embedded ePHY bring-up. Faithful to vendor jz4775-9161.c
 * PHY_TYPE_OMNI (branch T21-1.0.33): 25 MHz MAC-PHY clock, the CPM
 * ePHY enable/reset dance, an ePHY reg-0x18 power poke between the
 * two reset pulses, then MII interface select. No external pins,
 * no reset GPIO.
 */
static int t21_gmac_ephy_init(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	struct eth_pdata *edata = &pdata->dw_eth_pdata.eth_pdata;
	void __iomem *phyc = pdata->cpm_phyc_reg;
	void __iomem *gpb = (void __iomem *)T31_GPIO_PORTB_BASE;
	int retry = 1000;
	u32 v, rststat;
	int data;

	if (edata->phy_interface != PHY_INTERFACE_MODE_MII) {
		dev_err(dev, "inner ePHY path needs MII (mode %d)\n",
			edata->phy_interface);
		return -EINVAL;
	}

	v = macphy_clk_init(pdata);		/* 25 MHz (DT macphy-rate) */
	if (v) {
		dev_err(dev, "MAC-PHY clock did not lock (%d)\n", (int)v);
		return v;
	}

	v = readl(phyc);
	v |= INGENIC_ETH_EPHY_BIT21;
	writel(v, phyc);
	udelay(50000);

	/* Vendor OMNI muxes PB7+PB15 to device FUNC_2 (PAT1=1,PAT0=0). */
	writel(T21_EPHY_PB_PINS, gpb + G_PXINTC);
	writel(T21_EPHY_PB_PINS, gpb + G_PXMSKC);
	writel(T21_EPHY_PB_PINS, gpb + G_PXPAT1S);
	writel(T21_EPHY_PB_PINS, gpb + G_PXPAT0C);

	/* ePHY analog/clock seed (undocumented CPM word, vendor verbatim). */
	writel(T21_CPM_EPHY_SEED_VAL,
	       (void __iomem *)(T31_CPM_BASE + T21_CPM_EPHY_SEED_OFF));

	/* Enable the embedded PHY. */
	v = readl(phyc);
	v |= INGENIC_ETH_EPHY_ENABLE;
	writel(v, phyc);

	rststat = readl(phyc) & INGENIC_ETH_EPHY_RSTSTAT;
	t21_ephy_reset_pulse(phyc);

	/* ePHY reg 0x18 |= BIT(1): vendor power poke (MDIO addr 0). */
	data = t21_mdio_read(pdata->mac_base, 0, 0x18);
	if (data >= 0)
		t21_mdio_write(pdata->mac_base, 0, 0x18,
			       (u16)(data | BIT(1)));

	t21_ephy_reset_pulse(phyc);

	mdelay(1);
	while (retry--) {
		if ((readl(phyc) & INGENIC_ETH_EPHY_RSTSTAT) ^ rststat)
			break;
		mdelay(1);
	}
	if (retry <= 0)
		dev_warn(dev, "ePHY reset status did not toggle\n");

	/* MAC<->ePHY link is MII for the inner PHY. */
	v = readl(phyc);
	v &= ~INGENIC_ETH_SEL_MASK;
	v |= INGENIC_ETH_SEL_MII;
	writel(v, phyc);

	return 0;
}

static int dwmac_ingenic_probe(struct udevice *dev)
{
	struct dwmac_ingenic_plat *pdata = dev_get_plat(dev);
	int ret;

	if (pdata->socdata->inner_phy) {
		ret = t21_gmac_ephy_init(dev);
		if (ret)
			return ret;
	} else {
		ret = t31_gmac_rmii_init(dev);
		if (ret)
			return ret;
		ret = t31_gmac_setphy(dev);
		if (ret)
			return ret;
	}

	return designware_eth_probe(dev);
}

static const struct dwmac_ingenic_data t31_gmac_data = {
	.mpll_hz = 1200000000u,		/* T31 (also T23/T30) MPLL */
	.inner_phy = false,
};

static const struct dwmac_ingenic_data t21_gmac_data = {
	.mpll_hz = 900000000u,		/* T21N MPLL (t21/pll.c) */
	.inner_phy = true,
};

static const struct dwmac_ingenic_data t40_gmac_data = {
	.mpll_hz = 1000000000u,		/* T40 MPLL (t40/pll.c) */
	.inner_phy = false,		/* external RMII PHY */
};

static const struct udevice_id dwmac_ingenic_ids[] = {
	{ .compatible = "ingenic,t31-gmac", .data = (ulong)&t31_gmac_data },
	{ .compatible = "ingenic,t21-gmac", .data = (ulong)&t21_gmac_data },
	{ .compatible = "ingenic,t40-gmac", .data = (ulong)&t40_gmac_data },
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
