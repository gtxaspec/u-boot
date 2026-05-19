// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T10 board (DDR2 64 MB, SFC NOR)
 *
 * U-Boot-proper board glue: DRAM size, USB PHY bring-up. The SPL
 * (mach-xburst/t10) brings up console + PLL + Innophy DDR2 (T10N
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
 * USB PHY bring-up. Same XBurst1 USB PHY as T31/T23, so the host
 * path is the HW-proven T31 sequence (vendor isvp_t31 usb_init.c
 * board_usb_init): SRBC core reset, USBPCR1 word-IF / ref-clk,
 * clear USBVBFIL, USBRDT VBFIL-load, the vendor USBPCR host seed
 * then RMW, POR + UTMI_RST wrapped in an SRBC pulse with the exact
 * delays. board_init() runs the host path (for "usb start" /
 * USB-NIC); the dwc2_udc_otg gadget weak-hook otg_phy_init() runs
 * the device path (DFU/g_dnl): USB_MODE_ORG cleared, OTG disabled,
 * external VBUS-valid forced (no OTG VBUS sense on this board).
 */
static void t10_usb_phy_init(bool device)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	if (device) {
		v = readl(cpm + CPM_USBPCR1);
		v &= ~(USBPCR1_REFCLKSEL_MASK | USBPCR1_REFCLKDIV_MASK);
		v |= USBPCR1_REFCLKSEL_CORE | USBPCR1_WORD_IF0_16_30 |
		     USBPCR1_REFCLKDIV_24M;
		writel(v, cpm + CPM_USBPCR1);

		v = readl(cpm + CPM_USBPCR);
		v &= ~USBPCR_USB_MODE_ORG;
		v |= USBPCR_VBUSVLDEXTSEL | USBPCR_VBUSVLDEXT |
		     USBPCR_OTG_DISABLE;
		writel(v, cpm + CPM_USBPCR);

		setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);
		setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
		udelay(30);
		clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
		udelay(300);
		clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
		return;
	}

	/* Host path: HW-proven T31 sequence (same USB PHY). */
	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
	mdelay(100);

	setbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);
	udelay(40);
	clrbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);

	setbits_le32(cpm + CPM_USBPCR1,
		     BIT(8) | BIT(9) | BIT(28) | BIT(29) | BIT(30));
	clrbits_le32(cpm + CPM_USBPCR1, BIT(19));	/* WORD_IF0 */
	v = readl(cpm + CPM_USBPCR1);
	v &= ~(0x7u << 23);
	v |= (5u << 23);
	writel(v, cpm + CPM_USBPCR1);

	writel(0, cpm + CPM_USBVBFIL);
	writel(0x96, cpm + CPM_USBRDT);
	setbits_le32(cpm + CPM_USBRDT, USBRDT_VBFIL_LD_EN);

	writel(0x8380385a, cpm + CPM_USBPCR);
	v = readl(cpm + CPM_USBPCR);
	v |= USBPCR_USB_MODE_ORG | USBPCR_COMMONONN;
	v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ | USBPCR_IDPULLUP_MASK |
	       USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL);
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	clrbits_le32(cpm + CPM_USBRDT, USBRDT_UTMI_RST);
	setbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);
	udelay(10);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(20);
	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);
	mdelay(50);

	udelay(950);
	setbits_le32(cpm + CPM_USBRDT, USBRDT_UTMI_RST);
	udelay(20);
	clrbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);
	mdelay(10);
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
	 * SRBC core reset / UTMI_RST staging leaves the PHY mid-host
	 * and the gadget then fails to enumerate; the dwc2_udc_otg
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

/* Printed right after the "Model:" line; shows the exact T10 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T10_VARIANT_NAME);
#ifdef CONFIG_SPL_T10_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
