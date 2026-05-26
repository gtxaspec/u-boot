# SPDX-License-Identifier: GPL-2.0+
# Copyright (c) 2026 Thingino
#
# Entry-type module for the Ingenic T-series SFC-NOR SPL boot header
#

from binman.etype.blob import Entry_blob
from dtoc import fdt_util
from u_boot_pylib import tools

# T31 mask-ROM SFC-NOR header layout, from the vendor
# tools/ingenic-tools/spi_checksum.c (CONFIG_T31 / CONFIG_SPL_SFC path).
# This mirrors tools/mkt31spl.c, which is the hardware-validated
# reference. Do not change the offsets or the algorithm.
SKIP_SIZE = 2048		# CRC body starts here (header is 2048 bytes)
CRC_POSITION = 9		# CRC7 byte offset in the header
SPL_LENGTH_POSITION = 12	# little-endian u32 total image length

# INGE params header offset / structure, from vendor
# tools/ingenic-tools/spl_params_fixer.c. The T40 mask ROM (and the
# T31 mask ROM with SPL_PARAMS_FIXER) reads this header to learn the
# SPL size so it can size the pre-load zero-fill correctly. Without
# it the T40 bootrom uses a 0x19000-byte default that overflows the
# 32 KB on-chip SRAM into uninitialised DDR and wedges before the
# SPL is entered.
INGE_OFFSET = 0x100
INGE_MAGIC = 0x45474e49		# 'INGE' little-endian
INGE_DESC_OFFSET = 0x120	# bootrom CPM descriptor table starts here
INGE_DESC_SIZE = 20		# 5 x u32 per descriptor entry
INGE_BLOCK_MAX = 0x100		# total INGE block (header + descriptors) ceiling

# CRC7 syndrome table, identical to tools/mkt31spl.c.
crc7_syndrome_table = [
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
    0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
    0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
    0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
    0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
    0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
    0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
    0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
    0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
    0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
    0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
    0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
    0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
    0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
    0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
    0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
    0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
    0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
    0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
    0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
    0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
    0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
    0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
    0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
    0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
    0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
    0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
    0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
    0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
    0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
    0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79,
]


