"""DOSBox references (.binz) for the remc2 level tests: the level started without any input.

    python make_level_refs.py [levels...] [--frames N] [--jobs N] [--fast]

Output: remc2-regression-test/memimages/regressions/level<LLL>/sequence-002285FF-*.binz.
The recording given to run_replay.ps1 only satisfies its check, -NoPlayback gives the game no input.
"""
import concurrent.futures
import os
import shutil
import subprocess
import sys

from make_record_refs import HERE, REGRESSIONS, FAST, worker_conf


def any_recording():
    for name in sorted(os.listdir(REGRESSIONS)):
        folder = os.path.join(REGRESSIONS, name)
        if name.startswith('record') and os.path.isdir(folder):
            for n in os.listdir(folder):
                if n.endswith('.dem'):
                    return os.path.join(folder, n)
    raise SystemExit('no record<N>/*.dem')


def run(dem, level, frames, conf):
    tag = 'level%d_%df' % (level, frames)
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, 'run_replay.ps1'),
                    '-Play', dem, '-NoPlayback', '-Level', str(level - 1), '-Frames', str(frames), '-SeqZ', '-Tag', tag,
                    '-Conf', conf, '-TimeoutSec', '36000'] + FAST,
                   capture_output=True, text=True)
    run_dir = os.path.join(HERE, 'work', 'runs', tag)
    end = [l for l in open(os.path.join(run_dir, 'frames.txt'), encoding='latin-1') if l.startswith('# konec')]
    done = sum(1 for l in open(os.path.join(run_dir, 'frames.txt'), encoding='latin-1') if not l.startswith('#'))
    if done != frames:
        return '%s: only %d of %d frames, reference kept' % (tag, done, frames)
    dst = os.path.join(REGRESSIONS, 'level%03d' % level)
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(os.path.join(run_dir, 'regressions')):
        if name.endswith('.binz'):
            shutil.copy(os.path.join(run_dir, 'regressions', name), dst)
    return '%s: %d frames, %s' % (tag, frames, end[0].strip() if end else 'NO END')


def main():
    args = sys.argv[1:]
    if '--fast' in args:
        args.remove('--fast')
        FAST.extend(['-NoWait', '-NoRender'])
    options = {'--jobs': 6, '--frames': 2000}
    for key in options:
        if key in args:
            i = args.index(key)
            options[key] = int(args[i + 1])
            del args[i:i + 2]
    levels = [int(a) for a in args] or sorted(int(n[5:]) for n in os.listdir(REGRESSIONS) if n.startswith('level') and n[5:].isdigit())
    dem = any_recording()
    confs = [worker_conf(n) for n in range(options['--jobs'])]
    free = list(confs)
    with concurrent.futures.ThreadPoolExecutor(len(confs)) as pool:
        pending = {}
        for level in levels:
            if not free:
                done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
                for f in done:
                    print(f.result(), flush=True)
                    free.append(pending.pop(f))
            conf = free.pop()
            pending[pool.submit(run, dem, level, options['--frames'], conf)] = conf
        for f in concurrent.futures.as_completed(pending):
            print(f.result(), flush=True)


if __name__ == '__main__':
    main()
