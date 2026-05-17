// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic SFC (SPI Flash Controller) SPI-MEM driver.
 *
 * Copyright (C) 2024 Ingenic Semiconductor Co.,Ltd.
 * Copyright (C) 2026 Alfonso Gamboa <gtxent@gmail.com>
 *
 * The SFC is a dedicated SPI-NOR controller found on the Ingenic
 * XBurst1 SoCs (T31 and friends). It speaks the cmd / cmd+data /
 * cmd+addr+data transfer formats and is driven through the SPI-MEM
 * framework.
 *
 * This is a mainline driver-model port of the vendor U-Boot 2022.10
 * driver. The register programming sequence is kept faithful to the
 * vendor implementation; the two functional changes are:
 *
 *   1. The controller base address comes from the device tree
 *      ("reg") instead of a compiled-in SoC base.
 *   2. The SFC source clock is configured by the SPL before U-Boot
 *      proper runs, so set_speed() is a no-op and the input rate is
 *      taken from the (fixed-)clock referenced by the DT.
 */

#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <spi.h>
#include <spi-mem.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include "ingenic_sfc.h"

#ifdef DEBUG
#define sfc_debug(fmt, args...)	printf(fmt, ##args)
#else
#define sfc_debug(fmt, args...)	debug(fmt, ##args)
#endif

#ifdef CONFIG_SOC_T31
/*
 * T31 SFC clock-gate + pin mux. A SPI-NOR boot has the mask ROM do
 * this (bootrom FUN_bfc01878); a USB boot does not, and the port has
 * no pinctrl driver, so without this the SFC has no functional clock
 * and its pins are unmuxed - the JEDEC read returns 0xff. Faithful
 * transliteration of the bootrom routine: ungate CPM_CLKGR0[20] and
 * put GPIO port-A pins 23-28 (CLK/CE/DR/DT/WP/HOLD, mask 0x1f800000)
 * into device function 1 (PAT1=0 via PAT1C, PAT0=1 via PAT0S). Safe
 * to re-run when a NOR boot already did it (idempotent).
 */
#define T31_CPM_BASE		0xb0000000
#define T31_CPM_CLKGR0		((void __iomem *)0xb0000020)
#define T31_CPM_CLKGR0_SFC	BIT(20)
#define T31_CPM_SSICDR		0x74		/* SSI/SFC clock divider */
#define SSICDR_SRC_MPLL		(1u << 30)	/* {APLL,MPLL,VPLL} idx 1 */
#define SSICDR_CE		BIT(28)
#define SSICDR_BUSY		BIT(27)
#define SSICDR_STOP		BIT(26)
#define T31_MPLL_HZ		1200000000u
#define T31_SSI_HZ		70000000u
#define T31_GPIO_PORTA_BASE	0xb0010000
#define G_PXINTC		0x18
#define G_PXMSKC		0x28
#define G_PXPAT1C		0x38
#define G_PXPAT0S		0x44
#define G_PXPEN0		0x114
#define G_PXPEN1		0x128
#define T31_SFC_PA_PINS		0x1f800000

