// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T21 board (DDR2 64 MB, SFC NOR)
 *
 * U-Boot-proper board glue: DRAM size, USB PHY bring-up. The SPL
 * (mach-xburst/t21) brings up console + PLL + Innophy DDR2 (T21N
 * 64 MB M14D5121632A).
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <mach/t21.h>

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
#include <dm/ofnode.h>
#include <linux/usb/otg.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = 64 << 20;	/* T21N: DDR2 M14D5121632A 64 MB */
	return 0;
}

/*
 * MSC0 (microSD) is PB0..PB5 in device function 0 (CLK, CMD, D0..D3).
 * The SPL never touches MSC, so set up its pinmux and functional clock
 * here before the driver model probes - without an ungated MSC0 clock
 * the first jz_mmc register access bus-stalls (mmc list / mmcinfo
 * hang). Faithful to the vendor clk_set_rate(MSC, 24 MHz): source is
 * APLL/sclka ([31:30] select 0); jz_mmc assumes a 24 MHz functional
 * clock and T21N APLL is 864 MHz, so cdr = ((864/24)/2 - 1) = 17 and
 * 864/(2*(17+1)) = 24 MHz exactly. Set CE and the divider, wait BUSY,
 * and (like the vendor) never clear CE or the MSC goes unclocked on
 * real silicon. Same approach as the HW-proven isvp-t31 t31_msc0_init.
 */
#define GPIO_PORTB_BASE		0xb0011000	/* GPIO_BASE + port B * 0x1000 */
#define G_PXINTC		0x18
#define G_PXMSKC		0x28
#define G_PXPAT1C		0x38
#define G_PXPAT0C		0x48
#define G_PXPUENC		0x118
#define G_PXPDENC		0x128
#define MSC0_PINS		((0x3u << 4) | (0xfu << 0))	/* PB0..PB5 */
#define MSC0_CDR		17	/* ((APLL 864 / 24) / 2) - 1 */

static void t21_msc0_init(void)
{
	void __iomem *gpb = (void __iomem *)GPIO_PORTB_BASE;
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	/* Mux PB0..PB5 to device function 0 (vendor gpio_set_func). */
	writel(MSC0_PINS, gpb + G_PXINTC);
	writel(MSC0_PINS, gpb + G_PXMSKC);
	writel(MSC0_PINS, gpb + G_PXPAT1C);
	writel(MSC0_PINS, gpb + G_PXPAT0C);
	writel(MSC0_PINS, gpb + G_PXPUENC);
	writel(MSC0_PINS, gpb + G_PXPDENC);

	/* Ungate the MSC0 functional clock. */
	writel(readl(cpm + CPM_CLKGR0) & ~CPM_CLKGR0_MSC0, cpm + CPM_CLKGR0);

	/*
	 * Source = APLL (sel 0), clear src/stop/divider, set CE + cdr,
	 * wait BUSY. Leave CE set, exactly like the vendor.
	 */
	v = readl(cpm + CPM_MSC0CDR);
	v &= ~((3u << 30) | (3u << MSCCDR_STOP_SHIFT) | MSCCDR_DIV_MASK);
	v |= MSCCDR_CE | MSC0_CDR;
	writel(v, cpm + CPM_MSC0CDR);
	while (readl(cpm + CPM_MSC0CDR) & MSCCDR_BUSY)
		;
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
static void t21_usb_phy_init(bool device)
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
	t21_usb_phy_init(true);		/* DEVICE_ONLY_MODE for DFU/g_dnl */
}

#endif /* CONFIG_USB || CONFIG_USB_GADGET */

int board_init(void)
{
	t21_msc0_init();

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
	/*
	 * Only the host build (dr_mode="host", t21-isvp.dts) does the
	 * host PHY bring-up here. The DFU loader (t21-isvp-dfu.dts,
	 * dr_mode="peripheral") must NOT run the host sequence - its
	 * SRBC core reset / UTMI_RST staging leaves the PHY mid-host
	 * and the gadget then fails to enumerate; the dwc2_udc_otg
	 * weak hook otg_phy_init() does the device PHY init instead.
	 */
	{
		ofnode otg = ofnode_path("/usb@13500000");

		if (ofnode_valid(otg) &&
		    usb_get_dr_mode(otg) == USB_DR_MODE_HOST)
			t21_usb_phy_init(false);
	}
#endif

	return 0;
}

/* Printed right after the "Model:" line; shows the exact T21 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T21_VARIANT_NAME);
	return 0;
}
