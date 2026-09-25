#!/usr/bin/env python3
"""Compile portable tests against real roo dependency headers and run them."""
import argparse
import os
import sys
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--roo-root', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / 'build'
    build.mkdir(exist_ok=True)
    env = dict(os.environ, PYTHONPATH=str(root / 'compiler'))
    subprocess.run([sys.executable, '-m', 'roo_pbc', '-I', str(root / 'examples/telemetry'), '--out', str(build), 'telemetry.proto'], env=env, check=True)
    subprocess.run([sys.executable, '-m', 'roo_pbc', '-I', str(root / 'tests/schemas'), '--out', str(build), 'modern.proto'], env=env, check=True)
    subprocess.run([sys.executable, '-m', 'roo_pbc', '-I', str(root / 'examples/callbacks'), '--out', str(build), 'transfer.proto'], env=env, check=True)
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', str(root / 'tests'), '-p', 'test_*.py'], env=env, check=True)
    for header in build.rglob('*.pb.h'):
        subprocess.run(['clang-format', '-i', str(header)], check=True)
    # Checked-in Arduino example headers must match independent regeneration.
    checked = root / 'examples/telemetry/telemetry.pb.h'
    if checked.exists() and checked.read_bytes() != (build / 'telemetry.pb.h').read_bytes():
        raise RuntimeError('Regenerate examples/telemetry/telemetry.pb.h')
    flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti', '-g', '-DESP_PLATFORM']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    includes = ['-I' + str(root / 'src'), '-I' + str(build)]
    for name in ['roo_io', 'roo_backport', 'roo_logging', 'roo_flags', 'roo_time', 'roo_threads']:
        includes += ['-isystem', str(args.roo_root / name / 'src')]
    sources = sorted((root / 'tests').glob('*_test.cpp')) + [root / 'tests/oracle_driver.cpp'] + sorted((root / 'examples').glob('*/main.cpp'))
    for source in sources:
        exe = build / (source.parent.name + '_example' if source.name == 'main.cpp' else source.stem)
        subprocess.run(['g++', *flags, *includes, str(source), str(root / 'src/roo_pb/wire.cpp'), str(args.roo_root / 'roo_io/src/roo_io/text/unicode.cpp'), '-o', str(exe)], check=True)
        if source.stem != 'oracle_driver':
            subprocess.run([str(exe)], check=True)
        print('PASS', source.name, flush=True)


if __name__ == '__main__':
    main()
