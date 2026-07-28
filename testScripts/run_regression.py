#!/usr/bin/env python3
"""
run_regression.py

Drives the DOSBox-X binary (built with the modified src/engine/engine.cpp)
level by level: for every level in [--level-start, --level-end], launches
it twice ("pass 1" and "pass 2") with identical parameters, then calls
compare_dumps.py on the two resulting dump trees to look for the first
frame where the two runs diverge (typically caused by an uninitialised
variable, a use of real wall-clock time, or anything else that isn't
actually deterministic).

Each DOSBox-X launch is a single-purpose worker: load one level, dump
NETHERW_DUMP_STEPS simulation frames, exit. That's driven entirely by
environment variables read once in engine.cpp - see the comment block at
the top of that file for the full list.

Usage:
    python3 tools/run_regression.py \\
        --dosbox /path/to/dosbox-x.exe \\
        --conf /path/to/your.conf \\
        --level-start 1 --level-end 30 \\
        --dump-steps 3000 \\
        --out-dir dump

Adjust --dosbox-args if your build needs extra command-line flags to boot
straight into the game (e.g. -conf, -fastlaunch, whatever your setup uses).
"""
import argparse
import os
import subprocess
import sys

import compare_dumps


def run_one(dosbox_path, dosbox_args, level, run_pass, dump_steps, out_dir, timeout):
    env = os.environ.copy()
    env["NETHERW_LEVEL"] = str(level)
    env["NETHERW_RUN_PASS"] = str(run_pass)
    env["NETHERW_DUMP_STEPS"] = str(dump_steps)
    env["NETHERW_OUT_DIR"] = out_dir

    cmd = [dosbox_path] + dosbox_args
    print(f"[run_regression] level={level} pass={run_pass}: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, env=env, timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"[run_regression] level={level} pass={run_pass}: TIMED OUT after {timeout}s "
              f"- the dump probably never completed (level never loaded? "
              f"wrong *_EIP addresses for this build?)", file=sys.stderr)
        return False
    if result.returncode not in (0, None):
        print(f"[run_regression] level={level} pass={run_pass}: exited with code "
              f"{result.returncode}", file=sys.stderr)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dosbox", required=True, help="path to the dosbox-x executable")
    ap.add_argument("--dosbox-args", nargs=argparse.REMAINDER, default=[],
                     help="extra args passed through to dosbox-x (put last)")
    ap.add_argument("--level-start", type=int, default=1)
    ap.add_argument("--level-end", type=int, default=1)
    ap.add_argument("--dump-steps", type=int, default=3000)
    ap.add_argument("--out-dir", default="dump")
    ap.add_argument("--timeout", type=int, default=300,
                     help="seconds to wait for one dosbox-x run before giving up")
    ap.add_argument("--stop-on-diff", action="store_true",
                     help="stop the whole sweep as soon as a level shows a divergence")
    args = ap.parse_args()

    any_diff = False
    for level in range(args.level_start, args.level_end + 1):
        ok1 = run_one(args.dosbox, args.dosbox_args, level, 1, args.dump_steps, args.out_dir, args.timeout)
        ok2 = run_one(args.dosbox, args.dosbox_args, level, 2, args.dump_steps, args.out_dir, args.timeout)
        if not (ok1 and ok2):
            print(f"[run_regression] level {level}: skipping compare, a run failed", file=sys.stderr)
            continue

        dir1 = os.path.join(args.out_dir, f"level_{level:03d}", "pass1")
        dir2 = os.path.join(args.out_dir, f"level_{level:03d}", "pass2")
        diffs = compare_dumps.compare_trees(dir1, dir2)
        if diffs:
            any_diff = True
            print(f"[run_regression] level {level}: DIVERGED")
            for d in diffs:
                print(f"    {d}")
            if args.stop_on_diff:
                break
        else:
            print(f"[run_regression] level {level}: identical across both passes")

    sys.exit(1 if any_diff else 0)


if __name__ == "__main__":
    main()
