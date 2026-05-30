// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst common CPU routines (XBurst1 + XBurst2)
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <cpu_func.h>
#include <init.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int print_cpuinfo(void)
{
#if defined(CONFIG_SOC_A1)
	const char *soc = "A1";
	const char *core = "XBurst2";
#elif defined(CONFIG_SOC_T10)
	const char *soc = "T10";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T20)
	const char *soc = "T20";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T21)
	const char *soc = "T21";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T23)
	const char *soc = "T23";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T30)
	const char *soc = "T30";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T31)
	const char *soc = "T31";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T32)
	const char *soc = "T32";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T33)
	const char *soc = "T33";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T40)
	const char *soc = "T40";
	const char *core = "XBurst2";
#elif defined(CONFIG_SOC_T41)
	const char *soc = "T41";
	const char *core = "XBurst2";
#else
	const char *soc = "unknown";
	const char *core = "XBurst";
#endif

	printf("CPU:   Ingenic %s (%s)\n", soc, core);
	return 0;
}

#if !defined(CONFIG_XPL_BUILD) && \
    (defined(CONFIG_SOC_A1) || defined(CONFIG_SOC_T40) || defined(CONFIG_SOC_T41))
/*
 * XBurst2 cache flush. The generic MIPS flush_cache() is a no-op on
 * XBurst2: mips_cache_probe() reads CP0 Config1 and its IL/DL fields
 * read 0, so icache_line_size()/dcache_line_size() are 0 and cache_loop
 * never iterates. XBurst1 reports real line sizes and keeps the generic
 * path, so this override is gated to the XBurst2 SoCs.
 *
 * XBurst2 L1: 8-way x 128 sets x 32 B = 32 KiB I-cache + 32 KiB D-cache.
 *
 * Flow (matches the vendor flush_dcache_range): Hit_Writeback_Inv_D,
 * sync, then a read from KSEG1 to drain the write buffer (D-cache writes
 * pass through a write buffer that must be forced out before a read of
 * the same address sees updated DRAM), then Hit_Invalidate_I so later
 * fetches of the flushed region miss I-cache and refill from DRAM.
 */
#define XB2_LINE_SIZE	32
void flush_cache(ulong start_addr, ulong size)
{
	unsigned long a;
	unsigned long end;
	volatile unsigned int writebuffer;

	if (!size)
		return;

	end = (start_addr + size + XB2_LINE_SIZE - 1) & ~(XB2_LINE_SIZE - 1);
	start_addr &= ~(XB2_LINE_SIZE - 1);

	for (a = start_addr; a < end; a += XB2_LINE_SIZE)
		__asm__ volatile("cache 0x15, 0(%0)" : : "r"(a));	/* Hit_Writeback_Inv_D */
	__asm__ volatile("sync");
	writebuffer = *(volatile unsigned int *)0xa0000000;
	(void)writebuffer;

	for (a = start_addr; a < end; a += XB2_LINE_SIZE)
		__asm__ volatile("cache 0x10, 0(%0)" : : "r"(a));	/* Hit_Invalidate_I */
	__asm__ volatile("sync");
}
#endif
