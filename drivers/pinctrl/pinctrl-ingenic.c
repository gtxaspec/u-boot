// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst pin controller + GPIO (XBurst1 T10-T33, XBurst2 A1).
 *
 * Mirrors the mainline Linux ingenic pinctrl binding so one device tree is
 * valid on both: an "ingenic,<soc>-pinctrl" node owning the 0x10010000
 * register block with gpio child banks. Consumers select a function with the
 * standard pinmux binding (a pin node with function/groups), applied via
 * pinctrl-generic.
 *
 * The GPIO/pinmux register engine is identical across all these SoCs; only the
 * pin map, the bank stride and the bias style differ. Each SoC selects a
 * struct ingenic_chip_info from its of_match .data, exactly like the mainline
 * Linux driver - SoCs with an identical map share one chip_info (the "classic"
 * XBurst1 parts T10..T31), while T32/T33, T40/T41 and A1 each have their own;
 * the pin arrays and group/function tables are shared by reference where the
 * maps coincide. Pin values are transcribed from each SoC's vendor U-Boot
 * gpio_func[] table and GPIO allocation datasheet (the boot-critical SFC/MSC0/
 * UART groups verified against vendor code, which is unambiguous about the
 * device-function number).
 *
 * Per-pin mode is four register pairs with set(S)/clear(C) aliases:
 *   INT  0 = device/GPIO, 1 = interrupt
 *   MSK  (INT=0) 1 = GPIO, 0 = device function
 *   PAT1/PAT0 select device function 0..3, or GPIO dir/level
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
/*
 * Bias on the modern XBurst SoCs (T23, T31, T32/T33, T40/T41, A1) is two
 * separate 1-bit-per-pin enables: pull-up (PUEN) and pull-down (PDEN), each
 * with set/clear aliases at +4/+8. The older T10/T20/T21/T30 instead have a
 * single legacy PXPE at 0x60, so bias is only wired for the split-pull SoCs
 * (chip_info.split_pull).
 */
#define GPIO_PXPUENS	0x114	/* pull-up enable, set   */
#define GPIO_PXPUENC	0x118	/* pull-up enable, clear */
#define GPIO_PXPDENS	0x124	/* pull-down enable, set   */
#define GPIO_PXPDENC	0x128	/* pull-down enable, clear */

/*
 * GPIO bank stride. The first-generation XBurst1 parts (T10/T20) space their
 * banks 0x100 apart; T21/T30 and every later SoC use 0x1000. The within-bank
 * register offsets above are identical across the whole family, so only this
 * stride differs (chip_info.bank_stride).
 */
#define BANK_STRIDE		0x1000	/* T21/T23/T30 and newer */
#define BANK_STRIDE_LEGACY	0x100	/* T10/T20 first-gen XBurst1 */
#define PINS_PER_BANK	32

struct ingenic_group {
	const char *name;
	const int *pins;
	unsigned int npins;
	u8 func;		/* device function 0..3 */
};

struct ingenic_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

#define GRP(nm, arr, fn) { nm, arr, ARRAY_SIZE(arr), fn }
#define FUNC(nm, arr)	 { nm, arr, ARRAY_SIZE(arr) }

/* Pin number = bank * 32 + offset (PA=0x00.., PB=0x20.., PC=0x40..). */

/* MSC0/SD = PB0..PB5, device function 0 (every SoC except A1). */
static const int txx_mmc0_1bit[]  = { 0x20, 0x21, 0x22 };
static const int txx_mmc0_4bit[]  = { 0x23, 0x24, 0x25 };
/*
 * GMAC RMII = PB6..PB14, device function 0 (T10-T33 "mac", T40/T41 "mac0").
 * Do NOT add PB15/PB16 despite the datasheet listing them as GMAC_RXD0/RXD1:
 * pins 60/61 are shared with PA25/PA26 (SFC_GPC/SFC_CE1) and must stay
 * GPIO-input while the SFC drives them, so muxing PB15/PB16 to GMAC contends
 * with the flash controller on boards using the wide SFC (T31/Z55) and breaks
 * RX (10 Mbit link, no DHCP). This PB6..PB14 set is the proven vendor map.
 */
static const int txx_mac_rmii[]	  = { 0x26, 0x27, 0x28, 0x29, 0x2a,
				      0x2b, 0x2c, 0x2d, 0x2e };
