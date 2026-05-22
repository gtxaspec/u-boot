# SPDX-License-Identifier: GPL-2.0+

extra-$(CONFIG_XPL_BUILD)	:= start.o

obj-y				+= cpu.o

# DFU loaders (U-Boot proper only): runtime-sized dfu_alt_info hook.
ifndef CONFIG_XPL_BUILD
obj-$(CONFIG_SET_DFU_ALT_INFO)	+= dfu.o
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
