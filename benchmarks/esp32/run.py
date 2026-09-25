#!/usr/bin/env python3
"""Upload selected firmware, capture repeated runs, and compare saved timings."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import time

from check_build import check_commands

CASES = ('sample', 'flat_scalars', 'telemetry', 'wide_tree',
         'chain_1', 'chain_4', 'chain_8', 'chain_16')
FIELDS = ('backend', 'case', 'operation', 'wire_bytes', 'object_bytes', 'iterations',
          'median_us', 'min_us', 'max_us', 'wire_fnv1a')


def capture(port_name, backend, runs, output):
    import serial
    expected = 'roo_pb' if backend == 'roo' else 'nanopb-0.4.9.1'
    rows, metadata = [], []
    with serial.Serial(port_name, 115200, timeout=1) as port, \
            (output / f'{backend}.log').open('w') as log:
        # Reset to normal boot, not the ROM download mode (DTR stays deasserted).
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        for run in range(runs):
            if run != 0:
                port.write(b'r')
                port.flush()
            deadline = time.monotonic() + 180
            active = False
            current = {}
            while time.monotonic() < deadline:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                log.write(line + '\n')
                log.flush()
                print(line, flush=True)
                if line.startswith('FAIL,') or 'Guru Meditation' in line:
                    raise RuntimeError('Device failed correctness/measurement checks')
                if line.startswith('BEGIN,'):
                    if line != 'BEGIN,' + expected or active:
                        raise RuntimeError('Wrong backend or unexpected device reset')
                    active = True
                elif active and line.startswith('META,'):
                    metadata.append(line)
                elif active and line.startswith('RESULT,'):
                    values = line.split(',')[1:]
                    if len(values) != len(FIELDS):
                        raise RuntimeError('Malformed result: ' + line)
                    row = dict(zip(FIELDS, values))
                    key = (row['case'], row['operation'])
                    if key in current or row['backend'] != expected:
                        raise RuntimeError('Duplicate result or wrong backend')
                    for name in ('wire_bytes', 'object_bytes', 'iterations'):
                        row[name] = int(row[name])
                    for name in ('median_us', 'min_us', 'max_us'):
                        row[name] = float(row[name])
                    if not 0 < row['min_us'] <= row['median_us'] <= row['max_us']:
                        raise RuntimeError('Invalid measurement')
                    row['run'] = run + 1
                    current[key] = row
                elif active and line.startswith('END,'):
                    if line.split(',')[1] != expected:
                        raise RuntimeError('Wrong completion marker')
                    break
            else:
                raise TimeoutError('No complete benchmark run within 180 seconds')
            keys = {(case, operation) for case in CASES for operation in ('encode', 'decode')}
            if set(current) != keys:
                raise RuntimeError('Incomplete benchmark results')
            rows.extend(current.values())
    return rows, metadata


def report(rows):
    print('\nCase            bytes   encode roo/np (us)   decode roo/np (us)')
    for case in CASES:
        measurements = {}
        fingerprints = set()
        for backend in ('roo_pb', 'nanopb-0.4.9.1'):
            for operation in ('encode', 'decode'):
                selected = [r for r in rows if r['backend'] == backend
                            and r['case'] == case and r['operation'] == operation]
                if selected:
                    measurements[(backend, operation)] = statistics.median(r['median_us'] for r in selected)
                    fingerprints.update((r['wire_bytes'], r['wire_fnv1a']) for r in selected)
        if len(fingerprints) != 1:
            raise RuntimeError('Wire size/hash mismatch: ' + case)
        if len(measurements) == 4:
            enc = [measurements[(b, 'encode')] for b in ('roo_pb', 'nanopb-0.4.9.1')]
            dec = [measurements[(b, 'decode')] for b in ('roo_pb', 'nanopb-0.4.9.1')]
            print(f'{case:15} {next(iter(fingerprints))[0]:5}   '
                  f'{enc[0]:8.3f}/{enc[1]:8.3f}   {dec[0]:8.3f}/{dec[1]:8.3f}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--backend', choices=('both', 'roo', 'nanopb'), default='both')
    parser.add_argument('--upload', action='store_true', help='Authorize replacing the board firmware')
    parser.add_argument('--pio', default='pio', help='PlatformIO executable')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    if args.backend == 'both' and not args.upload:
        parser.error('--backend both requires --upload')
    project = Path(__file__).resolve().parent
    root = project.parents[1]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = args.output or root / 'build/esp32_benchmark' / ('results-' + stamp)
    output.mkdir(parents=True, exist_ok=False)
    backends = ('roo', 'nanopb') if args.backend == 'both' else (args.backend,)
    rows, meta = [], {}
    manifest = {
        'timestamp_utc': stamp, 'port': args.port, 'runs': args.runs,
        'roo_pb_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'tracked_diff_sha256': hashlib.sha256(subprocess.check_output(['git', 'diff', 'HEAD'], cwd=root)).hexdigest(),
        'benchmark_sha256': {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(project.rglob('*')) if p.is_file() and '__pycache__' not in p.parts},
    }
    for backend in backends:
        if args.upload:
            # Audit resolved commands before any upload and preserve the evidence.
            subprocess.run([args.pio, 'run', '-d', str(project), '-e', backend,
                            '-t', 'compiledb'], check=True)
            database = project / 'compile_commands.json'
            saved_database = output / f'{backend}-compile-commands.json'
            shutil.move(database, saved_database)
            units = check_commands(saved_database, backend)
            manifest[backend + '_verified_translation_units'] = units
            manifest[backend + '_compile_commands_sha256'] = hashlib.sha256(
                saved_database.read_bytes()).hexdigest()
            command = [args.pio, 'run', '-d', str(project), '-e', backend,
                       '-t', 'upload', '--upload-port', args.port, '-j', '4']
            with (output / f'{backend}-upload.log').open('w') as log:
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                for line in process.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                if process.wait() != 0:
                    raise RuntimeError('Build/upload failed; see ' + str(log.name))
        firmware = root / 'build/esp32_benchmark/pio' / backend / 'firmware.bin'
        if firmware.exists():
            manifest[backend + '_firmware_sha256'] = hashlib.sha256(firmware.read_bytes()).hexdigest()
        captured, metadata = capture(args.port, backend, args.runs, output)
        rows.extend(captured)
        meta[backend] = metadata
        with (output / 'results.csv').open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=('run', *FIELDS))
            writer.writeheader()
            writer.writerows(rows)
        (output / 'manifest.json').write_text(json.dumps({**manifest, 'device_metadata': meta}, indent=2) + '\n')
    report(rows)
    print('Saved raw logs, CSV and manifest in', output)


if __name__ == '__main__':
    main()
