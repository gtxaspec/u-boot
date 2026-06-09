// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-A1 board (DDR3, SFC NOR)
 */

#include <dm.h>
#include <init.h>
#include <ram.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <mach/a1.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * Per-OTG-instance CPM register layout. The A1 has three identical
 * Synopsys DWC2 USB OTG cores; each core's PHY is controlled from CPM
 * and has its own USBPCR/USBPCR1/USBVBFIL/USBRDT register set, clock
 * gate bit, core soft-reset bit and PHY suspend-disable bit.
 */
struct a1_otg_phy {
	u16 pcr, pcr1, vbfil, rdt;
	u32 clkgr_bit;	/* CPM_CLKGR0 gate bit */
	u32 srbc_bit;	/* CPM_SRBC0 core soft-reset bit */
	u32 spendn_bit;	/* CPM_OPCR PHY suspend-disable bit */
};

static const struct a1_otg_phy a1_otg[3] = {
	{ CPM_USB0PCR, CPM_USB0PCR1, CPM_USB0VBFIL, CPM_USB0RDT,
	  CPM_CLKGR0_OTG0, SRBC_USB0_SR, OPCR_SPENDN0 },
	{ CPM_USB1PCR, CPM_USB1PCR1, CPM_USB1VBFIL, CPM_USB1RDT,
	  CPM_CLKGR0_OTG1, SRBC_USB1_SR, OPCR_SPENDN1 },
	{ CPM_USB2PCR, CPM_USB2PCR1, CPM_USB2VBFIL, CPM_USB2RDT,
	  CPM_CLKGR0_OTG2, SRBC_USB2_SR, OPCR_SPENDN2 },
};

/*
 * Bring up one USB OTG PHY for host mode. Faithful transliteration of
 * the vendor board/ingenic/isvp_a1/usb_init.c board_usb{,1,2}_init() -
 * the board sequence the working vendor U-Boot runs. Ungate the OTG
 * clock, CPM-SRBC soft-reset the core, program the USBPCR1 PHY
 * pull-down / word-interface bits, clear USBVBFIL, set the USBRDT
 * VBUS-filter load, select USB host mode in USBPCR, then run POR +
 * UTMI_RST wrapped in an SRBC pulse with the vendor's exact delays.
 * The XBurst2 USB PHY lives in CPM, so doing this in board_init -
 * before the dwc2 driver probes on `usb start` - leaves the PHY live
 * for the host core init.
 */
static void a1_usb_phy_init(const struct a1_otg_phy *otg)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	/* Feed the clock to the OTG core, then let it settle. */
	clrbits_le32(cpm + CPM_CLKGR0, otg->clkgr_bit);
	mdelay(100);

	/* Soft-reset the OTG core via CPM SRBC. */
	setbits_le32(cpm + CPM_SRBC0, otg->srbc_bit);
	udelay(40);
	clrbits_le32(cpm + CPM_SRBC0, otg->srbc_bit);

	/* USBPCR1: PHY pull-down / ID pull-up, 16-bit word interface. */
	setbits_le32(cpm + otg->pcr1,
		     USBPCR1_DMPULLDOWN | USBPCR1_DPPULLDOWN |
		     USBPCR1_IDPULLUP_ZEAR);
	clrbits_le32(cpm + otg->pcr1, USBPCR1_WORD_IF0);
	v = readl(cpm + otg->pcr1);
	v &= ~(0x7u << 23);		/* vendor PHY signal-tuning field */
	v |= (5u << 23);
	writel(v, cpm + otg->pcr1);

	writel(0, cpm + otg->vbfil);
	setbits_le32(cpm + otg->rdt, USBRDT_VBFIL_LD_EN);

	/* Select USB host mode; clear OTG-disable / SIDDQ / external VBUS. */
	v = readl(cpm + otg->pcr);
	v |= USBPCR_USB_MODE | USBPCR_COMMONONN;
	v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ | USBPCR_IDPULLUP_MASK |
	       USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL);
	writel(v, cpm + otg->pcr);

	/* PHY reset: POR + UTMI_RST wrapped by an SRBC pulse. */
	setbits_le32(cpm + otg->pcr, USBPCR_POR);
	clrbits_le32(cpm + otg->rdt, USBRDT_UTMI_RST);
	setbits_le32(cpm + CPM_SRBC0, otg->srbc_bit);
	udelay(10);
	clrbits_le32(cpm + otg->pcr, USBPCR_POR);
	udelay(20);
	setbits_le32(cpm + CPM_OPCR, otg->spendn_bit);
	mdelay(10);

	udelay(950);
	setbits_le32(cpm + otg->rdt, USBRDT_UTMI_RST);
	udelay(20);
	clrbits_le32(cpm + CPM_SRBC0, otg->srbc_bit);
	mdelay(10);
}

/*
 * dwc2_udc_otg weak-hook override, called from udc_enable() when the
 * DFU/g_dnl gadget starts. board_init() ran a1_usb_phy_init() on all
 * three OTG cores in host/OTG mode (USBPCR.USB_MODE set). The DFU
 * gadget runs on OTG0 (the mask-ROM USB-boot port); reconfigure that
 * PHY for device mode: clear USB_MODE and force the external
 * VBUS-valid on (no OTG VBUS sense on this board) so the device core
 * sees session-valid. The dwc2 OTG core also samples the OTG ID pin,
 * so dwc2_udc_otg's reconfig_usbd() additionally sets
 * GUSBCFG.FORCEDEVMODE (CONFIG_USB_GADGET_DWC2_OTG_FORCE_DEV_MODE).
 */
struct dwc2_udc;
void otg_phy_init(struct dwc2_udc *dev)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	const struct a1_otg_phy *otg = &a1_otg[0];	/* OTG0 = DFU port */
	u32 v;

	(void)dev;

	v = readl(cpm + otg->pcr);
	v &= ~(USBPCR_USB_MODE | USBPCR_OTG_DISABLE);
	v |= USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL;
	writel(v, cpm + otg->pcr);

	setbits_le32(cpm + CPM_OPCR, otg->spendn_bit);

	/* PHY power-on reset pulse. */
	setbits_le32(cpm + otg->pcr, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + otg->pcr, USBPCR_POR);
	udelay(300);

	clrbits_le32(cpm + CPM_CLKGR0, otg->clkgr_bit);
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
#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
	int i;

	/*
	 * Bring up all three OTG PHYs in host mode. The DFU loader
	 * overrides OTG0 to device mode later via otg_phy_init() when
	 * the gadget starts.
	 */
	for (i = 0; i < 3; i++)
		a1_usb_phy_init(&a1_otg[i]);
#endif
	return 0;
}

int checkboard(void)
{
#ifdef CONFIG_SPL_A1_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
