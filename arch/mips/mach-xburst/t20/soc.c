// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T20 SoC SPL bring-up
 *
 * The SPL runs cache-as-RAM: the mask ROM cache-locks the image at
 * 0x80001000 and there is no backed memory until DDR is up - the pre-DDR
 * budget is the 32 KB L1 (+ 128 KB L2). That cannot hold the SPL image
 * plus the DM scan's heap/stack, so like T21/T23/T30 the DDR is brought
 * up imperatively (T20's own Synopsys DWC driver ddr_t20.c, variant
 * selected by the DDR node's per-SKU compatible) and the SPL makes itself
 * DRAM-resident *before* spl_init() brings driver model up: the image is
 * re-read from its pristine NOR source and the live data copied out of
 * the cache (see t20_spl_to_dram()). From there the flow matches
 * T21/T23/T31 - the UCLASS_RAM probe records the DRAM size, and
 * board_init_r() reads U-Boot proper from SPI-NOR via the DM SFC driver,
 * LZMA-decompresses it and jumps. Full U-Boot uses driver model.
 *
 * The T20-generation mask ROM loads only the first T20_ROM_SPL_LOAD bytes
 * of the SPL, so t20_spl_self_complete() re-reads the tail before fdtdec.
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
#include <mach/t20.h>
#include <mach/t20-sfc.h>

DECLARE_GLOBAL_DATA_PTR;

void pll_init(const void *blob);
void clk_ungate_uart(unsigned int idx);
void t20_spl_serial_init(void);
void t20_spl_puts(const char *s);
void t20_spl_putc(char c);
void t20_spl_sfc_clk_init(void);
void t20_spl_nor_read(unsigned int nor_off, unsigned int *dst,
		      unsigned int bytes);
void t20_spl_nor_read_noclk(unsigned int nor_off, unsigned int *dst,
			    unsigned int bytes);
void t20_spl_self_complete(unsigned int image_end);

/*
 * ddr_t20.c: imperative DDR bring-up before driver model (cache-as-RAM). It
 * parses the &ddr node's "ingenic,sdram-params" array out of @blob itself,
 * so soc.c stays opaque to the params layout.
 */
int ingenic_t20_ddr_sdram_init(const void *blob);

#ifdef CONFIG_XPL_BUILD
static void spl_put_hex(u32 v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	t20_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t20_spl_putc(hex[(v >> i) & 0xf]);
}

/*
 * Make the cache-resident SPL DRAM-resident. The bootrom loaded the SPL
 * into the cache and there is no backing store behind it, so an evicted
 * line is simply gone: clean lines (all of .text/.rodata) are discarded
 * on eviction and a later refill reads back un-initialised DRAM. By the
 * time DDR is up, pre-DDR stack/data cache pressure may ALREADY have
 * evicted cold image lines - the cache copy of the image is not
 * trustworthy, and copying it out would faithfully reproduce the garbage
 * (proven across the T2x family: which cold line dies varies with link
 * layout, so builds flip between booting and hanging). The image (+
 * appended DTB) is therefore re-read from its pristine source - NOR
 * offset 0 - straight into DRAM through the uncached window.
 *
 * The runtime data has no NOR source and IS copied from the cache: cached
 * read -> uncached (KSEG1) write, in separate small spans (reading the
 * un-backed gaps through the cache would itself allocate and evict
 * lines). These lines are hot (written throughout the pre-DDR phase), so
 * unlike the cold image lines they are reliably still resident.
 *
 * The USB-boot SPL was uploaded to the cache by the mask ROM and NOR
 * holds no (matching) image, so there the cache copy is the only source
 * for the image span too.
 */
static void t20_spl_to_dram(void)
{
	unsigned long a;

#define T20_CP(from, sz) do {						\
	for (a = 0; a < (sz); a += 4)					\
		*(volatile u32 *)(((from) + a) | 0x20000000) =		\
			*(volatile u32 *)((from) + a);			\
} while (0)
#ifdef CONFIG_SPL_T20_USB_BOOT
	/* bootrom vars + SPL + DTB */
	T20_CP(0x80000000UL, CONFIG_SPL_BSS_START_ADDR - 0x80000000UL);
#else
	t20_spl_nor_read(0, (unsigned int *)(CONFIG_SPL_TEXT_BASE |
					     0x20000000),
			 CONFIG_SPL_BSS_START_ADDR - CONFIG_SPL_TEXT_BASE);
	T20_CP(0x80000000UL, 0x1000);	/* bootrom vars page */
#endif
	T20_CP(CONFIG_SPL_BSS_START_ADDR, CONFIG_SPL_BSS_MAX_SIZE);
	T20_CP(CONFIG_SPL_STACK - 0x4000, 0x4000);	/* live stack */
#undef T20_CP
}

