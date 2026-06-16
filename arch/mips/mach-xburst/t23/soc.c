// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 SoC SPL bring-up
 *
 * The SPL runs cache-as-RAM: the mask ROM cache-locks the image at
 * 0x80001000 and there is no backed memory until DDR is up - the whole
 * pre-DDR budget is the 16 KB L1 + 64 KB L2 (~80 KB). That cannot hold
 * the SPL image plus the DM scan's heap/stack (T31's 32 KB L1 + 128 KB
 * L2 can), so unlike T31 the DDR is brought up imperatively (shared
 * XBurst1 ddr_t31.c, params from the &ddr node's "ingenic,sdram-params"
 * array) and the SPL makes itself DRAM-resident *before*
 * spl_init() brings driver model up (image re-read from NOR, live data
 * copied out of the cache - see t23_spl_to_dram()). From there the
 * flow matches T31: the UCLASS_RAM probe records the DRAM size, and
 * board_init_r() reads U-Boot proper from SPI-NOR via the DM SFC
 * driver, LZMA-decompresses it and jumps. Full U-Boot uses driver
 * model. Forward-ported from the vendor T23 spl.c.
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
#include <mach/t23.h>

DECLARE_GLOBAL_DATA_PTR;

void clk_ungate_uart(unsigned int idx);
void t23_spl_serial_init(void);
void t23_spl_puts(const char *s);
void t23_spl_putc(char c);
void t23_spl_sfc_clk_init(void);
void t23_spl_nor_read(unsigned int nor_off, unsigned int *dst,
		      unsigned int bytes);

/* ddr_t31.c: imperative pre-DM DDR bring-up (PLLs + DDR from FDT params). */
int ingenic_t31_ddr_bringup_from_fdt(void);

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t23_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t23_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * Make the cache-resident SPL DRAM-resident. The bootrom loaded the SPL
 * into the cache and there is no backing store behind it, so an evicted
 * line is simply gone: clean lines (all of .text/.rodata) are discarded
 * on eviction and a later refill reads back un-initialised DRAM. By the
 * time DDR is up, pre-DDR stack/data cache pressure may ALREADY have
 * evicted cold image lines - the cache copy of the image is not
 * trustworthy, and copying it out would faithfully reproduce the
 * garbage (observed on real T23N silicon: which cold line dies varies
 * with link layout, so builds flip between booting and hanging). The
 * image (+ appended DTB) is therefore re-read from its pristine source
 * - NOR offset 0 - straight into DRAM through the uncached window.
 *
 * The runtime data has no NOR source and IS copied from the cache:
 * cached read -> uncached (KSEG1) write, in separate small spans
 * (reading the un-backed gaps through the cache would itself allocate
 * and evict lines). These lines are hot (written throughout the
 * pre-DDR phase), so unlike the cold image lines they are reliably
 * still resident.
 *
 * The USB-boot SPL was uploaded to the cache by the mask ROM and NOR
 * holds no (matching) image, so there the cache copy is the only
 * source for the image span too.
 */
static void t23_spl_to_dram(void)
{
	unsigned long a;

#define T23_CP(from, sz) do {						\
	for (a = 0; a < (sz); a += 4)					\
		*(volatile u32 *)(((from) + a) | 0x20000000) =		\
			*(volatile u32 *)((from) + a);			\
} while (0)
#ifdef CONFIG_SPL_T23_USB_BOOT
	/* bootrom vars + SPL + DTB */
	T23_CP(0x80000000UL, CONFIG_SPL_BSS_START_ADDR - 0x80000000UL);
#else
	t23_spl_nor_read(0, (unsigned int *)(CONFIG_SPL_TEXT_BASE |
					     0x20000000),
			 CONFIG_SPL_BSS_START_ADDR - CONFIG_SPL_TEXT_BASE);
	T23_CP(0x80000000UL, 0x1000);	/* bootrom vars page */
#endif
	T23_CP(CONFIG_SPL_BSS_START_ADDR, CONFIG_SPL_BSS_MAX_SIZE);
	T23_CP(CONFIG_SPL_STACK - 0x4000, 0x4000);	/* live stack */
#undef T23_CP
}

/*
 * Verify DRAM up to the DT-selected variant size (T23 is 64 MB, or
 * 32 MB for T23DL/DN - never 128 MB). Two passes:
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
					t23_spl_puts("T23 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t23_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t23_spl_puts(" read ");
					spl_put_hex(*a);
					t23_spl_putc('\n');
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
			t23_spl_puts("T23 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t23_spl_puts(" read ");
			spl_put_hex(*a);
			t23_spl_puts(" (controller mis-sized vs part)\n");
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
	clk_ungate_uart(T23_CONSOLE_UART);
	t23_spl_serial_init();

	/*
	 * Make the FDT blob available (OF_SEPARATE: appended after the SPL) so
	 * the imperative bring-up below can read the &ddr node's per-SKU
	 * "ingenic,sdram-params" array before driver model comes up.
	 */
	if (fdtdec_setup())
		hang();

	/*
	 * Bring PLLs + DDR up imperatively, BEFORE spl_init(). The ~80 KB
	 * cache-as-RAM budget cannot hold the SPL image plus the DM scan's
	 * heap/stack, so driver model must run DRAM-backed. Reads the per-SKU
	 * params from the &ddr node and runs the shared ddr_t31 bring-up
	 * (one-shot; the UCLASS_RAM probe below then just records the size).
	 * T31's 32 KB L1 + 128 KB L2 fits DM-in-SPL, so it brings DDR up via
	 * the uclass probe instead.
	 */
	if (ingenic_t31_ddr_bringup_from_fdt())
		hang();

	/*
	 * Make the SPL DRAM-resident, then invalidate the cache so all
	 * further execution refills from DRAM - past here the cache is a
	 * normal write-back cache over real DRAM, not the bootrom's cache-
	 * as-RAM (whose clean lines vanish on eviction). flush_cache's
	 * I-invalidate is only safe now: before the reload it would kill
	 * the running instruction stream.
	 */
	t23_spl_to_dram();
	flush_cache(0x80000000, 0x100000);

	/*
	 * Bring driver model up and probe the UCLASS_RAM driver. With DDR
	 * already alive, the DM scan's allocations land in DRAM.
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

#ifdef CONFIG_SPL_T23_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Set up the SFC clock so
	 * U-Boot proper (uploaded to DRAM by the mask ROM) can probe NOR,
	 * then return into the mask ROM USB loop (start.S kept the bootrom
	 * sp, so a plain jr ra resumes it).
	 */
	t23_spl_sfc_clk_init();
	return;
#else
	/*
	 * NOR cold-boot: DDR is up, so hand off to the standard SPL
	 * framework board_init_r(). It sets up the DRAM malloc heap and
	 * loads u-boot-lzma.img from CONFIG_SYS_SPI_U_BOOT_OFFS via the DM
	 * SFC driver (spl_boot_device() == BOOT_DEVICE_SPI),
	 * LZMA-decompresses it and jumps. Does not return.
	 */
	preloader_console_init();
	t23_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
