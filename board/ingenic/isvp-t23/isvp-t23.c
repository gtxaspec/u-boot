// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T23 board (DDR2, SFC NOR)
 *
 * U-Boot-proper board glue: DRAM size, USB PHY bring-up. The SPL
 * (mach-xburst/t23) brings up console + PLL + Innophy DDR2.
 * T23/T23N = 64 MB, T23DL/T23DN = 32 MB (CONFIG_T23_DRAM_32M);
 * no 128 MB board.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <mach/t23.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
#if defined(CONFIG_T23_DRAM_32M)
	gd->ram_size = 32 << 20;	/* T23DL/T23DN: M14D2561616A */
#else
	gd->ram_size = 64 << 20;	/* T23/T23N: M14D5121632A */
#endif
	return 0;
}

/*
 * USB PHY bring-up, transliterated from the vendor T23
 * otg_phy_init() (arch/mips/cpu/xburst/t23/clk.c). The CPM USB PHY
 * block is the same XBurst1 IP as T31; the sequence is the vendor's
 * exact one for T23 with the 24 MHz EXTAL ref clock.
 *
 * board_init() runs the OTG/host path (for "usb start"). The
 * dwc2_udc_otg gadget weak-hook otg_phy_init() below re-runs the
 * vendor DEVICE_ONLY_MODE path so DFU/g_dnl enumerates - on T23
 * device mode sets OTG_DISABLE and forces external VBUS-valid (the
 * board has no OTG VBUS sense), unlike the host path.
 */
static void t23_usb_phy_init(bool device)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	/* USBPCR1: PHY ref clock = core, 16/30-bit word IF, /24 MHz. */
	v = readl(cpm + CPM_USBPCR1);
	v &= ~(USBPCR1_REFCLKSEL_MASK | USBPCR1_REFCLKDIV_MASK);
	v |= USBPCR1_REFCLKSEL_CORE | USBPCR1_WORD_IF0_16_30 |
	     USBPCR1_REFCLKDIV_24M;
	writel(v, cpm + CPM_USBPCR1);

	v = readl(cpm + CPM_USBPCR);
	if (device) {
		v &= ~USBPCR_USB_MODE_ORG;
		v |= USBPCR_VBUSVLDEXTSEL | USBPCR_VBUSVLDEXT |
		     USBPCR_OTG_DISABLE;
	} else {
		v |= USBPCR_USB_MODE_ORG;
		v &= ~(USBPCR_VBUSVLDEXTSEL | USBPCR_VBUSVLDEXT |
		       USBPCR_OTG_DISABLE);
	}
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);

	/* PHY power-on reset pulse. */
	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(300);

	/* Ungate the OTG core clock. */
	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
}

struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	(void)dev;
	t23_usb_phy_init(true);		/* DEVICE_ONLY_MODE for DFU/g_dnl */
}

int board_init(void)
{
	t23_usb_phy_init(false);	/* OTG/host path for "usb start" */
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T23 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T23_VARIANT_NAME);
	return 0;
}
