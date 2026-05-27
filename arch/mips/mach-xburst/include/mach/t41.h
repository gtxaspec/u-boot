/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T41 SoC register definitions (XBurst2)
 *
 * T41 is the dual-core XBurst2 successor to T40, byte-for-byte
 * compatible base address map (verified against vendor T41-1.2.6
 * `arch/mips/include/asm/arch-t41/base.h`). Differences vs T40:
 *
 *   - DDR3 16-bit bus on most variants (vs T40XP 32-bit DDR3, T40N
 *     32-bit DDR2), DW32=0 in vendor CONFIG_DDR_DW32.
 *   - 13 chip variants in vendor source; lab board is T41NQ
 *     (APLL=1104, MPLL=1400, DDR=700, W631GU6NG 128 MiB).
 *   - Boot media on the lab board is SPI-NOR (MX25L25645G 32 MiB)
 *     rather than NAND - same SFC v2 controller as T40.
 *
 * CPM register layout, UART layout, GPIO stride, OTG location, and
 * Innophy PHY register layout all match T40, so this header is a
 * direct fork of t40.h. The variant headers (t41nq-ddr.h etc.)
 * carry the per-chip PLL MNOD + DDR3 register values.
 */

#ifndef __T41_H__
#define __T41_H__

/* APB bus peripherals */
#define CPM_BASE	0xb0000000
#define TCU_BASE	0xb0002000
#define WDT_BASE	0xb0002000
#define RTC_BASE	0xb0003000
#define GPIO_BASE	0xb0010000
#define AIC_BASE	0xb0020000
#define I2C0_BASE	0xb0050000
#define I2C1_BASE	0xb0051000
#define I2C2_BASE	0xb0052000
#define I2C3_BASE	0xb0053000

/* UART (4 channels, stride 0x1000) */
#define UART0_BASE	0xb0030000
#define UART1_BASE	0xb0031000
#define UART2_BASE	0xb0032000
#define UART3_BASE	0xb0033000
#define UART_BASE	UART0_BASE
#define UART_STEP	0x1000

/* AHB0 bus peripherals */
#define G_OST_BASE	0xb2000000
#define CCU_BASE	0xb2200000
#define INTCN_BASE	0xb2300000

/* AHB2 bus peripherals */
#define MSC0_BASE	0xb3060000
#define MSC1_BASE	0xb3070000
#define DDR_PHY_BASE	0xb3011000
#define NEMC_BASE	0xb3410000
#define PDMA_BASE	0xb3420000
#define SFC_BASE	0xb3440000
#define GMAC_BASE	0xb34b0000
#define DDRC_BASE	0xb34f0000
#define OTG_BASE	0xb3500000
#define EFUSE_BASE	0xb3540000

#define DDR_PHY_OFFSET	(-0x4e0000 + 0x1000)

/* Console UART index. Vendor isvp_t41.h sets CONFIG_SYS_UART_INDEX=1
 * and routes UART1 to PB23 TX / PB24 RX FUNC 0 - same pad pair as T40
 * and the one wired to the lab FTDI on stock T41NQ dev boards.
 */
#define T41_CONSOLE_UART	1

/*
 * SoC identification (CPM_CPSPPR / EFUSE chipid path).
 * Vendor U-Boot reads this at boot to confirm T41 silicon.
 */
#define T41_SOCID_ADDR		(EFUSE_BASE + 0x200)

/* CPM register offsets (same T40-style reshuffle vs A1) */
#define CPM_CPCCR	0x00
#define CPM_LCR		0x04
#define CPM_RSR		0x08
#define CPM_CPPCR	0x0c
#define CPM_CPAPCR	0x10	/* APLL */
#define CPM_CPMPCR	0x14	/* MPLL */
#define CPM_CPAPACR	0x18
#define CPM_CPMPACR	0x1c
#define CPM_CLKGR0	0x20
#define CPM_OPCR	0x24
#define CPM_CLKGR1	0x28
#define CPM_DDRCDR	0x2c
#define CPM_EL150CDR	0x30
#define CPM_CPSPR	0x34
#define CPM_CPSPPR	0x38
#define CPM_USBPCR	0x3c
#define CPM_USBRDT	0x40
#define CPM_USBVBFIL	0x44
#define CPM_USBPCR1	0x48
#define CPM_RSACDR	0x4c
#define CPM_MACCDR	0x54
#define CPM_CPEPCR	0x58	/* EPLL */
#define CPM_CPEPACR	0x5c
#define CPM_SFCCDR	0x60
#define CPM_LPCDR	0x64
#define CPM_MSC0CDR	0x68
#define CPM_MSC1CDR	0x6c
#define CPM_I2STCDR	0x70
#define CPM_SSICDR	0x74
#define CPM_I2STCDR1	0x78
#define CPM_I2S1CDR	0x7c
#define CPM_ISPCDR	0x80
#define CPM_SRBC0	0xc4
#define CPM_DRCG	0xd0
#define CPM_CPCSR	0xd4
#define CPM_CPVPCR	0xe0	/* VPLL */
#define CPM_CPVPACR	0xe4
#define CPM_GMACPHYC	0xe8
#define CPM_MESTSEL	0xec

