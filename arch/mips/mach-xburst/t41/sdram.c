// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T41 DDR3 init - minimal test version
 *
 * Skip our PHY PLL init entirely. Just do DDRC reset + DFI handshake
 * + controller init and see if DDR works WITHOUT touching the PHY PLL.
 * The bootrom may have already configured the PHY PLL for us.
 */

#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <mach/t41.h>
#include <mach/t41-ddr.h>

void t41_spl_puts(const char *s);
void t41_spl_putc(char c);

static void spl_put_hex(u32 v)
{
	static const char h[] = "0123456789abcdef";
	int i;
	t41_spl_puts("0x");
	for (i = 28; i >= 0; i -= 4)
		t41_spl_putc(h[(v >> i) & 0xf]);
}

static inline void ddr_writel(u32 v, u32 off)
{
	*(volatile u32 *)(DDRC_BASE + (off)) = v;
	asm volatile("sync" ::: "memory");
}

static inline u32 ddr_readl(u32 off)
{
	asm volatile("sync" ::: "memory");
	return *(volatile u32 *)(DDRC_BASE + (off));
}

#define PHY_BASE	(DDRC_BASE + DDR_PHY_OFFSET)
#define PHY(off)	(*(volatile u32 *)(PHY_BASE + (off)))

#define APB_OFF		(-0x4e0000 + 0x2000)
#define APB_DWCFG	(APB_OFF + 0x00)
#define APB_DWSTATUS	(APB_OFF + 0x04)
#define APB_REMAP(n)	(APB_OFF + 0x08 + 4*((n)-1))
#define APB_CGUC0	(APB_OFF + 0x64)
#define APB_CGUC1	(APB_OFF + 0x68)
#define APB_PREGPRO	(APB_OFF + 0x6c)

static u32 cpm_readl(unsigned int off) { return readl((void __iomem *)(CPM_BASE + off)); }
static void cpm_writel(u32 v, unsigned int off) { writel(v, (void __iomem *)(CPM_BASE + off)); }

void sdram_init(void)
{
	u32 val, regval;

	t41_spl_puts("DDR: minimal init\n");

	/* DRCG bit 6 */
	cpm_writel(cpm_readl(CPM_DRCG) | 0x40, CPM_DRCG);

	/* DDR CGU: MPLL source + div=1 */
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(3 << 30);
	regval |= (2 << 30);
	cpm_writel(regval, CPM_DDRCDR);
	regval = cpm_readl(CPM_DDRCDR);
	regval &= ~(0xf | (0x3f << 24));
	regval |= (1 << 29) | 1;
	cpm_writel(regval, CPM_DDRCDR);
	while (cpm_readl(CPM_DDRCDR) & (1 << 28));

	/* DDRC reset - busy wait instead of mdelay (timer may not work
	 * if pll_init was skipped) */
	ddr_writel(0xf << 20, 0x010);
	{ volatile int d; for (d=0; d<1000000; d++); }
	ddr_writel(0x8 << 20, 0x010);
	{ volatile int d; for (d=0; d<1000000; d++); }

	t41_spl_puts("PLK pre="); spl_put_hex(PHY(0x180)); t41_spl_puts("\n");

	/* DON'T touch PHY PLL - use bootrom state */
	/* Just configure MEM_CFG, DQ_WIDTH, CWL, CL, AL */
	val = PHY(0x004); val &= ~0xff; val |= DDRP_MEMCFG_VALUE; PHY(0x004) = val;
	PHY(0x034) = 0x3; /* 16-bit */
	val = PHY(0x000); val &= ~0xff; val |= 0x0d; PHY(0x000) = val;
	val = PHY(0x01c); val &= ~0xf; val |= DDRP_CWL_VALUE; PHY(0x01c) = val;
	val = PHY(0x014); val &= ~0xf; val |= DDRP_CL_VALUE; PHY(0x014) = val;
	PHY(0x018) = 0;

	t41_spl_puts("PLK post="); spl_put_hex(PHY(0x180)); t41_spl_puts("\n");

	/* DFI init */
	ddr_writel((1<<3), APB_DWCFG);
	ddr_writel(0, APB_DWCFG);
	while (!(ddr_readl(APB_DWSTATUS) & 1));
	t41_spl_puts("DWST="); spl_put_hex(ddr_readl(APB_DWSTATUS)); t41_spl_puts("\n");

	ddr_writel(0, 0x010);
	{ volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(DDRC_CFG_VALUE, 0x008);
	{ volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(2, 0x010); /* CKE */
	{ volatile int d; for(d=0;d<50000;d++); }

	/* DDR3 LMR */
#define _LMR(n) (DDRC_DLMR_VALUE | 1 | (2<<6) | \
	((DDR_MR##n##_VALUE & 0xffff) << 12) | \
	(((DDR_MR##n##_VALUE >> 16) & 0x7) << 9))
	ddr_writel(0, 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(_LMR(2), 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(0, 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(_LMR(3), 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(0, 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel((_LMR(1) & ~0x266000) | (0x02 << 12), 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(0, 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(_LMR(0), 0x018); { volatile int d; for(d=0;d<50000;d++); }
	ddr_writel(DDRC_DLMR_VALUE | 1 | (3<<6), 0x018); { volatile int d; for(d=0;d<50000;d++); } /* ZQCL */
#undef _LMR

	/* Controller TIMINGs + MMAPs */
	ddr_writel(DDRC_TIMING1_VALUE, 0x040);
	ddr_writel(DDRC_TIMING2_VALUE, 0x048);
	ddr_writel(DDRC_TIMING3_VALUE, 0x050);
	ddr_writel(DDRC_TIMING4_VALUE, 0x058);
	ddr_writel(DDRC_TIMING5_VALUE, 0x060);
	ddr_writel(DDRC_MMAP0_VALUE, 0x078);
	ddr_writel(DDRC_MMAP1_VALUE, 0x080);
	ddr_writel(DDRC_CTRL_VALUE & ~(7 << 12), 0x010);

	/* Skip calibration for now */

	/* Post init */
	ddr_writel(DDRC_REFCNT_VALUE, 0x038);
	ddr_writel(DDRC_CTRL_VALUE, 0x010);
	ddr_writel(DDRC_CGUC0_VALUE, APB_CGUC0);
	ddr_writel(DDRC_CGUC1_VALUE, APB_CGUC1);
	ddr_writel(DDRC_AUTOSR_CNT_VALUE, 0x030);
	ddr_writel(0, 0x028);
	ddr_writel(DDRC_HREGPRO_VALUE, 0x0d8);
	ddr_writel(DDRC_PREGPRO_VALUE, APB_PREGPRO);

	t41_spl_puts("CTRL="); spl_put_hex(ddr_readl(0x010)); t41_spl_puts("\n");

	/* Test read */
	{
		volatile u32 *a = (volatile u32 *)0xa0000000;
		*a = 0xdeadbeef;
		t41_spl_puts("RD="); spl_put_hex(*a); t41_spl_puts("\n");
	}
}
