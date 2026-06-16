// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T10 TPL: cache-as-RAM DDR bring-up.
 *
 * The mask ROM loads this tiny TPL into the 32 KB L1 cache-as-RAM window and
 * runs it before any DRAM exists. Unlike the old non-DM single-stage SPL (which
 * brought DDR up imperatively and hand-loaded U-Boot proper itself), the TPL is
 * the mainline rk3328 DMC shape: it runs a minimal driver model off dtoc-baked
 * platdata (TPL_OF_PLATDATA, no libfdt) and the UCLASS_RAM probe brings up PLL +
 * DDR (the shared Synopsys-DWC driver, drivers/ram/ingenic/ddr_t20.c). With DDR
 * alive the tail depends on the boot medium:
 *   - NOR (cold boot): load the DRAM-resident SPL from SPI-NOR and jump to it;
 *     the SPL then runs the full DM scan and loads U-Boot proper from NOR, with
 *     no cache-as-RAM size ceiling.
 *   - USB (SPL_T10_USB_BOOT): there is no on-flash SPL - return into the mask
 *     ROM's USB loop, and the ROM uploads U-Boot proper to DRAM directly.
 *
 * Keeping the malloc-f heap, stack and gd inside the cache-as-RAM window means
 * nothing touches the (un-init'd) DRAM before the RAM probe - which matters on
 * T10 because its Synopsys-DWC controller hangs on any pre-DDR DRAM access.
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
#include <mach/t10.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * SPI-NOR offset of the DRAM-resident SPL. binman lays the flash out as TPL@0
 * (the mask ROM loads this), then the SPL here, then U-Boot.
 */
#define T10_TPL_SPL_NOR_OFFS	0x8000

/*
 * The XBurst start.S sets up neither gd nor a fresh stack (it keeps the
 * mask-ROM sp), leaving both to C - so gd and the early-malloc pool live in
 * BSS, which for the TPL is linked into the cache-as-RAM window (no DRAM yet;
 * T10's DWC controller hangs on any pre-init DRAM access). spl_common_init()
 * leaves gd->malloc_base as set here because CFG_MALLOC_F_ADDR is undefined
 * for the TPL (see configs/isvp-t10.h).
 */
static gd_t gdata __section(".bss");
static u8 tpl_malloc_f[CONFIG_VAL(SYS_MALLOC_F_LEN)] __section(".bss");

void board_init_f(ulong dummy)
{
	struct udevice *dev;

	clk_ungate_uart(T10_CONSOLE_UART);
	t10_spl_serial_init();
	t10_spl_puts("\nT10 TPL\n");

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
	t10_spl_puts("T10 TPL: DDR up\n");

	t10_spl_sfc_clk_init();

#ifdef CONFIG_SPL_T10_USB_BOOT
	/*
	 * USB-boot: no on-flash SPL stage. DDR is up and the SFC clock is set,
	 * so return into the mask ROM's USB loop (start.S kept the bootrom sp,
	 * so a plain return resumes it); the ROM then uploads U-Boot proper to
	 * DRAM and jumps to it.
	 */
	t10_spl_puts("T10 TPL: DDR up, return to ROM\n");
#else
	/*
	 * NOR: DDR is alive. Read the SPL image from SPI-NOR into DRAM at its
	 * link base and jump past its 0x800 boot header to the entry - exactly
	 * how the mask ROM launched this TPL. The SPL then runs the full DM scan
	 * from DRAM with no cache-as-RAM size ceiling.
	 */
	{
		void (*spl_entry)(void);

		t10_spl_nor_read(T10_TPL_SPL_NOR_OFFS,
				 (unsigned int *)CONFIG_SPL_TEXT_BASE,
				 CONFIG_SPL_MAX_SIZE);
		flush_cache(CONFIG_SPL_TEXT_BASE, CONFIG_SPL_MAX_SIZE);
		t10_spl_puts("T10 TPL: SPL loaded, jumping\n");

		spl_entry = (void (*)(void))(CONFIG_SPL_TEXT_BASE + 0x800);
		spl_entry();
	}
#endif
}