/* CLKGR0 gate bits (set = clock disabled) */
#define CPM_CLKGR0_DDR		BIT(31)
#define CPM_CLKGR0_TCU		BIT(30)
#define CPM_CLKGR0_DES		BIT(28)
#define CPM_CLKGR0_RSA		BIT(27)
#define CPM_CLKGR0_MIPI_CSI	BIT(25)
#define CPM_CLKGR0_LCD		BIT(24)
#define CPM_CLKGR0_ISP		BIT(23)
#define CPM_CLKGR0_PDMA	BIT(22)
#define CPM_CLKGR0_SFC		BIT(21)
#define CPM_CLKGR0_SSI1	BIT(20)
#define CPM_CLKGR0_SC_HASH	BIT(19)
#define CPM_CLKGR0_SSI_SLV	BIT(18)
#define CPM_CLKGR0_UART3	BIT(17)
#define CPM_CLKGR0_UART2	BIT(16)
#define CPM_CLKGR0_UART1	BIT(15)
#define CPM_CLKGR0_UART0	BIT(14)
#define CPM_CLKGR0_SADC	BIT(13)
#define CPM_CLKGR0_AIC		BIT(11)
#define CPM_CLKGR0_I2C3	BIT(10)
#define CPM_CLKGR0_I2C2	BIT(9)
#define CPM_CLKGR0_I2C1	BIT(8)
#define CPM_CLKGR0_I2C0	BIT(7)
#define CPM_CLKGR0_SSI0	BIT(6)
#define CPM_CLKGR0_MSC1	BIT(5)
#define CPM_CLKGR0_MSC0	BIT(4)
#define CPM_CLKGR0_OTG		BIT(3)

/* USB OTG PHY control (CPM); single OTG core */
#define USBPCR_USB_MODE_ORG	BIT(31)
#define USBPCR_IDPULLUP_MASK	(0x3u << 28)
#define USBPCR_COMMONONN	BIT(25)
#define USBPCR_VBUSVLDEXT	BIT(24)
#define USBPCR_VBUSVLDEXTSEL	BIT(23)
#define USBPCR_POR		BIT(22)
#define USBPCR_SIDDQ		BIT(21)
#define USBPCR_OTG_DISABLE	BIT(20)

#define USBRDT_UTMI_RST		BIT(27)
#define USBRDT_VBFIL_LD_EN	BIT(25)

#define SRBC_USB_SR		BIT(12)
#define OPCR_SPENDN0		BIT(7)

/* PLL control register layout (CPAPCR/CPMPCR/CPEPCR/CPVPCR) */
#define PLL_PLLEN	BIT(0)
#define PLL_PLLON	BIT(3)

/* GPIO port register offsets, port stride 0x1000 (PA..PD) */
#define GPIO_PXPIN(n)	(0x00 + (n) * 0x100)
#define GPIO_PXINT(n)	(0x10 + (n) * 0x100)
#define GPIO_PXINTS(n)	(0x14 + (n) * 0x100)
#define GPIO_PXINTC(n)	(0x18 + (n) * 0x100)
#define GPIO_PXMSK(n)	(0x20 + (n) * 0x100)
#define GPIO_PXMSKS(n)	(0x24 + (n) * 0x100)
#define GPIO_PXMSKC(n)	(0x28 + (n) * 0x100)
#define GPIO_PXPAT1(n)	(0x30 + (n) * 0x100)
#define GPIO_PXPAT1S(n)	(0x34 + (n) * 0x100)
#define GPIO_PXPAT1C(n)	(0x38 + (n) * 0x100)
#define GPIO_PXPAT0(n)	(0x40 + (n) * 0x100)
#define GPIO_PXPAT0S(n)	(0x44 + (n) * 0x100)
#define GPIO_PXPAT0C(n)	(0x48 + (n) * 0x100)

#endif /* __T41_H__ */
