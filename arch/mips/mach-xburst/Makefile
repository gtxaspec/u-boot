# SPDX-License-Identifier: GPL-2.0+

extra-$(CONFIG_XPL_BUILD)	:= start.o

obj-y				+= cpu.o

# U-Boot proper only (these hook misc_init_r / board_late_init, not the SPL).
ifndef CONFIG_XPL_BUILD
obj-$(CONFIG_SET_DFU_ALT_INFO)	+= dfu.o
obj-$(CONFIG_XBURST_MAC_FROM_EFUSE)	+= ethaddr.o
endif

obj-$(CONFIG_SOC_A1)		+= a1/
obj-$(CONFIG_SOC_T10)		+= t10/
obj-$(CONFIG_SOC_T20)		+= t20/
obj-$(CONFIG_SOC_T21)		+= t21/
obj-$(CONFIG_SOC_T23)		+= t23/
obj-$(CONFIG_SOC_T30)		+= t30/
obj-$(CONFIG_SOC_T31)		+= t31/
obj-$(CONFIG_SOC_T32)		+= t32/
obj-$(CONFIG_SOC_T33)		+= t33/
obj-$(CONFIG_SOC_T40)		+= t40/
obj-$(CONFIG_SOC_T41)		+= t41/
