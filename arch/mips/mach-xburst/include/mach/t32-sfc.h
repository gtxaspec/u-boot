/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T32 SFC ("SFC2") SPI-NOR controller register map
 *
 * T32 (PRJ007) has the newer descriptor-capable SFC, NOT the simple
 * FIFO SFC of T10-T31: separate GLB0/GLB1, CDT, dedicated SFC0 clock
 * divider (CPM_SFC0CDR). The lean SPL NOR loader only uses the
 * CPU/FIFO (non-DMA, non-CDT) read path, so just those offsets and
 * bit fields are reproduced here, byte-for-byte from the vendor
 * U-Boot 2022.10 arch-PRJ/spl_sfc.h. The vendor uses packed bitfield
 * unions; this header gives explicit shifted masks instead (the
 * mainline / T31-port style - endianness-safe and review-safe).
 *
 * SFC_BASE comes from <mach/t32.h> (0xb3440000).
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T32_SFC_H__
#define __T32_SFC_H__

/* SFC2 register offsets */
#define SFC_GLB0		0x0000
#define SFC_DEV_CONF		0x0004
#define SFC_TRAN_CONF0(n)	(0x0014 + (n) * 4)
#define SFC_TRAN_LEN		0x002c
#define SFC_DEV_ADDR(n)		(0x0030 + (n) * 4)
#define SFC_DEV_ADDR_PLUS(n)	(0x0048 + (n) * 4)
#define SFC_TRIG		0x0064
#define SFC_SR			0x0068
#define SFC_SCR			0x006c
#define SFC_INTC		0x0070
#define SFC_CGE			0x0078
#define SFC_TRAN_CONF1(n)	(0x009c + (n) * 4)
#define SFC_RM_DR		0x1000

/* SFC_GLB0 */
#define GLB_TRAN_DIR		(1 << 13)	/* 0 = read */
#define GLB_OP_MODE		(1 << 6)	/* 0 = slave/CPU */
#define GLB_THRESHOLD_OFFSET	7
#define GLB_THRESHOLD_MSK	(0x3f << GLB_THRESHOLD_OFFSET)
#define GLB_WP_EN		(1 << 2)
#define GLB_PHASE_NUM_OFFSET	3
#define GLB_PHASE_NUM_MSK	(0x7 << GLB_PHASE_NUM_OFFSET)

/* SFC_DEV_CONF (low delay-line enables; the rest left at 0) */
#define DEV_CONF_CEDL		(1 << 2)
#define DEV_CONF_HOLDDL		(1 << 1)
#define DEV_CONF_WPDL		(1 << 0)

/* SFC_TRAN_CONF0(n) */
#define TRAN_CONF0_ADDR_WIDTH_OFFSET	26
#define TRAN_CONF0_ADDR_WIDTH_MSK	(0x7 << TRAN_CONF0_ADDR_WIDTH_OFFSET)
#define TRAN_CONF0_CMDEN		(1 << 24)
#define TRAN_CONF0_FMAT			(1 << 23)
#define TRAN_CONF0_DMYBITS_OFFSET	17
#define TRAN_CONF0_DMYBITS_MSK		(0x3f << TRAN_CONF0_DMYBITS_OFFSET)
#define TRAN_CONF0_DATEEN		(1 << 16)
#define TRAN_CONF0_CMD_OFFSET		0
#define TRAN_CONF0_CMD_MSK		(0xffff << TRAN_CONF0_CMD_OFFSET)

/* SFC_TRAN_CONF1(n) */
#define TRAN_CONF1_TRAN_MODE_OFFSET	4
#define TRAN_CONF1_TRAN_MODE_MSK	(0xf << TRAN_CONF1_TRAN_MODE_OFFSET)

/* SFC_TRIG */
#define TRIG_FLUSH		(1 << 2)
#define TRIG_STOP		(1 << 1)
#define TRIG_START		(1 << 0)

/* SFC_SR */
#define SR_BUSY_OFFSET		5
#define SR_BUSY_MSK		(0x3 << SR_BUSY_OFFSET)
#define SR_END			(1 << 4)
#define SR_RECE_REQ		(1 << 2)

/* SFC_SCR (write-1-clear) */
#define CLR_END			(1 << 4)
#define CLR_TREQ		(1 << 3)
#define CLR_RREQ		(1 << 2)
#define CLR_OVER		(1 << 1)
#define CLR_UNDER		(1 << 0)

/* SFC_INTC (mask bits) */
#define MASK_END		(1 << 4)
#define MASK_TREQ		(1 << 3)
#define MASK_RREQ		(1 << 2)
#define MASK_OVER		(1 << 1)
#define MASK_UNDR		(1 << 0)

#define SFC_THRESHOLD		32		/* FIFO threshold (words) */
#define SFC_NOR_CMD_READ	0x03		/* slow read, 3-byte addr */
#define SFC_NOR_ADDR_LEN	3

/*
 * SFC0 clock. Dedicated divider CPM_SFC0CDR (NOT the shared SSI
 * divider T31 uses). Vendor cgu_clk_sel[SFC0] = {.., CPM_SFC0CDR,
 * sel_bit 30, src MPLL, ce 29, busy 28, stop 27}. Bits [9:8] are an
 * extra source pre-divide select (1/2/4). Vendor sfc_init() targets
 * 25 MHz for the slow 0x03 read.
 */
#define CPM_SFC0CDR		0x58
#define SFC0_CGU_CE		29
#define SFC0_CGU_BUSY		28
#define SFC0_CGU_STOP		27
#define SFC0_INIT_RATE		25000000U

#endif /* __T32_SFC_H__ */
