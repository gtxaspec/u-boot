// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T40 board (DDR2, SFC NOR)
 *
 * T40 board glue. The SPL (mach-xburst/t40) brings up console + PLL
 * + DDR + loads U-Boot proper from SFC NOR. Full U-Boot uses driver
 * model. T40 has a single OTG core (unlike A1's three), so the USB
 * PHY init is simpler.
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <mach/t40.h>
#include <mach/t40-ddr.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	gd->ram_size = T40_DDR_SIZE;
	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * T40 USB OTG PHY bring-up. Single DWC2 core (CPM_USBPCR /
 * CPM_USBPCR1 / CPM_USBVBFIL / CPM_USBRDT at the no-instance
 * offsets). Same bring-up shape as the A1 / T32 board files,
 * adapted for the single-instance T40 layout.
 *
 * Untested on real T40 silicon. Bring-up sequence transliterated
 * from the working A1 a1_usb_phy_init().
 */
static void t40_usb_phy_init(void)
{
	void __iomem *cpm = (void __iomem *)CPM_BASE;
	u32 v;

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
	mdelay(100);

	setbits_le32(cpm + CPM_SRBC0, SRBC_USB_SR);
	udelay(40);
	clrbits_le32(cpm + CPM_SRBC0, SRBC_USB_SR);

	setbits_le32(cpm + CPM_USBPCR1,
		     BIT(8) | BIT(9) | BIT(28) | BIT(29) | BIT(30));
	clrbits_le32(cpm + CPM_USBPCR1, BIT(19));	/* WORD_IF0 */
	v = readl(cpm + CPM_USBPCR1);
	v &= ~(0x7u << 23);
	v |= (5u << 23);
	writel(v, cpm + CPM_USBPCR1);

	writel(0, cpm + CPM_USBVBFIL);
	setbits_le32(cpm + CPM_USBRDT, USBRDT_VBFIL_LD_EN);

	v = readl(cpm + CPM_USBPCR);
	v |= USBPCR_USB_MODE_ORG | USBPCR_COMMONONN;
	v &= ~(USBPCR_OTG_DISABLE | USBPCR_SIDDQ | USBPCR_IDPULLUP_MASK |
	       USBPCR_VBUSVLDEXT | USBPCR_VBUSVLDEXTSEL);
	writel(v, cpm + CPM_USBPCR);

	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	clrbits_le32(cpm + CPM_USBRDT, USBRDT_UTMI_RST);
	setbits_le32(cpm + CPM_SRBC0, SRBC_USB_SR);
	udelay(10);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(20);
	setbits_le32(cpm + CPM_OPCR, OPCR_SPENDN0);
	mdelay(10);

	udelay(950);
	setbits_le32(cpm + CPM_USBRDT, USBRDT_UTMI_RST);
	udelay(20);
	clrbits_le32(cpm + CPM_SRBC0, SRBC_USB_SR);
	mdelay(10);
}

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

	setbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(30);
	clrbits_le32(cpm + CPM_USBPCR, USBPCR_POR);
	udelay(300);

	clrbits_le32(cpm + CPM_CLKGR0, CPM_CLKGR0_OTG);
}
#endif /* CONFIG_USB || CONFIG_USB_GADGET */

int board_init(void)
{
#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
	t40_usb_phy_init();
#endif
	return 0;
}

int checkboard(void)
{
	puts("Board: Ingenic ISVP-T40 (XBurst2)\n");
#ifdef CONFIG_SPL_T40_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
