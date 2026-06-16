// SPDX-License-Identifier: GPL-2.0+
/*
 * Shared XBurst1 TPL: cache-as-RAM DDR bring-up for the capped SoCs.
 *
 * The mask ROM loads this tiny TPL into the L1 cache-as-RAM window and runs it
 * before any DRAM exists. It is the mainline rk3328 DMC shape: a minimal driver
 * model off dtoc-baked platdata (TPL_OF_PLATDATA, no libfdt) whose UCLASS_RAM
 * probe brings up PLL + DDR (the shared Innophy ddr_t31.c or DWC ddr_t20.c).
 * With DDR alive the tail depends on the boot medium:
 *   - NOR (cold boot): load the DRAM-resident SPL from SPI-NOR and jump to it;
 *     the SPL runs the full DM scan and loads U-Boot proper, no size ceiling.
 *   - USB: there is no on-flash SPL - return into the mask ROM's USB loop, and
 *     the ROM uploads U-Boot proper to DRAM directly.
 *
 * board_init_f() is identical for T10/T20/T21/T30; the per-SoC bare-metal
 * console/SFC helpers, console UART, banner and the T30 dead-L2 load quirk come
 * from the xburst_tpl_soc hook table that each SoC defines in t<soc>/tpl.c.
 *
 * Keeping gd, the malloc-f heap and the stack inside the cache-as-RAM window
 * means nothing touches the (un-init'd) DRAM before the RAM probe - which
 * matters on the DWC SoCs (T10/T20), whose controller hangs on any pre-DDR
 * DRAM access.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <cpu_func.h>
#include <dm.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/xburst-tpl.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * The XBurst start.S sets up neither gd nor a fresh stack (it keeps the
 * mask-ROM sp), leaving both to C - so gd and the early-malloc pool live in
 * BSS, which for the TPL is linked into the cache-as-RAM window (no DRAM yet).
 * spl_common_init() leaves gd->malloc_base as set here because CFG_MALLOC_F_ADDR
 * is undefined for the TPL (see each SoC's configs/isvp-t<soc>.h).
 */
static gd_t gdata __section(".bss");
static u8 tpl_malloc_f[CONFIG_VAL(SYS_MALLOC_F_LEN)] __section(".bss");

void board_init_f(ulong dummy)
{
	const struct xburst_tpl_soc *soc = &xburst_tpl_soc;
	struct udevice *dev;

	clk_ungate_uart(soc->console_uart);
	soc->serial_init();
	soc->puts(soc->banner);

	/*
	 * Establish gd + the cache-as-RAM malloc pool, then run the minimal DM
	 * off the dtoc platdata.
	 */
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;
	gd->malloc_base = (unsigned long)tpl_malloc_f;

	/* Minimal DM off dtoc platdata (only &ddr); its probe brings up DDR. */
	if (spl_early_init())
		hang();

	/* The UCLASS_RAM probe brings up PLL + DDR from the platdata. */
	if (uclass_first_device_err(UCLASS_RAM, &dev))
		hang();
	soc->puts("TPL: DDR up\n");

	soc->sfc_clk_init();

	if (soc->usb_boot) {
		/*
		 * USB-boot: no on-flash SPL stage. DDR is up and the SFC clock is
		 * set, so return into the mask ROM's USB loop (start.S kept the
		 * bootrom sp, so a plain return resumes it); the ROM then uploads
		 * U-Boot proper to DRAM and jumps to it.
		 */
		soc->puts("TPL: return to ROM\n");
		return;
	}

	/*
	 * NOR: DDR is alive. Read the SPL image from SPI-NOR into DRAM at its link
	 * base and jump past its 0x800 boot header to the entry - exactly how the
	 * mask ROM launched this TPL. The SPL then runs the full DM scan from DRAM.
	 * T30 loads through the uncached window and does not flush (dead L2 - see
	 * the hook table); the others load cached and flush before jumping.
	 */
	{
		unsigned int *dst = (unsigned int *)CONFIG_SPL_TEXT_BASE;
		void (*spl_entry)(void);

		if (soc->uncached_load)
			dst = (unsigned int *)(CONFIG_SPL_TEXT_BASE | 0x20000000);

		soc->nor_read(soc->spl_nor_offs, dst, CONFIG_SPL_MAX_SIZE);
		if (!soc->uncached_load)
			flush_cache(CONFIG_SPL_TEXT_BASE, CONFIG_SPL_MAX_SIZE);
		soc->puts("TPL: SPL loaded, jumping\n");

		spl_entry = (void (*)(void))(CONFIG_SPL_TEXT_BASE + 0x800);
		spl_entry();
	}
}
