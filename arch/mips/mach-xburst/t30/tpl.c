// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 TPL: cache-as-RAM DDR bring-up.
 *
 * The mask ROM loads this tiny TPL into the cache-as-RAM window and runs it
 * before any DRAM exists. Unlike the old single-stage SPL (which brought DDR up
 * imperatively, self-completed its NOR load past the 0x6800 ROM cap, made itself
 * DRAM-resident, then danced around the dead L2 with an Index-Store-Tag drop +
 * cache-wash + write-through hand-off), the TPL is the mainline rk3328 DMC
 * shape: it runs a minimal driver model off dtoc-baked platdata
 * (TPL_OF_PLATDATA, no libfdt) and the UCLASS_RAM probe brings up PLL + DDR (the
 * shared XBurst1 Innophy driver, drivers/ram/ingenic/ddr_t31.c). With DDR alive
 * the tail depends on the boot medium:
 *   - NOR (cold boot): load the DRAM-resident SPL from SPI-NOR and jump to it;
 *     the SPL then runs the full DM scan and loads U-Boot proper from NOR.
 *   - USB (SPL_T30_USB_BOOT): there is no on-flash SPL - return into the mask
 *     ROM's USB loop, and the ROM uploads U-Boot proper to DRAM directly.
 *
 * The gen-1 T30 ROM enables the L2 (Config7 bit 1) but never runs its
 * cache_init, so it is non-functional and cannot be software-(re)initialised:
 * stale tags corrupt cached DRAM fetches. The TPL itself never touches DRAM
 * cached (it runs in the L1 cache-as-RAM window and writes the SPL out through
 * the uncached window), so it never trips the dead L2; the DRAM-resident SPL
 * runs uncached for the same reason (see soc.c). This is why the SPL must be
 * loaded uncached here - a cached load of the ~50 KB SPL would overflow the
 * 32 KB L1 and spill dirty lines into the L2 (which has no clean/invalidate op),
 * so they would never reach DRAM.
 *
 * Keeping gd, the malloc-f heap and the stack inside the cache-as-RAM window
 * means nothing touches the (un-init'd) DRAM before the RAM probe.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <dm.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t30.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * SPI-NOR offset of the DRAM-resident SPL. binman lays the flash out as TPL@0
 * (the mask ROM loads this), then the SPL here, then U-Boot.
 */
#define T30_TPL_SPL_NOR_OFFS	0x8000

/*
 * The XBurst start.S sets up neither gd nor a fresh stack (it keeps the
 * mask-ROM sp), leaving both to C - so gd and the early-malloc pool live in
 * BSS, which for the TPL is linked into the cache-as-RAM window (no DRAM yet).
 * spl_common_init() leaves gd->malloc_base as set here because CFG_MALLOC_F_ADDR
 * is undefined for the TPL (see configs/isvp-t30.h).
 */
static gd_t gdata __section(".bss");
static u8 tpl_malloc_f[CONFIG_VAL(SYS_MALLOC_F_LEN)] __section(".bss");

void board_init_f(ulong dummy)
{
	struct udevice *dev;

	clk_ungate_uart(T30_CONSOLE_UART);
	t30_spl_serial_init();
	t30_spl_puts("\nT30 TPL\n");

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
	t30_spl_puts("T30 TPL: DDR up\n");

	t30_spl_sfc_clk_init();

#ifdef CONFIG_SPL_T30_USB_BOOT
	/*
	 * USB-boot: no on-flash SPL stage. DDR is up and the SFC clock is set,
	 * so return into the mask ROM's USB loop (start.S kept the bootrom sp,
	 * so a plain return resumes it); the ROM then uploads U-Boot proper to
	 * DRAM and jumps to it.
	 */
	t30_spl_puts("T30 TPL: DDR up, return to ROM\n");
#else
	/*
	 * NOR: DDR is alive. Load the SPL image from SPI-NOR straight into DRAM
	 * through the uncached window: the dead L2 plus the ~50 KB SPL
	 * overflowing the 32 KB L1 means a cached load would spill dirty lines
	 * into the un-cleanable L2 and never reach DRAM. Then jump past the SPL
	 * boot header (0x800) to the entry, exactly how the mask ROM launched
	 * this TPL. The SPL switches K0 to uncached at entry and runs the full
	 * DM scan from DRAM, clear of the dead L2 and with no size ceiling.
	 */
	{
		void (*spl_entry)(void);

		t30_spl_nor_read(T30_TPL_SPL_NOR_OFFS,
				 (unsigned int *)(CONFIG_SPL_TEXT_BASE |
						  0x20000000),
				 CONFIG_SPL_MAX_SIZE);
		t30_spl_puts("T30 TPL: SPL loaded, jumping\n");

		spl_entry = (void (*)(void))(CONFIG_SPL_TEXT_BASE + 0x800);
		spl_entry();
	}
#endif
}
