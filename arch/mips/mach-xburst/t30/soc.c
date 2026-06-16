// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T30 SoC SPL bring-up
 *
 * This SPL is DRAM-resident: the TPL (tpl.c) runs first in the cache-as-RAM
 * window, brings up PLL + DDR (the shared XBurst1 Innophy driver ddr_t31.c via
 * the UCLASS_RAM probe), then loads this SPL from SPI-NOR into real DRAM and
 * jumps to it. So none of the cache-as-RAM gymnastics the old single-stage SPL
 * needed (imperative pre-DM DDR, the self_complete() tail read past the 0x6800
 * ROM cap, the to_dram() re-read) apply here.
 *
 * T30's one remaining quirk is the dead L2: the gen-1 ROM enables it (Config7
 * bit 1) but never runs its cache_init, so it cannot be software-initialised
 * and its stale reset tags corrupt cached DRAM fetches. The SPL handles this
 * the same proven way the old single-stage SPL did, only simpler now that it is
 * DRAM-resident:
 *   - board_init_f switches K0 to cacheable write-through as its first act, so
 *     reads are cached (the board_init_r LZMA decompress stays fast) but every
 *     store goes straight to DRAM and no dirty line ever evicts into the dead
 *     L2. Set before gd/DRAM is touched (no cached-then-uncached gap), and the
 *     cached ABI prologue saves are never read back (board_init_f never returns).
 *   - before the heavy board_init_r working set, t30_l2_wash() evicts the stale
 *     L2: drop L1 (no writeback needed - write-through left nothing dirty), then
 *     stream ~4 MB of clean DRAM through L1+L2 so every set/way is refilled.
 *   - spl_board_prepare_for_boot() flushes and hands U-Boot proper an uncached
 *     K0; its start.S re-inits the caches (mips_cache_reset), so it runs cached.
 * The flow is otherwise the plain T31/T23 one: fdtdec + the DM scan, the
 * UCLASS_RAM probe records the (already-up) DRAM size, and board_init_r() reads
 * U-Boot proper from SPI-NOR via the DM SFC driver, LZMA-decompresses it and
 * jumps.
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <config.h>
#include <cpu_func.h>
#include <dm.h>
#include <fdtdec.h>
#include <hang.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/string.h>
#include <mach/t30.h>

DECLARE_GLOBAL_DATA_PTR;

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
 * Verify DRAM up to the DT-selected SKU size (T30L/T30N are 64 MB, T30X/T30A
 * 128 MB). Two passes:
 *
 *  1. Stuck-bit: walk patterns at offsets within [0, size) at both the
 *     KSEG1 (uncached) and KSEG0 (cached) windows.
 *  2. Alias: write a unique marker per offset across [0, size) via KSEG1, then
 *     read them all back. If the DDR controller is configured larger than the
 *     populated part the high offsets wrap onto the low ones and the markers
 *     collide - so "DDR OK" actually proves the size/geometry.
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

/*
 * Evict the gen-1 ROM's stale, un-init'd L2 before the heavy board_init_r
 * working set. XBurst1 has no L2 invalidate/disable op, but the L2 is an
 * ordinary set-associative cache, so evict the stale lines the ordinary way:
 *   1. drop L1 with Index-Store-Tag (no writeback - the SPL runs write-through,
 *      so nothing is dirty);
 *   2. "cache-wash": stream ~4 MB of clean, untouched high DRAM through L1+L2 so
 *      every set/way is refilled and the stale reset-tag lines are pushed out.
 * After this, cached access to the SPL's working set misses and refills from
 * real DRAM instead of false-hitting a stale L2 tag. (Proven on T30 silicon in
 * the old single-stage SPL; here it runs once, after DDR is already up and the
 * SPL is DRAM-resident, so no to_dram() copy is needed first.)
 */
static void t30_l2_wash(void)
{
	unsigned long a;
	unsigned long p = 0x82000000UL, e = 0x82400000UL;

	__asm__ __volatile__("mtc0 $0, $28, 0");	/* TagLo = 0 */
	for (a = 0x80000000UL; a != 0x80008000UL; a += 0x20)
		__asm__ __volatile__(".set push; .set noreorder;"
				     "cache 0x08, 0(%0);"	/* Index-Store-Tag I */
				     "cache 0x09, 0(%0);"	/* Index-Store-Tag D */
				     ".set pop" : : "r" (a) : "memory");
	__asm__ __volatile__("sync");

	/*
	 * Register-only wash (load to $zero, counter in a register, no stack
	 * touch) so its own loop state is never evicted mid-stream.
	 */
	__asm__ __volatile__(
		".set push; .set noreorder;"
		"1: lw $0, 0(%0);"
		"   bne %0, %1, 1b;"
		"   addiu %0, %0, 0x20;"
		".set pop"
		: "+r" (p) : "r" (e) : "memory");
	__asm__ __volatile__("sync");
}

gd_t gdata __section(".bss");

/*
 * Called by the SPL framework right before it jumps to U-Boot proper. The
 * write-through SPL leaves no dirty lines, but board_init_r decompressed U-Boot
 * into DRAM via the cache, so flush it back, then hand off UNCACHED (K0 = 2).
 * U-Boot proper's start.S runs mips_cache_reset (CONFIG_MIPS_CACHE_SETUP), which
 * invalidates the caches and re-enables K0 cachable cleanly - so U-Boot proper
 * and the kernel still run cached. Entering U-Boot proper cached hangs (it
 * inherits the SPL's live cache state over the dead L2); the uncached hand-off
 * is the proven-good path.
 */
void spl_board_prepare_for_boot(void)
{
	unsigned int c0;

	flush_cache(0x80000000, 0x00c00000);

	__asm__ __volatile__("mfc0 %0, $16, 0" : "=r" (c0));
	c0 = (c0 & ~7u) | 2u;		/* K0 = 2 (uncached) */
	__asm__ __volatile__("mtc0 %0, $16, 0; ehb" : : "r" (c0));
}

void board_init_f(ulong dummy)
{
	struct udevice *dev;
	struct ram_info ram;
	unsigned int c0;

	/*
	 * Run the SPL cacheable write-through (Config0.K0 = 1): reads are cached
	 * (fast board_init_r LZMA), every store goes straight to DRAM so no dirty
	 * line evicts into the gen-1 ROM's dead L2. Set first, before gd or any
	 * DRAM write, so the whole SPL is coherent; the cached ABI prologue saves
	 * above are never read back (board_init_f does not return).
	 */
	__asm__ __volatile__("mfc0 %0, $16, 0" : "=r" (c0));
	c0 = (c0 & ~7u) | 1u;
	__asm__ __volatile__("mtc0 %0, $16, 0; ehb" : : "r" (c0));

	/*
	 * The mask ROM leaves a usable EXTAL-based clock, so the console works
	 * before the PLLs are up - bring it up first so any later hang still
	 * produces output.
	 */
	clk_ungate_uart(T30_CONSOLE_UART);
	t30_spl_serial_init();

	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;

	/*
	 * Wash the stale L2 now, before fdtdec/the DM scan/board_init_r read
	 * through the cache. Write-through left nothing dirty, so the drop +
	 * wash lose no live data.
	 */
	t30_l2_wash();

	if (fdtdec_setup())
		hang();
	if (spl_init())
		hang();
	if (uclass_first_device_err(UCLASS_RAM, &dev))
		hang();
	if (ram_get_info(dev, &ram))
		hang();
	dram_verify((u32)ram.size);

	preloader_console_init();
	t30_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
