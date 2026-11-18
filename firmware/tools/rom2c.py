#!/usr/bin/env python3
"""Turn a Springboard ROM image into a C array for the firmware."""
import binascii
import sys
import zlib

data = open(sys.argv[1], "rb").read()
# CS0 is only the ROM (include/sbnio.h), served by A1-A17: up to 256 KiB.
if not 512 <= len(data) <= 0x40000 or len(data) % 2:
    sys.exit(f"{sys.argv[1]}: {len(data)} bytes; a ROM must be 512-262144 bytes and even")
# The Visor checks the card header CRC16 (bytes 0-111, then 114 up to
# checksumBytes) with a 16-bit byte count: it checksums checksumBytes mod
# 65536 bytes. A ROM over 64 KB built with a bare -chChecksum (the whole ROM)
# fails that check and the Visor shows none of its databases; build it with
# -chChecksum 0xFFFE.
if data[8:12] == b"\xfe\xed\xbe\xef":
    count = int.from_bytes(data[108:112], "big")
    if count > 0xFFFF:
        sys.exit(f"{sys.argv[1]}: card checksumBytes is {count:#x}; the Visor checks only "
                 f"{count & 0xFFFF:#x} bytes. Build the ROM with -chChecksum 0xFFFE.")
    crc = binascii.crc_hqx(data[114:count], binascii.crc_hqx(data[:112], 0))
    if crc != int.from_bytes(data[112:114], "big"):
        sys.exit(f"{sys.argv[1]}: card CRC16 {crc:04x} differs from the stored one")
with open(sys.argv[2], "w") as out:
    out.write("#include <stdint.h>\n")
    out.write(f"const uint32_t sbnio_rom_size = {len(data)};\n")
    out.write(f"const uint32_t sbnio_rom_crc = 0x{zlib.crc32(data):08x};\n")
    # The firmware runs from RAM (copy_to_ram); the ROM bytes are only read
    # once at boot into the CS0 table, so keep them in flash (.flashdata).
    out.write('__attribute__((section(".flashdata.sbnio_rom")))\n')
    out.write("const uint8_t sbnio_rom[] = {\n")
    for i in range(0, len(data), 16):
        out.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
    out.write("};\n")
