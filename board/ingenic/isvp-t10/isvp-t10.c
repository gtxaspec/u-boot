// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T10 board (DDR2 64 MB, SFC NOR)
 *
 * U-Boot-proper board glue: DRAM size, USB PHY bring-up. The SPL
 * (mach-xburst/t10) brings up console + PLL + Synopsys-DWC DDR2 (T10N
 * 64 MB M14D5121632A).
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <mach/t10.h>

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
#include <dm/ofnode.h>
#include <linux/usb/otg.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = 64 << 20;	/* T10N: DDR2 M14D5121632A 64 MB */
	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * USB PHY bring-up: the vendor T10 sequence, taken 1:1 from the 3.10
 * kernel soc-t10/common/cpm_usb.c jz_otg_phy_init() (the 2013 vendor
 * U-Boot board/ingenic/isvp_t10/usb_init.c is line-identical). The
 * T10 PHY is NOT the T31/T23 one: dwc-otg select is USBPCR1 bits
 * 29:28, the UTMI word interface is 16-bit/30M (bit 19 SET), and
 * bits [25:23] carry a 3-bit tune field = 5. The T31-style
 * REFCLKSEL/REFCLKDIV writes land on those bits and break the
 * handshake (host-side descriptor-read EPROTO storms, full/high
 * speed flapping; bench-verified on T10L). Mode tails per vendor:
 * DEVICE clears USB_MODE_ORG/OTG_DISABLE/SIDDQ and keeps COMMONONN;
 * HOST/OTG sets USB_MODE_ORG and clears the VBUS-ext/ID-pullup
 * sensing bits.
 */
static void t10_usb_phy_init(bool device)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);

	/* select dwc otg, 16-bit/30M word interface, tune [25:23] = 5 */
	setbits_le32(cpm + CPM_USBPCR1,
		     BIT(29) | BIT(28) | USBPCR1_WORD_IF0_16_30);
	v = readl(cpm + CPM_USBPCR1);
	v &= ~(0x7u << 23);
	v |= 5u << 23;
	writel(v, cpm + CPM_USBPCR1);

	/* un-suspend the PHY, then drop SIDDQ */
	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);
	udelay(45);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_SIDDQ);

	writel(0, cpm + CPM_USBVBFIL);

	v = readl(cpm + CPM_USBRDT);
	v &= ~(USBRDT_VBFIL_LD_EN | GENMASK(22, 0));
	v |= 0x96;
	writel(v, cpm + CPM_USBRDT);
	setbits_le32(cpm + CPM_USBRDT, USBRDT_VBFIL_LD_EN);

	/* vendor USBPCR seed, then the mode tail */
	writel(0x83803857, cpm + CPM_USBPCR);
	v = readl(cpm + CPM_USBPCR);
	if (device) {
		v &= ~(USBPCR_USB_MODE_ORG | USBPCR_OTG_DISABLE |
		       USBPCR_SIDDQ);
		v |= USBPCR_COMMONONN;
	} else {
		v |= USBPCR_USB_MODE_ORG | USBPCR_COMMONONN;
		v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ |
		       USBPCR_IDPULLUP_MASK | USBPCR_VBUSVLDEXT |
		       USBPCR_VBUSVLDEXTSEL);
	}
	writel(v, cpm + CPM_USBPCR);

	/* POR pulse */
	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	mdelay(1);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	mdelay(1);
}

struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	(void)dev;
	t10_usb_phy_init(true);		/* DEVICE_ONLY_MODE for DFU/g_dnl */
}

int board_init(void)
{
	ofnode otg = ofnode_path("/usb@13500000");

	/*
	 * Only the host build (dr_mode="host", t10-isvp.dts) does the
	 * host PHY bring-up here. The DFU loader (t10-isvp-dfu.dts,
	 * dr_mode="peripheral") must NOT run the host sequence - its
	 * host mode tail is wrong for a gadget; the dwc2_udc_otg
	 * weak hook otg_phy_init() does the device PHY init instead.
	 */
	if (ofnode_valid(otg) && usb_get_dr_mode(otg) == USB_DR_MODE_HOST)
		t10_usb_phy_init(false);

	return 0;
}
#else  /* no USB (slim/wired-eth-only build) */
int board_init(void)
{
	return 0;
}
#endif

int checkboard(void)
{
	/*
	 * No "Variant:" line: the SKU is carried by the leaf-DT Model: string
	 * (params-in-DT, no compile-time variant). DM-SPL boards do not re-add it.
	 */
	return 0;
}