static void t31_sfc_padconf(void)
{
	void __iomem *cpm = (void __iomem *)T31_CPM_BASE;
	void __iomem *pa = (void __iomem *)T31_GPIO_PORTA_BASE;
	unsigned int pll = T31_MPLL_HZ, rate = T31_SSI_HZ, cdr;
	u32 v;

	/*
	 * SSI/SFC clock from MPLL. A NOR boot has the SPL do this
	 * (cgu_clks_set selects MPLL in SSICDR[31:30]; ssi_clk_set_rate
	 * sets the divider); the USB-boot SPL runs neither, so program
	 * the source, divider and CE here. Rounding matches the vendor
	 * clk_set_rate(); leave CE set (clearing it kills the clock).
	 */
	if (pll % rate >= rate / 2)
		pll += rate - (pll % rate);
	else
		pll -= (pll % rate);
	cdr = (pll / rate - 1) & 0xff;
	v = readl(cpm + T31_CPM_SSICDR);
	v &= ~((3u << 30) | SSICDR_STOP | SSICDR_BUSY | 0xff);
	v |= SSICDR_SRC_MPLL | SSICDR_CE | cdr;
	writel(v, cpm + T31_CPM_SSICDR);
	while (readl(cpm + T31_CPM_SSICDR) & SSICDR_BUSY)
		;

	clrbits_le32(T31_CPM_CLKGR0, T31_CPM_CLKGR0_SFC);

	writel(T31_SFC_PA_PINS, pa + G_PXINTC);
	writel(T31_SFC_PA_PINS, pa + G_PXMSKC);
	writel(T31_SFC_PA_PINS, pa + G_PXPAT1C);
	writel(T31_SFC_PA_PINS, pa + G_PXPAT0S);
	writel(T31_SFC_PA_PINS, pa + G_PXPEN0);
	writel(T31_SFC_PA_PINS, pa + G_PXPEN1);
}
#else
static inline void t31_sfc_padconf(void) { }
#endif

static inline u32 sfc_readl(struct sfc_priv *sfc, unsigned int off)
{
	return readl(sfc->base + off);
}

static inline void sfc_writel(struct sfc_priv *sfc, unsigned int off, u32 val)
{
	writel(val, sfc->base + off);
}

static u8 sfc_set_tran_mode(const struct spi_mem_op *op)
{
	u8 cmd_width, addr_width, data_width;

	if (!op)
		return TM_STD_SPI;

	cmd_width = op->cmd.buswidth;
	addr_width = op->addr.buswidth;
	data_width = op->data.buswidth;

	if (cmd_width == 1 && addr_width == 1 && data_width == 1)
		return TM_STD_SPI;	/* 1-1-1 */
	else if (cmd_width == 1 && addr_width == 1 && data_width == 2)
		return TM_DI_DO_SPI;	/* 1-1-2 */
	else if (cmd_width == 1 && addr_width == 2 && data_width == 2)
		return TM_DIO_SPI;	/* 1-2-2 */
	else if (cmd_width == 2 && addr_width == 2 && data_width == 2)
		return TM_FULL_DIO_SPI;	/* 2-2-2 */
	else if (cmd_width == 1 && addr_width == 1 && data_width == 4)
		return TM_QI_QO_SPI;	/* 1-1-4 */
	else if (cmd_width == 1 && addr_width == 4 && data_width == 4)
		return TM_QIO_SPI;	/* 1-4-4 */
	else if (cmd_width == 4 && addr_width == 4 && data_width == 4)
		return TM_FULL_QIO_SPI;	/* 4-4-4 */

	return TM_STD_SPI;
}

static int sfc_write_data(struct sfc_priv *sfc, const void *data, u32 len)
{
	u32 i, timeout = 0xffff;
	u32 tmp_buf[THRESHOLD];
	u32 fifo_num, data_len, fifo_len = THRESHOLD * 4;

	if (!len)
		return 0;

	while (len > 0) {
		if (!(sfc_readl(sfc, SFC_SR) & TRAN_REQ)) {
			if (timeout == 0) {
				printf("SFC: wait TRAN_REQ timeout\n");
				break;
			}
			timeout--;
			continue;
		}
		timeout = 0xffff;

		sfc_writel(sfc, SFC_SCR, CLR_TREQ);
		data_len = (len >= fifo_len) ? fifo_len : len;
		fifo_num = (data_len + 3) / 4;

		memset(tmp_buf, 0, sizeof(tmp_buf));
		memcpy(tmp_buf, data, data_len);
		for (i = 0; i < fifo_num; i++)
			sfc_writel(sfc, SFC_RM_DR, tmp_buf[i]);

		data = (const u8 *)data + data_len;
		len -= data_len;
	}

	return len;
}

