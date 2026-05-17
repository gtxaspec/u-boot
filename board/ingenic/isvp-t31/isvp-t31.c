// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T31 board (DDR2 128 MB, SFC NOR)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <mach/t31.h>
#include <usb.h>

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

/*
 * Bring up the USB OTG PHY for dual-role (host + gadget). Faithful
 * transliteration of the vendor otg_phy_init() OTG/host path with a
 * 24 MHz EXTAL reference: program the ref-clock in USBPCR1, put the
 * PHY in OTG mode in USBPCR, assert OPCR SPENDN0 (without which the
 * PHY stays suspended on real silicon), pulse POR, then ungate the
 * OTG clock. Host (dwc2) and gadget (dwc2_udc_otg) share this PHY.
 */
static void t31_usb_phy_init(void)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	v = readl(cpm + CPM_USBPCR1);
	v &= ~(USBPCR1_REFCLKSEL_MASK | USBPCR1_REFCLKDIV_MASK);
	v |= USBPCR1_REFCLKSEL_CORE | USBPCR1_WORD_IF0_16_30 |
	     USBPCR1_REFCLKDIV_24M;
	writel(v, cpm + CPM_USBPCR1);

	/* OTG mode (same bits as host): USB_MODE_ORG set, VBUS ext /
	 * OTG_DISABLE cleared. */
	v = readl(cpm + CPM_USBPCR);
	v |= USBPCR_USB_MODE_ORG;
	v &= ~(USBPCR_VBUSVLDEXTSEL | USBPCR_VBUSVLDEXT | USBPCR_OTG_DISABLE);
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);

	/* PHY power-on reset pulse. */
	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(300);

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
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
	 * Device mode: USB_MODE_ORG cleared, OTG block enabled, and the
	 * external VBUS-valid forced on (this board has no OTG VBUS
	 * sense) so the device core sees session-valid.
	 */
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

int dram_init(void)
{
	/* DDR2 128 MB; TODO: derive from the DDR controller once it is up */
	gd->ram_size = 128 << 20;
	return 0;
}

int board_init(void)
{
	t31_msc0_init();
	t31_usb_phy_init();
	return 0;
}

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
