// SPDX-License-Identifier: GPL-2.0+
/*
 * Dynamic DFU setup for the Ingenic XBurst USB-boot loaders. Built only
 * into the USB-boot loaders (CONFIG_SET_DFU_ALT_INFO).
 *
 * Two mechanisms:
 *
 * 1. set_dfu_alt_info(): the mask-ROM USB-boot loader auto-runs
 *    "dfu 0 sf 0:0"; this sizes the raw DFU region from the SPI-NOR that
 *    was actually probed, so the loader spans the whole chip whatever its
 *    size. Used by the SPI-NOR-only loaders.
 *
 * 2. board_late_init() (CONFIG_BOARD_LATE_INIT): auto-detect NOR vs
 *    SPI-NAND on the SFC so one loader flashes either. The flash@0 node
 *    is declared spi-nand; on a board that actually has NAND it probes and
 *    we DFU over MTD, otherwise the (failed) spi-nand device is unbound to
 *    free chip-select 0 and a SPI-NOR is probed instead. It sets both
 *    dfu_alt_info and dfubootcmd; the loader's bootcmd is "run dfubootcmd".
 *    (A single static devicetree cannot serve both: whatever binds on CS0
 *    blocks the other type's probe.)
 */

#include <dfu.h>
#include <dm.h>
#include <env.h>
#include <mtd.h>
#include <spi.h>
#include <spi_flash.h>
#include <vsprintf.h>
#include <dm/device-internal.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/string.h>

void set_dfu_alt_info(char *interface, char *devstr)
{
	struct udevice *dev;
	struct spi_flash *flash;
	char info[48];

	/* A dfu_alt_info set by hand on the console (or by board_late_init) wins. */
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

#if defined(CONFIG_BOARD_LATE_INIT)
/*
 * Unbind a spi-nand device that bound from the devicetree but is not a
 * real NAND (i.e. the board has NOR), freeing CS0 for the SPI-NOR probe.
 */
static void free_cs0_of_spinand(void)
{
	struct udevice *sfc, *child;

	/*
	 * Walk every SFC controller (T41 has two) and free the spi-nand stub
	 * that bound from the devicetree on each, so a SPI-NOR can probe the
	 * now node-less CS0 of whichever bus actually has a NOR.
	 */
	for (uclass_first_device(UCLASS_SPI, &sfc); sfc;
	     uclass_next_device(&sfc)) {
		device_foreach_child(child, sfc) {
			if (device_is_compatible(child, "spi-nand")) {
				device_remove(child, DM_REMOVE_NORMAL);
				device_unbind(child);
				break;
			}
		}
	}
}

int board_late_init(void)
{
	static const char * const nand[] = { "spi-nand0", "spi-nand1" };
	struct spi_flash *flash;
	struct mtd_info *mtd;
	char info[48];
	char cmd[32];
	int i;

	/*
	 * SPI-NAND first: each SFC's flash@0 is declared spi-nand, so this
	 * probes them. T41 has two SFC controllers and the boot NAND may sit
	 * on either; single-SFC SoCs simply lack spi-nand1.
	 */
	mtd_probe_devices();
	for (i = 0; i < 2; i++) {
		mtd = get_mtd_device_nm(nand[i]);
		if (IS_ERR_OR_NULL(mtd))
			continue;
		snprintf(info, sizeof(info), "%s raw 0x0 0x%llx",
			 nand[i], (unsigned long long)mtd->size);
		put_mtd_device(mtd);
		env_set("dfu_alt_info", info);
		snprintf(cmd, sizeof(cmd), "dfu 0 mtd %s", nand[i]);
		env_set("dfubootcmd", cmd);
		return 0;
	}

	/*
	 * No NAND on any SFC: unbind the failed spi-nand stubs to free the
	 * chip-selects, then probe a SPI-NOR on each SFC bus in turn.
	 * spi_flash_probe() (not _bus_cs) binds the jedec_spi_nor driver
	 * itself, so it works on the now node-less CS0; the device it creates
	 * stays bound for the subsequent "dfu 0 sf N:0". On single-SFC SoCs
	 * bus 0 answers and bus 1 is a harmless miss.
	 */
	free_cs0_of_spinand();
	for (i = 0; i < 2; i++) {
		flash = spi_flash_probe(i, 0, 50000000, 0);
		if (!flash || !flash->size)
			continue;
		snprintf(info, sizeof(info), "flash raw 0x0 0x%x", flash->size);
		env_set("dfu_alt_info", info);
		snprintf(cmd, sizeof(cmd), "dfu 0 sf %d:0", i);
		env_set("dfubootcmd", cmd);
		return 0;
	}

	/* Nothing detected: leave a harmless default so the loader still
	 * enters DFU (the host will see an empty/zero-size alt). */
	env_set("dfubootcmd", "dfu 0 sf 0:0");
	return 0;
}
#endif
