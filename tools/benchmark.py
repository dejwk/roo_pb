#!/usr/bin/env python3
"""Compare buffer throughput and text size with a pre-rewrite checkout.

Both builds use identical flat and nested in-memory workloads. No Bazel output
roots or caches are created. The optional baseline must have the iterator API.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--roo-root', type=Path, required=True)
    parser.add_argument('--baseline-root', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / 'build/benchmark'
    build.mkdir(parents=True, exist_ok=True)
    checkouts = [('buffer', root, 1)]
    if args.baseline_root:
        checkouts.insert(0, ('baseline', args.baseline_root, 0))
    for name, checkout, buffer_api in checkouts:
        exe = build / (name + '_bench')
        cmd = ['g++', '-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-DESP_PLATFORM', f'-DBUFFER_API={buffer_api}',
               '-I' + str(checkout / 'src'),
               '-I' + str(checkout / 'examples/telemetry')]
        for dep in ['roo_io', 'roo_backport', 'roo_logging', 'roo_flags',
                    'roo_time', 'roo_threads']:
            cmd += ['-isystem', str(args.roo_root / dep / 'src')]
        cmd += [str(root / 'tools/buffer_benchmark.cpp')]
        cmd += [str(source) for source in sorted((checkout / 'src').rglob('*.cpp'))]
        cmd += [str(args.roo_root / 'roo_io/src/roo_io/text/unicode.cpp'), '-o', str(exe)]
        subprocess.run(cmd, check=True)
        print(name, flush=True)
        subprocess.run(['size', str(exe)], check=True)
        for _ in range(3):
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
