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
#elif defined(CONFIG_SOC_T41)
#include <mach/t41.h>
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
#elif defined(CONFIG_SPL_T40_SFC_NAND_BOOT)
	/*
	 * T40 SFC SPI-NAND boot header.
	 *
	 * Byte 8 = 0x00: NOR/NAND flash-type discriminator the T40 mask
	 * ROM checks in FUN_bfc02070 (0xaa = SPI NOR, 0x00 = SPI NAND).
	 *
	 * Bytes 10-11 = NAND geometry hints the mask ROM reads in the
	 * NAND branch (bVar1=true): byte 10 << 5 -> _DAT_80000070 (OOB
	 * size, vendor 2 -> 64 B), byte 11 << 10 -> _DAT_80000074 (page
	 * size, vendor 2 -> 2048 B). Vendor SPL on 2-KiB-page SPI-NAND
	 * sets both to 2.
	 *
	 * Byte 9 = CRC, written by binman ingenic_t31_spl after CRC
	 * computation.
	 */
	.word 0x02020000		/* SPI NAND: byte 8=0x00, 10=2, 11=2 */
	.space 500, 0
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
#elif defined(CONFIG_SOC_A1) || defined(CONFIG_SOC_T40) || defined(CONFIG_SOC_T41)
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
#if !defined(CONFIG_SOC_T40) && !defined(CONFIG_SOC_T41)
	mfc0	t0, CP0_CONFIG, 7
	nop
	ori	t0, 2
	mtc0	t0, CP0_CONFIG, 7
	nop
#endif

#if !defined(CONFIG_SOC_T32) && !defined(CONFIG_SOC_T33) && \
    !defined(CONFIG_SOC_A1) && !defined(CONFIG_SOC_T40) && \
    !defined(CONFIG_SOC_T41)
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
#if !defined(CONFIG_SOC_T41)
	/* T41 vendor start.S does NOT touch CP0 - the bootrom-left state
	 * is used as-is. Writing CP0_STATUS breaks DDR PHY PLL lock. */
	li	t0, 0x0040FC04
	mtc0	t0, CP0_STATUS
#endif

	/* CAUSE: IV=1, use the special interrupt vector (0x200).
	 * T40 vendor SPL does this as a RMW (preserves other CAUSE
	 * bits); other ports overwrite. Match vendor on T40. */
#if defined(CONFIG_SOC_T40)
	mfc0	t1, CP0_CAUSE
	nop
	lui	t2, 0x80
	or	t1, t1, t2
	mtc0	t1, CP0_CAUSE
#elif !defined(CONFIG_SOC_T41)
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
