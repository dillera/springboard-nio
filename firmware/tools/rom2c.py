#!/usr/bin/env python3
"""Turn a Springboard ROM image into a C array for the firmware."""
import sys
import zlib

data = open(sys.argv[1], "rb").read()
# The mailbox occupies CS0 offsets 0x8000 and up (include/sbnio.h).
if not 512 <= len(data) <= 0x8000 or len(data) % 2:
    sys.exit(f"{sys.argv[1]}: {len(data)} bytes; a ROM must be 512-32768 bytes and even")
with open(sys.argv[2], "w") as out:
    out.write("#include <stdint.h>\n")
    out.write(f"const uint32_t sbnio_rom_size = {len(data)};\n")
    out.write(f"const uint32_t sbnio_rom_crc = 0x{zlib.crc32(data):08x};\n")
    out.write("const uint8_t sbnio_rom[] = {\n")
    for i in range(0, len(data), 16):
        out.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
    out.write("};\n")