static int sfc_read_data(struct sfc_priv *sfc, void *data, u32 len)
{
	u32 i, timeout = 0xffff;
	u32 tmp_buf[THRESHOLD];
	u32 fifo_num, data_len, fifo_len = THRESHOLD * 4;

	if (!len)
		return 0;

	while (len > 0) {
		if (!(sfc_readl(sfc, SFC_SR) & RECE_REQ)) {
			if (timeout == 0) {
				printf("SFC: wait RECE_REQ timeout\n");
				break;
			}
			timeout--;
			continue;
		}
		timeout = 0xffff;

		sfc_writel(sfc, SFC_SCR, CLR_RREQ);
		data_len = (len >= fifo_len) ? fifo_len : len;
		fifo_num = (data_len + 3) / 4;

		for (i = 0; i < fifo_num; i++)
			tmp_buf[i] = sfc_readl(sfc, SFC_RM_DR);
		memcpy(data, tmp_buf, data_len);

		data = (u8 *)data + data_len;
		len -= data_len;
	}

	return len;
}

/*
 * Drive one SPI-MEM operation on the SFC hardware. Supports the
 * cmd / cmd+data / cmd+addr+data transfer formats.
 */
static int sfc_hw_exec_op(struct sfc_priv *sfc, const struct spi_mem_op *op)
{
	u32 timeout = 0xffff;
	u32 dummy = 0;
	u32 data_dir = 1;
	sfc_reg_t reg = { 0 };

	if (!sfc || !op)
		return -EINVAL;

	sfc_debug("SFC: cmd=0x%02x addr=0x%08llx len=%u dir=%s\n",
		  op->cmd.opcode, (unsigned long long)op->addr.val,
		  op->data.nbytes,
		  op->data.dir == SPI_MEM_DATA_IN ? "in" : "out");

	if (op->dummy.nbytes) {
		dummy = op->dummy.nbytes * 8 / op->dummy.buswidth;
		if (dummy > 32) {
			printf("SFC: dummy bits > 32, not supported\n");
			return -EINVAL;
		}
	}

	if (op->data.nbytes)
		data_dir = (op->data.dir == SPI_MEM_DATA_IN) ? 0 : 1;

	/*
	 * Quiesce the controller before every op. The vendor sfc_read()
	 * issues STOP+FLUSH per transaction; without it a prior transfer
	 * can leave the RX FIFO/state machine wedged so the next op never
	 * sees RECE_REQ.
	 */
	sfc_writel(sfc, SFC_TRIG, TRIG_STOP);
	sfc_writel(sfc, SFC_TRIG, TRIG_FLUSH);
	sfc_writel(sfc, SFC_SCR, CLR_END | CLR_TREQ | CLR_RREQ |
				 CLR_OVER | CLR_UNDER);

	/* SFC_TRAN_LEN is a byte count, not a word count */
	sfc_writel(sfc, SFC_TRAN_LEN, op->data.nbytes);

	reg.b32 = sfc_readl(sfc, SFC_GLB0);
	reg.sfc_glb0.phase_num = 1;
	reg.sfc_glb0.tran_dir = data_dir;
	sfc_writel(sfc, SFC_GLB0, reg.b32);

	reg.b32 = sfc_readl(sfc, SFC_TRAN_CONF0(0));
	reg.sfc_tran_cfg0.addr_width = op->addr.nbytes;
	reg.sfc_tran_cfg0.cmd_en = 1;
	reg.sfc_tran_cfg0.data_en = (op->data.nbytes > 0) ? 1 : 0;
	reg.sfc_tran_cfg0.cmd = op->cmd.opcode;
	reg.sfc_tran_cfg0.dmy_bits = dummy;
	sfc_writel(sfc, SFC_TRAN_CONF0(0), reg.b32);

	reg.b32 = sfc_readl(sfc, SFC_TRAN_CONF1(0));
	reg.sfc_tran_cfg1.tran_md = sfc_set_tran_mode(op);
	sfc_writel(sfc, SFC_TRAN_CONF1(0), reg.b32);

	sfc_writel(sfc, SFC_DEV_ADDR(0), op->addr.val);
	sfc_writel(sfc, SFC_DEV_ADDR_PLUS(0), 0);
	sfc_writel(sfc, SFC_TRIG, TRIG_START);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		sfc_write_data(sfc, op->data.buf.out, op->data.nbytes);
	else if (op->data.dir == SPI_MEM_DATA_IN)
		sfc_read_data(sfc, op->data.buf.in, op->data.nbytes);

	timeout = 0xffff;
	while (!(sfc_readl(sfc, SFC_SR) & SR_END)) {
		if (timeout == 0) {
			printf("SFC: wait end timeout\n");
			break;
		}
		timeout--;
	}

	if (sfc_readl(sfc, SFC_SR) & SR_END)
		sfc_writel(sfc, SFC_SCR, CLR_END);

	return 0;
}

