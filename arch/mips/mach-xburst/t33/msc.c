// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 SPL SD loader: read U-Boot proper off the card the bootrom
 * left initialised and selected, through the shared bare-metal SDHCI reader.
 * The reader keys on xburst_tpl_soc for the controller base and clock
 * source; the T33 has no TPL, so only those two fields are set here.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <mach/t33.h>
#include <mach/xburst-tpl.h>

/* MSC0CDR[31:30]: 0 sclka, 1 mpll, 2 ext (T32 differs: ext is 3). */
const struct xburst_tpl_soc xburst_tpl_soc = {
	.msc_base	= MSC0_BASE,
	.msc_clk_src	= 2,
};

/* The reader streams from LBA 0, so offset @off is skipped, not seeked. */
static int msc_boot_read(u32 off, u32 len, u8 *dst)
{
	xburst_tpl_sdhci_read(off, (u32 *)((uintptr_t)dst | 0x20000000), len);
	return 0;
}

void t33_spl_load_uboot(void)
{
	t33_spl_load_image(msc_boot_read);
}
