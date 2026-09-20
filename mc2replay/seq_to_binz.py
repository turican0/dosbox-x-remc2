"""Converts sequence-*.bin (frames one after another) to .binz, the layout mc2check.h writes with -SeqZ.

    python seq_to_binz.py <folder> [--check] [--delete]

All regions of writeseqall: maps (002DC4E0), D41A0 (00356038), screen (003AA0A4), 003514B0, 002B3A74, 0034C4E0.
.binz: "MC2SEQZ1", uint32 frame size; per frame uint32 length + runs against the previous frame:
varint unchanged bytes, varint changed bytes, the changed bytes (gaps under 4 bytes are rewritten).
--check decodes the result and compares it with the .bin, --delete then removes the .bin.
"""
import os
import struct
import sys

import numpy as np

SIZES = {'002DC4E0': 0x70000, '00356038': 0x36E16, '003AA0A4': 320 * 200, '003514B0': 0xAB, '002B3A74': 0xC4E, '0034C4E0': 0x2}


def varint(v):
    out = bytearray()
    while v >= 0x80:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)
    return out


def encode(cur, prev):
    diff = np.flatnonzero(cur != prev)
    out = bytearray()
    pos = 0
    k = 0
    while k < len(diff):
        start = int(diff[k])
        end = start + 1
        k += 1
        while k < len(diff) and diff[k] - end < 4:  # a gap under 4 bytes is rewritten
            end = int(diff[k]) + 1
            k += 1
        out += varint(start - pos)
        out += varint(end - start)
        out += cur[start:end].tobytes()
        pos = end
    if pos < len(cur) or not out:
        out += varint(len(cur) - pos)
        out += varint(0)
    return out


def decode_frames(path):
    with open(path, 'rb') as f:
        assert f.read(8) == b'MC2SEQZ1'
        size, = struct.unpack('<I', f.read(4))
        frame = bytearray(size)
        while True:
            head = f.read(4)
            if len(head) < 4:
                return
            length, = struct.unpack('<I', head)
            data = f.read(length)
            p = pos = 0
            while p < length:
                for field in range(2):
                    v, shift = 0, 0
                    while True:
                        b = data[p]
                        p += 1
                        v |= (b & 0x7F) << shift
                        shift += 7
                        if not b & 0x80:
                            break
                    if field == 0:
                        pos += v
                    else:
                        changed = v
                frame[pos:pos + changed] = data[p:p + changed]
                p += changed
                pos += changed
            yield bytes(frame)


def convert(folder, check, delete):
    for src in sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.startswith('sequence-') and f.endswith('.bin')):
        size = SIZES.get(src[-12:-4])
        if size is None:
            continue
        dst = src + 'z'
        frames = os.path.getsize(src) // size
        prev = np.zeros(size, np.uint8)
        with open(src, 'rb') as fi, open(dst, 'wb') as fo:
            fo.write(b'MC2SEQZ1' + struct.pack('<I', size))
            for _ in range(frames):
                cur = np.frombuffer(fi.read(size), np.uint8)
                changes = encode(cur, prev)
                fo.write(struct.pack('<I', len(changes)) + changes)
                prev = cur
        print('%s: %d frames, %d -> %d bytes' % (os.path.basename(dst), frames, os.path.getsize(src), os.path.getsize(dst)))
        if check:
            with open(src, 'rb') as fi:
                n = 0
                for frame in decode_frames(dst):
                    assert frame == fi.read(size), 'frame %d differs' % n
                    n += 1
            assert n == frames
            print('   check OK')
        if os.path.getsize(src) % size:
            print('   incomplete last frame (%d bytes), .bin kept' % (os.path.getsize(src) % size))
        elif delete and check:
            os.remove(src)


if __name__ == '__main__':
    convert(sys.argv[1], '--check' in sys.argv, '--delete' in sys.argv)
