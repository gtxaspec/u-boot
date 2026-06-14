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
		 * T20/T30 NOR boot: the gen-1 mask ROM loads only the first
		 * 0x6800 bytes of the SPL, and its L2 cannot be software-
		 * initialised (T30) / only the 32 KB L1 is reliably usable, so
		 * the cache-as-RAM budget is tight. Front-load the whole
		 * pre-DRAM bring-up path - start.S, the SoC SPL (console/SFC/
		 * self_complete), the DDR driver, the fdt code fdtdec_setup()/
		 * pll_setpoints() need, and the mem/str helpers that path calls
		 * (memset for the BSS clear before self_complete(), memcpy/strlen
		 * for fdtdec) - into the ROM-loaded, L1-resident region. The
		 * heavy DM/SFC/LZMA code follows and runs DRAM-resident after
		 * to_dram()/flush_cache(). start.o stays first so its boot header
		 * is at image offset 0. (T21/T23 don't need this - their natural
		 * link order already lands the bring-up path in the head.)
		 */
#if defined(CONFIG_SOC_T20) || defined(CONFIG_SOC_T30)
		*mach-xburst/start.o(.text*)
#ifdef CONFIG_SOC_T20
		/*
		 * T20's Synopsys-DWC DDR controller HANGS on any access to the
		 * un-loaded image tail before DDR is up (T30's Innophy core only
		 * returns garbage, so T30 needs only its .text front-loaded). So
		 * for T20 the pre-DDR path's .rodata - the DDR variant tables and
		 * per-SKU compatible strings (ddr_t20*), the pll setpoints, the
		 * console marker strings, and the fdt/libfdt read-only data
		 * get_variant() walks - must ALSO live in the ROM-loaded head,
		 * not after all .text at 0x8000b780 (well past the 0x6800 load).
		 */
		*mach-xburst/t20*(.text* .rodata*)
		*ram/ingenic*(.text* .rodata*)
		*lib/fdtdec.o(.text* .rodata*)
		*lib/libfdt*(.text* .rodata*)
		*lib/string.o(.text*)
#endif
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

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss*)
		*(.sbss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > .bss_mem

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