/* UART1 console = PB23/PB24, function 0 (T40/T41; A1 puts it elsewhere). */
static const int txx_uart1_data[] = { 0x37, 0x38 };
/* SFC0 4-wire = PA23,24,27,28 function 1 - the classic XBurst1 parts. */
static const int txx_sfc4_data[]  = { 0x17, 0x18, 0x1b, 0x1c };
/*
 * SFC0 6-wire = PA23..PA28. T32/T33 use device function 0; T40/T41 use
 * function 1 (vendor t40_gpio.c: GPIO_PORT_A, GPIO_FUNC_1, 0x3f << 23).
 */
static const int txx_sfc6_data[]  = { 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c };
/* T32 SFC1 = PC2..PC7 function 1 (parks the pins off the MSC0 lines). */
static const int t32_sfc1_data[]  = { 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 };

/* A1 (XBurst2, banks PA..PE) has its own map. */
static const int a1_uart1_data[]  = { 0x46, 0x47 };		/* PC6, PC7   */
static const int a1_sfc_data[]	  = { 0x54, 0x55, 0x56,		/* PC20..PC25 */
				      0x57, 0x58, 0x59 };
static const int a1_mac0_data[]	  = {				/* PA0..PA14  */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
};

/* ---- classic XBurst1: T10/T20/T21/T23/T30/T31 ---------------------- */
static const struct ingenic_group txx_classic_groups[] = {
	GRP("mmc0-1bit", txx_mmc0_1bit, 0),
	GRP("mmc0-4bit", txx_mmc0_4bit, 0),
	GRP("sfc-data",	 txx_sfc4_data, 1),
	GRP("mac-rmii",	 txx_mac_rmii,	0),
};
static const char * const txx_g_mmc0[] = { "mmc0-1bit", "mmc0-4bit" };
static const char * const txx_g_sfc[]  = { "sfc-data" };
static const char * const txx_g_mac[]  = { "mac-rmii" };
static const struct ingenic_function txx_classic_functions[] = {
	FUNC("mmc0", txx_g_mmc0),
	FUNC("sfc",  txx_g_sfc),
	FUNC("mac",  txx_g_mac),
};

/* ---- T32/T33: 6-wire SFC0 (func 0) plus the SFC1 park group --------- */
static const struct ingenic_group t32_groups[] = {
	GRP("mmc0-1bit", txx_mmc0_1bit, 0),
	GRP("mmc0-4bit", txx_mmc0_4bit, 0),
	GRP("sfc-data",	 txx_sfc6_data, 0),
	GRP("sfc1-data", t32_sfc1_data, 1),
	GRP("mac-rmii",	 txx_mac_rmii,	0),
};
static const char * const t32_g_sfc1[] = { "sfc1-data" };
static const struct ingenic_function t32_functions[] = {
	FUNC("mmc0", txx_g_mmc0),
	FUNC("sfc",  txx_g_sfc),
	FUNC("sfc1", t32_g_sfc1),
	FUNC("mac",  txx_g_mac),
};

/* ---- T40/T41: 6-wire SFC0 (func 1), mac0, uart1 -------------------- */
static const struct ingenic_group t40_groups[] = {
	GRP("mmc0-1bit",  txx_mmc0_1bit,  0),
	GRP("mmc0-4bit",  txx_mmc0_4bit,  0),
	GRP("sfc-data",	  txx_sfc6_data,  1),
	GRP("uart1-data", txx_uart1_data, 0),
	GRP("mac0-data",  txx_mac_rmii,	  0),
};
static const char * const txx_g_uart1[] = { "uart1-data" };
static const char * const t40_g_mac0[]  = { "mac0-data" };
static const struct ingenic_function t40_functions[] = {
	FUNC("mmc0",  txx_g_mmc0),
	FUNC("sfc",   txx_g_sfc),
	FUNC("uart1", txx_g_uart1),
	FUNC("mac0",  t40_g_mac0),
};

/* ---- A1 (XBurst2) ------------------------------------------------- */
static const struct ingenic_group a1_groups[] = {
	GRP("sfc-data",	  a1_sfc_data,	 0),
	GRP("uart1-data", a1_uart1_data, 0),
	GRP("mac0-data",  a1_mac0_data,	 0),
};
static const char * const a1_g_sfc[]   = { "sfc-data" };
static const char * const a1_g_uart1[] = { "uart1-data" };
static const char * const a1_g_mac0[]  = { "mac0-data" };
static const struct ingenic_function a1_functions[] = {
	FUNC("sfc",   a1_g_sfc),
	FUNC("uart1", a1_g_uart1),
	FUNC("mac0",  a1_g_mac0),
};

