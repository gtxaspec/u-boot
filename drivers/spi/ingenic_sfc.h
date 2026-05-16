/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic SFC (SPI Flash Controller) register definitions.
 *
 * Copyright (C) 2024 Ingenic Semiconductor Co.,Ltd.
 * Copyright (C) 2026 Alfonso Gamboa <gtxent@gmail.com>
 *
 * Ported from the vendor U-Boot 2022.10 SFC SPI-MEM driver to mainline
 * driver-model. The register layout is taken verbatim from the vendor
 * header; the only change is that the controller base address now comes
 * from the device tree instead of a hard-coded SoC base.
 */

#ifndef _SPI_INGENIC_SFC_H
#define _SPI_INGENIC_SFC_H

#include <linux/types.h>

/* SFC register offsets */
#define SFC_GLB0			0x0000
#define SFC_DEV_CONF			0x0004
#define SFC_DEV_STA_EXP			0x0008
#define SFC_DEV_STA_RT			0x000c
#define SFC_DEV_STA_MSK			0x0010
#define SFC_TRAN_CONF0(n)		(0x0014 + ((n) * 4))
#define SFC_TRAN_LEN			0x002c
#define SFC_DEV_ADDR(n)			(0x0030 + ((n) * 4))
#define SFC_DEV_ADDR_PLUS(n)		(0x0048 + ((n) * 4))
#define SFC_MEM_ADDR			0x0060
#define SFC_TRIG			0x0064
#define SFC_SR				0x0068
#define SFC_SCR				0x006c
#define SFC_INTC			0x0070
#define SFC_FSM				0x0074
#define SFC_CGE				0x0078
#define SFC_CMD_IDX			0x007c
#define SFC_COL_ADDR			0x0080
#define SFC_ROW_ADDR			0x0084
#define SFC_STA_ADDR0			0x0088
#define SFC_STA_ADDR1			0x008c
#define SFC_DES_ADDR			0x0090
#define SFC_GLB1			0x0094
#define SFC_DEV1_STA_RT			0x0098
#define SFC_TRAN_CONF1(n)		(0x009c + ((n) * 4))
#define SFC_CDT				0x0800
#define SFC_RM_DR			0x1000

/* SFC_TRAN_CONF0 */
#define TRAN_CONF0_FMAT			BIT(23)

/* SFC_TRAN_CONF1 */
#define TRAN_CONF1_TRAN_MODE_OFFSET	4
#define TRAN_CONF1_TRAN_MODE_MSK	(0xf << TRAN_CONF1_TRAN_MODE_OFFSET)

/* SFC_TRIG */
#define TRIG_FLUSH			BIT(2)
#define TRIG_STOP			BIT(1)
#define TRIG_START			BIT(0)

/* SFC_SR */
#define BUSY_OFFSET			5
#define BUSY_MSK			(0x3 << BUSY_OFFSET)
#define SR_END				BIT(4)
#define TRAN_REQ			BIT(3)
#define RECE_REQ			BIT(2)
#define SR_OVER				BIT(1)
#define SR_UNDER			BIT(0)

/* SFC_SCR */
#define CLR_END				BIT(4)
#define CLR_TREQ			BIT(3)
#define CLR_RREQ			BIT(2)
#define CLR_OVER			BIT(1)
#define CLR_UNDER			BIT(0)

/* SFC_INTC */
#define MASK_END			BIT(4)
#define MASK_TREQ			BIT(3)
#define MASK_RREQ			BIT(2)
#define MASK_OVER			BIT(1)
#define MASK_UNDR			BIT(0)

/* Transfer modes (SFC_TRAN_CONF1[7:4]) */
#define TM_STD_SPI			0
#define TM_DI_DO_SPI			1
#define TM_DIO_SPI			2
#define TM_FULL_DIO_SPI			3
#define TM_QI_QO_SPI			5
#define TM_QIO_SPI			6
#define TM_FULL_QIO_SPI			7

/* FIFO threshold, in 32-bit words */
#define THRESHOLD			32

#define SFC_SUPPORTED_CS		0	/* CS0 only */

#define SFC_DEFAULT_FREQ		24000000	/* 24 MHz */

/* SFC controller private data */
struct sfc_priv {
	struct udevice *dev;
	void __iomem *base;
	struct clk clk;
	u32 max_freq;
	u32 mode;
};

/*
 * Bit-field register overlay. Matches the vendor sfc_reg_t so the
 * ported register sequence stays byte-for-byte equivalent.
 */
typedef union sfc_reg {
	u32 b32;

	struct {
		unsigned burst_md : 2;
		unsigned wp_en : 1;
		unsigned phase_num : 3;
		unsigned op_mode : 1;
		unsigned threshold : 6;
		unsigned tran_dir : 1;
		unsigned cdt_en : 1;
		unsigned des_en : 1;
		unsigned poll_time : 16;
	} sfc_glb0;		/* 0x00 */

	struct {
		unsigned chip_sel : 2;
		unsigned dqs_en : 1;
		unsigned reserved2 : 29;
	} sfc_glb1;		/* 0x94 */

	struct {
		unsigned wp_dl : 1;
		unsigned hold_dl : 1;
		unsigned ce_dl : 1;
		unsigned cpol : 1;
		unsigned cpha : 1;
		unsigned tsh : 4;
		unsigned tsetup : 2;
		unsigned thold : 2;
		unsigned sta_type : 2;
		unsigned cmd_type : 1;
		unsigned smp_delay : 5;
		unsigned reserved : 10;
		unsigned sta_endian : 1;
	} sfc_dev_conf;		/* 0x04 */

	struct {
		unsigned cmd : 16;
		unsigned data_en : 1;
		unsigned dmy_bits : 6;
		unsigned phase_format : 1;
		unsigned cmd_en : 1;
		unsigned poll_en : 1;
		unsigned addr_width : 3;
		unsigned clk_mode : 3;
	} sfc_tran_cfg0;	/* 0x14-0x28 */

	struct {
		unsigned reserved1 : 4;
		unsigned tran_md : 4;
		unsigned reserved2 : 8;
		unsigned word_unit : 2;
		unsigned data_endian : 1;
		unsigned reserved3 : 13;
	} sfc_tran_cfg1;	/* 0x9C-0xB0 */
} sfc_reg_t;

#endif /* _SPI_INGENIC_SFC_H */