/*
 * Verify DRAM up to the DT-selected variant size (T20N/T20L are 64 MB,
 * T20X 128 MB). Two passes:
 *
 *  1. Stuck-bit: walk patterns at offsets within [0, size) at both the
 *     KSEG1 (uncached) and KSEG0 (cached) windows.
 *  2. Alias: write a unique marker per offset across [0, size) via KSEG1,
 *     then read them all back. If the DDR controller is configured larger
 *     than the populated part (the classic wrong-geometry bug) the high
 *     offsets wrap onto the low ones and the markers collide - so "DDR OK"
 *     actually proves the size/geometry.
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
					t20_spl_puts("T20 SPL: DDR FAIL @");
					spl_put_hex((u32)(uintptr_t)a);
					t20_spl_puts(" wrote ");
					spl_put_hex(pat[p]);
					t20_spl_puts(" read ");
					spl_put_hex(*a);
					t20_spl_putc('\n');
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
			t20_spl_puts("T20 SPL: DDR ALIAS @");
			spl_put_hex((u32)(uintptr_t)a);
			t20_spl_puts(" read ");
			spl_put_hex(*a);
			t20_spl_puts(" (controller mis-sized vs part)\n");
			return -1;
		}
	}
	return 0;
}

gd_t gdata __section(".bss");

void board_init_f(ulong dummy)
{
	const void *blob;

	clk_ungate_uart(T20_CONSOLE_UART);
	t20_spl_serial_init();

	/*
	 * Bring pll + DDR up FIRST, gd-free, so the standard-map BSS and the
	 * malloc-f heap below land in real DRAM by the time anything touches them.
	 *
	 * Why T20 differs from T21/T23/T30: those have an Innophy DDR controller
	 * that returns garbage on a pre-DDR access, so their SPL clears the far
	 * 0x80012000 BSS as cache-as-RAM before DDR. T20's Synopsys-DWC controller
	 * HANGS on any pre-DDR DRAM access, and the T20 mask ROM only sets up a
	 * 32 KB L1 cache-as-RAM window (it skips the S-cache pass the other ROMs
	 * run, and the 128 KB L2 can't be locked in from software - it has no
	 * Index-Store-Tag). So we parse the DDR node's per-SKU compatible straight
	 * out of the appended DTB (gd-free, at _image_binary_end - the USB loader
	 * put the whole image incl DTB in the window), bring DDR up, and only THEN
	 * clear BSS / set gd / run fdtdec + the DM scan, all from real DRAM. The
	 * heavy SPL tail likewise runs DRAM-resident after to_dram().
	 */
#ifndef CONFIG_SPL_T20_USB_BOOT
	/*
	 * NOR: the mask ROM loaded only the first T20_ROM_SPL_LOAD bytes; the
	 * appended DTB lives in the un-loaded tail and the whole 54 KB tail won't
	 * fit the 32 KB window. Stage JUST the DTB (small - OF_SPL_REMOVE_PROPS
	 * strips it) from NOR into a scratch spot at the top of the window so
	 * get_variant() can read the per-SKU compatible gd-free before DDR. The
	 * SFC is still set up from the ROM's head load. to_dram() later re-reads
	 * the whole image from NOR (restoring this scratch) and fdtdec_setup()
	 * then reads the DTB at its real _image_binary_end, now DRAM-resident.
	 */
	{
		void *dtb = (void *)0x80007800;	/* window, past the 0x6800 head */
		unsigned int off = (unsigned int)_image_binary_end -
				   CONFIG_SPL_TEXT_BASE;
		unsigned int sz;

		/*
		 * pll_init() has not run, so MPLL is down: use the no-clock NOR
		 * read (reuses the bootrom's live SFC clock). The regular
		 * t20_spl_nor_read() would point the SFC cgu at the dead MPLL and
		 * hang.
		 */
		t20_spl_nor_read_noclk(off, dtb, 64);
		sz = fdt_totalsize(dtb);
		if (sz < 0x40 || sz > 0x800)
			hang();
		t20_spl_nor_read_noclk(off, dtb, sz);
		blob = dtb;
	}
#else
	blob = (const void *)_image_binary_end;
#endif

	pll_init(blob);

	ingenic_t20_ddr_sdram_init(blob);

#ifndef CONFIG_SPL_T20_USB_BOOT
	/*
	 * The DTB scratch at 0x80007800 was written through the cache (it is in
	 * the cache-as-RAM window). It is no longer needed - get_variant() has
	 * run - and it sits at a link address inside .text, so its dirty cache
	 * line would, at the flush_cache() below, write back over the copy of
	 * .text that to_dram() re-reads from NOR into that same DRAM address.
	 * Drop it now (before to_dram) so the re-read image is the final word;
	 * otherwise the boot works only by cache-eviction luck (the silent
	 * layout-dependent hang the T2x family is prone to).
	 */
	invalidate_dcache_range(0x80007800, 0x80008000);
#endif

	/*
	 * DDR is live. Make the SPL DRAM-resident (NOR re-reads the pristine
	 * image+DTB from flash; USB copies it from the cache) and flush, then run
	 * uncached past the dead L2 (same as T30) - now every access, including the
	 * far BSS clear below, goes straight to real DRAM. to_dram() must run
	 * before the BSS clear so the cache copy of the image is still pristine
	 * (the clear would otherwise evict cold image lines on USB).
	 */
	t20_spl_to_dram();
	flush_cache(0x80000000, 0x100000);
	{
		unsigned int c0;

		__asm__ __volatile__("mfc0 %0, $16, 0" : "=r" (c0));
		c0 = (c0 & ~7u) | 2u;		/* K0 = 2 (uncached) */
		__asm__ __volatile__("mtc0 %0, $16, 0; nop; nop" : : "r" (c0));
	}

	/* Now in real DRAM: clear BSS, point gd at it, publish the DTB. */
	memset(__bss_start, 0, (size_t)__bss_end - (size_t)__bss_start);
	gd = &gdata;

	if (fdtdec_setup())
		hang();

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

#ifdef CONFIG_SPL_T20_USB_BOOT
	/*
	 * USB-boot stage1: clocks and DDR are up. Set up the SFC clock so
	 * U-Boot proper (uploaded to DRAM by the mask ROM) can probe NOR,
	 * then return into the mask ROM USB loop (start.S kept the bootrom
	 * sp, so a plain jr ra resumes it).
	 */
	t20_spl_sfc_clk_init();
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
	t20_spl_sfc_clk_init();
	board_init_r(NULL, 0);
	__builtin_unreachable();
#endif
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_SPI;
}
#endif /* CONFIG_XPL_BUILD */
