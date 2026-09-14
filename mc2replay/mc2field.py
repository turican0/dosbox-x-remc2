"""Names a byte of D41A0 and shows how it evolves in a run's sequence files.

    python mc2field.py <run dir> <offset hex> [frame]

Offsets are D41A0 offsets as remc2 reports them ("Compare error ... byte: 63654/0xf8a6").
The field is located in the big arrays (players, entities, StageVars2) and the bytes around
it are printed for the frames just before and after <frame>, so a change can be seen in
context rather than as one byte.
"""
import os
import struct
import sys

SIZE = 0x36E16           # D41A0 as remc2 compares it (shadow layout, 224790 bytes)
PLAYERS, PLAYER_SIZE = 0x2BDE, 0x84C
ENTITIES, ENTITY_SIZE = 0x6E8E, 0xA8
STAGEVARS2, STAGEVAR_SIZE = 0x365F4, 8

ENTITY_FIELDS = {
    0x00: 'dword_0x0', 0x08: 'life_0x8', 0x0C: 'struct_byte_0xc_12_15', 0x14: 'rand_0x14_20',
    0x20: 'roll_0x20_32', 0x30: 'word_0x30_48', 0x3E: 'byte_0x3E_62', 0x3F: 'class_0x3F_63',
    0x40: 'model_0x40_64', 0x45: 'actionIndex_0x45_69', 0x48: 'StageVar1_0x48_72',
    0x49: 'StageVar2_0x49_73', 0x4A: 'word_0x4A_74', 0x4C: 'position_0x4C_76 (x,y,z)',
    0x5C: 'animationFrame_0x5C_92', 0x96: 'word_0x96_150', 0xA0: 'dword_0xA0_160x (ptr)',
    0xA4: 'dword_0xA4_164x (ptr)',
}


def locate(off):
    if PLAYERS <= off < PLAYERS + 8 * PLAYER_SIZE:
        i, rel = divmod(off - PLAYERS, PLAYER_SIZE)
        extra = ''
        if 0x3E6 <= rel:
            extra = ' = dword_0x3E6_2BE4_12228 +0x%X' % (rel - 0x3E6)
            if 0x3E6 + 0x263 <= rel:
                extra += ' = str_611 +0x%X' % (rel - 0x3E6 - 0x263)
        return 'array_0x2BDE[%d] +0x%X%s' % (i, rel, extra), PLAYERS + i * PLAYER_SIZE, PLAYER_SIZE
    if ENTITIES <= off < ENTITIES + 1000 * ENTITY_SIZE:
        i, rel = divmod(off - ENTITIES, ENTITY_SIZE)
        name = max((k for k in ENTITY_FIELDS if k <= rel), default=0)
        return 'struct_0x6E8E[%d] +0x%X (%s +%d)' % (i, rel, ENTITY_FIELDS[name], rel - name), ENTITIES + i * ENTITY_SIZE, ENTITY_SIZE
    if STAGEVARS2 <= off < STAGEVARS2 + 400 * STAGEVAR_SIZE:
        i, rel = divmod(off - STAGEVARS2, STAGEVAR_SIZE)
        names = ['index_0x3647A_0', 'stage_0x3647A_1', 'str_0x3647A_2.x', 'str_0x3647A_2.y', 'str_0x3647C_4', 'str_0x3647C_4', 'str_0x3647C_4', 'str_0x3647C_4']
        return 'StageVars2_0x365F4[%d] +%d (%s)' % (i, rel, names[rel]), STAGEVARS2 + i * STAGEVAR_SIZE, STAGEVAR_SIZE
    return 'D41A0 +0x%X' % off, off - (off % 16), 32


def main():
    run, off = sys.argv[1], int(sys.argv[2], 16)
    frame = int(sys.argv[3]) if len(sys.argv) > 3 else None
    path = os.path.join(run, 'regressions', 'sequence-002285FF-00356038.bin')
    frames = os.path.getsize(path) // SIZE
    name, base, length = locate(off)
    print('0x%X = %s' % (off, name))
    first = 0 if frame is None else max(0, frame - 2)
    last = frames - 1 if frame is None else min(frames - 1, frame + 1)
    with open(path, 'rb') as f:
        for fr in range(first, last + 1):
            f.seek(fr * SIZE + base)
            data = f.read(length)
            mark = '>' if fr == frame else ' '
            row = []
            for k in range(0, length, 16):
                row.append(data[k:k + 16].hex(' '))
            print('%s frame %d:' % (mark, fr))
            for k, line in enumerate(row):
                print('     +0x%02X  %s' % (k * 16, line))


if __name__ == '__main__':
    main()
