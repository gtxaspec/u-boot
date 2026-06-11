// SPDX-License-Identifier: GPL-2.0+
/*
 * Derive a stable Ethernet MAC address from the Ingenic XBurst SoC's per-die
 * eFUSE chip serial, so every board gets a unique, deterministic MAC without
 * per-unit provisioning.
 *
 * The eFUSE holds a 128-bit chip serial at a fixed address that is the same
 * across the T-series. The derivation below is bit-for-bit identical to the
 * one the running Linux system uses (thingino's overlay/etc/init.d/S03mac),
 * so the bootloader and the OS present the same address - which keeps DHCP
 * reservations and the like stable across the hand-off.
 *
 * It runs from misc_init_r(), after the environment is loaded and before the
 * network stack starts, mirroring arch/arm/mach-rockchip/board.c. A MAC that
 * is already present in the environment always wins.
 */

#include <config.h>
#include <env.h>
#include <init.h>
#include <net.h>
#include <stdio.h>
#include <asm/io.h>
#include <linux/types.h>

/*
 * eFUSE chip-serial words, uncached KSEG1. Identical across the XBurst
 * T-series and the same addresses the Linux side reads, keeping the two in
 * sync. (Physical 0x13540200.. ; KSEG1 base also defined as EFUSE_BASE in the
 * per-SoC mach headers.)
 */
#define XBURST_EFUSE_SERIAL0	0xb3540200
#define XBURST_EFUSE_SERIAL1	0xb3540204
#define XBURST_EFUSE_SERIAL2	0xb3540208
#define XBURST_EFUSE_SERIAL3	0xb354023c

static void mac_from_serial(u8 *mac, u32 s0, u32 s1, u32 s2, u32 s3)
{
	mac[0] = 0x02;			/* locally administered, unicast */
	if (s3) {
		mac[1] = (s3 >> 24) & 0xff;
		mac[2] = (s3 >> 16) & 0xff;
		mac[3] = (s3 >> 8) & 0xff;
		mac[4] = s3 & 0xff;
	} else {
		mac[1] = (s0 >> 24) & 0xff;
		mac[2] = (s0 >> 16) & 0xff;
		mac[3] = (s1 >> 24) & 0xff;
		mac[4] = (s1 >> 16) & 0xff;
	}
	mac[5] = ((s0 ^ s1 ^ s2 ^ s3) & 0xff) | 0x01;
}

int misc_init_r(void)
{
	u8 mac[ARP_HLEN];
	u32 s0, s1, s2, s3;

	/* A MAC provisioned in the environment always wins. */
	if (eth_env_get_enetaddr("ethaddr", mac))
		return 0;

	s0 = readl((void __iomem *)XBURST_EFUSE_SERIAL0);
	s1 = readl((void __iomem *)XBURST_EFUSE_SERIAL1);
	s2 = readl((void __iomem *)XBURST_EFUSE_SERIAL2);
	s3 = readl((void __iomem *)XBURST_EFUSE_SERIAL3);

	if (!(s0 | s1 | s2 | s3)) {
		/* Unfused part: nothing to derive from, use a random MAC. */
		net_random_ethaddr(mac);
		printf("Net:   no eFUSE serial, using random MAC address\n");
	} else {
		mac_from_serial(mac, s0, s1, s2, s3);
	}

	eth_env_set_enetaddr("ethaddr", mac);
	return 0;
}
