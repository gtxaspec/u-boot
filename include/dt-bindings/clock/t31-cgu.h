/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T31 CGU clock IDs.
 *
 * U-Boot only consumes a small subset of the SoC clock tree (the
 * peripherals U-Boot drives), so this is intentionally a compact,
 * U-Boot-specific ID space rather than a mirror of Linux's full
 * dt-bindings/clock/ingenic-t31.h.
 */

#ifndef __DT_BINDINGS_CLOCK_T31_CGU_H__
#define __DT_BINDINGS_CLOCK_T31_CGU_H__

#define T31_CLK_EXT	0	/* 24 MHz external oscillator */
#define T31_CLK_RTC	1	/* 32.768 kHz RTC clock */
#define T31_CLK_APLL	2
#define T31_CLK_MPLL	3
#define T31_CLK_VPLL	4
#define T31_CLK_SFC	5	/* SPI-flash controller (SSI/SFC) */
#define T31_CLK_MSC0	6	/* SD/MMC 0 */
#define T31_CLK_MSC1	7	/* SD/MMC 1 */
#define T31_CLK_MAC	8	/* GMAC PHY clock (MACCDR) */
#define T31_CLK_UART1	9
#define T31_CLK_OTG	10	/* DWC2 OTG AHB gate */
#define T31_CLK_TCU	11	/* timer/counter unit */
#define T31_CLK_OST	12	/* OS timer */

#define T31_CLK_NR	13

#endif /* __DT_BINDINGS_CLOCK_T31_CGU_H__ */
