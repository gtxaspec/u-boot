// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T-series SDHCI host glue.
 *
 * Standard SDHCI v4.20 core, driven entirely through drivers/mmc/sdhci.c.
 * Quirks below keep the controller in 3.3 V SD-legacy mode; the MSC0
 * bus clock is set via the clk-t32 driver (which knows about MSC's
 * H_FREQ /2 bit).
 */

#include <clk.h>
#include <dm.h>
#include <mapmem.h>
#include <mmc.h>
#include <sdhci.h>

struct ingenic_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

static int ingenic_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct ingenic_sdhci_plat *plat = dev_get_plat(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	struct clk clk;
	int ret;

	host->name	= dev->name;
	host->ioaddr	= map_physmem(dev_read_addr(dev), 0x10000,
				      MAP_NOCACHE);
	host->mmc	= &plat->mmc;
	host->mmc->dev	= dev;
	host->mmc->priv	= host;

	/* 3.3 V SD legacy only on this controller. */
	host->quirks	= SDHCI_QUIRK_BROKEN_HISPD_MODE |
			  SDHCI_QUIRK_NO_1_8_V |
			  SDHCI_QUIRK_BROKEN_VOLTAGE |
			  SDHCI_QUIRK_WAIT_SEND_CMD;
	host->voltages	= MMC_VDD_32_33 | MMC_VDD_33_34;

	ret = clk_get_by_name(dev, "mmc", &clk);
	if (ret)
		return ret;
	ret = clk_enable(&clk);
	if (ret)
		return ret;

	/*
	 * Enable the SDHCI internal clock (CLOCK_CONTROL = 0x07) before
	 * touching the MSC CGU divisor. The CPM busy bit otherwise hangs
	 * after the SDHCI controller's "clock off" state.
	 */
	sdhci_writew(host, SDHCI_CLOCK_INT_EN | SDHCI_CLOCK_INT_STABLE |
			   SDHCI_CLOCK_CARD_EN, SDHCI_CLOCK_CONTROL);

	clk_set_rate(&clk, 25000000);
	host->max_clk	= clk_get_rate(&clk);

	ret = mmc_of_parse(dev, &plat->cfg);
	if (ret)
		return ret;

	ret = sdhci_setup_cfg(&plat->cfg, host, 25000000, 200000);
	if (ret)
		return ret;

	upriv->mmc = host->mmc;

	return sdhci_probe(dev);
}

static int ingenic_sdhci_bind(struct udevice *dev)
{
	struct ingenic_sdhci_plat *plat = dev_get_plat(dev);

	return mmc_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id ingenic_sdhci_ids[] = {
	{ .compatible = "ingenic,t32-mmc" },
	{ .compatible = "ingenic,t40-mmc" },
	{ .compatible = "ingenic,t41-mmc" },
	{ }
};

U_BOOT_DRIVER(ingenic_sdhci) = {
	.name		= "ingenic_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= ingenic_sdhci_ids,
	.ops		= &sdhci_ops,
	.bind		= ingenic_sdhci_bind,
	.probe		= ingenic_sdhci_probe,
	.priv_auto	= sizeof(struct sdhci_host),
	.plat_auto	= sizeof(struct ingenic_sdhci_plat),
};
