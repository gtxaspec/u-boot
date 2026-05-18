// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic XBurst1 T-series common CPU routines
 *
 * Copyright (c) 2019 Ingenic Semiconductor Co.,Ltd
 */

#include <init.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int print_cpuinfo(void)
{
#if defined(CONFIG_SOC_T10)
	const char *soc = "T10";
#elif defined(CONFIG_SOC_T20)
	const char *soc = "T20";
#elif defined(CONFIG_SOC_T21)
	const char *soc = "T21";
#elif defined(CONFIG_SOC_T23)
	const char *soc = "T23";
#elif defined(CONFIG_SOC_T30)
	const char *soc = "T30";
#elif defined(CONFIG_SOC_T31)
	const char *soc = "T31";
#elif defined(CONFIG_SOC_T33)
	const char *soc = "T33";
#else
	const char *soc = "T-series";
#endif

	printf("CPU:   Ingenic %s (XBurst1)\n", soc);
	return 0;
}
