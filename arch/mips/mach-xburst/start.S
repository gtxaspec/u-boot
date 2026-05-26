/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Startup code for Ingenic XBurst1/XBurst2 T-series SPL
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/cacheops.h>
#include <asm/cache.h>
#if defined(CONFIG_SOC_A1)
#include <mach/a1.h>
#elif defined(CONFIG_SOC_T40)
#include <mach/t40.h>
#else
#include <mach/t31.h>
#endif

	.set noreorder

	.globl _start
	.text
_start:
#ifdef CONFIG_XPL_BUILD
	/*
	 * On-chip mask ROM SFC (SPI flash) boot header. The mask ROM reads
	 * these words from offset 0 of the SPL image to learn the flash
	 * geometry, copies the SPL into SRAM and enters at the fixed offset
	 * below. Layout (3 words + padding to 0x800) matches the T31 mask
	 * ROM contract; the SPI-NAND variant differs and is added with the
	 * SFC driver.
	 *
	 * TODO: validate header/entry offset against the T31 bootrom
	 * decompilation and on real hardware during board bring-up.
	 */
	.word 0x03040506
	.word 0x55aa5502
#if defined(CONFIG_SOC_T32) || defined(CONFIG_SOC_T33)
	/*
	 * Vendor U-Boot 2022.10 PRJ/start.S SFC_NOR header (T32 = PRJ007,
	 * T33 = PRJ008). T32/T33 bootroms require 0xffff00aa here; the
	 * older T10..T31 bootroms accept 0x000000aa, which is why this
	 * is gated. Layout is 4 .word (16 bytes) + 496-byte pad + 1536-
	 * byte reserved = 2048-byte header, then SPL code at 0x800.
	 */
	.word 0xffff00aa
	.word 0x00000000
	.space 496, 0
#else
	.word 0x000000aa		/* SPI NOR */
	.space 500, 0
#endif
	.space 1536, 0

#if defined(CONFIG_SOC_T32)
	/*
	 * T32/PRJ007 GPIO bring-up - faithful port of vendor
	 * arch/mips/mach-xburst/PRJ/start.S init_gpio_PRJ007 (U-Boot
	 * 2022.10 T32-2.1.0). Required before the C-level UART pinmux:
	 * sets Schmitt-trigger enables on PA/PB/PC active pads and
	 * programs per-pin drive strength (4 mA / 8 mA). Without these
	 * the UART1 TX pad does not drive the line, so the SPL boots
	 * and the printf path runs but no character ever appears on
	 * the console. Values are GPIO-controller bit masks; do not
	 * rearrange - order matches vendor.
	 */
	/* PA10~PA28 Schmitt */
	li	t8, 0x1ffffc00
	li	t9, 0xb0010164
	sw	t8, 0(t9)
	/* PB00~PB11 PB13~PB31 Schmitt */
	li	t8, 0xffffefff
	li	t9, 0xb0011164
	sw	t8, 0(t9)
	/* PC00~PC09 PC11~PC29 Schmitt */
	li	t8, 0x3ffffbff
	li	t9, 0xb0012164
	sw	t8, 0(t9)
	/* PA10~PA15 PA18~PA22 4 mA */
	li	t8, 0x55500000
	li	t9, 0xb0010134
	sw	t8, 0(t9)
	li	t8, 0x00001550
	li	t9, 0xb0010144
	sw	t8, 0(t9)
	/* PA16~PA17 PA23~PA28 8 mA */
	li	t8, 0x02aa800a
	li	t9, 0xb0010144
	sw	t8, 0(t9)
	/* PB00~PB03 PB05~PB06 PB08~PB11 PB13~PB15 4 mA */
	li	t8, 0x54551455
	li	t9, 0xb0011134
	sw	t8, 0(t9)
	/* PB04 PB07 8 mA */
	li	t8, 0x00008200
	li	t9, 0xb0011134
	sw	t8, 0(t9)
	/* PB16~PB31 4 mA (covers UART1 PB23/PB24) */
	li	t8, 0x55555555
	li	t9, 0xb0011144
	sw	t8, 0(t9)
	/* PC00~PC01 PC03~PC09 PC12~PC15 4 mA */
	li	t8, 0x55055545
	li	t9, 0xb0012134
	sw	t8, 0(t9)
	/* PC02 PC11 8 mA */
	li	t8, 0x00020020
	li	t9, 0xb0012134
	sw	t8, 0(t9)
	/* PC16~PC29 4 mA */
	li	t8, 0x05555555
	li	t9, 0xb0012144
	sw	t8, 0(t9)
