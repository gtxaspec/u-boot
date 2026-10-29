// SPDX-License-Identifier: GPL-2.0+
/*
 * Shared XBurst1 TPL bare-metal SD reader for the SDHCI-class MSC.
 *
 * T32 and T33 dropped the legacy Ingenic MSC register interface that
 * mach-xburst/msc.c drives: their MSC0/MSC1 are standard SDHCI blocks at
 * 0x13060000/0x13070000, and the mask ROM reads the first stage off the card
 * through SDHCI too. Those SoCs point their msc_read hook here instead.
 *
 * Like the legacy reader this reuses the card the ROM left initialised and
 * selected, bus width and all: it streams blocks from LBA 0 with one CMD18
 * and discards up to
 * @skip bytes before storing, so it is addressing-agnostic (byte- and
 * block-addressed cards alike). PIO, not ADMA: the TPL runs in the bootrom's
 * cache-as-RAM window, where a descriptor-driven path does not fit.
 *
 * The SD clock does not need the MSC0CDR rewrite the legacy reader performs.
 * MSC0CDR[31:30] selects the source (T32: 0 sclka, 1 mpll, 2 vpll, 3 ext;
 * T33: 0 sclka, 1 mpll, 2 ext) and the rate is source / ((CDR[7:0] + 1) * 4);
 * the vendor's own SDHCI driver leaves the controller's divider at zero and
 * sets the rate through MSC0CDR alone. The ROM selects ext with divider 0,
 * EXCLK/4 = 6 MHz, which is independent of every PLL the TPL then moves - so
 * the reader only re-asserts that selection, from the SoC's own source value,
 * rather than depending on what the ROM happened to leave behind.
 */

#include <config.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <mach/xburst-tpl.h>

/* SDHCI block, KSEG1 (uncached - the TPL runs cache-as-RAM). */
#define SDHCI_BASE		(xburst_tpl_soc.msc_base)
#define SDHCI_BLOCK_SIZE	0x004	/* 16-bit */
#define SDHCI_BLOCK_COUNT	0x006	/* 16-bit */
#define SDHCI_ARGUMENT		0x008
#define SDHCI_TRANSFER_MODE	0x00c	/* 16-bit */
#define SDHCI_COMMAND		0x00e	/* 16-bit */
#define SDHCI_BUFFER		0x020
#define SDHCI_PRESENT_STATE	0x024
#define SDHCI_CLOCK_CONTROL	0x02c	/* 16-bit */
#define SDHCI_INT_STATUS	0x030

#define STATE_CMD_INHIBIT	BIT(0)
#define STATE_DAT_INHIBIT	BIT(1)
#define STATE_BUF_READ_ENABLE	BIT(11)

#define INT_CMD_COMPLETE	BIT(0)
#define INT_XFER_COMPLETE	BIT(1)
#define INT_ERROR		BIT(15)

#define CLOCK_INT_ENABLE	BIT(0)
#define CLOCK_INT_STABLE	BIT(1)
#define CLOCK_SD_ENABLE		BIT(2)

#define TRNS_BLK_CNT_EN		BIT(1)
#define TRNS_AUTO_CMD12		BIT(2)
#define TRNS_READ		BIT(4)
#define TRNS_MULTI		BIT(5)

/* CMD18 READ_MULTIPLE_BLOCK: data present, 48-bit response, check both. */
#define CMD_READ_MULTIPLE	((18 << 8) | 0x3a)

/* CPM MSC0 clock divider, KSEG1 (same offset on every XBurst1 SoC). */
#define CPM_MSC0CDR		0xb0000068
#define MSCCDR_SRC_MASK		(0x3 << 30)
#define MSCCDR_SRC_SHIFT	30
#define MSCCDR_CE		BIT(29)		/* apply (self-clears) */
#define MSCCDR_BUSY		BIT(28)
#define MSCCDR_DIV_MASK		0xff

static inline u32 sdhci_readl(u32 reg)
{
	return readl((void __iomem *)(SDHCI_BASE + reg));
}

static inline void sdhci_writel(u32 val, u32 reg)
{
	writel(val, (void __iomem *)(SDHCI_BASE + reg));
}

