/* SPDX-License-Identifier: GPL-2.0+ */

MEMORY { .spl_mem : ORIGIN = IMAGE_TEXT_BASE, \
		LENGTH = IMAGE_MAX_SIZE }
MEMORY { .bss_mem : ORIGIN = CONFIG_SPL_BSS_START_ADDR, \
		LENGTH = CONFIG_SPL_BSS_MAX_SIZE }

OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS
{
	. = 0x00000000;

	. = ALIGN(4);
	.text : {
		__text_start = .;
		/*
		 * start.o must link first so its boot header is at image offset 0
		 * (the mask ROM / TPL jump to base+0x800). T20 is a TPL chain now -
		 * the DRAM-resident SPL has no ROM load cap and the TPL is small
		 * enough to load whole - so it needs nothing more than that.
		 *
		 * T30 is still a single-stage cache-as-RAM SPL: the gen-1 mask ROM
		 * loads only the first 0x6800 bytes and its L2 cannot be software-
		 * initialised, so its whole pre-DRAM bring-up path (start.S, the SoC
		 * SPL, the DDR driver, the fdt code + mem/str helpers) must be
		 * front-loaded into the ROM-loaded, L1-resident head; the heavy
		 * DM/SFC/LZMA tail follows and runs DRAM-resident after to_dram().
		 * (T21/T23 - and now T20 - don't need this: start.o links first
		 * naturally and the rest runs from DRAM.)
		 */
#if defined(CONFIG_SOC_T20) || defined(CONFIG_SOC_T30)
		*mach-xburst/start.o(.text*)
#ifdef CONFIG_SOC_T30
		*mach-xburst/t30*(.text*)
		*ram/ingenic*(.text*)
		*lib/fdtdec.o(.text*)
		*lib/libfdt*(.text*)
		*lib/string.o(.text*)
#endif
#endif
		*(.text*)
		__text_end = .;
	} > .spl_mem

	. = ALIGN(4);
	.rodata : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} > .spl_mem

	. = ALIGN(4);
	.data : {
		*(SORT_BY_ALIGNMENT(.data*))
		*(SORT_BY_ALIGNMENT(.sdata*))
		. = ALIGN(8);
	} > .spl_mem

#if defined(CONFIG_SPL_DM) || defined(CONFIG_SPL_LOADER_SUPPORT)
	. = ALIGN(4);
	__u_boot_list : {
		KEEP(*(SORT(__u_boot_list*)));
		. = ALIGN(8);
	} > .spl_mem
#endif

	__image_copy_end = .;
	__image_copy_len = __image_copy_end - __text_start;

	_image_binary_end = .;

#if CONFIG_IS_ENABLED(SEPARATE_BSS)
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss*)
		*(.sbss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > .bss_mem
#else
	/*
	 * No separate BSS region: place BSS in the image address space (it is
	 * still NOLOAD - no file bytes). Used by the cache-as-RAM TPL, whose BSS
	 * must live in the mask-ROM cache window, not the far DRAM .bss_mem. Safe
	 * because that phase has no appended devicetree (OF_PLATDATA) to collide
	 * with at _image_binary_end.
	 */
	.bss (NOLOAD) : {
		. = ALIGN(8);
		__bss_start = .;
		*(.bss*)
		*(.sbss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > .spl_mem
#endif

	/* These mark the ABI of U-Boot for debuggers. */
	.mdebug.abi32 : {
		KEEP(*(.mdebug.abi32))
	}
	.mdebug.abi64 : {
		KEEP(*(.mdebug.abi64))
	}

	/* This is the MIPS specific mdebug section. */
	.mdebug : { *(.mdebug) }

	/* Stabs debugging sections.  */
	.stab 0 : { *(.stab) }
	.stabstr 0 : { *(.stabstr) }
	.stab.excl 0 : { *(.stab.excl) }
	.stab.exclstr 0 : { *(.stab.exclstr) }
	.stab.index 0 : { *(.stab.index) }
	.stab.indexstr 0 : { *(.stab.indexstr) }
	.comment 0 : { *(.comment) }

	/*
	 * DWARF debug sections.
	 * Symbols in the DWARF debugging sections are relative to
	 * the beginning of the section so we begin them at 0.
	 */
	/* DWARF 1 */
	.debug 0 : { *(.debug) }
	.line 0 : { *(.line) }
	/* GNU DWARF 1 extensions */
	.debug_srcinfo 0 : { *(.debug_srcinfo) }
	.debug_sfnames 0 : { *(.debug_sfnames) }
	/* DWARF 1.1 and DWARF 2 */
	.debug_aranges 0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	/* DWARF 2 */
	.debug_info 0 : {
		*(.debug_info
		.gnu.linkonce.wi.*)
	}
	.debug_abbrev 0 : { *(.debug_abbrev) }
	.debug_line 0 : { *(.debug_line) }
	.debug_frame 0 : { *(.debug_frame) }
	.debug_str 0 : { *(.debug_str) }
	.debug_loc 0 : { *(.debug_loc) }
	.debug_macinfo 0 : { *(.debug_macinfo) }
	.debug_pubtypes 0 : { *(.debug_pubtypes) }
	/* DWARF 3 */
	.debug_ranges 0 : { *(.debug_ranges) }
	/* SGI/MIPS DWARF 2 extensions */
	.debug_weaknames 0 : { *(.debug_weaknames) }
	.debug_funcnames 0 : { *(.debug_funcnames) }
	.debug_typenames 0 : { *(.debug_typenames) }
	.debug_varnames 0 : { *(.debug_varnames) }
	/* GNU DWARF 2 extensions */
	.debug_gnu_pubnames 0 : { *(.debug_gnu_pubnames) }
	.debug_gnu_pubtypes 0 : { *(.debug_gnu_pubtypes) }
	/* DWARF 4 */
	.debug_types 0 : { *(.debug_types) }
	/* DWARF 5 */
	.debug_macro 0 : { *(.debug_macro) }
	.debug_addr 0 : { *(.debug_addr) }

	/DISCARD/ : {
		/* ABI crap starts here */
		*(.MIPS.abiflags)
		*(.MIPS.options)
		*(.options)
		*(.pdr)
		*(.reginfo)
		*(.eh_frame)
	}
}

ASSERT(_image_binary_end % 8 == 0, \
       "_image_binary_end must be 8-byte aligned for device tree");
