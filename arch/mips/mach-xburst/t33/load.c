// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic T33 SPL U-Boot-proper loader, shared by the NOR (sfc.c) and SD
 * (msc.c) boot media: the image is a 64-byte legacy mkimage header plus an
 * LZMA payload at T33_UBOOT_OFFSET, decompressed to ih_load. The medium
 * supplies only the read callback.
 *
 * Copyright (c) 2024 Ingenic Semiconductor Co.,Ltd
 */

#include <asm/global_data.h>
#include <lzma/LzmaTools.h>
#include <malloc.h>
#include <linux/string.h>
#include <mach/t33.h>

DECLARE_GLOBAL_DATA_PTR;

void t33_spl_puts(const char *s);

/*
 * Flash / DRAM layout (matches the binman NOR image and the T31
 * profile): SPL at flash 0, U-Boot proper (LZMA + 64-byte mkimage
 * header) at T33_UBOOT_OFFSET, decompressed to 0x80100000. 64 MB
 * DRAM at 0x80000000, so scratch/heap below are all disjoint.
 */
#define T33_UBOOT_OFFSET	0x10000
#define T33_IH_MAGIC		0x27051956
#define T33_IH_HDR_LEN		64
#define T33_UBOOT_SCRATCH	0x81000000
#define T33_SPL_HEAP_BASE	0x80a00000
#define T33_SPL_HEAP_SIZE	0x00200000
#define T33_UBOOT_MAX		0x00800000
#define T33_DRAM_BASE		0x80000000	/* KSEG0 cached DRAM */
#define T33_DRAM_SIZE		0x04000000	/* 64 MB (M14D5121632A) */

static u32 hdr_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

/*
 * Read the LZMA-compressed, mkimage-wrapped U-Boot proper through @rd,
 * decompress to its load address and jump (no return). DRAM is already
 * initialised.
 */
void t33_spl_load_image(int (*rd)(u32 off, u32 len, u8 *dst))
{
	u8 *scratch = (u8 *)T33_UBOOT_SCRATCH;
	u32 ih_magic, ih_size, ih_load;
	SizeT out_len;
	int ret;

	/* Legacy mkimage header (64 bytes, big-endian fields). */
	if (rd(T33_UBOOT_OFFSET, T33_IH_HDR_LEN, scratch) < 0) {
		t33_spl_puts("SPL: header read failed\n");
		return;
	}
	ih_magic = hdr_be32(scratch + 0);
	ih_size  = hdr_be32(scratch + 12);
	ih_load  = hdr_be32(scratch + 16);
	if (ih_magic != T33_IH_MAGIC) {
		t33_spl_puts("SPL: bad U-Boot image magic\n");
		return;
	}

	/*
	 * The header fields come off NOR - bound them before they drive a
	 * read length / decompress destination / jump. ih_size caps the
	 * scratch write; ih_load must land in DRAM.
	 */
	if (ih_size == 0 || ih_size > T33_UBOOT_MAX) {
		t33_spl_puts("SPL: U-Boot image size out of range\n");
		return;
	}
	if (ih_load < T33_DRAM_BASE ||
	    ih_load >= T33_DRAM_BASE + T33_DRAM_SIZE) {
		t33_spl_puts("SPL: U-Boot load addr out of range\n");
		return;
	}

	/* Header + LZMA payload into DRAM scratch. */
	if (rd(T33_UBOOT_OFFSET, T33_IH_HDR_LEN + ih_size,
			 scratch) < 0) {
		t33_spl_puts("SPL: image read failed\n");
		return;
	}

	/*
	 * Lean SPL: no heap until DRAM is up and we never call
	 * spl_init(), so arm the full malloc arena ourselves or the
	 * LZMA dictionary allocation fails.
	 */
	mem_malloc_init(T33_SPL_HEAP_BASE, T33_SPL_HEAP_SIZE);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;

	out_len = T33_UBOOT_MAX;
	ret = lzmaBuffToBuffDecompress((unsigned char *)(uintptr_t)ih_load,
				       &out_len, scratch + T33_IH_HDR_LEN,
				       (SizeT)ih_size);
	if (ret) {
		char m[2] = { (char)('0' + (ret & 7)), 0 };

		t33_spl_puts("SPL: U-Boot LZMA decompress failed, SZ_ERROR=");
		t33_spl_puts(m);
		t33_spl_puts("\n");
		return;
	}

	((void (*)(void))(uintptr_t)ih_load)();
}