#elif defined(CONFIG_SOC_A1) || defined(CONFIG_SOC_T40)
	/*
	 * A1 / T40 (XBurst2): GPIO drive strength / Schmitt trigger
	 * setup is done in C code (soc.c board_init_f) since it is
	 * not pad-critical for the default UART pad routing. If
	 * board-specific drive-strength tuning is needed it can be
	 * added here per SoC.
	 */
#elif !defined(CONFIG_SOC_T33)
	/*
	 * T10..T31 boot pin mux / drive-strength setup, required
	 * before the console: the mask ROM does not mux the UART (and
	 * other) pads, so without this the SPL runs but produces no
	 * UART output on real silicon. Faithful transliteration of
	 * the vendor T31 SPL start.S (isvp_t31_sfcnor_ddr128M,
	 * CONFIG_NET_GMAC path) - do not remove or "tidy"; order and
	 * values are board-critical.
	 */
	la	t8, 0xffffffff
	la	t9, 0xb0010138
	sw	t8, 0(t9)
	la	t8, 0xffffffff
	la	t9, 0xb0010148
	sw	t8, 0(t9)
	la	t8, 0x55555555
	la	t9, 0xb0010134
	sw	t8, 0(t9)
	la	t8, 0x55555555
	la	t9, 0xb0010144
	sw	t8, 0(t9)
	la	t8, 0xFCFFF000
	la	t9, 0xb0011138
	sw	t8, 0(t9)
	la	t8, 0x55559555
	la	t9, 0xb0011134
	sw	t8, 0(t9)
	la	t8, 0x55555555
	la	t9, 0xb0011144
	sw	t8, 0(t9)
#endif

	/*
	 * Invalidate BTB. T40 vendor SPL skips this; cold-boot probe
	 * 2026-05-26 also tries skipping to match vendor exactly in
	 * case CP0 Config7 RMW is the cold-boot wedge.
	 */
#if !defined(CONFIG_SOC_T40)
	mfc0	t0, CP0_CONFIG, 7
	nop
	ori	t0, 2
	mtc0	t0, CP0_CONFIG, 7
	nop
#endif

#if !defined(CONFIG_SOC_T32) && !defined(CONFIG_SOC_T33) && \
    !defined(CONFIG_SOC_A1) && !defined(CONFIG_SOC_T40)
	/*
	 * XBurst1 CP0 $12,2: clear bit 31 (faithful to vendor T31
	 * start.S). Vendor T32/T33 and the XBurst2 SoCs (A1, T40)
	 * do NOT touch this register - the bit is reserved or
	 * repurposed and writing to it can hang the SPL.
	 */
	mfc0	t0, $12, 2
	nop
	li	t1, 0x7fffffff
	and	t0, t0, t1
	mtc0	t0, $12, 2
	nop
#endif

	/* CU0=UM=EXL=IE=0, BEV=ERL=1, IP2~7=1 */
	li	t0, 0x0040FC04
	mtc0	t0, CP0_STATUS

	/* CAUSE: IV=1, use the special interrupt vector (0x200).
	 * T40 vendor SPL does this as a RMW (preserves other CAUSE
	 * bits); other ports overwrite. Match vendor on T40. */
