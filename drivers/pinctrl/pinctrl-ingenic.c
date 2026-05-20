// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 T31/T23 pin controller + GPIO.
 *
 * Mirrors the mainline Linux ingenic pinctrl binding so one device
 * tree is valid on both: an "ingenic,t31-pinctrl" (or "ingenic,
 * t23-pinctrl") node owning the 0x10010000 register block, with
 * four gpio child banks (PA..PD, 0x1000 stride). Consumers select a
 * function with the standard pinmux binding (a pin node with
 * function/groups), applied via pinctrl-generic.
 *
 * T23 is XBurst1 like T31: the GPIO/pinmux register engine is
 * identical and the boot-critical pin assignments match (UART1 on
 * PB23/PB24 device-function 0, the shared SFC pins), so the same
 * group/function tables and driver serve both - the T23 compatibles
 * are added to the of_match tables rather than duplicating a
 * near-identical chip info.
 *
 * Per-pin mode is four register pairs with set(S)/clear(C) aliases:
 *   INT  0 = device/GPIO, 1 = interrupt
 *   MSK  (INT=0) 1 = GPIO, 0 = device function
 *   PAT1/PAT0 select device function 0..3, or GPIO dir/level
 * Group/function tables and the per-group mux value are transcribed
 * from the mainline Linux port's pinctrl-ingenic.c T31 chip info.
 */

#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/pinctrl.h>
#include <dm/ofnode.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <errno.h>
#include <linux/bitops.h>
#include <linux/string.h>

#define GPIO_PXPIN	0x00
#define GPIO_PXINTC	0x18
#define GPIO_PXMSK	0x20
#define GPIO_PXMSKS	0x24	/* -> GPIO */
#define GPIO_PXMSKC	0x28	/* -> device function */
#define GPIO_PXPAT1	0x30
#define GPIO_PXPAT1S	0x34
#define GPIO_PXPAT1C	0x38
#define GPIO_PXPAT0S	0x44
#define GPIO_PXPAT0C	0x48
#define GPIO_PXPENS	0x118	/* pull set   -> disabled */
#define GPIO_PXPENC	0x11c	/* pull clear -> enabled  */

#define BANK_STRIDE	0x1000
#define PINS_PER_BANK	32

struct t31_group {
	const char *name;
	const int *pins;
	unsigned int npins;
	u8 func;		/* device function 0..3 */
};

struct t31_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

#define GRP(nm, arr, fn) { nm, arr, ARRAY_SIZE(arr), fn }
#define FUNC(nm, arr)	 { nm, arr, ARRAY_SIZE(arr) }

/* Pin number = bank * 32 + offset (PA=0x00.., PB=0x20.., PC=0x40..). */
static const int t31_uart0_data_b[] = { 0x33, 0x36 };
static const int t31_uart0_data_c[] = { 0x48, 0x49 };
static const int t31_uart0_hwflow[] = { 0x34, 0x35 };
static const int t31_uart1_data_b[] = { 0x37, 0x38 };
static const int t31_uart1_data_a[] = { 0x06, 0x07 };
static const int t31_uart2_data_c[] = { 0x4d, 0x4e };
static const int t31_uart2_data_a[] = { 0x0a, 0x0b };
static const int t31_i2c0[]	    = { 0x0c, 0x0d };
static const int t31_i2c1_a[]	    = { 0x10, 0x11 };
static const int t31_i2c1_b[]	    = { 0x39, 0x3a };
static const int t31_i2c1_c[]	    = { 0x48, 0x49 };
static const int t31_mmc0_1bit[]    = { 0x20, 0x21, 0x22 };
static const int t31_mmc0_4bit[]    = { 0x23, 0x24, 0x25 };
static const int t31_sfc_data[]	    = { 0x17, 0x18, 0x1b, 0x1c };
/*
 * T32/T33 SFC0: PA23..PA28 (6 pins, function 0). Vendor binding
 * `sfc0-pa` in PRJ-pinctrl.dtsi:
 *   ingenic,pinmux = <&gpa 23 28>;
 *   ingenic,pinmux-funcsel = <PINCTL_FUNCTION0>;
 * Different shape from T31 (4 pins, function 1) so the same
 * "sfc-data" group resolves per-SoC at pinmux time.
 */
static const int t32_sfc_data[]	    = { 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c };
static const int t31_mac_rmii[]	    = { 0x26, 0x27, 0x28, 0x29, 0x2a,
					0x2b, 0x2c, 0x2d, 0x2e };
static const int t31_cim_mclk[]	    = { 0x0f };

static const struct t31_group t31_groups[] = {
	GRP("uart0-data-b", t31_uart0_data_b, 0),
	GRP("uart0-data-c", t31_uart0_data_c, 1),
	GRP("uart0-hwflow", t31_uart0_hwflow, 0),
	GRP("uart1-data-b", t31_uart1_data_b, 0),
	GRP("uart1-data-a", t31_uart1_data_a, 2),
	GRP("uart2-data-c", t31_uart2_data_c, 2),
	GRP("uart2-data-a", t31_uart2_data_a, 2),
	GRP("i2c0-data",    t31_i2c0,	      1),
	GRP("i2c1-data-a",  t31_i2c1_a,	      2),
	GRP("i2c1-data-b",  t31_i2c1_b,	      0),
	GRP("i2c1-data-c",  t31_i2c1_c,	      3),
	GRP("mmc0-1bit",    t31_mmc0_1bit,    0),
	GRP("mmc0-4bit",    t31_mmc0_4bit,    0),
	GRP("sfc-data",	    t31_sfc_data,     1),
	GRP("mac-rmii",	    t31_mac_rmii,     0),
	GRP("cim-mclk",	    t31_cim_mclk,     0),
};

static const char * const t31_g_uart0[] = {
	"uart0-data-b", "uart0-data-c", "uart0-hwflow" };
static const char * const t31_g_uart1[] = { "uart1-data-b", "uart1-data-a" };
static const char * const t31_g_uart2[] = { "uart2-data-c", "uart2-data-a" };
static const char * const t31_g_i2c0[]	= { "i2c0-data" };
static const char * const t31_g_i2c1[]	= {
	"i2c1-data-a", "i2c1-data-b", "i2c1-data-c" };
static const char * const t31_g_mmc0[]	= { "mmc0-1bit", "mmc0-4bit" };
static const char * const t31_g_sfc[]	= { "sfc-data" };
static const char * const t31_g_mac[]	= { "mac-rmii" };
static const char * const t31_g_cim[]	= { "cim-mclk" };

static const struct t31_function t31_functions[] = {
	FUNC("uart0", t31_g_uart0),
	FUNC("uart1", t31_g_uart1),
	FUNC("uart2", t31_g_uart2),
	FUNC("i2c0",  t31_g_i2c0),
	FUNC("i2c1",  t31_g_i2c1),
	FUNC("mmc0",  t31_g_mmc0),
	FUNC("sfc",   t31_g_sfc),
	FUNC("mac",   t31_g_mac),
	FUNC("cim",   t31_g_cim),
};

struct t31_pinctrl_priv {
	void __iomem *base;
	bool is_t32_family;	/* T32/T33: 6-pin SFC0 on PA23-28, func 0 */
};

static int t31_get_groups_count(struct udevice *dev)
{
	return ARRAY_SIZE(t31_groups);
}

static const char *t31_get_group_name(struct udevice *dev, unsigned int sel)
{
	return t31_groups[sel].name;
}

static int t31_get_functions_count(struct udevice *dev)
{
	return ARRAY_SIZE(t31_functions);
}

static const char *t31_get_function_name(struct udevice *dev, unsigned int sel)
{
	return t31_functions[sel].name;
}

static void t31_set_pin_fn(struct t31_pinctrl_priv *p, int pin, u8 func)
{
	void __iomem *r = p->base + (pin / PINS_PER_BANK) * BANK_STRIDE;
	u32 bit = BIT(pin % PINS_PER_BANK);

	writel(bit, r + GPIO_PXINTC);				/* not irq */
	writel(bit, r + GPIO_PXMSKC);				/* device fn */
	writel(bit, r + ((func & 2) ? GPIO_PXPAT1S : GPIO_PXPAT1C));
	writel(bit, r + ((func & 1) ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	writel(bit, r + GPIO_PXPENS);				/* pull off */
}

static int t31_pinmux_group_set(struct udevice *dev, unsigned int group,
				unsigned int func)
{
	struct t31_pinctrl_priv *p = dev_get_priv(dev);
	const struct t31_group *g = &t31_groups[group];
	const int *pins = g->pins;
	unsigned int npins = g->npins;
	u8 pin_func = g->func;
	unsigned int i;

	if (p->is_t32_family && !strcmp(g->name, "sfc-data")) {
		pins = t32_sfc_data;
		npins = ARRAY_SIZE(t32_sfc_data);
		pin_func = 0;
	}

	for (i = 0; i < npins; i++)
		t31_set_pin_fn(p, pins[i], pin_func);

	return 0;
}

static const struct pinctrl_ops t31_pinctrl_ops = {
	.get_groups_count	= t31_get_groups_count,
	.get_group_name		= t31_get_group_name,
	.get_functions_count	= t31_get_functions_count,
	.get_function_name	= t31_get_function_name,
	.pinmux_group_set	= t31_pinmux_group_set,
	.set_state		= pinctrl_generic_set_state,
};

static int t31_pinctrl_bind(struct udevice *dev)
{
	/* Bind the GPIO bank children (gpio@0..3). */
	return dm_scan_fdt_dev(dev);
}

static int t31_pinctrl_probe(struct udevice *dev)
{
	struct t31_pinctrl_priv *p = dev_get_priv(dev);

	p->base = dev_remap_addr(dev);
	if (!p->base)
		return -EINVAL;

	p->is_t32_family = device_is_compatible(dev, "ingenic,t32-pinctrl") ||
			   device_is_compatible(dev, "ingenic,t33-pinctrl");

	return 0;
}

static const struct udevice_id t31_pinctrl_ids[] = {
	{ .compatible = "ingenic,t10-pinctrl" },
	{ .compatible = "ingenic,t20-pinctrl" },
	{ .compatible = "ingenic,t21-pinctrl" },
	{ .compatible = "ingenic,t23-pinctrl" },
	{ .compatible = "ingenic,t30-pinctrl" },
	{ .compatible = "ingenic,t31-pinctrl" },
	{ .compatible = "ingenic,t32-pinctrl" },
	{ .compatible = "ingenic,t33-pinctrl" },
	{ }
};

U_BOOT_DRIVER(ingenic_t31_pinctrl) = {
	.name		= "ingenic_t31_pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= t31_pinctrl_ids,
	.bind		= t31_pinctrl_bind,
	.probe		= t31_pinctrl_probe,
	.priv_auto	= sizeof(struct t31_pinctrl_priv),
	.ops		= &t31_pinctrl_ops,
};

/* ---- GPIO bank child (PA..PD) ---------------------------------- */

struct t31_gpio_priv {
	void __iomem	*regs;
	char		bank_name[4];
};

static int t31_gpio_direction_input(struct udevice *dev, unsigned int off)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);
	writel(bit, priv->regs + GPIO_PXMSKS);
	writel(bit, priv->regs + GPIO_PXPAT1S);
	return 0;
}

static int t31_gpio_direction_output(struct udevice *dev, unsigned int off,
				     int value)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);
	writel(bit, priv->regs + GPIO_PXMSKS);
	writel(bit, priv->regs + GPIO_PXPAT1C);
	writel(bit, priv->regs + (value ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	return 0;
}

static int t31_gpio_get_value(struct udevice *dev, unsigned int off)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);

	return !!(readl(priv->regs + GPIO_PXPIN) & BIT(off));
}