static int sfc_hw_init(struct sfc_priv *sfc)
{
	sfc_reg_t reg = { 0 };
	u32 i;

	reg.b32 = sfc_readl(sfc, SFC_GLB0);
	reg.sfc_glb0.wp_en = 1;
	reg.sfc_glb0.tran_dir = 0;
	reg.sfc_glb0.op_mode = 0;
	reg.sfc_glb0.threshold = THRESHOLD;
	sfc_writel(sfc, SFC_GLB0, reg.b32);

	reg.b32 = sfc_readl(sfc, SFC_DEV_CONF);
	reg.sfc_dev_conf.cmd_type = 0;
	reg.sfc_dev_conf.cpol = 0;
	reg.sfc_dev_conf.cpha = 0;
	reg.sfc_dev_conf.smp_delay = 0;
	reg.sfc_dev_conf.thold = 0;
	reg.sfc_dev_conf.tsetup = 0;
	reg.sfc_dev_conf.tsh = 0;
	reg.sfc_dev_conf.ce_dl = 1;
	reg.sfc_dev_conf.hold_dl = 1;
	reg.sfc_dev_conf.wp_dl = 1;
	sfc_writel(sfc, SFC_DEV_CONF, reg.b32);

	for (i = 0; i < 6; i++) {
		sfc_writel(sfc, SFC_TRAN_CONF0(i),
			   sfc_readl(sfc, SFC_TRAN_CONF0(i)) &
			   ~TRAN_CONF0_FMAT);
		sfc_writel(sfc, SFC_TRAN_CONF1(i),
			   sfc_readl(sfc, SFC_TRAN_CONF1(i)) &
			   ~TRAN_CONF1_TRAN_MODE_MSK);
	}

	sfc_writel(sfc, SFC_SCR, CLR_END | CLR_TREQ | CLR_RREQ |
				 CLR_OVER | CLR_UNDER);
	sfc_writel(sfc, SFC_INTC, sfc_readl(sfc, SFC_INTC) |
				  MASK_END | MASK_TREQ | MASK_RREQ |
				  MASK_OVER | MASK_UNDR);
	sfc_writel(sfc, SFC_CGE, 0);

	sfc_writel(sfc, SFC_TRIG, TRIG_STOP);
	sfc_writel(sfc, SFC_TRIG, TRIG_FLUSH);
	sfc_writel(sfc, SFC_TRAN_LEN, 0);

	return 0;
}