#if defined(CONFIG_SOC_T40)
	mfc0	t1, CP0_CAUSE
	nop
	lui	t2, 0x80
	or	t1, t1, t2
	mtc0	t1, CP0_CAUSE
#else
	li	t1, 0x00800000
	mtc0	t1, CP0_CAUSE
#endif

	/*
	 * Stack setup. XBurst1 (T10..T33) and the A1 USB-boot SPL keep
	 * the bootrom-provided sp: the SPL runs as a normal nested call,
	 * and a plain board_init_f return (jr ra) resumes the mask ROM
	 * cleanly. The A1 NOR-boot SPL switches to its own larger stack -
	 * the bootrom sp has too little headroom for the LZMA decompress.
	 */
#if defined(CONFIG_SOC_A1) && !defined(CONFIG_SPL_A1_USB_BOOT)
	lui	sp, 0x8000
	ori	sp, sp, 0x8000
#endif
	/*
	 * XBurst2 (A1) requires explicit sp init for NOR boot - the bootrom
	 * does not leave a usable stack pointer like XBurst1 does. Stack
	 * top = top of the 32 KB cache-as-SRAM region; BSS + stack must
	 * fit below this in SRAM (per the A1 stage-1 bring-up commit).
	 *
	 * T40 vendor SPL does NOT touch SP at entry - it keeps the
	 * bootrom-provided stack pointer. Match vendor behaviour: do not
	 * overwrite SP on T40 SFC NOR boot. (USB-boot SPL also keeps
	 * bootrom SP, and was already not overwriting it.)
	 */