/*
 * Per-SoC chip info, selected from the of_match .data. Identical SoCs share
 * a chip_info; the classic XBurst1 parts share txx_classic_*, differing only
 * in bank stride (T10/T20) and bias style (T10/T20/T21/T30 lack split pull).
 */
struct ingenic_chip_info {
	const struct ingenic_group	*groups;
	unsigned int			 ngroups;
	const struct ingenic_function	*functions;
	unsigned int			 nfunctions;
	u32				 bank_stride;
	bool				 split_pull;
};

static const struct ingenic_chip_info t10_chip_info = {		/* T10/T20 */
	.groups = txx_classic_groups, .ngroups = ARRAY_SIZE(txx_classic_groups),
	.functions = txx_classic_functions,
	.nfunctions = ARRAY_SIZE(txx_classic_functions),
	.bank_stride = BANK_STRIDE_LEGACY, .split_pull = false,
};
static const struct ingenic_chip_info t21_chip_info = {		/* T21/T30 */
	.groups = txx_classic_groups, .ngroups = ARRAY_SIZE(txx_classic_groups),
	.functions = txx_classic_functions,
	.nfunctions = ARRAY_SIZE(txx_classic_functions),
	.bank_stride = BANK_STRIDE, .split_pull = false,
};
static const struct ingenic_chip_info t31_chip_info = {		/* T23/T31 */
	.groups = txx_classic_groups, .ngroups = ARRAY_SIZE(txx_classic_groups),
	.functions = txx_classic_functions,
	.nfunctions = ARRAY_SIZE(txx_classic_functions),
	.bank_stride = BANK_STRIDE, .split_pull = true,
};
static const struct ingenic_chip_info t32_chip_info = {		/* T32/T33 */
	.groups = t32_groups, .ngroups = ARRAY_SIZE(t32_groups),
	.functions = t32_functions, .nfunctions = ARRAY_SIZE(t32_functions),
	.bank_stride = BANK_STRIDE, .split_pull = true,
};
static const struct ingenic_chip_info t40_chip_info = {		/* T40/T41 */
	.groups = t40_groups, .ngroups = ARRAY_SIZE(t40_groups),
	.functions = t40_functions, .nfunctions = ARRAY_SIZE(t40_functions),
	.bank_stride = BANK_STRIDE, .split_pull = true,
};
static const struct ingenic_chip_info a1_chip_info = {		/* A1 */
	.groups = a1_groups, .ngroups = ARRAY_SIZE(a1_groups),
	.functions = a1_functions, .nfunctions = ARRAY_SIZE(a1_functions),
	.bank_stride = BANK_STRIDE, .split_pull = true,
};

struct ingenic_pinctrl_priv {
	void __iomem			*base;
	u32				 bank_stride;
	bool				 split_pull;
	const struct ingenic_group	*groups;
	unsigned int			 ngroups;
	const struct ingenic_function	*functions;
	unsigned int			 nfunctions;
};

static int ingenic_get_groups_count(struct udevice *dev)
{
	return ((struct ingenic_pinctrl_priv *)dev_get_priv(dev))->ngroups;
}

static const char *ingenic_get_group_name(struct udevice *dev, unsigned int sel)
{
	return ((struct ingenic_pinctrl_priv *)dev_get_priv(dev))->groups[sel].name;
}

static int ingenic_get_functions_count(struct udevice *dev)
{
	return ((struct ingenic_pinctrl_priv *)dev_get_priv(dev))->nfunctions;
}

static const char *ingenic_get_function_name(struct udevice *dev,
					     unsigned int sel)
{
	return ((struct ingenic_pinctrl_priv *)dev_get_priv(dev))->functions[sel].name;
}

