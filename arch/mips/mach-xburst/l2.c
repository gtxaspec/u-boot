// SPDX-License-Identifier: GPL-2.0+
/*
 * XBurst2 L2 cache / NNA ORAM split
 *
 * The on-chip SRAM of the T40, T41 and A1 is shared between the CPU's L2
 * cache and the NNA's ORAM. CCU MSCR[12:10] sets the L2's share
 * (n: 128 KiB << (n - 1)); the ORAM is the rest, starting at 0x12600000
 * plus the L2's share. Ingenic's SPL gives the L2 128 KiB before it starts
 * U-Boot, and the vendor NNA stack is built for that layout: soc-nna maps
 * the ORAM at 0x12620000 only. Out of reset the L2 holds 512 KiB (T40) or
 * 256 KiB (T41), so the NNA would DMA into live L2 lines.
 */

#include <asm/cacheops.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <mach/xburst2.h>

#define CCU_MSCR		0xb2200060	/* same CCU address on all XBurst2 */
#define CCU_MSCR_L2_SHARE	GENMASK(12, 10)
#define L2_SHARE_128K		1

static void cache_blast(int op, unsigned long size, unsigned long line)
{
	unsigned long addr;

	for (addr = CKSEG0; addr < CKSEG0 + size; addr += line)
		mips_cache(op, (void *)addr);
}

/*
 * Write every L1 D and L2 line back and drop them, as the vendor SPL does
 * before it changes the split. The SPL runs from cache lines the bootrom
 * locked; DRAM is up by now, so this also lands the SPL itself in DRAM at
 * its KSEG0 address, where it keeps running from once the lines are gone.
 */
static void flush_all_caches(void)
{
	u32 c1 = read_c0_config1();
	u32 c2 = read_c0_config2();
	unsigned long line, size;

	line = 2UL << ((c1 >> MIPS_CONF1_IL_SHF) & 7);
	size = (64UL << ((c1 >> MIPS_CONF1_IS_SHF) & 7)) * line *
	       (((c1 >> MIPS_CONF1_IA_SHF) & 7) + 1);
	cache_blast(INDEX_INVALIDATE_I, size, line);

	line = 2UL << ((c1 >> MIPS_CONF1_DL_SHF) & 7);
	size = (64UL << ((c1 >> MIPS_CONF1_DS_SHF) & 7)) * line *
	       (((c1 >> MIPS_CONF1_DA_SHF) & 7) + 1);
	cache_blast(INDEX_WRITEBACK_INV_D, size, line);
	__asm__ __volatile__("sync" : : : "memory");

	if (FIELD_GET(MIPS_CONF2_SL, c2)) {
		line = 2UL << FIELD_GET(MIPS_CONF2_SL, c2);
		size = (64UL << FIELD_GET(MIPS_CONF2_SS, c2)) * line *
		       (FIELD_GET(MIPS_CONF2_SA, c2) + 1);
		cache_blast(INDEX_WRITEBACK_INV_SD, size, line);
	}
}

/* Order the CCU write against everything before it, then wait for it. */
static void drain(void)
{
	__asm__ __volatile__("sync" : : : "memory");
	(void)readl((void __iomem *)CKSEG1);
}

void xburst2_set_l2_128k(void)
{
	u32 mscr = readl((void __iomem *)CCU_MSCR);

	if (FIELD_GET(CCU_MSCR_L2_SHARE, mscr) == L2_SHARE_128K)
		return;

	flush_all_caches();
	drain();
	writel((mscr & ~CCU_MSCR_L2_SHARE) |
	       FIELD_PREP(CCU_MSCR_L2_SHARE, L2_SHARE_128K),
	       (void __iomem *)CCU_MSCR);
	drain();
}

void spl_board_prepare_for_boot(void)
{
	xburst2_set_l2_128k();
}