#if defined(CONFIG_SOC_T40) && defined(CONFIG_SPL_T40_NOR_PROBE)
	/*
	 * Diagnostic probe for T40 SFC NOR cold-boot bring-up.
	 * Reached AFTER the BTB invalidate / CP0 status / SP setup;
	 * if 'A' shows up here, bootrom + the standard MIPS entry
	 * sequence (BTB + CP0 + SP) all work. If 'A' is silent we
	 * have to move this probe earlier (before BTB) to isolate.
	 *
	 * Mirrors soc.c board_init_f: WDT disable, CCU +0xfe0 |= 0x78
	 * (IFU simple loop disable), CCU +0x060 |= 0x10 (L1 prefetcher
	 * trust disable), UART1 clk-ungate, GPIO PB23/24 -> FUNC0,
	 * UART1 setup, spin 'A'.
	 */
	/* WDT disable: write 0 to TCER (WDT enable register), but the
	 * actual reg is at b00200c+4 - the WDT register block is at
	 * 0xb0002000+0x40. Per t40.h WDT_BASE = 0xb0002000, +0x04 is
	 * WDT_TDR / WDT enable on T-series. */
	li	t0, 0xb0002000
	sw	zero, 0x04(t0)
	/* XBurst2 CCU tweaks (required for SPL to run on real silicon)
	 *   CCU +0xfe0 |= 0x78  disable IFU simple loop
	 *   CCU +0x060 |= 0x10  disable L1 prefetcher trust
	 * CCU_BASE = 0xb2200000.
	 */
	li	t0, 0xb2200000
	lw	t1, 0xfe0(t0)
	ori	t1, t1, 0x78
	sw	t1, 0xfe0(t0)
	lw	t1, 0x060(t0)
	ori	t1, t1, 0x10
	sw	t1, 0x060(t0)
	/* Ungate UART1 clock: CPM_CLKGR0 (0xb0000020) bit 15 clear
	 * (T40 UART1 = BIT(15), see mach/t40.h CPM_CLKGR0_UART1)
	 */
	li	t0, 0xb0000020
	lw	t1, 0(t0)
	li	t2, ~(1 << 15)
	and	t1, t1, t2
	sw	t1, 0(t0)
	/* GPIO PB23 / PB24 to device FUNC0:
	 *   PXINTC (clear INT)  : 0xb0011018 |= bit
	 *   PXMSKC (clear MSK)  : 0xb0011028 |= bit  (-> device function)
	 *   PXPAT1C (clear PAT1): 0xb0011038 |= bit  (-> FUNC0/1 low half)
	 *   PXPAT0C (clear PAT0): 0xb0011048 |= bit  (-> FUNC 0)
	 *   PXPUENC (pull off)  : 0xb0011118 |= bit
	 *   PXPDENC (pull off)  : 0xb0011128 |= bit
	 * Pin bits: PB23 = 1<<23, PB24 = 1<<24 (mask 0x01800000)
	 */
	li	t1, 0x01800000
	li	t0, 0xb0011018
	sw	t1, 0(t0)
	li	t0, 0xb0011028
	sw	t1, 0(t0)
	li	t0, 0xb0011038
	sw	t1, 0(t0)
	li	t0, 0xb0011048
	sw	t1, 0(t0)
	li	t0, 0xb0011118
	sw	t1, 0(t0)
	li	t0, 0xb0011128
	sw	t1, 0(t0)
	/* UART1 @ 0xb0031000, ns16550-compat reg-shift=2.
	 * Mirrors drivers/.../t40/serial.c t40_spl_serial_init:
	 *   IER=0, LCR=8N1 (0x03), then LCR|DLAB (0x83), DLH=0, DLL=13,
	 *   LCR=8N1, FCR=UUE|FE|TFLS|RFLS (0x17).
	 * UUE (FCR bit 4) is the UART unit enable - without it, no TX.
	 */
	li	t0, 0xb0031000
	sb	zero, 0x04(t0)		/* IER = 0  */
	li	t1, 0x03
	sb	t1, 0x0c(t0)		/* LCR = 8N1 */
	li	t1, 0x83
	sb	t1, 0x0c(t0)		/* LCR |= DLAB */
	sb	zero, 0x04(t0)		/* DLH = 0 */
	li	t1, 13
	sb	t1, 0x00(t0)		/* DLL = 13 (24 MHz / 16 / 115200) */
	li	t1, 0x03
	sb	t1, 0x0c(t0)		/* LCR = 8N1 (clear DLAB) */
	li	t1, 0x17
	sb	t1, 0x08(t0)		/* FCR = UUE|FE|TFLS|RFLS */
	/* Spin emit 'A' (with brief inter-byte delay so the TX FIFO
	 * has time to drain at 115200). */
1:	li	t1, 'A'
	sb	t1, 0x00(t0)
	li	t2, 0x100000
2:	addiu	t2, t2, -1
	bne	t2, zero, 2b
	 nop
	b	1b
	 nop
#endif

	b	board_init_f
	 nop

	.globl enable_caches
	.ent enable_caches
enable_caches:
	mtc0	zero, CP0_TAGLO
	mtc0	zero, CP0_TAGHI

	li	t0, KSEG0
	addu	t1, t0, CONFIG_SYS_DCACHE_SIZE
1:
	cache	INDEX_STORE_TAG_D, 0(t0)
	bne	t0, t1, 1b
	 addiu	t0, t0, CONFIG_SYS_CACHELINE_SIZE

	li	t0, KSEG0
	addu	t1, t0, CONFIG_SYS_ICACHE_SIZE
2:
	cache	INDEX_STORE_TAG_I, 0(t0)
	bne	t0, t1, 2b
	 addiu	t0, t0, CONFIG_SYS_CACHELINE_SIZE

	/* Invalidate BTB */
	mfc0	t0, CP0_CONFIG, 7
	nop
	ori	t0, 2
	mtc0	t0, CP0_CONFIG, 7
	nop

	/* Enable caches: cacheable, noncoherent */
	li	t0, CONF_CM_CACHABLE_NONCOHERENT
	mtc0	t0, CP0_CONFIG
	nop

	jr	ra
	 nop
	.end enable_caches
#endif /* CONFIG_XPL_BUILD */
