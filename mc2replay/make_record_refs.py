"""DOSBox references (.binz) for the remc2 record tests: every level of every recording.

    python make_record_refs.py [record numbers or record:level...] [--jobs N] [--fast]

--fast: run_replay.ps1 -NoWait -NoRender (no wait for the timer, no DrawWorld), ~6x faster, same .binz.

Recordings: remc2-regression-test/memimages/regressions/record<NNN>/*.dem (MC2-HD-RecordV03).
Output: record<N>/level<L>/sequence-002285FF-*.binz in the repository.  Frames = turns of the level.
Every parallel run has its own copy of the game: the game writes NETHERW/CLEVELS and NETHERW/SAVE.
"""
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = r'C:\prenos\remc2-dev2\remc2'
REGRESSIONS = os.path.join(REPO, r'remc2-regression-test\memimages\regressions')


def level_turns(path):
    d = open(path, 'rb').read()
    assert d[:16] == b'MC2-HD-RecordV03', path
    p, out = 16, {}
    while p < len(d):
        lv, pc = struct.unpack_from('<HH', d, p)
        p += 4
        sc, = struct.unpack_from('<I', d, p)
        p += 4
        for _ in range(sc):
            s, = struct.unpack_from('<I', d, p)
            p += 4 + s
        for _ in range(pc):
            pi, tc = struct.unpack_from('<HI', d, p)
            p += 6 + 208
            for _ in range(tc):
                size, = struct.unpack_from('<I', d, p + 12)
                p += 16 + size
            if pi == 0 and tc:
                out[lv + 1] = tc
    return out


def worker_conf(n):
    game = os.path.join(HERE, 'work', 'game_w%d' % n)
    if not os.path.exists(game):
        shutil.copytree(os.path.join(HERE, 'work', 'game'), game)
    conf = os.path.join(HERE, 'work', 'mc2replay_w%d.conf' % n)
    text = open(os.path.join(HERE, 'mc2replay.conf'), encoding='latin-1').read()
    text = text.replace('mount c c:/prenos/dosbox-x-remc2/mc2replay/work/game/', 'mount c ' + game.replace('\\', '/') + '/')
    open(conf, 'w', encoding='latin-1').write(text)
    return conf


FAST = []  # ['-NoWait', '-NoRender'] with --fast


def run(job, conf):
    record, dem, level, frames = job
    tag = 'record%d_L%d' % (record, level)
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, 'run_replay.ps1'),
                    '-Play', dem, '-Level', str(level - 1), '-Frames', str(frames), '-SeqZ', '-Tag', tag, '-Conf', conf, '-TimeoutSec', '36000'] + FAST,
                   capture_output=True, text=True)
    run_dir = os.path.join(HERE, 'work', 'runs', tag)
    lines = open(os.path.join(run_dir, 'frames.txt'), encoding='latin-1').readlines()
    end = [l for l in lines if l.startswith('# konec')]
    done = sum(1 for l in lines if not l.startswith('#'))
    if not end or done != frames:  # an unfinished run leaves only *.z1tmp, the test would see no reference
        return '%s: UNFINISHED, %d of %d frames, %s - reference not written' % (tag, done, frames, end[0].strip() if end else 'no end')
    dst = os.path.join(REGRESSIONS, 'record%03d' % record, 'level%03d' % level)
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(os.path.join(run_dir, 'regressions')):
        if name.endswith('.binz'):  # MC2SEQZ4; a <name>.z1tmp is left only when its conversion failed
            shutil.copy(os.path.join(run_dir, 'regressions', name), dst)
    return '%s: %d frames, %s' % (tag, frames, end[0].strip() if end else 'NO END')


def main():
    args = sys.argv[1:]
    if '--fast' in args:
        args.remove('--fast')
        FAST.extend(['-NoWait', '-NoRender'])
    jobs_count = 6
    if '--jobs' in args:
        i = args.index('--jobs')
        jobs_count = int(args[i + 1])
        del args[i:i + 2]
    only = {tuple(map(int, a.split(':'))) for a in args if ':' in a}  # record:level
    records = sorted({int(a.split(':')[0]) for a in args}) or sorted(int(n[6:]) for n in os.listdir(REGRESSIONS) if n.startswith('record'))
    jobs = []
    for record in records:
        folder = os.path.join(REGRESSIONS, 'record%03d' % record)
        dem = [n for n in os.listdir(folder) if n.endswith('.dem')][0]
        for level, frames in level_turns(os.path.join(folder, dem)).items():
            if not only or (record, level) in only:
                jobs.append((record, os.path.join(folder, dem), level, frames))
    jobs.sort(key=lambda j: -j[3])  # the longest first
    confs = [worker_conf(n) for n in range(jobs_count)]
    free = list(confs)
    with concurrent.futures.ThreadPoolExecutor(jobs_count) as pool:
        pending = {}
        for job in jobs:
            if not free:
                done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
                for f in done:
                    print(f.result(), flush=True)
                    free.append(pending.pop(f))
            conf = free.pop()
            pending[pool.submit(run, job, conf)] = conf
        for f in concurrent.futures.as_completed(pending):
            print(f.result(), flush=True)


if __name__ == '__main__':
    main()
