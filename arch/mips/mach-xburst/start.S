/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Startup code for Ingenic XBurst1 T-series (T31) SPL
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/cacheops.h>
#include <asm/cache.h>
#include <mach/t31.h>

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
	.word 0x000000aa		/* SPI NOR */
	.space 500, 0
	.space 1536, 0

	/* Invalidate BTB */
	mfc0	t0, CP0_CONFIG, 7
	nop
	ori	t0, 2
	mtc0	t0, CP0_CONFIG, 7
	nop

	/* CU0=UM=EXL=IE=0, BEV=ERL=1, IP2~7=1 */
	li	t0, 0x0040FC04
	mtc0	t0, CP0_STATUS

	/* CAUSE: IV=1, use the special interrupt vector (0x200) */
	li	t1, 0x00800000
	mtc0	t1, CP0_CAUSE

	/* Set up the stack */
	li	sp, CONFIG_SPL_STACK

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
