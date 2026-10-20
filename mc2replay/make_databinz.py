"""remc2-regression-test/data/data.binz: the only game data of the regression tests in GitHub Actions.

    python make_databinz.py [out.binz]

Everything the simulation reads, and of the rest only the skeletons (tables of sprite sizes), no graphics:
LEVELS, SPELLS, BLDGPRM, BUILD0-0.DAT (hit tests of the building sprites), SEARCH.DAT (terrain), every DATA/*.TAB,
TMAPSMETA.DAT (sprite headers and frame counts, remc2-regression-test --make_tmaps_meta) and a skeleton of
SCREENS/HSCREEN0.DAT: its size, zeros, only the blocks the menus read as sprite tables (x_DWORD_17DED4/17DEC0/17DEC8).
"""
import os
import shutil
import subprocess
import sys
import tempfile

REMC2 = r'C:\prenos\remc2-dev2\remc2'
CD = os.path.join(REMC2, r'x64\Release\CD_Files')
EXE = os.path.join(REMC2, r'x64\Release\remc2-regression-test.exe')
DATAPACK = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'datapack.exe')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REMC2, r'remc2-regression-test\data\data.binz')

FILES = ['LEVELS/LEVELS.TAB', 'LEVELS/LEVELS.DAT', 'DATA/SPELLS.DAT', 'DATA/BLDGPRM.DAT', 'DATA/BUILD0-0.DAT',
         'DATA/SEARCH.DAT']
# (offset, length) of HSCREEN0.DAT read into the sprite tables (EventsFunctions.cpp / MenusAndIntros.cpp sub_7AA70)
HSCREEN_TABLES = [(271062, 411), (0x91856, 1027), (375690, 148), (0x13B194, 548), (0x1646BA, 589), (0x164DAE, 543),
                  (0x165329, 548)]


def main():
    stage = tempfile.mkdtemp(prefix='databinz-')
    try:
        names = list(FILES)
        for name in FILES:
            os.makedirs(os.path.join(stage, os.path.dirname(name)), exist_ok=True)
            shutil.copy(os.path.join(CD, name), os.path.join(stage, name))
        for tab in sorted(os.listdir(os.path.join(CD, 'DATA'))):
            if tab.upper().endswith('.TAB'):
                shutil.copy(os.path.join(CD, 'DATA', tab), os.path.join(stage, 'DATA', tab))
                names.append('DATA/' + tab)
        meta = os.path.join(stage, 'DATA', 'TMAPSMETA.DAT')
        subprocess.run([EXE, '--make_tmaps_meta', os.path.join(CD, 'DATA'), meta], check=True, capture_output=True)
        names.append('DATA/TMAPSMETA.DAT')
        original = open(os.path.join(CD, 'DATA', 'SCREENS', 'HSCREEN0.DAT'), 'rb').read()
        skeleton = bytearray(len(original))
        for offset, length in HSCREEN_TABLES:
            skeleton[offset:offset + length] = original[offset:offset + length]
        os.makedirs(os.path.join(stage, 'DATA', 'SCREENS'), exist_ok=True)
        open(os.path.join(stage, 'DATA', 'SCREENS', 'HSCREEN0.DAT'), 'wb').write(skeleton)
        names.append('DATA/SCREENS/HSCREEN0.DAT')
        r = subprocess.run([DATAPACK, 'pack', OUT, stage] + names, capture_output=True, text=True)
        print(r.stdout)
        r = subprocess.run([DATAPACK, 'check', OUT, stage], capture_output=True, text=True)
        print('check', 'OK' if r.returncode == 0 else 'FAILED\n' + r.stdout)
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == '__main__':
    main()
