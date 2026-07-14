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
 *
 * The loader also exposes DFU alt "erase" (virt backend): downloading the
 * exact token XBURST_ERASE_TOKEN wipes the whole boot flash. On NAND this
 * is a hard requirement for reflashing: UBI needs everything beyond the
 * written image to be erased (stale PEBs fail the attach), and dfu_mtd
 * only erases the range it writes - so the host erases first, then writes.
 * On NOR it is a convenience (full images are chip-sized and erase as they
 * write): a fast way to blank a chip or clear a stale env.
 */

#include <dfu.h>
#include <dm.h>
#include <env.h>
#include <mmc.h>
#include <mtd.h>
#include <spi.h>
#include <spi_flash.h>
#include <vsprintf.h>
#include <dm/device-internal.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/usb/gadget.h>
#include <u-boot/schedule.h>
#include <mach/efuse.h>

/* Boot flash detected by board_late_init(): the SPI-NAND mtd name
 * ("spi-nand0"/"spi-nand1"), or the probed SPI-NOR when there is no NAND.
 * Consumed by the "erase" DFU alt.
 */
static char xburst_nand_mtd[16];
static struct spi_flash *xburst_nor_flash;

void set_dfu_alt_info(char *interface, char *devstr)
{
	struct udevice *dev;
	struct spi_flash *flash;
	char info[48];

	/*
	 * Give the DFU gadget a USB serial number derived from the SoC's per-die
	 * eFUSE chip serial, via the standard serial# env var (g_dnl's on_serialno
	 * callback picks it up before run_usb_dnl_gadget binds the gadget). WebUSB
	 * only persists a device permission for a device that reports a serial
	 * (Chromium's CanStorePersistentEntry), so this lets the web flasher keep
	 * access across the bootrom->gadget re-enumeration instead of re-prompting.
	 * A serial# provisioned elsewhere (console/env) always wins.
	 */
	if (!env_get("serial#")) {
		u32 s[4];
		char serial[33];

		xburst_chip_serial(s);
		snprintf(serial, sizeof(serial), "%08x%08x%08x%08x",
			 s[0], s[1], s[2], s[3]);
		env_set("serial#", serial);
	}

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

#if IS_ENABLED(CONFIG_DFU_VIRT)
#define XBURST_ERASE_TOKEN "XBURST-FLASH-WIPE"

/* Set by write_medium when the wipe token arrives; the erase itself runs
 * in flush_medium. dfu_write() drains the buffer INSIDE the zero-length
 * DNLOAD's completion handler (dwc2_udc_irq context) - blocking there
 * stalls the control transfer's own status phase for the whole erase and
 * the host times out. dfu->flush_medium, by contrast, is only called from
 * the DEFERRED dfu_flush() in the gadget main loop (common/dfu.c), where
 * pumping the gadget keeps GETSTATUS answered.
 */
static bool xburst_erase_armed;

/* While the erase runs, tell the host to re-poll GETSTATUS at this pace
 * (well inside its per-transfer timeout, well under its total poll budget).
 */
static unsigned int xburst_erase_poll_timeout(struct dfu_entity *dfu)
{
	return 500;
}

/* Keep the gadget answering GETSTATUS while a long erase runs: flush_medium
 * executes in the gadget main loop, so pumping here is the same call the
 * main loop makes. A silent EP0 would trip the host's 5s control timeout.
 */
static void xburst_erase_pump(struct udevice *udc)
{
	if (udc)
		dm_usb_gadget_handle_interrupts(udc);
	schedule();
}

static int xburst_erase_nand(struct udevice *udc)
{
	struct erase_info erase_op = {};
	struct mtd_info *mtd;
	unsigned int blocks = 0, bad = 0, failed = 0;
	u64 off;

	mtd = get_mtd_device_nm(xburst_nand_mtd);
	if (IS_ERR_OR_NULL(mtd))
		return -ENODEV;

	printf("Erasing %s: %llu MiB...\n", xburst_nand_mtd, mtd->size >> 20);
	erase_op.mtd = mtd;
	erase_op.len = mtd->erasesize;
	for (off = 0; off < mtd->size; off += mtd->erasesize) {
		blocks++;
		if (mtd_block_isbad(mtd, off)) {
			bad++;
			continue;
		}
		erase_op.addr = off;
		if (mtd_erase(mtd, &erase_op))
			failed++;
		xburst_erase_pump(udc);
	}
	printf("Erase done: %u blocks, %u bad skipped, %u failed\n",
	       blocks, bad, failed);
	put_mtd_device(mtd);
	return failed ? -EIO : 0;
}

static int xburst_erase_nor(struct udevice *udc)
{
	struct spi_flash *flash = xburst_nor_flash;
	u32 chunk, off;
	int ret = 0;

	/* One erase call per <=64K so the pump runs often enough. */
	chunk = flash->erase_size < SZ_64K ? SZ_64K : flash->erase_size;

	printf("Erasing SPI-NOR: %u MiB...\n", flash->size >> 20);
	for (off = 0; off < flash->size && !ret; off += chunk) {
		ret = spi_flash_erase(flash, off, min(chunk, flash->size - off));
		xburst_erase_pump(udc);
	}
	if (ret)
		printf("Erase FAILED at 0x%x (%d)\n", off - chunk, ret);
	else
		printf("Erase done\n");
	return ret;
}

/*
 * DFU alt "erase" (virt 0), data stage: validate the wipe token and arm.
 * Runs in the ZLP completion (irq context), so it must not block - the
 * erase itself runs in xburst_erase_flush below.
 */
int dfu_write_medium_virt(struct dfu_entity *dfu, u64 offset, void *buf,
			  long *len)
{
	if (dfu->data.virt.dev_num != 0)
		return -EINVAL;
	if (offset != 0 || *len < (long)strlen(XBURST_ERASE_TOKEN) ||
	    memcmp(buf, XBURST_ERASE_TOKEN, strlen(XBURST_ERASE_TOKEN))) {
		printf("dfu erase: bad token, not erasing\n");
		return -EINVAL;
	}
	xburst_erase_armed = true;
	return 0;
}

/* Manifest stage: wipe the whole boot flash. NAND skips bad blocks; NOR
 * erases in pumped chunks. Only called from the deferred dfu_flush() in
 * the gadget main loop, so the pump works and the host's polls (paced by
 * xburst_erase_poll_timeout) are answered throughout.
 */
static int xburst_erase_flush(struct dfu_entity *dfu)
{
	struct udevice *udc = NULL;
	int ret;

	if (!xburst_erase_armed)
		return 0;
	xburst_erase_armed = false;

	ret = udc_device_get_by_index(0, &udc);
	if (ret)
		printf("dfu erase: no UDC to pump (%d) - host polls may stall\n",
		       ret);

	if (xburst_nand_mtd[0])
		return xburst_erase_nand(udc);
	if (xburst_nor_flash)
		return xburst_erase_nor(udc);
	printf("dfu erase: no boot flash detected\n");
	return -ENODEV;
}

/*
 * Wire up the virt ("erase") entity when a transaction starts:
 * dfu_fill_entity_virt() gives us no hook, and both fields must be in
 * place BEFORE the manifest phase - f_dfu's MANIFEST_SYNC GETSTATUS reply
 * already reads poll_timeout, and flush_medium is what defers the erase
 * out of irq context. This weak override runs at the first dfu_write()
 * of the transaction.
 */
void dfu_initiated_callback(struct dfu_entity *dfu)
{
	if (dfu->dev_type != DFU_DEV_VIRT)
		return;
	dfu->poll_timeout = xburst_erase_poll_timeout;
	dfu->flush_medium = xburst_erase_flush;
}

/* A failed/aborted transaction never reaches the flush: drop the arming so
 * a stale token can't erase on some later flush.
 */
void dfu_error_callback(struct dfu_entity *dfu, const char *msg)
{
	if (dfu->dev_type == DFU_DEV_VIRT)
		xburst_erase_armed = false;
}
#endif

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
	const char *erase_alt = "";
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
		strlcpy(xburst_nand_mtd, nand[i], sizeof(xburst_nand_mtd));
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
			xburst_nor_flash = flash;
			size = flash->size;
			break;
		}
	}

	if (!ifc[0]) {
		/* Nothing detected: harmless default so DFU still comes up. */
		env_set("dfubootcmd", "dfu 0 sf 0:0");
		return 0;
	}

	/*
	 * Default alt list: the boot flash (alt 0 "flash"), plus the "erase"
	 * virt alt (see dfu_write_medium_virt) when the backend is built in.
	 * The multi-alt list needs the interface-prefixed alt-info form and a
	 * bare "dfu 0"; without DFU_VIRT the exact single-alt strings the
	 * loaders always used are kept.
	 */
	if (IS_ENABLED(CONFIG_DFU_VIRT))
		erase_alt = "&virt 0=erase";

	if (erase_alt[0]) {
		snprintf(info, sizeof(info), "%s=flash raw 0x0 0x%llx%s",
			 ifc, size, erase_alt);
		env_set("dfu_alt_info", info);
		env_set("dfubootcmd", "dfu 0");
	} else {
		snprintf(info, sizeof(info), "flash raw 0x0 0x%llx", size);
		env_set("dfu_alt_info", info);
		snprintf(info, sizeof(info), "dfu 0 %s", ifc);
		env_set("dfubootcmd", info);
	}

	/*
	 * With MMC DFU built in, additionally expose the SD on MSC0 as alt 1
	 * "sdcard" - but ONLY when a card actually inits. dfu_mmc binds every
	 * declared entity at "dfu 0" time, so declaring "sdcard" with no card
	 * makes the whole enumeration fail (-ENODEV) and the gadget never comes
	 * up: the loader would be unusable on any unit booted without a card. So
	 * probe the card and only widen to the dual alt list when it answers.
	 * (board init only registers the MMC; dfu_mmc does find_mmc_device()
	 * without mmc_init(), so this probe also does the init the first SD
	 * transfer needs - "mmc dev 0" in the bootcmd re-asserts it at run time.)
	 */
	if (IS_ENABLED(CONFIG_DFU_MMC)) {
		struct mmc *mmc = find_mmc_device(0);

		if (mmc && !mmc_init(mmc)) {
			/* size 0 => dfu_mmc spans the whole card (blk_dev->lba). */
			snprintf(info, sizeof(info),
				 "%s=flash raw 0x0 0x%llx&mmc 0=sdcard raw 0x0 0%s",
				 ifc, size, erase_alt);
			env_set("dfu_alt_info", info);
			env_set("dfubootcmd", "mmc dev 0; dfu 0");
		}
	}
	return 0;
}
#endif