/* SPI-MEM entry point */
static int sfc_exec_op(struct spi_slave *slave, const struct spi_mem_op *op)
{
	struct udevice *bus = slave->dev->parent;
	struct sfc_priv *sfc = dev_get_priv(bus);
	int ret;

	if (!op->cmd.buswidth || op->cmd.buswidth > 4)
		return -EINVAL;
	if (op->addr.nbytes && (!op->addr.buswidth || op->addr.buswidth > 4))
		return -EINVAL;
	if (op->data.nbytes && (!op->data.buswidth || op->data.buswidth > 4))
		return -EINVAL;

	ret = sfc_hw_exec_op(sfc, op);
	if (ret) {
		printf("SFC: operation failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct spi_controller_mem_ops sfc_mem_ops = {
	.exec_op = sfc_exec_op,
};

static int sfc_set_speed(struct udevice *bus, uint speed)
{
	struct sfc_priv *sfc = dev_get_priv(bus);

	/*
	 * The SFC/SSI source clock is configured by the SPL (70 MHz on
	 * T31) and inherited by U-Boot proper, so there is nothing to
	 * reprogram here. Just clamp to the controller maximum.
	 */
	if (speed > sfc->max_freq)
		speed = sfc->max_freq;

	sfc_debug("SFC: set speed %u Hz (clock owned by SPL)\n", speed);
	return 0;
}

static int sfc_set_mode(struct udevice *bus, uint mode)
{
	struct sfc_priv *sfc = dev_get_priv(bus);

	sfc->mode = mode;

	/* SFC only supports SPI mode 0 (CPOL=0, CPHA=0) */
	if ((mode & (SPI_CPOL | SPI_CPHA)) != 0)
		return -EINVAL;

	return 0;
}

static int sfc_claim_bus(struct udevice *dev)
{
	return 0;
}

static int sfc_release_bus(struct udevice *dev)
{
	return 0;
}

static int sfc_cs_info(struct udevice *bus, uint cs, struct spi_cs_info *info)
{
	if (cs != SFC_SUPPORTED_CS)
		return -ENODEV;

	info->dev = NULL;
	return 0;
}

static const struct dm_spi_ops sfc_ops = {
	.claim_bus	= sfc_claim_bus,
	.release_bus	= sfc_release_bus,
	.set_speed	= sfc_set_speed,
	.set_mode	= sfc_set_mode,
	.cs_info	= sfc_cs_info,
	.mem_ops	= &sfc_mem_ops,
};

static int sfc_of_to_plat(struct udevice *bus)
{
	struct sfc_priv *sfc = dev_get_priv(bus);
	int ret;

	sfc->base = dev_read_addr_ptr(bus);
	if (!sfc->base)
		return -EINVAL;

	ret = dev_read_u32(bus, "spi-max-frequency", &sfc->max_freq);
	if (ret)
		sfc->max_freq = SFC_DEFAULT_FREQ;

	sfc->mode = 0;
	if (dev_read_bool(bus, "spi-cpol"))
		sfc->mode |= SPI_CPOL;
	if (dev_read_bool(bus, "spi-cpha"))
		sfc->mode |= SPI_CPHA;

	return 0;
}

static int sfc_probe(struct udevice *bus)
{
	struct sfc_priv *sfc = dev_get_priv(bus);
	int ret;

	sfc->dev = bus;

	/*
	 * Ungate the SFC clock and mux its pins before any register
	 * access - a USB boot leaves both unconfigured (a NOR boot does
	 * not), which otherwise makes the flash probe read 0xff.
	 */
	t31_sfc_padconf();

	/*
	 * Optional: the SFC input clock is set up by the SPL. If a
	 * clock phandle is present we enable it and refine max_freq,
	 * but a missing clock is not fatal.
	 */
	ret = clk_get_by_index(bus, 0, &sfc->clk);
	if (!ret) {
		clk_enable(&sfc->clk);
		if (!sfc->max_freq) {
			ulong rate = clk_get_rate(&sfc->clk);

			if (rate)
				sfc->max_freq = rate;
		}
	}
	if (!sfc->max_freq)
		sfc->max_freq = SFC_DEFAULT_FREQ;

	ret = sfc_hw_init(sfc);
	if (ret) {
		printf("SFC: hardware init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct udevice_id sfc_ids[] = {
	{ .compatible = "ingenic,t31-sfc" },
	{ .compatible = "ingenic,t40-sfc" },
	{ .compatible = "ingenic,t41-sfc" },
	{ }
};

U_BOOT_DRIVER(ingenic_sfc) = {
	.name		= "ingenic_sfc",
	.id		= UCLASS_SPI,
	.of_match	= sfc_ids,
	.ops		= &sfc_ops,
	.of_to_plat	= sfc_of_to_plat,
	.probe		= sfc_probe,
	.priv_auto	= sizeof(struct sfc_priv),
};
