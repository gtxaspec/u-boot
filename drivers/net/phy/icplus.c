// SPDX-License-Identifier: GPL-2.0+
/*
 * ICPlus IP101G PHY driver.
 *
 * The IP101G needs RMII-specific setup before it will establish a
 * link: an extended page is selected through register 0x1e and the
 * RMII reference-clock divider and RMII mode are set through the
 * paged register 0x1f. Without this auto-negotiation never completes
 * (MDIO still works, so phylib finds the PHY but the link stays down).
 *
 * The sequence mirrors the known-good vendor Ingenic U-Boot
 * (SynopGMAC_Dev.c synopGMAC_search_phy) for this PHY.
 */
#include <miiphy.h>

#define IP101G_PAGE_SEL		0x1e	/* extended page select */
#define IP101G_PAGE_DATA	0x1f	/* selected-page data */

#define IP101G_PAGE_RMII_CLK	0x0050
#define IP101G_RMII_CLK_25M	0x0040	/* bit6: REFCLK divide to 25 MHz */

#define IP101G_PAGE_RMII_MODE	0x4000
#define IP101G_RMII_MODE	0x0010	/* bit4: RMII mode */

static int ip101g_config(struct phy_device *phydev)
{
	int reg;

	phy_write(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_SEL,
		  IP101G_PAGE_RMII_CLK);
	reg = phy_read(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_DATA);
	reg |= IP101G_RMII_CLK_25M;
	phy_write(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_DATA, reg);

	phy_write(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_SEL,
		  IP101G_PAGE_RMII_MODE);
	reg = phy_read(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_DATA);
	reg |= IP101G_RMII_MODE;
	phy_write(phydev, MDIO_DEVAD_NONE, IP101G_PAGE_DATA, reg);

	return genphy_config_aneg(phydev);
}

/*
 * This IP101G variant reports PHY ID register 2 = 0x0000 and register
 * 3 = 0x0118 or 0x0128 (two silicon revisions), i.e. phy_id 0x00000118
 * / 0x00000128. Match both exactly, as the vendor driver does.
 */
U_BOOT_PHY_DRIVER(ip101g_0118) = {
	.name = "ICPlus IP101G",
	.uid = 0x00000118,
	.mask = 0xffffffff,
	.features = PHY_BASIC_FEATURES,
	.config = &ip101g_config,
	.startup = &genphy_startup,
	.shutdown = &genphy_shutdown,
};

U_BOOT_PHY_DRIVER(ip101g_0128) = {
	.name = "ICPlus IP101G",
	.uid = 0x00000128,
	.mask = 0xffffffff,
	.features = PHY_BASIC_FEATURES,
	.config = &ip101g_config,
	.startup = &genphy_startup,
	.shutdown = &genphy_shutdown,
};
