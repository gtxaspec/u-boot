# SPDX-License-Identifier: GPL-2.0+

obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_phy_t41.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_phy_t40.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_types.o
# A1-family PHY path: only linked into A1 builds (keeps the T40/T41 SPLs
# from carrying A1's DDR tables, which overflowed the T41 NOR SPL window).
obj-$(CONFIG_SOC_A1) += ddr_innophy_phy_a1.o

# T31 (XBurst1) DDR - sibling driver, distinct IP. T23, T21 and T30 reuse the
# same driver (identical legacy DDRC + Innophy DDR2 IP); the per-SKU values come
# from each board's "ingenic,sdram-params" devicetree array, not a C table.
obj-$(CONFIG_RAM_INGENIC_DDR_T31) += ddr_t31.o

# T32 (PRJ007) uMCTL2 + Innophy DDR - sibling driver, distinct IP. Per-SKU
# params live in the devicetree (ingenic,sdram-params), not a C table.
obj-$(CONFIG_RAM_INGENIC_DDR_T32) += ddr_t32.o

# T20 (XBurst1) DDR2 + Synopsys DWC PHY - the only non-Innophy XBurst1
# camera SoC, so its own driver/struct (hardware ZQ cal + DQS training).
# Per-SKU params live in the devicetree (ingenic,sdram-params), not a C table.
obj-$(CONFIG_RAM_INGENIC_DDR_T20) += ddr_t20.o
