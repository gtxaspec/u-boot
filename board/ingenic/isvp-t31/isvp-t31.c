// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T31 board (DDR2 128 MB, SFC NOR)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <dm.h>
#include <ram.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t31.h>
#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
#include <usb.h>
#include <dm/ofnode.h>
#include <linux/usb/otg.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

/*
 * MSC0 (microSD) is wired to GPIO port B pins PB0..PB5 in device
 * function 0 (CLK, CMD, D0..D3). The SPL never touches MSC, so set up
 * its pinmux and functional clock here before the driver model probes.
 *
 * The clock setup is a faithful transliteration of the vendor
 * clk_set_rate(MSC, 24 MHz): MSC source is APLL (CONFIG_CPU_SEL_PLL),
 * which is index 0 of {APLL,MPLL,VPLL} so the [31:30] select is 0;
 * cdr = ((apll/rate)/2 - 1) = ((1392/24)/2 - 1) = 28; set CE and the
 * divider and wait for BUSY to clear. Critically the vendor does NOT
 * clear CE afterwards - doing so leaves the MSC unclocked on real
 * silicon (the controller reset never completes, jz_mmc probe times
 * out -110), even though QEMU's MSC model tolerates it. jz_mmc assumes
 * a 24 MHz functional clock; 1392/(2*(28+1)) = 24 MHz exactly.
 */
#define GPIO_PORTB_BASE		0xb0011000	/* GPIO_BASE + port B * 0x1000 */
#define G_PXINTC		0x18
#define G_PXMSKC		0x28
#define G_PXPAT1C		0x38
#define G_PXPAT0C		0x48
#define G_PXPUENC		0x118
#define G_PXPDENC		0x128
#define MSC0_PINS		((0x3u << 4) | (0xfu << 0))	/* PB0..PB5 */
#define MSC0_CDR		28	/* ((APLL 1392 / 24) / 2) - 1 */

static void t31_msc0_init(void)
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
 * Bring up the USB OTG PHY for host. Faithful transliteration of the
 * vendor board/ingenic/isvp_t31/usb_init.c board_usb_init() - the
 * board-specific sequence the working vendor U-Boot actually runs.
 * (The generic clk.c otg_phy_init mirrored before was the wrong
 * reference; matching every dwc2/PHY register to it still left the
 * PHY powered but deaf - HPRT0.CONNSTS stuck 0.) Ungate the OTG
 * clock, CPM-SRBC soft-reset the core, program the vendor USBPCR1
 * word-interface, clear USBVBFIL, set USBRDT (VBFIL load), seed
 * USBPCR then drop the OTG/ID/VBUS bits, and run POR + UTMI_RST
 * wrapped in an SRBC pulse with the vendor's exact delays. Gadget
 * mode uses the separate device-PHY weak hook
 * otg_phy_init(struct dwc2_udc *).
 */
static void t31_usb_phy_init(void)
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

	/*
	 * Seed USBPCR with the vendor host value, then clear the
	 * OTG-disable / SIDDQ / ID-pullup / external-VBUS bits. The
	 * end state is 0x8200385a (USB mode + COMMONONN + the vendor
	 * host signal tuning), matching the working vendor U-Boot.
	 */
	writel(0x8380385a, cpm + CPM_USBPCR);
	v = readl(cpm + CPM_USBPCR);
	v |= USBPCR_USB_MODE_ORG | USBPCR_COMMONONN;
	v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ | USBPCR_IDPULLUP_MASK |
	       USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL);
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
 * dwc2_udc_otg weak-hook override, called from udc_enable() (gadget
 * .udc_start, i.e. when "dfu"/g_dnl starts) just before the DWC2 core
 * soft-reset. t31_usb_phy_init() above configures the PHY for host/OTG
 * (USB_MODE_ORG set, VBUS-valid select cleared) - correct for "usb
 * start" but wrong for the gadget: the device core then never sees
 * session-valid and EP0 never answers (host enumeration times out,
 * -110). Configure the PHY for device mode instead, mirroring the
 * mask-ROM USB-device setup (bootrom: USBPCR &= ~USB_MODE_ORG;
 * |= VBUSVLDEXT|VBUSVLDEXTSEL; POR pulse) - it is a proven USB device
 * on this exact PHY. dev is unused.
 */
struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	(void)dev;

	v = readl(cpm + CPM_USBPCR1);
	v &= ~(USBPCR1_REFCLKSEL_MASK | USBPCR1_REFCLKDIV_MASK);
	v |= USBPCR1_REFCLKSEL_CORE | USBPCR1_WORD_IF0_16_30 |
	     USBPCR1_REFCLKDIV_24M;
	writel(v, cpm + CPM_USBPCR1);

	/*
	 * Device mode: USB_MODE_ORG cleared, OTG block disabled so the
	 * dwc2 core does not sample the OTG ID pin and come up in host
	 * mode (the Z55 board reads the ID as A-device), and the external
	 * VBUS-valid forced on (this board has no OTG VBUS sense) so the
	 * device core sees session-valid.
	 */
	v = readl(cpm + CPM_USBPCR);
	v &= ~USBPCR_USB_MODE_ORG;
	v |= USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL | USBPCR_OTG_DISABLE;
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);

	/* PHY power-on reset pulse. */
	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(300);

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
}
#endif /* CONFIG_USB || CONFIG_USB_GADGET */

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

int board_init(void)
{
	t31_msc0_init();
#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
	/*
	 * Only the host build (dr_mode="host", t31-isvp.dts) does the
	 * host PHY bring-up here. The DFU loader (t31-isvp-dfu.dts,
	 * dr_mode="peripheral") must NOT run the host sequence - its
	 * SRBC core reset / UTMI_RST staging leaves the PHY mid-host
	 * and the gadget then fails to enumerate; the dwc2_udc_otg
	 * weak hook otg_phy_init() does the device PHY init instead.
	 */
	{
		ofnode otg = ofnode_path("/usb@13500000");

		if (ofnode_valid(otg) &&
		    usb_get_dr_mode(otg) == USB_DR_MODE_HOST)
			t31_usb_phy_init();
	}
#endif
	return 0;
}

/* Printed right after the "Model:" line; shows the exact T31 SKU. The
 * variant + CPU clock come from the DT-selected DDR driver (no compile-
 * time CONFIG_T31_VARIANT_*). */
int checkboard(void)
{
#ifdef CONFIG_SPL_T31_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * Re-run the host PHY bring-up immediately before the dwc2 host core
 * init. The dwc2 host path (usb_lowlevel_init) calls this just before
 * dwc_otg_core_init()/the core reset; the one-shot board_init() PHY
 * setup runs far too early (decoupled from the core reset) and the
 * port never enumerates. This mirrors the vendor U-Boot ordering
 * (otg_phy_init() then the dwc2 core init). Gadget mode uses the
 * separate device-PHY weak hook (otg_phy_init(struct dwc2_udc *)).
 */
int board_usb_init(int index, enum usb_init_type init)
{
	if (init == USB_INIT_HOST)
		t31_usb_phy_init();
	return 0;
}
#endif /* CONFIG_USB || CONFIG_USB_GADGET */
