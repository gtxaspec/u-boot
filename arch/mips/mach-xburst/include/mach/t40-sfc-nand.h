/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Ingenic T40 SFC SPI-NAND defines for the SPL loader.
 *
 * Faithful copies of the constants from the vendor headers
 * arch/mips/include/asm/arch-t40/spi_nand.h and ditto sfc.h.
 *
 * Copyright (c) 2016 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __T40_SFC_NAND_H__
#define __T40_SFC_NAND_H__

#include <linux/types.h>

/* SPI-NAND opcodes */
#define CMD_PARD			0x13	/* page read (cell -> cache) */
#define CMD_FR_CACHE			0x0b	/* fast read from cache */
#define CMD_FR_CACHE_QUAD		0x6b	/* fast read from cache, quad I/O */
#define CMD_GET_FEATURE			0x0f
#define CMD_SET_FEATURE			0x1f
#define CMD_RDID			0x9f

/* SPI-NAND feature register addresses */
#define FEATURE_REG_PROTECT		0xa0
#define FEATURE_REG_FEATURE1		0xb0
#define FEATURE_REG_STATUS1		0xc0
#define FEATURE_REG_FEATURE2		0xd0
#define FEATURE_REG_STATUS2		0xf0

/* Feature register bits */
#define BITS_ECC_EN			(1 << 4)
#define BITS_QUAD_EN			(1 << 0)
#define BITS_BUF_EN			(1 << 3)	/* Winbond only */

/* SFC TRAN_CONF1 tran_mode encodings */
#define TRAN_CONF1_SPI_STANDARD		0x0
#define TRAN_CONF1_SPI_QUAD		0x5

/* SFC register additions beyond t31-sfc.h */
#define SFC_TRAN_CONF1(n)		(0x009c + (n * 4))
#define TRAN_CONF1_TRAN_MODE_OFFSET	(4)
#define TRAN_CONF1_TRAN_MODE_MSK	(0xf << TRAN_CONF1_TRAN_MODE_OFFSET)
#define GLB_TRAN_DIR_OFFSET		(13)
#define DEV_CONF_SMP_DELAY_OFFSET	(16)
#define DEV_CONF_SMP_DELAY_MSK		(0x1f << DEV_CONF_SMP_DELAY_OFFSET)

/* SFC tranconf register layout for SPL command builder. */
typedef union sfc_tranconf_r {
	u32 d32;
	struct {
		unsigned cmd:16;
		unsigned data_en:1;
		unsigned dmy_bits:6;
		unsigned phase_format:1;
		unsigned cmd_en:1;
		unsigned poll_en:1;
		unsigned addr_width:3;
		unsigned tran_mode:3;
	} reg;
} sfc_tranconf_r;

struct jz_sfc {
	sfc_tranconf_r tranconf;
	u32 addr;
	u32 len;
	u32 addr_plus;
};

/*
 * Per-chip parameters mirrored from the vendor host tool output
 * (tools/ingenic-tools/nand_device/*.c -> sfc_nand_params.h):
 *
 *   pagesize        : 2048 for current Micron/Winbond chips
 *   id_manufactory  : SPI-NAND vendor byte
 *   device_id       : SPI-NAND device byte
 *   addrlen         : column-address bytes for FR_CACHE (1 or 2)
 *   ecc_bit         : start bit of the ECC-status field in STATUS1
 *   bit_counts      : width of the ECC-status field
 *   eccstat_count   : count of values in eccerrstatus[] that mean fail
 *   eccerrstatus[]  : status values that mean ECC uncorrectable
 */
struct spl_nand_param {
	u32 pagesize:16;
	u32 id_manufactory:8;
	u32 device_id:8;

	u32 addrlen:2;
	u32 ecc_bit:3;
	u32 bit_counts:3;

	u8 eccstat_count;
	u8 eccerrstatus[2];
} __attribute__((aligned(4)));

#endif /* __T40_SFC_NAND_H__ */
