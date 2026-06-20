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
	char ifc[24] = "";
	char info[160];
	unsigned long long size = 0;
	int i;

	/*
	 * Detect the boot flash (DFU alt 0): SPI-NAND on either SFC first
	 * (T41 has two and the boot NAND may sit on either), else SPI-NOR.
	 * flash@0 is declared spi-nand, so mtd_probe_devices() probes that;
	 * if no NAND answers, unbind the stubs to free CS0 and probe a NOR.
	 */
	mtd_probe_devices();
	for (i = 0; i < 2; i++) {
		mtd = get_mtd_device_nm(nand[i]);
		if (IS_ERR_OR_NULL(mtd))
			continue;
		snprintf(ifc, sizeof(ifc), "mtd %s", nand[i]);
		size = mtd->size;
		put_mtd_device(mtd);
		break;
	}
	if (!ifc[0]) {
		free_cs0_of_spinand();
		for (i = 0; i < 2; i++) {
			flash = spi_flash_probe(i, 0, 50000000, 0);
			if (!flash || !flash->size)
				continue;
			snprintf(ifc, sizeof(ifc), "sf %d:0", i);
			size = flash->size;
			break;
		}
	}

	if (!ifc[0]) {
		/* Nothing detected: harmless default so DFU still comes up. */
		env_set("dfubootcmd", "dfu 0 sf 0:0");
		return 0;
	}

	if (IS_ENABLED(CONFIG_DFU_MMC)) {
		/*
		 * Expose the boot flash (alt 0 "flash") AND the SD on MSC0
		 * (alt 1 "sdcard") as one multi-device alt list, run via
		 * "dfu 0". The SD alt is ALWAYS declared, never probed here:
		 * some boards gate the SD slot behind a GPIO this generic
		 * loader doesn't drive, so a boot-time card probe would be
		 * unreliable - the card is only touched at transfer time, and
		 * an absent/unpowered card just fails that one transfer. The
		 * host defaults to alt 0 (flash); the SD is opt-in via --alt.
		 */
		snprintf(info, sizeof(info),
			 "%s=flash raw 0x0 0x%llx&mmc 0=sdcard raw 0x0 0x4000",
			 ifc, size);
		env_set("dfu_alt_info", info);
		/*
		 * Bring MSC0 up before entering DFU. board init only registers
		 * the MMC; dfu_mmc does find_mmc_device() without mmc_init(), so
		 * the card must be initialised here or the first DFU write to it
		 * fails (and wedges the controller until the next rescan).
		 */
		env_set("dfubootcmd", "mmc dev 0; dfu 0");
	} else {
		snprintf(info, sizeof(info), "flash raw 0x0 0x%llx", size);
		env_set("dfu_alt_info", info);
		snprintf(info, sizeof(info), "dfu 0 %s", ifc);
		env_set("dfubootcmd", info);
	}
	return 0;
}
#endif
