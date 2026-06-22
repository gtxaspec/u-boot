// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T23 TPL bare-metal SD (MSC) reader.
 *
 * On an SD (MSC) boot the first stage is the TPL. The mask ROM does the
 * full SD card init (CMD0/8/41/2/3/7), reads the headered TPL off the
 * card and jumps to it; by then the card is initialised and selected, and
 * that card state survives the TPL's DDR bring-up. So this reader reuses
 * the live card instead of re-initialising it: it re-asserts the 512-byte
 * block length and streams blocks via CMD18, exactly like the ROM's own
 * reader. It reads from block 0 and discards up to @skip bytes before
 * storing, so it is addressing-agnostic (works on byte- and block-
 * addressed cards alike), mirroring the ROM. (The SD *clock* does need
 * reprogramming - see t23_tpl_msc_read() - because pll_init has moved
 * MPLL out from under the ROM's divider.)
 *
 * The read is hand-rolled rather than going through DM MMC because the
 * full MMC stack does not fit the bootrom's cache-as-RAM lock window in
 * the TPL - the same reason the NOR path uses a bare-metal SFC read.
 * Symmetric with t23_spl_nor_read(); built only for the MSC TPL
 * (CONFIG_SPL_MMC).
 */

#include <config.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <mach/t23.h>

/* MSC0 controller, KSEG1 (uncached - the TPL runs cache-as-RAM) */
#define MSC_BASE		0xb3450000
#define MSC_STAT		0x004
#define MSC_CMDAT		0x00c
#define MSC_BLKLEN		0x018
#define MSC_NOB			0x01c
#define MSC_IREG		0x028
#define MSC_CMD			0x02c
#define MSC_ARG			0x030
#define MSC_RXFIFO		0x038
#define MSC_STRPCL		0x000

#define STAT_TIME_OUT_READ	BIT(0)
#define STAT_CRC_READ_ERROR	BIT(4)
#define STAT_DATA_FIFO_EMPTY	BIT(6)
#define IREG_RXFIFO_RD_REQ	BIT(5)
#define IREG_END_CMD_RES	BIT(2)
#define STRPCL_START_OP		BIT(2)
#define CMDAT_BUSY		BIT(6)
#define CMDAT_DATA_EN		BIT(3)
#define CMDAT_RESPONSE_R1	(0x1 << 0)

static inline u32 msc_readl(u32 reg)
{
	return readl((void __iomem *)(MSC_BASE + reg));
}

static inline void msc_writel(u32 val, u32 reg)
{
	writel(val, (void __iomem *)(MSC_BASE + reg));
}

/* Issue one command (T31-family MSC: a plain START_OP, no clock handshake). */
static void msc_cmd(u32 cmd, u32 arg, u32 cmdat)
{
	int i;

	msc_writel(cmd, MSC_CMD);
	msc_writel(arg, MSC_ARG);
	msc_writel(cmdat, MSC_CMDAT);
	msc_writel(0xffffffff, MSC_IREG);		/* clear stale status */
	msc_writel(STRPCL_START_OP, MSC_STRPCL);

	for (i = 0; i < 100000; i++)
		if (msc_readl(MSC_IREG) & IREG_END_CMD_RES)
			break;
	msc_writel(IREG_END_CMD_RES, MSC_IREG);
}

/*
 * Read @bytes into @dst from the card, where the wanted data begins
 * @skip bytes into the image (block-0-relative). Streams 512-byte blocks
 * with CMD18 and drains the RX FIFO 16 words (64 bytes) at a time,
 * re-anchoring @dst until @skip bytes have passed so the leading region
 * is overwritten in place and discarded - the ROM's FUN_bfc00f9c idiom.
 * @dst should be a KSEG1 (uncached) pointer so the fill does not allocate
 * cache lines over the cache-as-RAM window.
 */
void t23_tpl_msc_read(u32 skip, u32 *dst, u32 bytes)
{
	u32 total = skip + bytes;
	u32 total_iters = (total + 63) >> 6;		/* 64-byte chunks */
	u32 store_iters = (bytes + 63) >> 6;
	u32 *p = dst;
	u32 it, w, st;

	/*
	 * Program the SD read clock. The ROM's INGE block used to do this; with a
	 * TPL the reader does it instead, because pll_init has by now moved MPLL
	 * out from under the ROM's default divider (leaving it corrupts the read).
	 * Source MPLL and divide by 64 (div = 31): that holds the SD clock at or
	 * below 25 MHz (SD default-speed) for any MPLL these XBurst1 parts run
	 * (~0.6-1.5 GHz) without decoding the per-SoC PLL register, and the SPL
	 * read is small so the exact rate does not matter.
	 * rate = MPLL / ((div + 1) * 2).
	 */
	writel(MSCCDR_SRC_MPLL | MSCCDR_CE | 31,
	       (void __iomem *)(CPM_BASE + CPM_MSC0CDR));
	while (readl((void __iomem *)(CPM_BASE + CPM_MSC0CDR)) & MSCCDR_BUSY)
		;

	msc_cmd(16, 512, CMDAT_RESPONSE_R1);		/* SET_BLOCKLEN */
	msc_writel(512, MSC_BLKLEN);
	msc_writel((total + 0x1ff) >> 9, MSC_NOB);
	msc_cmd(18, 0, CMDAT_RESPONSE_R1 | CMDAT_DATA_EN); /* READ_MULTIPLE */

	for (it = total_iters; it != 0; it--) {
		do {
			st = msc_readl(MSC_STAT);
			if (st & (STAT_TIME_OUT_READ | STAT_CRC_READ_ERROR))
				goto stop;	/* leave dst short - SPL fails visibly */
		} while (st & STAT_DATA_FIFO_EMPTY);

		if (store_iters <= it)
			p = dst;		/* still skipping: discard in place */

		while (!(msc_readl(MSC_IREG) & IREG_RXFIFO_RD_REQ))
			;
		for (w = 0; w < 16; w++)
			*p++ = msc_readl(MSC_RXFIFO);
	}

stop:
	msc_cmd(12, 0, CMDAT_RESPONSE_R1 | CMDAT_BUSY);	/* STOP_TRANSMISSION */
}
