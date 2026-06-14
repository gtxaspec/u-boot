/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T20 SFC (SPI-NOR controller) register map
 *
 * The SFC block is identical to T31/T23's (same SFC_BASE 0xb3440000
 * from <mach/t20.h>, same register offsets and bit fields).
 * Reproduced byte-for-byte from the vendor arch-t20/sfc.h; only the
 * defs the lean SPL NOR loader needs are kept (do not "clean up").
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T20_SFC_H__
#define __T20_SFC_H__

/* SFC register offsets (from vendor arch-t20/sfc.h) */
#define SFC_GLB			(0x0000)
#define SFC_DEV_CONF		(0x0004)
#define SFC_TRAN_CONF(n)	(0x0014 + (n * 4))
#define SFC_DEV_ADDR(n)		(0x0030 + (n * 4))
#define SFC_DEV_ADDR_PLUS(n)	(0x0048 + (n * 4))
#define SFC_TRAN_LEN		(0x002c)
#define SFC_TRIG		(0x0064)
#define SFC_SR			(0x0068)
#define SFC_SCR			(0x006c)
#define SFC_CGE			(0x0078)
#define SFC_DR			(0x1000)

/* For SFC_GLB */
#define THRESHOLD_OFFSET	(7)
#define THRESHOLD_MSK		(0x3f << THRESHOLD_OFFSET)
#define PHASE_NUM_OFFSET	(3)
#define PHASE_NUM_MSK		(0x7 << PHASE_NUM_OFFSET)

/* For SFC_DEV_CONF */
#define CEDL			(1 << 2)
#define HOLDDL			(1 << 1)
#define WPDL			(1 << 0)

/* For SFC_TRAN_CONFx */
#define TRAN_MODE_OFFSET	(29)
#define TRAN_MODE_MSK		(0x7 << TRAN_MODE_OFFSET)
#define ADDR_WIDTH_OFFSET	(26)
#define ADDR_WIDTH_MSK		(0x7 << ADDR_WIDTH_OFFSET)
#define CMD_EN			(1 << 24)
#define TRAN_CONF_DMYBITS_OFFSET	(17)
#define DMYBITS_OFFSET		(17)
#define TRAN_CONF_DMYBITS_MSK	(0x3f << DMYBITS_OFFSET)
#define DATEEN			(1 << 16)
#define CMD_OFFSET		(0)

/* For SFC_TRIG */
#define FLUSH			(1 << 2)
#define STOP			(1 << 1)
#define START			(1 << 0)

/*
 * For SFC_SR. The vendor names the transfer-end bit END; that token
 * collides with the MIPS asm.h END(function) assembler macro in the
 * mainline tree, so it is spelled SFC_SR_END here. The value (1 << 4)
 * is identical to the vendor define.
 */
#define SFC_SR_END		(1 << 4)
#define RECE_REQ		(1 << 2)

/* For SFC_SCR */
#define CLR_END			(1 << 4)
#define CLR_RREQ		(1 << 2)

#define SFC_FIFO_LEN		(63)
#define THRESHOLD		(31)

/* NOR fast-read command (vendor spi.h CMD_READ) */
#define CMD_READ		(0x03)

/*
 * The T20-generation mask ROM loads only the first 0x6800 bytes of the
 * SPL payload from NOR (sized for the 32 KB cache-as-RAM), regardless of
 * the header length - the same cap T21/T30 carry. The old sub-22 KB
 * imperative SPL fit; the DM-in-SPL image does not, so soc.c re-reads the
 * missing tail via t20_spl_self_complete().
 */
#define T20_ROM_SPL_LOAD	0x6800

#endif /* __T20_SFC_H__ */