static inline u16 sdhci_readw(u32 reg)
{
	return readw((void __iomem *)(SDHCI_BASE + reg));
}

static inline void sdhci_writew(u16 val, u32 reg)
{
	writew(val, (void __iomem *)(SDHCI_BASE + reg));
}

/*
 * Read @bytes into @dst from the card, where the wanted data begins @skip
 * bytes into the image (block-0-relative). One CMD18 streams the whole range
 * and the buffer port is drained 16 words (64 bytes) at a time, re-anchoring
 * @dst until @skip bytes have passed so the leading region is overwritten in
 * place and discarded. @dst should be a KSEG1 (uncached) pointer so the fill
 * does not allocate cache lines over the cache-as-RAM window.
 */
void xburst_tpl_sdhci_read(u32 skip, u32 *dst, u32 bytes)
{
	u32 total = skip + bytes;
	u32 total_iters = (total + 63) >> 6;		/* 64-byte chunks */
	u32 store_iters = (bytes + 63) >> 6;
	u32 *p = dst;
	u32 it, w, st, cdr;

	/*
	 * EXCLK/4 = 6 MHz, as the ROM selected; see the header comment. Only
	 * the source and divider fields are written, so the rest of the
	 * register stays as the ROM left it.
	 */
	cdr = readl((void __iomem *)CPM_MSC0CDR);
	cdr &= ~(MSCCDR_SRC_MASK | MSCCDR_DIV_MASK);
	cdr |= xburst_tpl_soc.msc_clk_src << MSCCDR_SRC_SHIFT | MSCCDR_CE;
	writel(cdr, (void __iomem *)CPM_MSC0CDR);
	while (readl((void __iomem *)CPM_MSC0CDR) & MSCCDR_BUSY)
		;
	/* Internal clock first, then the card clock once it reports stable. */
	sdhci_writew(CLOCK_INT_ENABLE, SDHCI_CLOCK_CONTROL);
	while (!(sdhci_readw(SDHCI_CLOCK_CONTROL) & CLOCK_INT_STABLE))
		;
	sdhci_writew(CLOCK_INT_ENABLE | CLOCK_SD_ENABLE, SDHCI_CLOCK_CONTROL);

	while (sdhci_readl(SDHCI_PRESENT_STATE) &
	       (STATE_CMD_INHIBIT | STATE_DAT_INHIBIT))
		;
	sdhci_writel(0xffffffff, SDHCI_INT_STATUS);	/* clear stale status */

	sdhci_writew(512, SDHCI_BLOCK_SIZE);
	sdhci_writew((total + 0x1ff) >> 9, SDHCI_BLOCK_COUNT);
	sdhci_writew(TRNS_MULTI | TRNS_READ | TRNS_AUTO_CMD12 | TRNS_BLK_CNT_EN,
		     SDHCI_TRANSFER_MODE);
	sdhci_writel(0, SDHCI_ARGUMENT);
	sdhci_writew(CMD_READ_MULTIPLE, SDHCI_COMMAND);

	do {
		st = sdhci_readl(SDHCI_INT_STATUS);
		if (st & INT_ERROR)
			return;		/* leave dst short - SPL fails visibly */
	} while (!(st & INT_CMD_COMPLETE));
	sdhci_writel(INT_CMD_COMPLETE, SDHCI_INT_STATUS);

	for (it = total_iters; it != 0; it--) {
		do {
			st = sdhci_readl(SDHCI_PRESENT_STATE);
			if (sdhci_readl(SDHCI_INT_STATUS) & INT_ERROR)
				return;
		} while (!(st & STATE_BUF_READ_ENABLE));

		if (store_iters <= it)
			p = dst;	/* still skipping: discard in place */

		for (w = 0; w < 16; w++)
			*p++ = sdhci_readl(SDHCI_BUFFER);
	}

	/*
	 * Auto-CMD12 ends the transfer; wait for it so the card is idle before
	 * the SPL's DM MMC driver probes the controller from scratch.
	 */
	do {
		st = sdhci_readl(SDHCI_INT_STATUS);
		if (st & INT_ERROR)
			return;
	} while (!(st & INT_XFER_COMPLETE));
	sdhci_writel(0xffffffff, SDHCI_INT_STATUS);
}