static void ingenic_set_pin_fn(struct ingenic_pinctrl_priv *p, int pin, u8 func)
{
	void __iomem *r = p->base + (pin / PINS_PER_BANK) * p->bank_stride;
	u32 bit = BIT(pin % PINS_PER_BANK);

	writel(bit, r + GPIO_PXINTC);				/* not irq */
	writel(bit, r + GPIO_PXMSKC);				/* device fn */
	writel(bit, r + ((func & 2) ? GPIO_PXPAT1S : GPIO_PXPAT1C));
	writel(bit, r + ((func & 1) ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	writel(bit, r + GPIO_PXPUENC);				/* pull-up off */
}

static int ingenic_pinmux_group_set(struct udevice *dev, unsigned int group,
				    unsigned int func)
{
	struct ingenic_pinctrl_priv *p = dev_get_priv(dev);
	const struct ingenic_group *g = &p->groups[group];
	unsigned int i;

	for (i = 0; i < g->npins; i++)
		ingenic_set_pin_fn(p, g->pins[i], g->func);

	return 0;
}

static const struct pinctrl_ops ingenic_pinctrl_ops = {
	.get_groups_count	= ingenic_get_groups_count,
	.get_group_name		= ingenic_get_group_name,
	.get_functions_count	= ingenic_get_functions_count,
	.get_function_name	= ingenic_get_function_name,
	.pinmux_group_set	= ingenic_pinmux_group_set,
	.set_state		= pinctrl_generic_set_state,
};

static int ingenic_pinctrl_bind(struct udevice *dev)
{
	/* Bind the GPIO bank children (gpio@0..3). */
	return dm_scan_fdt_dev(dev);
}

static int ingenic_pinctrl_probe(struct udevice *dev)
{
	struct ingenic_pinctrl_priv *p = dev_get_priv(dev);
	const struct ingenic_chip_info *c =
		(const struct ingenic_chip_info *)dev_get_driver_data(dev);

	p->base = dev_remap_addr(dev);
	if (!p->base || !c)
		return -EINVAL;

	p->groups	= c->groups;
	p->ngroups	= c->ngroups;
	p->functions	= c->functions;
	p->nfunctions	= c->nfunctions;
	p->bank_stride	= c->bank_stride;
	p->split_pull	= c->split_pull;

	return 0;
}

static const struct udevice_id ingenic_pinctrl_ids[] = {
	{ .compatible = "ingenic,t10-pinctrl", .data = (ulong)&t10_chip_info },
	{ .compatible = "ingenic,t20-pinctrl", .data = (ulong)&t10_chip_info },
	{ .compatible = "ingenic,t21-pinctrl", .data = (ulong)&t21_chip_info },
	{ .compatible = "ingenic,t30-pinctrl", .data = (ulong)&t21_chip_info },
	{ .compatible = "ingenic,t23-pinctrl", .data = (ulong)&t31_chip_info },
	{ .compatible = "ingenic,t31-pinctrl", .data = (ulong)&t31_chip_info },
	{ .compatible = "ingenic,t32-pinctrl", .data = (ulong)&t32_chip_info },
	{ .compatible = "ingenic,t33-pinctrl", .data = (ulong)&t32_chip_info },
	{ .compatible = "ingenic,t40-pinctrl", .data = (ulong)&t40_chip_info },
	{ .compatible = "ingenic,t41-pinctrl", .data = (ulong)&t40_chip_info },
	{ .compatible = "ingenic,a1-pinctrl",  .data = (ulong)&a1_chip_info },
	{ }
};

U_BOOT_DRIVER(ingenic_pinctrl) = {
	.name		= "ingenic_pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= ingenic_pinctrl_ids,
	.bind		= ingenic_pinctrl_bind,
	.probe		= ingenic_pinctrl_probe,
	.priv_auto	= sizeof(struct ingenic_pinctrl_priv),
	.ops		= &ingenic_pinctrl_ops,
};

/* ---- GPIO bank child (PA..PD) ---------------------------------- */

struct ingenic_gpio_priv {
	void __iomem	*regs;
	char		bank_name[4];
	bool		split_pull;	/* PUEN/PDEN bias available */
};

static int ingenic_gpio_direction_input(struct udevice *dev, unsigned int off)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);
	writel(bit, priv->regs + GPIO_PXMSKS);
	writel(bit, priv->regs + GPIO_PXPAT1S);
	return 0;
}

static int ingenic_gpio_direction_output(struct udevice *dev, unsigned int off,
					 int value)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	writel(bit, priv->regs + GPIO_PXINTC);
	writel(bit, priv->regs + GPIO_PXMSKS);
	writel(bit, priv->regs + GPIO_PXPAT1C);
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

	writel(BIT(off), priv->regs +
	       (value ? GPIO_PXPAT0S : GPIO_PXPAT0C));
	return 0;
}

