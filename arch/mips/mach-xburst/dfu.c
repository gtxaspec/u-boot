// SPDX-License-Identifier: GPL-2.0+
/*
 * Dynamic DFU alt-info for the Ingenic XBurst USB-boot loaders.
 *
 * The mask-ROM USB-boot loader auto-runs "dfu 0 sf 0:0". This hook
 * sizes the raw DFU region from the SPI-NOR that was actually probed,
 * so the loader spans the whole chip whatever its size (8/16/32 MiB)
 * instead of a hardcoded 16 MiB. Shared by every isvp board; built
 * only into the USB-boot loaders (CONFIG_SET_DFU_ALT_INFO).
 */

#include <dfu.h>
#include <dm.h>
#include <env.h>
#include <spi.h>
#include <spi_flash.h>
#include <vsprintf.h>
#include <linux/string.h>

void set_dfu_alt_info(char *interface, char *devstr)
{
	struct udevice *dev;
	struct spi_flash *flash;
	char info[48];

	/* A dfu_alt_info set by hand on the console wins. */
	if (env_get("dfu_alt_info"))
		return;

	/* The loader's bootcmd only ever runs "dfu 0 sf 0:0". */
	if (!interface || strcmp(interface, "sf"))
		return;

	if (spi_flash_probe_bus_cs(0, 0, &dev))
		return;

	flash = dev_get_uclass_priv(dev);
	if (!flash || !flash->size)
		return;

	snprintf(info, sizeof(info), "flash raw 0x0 0x%x", flash->size);
	env_set("dfu_alt_info", info);
}