static int t31_gpio_set_value(struct udevice *dev, unsigned int off,
			      int value)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);

	writel(BIT(off), priv->regs +
	       (value ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	return 0;
}

static int t31_gpio_get_function(struct udevice *dev, unsigned int off)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	if (!(readl(priv->regs + GPIO_PXMSK) & bit))
		return GPIOF_FUNC;
	if (readl(priv->regs + GPIO_PXPAT1) & bit)
		return GPIOF_INPUT;
	return GPIOF_OUTPUT;
}

static const struct dm_gpio_ops t31_gpio_ops = {
	.direction_input	= t31_gpio_direction_input,
	.direction_output	= t31_gpio_direction_output,
	.get_value		= t31_gpio_get_value,
	.set_value		= t31_gpio_set_value,
	.get_function		= t31_gpio_get_function,
};

static int t31_gpio_probe(struct udevice *dev)
{
	struct t31_gpio_priv *priv = dev_get_priv(dev);
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct t31_pinctrl_priv *pc = dev_get_priv(dev->parent);
	u32 bank = dev_read_addr(dev);

	priv->regs = pc->base + bank * BANK_STRIDE;
	priv->bank_name[0] = 'P';
	priv->bank_name[1] = 'A' + bank;
	priv->bank_name[2] = '\0';
	uc_priv->bank_name = priv->bank_name;
	uc_priv->gpio_count = PINS_PER_BANK;
	return 0;
}

static const struct udevice_id t31_gpio_ids[] = {
	{ .compatible = "ingenic,t10-gpio" },
	{ .compatible = "ingenic,t20-gpio" },
	{ .compatible = "ingenic,t21-gpio" },
	{ .compatible = "ingenic,t23-gpio" },
	{ .compatible = "ingenic,t30-gpio" },
	{ .compatible = "ingenic,t31-gpio" },
	{ .compatible = "ingenic,t32-gpio" },
	{ .compatible = "ingenic,t33-gpio" },
	{ }
};

U_BOOT_DRIVER(ingenic_t31_gpio) = {
	.name		= "ingenic_t31_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= t31_gpio_ids,
	.ops		= &t31_gpio_ops,
	.probe		= t31_gpio_probe,
	.priv_auto	= sizeof(struct t31_gpio_priv),
};