class Entry_ingenic_t31_spl(Entry_blob):
    """Ingenic T-series SPL with a finalized SFC-NOR boot header

    Properties / Entry arguments:
        - filename: Filename of SPL binary to read (default
          'spl/u-boot-spl.bin')
        - ingenic,inge-descriptors: (optional) flat list of u32 cells,
          5 per descriptor: (set_addr, poll_addr, value, poll_h_mask,
          poll_l_mask). The bootrom applies these between loading the
          SPL into SRAM and jumping to it - typically to program
          APLL/MPLL/CPCCR/SFC clocks before the SPL touches any
          clock-dependent peripheral. Used by T40 (XBurst2) where the
          UART clock at fresh boot is not 24 MHz EXTAL; T31/A1 boards
          can omit the property and the bootrom skips the table.

    The Ingenic T-series mask ROM boots from SPI-NOR flash by reading a
    fixed 2048-byte header at the start of the SPL. The boot magic words
    are emitted by start.S; this entry finalizes the remaining header
    fields so the mask ROM accepts and copies the image:

        - byte 9:         CRC7 of the SPL body (offset 2048 to end of file)
        - bytes 12..15:   total image length, little-endian u32
        - bytes 0x100+:   INGE params header (magic, size, optional CPM
                          descriptor table for bootrom clock programming)

    This is a faithful reimplementation of the vendor
    tools/ingenic-tools/spi_checksum.c + spl_params_fixer.c, and matches
    tools/mkt31spl.c (the standalone, hardware-validated reference) byte
    for byte. The headered SPL is typically placed at offset 0 of the
    SFC-NOR flash, with U-Boot proper following at a later offset.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node)

    def ReadNode(self):
        super().ReadNode()
        # fdt_util has no generic u32-list reader; GetPhandleList decodes
        # any u32 cell array, which is exactly what we need here.
        self.inge_descriptors = (
            fdt_util.GetPhandleList(self._node, 'ingenic,inge-descriptors')
            or [])
        if len(self.inge_descriptors) % 5 != 0:
            self.Raise("'ingenic,inge-descriptors' must be a multiple of "
                       "5 u32 values (set, poll, val, hmask, lmask)")
        # Header (32 bytes) + descriptors + terminator (20 bytes) <= 256
        descriptors_bytes = (len(self.inge_descriptors) // 5 + 1) * INGE_DESC_SIZE
        if 0x20 + descriptors_bytes > INGE_BLOCK_MAX:
            self.Raise("'ingenic,inge-descriptors' table too large for "
                       "256-byte INGE block")

    def GetDefaultFilename(self):
        return 'spl/u-boot-spl.bin'

    def ReadBlobContents(self):
        data = bytearray(tools.read_file(self._pathname))

        # CRC7 over the SPL body: bytes from SKIP_SIZE to EOF. crc
        # starts at 0; crc = table[(crc << 1) ^ byte] per the vendor
        # algorithm (see tools/mkt31spl.c).
        crc = 0
        for byte in data[SKIP_SIZE:]:
            crc = crc7_syndrome_table[((crc << 1) ^ byte) & 0xff]

        size = len(data)
        data[CRC_POSITION] = crc & 0xff
        #
        # SPL_LENGTH field bootrom semantics (T40 SFC NOR boot path,
        # FUN_bfc02070): bootrom loads uVar5 = MIN(SPL_LENGTH, INGE_length)
        # bytes from NOR into SRAM at 0x80001000 before jumping to
        # 0x80001800.
        #
        # 2026-05-26 cold-boot bisect on real T40NN silicon: small SPLs
        # (e.g. our 2872-byte probe SPL with stored SPL_LENGTH = 2872)
        # are silently rejected by the bootrom on cold POR even with
        # correct magic + valid CRC. Bumping SPL_LENGTH to vendor's
        # 13184 (with our actual binary still 2872 bytes) makes the
        # bootrom load the SPL and reach 0x80001800.
        #
        # The exact threshold is around the bootrom's default SPL load
        # size (0x4400 = 17408, set at boot via `_DAT_80000010 = 0x4400`
        # in the bootrom reset stub). Empirically: SPL_LENGTH < ~13 KiB
        # is treated as invalid; >= 13 KiB cold-boots reliably.
        #
        # T40 bootrom SFC NOR boot path (FUN_bfc02070) reads the
        # SPL_LENGTH field at SPL header byte 12 and enforces an upper
        # bound at the bootrom's default SPL load size:
        #     _DAT_80000010 = 0x4400   (set in reset stub)
        # SPL_LENGTH > 0x4400 is silently rejected on cold POR - the
        # bootrom never reaches the SPL entry point at 0x80001800, no
        # UART output, no fallback to USB-boot. Bootrom CRC is NOT
        # checked (a CRC-corrupted vendor binary still cold-boots).
        #
        # Empirical A/B (2026-05-26 on real T40NN silicon):
        #   stored = 2872    -> silent (below lower threshold)
        #   stored = 13184   -> works  (vendor T40N SFCNOR value)
        #   stored = 17408   -> works  (= 0x4400 exactly)
        #   stored = 17496   -> silent (> 0x4400)
        #
        # The actual amount loaded from NOR into SRAM 0x80001000 is
        # MIN(SPL_LENGTH, INGE_length). Pin SPL_LENGTH at 0x4400 so
        # the bootrom loads up to 17408 bytes of SPL. If the .text +
        # .rodata + .data of the SPL fits in those 17408 bytes plus
        # the 2048-byte header (so total file <= 0x4400), the SPL runs
        # to completion. INGE_length is still rounded up from the
        # actual file size, so on USB / SD / NAND boot paths (which
        # use INGE_length, not SPL_LENGTH) the full SPL is still
        # loaded.
        stored_size = 0x4400
        data[SPL_LENGTH_POSITION:SPL_LENGTH_POSITION + 4] = \
            stored_size.to_bytes(4, 'little')

        # INGE params header. The T-series mask ROMs check for the
        # INGE block at two distinct offsets depending on the boot
        # source (per the T40 bootrom decompilation):
        #
        #   - SFC NOR boot path (FUN_bfc015e0)  -> param block at 0x40
        #   - USB/SD/NAND boot paths            -> param block at 0x100
        #
        # Bytes [0x00..0x03] of the block hold the 'INGE' magic, bytes
        # [0x04..0x07] hold the SPL length (rounded up to 512), bytes
        # [0x08..0x1f] are pll_freq + cpccr + nand_timing (left zero -
        # the vendor FPGA-mode default; clock setup is done by the
        # descriptor table below or by the SPL's pll_init), and bytes
        # [0x20..] are the CPM descriptor table (5 u32 cells per entry,
        # terminated by (0xffffffff, 0xffffffff, 0, 0, 0)). Emit the
        # exact same block at both offsets so the binary boots from
        # either path without needing per-board headers.
        #
        # Layout sanity at 0x40: header 32 bytes + 11 entries * 20 +
        # terminator 20 = 272 bytes -> ends at 0x150. Our DT supplies
        # 5 entries (so total = 152 = 0x98, ends at 0xd8 - well before
        # the 0x100 mirror) and the etype rejects anything that would
        # overflow the 0x100 block boundary.
        inge_length = (size + 511) & ~511
        # Vendor T40N SFCNOR SPL binary places INGE only at 0x100.
        # 2026-05-26 cold-boot A/B test: vendor (INGE@0x100 only) cold-
        # boots T40NN cleanly to U-Boot. Our build with INGE mirrored
        # at 0x40 was silent on cold boot but worked on warm reset.
        # Drop the 0x40 mirror to mirror vendor layout exactly.
        for inge_off in (INGE_OFFSET,):
            data[inge_off:inge_off + 4] = \
                INGE_MAGIC.to_bytes(4, 'little')
            data[inge_off + 4:inge_off + 8] = \
                inge_length.to_bytes(4, 'little')
            if self.inge_descriptors:
                off = inge_off + 0x20
                for val in self.inge_descriptors:
                    data[off:off + 4] = (val & 0xffffffff).to_bytes(4, 'little')
                    off += 4
                # Terminator (set=0xffffffff, poll=0xffffffff, rest 0).
                data[off:off + 4] = (0xffffffff).to_bytes(4, 'little')
                data[off + 4:off + 8] = (0xffffffff).to_bytes(4, 'little')
                data[off + 8:off + 20] = b'\x00' * 12

        self.SetContents(bytes(data))
        return True
