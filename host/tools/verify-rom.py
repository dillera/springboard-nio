#!/usr/bin/env python3
"""Adapted from fujinet-palm tools/verify-springboard-rom.py for the SB NIO app.
Independently check the test card, CRC, token and every embedded PRC resource."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct

def verify(data, prc, creator=b'FNVB'):
    if len(creator) != 4:
        raise ValueError('unsupported expected creator')
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    def u16(offset):
        require(0 <= offset <= len(data)-2, 'u16 outside image')
        return struct.unpack_from('>H', data, offset)[0]
    def u32(offset):
        require(0 <= offset <= len(data)-4, 'u32 outside image')
        return struct.unpack_from('>I', data, offset)[0]
    def ptr(value):
        offset = value - 0x08000000
        require(0 <= offset < len(data), 'local ID outside image')
        return offset
    def chunk(offset):
        header = u32(offset-8)
        require(not header & 0x80000000, 'resource points at free chunk')
        size = (header & 0xffffff) - 8 - ((header >> 24) & 15)
        require(size >= 0 and offset+size <= len(data), 'invalid chunk size')
        return data[offset:offset+size]
    require(512 <= len(data) <= 65536 and len(data) % 2 == 0, 'invalid ROM size')
    require(u32(8) == 0xfeedbeef, 'card signature')
    require(u16(12) == 4, 'card header version')
    require(u32(256) == 0xfeedface, 'store signature')
    count = u32(108)
    require(count == len(data), 'checksum must cover entire ROM')
    crc = binascii.crc_hqx(data[:112], 0)
    crc = binascii.crc_hqx(data[114:count], crc)
    require(crc == u16(112), 'card CRC16 mismatch')
    token = ptr(u32(100))
    require(data[token:token+6] == b'HsAT\x00\x03', 'HsAT inline token')
    access_time = 330
    require(data[token+6:token+9] == str(access_time).encode(), f'HsAT must be {access_time} ns')
    require(data[token+10:token+14] == b'\xff'*4, 'token terminator')
    directory = ptr(u32(256+60))
    require(u32(directory) == 0 and u16(directory+4) == 1, 'one database expected')
    db = ptr(u32(directory+6))
    require(data[db:db+32] == prc[:32], 'database name differs')
    require(data[db+60:db+68] == b'appl'+creator, 'application identity')
    require(u16(db+32) & 3 == 3, 'read-only resource database required')
    if creator in (b'FNVR', b'FNsb'):
        pass
        require(not u16(db+32) & 0x0020, 'ROM probe must not request reset notification')
    require(u32(db+72) == 0, 'unexpected continuation list')
    require(len(prc) >= 78 and prc[60:68] == b'appl'+creator, 'unexpected PRC identity')
    resources = struct.unpack_from('>H', prc, 76)[0]
    require(u16(db+76) == resources, 'resource count differs')
    require(78 + resources*10 <= len(prc), 'truncated PRC directory')
    entries = [struct.unpack_from('>4sHI', prc, 78+i*10) for i in range(resources)]
    require(all(78+resources*10 <= e[2] <= len(prc) for e in entries), 'bad PRC offset')
    require(all(entries[i][2] <= entries[i+1][2] for i in range(resources-1)), 'unordered PRC')
    pattern_seen = False
    for i, (kind, ident, start) in enumerate(entries):
        end = entries[i+1][2] if i+1 < resources else len(prc)
        pos = db+78+i*10
        require(data[pos:pos+4] == kind and u16(pos+4) == ident, 'resource identity differs')
        contents = chunk(ptr(u32(pos+6)))
        require(contents == prc[start:end], 'embedded resource differs from PRC')
        if (kind, ident) == (b'tPAT', 1000):
            require(contents == bytes(range(256)), 'pattern differs')
            pattern_seen = True
    require(pattern_seen or creator == b'FNsb', 'pattern resource missing')
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'crc16': f'{crc:04x}', 'access_time_ns': access_time, 'resource_count': resources,
            'database': prc[:32].split(b'\0',1)[0].decode('ascii'), 'creator': creator.decode('ascii'), 'prc_sha256': hashlib.sha256(prc).hexdigest(),
            'hardware_verified': False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--prc', required=True, type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--creator', default='FNsb')
    args = parser.parse_args()
    try:
        result = verify(args.rom.read_bytes(), args.prc.read_bytes(), args.creator.encode())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'ROM validation failed: {error}\n')
    text = json.dumps(result, indent=2)+'\n'
    if args.manifest:
        args.manifest.write_text(text)
    print(text, end='')

if __name__ == '__main__':
    main()
