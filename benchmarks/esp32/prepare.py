#!/usr/bin/env python3
"""Fetch pinned test-only nanopb tooling and generate both benchmark backends."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

NAME = 'nanopb-0.4.9.1-linux-x86'
SHA256 = '951a9ab2385424a4cdf245d0c84f4c88c6ccbc65a0dade4b246d50c068f24128'
URL = f'https://github.com/nanopb/nanopb/releases/download/nanopb-0.4.9.1/{NAME}.tar.gz'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='Use an already downloaded release archive')
    args = parser.parse_args()
    project = Path(__file__).resolve().parent
    root = project.parents[1]
    build = root / 'build/esp32_benchmark'
    build.mkdir(parents=True, exist_ok=True)
    archive = args.archive or build / (NAME + '.tar.gz')
    if not archive.exists():
        print('Downloading', URL, flush=True)
        urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('nanopb archive checksum mismatch')
    nanopb = build / NAME
    if not nanopb.exists():
        with tarfile.open(archive) as source:
            source.extractall(build, filter='data')
    for backend in ('roo', 'nanopb'):
        (build / 'generated' / backend).mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(root / 'tools/generate.py'), '-I', str(project),
                    '--out', str(build / 'generated/roo'), 'benchmark.proto'], check=True)
    subprocess.run([str(nanopb / 'generator-bin/protoc'), '-I', str(project),
                    '--nanopb_out=' + str(build / 'generated/nanopb'),
                    'benchmark.proto'], cwd=project, check=True)
    subprocess.run(['clang-format', '-i', str(build / 'generated/roo/benchmark.pb.h')], check=True)
    print('Generated both backends in', build / 'generated')


if __name__ == '__main__':
    main()
