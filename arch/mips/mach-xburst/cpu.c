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
#if defined(CONFIG_SOC_A1)
	const char *soc = "A1";
	const char *core = "XBurst2";
#elif defined(CONFIG_SOC_T10)
	const char *soc = "T10";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T20)
	const char *soc = "T20";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T21)
	const char *soc = "T21";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T23)
	const char *soc = "T23";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T30)
	const char *soc = "T30";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T31)
	const char *soc = "T31";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T32)
	const char *soc = "T32";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T33)
	const char *soc = "T33";
	const char *core = "XBurst1";
#elif defined(CONFIG_SOC_T40)
	const char *soc = "T40";
	const char *core = "XBurst2";
#elif defined(CONFIG_SOC_T41)
	const char *soc = "T41";
	const char *core = "XBurst2";
#else
	const char *soc = "unknown";
	const char *core = "XBurst";
#endif

	printf("CPU:   Ingenic %s (%s)\n", soc, core);
	return 0;
}
