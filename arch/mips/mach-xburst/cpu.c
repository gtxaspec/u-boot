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
	printf("CPU:   Ingenic T31 (XBurst1)\n");
	return 0;
}
