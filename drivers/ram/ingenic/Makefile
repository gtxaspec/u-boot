# SPDX-License-Identifier: GPL-2.0+

obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_phy.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_phy_t40.o
obj-$(CONFIG_RAM_INGENIC_DDR_INNOPHY) += ddr_innophy_types.o
# A1-family PHY path: only linked into A1 builds (keeps the T40/T41 SPLs
# from carrying A1's DDR tables, which overflowed the T41 NOR SPL window).
obj-$(CONFIG_SOC_A1) += ddr_innophy_phy_a1.o
