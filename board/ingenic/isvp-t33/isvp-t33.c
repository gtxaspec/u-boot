// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T33 board (DDR2 M14D5121632A target)
 *
 * U-Boot-proper board glue. The SPL (mach-xburst/t33) brings up
 * console + PLL (Stage 1); Innophy DDR2 + the real RAM size land
 * with Stage 2. Forward-ported from the vendor U-Boot 2022.10 T33.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <stdio.h>
#include <usb.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <mach/t33.h>
#include <mach/t33-ddr.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/* Per the Kconfig DDR class: 64 MB DDR2 / 128 MB DDR3. */
	gd->ram_size = T33_DDR_SIZE;
	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * T33 USB OTG PHY bring-up. Mirrors the T32 (PRJ007 sibling)
 * sequence in board/ingenic/isvp-t32/isvp-t32.c, which itself
 * tracks the vendor U-Boot 2022.10 + kernel cpm_usb.c. Same CPM
 * offsets, same USBPCR seed (0x8380385a), same SPENDN0 wake bit;
 * the OTG gate sits at CPM_CLKGR0 BIT(2) on both PRJ008/PRJ007.
 *
 * Untested on real silicon (no T33 hardware yet) - the T32 port
 * was HW-verified, so as long as the silicon parity holds this
 * should come up. Adjust if HW bring-up reveals deltas.
 */
static void t33_usb_phy_init(void)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	/* Feed the clock to the OTG core, then let it settle. */
	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
	mdelay(100);

	/* Soft-reset the OTG core via CPM SRBC. */
	setbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);
	udelay(40);
	clrbits_le32(cpm + CPM_SRBC, SRBC_USB_SR);

	/* USBPCR1: vendor PHY word-interface / ref-clock programming. */
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

	/* Vendor seed + OTG/host mode bits. */
	writel(0x8380385au, cpm + CPM_USBPCR);
	v = readl(cpm + CPM_USBPCR);
	v |= USBPCR_USB_MODE_ORG | USBPCR_COMMONONN;
	v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ | USBPCR_IDPULLUP_MASK |
	       USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL);
	v |= BIT(6);	/* TX pre-emphasis (matches T32 lab fix) */
	writel(v, cpm + CPM_USBPCR);

	/* PHY reset: POR + UTMI_RST wrapped by an SRBC pulse. */
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

/*
 * dwc2_udc_otg weak-hook override, called from udc_enable() when the
 * DFU/g_dnl gadget starts. board_init() ran t33_usb_phy_init() which
 * left the PHY in host/OTG mode; reconfigure it for device mode:
 * clear USB_MODE_ORG and force the external VBUS-valid on.
 */
struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	(void)dev;

	v = readl(cpm + CPM_USBPCR);
	v &= ~(USBPCR_USB_MODE_ORG | USBPCR_OTG_DISABLE);
	v |= USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL;
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);

	/* PHY power-on reset pulse. */
	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(300);

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
}
#endif

int board_init(void)
{
#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
	/*
	 * The DM USB stack's dwc2_usb_probe() does not call
	 * board_usb_init(), so the PHY must be brought up here.
	 */
	t33_usb_phy_init();
#endif
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T33 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T33_VARIANT_NAME);
#ifdef CONFIG_SPL_T33_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
