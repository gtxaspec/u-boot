// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T40 SPL SPI-NOR loader.
 *
 * STUB: this is a placeholder while we port the T40 SPL loader. The
 * vendor T40 U-Boot 2013 branch has a working SFC-NOR SPL loader at
 * common/spl/spl_sfc_nor.c with the T40 register model in
 * arch/mips/include/asm/arch-t40/sfc.h (same SFC v2 IP as A1).
 *
 * For an initial QEMU-driven bring-up the SPL can defer to the
 * mainline SPL framework via spl_load_image_fat / network if needed,
 * but real-silicon NOR boot requires the bespoke SFC loader. Same
 * pattern as a1/sfc.c.
 */

#include <asm/io.h>
#include <mach/t40.h>

void t40_spl_sfc_clk_init(void)
{
	/*
	 * TODO: ungate SFC + program SFCCDR for ~50 MHz from MPLL. See
	 * vendor T40 clk.c -> cgu_clks_set() for the SFC entry.
	 */
}

void t40_spl_load_uboot(void)
{
	/*
	 * TODO: faithful transliteration of A1's a1_spl_load_uboot:
	 *   - sfc_init()
	 *   - read mkimage header at T40_UBOOT_OFFSET (probably 0x10000)
	 *   - allocate scratch + heap in DRAM
	 *   - LZMA-decompress to CONFIG_TEXT_BASE (0x80100000)
	 *   - jump to (void (*)(void))CONFIG_TEXT_BASE
	 * Reuses t31-sfc.h / a1/sfc.c structure; the T40 SFC controller
	 * is the same IP block as A1 and T32 with identical bit layout.
	 */
}
