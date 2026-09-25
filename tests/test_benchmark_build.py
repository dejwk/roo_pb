"""Verifies ESP32 measurement settings fail closed when compiler flags drift."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1] / 'benchmarks/esp32'
SPEC = importlib.util.spec_from_file_location('check_build', PROJECT / 'check_build.py')
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class BenchmarkBuildTest(unittest.TestCase):
    def check(self, extra='', backend='roo', base=None):
        base = base if base is not None else '-Os -std=gnu++17 -fno-exceptions -fno-rtti -fno-lto'
        sources = ['src/main.cpp', 'wire.cpp'] if backend == 'roo' else [
            'src/main.cpp', 'pb_common.c', 'pb_encode.c', 'pb_decode.c']
        entries = [{'file': source, 'command': f'g++ {base} {extra} -c {source}'}
                   for source in sources]
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / 'compile_commands.json'
            database.write_text(json.dumps(entries))
            return CHECK.check_commands(database, backend)

    def test_verified_commands(self):
        self.assertEqual(self.check(), 2)
        self.assertEqual(self.check('-DPB_BUFFER_ONLY -DPB_VALIDATE_UTF8', 'nanopb'), 4)

    def test_reject_overrides_and_missing_flags(self):
        for flag in ('-O0', '-O2', '-std=gnu++2a', '-fexceptions', '-frtti', '-flto'):
            with self.subTest(flag=flag), self.assertRaises(RuntimeError):
                self.check(flag)
        with self.assertRaises(RuntimeError):
            self.check(base='-std=gnu++17 -fno-exceptions -fno-rtti -fno-lto')
        with self.assertRaises(RuntimeError):
            self.check(backend='nanopb')

    def test_actual_preprocessor_contract(self):
        flags = ['-Os', '-std=c++17', '-fno-exceptions', '-fno-rtti']
        for override in ([], ['-O0'], ['-std=c++20'], ['-fexceptions'], ['-frtti']):
            result = subprocess.run(['g++', *flags, *override, '-E', '-x', 'c++',
                                     '-include', str(PROJECT / 'build_contract.h'), '-'],
                                    input='', capture_output=True, text=True)
            self.assertEqual(result.returncode == 0, not override, result.stderr)
        result = subprocess.run(['gcc', '-Os', '-E', '-x', 'c', '-include',
                                 str(PROJECT / 'build_contract.h'), '-'],
                                input='', capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
