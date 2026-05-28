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
        # INGE block (offset 0x100, magic 'INGE', SPL_LENGTH, and
        # optional CGU descriptor table) is an XBurst2 mask ROM
        # feature: the bootrom programs the descriptors before
        # jumping to SPL so the SPL inherits sane PLLs / CPCCR / SFC
        # clocks. XBurst1 mask ROMs (T10/T20/T21/T23/T30/T31) predate
        # this and treat the INGE magic at 0x100 as an invalid header
        # field - they silently reject the image (no UART, no
        # fallback). XBurst2 NAND boot paths (T40XP) also reject INGE
        # because the NAND header layout differs from the NOR one.
        #
        # Net rule: emit INGE only when the board has actually
        # declared `ingenic,inge-descriptors` to fill it. Boards that
        # just need a flat SPL header (CRC + SPL_LENGTH, no INGE)
        # leave the property unset and get a vendor-shaped no-INGE
        # image automatically. There is no longer a separate
        # `ingenic,no-inge` boolean (deleted - presence-of-descriptors
        # is the signal).
        self.emit_inge = bool(self.inge_descriptors)
        # Backwards-compat: the previous etype version required
        # `ingenic,no-inge` on NAND/T31 dtsis. Honour the old
        # property for downstream branches that still set it (it just
        # forces no-INGE; same behaviour as omitting descriptors).
        if fdt_util.GetBool(self._node, 'ingenic,no-inge'):
            self.emit_inge = False
        # `no_inge` retained as the NAND-style alignment switch -
        # boards with SPI-NAND need the 64-byte SPL pad whether or
        # not they emit INGE. Default to "NAND alignment" only when
        # the board explicitly opts out of INGE via the legacy
        # property; for clean XBurst1 NOR boards (no descriptors,
        # no explicit no-inge) we keep the NOR alignment path.
        self.no_inge = fdt_util.GetBool(self._node, 'ingenic,no-inge')
        # Minimum SPL_LENGTH to stamp, in bytes from CONFIG_SPL_TEXT_BASE.
        # T-series bootroms lock cache lines as SRAM only for the address
        # range MIN(SPL_LENGTH, _DAT_80000000) starting at the load
        # address; bytes accessed past the loaded region (typically the
        # SPL's BSS region beyond the file's .data end) miss cache and
        # fetch from uninitialised DRAM. Boards whose BSS lands past the
        # actual file size need to bump SPL_LENGTH so the bootrom locks
        # SRAM lines through the BSS area. The bootrom happily loads
        # past file-EOF (into the U-Boot proper region on the SFC NOR
        # layout) - those junk bytes land in BSS area, which gets
        # zeroed during SPL startup anyway, so the overload is benign.
        #
        # Value is the byte offset from CONFIG_SPL_TEXT_BASE through the
        # last address the SPL needs cache-locked (= __bss_end -
        # CONFIG_SPL_TEXT_BASE, rounded up).
        self.min_spl_length = fdt_util.GetInt(
            self._node, 'ingenic,min-spl-length', 0)

    def GetDefaultFilename(self):
        return 'spl/u-boot-spl.bin'

    def ReadBlobContents(self):
        data = bytearray(tools.read_file(self._pathname))
        orig_size = len(data)

        # NAND boot path: pad file size up to 64-byte boundary BEFORE
        # CRC, because the bootrom CRCs over the full SPL_LENGTH range
        # of SRAM (incl. padding bytes loaded from NAND). The padding
        # MUST be in the file so binman's CRC and bootrom's CRC see
        # the same bytes.
        #
        # If `ingenic,min-spl-length` is set (BSS coverage; see the
        # NOR-path commentary below), pad UP to that length too. The
        # bootrom NAND loader has the same cache-as-SRAM semantics as
        # NOR: anything past SPL_LENGTH is not cache-locked, so a BSS
        # touch into uninit DRAM crashes silently. Padding with 0xff
        # to bss_end is the per-vendor convention.
        if self.no_inge:
            stored_size = (max(orig_size, self.min_spl_length) + 0x3f) & ~0x3f
            if stored_size > orig_size:
                data.extend(b'\xff' * (stored_size - orig_size))

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
        # FUN_bfc02070): bootrom reads SPL_LENGTH (header byte 12, u32
        # LE) into local_44, then loads MIN(local_44, _DAT_80000000)
        # bytes from NOR offset 0 into SRAM at _DAT_80000034 (=
        # 0x80001000 by default). _DAT_80000000 starts at the bootrom's
        # internal soft cap (~0x19000 = 100 KiB, see lines 1155-1158
        # of the T40 bootrom decompile in thingino-cloner). The
        # earlier "0x4400 hard cap" claim came from a contaminated
        # 2026-05-26 bisect and was wrong: vendor T40N SFCNOR SPL has
        # SPL_LENGTH = 0x4640 = 17984 bytes (= actual file size) and
        # cold-boots cleanly, so 0x4400 is NOT the upper bound.
        #
        # The vendor T-series tool that writes this field is
        # `tools/ingenic-tools/spi_checksum.c`; it simply writes the
        # exact file byte count (see its main(): `count` = total bytes
        # read, then `write(fd, &count, 4)` at SPL_LENGTH_POSITION).
        # The corresponding CONFIG_SPL_MAX_SIZE in vendor T40 is
        # 26624 (= 0x6800), which sets the SPL+padding cap and the
        # NOR offset of U-Boot proper but is unrelated to the
        # bootrom-side SPL_LENGTH check.
        #
        # We mirror vendor behaviour: store actual file size, BUT
        # the bootrom rejects (silent fail, no UART, no fallback) any
        # SPL_LENGTH > (INGE_length - 0x180). Cold-boot bisect on real
        # T40NN silicon 2026-05-26:
        #   stored = 0x4640 (vendor T40N, INGE_len=0x4800) -> works
        #   stored = 0x4400..0x4800 (our 20120-B SPL)      -> SPL loads
        #                                                    but truncates,
        #                                                    crashes at
        #                                                    udelay()
        #   stored = 0x4C00 (our 20120-B SPL)              -> works
        #   stored = 0x4E00 (our 20120-B SPL)              -> works
        #   stored = 0x4E80 (our 20120-B SPL)              -> works
        #   stored = 0x4E98 (= our file size, 20120)       -> SILENT
        # Empirical bootrom cap = INGE_length - 0x180:
        #   INGE_length 0x5000 -> cap 0x4E80 (our 20120 just fits)
        #   INGE_length 0x4800 -> cap 0x4680 (vendor 17984 fits w/ 64-B margin)
        # The 0x180 (= 384-byte) margin is undocumented in the T40
        # bootrom decompile (FUN_bfc02070 + FUN_bfc039a0); likely a
        # bootrom-side staging buffer reserved inside the SRAM region
        # described by INGE_length. Store min(file_size, INGE_length -
        # 0x180) so we never trip the cap.
        if self.no_inge:
            # NAND boot path: stored_size already computed above
            # (padding applied before CRC). SPL_LENGTH = padded size.
            #
            # Pad to 64-byte boundary to match vendor build behaviour.
            # The vendor tool `tools/ingenic-tools/spl_pad_to_block.c`
            # pads SPL with 0xff to a multiple of 64 bytes
            # unconditionally before stamping CRC + SPL_LENGTH;
            # vendor's comment attributes the rule to "scboot hash
            # module 64-byte minimum granularity". The check itself
            # lives in FUN_bfc04ea0 (line 2983 of the t40 decompile),
            # gated by `(DAT_800000b1 & 1) != 0` (secure-boot fuse).
            # Whether a specific chip has the fuse set or not, the
            # vendor build pads anyway, so we match that convention.
            #
            # The bootrom code on the non-secure path requires only
            # 4-byte word granularity (FUN_bfc02070: `(size + 3) & ~3`
            # and FUN_bfc01ffc reads in word units); a size cap at
            # _DAT_80000000 default 0x19000 (100 KiB) caps anything
            # larger. Padding to 64 bytes is harmless under that
            # cap and matches vendor.
            pass
        else:
            # If the board asked for a larger SPL_LENGTH than the file
            # (to cover BSS past file-EOF, see above), grow the cap
            # computation to accommodate. Bootrom rejects stored >
            # INGE_length - 0x180, so INGE_length must be at least
            # min_spl_length + 0x180 rounded up to 512.
            effective_size = max(size, self.min_spl_length)
            inge_len_for_cap = (effective_size + 0x180 + 511) & ~511
            cap = inge_len_for_cap - 0x180
            stored_size = min(effective_size, cap)
        data[SPL_LENGTH_POSITION:SPL_LENGTH_POSITION + 4] = \
            stored_size.to_bytes(4, 'little')

        # INGE params header (emitted only when the board supplies
        # `ingenic,inge-descriptors`). Per the T40/T41/A1 bootrom
        # decompilation the mask ROM reads this block at a per-boot-
        # source offset, and if the 'INGE' magic matches it walks the
        # descriptor table to program clocks before entering the SPL:
        #
        #   - SFC NOR boot path (FUN_bfc02070) -> block at 0x100
        #   - SD/MMC boot path  (FUN_bfc015e0) -> block at 0x40
        #
        # (An earlier version of this comment had the two swapped and
        # mirrored the block to 0x40; NOR is bfc02070 reading 0x100,
        # bfc015e0 is the SD/MMC path. We emit only at the NOR offset
        # 0x100 = INGE_OFFSET, matching the vendor T40N SFCNOR layout.)
        #
        # Bytes [0x00..0x03] hold the 'INGE' magic, [0x04..0x07] the SPL
        # length, [0x08..0x1f] pll_freq + cpccr + nand_timing (left zero),
        # and [0x20..] the CPM descriptor table (5 u32 cells per entry,
        # terminated by (0xffffffff, 0xffffffff, 0, 0, 0)).
        #
        # NOTE: no mainline SoC currently sets descriptors. Our DM-in-SPL
        # SPLs program their own clocks in board_init_f before any clock-
        # dependent work, so this bootrom pre-programming is redundant
        # (HW-verified: T40NN + T41NQ cold-boot identically with and
        # without INGE, 2026-05-28). Kept as opt-in infrastructure for a
        # vendor-style SPL that uses UART/timing before its own clk init.
        # INGE_length tracks effective size (file + min_spl_length pad)
        # so the bootrom-side cap matches the stamped SPL_LENGTH.
        # Rounded up to 512 with 0x180 margin per the cap formula.
        if self.emit_inge:
            inge_length = (max(size, self.min_spl_length) + 0x180 + 511) & ~511
        else:
            inge_length = (size + 511) & ~511
        # Vendor T40N SFCNOR SPL binary places INGE only at 0x100.
        # 2026-05-26 cold-boot A/B test: vendor (INGE@0x100 only) cold-
        # boots T40NN cleanly to U-Boot. Our build with INGE mirrored
        # at 0x40 was silent on cold boot but worked on warm reset.
        # Drop the 0x40 mirror to mirror vendor layout exactly.
        if self.emit_inge:
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
