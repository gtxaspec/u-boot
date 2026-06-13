// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 SoC SPL bring-up
 *
 * The SPL runs cache-as-RAM: the mask ROM cache-locks the image at
 * 0x80001000 with no backed memory until DDR is up. T30 has a 32 KB L1
 * and a 128 KB L2, but - unlike the T21/T23 mask ROMs - the gen-1 T30
 * ROM enables the L2 (Config7 bit 1) yet never runs its cache_init, so
 * the L2 is non-functional: it cannot be software-(re)initialised post
 * reset, the usable cache-as-RAM budget is only the 32 KB L1, and once
 * DDR is up the stale L2 must be bypassed (KSEG0 set uncached) or its
 * garbage-tagged lines corrupt cached DRAM fetches. DDR is brought up
 * imperatively (shared XBurst1 ddr_t31.c, T30 variant, selected by the
 * DDR node's per-SKU compatible) before driver model. From there both
 * boot modes run the SAME DM-in-SPL flow as T21/T23: t30_spl_to_dram()
 * makes the image DRAM-resident, flush_cache() plus a KSEG0-uncached
 * switch move execution to DRAM past the dead L2, then spl_init() brings
 * driver model up and the UCLASS_RAM probe records the size. They differ
 * only in how the image reaches DRAM and the final hand-off:
 *
 *  - NOR cold boot (#else): the gen-1 (T20-era) ROM loads only the first
 *    T30_ROM_SPL_LOAD bytes, so self_complete() re-reads the tail before
 *    fdtdec; t30_spl_to_dram() then re-reads the whole image from its
 *    pristine NOR source into DRAM, and board_init_r() loads U-Boot from
 *    SPI-NOR via the DM SFC driver. HW-validated cold-booting to a U-Boot
 *    prompt on T30X silicon.
 *
 *  - USB boot (#ifdef): the mask ROM uploaded the image into the cache, so
 *    t30_spl_to_dram() copies it (vars + image + DTB, bounded to its exact
 *    end so the unused image->BSS gap is never read - that would thrash
 *    the 32 KB L1 and evict the running code into dead-L2 garbage) out to
 *    DRAM. After the DM scan the SPL returns into the ROM USB loop; the
 *    ROM then uploads U-Boot proper to DRAM and runs it. HW-validated on
 *    T30L/T30X: full DM scan + U-Boot DFU gadget reading/writing the NOR.
 *
 * Full U-Boot uses driver model. Forward-ported from the vendor T30
 * spl.c.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <cpu_func.h>
#include <dm.h>
#include <fdtdec.h>
#include <linux/libfdt.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t30.h>
#include <mach/t30-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(void);
void clk_ungate_uart(unsigned int idx);
void t30_spl_serial_init(void);
void t30_spl_puts(const char *s);
void t30_spl_putc(char c);
void t30_spl_sfc_clk_init(void);
void t30_spl_nor_read(unsigned int nor_off, unsigned int *dst,
		      unsigned int bytes);
void t30_spl_self_complete(unsigned int image_end);

/* ddr_t31.c: imperative DDR bring-up before driver model (cache-as-RAM). */
struct ingenic_t31_ddr_variant;
const struct ingenic_t31_ddr_variant *ingenic_t31_ddr_get_variant(void);
int ingenic_t31_ddr_sdram_init(const struct ingenic_t31_ddr_variant *cfg);

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t30_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t30_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * Make the cache-resident SPL DRAM-resident so spl_init()'s DM scan (and,
 * on NOR, board_init_r()) run from real DRAM: the bootrom's cache-as-RAM
 * has no backing store, so an evicted clean line is gone and a later
 * cached refill reads back garbage (the dead L2's random tags can even
 * false-hit). Both boot modes need it; they differ in the image source:
 *  - NOR: re-read the whole image (+ appended DTB) from its pristine
 *    source - NOR offset 0 - straight into DRAM through the uncached
 *    window (the cache copy is not trusted: pre-DDR pressure may already
 *    have evicted cold image lines; proven on T23N, layout-dependent).
 *  - USB: no NOR copy matches the ROM-uploaded image, so the cache IS the
 *    only source - copy vars + image + DTB out. Bound the copy to the
 *    image's EXACT end (not BSS_START): the ~40 KB gap between image and
 *    BSS was never uploaded, so reading it would miss in the 32 KB L1,
 *    false-hit the dead L2 and evict the live image. The resident image
 *    reads are all cache hits, so this copy causes no eviction.
 * Runtime data (vars/BSS/stack) has no NOR source and is copied from the
 * cache in either mode: cached read -> uncached (KSEG1) write. These are
 * hot, written throughout board_init_f, so reliably still resident.
 */
static void t30_spl_to_dram(void)
{
	unsigned long a;

#define T30_CP(from, sz) do {						\
	for (a = 0; a < (sz); a += 4)					\
		*(volatile u32 *)(((from) + a) | 0x20000000) =		\
			*(volatile u32 *)((from) + a);			\
} while (0)
#ifdef CONFIG_SPL_T30_USB_BOOT
	{
		/* vars + image + appended DTB, bounded to the DTB's real end */
		unsigned long end = (unsigned long)_image_binary_end;

		end += (fdt_totalsize((void *)end) + 3) & ~3UL;
		T30_CP(0x80000000UL, end - 0x80000000UL);
	}
#else
	t30_spl_nor_read(0, (unsigned int *)(CONFIG_SPL_TEXT_BASE | 0x20000000),
			 CONFIG_SPL_BSS_START_ADDR - CONFIG_SPL_TEXT_BASE);
	T30_CP(0x80000000UL, 0x1000);	/* bootrom vars page */
#endif
	T30_CP(CONFIG_SPL_BSS_START_ADDR, CONFIG_SPL_BSS_MAX_SIZE);
	T30_CP(CONFIG_SPL_STACK - 0x4000, 0x4000);	/* live stack */
#undef T30_CP
}

/*
 * Verify DRAM up to the DT-selected variant size (both T30 SKUs are
 * 64 MB). Two passes:
 *
 *  1. Stuck-bit: walk patterns at offsets within [0, size) at both
 *     the KSEG1 (uncached) and KSEG0 (cached) windows.
 *  2. Alias: write a unique marker per offset across [0, size) via
 *     KSEG1, then read them all back. If the DDR controller is
 *     configured larger than the populated part (the classic
 *     wrong-geometry bug) the high offsets wrap onto the low ones and
 *     the markers collide - so "DDR OK" actually proves the size/
 *     geometry, instead of a write-then-read-same-address test that
 *     silently passes on an aliased part.
 */
static int dram_verify(u32 size)
{
	static const u32 pat[] = {
		0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a,
		0xdeadbeef, 0x12345678,
	};
	const u32 offs[] = {
		0x0, 0x4, 0x100000, size / 4, size / 2, size - 4,
	};
	const u32 bases[] = { 0xa0000000, 0x80000000 };
	int b, o, p;

	for (b = 0; b < 2; b++) {
		for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
			volatile u32 *a =
				(volatile u32 *)(bases[b] + offs[o]);

			for (p = 0; p < (int)ARRAY_SIZE(pat); p++) {
				*a = pat[p];
				if (*a != pat[p]) {
					t30_spl_puts("T30 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t30_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t30_spl_puts(" read ");
					spl_put_hex(*a);
					t30_spl_putc('\n');
					return -1;
				}
			}
		}
	}

	for (o = 0; o < (int)ARRAY_SIZE(offs); o++)
		*(volatile u32 *)(0xa0000000 + offs[o]) = 0xa5000000 | offs[o];
	for (o = 0; o < (int)ARRAY_SIZE(offs); o++) {
		volatile u32 *a = (volatile u32 *)(0xa0000000 + offs[o]);

		if (*a != (0xa5000000 | offs[o])) {
			t30_spl_puts("T30 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t30_spl_puts(" read ");
			spl_put_hex(*a);
			t30_spl_puts(" (controller mis-sized vs part)\n");
			return -1;
		}
	}
	return 0;
}

gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	/*
	 * Zero BSS - start.S jumps straight here without clearing it.
	 * DM-in-SPL needs a clean gd / BSS before spl_init() brings driver
	 * model up.
	 */
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console
	 * works before pll_init() - bring it up first so any later hang
	 * still produces output.
	 */
	clk_ungate_uart(T30_CONSOLE_UART);
	t30_spl_serial_init();

#ifndef CONFIG_SPL_T30_USB_BOOT
	/*
	 * The T20-generation mask ROM loaded only the first
	 * T30_ROM_SPL_LOAD bytes of this image from NOR (the USB loader
	 * uploads it whole). Read the missing tail - which holds most of
	 * .text/.rodata and the appended DTB - from NOR into cached
	 * memory at its link address BEFORE anything touches it
	 * (fdtdec_setup() below reads the DTB), then flush over the TAIL
	 * ONLY: the NOR read populated it via D-side stores, so the
	 * writeback + I-invalidate makes the fresh tail visible to later
	 * I-fetches; the running head must not have its I-stream
	 * invalidated (its only copy is the cache).
	 */
	t30_spl_self_complete((unsigned int)_image_binary_end + 0x1000);
	flush_cache(CONFIG_SPL_TEXT_BASE + T30_ROM_SPL_LOAD,
		    (unsigned int)_image_binary_end + 0x1000 -
		    (CONFIG_SPL_TEXT_BASE + T30_ROM_SPL_LOAD));
#endif

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL)
	 * so pll_init() can match the DDR node's per-SKU compatible and pick
	 * the per-SKU PLL setpoints before driver model comes up.
	 */
	if (fdtdec_setup())
		hang();

	pll_init();

	/* Bring DDR up imperatively (shared ddr_t31 driver, DT-selected SKU). */
	ingenic_t31_ddr_sdram_init(ingenic_t31_ddr_get_variant());

	/*
	 * Make the SPL DRAM-resident, then invalidate the cache so all further
	 * execution refills from DRAM - the bootrom's cache-as-RAM has no
	 * backing store (clean lines vanish on eviction). flush_cache's
	 * I-invalidate is only safe now: before the reload it would kill the
	 * running instruction stream. Both modes do this so the DM scan runs
	 * DRAM-backed and the flow matches T21/T23 - only the final hand-off
	 * differs (USB returns to the ROM; NOR loads U-Boot from SPI-NOR).
	 */
	t30_spl_to_dram();
	flush_cache(0x80000000, 0x100000);

	/*
	 * Run the post-DRAM stage uncached. The gen-1 T30 ROM left the L2
	 * enabled but uninitialised and it cannot be software-(re)init'd, so
	 * its garbage-tagged lines would corrupt cached fetches/loads now that
	 * execution is DRAM-backed. Set Config.K0 [2:0] = 2 (uncached) so every
	 * access bypasses the L1 and the dead L2 straight to DRAM. T23/T31 keep
	 * caching here; only T30's broken L2 forces it. (USB returns to the
	 * mask ROM still uncached - U-Boot proper re-inits its own caches, the
	 * same state the NOR path hands to board_init_r.)
	 */
	{
		unsigned int c0;

		__asm__ __volatile__("mfc0 %0, $16, 0" : "=r" (c0));
		c0 = (c0 & ~7u) | 2u;		/* K0 = 2 (uncached) */
		__asm__ __volatile__("mtc0 %0, $16, 0; nop; nop" : : "r" (c0));
	}

	/*
	 * Bring driver model up and probe the UCLASS_RAM driver. With DDR
	 * already alive, the DM scan's allocations land in DRAM. The bootph
	 * SFC autoprobe inside spl_init() runs without the SFC clock on USB
	 * (NOR sets it via t30_spl_to_dram()/nor_read(); USB's to_dram() copies
	 * from cache) - harmless, U-Boot proper re-probes the SFC cleanly.
	 */
	if (spl_init())
		hang();
	{
		struct udevice *dev;
		struct ram_info ram;

		if (uclass_first_device_err(UCLASS_RAM, &dev))
			hang();
		if (ram_get_info(dev, &ram))
			hang();
		dram_verify((u32)ram.size);
	}

#ifdef CONFIG_SPL_T30_USB_BOOT
	/*
	 * USB boot: DDR is up and driver model is scanned - the same DM-in-SPL
	 * flow T21/T23 run on USB. The mask ROM now uploads U-Boot proper to
	 * DRAM and jumps to it, so just set the SFC clock and return into the
	 * ROM USB loop (start.S kept the bootrom sp, so a plain jr ra resumes
	 * it). HW-validated on T30X: full DM scan runs, U-Boot proper boots and
	 * its DFU gadget reads/writes the SFC-NOR (with the boot strap released,
	 * which un-muxes the shared SFC pin).
	 */
	t30_spl_sfc_clk_init();
	return;
#else
	/*
	 * NOR cold-boot: DDR is up, so hand off to the standard SPL framework
	 * board_init_r(). It sets up the DRAM malloc heap and loads
	 * u-boot-lzma.img from CONFIG_SYS_SPI_U_BOOT_OFFS via the DM SFC
	 * driver (spl_boot_device() == BOOT_DEVICE_SPI), LZMA-decompresses it
	 * and jumps. Does not return.
	 */
	preloader_console_init();
	t30_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