static int ingenic_gpio_get_function(struct udevice *dev, unsigned int off)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	if (!(readl(priv->regs + GPIO_PXMSK) & bit))
		return GPIOF_FUNC;
	if (readl(priv->regs + GPIO_PXPAT1) & bit)
		return GPIOF_INPUT;
	return GPIOF_OUTPUT;
}

/*
 * Apply consumer/DT GPIOD_ flags in one shot: direction (mirrors the
 * direction_* ops) plus bias. The uclass folds active-low into
 * GPIOD_IS_OUT_ACTIVE, so the output level is read straight from it. Bias maps
 * to the split PUEN/PDEN enables and is only touched on SoCs that have them; a
 * request carrying neither pull flag leaves the bias untouched, so a plain
 * "gpio input" keeps its power-on bias.
 */
static int ingenic_gpio_set_flags(struct udevice *dev, unsigned int off,
				  ulong flags)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	u32 bit = BIT(off);

	if (flags & GPIOD_IS_OUT) {
		writel(bit, priv->regs + GPIO_PXINTC);
		writel(bit, priv->regs + GPIO_PXMSKS);
		writel(bit, priv->regs + GPIO_PXPAT1C);
		writel(bit, priv->regs + ((flags & GPIOD_IS_OUT_ACTIVE) ?
					  GPIO_PXPAT0S : GPIO_PXPAT0C));
	} else if (flags & GPIOD_IS_IN) {
		writel(bit, priv->regs + GPIO_PXINTC);
		writel(bit, priv->regs + GPIO_PXMSKS);
		writel(bit, priv->regs + GPIO_PXPAT1S);
	}

	if (priv->split_pull) {
		if (flags & GPIOD_PULL_UP) {
			writel(bit, priv->regs + GPIO_PXPDENC); /* pull-down off */
			writel(bit, priv->regs + GPIO_PXPUENS); /* pull-up on    */
		} else if (flags & GPIOD_PULL_DOWN) {
			writel(bit, priv->regs + GPIO_PXPUENC); /* pull-up off    */
			writel(bit, priv->regs + GPIO_PXPDENS); /* pull-down on   */
		}
	}

	return 0;
}

static const struct dm_gpio_ops ingenic_gpio_ops = {
	.direction_input	= ingenic_gpio_direction_input,
	.direction_output	= ingenic_gpio_direction_output,
	.get_value		= ingenic_gpio_get_value,
	.set_value		= ingenic_gpio_set_value,
	.get_function		= ingenic_gpio_get_function,
	.set_flags		= ingenic_gpio_set_flags,
};

static int ingenic_gpio_probe(struct udevice *dev)
{
	struct ingenic_gpio_priv *priv = dev_get_priv(dev);
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct ingenic_pinctrl_priv *pc = dev_get_priv(dev->parent);
	u32 bank = dev_read_addr(dev);

	priv->regs = pc->base + bank * pc->bank_stride;
	priv->split_pull = pc->split_pull;
	priv->bank_name[0] = 'P';
	priv->bank_name[1] = 'A' + bank;
	priv->bank_name[2] = '\0';
	uc_priv->bank_name = priv->bank_name;
	uc_priv->gpio_count = PINS_PER_BANK;
	return 0;
}

static const struct udevice_id ingenic_gpio_ids[] = {
	{ .compatible = "ingenic,a1-gpio" },
	{ .compatible = "ingenic,t40-gpio" },
	{ .compatible = "ingenic,t10-gpio" },
	{ .compatible = "ingenic,t20-gpio" },
	{ .compatible = "ingenic,t21-gpio" },
	{ .compatible = "ingenic,t23-gpio" },
	{ .compatible = "ingenic,t30-gpio" },
	{ .compatible = "ingenic,t31-gpio" },
	{ .compatible = "ingenic,t32-gpio" },
	{ .compatible = "ingenic,t33-gpio" },
	{ .compatible = "ingenic,t41-gpio" },
	{ }
};

U_BOOT_DRIVER(ingenic_gpio) = {
	.name		= "ingenic_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= ingenic_gpio_ids,
	.ops		= &ingenic_gpio_ops,
	.probe		= ingenic_gpio_probe,
	.priv_auto	= sizeof(struct ingenic_gpio_priv),
};
