#!/usr/bin/env python3
"""
compare_dumps.py

Compares two dump directories produced by the same DOSBox-X level run
(engine.cpp's writeseqall(0x2285ff, 0, N) via two separate process
launches - "pass 1" and "pass 2"). Each directory contains one file per
dumped memory region, named:

    sequence-<EIP:08X>-<DATAADDR:08X>.bin

Every file is just N fixed-size chunks concatenated back to back (one
chunk per simulation frame). This script finds, per file, the first byte
offset where the two passes differ, converts that back to a frame number,
and prints it - that's the earliest point at which the two "identical"
runs actually diverged.

Can be used standalone:
    python3 tools/compare_dumps.py dump/level_003/pass1 dump/level_003/pass2

or imported (see run_regression.py) via compare_trees(dir1, dir2).
"""
import glob
import os
import re
import sys

# Must match the six writesequence() calls inside writeseqall() in
# engine.cpp - keep these two lists in sync if you ever change that
# function. Maps a dataaddress -> (chunk size in bytes, human label).
REGION_INFO = {
    0x2dc4e0: (0x70000,   "screen/framebuffer"),
    0x356038: (0x36e16,   "D41A0 main game-state struct"),
    0x3aa0a4: (320 * 200, "vga plane"),
    0x3514b0: (0xab,      "player struct(s)"),
    0x2b3a74: (0xc4e,     "table @2b3a74"),
    0x34c4e0: (0x2,       "RNG seed - check this one first"),
}

FILENAME_RE = re.compile(r"sequence-([0-9A-Fa-f]{8})-([0-9A-Fa-f]{8})\.bin$")


def describe_file(path):
    m = FILENAME_RE.search(os.path.basename(path))
    if not m:
        return None
    eip = int(m.group(1), 16)
    dataaddr = int(m.group(2), 16)
    size, label = REGION_INFO.get(dataaddr, (None, "unknown region"))
    return eip, dataaddr, size, label


def first_diff_offset(path1, path2):
    """Returns the first byte offset where the two files differ, or None
    if they're identical (comparing up to the shorter file's length; a
    length mismatch itself is reported by the caller)."""
    CHUNK = 1 << 20
    with open(path1, "rb") as f1, open(path2, "rb") as f2:
        offset = 0
        while True:
            b1 = f1.read(CHUNK)
            b2 = f2.read(CHUNK)
            if not b1 and not b2:
                return None
            n = min(len(b1), len(b2))
            for i in range(n):
                if b1[i] != b2[i]:
                    return offset + i
            if len(b1) != len(b2):
                return offset + n  # one file ends before the other
            offset += n


def compare_trees(dir1, dir2):
    """Compares every sequence-*.bin file that appears in dir1 (and,
    where present, dir2). Returns a list of human-readable diff strings;
    an empty list means the two runs were byte-for-byte identical."""
    diffs = []
    files1 = sorted(glob.glob(os.path.join(dir1, "sequence-*.bin")))
    if not files1:
        diffs.append(f"no sequence-*.bin files found in {dir1} - did the dump actually run?")
        return diffs

    for path1 in files1:
        name = os.path.basename(path1)
        path2 = os.path.join(dir2, name)
        info = describe_file(path1)
        if info is None:
            continue
        eip, dataaddr, size, label = info

        if not os.path.exists(path2):
            diffs.append(f"{name}: missing in {dir2}")
            continue

        s1 = os.path.getsize(path1)
        s2 = os.path.getsize(path2)
        if s1 != s2:
            diffs.append(f"{name} ({label}): size differs ({s1} vs {s2} bytes) - "
                          f"one run dumped fewer frames than the other")

        off = first_diff_offset(path1, path2)
        if off is None:
            continue

        if size:
            frame = off // size
            byte_in_frame = off % size
            diffs.append(
                f"{name} ({label}, dumped at EIP 0x{eip:08X}, source addr 0x{dataaddr:08X}): "
                f"first difference at byte offset {off} -> frame {frame}, "
                f"byte {byte_in_frame} within that frame's {size}-byte snapshot"
            )
        else:
            diffs.append(f"{name}: first difference at byte offset {off} "
                          f"(unknown chunk size for addr 0x{dataaddr:08X} - "
                          f"add it to REGION_INFO)")

    return diffs


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <pass1_dir> <pass2_dir>", file=sys.stderr)
        sys.exit(2)

    dir1, dir2 = sys.argv[1], sys.argv[2]
    diffs = compare_trees(dir1, dir2)
    if not diffs:
        print("identical - no divergence found across the dumped frames")
        sys.exit(0)

    print(f"found {len(diffs)} difference(s):")
    for d in diffs:
        print(f"  {d}")
    sys.exit(1)


if __name__ == "__main__":
    main()
