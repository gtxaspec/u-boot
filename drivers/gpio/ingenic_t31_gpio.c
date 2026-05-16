// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 T-series GPIO driver
 *
 * Copyright (c) 2026
 *
 * The T-series GPIO has four 32-bit ports (PA..PD) at a 0x1000 stride.
 * A pin's mode is encoded across four register pairs, each with
 * set (xxxS) and clear (xxxC) aliases:
 *
 *   INT  0 = device/GPIO, 1 = interrupt
 *   MSK  (INT=0) 1 = GPIO, 0 = device function
 *   PAT1 (GPIO)  1 = input, 0 = output
 *   PAT0 (GPIO output) = output level
 *
 * The live pin level is read from PXPIN. This is the same register
 * model the SPL/board pinmux already pokes for the UART and MSC.
 */

#include <dm.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <errno.h>
#include <linux/bitops.h>

#define GPIO_PXPIN	0x00	/* pin level (read-only) */
#define GPIO_PXINTC	0x18	/* interrupt-bit clear */
#define GPIO_PXMSK	0x20	/* function mask (read) */
#define GPIO_PXMSKS	0x24	/* function mask set   -> GPIO */
#define GPIO_PXMSKC	0x28	/* function mask clear -> device */
#define GPIO_PXPAT1	0x30	/* pattern1 (read) */
#define GPIO_PXPAT1S	0x34	/* pattern1 set   -> input */
#define GPIO_PXPAT1C	0x38	/* pattern1 clear -> output */
#define GPIO_PXPAT0S	0x44	/* pattern0 set   -> drive high */
#define GPIO_PXPAT0C	0x48	/* pattern0 clear -> drive low */

struct ingenic_gpio_priv {
	void __iomem	*regs;
	char		bank_name[8];
};

static int ingenic_gpio_direction_input(struct udevice *dev, unsigned int off)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);	/* not interrupt */
	writel(bit, priv->regs + GPIO_PXMSKS);	/* GPIO mode */
	writel(bit, priv->regs + GPIO_PXPAT1S);	/* input */
	return 0;
}

static int ingenic_gpio_direction_output(struct udevice *dev,
					 unsigned int off, int value)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);	/* not interrupt */
	writel(bit, priv->regs + GPIO_PXMSKS);	/* GPIO mode */
	writel(bit, priv->regs + GPIO_PXPAT1C);	/* output */
	writel(bit, priv->regs + (value ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	return 0;
}

static int ingenic_gpio_get_value(struct udevice *dev, unsigned int off)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);

	return !!(readl(priv->regs + GPIO_PXPIN) & BIT(off));
}

static int ingenic_gpio_set_value(struct udevice *dev, unsigned int off,
				  int value)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + (value ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	return 0;
}

static int ingenic_gpio_get_function(struct udevice *dev, unsigned int off)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	if (!(readl(priv->regs + GPIO_PXMSK) & bit))
		return GPIOF_FUNC;	/* device function, not GPIO */
	if (readl(priv->regs + GPIO_PXPAT1) & bit)
		return GPIOF_INPUT;
	return GPIOF_OUTPUT;
}

static const struct dm_gpio_ops ingenic_gpio_ops = {
	.direction_input	= ingenic_gpio_direction_input,
	.direction_output	= ingenic_gpio_direction_output,
	.get_value		= ingenic_gpio_get_value,
	.set_value		= ingenic_gpio_set_value,
	.get_function		= ingenic_gpio_get_function,
};

static int ingenic_gpio_probe(struct udevice *dev)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	const char *name;

	priv->regs = dev_remap_addr(dev);
	if (!priv->regs)
		return -EINVAL;

	name = dev_read_string(dev, "gpio-bank-name");
	if (name) {
		strncpy(priv->bank_name, name, sizeof(priv->bank_name) - 1);
	} else {
		/* default PA, PB, PC, PD by probe order */
		priv->bank_name[0] = 'P';
		priv->bank_name[1] = 'A' + dev_seq(dev);
		priv->bank_name[2] = '\0';
	}

	uc_priv->bank_name = priv->bank_name;
	uc_priv->gpio_count = dev_read_u32_default(dev, "ngpios", 32);
	return 0;
}

static const struct udevice_id ingenic_gpio_ids[] = {
	{ .compatible = "ingenic,t31-gpio" },
	{ }
};

U_BOOT_DRIVER(ingenic_t31_gpio) = {
	.name		= "ingenic_t31_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= ingenic_gpio_ids,
	.ops		= &ingenic_gpio_ops,
	.probe		= ingenic_gpio_probe,
	.priv_auto	= sizeof(struct ingenic_gpio_priv),
};
