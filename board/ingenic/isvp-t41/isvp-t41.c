// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic ISVP-T41 board (DDR2, SFC NOR)
 *
 * T41 board glue. The SPL (mach-xburst/t41) brings up console + PLL
 * + DDR + loads U-Boot proper from SFC NOR. Full U-Boot uses driver
 * model. T41 has a single OTG core (unlike A1's three), so the USB
 * PHY init is simpler.
 */

#include <init.h>
#include <stdio.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <cpu_func.h>
#include <linux/delay.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * XBurst2 cache flush. The U-Boot generic MIPS flush_cache is a no-op
 * here because mips_cache_probe() reads CP0 Config1, and on XBurst2
 * Config1.IL/DL come back 0, so icache_line_size()/dcache_line_size()
 * return 0 and the cache_loop never iterates.
 *
 * XBurst2 L1: 8-way x 128 sets x 32B = 32KB I-cache + 32KB D-cache.
 *
 * Vendor flow (matches arch/mips/cpu/xburst/cpu.c flush_dcache_range):
 * Hit_Writeback_Inv_D, sync, then read from KSEG1 to drain the write
 * buffer (writes from D-cache go through a write buffer that must be
 * forced out before reads of the same address see updated DRAM). Then
 * Hit_Invalidate_I so subsequent fetches of the relocated code miss
 * I-cache and refill from DRAM.
 */
#define XB2_LINE_SIZE	32
void flush_cache(ulong start_addr, ulong size)
{
	unsigned long a;
	unsigned long end;
	volatile unsigned int writebuffer;

	if (!size)
		return;

	end = (start_addr + size + XB2_LINE_SIZE - 1) & ~(XB2_LINE_SIZE - 1);
	start_addr &= ~(XB2_LINE_SIZE - 1);

	for (a = start_addr; a < end; a += XB2_LINE_SIZE)
		__asm__ volatile("cache 0x15, 0(%0)" : : "r"(a));	/* Hit_Writeback_Inv_D */
	__asm__ volatile("sync");
	writebuffer = *(volatile unsigned int *)0xa0000000;
	(void)writebuffer;

	for (a = start_addr; a < end; a += XB2_LINE_SIZE)
		__asm__ volatile("cache 0x10, 0(%0)" : : "r"(a));	/* Hit_Invalidate_I */
	__asm__ volatile("sync");
}

int dram_init(void)
{
	gd->ram_size = T41_DDR_SIZE;
	return 0;
}

#if defined(CONFIG_USB) || defined(CONFIG_USB_GADGET)
/*
 * T41 USB OTG PHY bring-up. Single DWC2 core (CPM_USBPCR /
 * CPM_USBPCR1 / CPM_USBVBFIL / CPM_USBRDT at the no-instance
 * offsets). Same bring-up shape as the A1 / T32 board files,
 * adapted for the single-instance T41 layout.
 *
 * Untested on real T41 silicon. Bring-up sequence transliterated
 * from the working A1 a1_usb_phy_init().
 */
static void t41_usb_phy_init(void)
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
	t41_usb_phy_init();
#endif
	return 0;
}

int checkboard(void)
{
	puts("Board: Ingenic ISVP-T41 (XBurst2)\n");
#ifdef CONFIG_SPL_T41_USB_BOOT
	puts("Loader: USB-boot\n");
#endif
	return 0;
}
