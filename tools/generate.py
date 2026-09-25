#!/usr/bin/env python3
"""Repository-local launcher also usable from Bazel's execution root."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / 'compiler'))
from roo_pbc.__main__ import main

if __name__ == '__main__':
    main()
