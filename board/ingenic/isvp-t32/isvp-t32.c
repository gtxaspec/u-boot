// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T32 board (PRJ007, DDR2 M14D5121632A target)
 *
 * U-Boot-proper board glue. The SPL (mach-xburst/t32) brings up
 * console + PLL; DDR comes up via the UCLASS_RAM driver
 * (drivers/ram/ingenic/ddr_t32.c) probed off the devicetree, which
 * also reports the per-SKU DRAM size consumed by dram_init() below.
 * Forward-ported from the vendor U-Boot 2022.10 PRJ (PRJ007 = T32).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <dm.h>
#include <ram.h>
#include <stdio.h>
#include <usb.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <mach/t32.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	struct ram_info ram;
	struct udevice *dev;
	int ret;

	ret = uclass_first_device_err(UCLASS_RAM, &dev);
	if (ret)
		return ret;

	ret = ram_get_info(dev, &ram);
	if (ret)
		return ret;

	gd->ram_size = ram.size;

	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * T32 (PRJ007) USB OTG PHY bring-up. Byte-for-byte transliteration
 * of the working T31 sequence in board/ingenic/isvp-t31/isvp-t31.c
 * (which itself mirrors the vendor U-Boot 2022.10 + kernel
 * cpm_usb.c). T31 and T32 share the same USB PHY block on identical
 * CPM offsets; the seed value 0x8380385a is the same, the bit
 * layouts are the same, and the SPENDN0 wake bit is the same. The
 * only T32 delta is which CPM_CLKGR0 bit holds the OTG gate
 * (BIT(2) on T32 vs BIT(3) on T31), and that is hidden behind the
 * CPM_CLKGR0_OTG macro in mach/t32.h.
 *
 * Earlier attempts also wrote the two undocumented USBPHY tail-fix
 * registers at USBPHY_BASE + 0x70/0x78 (vendor kernel comment:
 * "host can not identify device"). On T32LQ those writes cause
 * XACTERR on the first HS bulk-IN and then wedge the dwc2 channel,
 * so they are left out - the standard PHY init below enumerates
 * the ASIX without them.
 */
static void t32_usb_phy_init(void)
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
	/*
	 * Enable TX pre-emphasis (bit 6). Vendor cpm_usb.c has this
	 * commented out ("enable tx pre-emphasis") but on the T32LQ
	 * lab harness without it bulk-IN packets from the ASIX
	 * upstream NIC get XACTERR on the first HS burst (signal
	 * eye marginal through the USB MUX + cable). Pre-emphasis
	 * sharpens the rising edges and recovers eye margin.
	 */
	v |= BIT(6);	/* USBPCR_TXPREEMPHTUNE */
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
 * DFU/g_dnl gadget starts. board_init() ran t32_usb_phy_init() which
 * left the PHY in host/OTG mode (USBPCR.USB_MODE_ORG set); reconfigure
 * it for device mode here: clear USB_MODE_ORG and force the external
 * VBUS-valid on (this board has no OTG VBUS sense) so the device core
 * sees session-valid (GOTGCTL.BSesVld).
 *
 * This is necessary but not sufficient: the dwc2 OTG core also samples
 * the OTG ID pin, which the T32LQ wires to A-device, so the core would
 * still come up in host mode. dwc2_udc_otg's reconfig_usbd() sets
 * GUSBCFG.FORCEDEVMODE to override that (the T32 USB-boot defconfig
 * selects CONFIG_USB_GADGET_DWC2_OTG_FORCE_DEV_MODE). USBPCR1 (PHY
 * ref-clock / word interface) is mode-independent and already correct
 * from t32_usb_phy_init(), so it is left untouched.
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
	t32_usb_phy_init();
#endif
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T32 SKU. */
int checkboard(void)
{
	printf("Variant: %s\n", CONFIG_T32_VARIANT_NAME);
#ifdef CONFIG_SPL_T32_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
