#!/usr/bin/env python3
"""Host-check both compiled backends and their exact wire format before flashing."""
import argparse
import importlib.util
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--oracle', action='store_true',
                        help='Also validate with Python protobuf (test-only dependency)')
    args = parser.parse_args()
    project = Path(__file__).resolve().parent
    root = project.parents[1]
    build = root / 'build/esp32_benchmark'
    nanopb = build / 'nanopb-0.4.9.1-linux-x86'
    host = build / 'host'
    host.mkdir(exist_ok=True)
    common = ['-O2', '-g', '-Wall', '-Wextra', '-Werror']
    if args.sanitize:
        common += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    outputs = {}
    for backend in ('roo', 'nanopb'):
        command = ['g++', '-std=c++17', '-fno-exceptions', '-fno-rtti', *common,
                   '-DPROTO_BENCH_' + backend.upper(),
                   '-I' + str(build / 'generated' / backend),
                   str(project / 'src/main.cpp')]
        if backend == 'roo':
            command += ['-DESP_PLATFORM', '-I' + str(root / 'src')]
            for name in ('roo_io', 'roo_backport', 'roo_logging', 'roo_flags', 'roo_time', 'roo_threads'):
                command += ['-isystem', str(root.parent / name / 'src')]
            command += [str(root / 'src/roo_pb/wire.cpp'),
                        str(root.parent / 'roo_io/src/roo_io/text/unicode.cpp')]
        else:
            nanopb_flags = ['-DPB_BUFFER_ONLY', '-DPB_VALIDATE_UTF8']
            command += [*nanopb_flags, '-I' + str(nanopb)]
            sources = [nanopb / name for name in ('pb_common.c', 'pb_encode.c', 'pb_decode.c')]
            sources += [build / 'generated/nanopb/benchmark.pb.c']
            for source in sources:
                obj = host / (source.stem + '.o')
                subprocess.run(['gcc', '-std=c11', *common, *nanopb_flags, '-I' + str(nanopb),
                                '-I' + str(build / 'generated/nanopb'),
                                '-c', str(source), '-o', str(obj)], check=True)
                command += [str(obj)]
        exe = host / backend
        subprocess.run([*command, '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        outputs[backend] = result.stdout
    if outputs['roo'] != outputs['nanopb']:
        raise RuntimeError('Backend wire bytes differ')
    wires = {}
    for line in outputs['roo'].splitlines():
        marker, name, encoded = line.split(',')
        if marker != 'WIRE' or name in wires:
            raise RuntimeError('Malformed validation output')
        wires[name] = bytes.fromhex(encoded)
    expected = {'sample', 'flat_scalars', 'telemetry', 'wide_tree',
                'chain_1', 'chain_4', 'chain_8', 'chain_16'}
    if set(wires) != expected:
        raise RuntimeError('Missing workload')
    if args.oracle:
        subprocess.run([str(nanopb / 'generator-bin/protoc'), '-I', str(project),
                        '--python_out=' + str(host), 'benchmark.proto'], check=True)
        spec = importlib.util.spec_from_file_location('benchmark_pb2', host / 'benchmark_pb2.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        types = {'sample': module.Sample, 'flat_scalars': module.Flat,
                 'telemetry': module.Telemetry, 'wide_tree': module.Tree}
        types.update({f'chain_{n}': getattr(module, f'Chain{n}') for n in (1, 4, 8, 16)})
        for name, wire in wires.items():
            value = types[name].FromString(wire)
            if value.SerializeToString(deterministic=True) != wire:
                raise RuntimeError('Oracle wire mismatch: ' + name)
            if name.startswith('chain_'):
                depth = int(name.split('_')[1])
                for level in range(depth):
                    assert value.id == level + 1 and value.HasField('child')
                    value = value.child
                assert value.id == 1000 + depth + 1
            elif name == 'wide_tree':
                assert len(value.branches) == 4
                assert all(len(b.samples) == 8 for b in value.branches)
                assert value.branches[3].samples[7].id == 1032
            elif name == 'telemetry':
                assert len(value.payload) == 128 and len(value.readings) == 64
                assert value.WhichOneof('state') == 'sample'
                assert value.readings[63] == 63 * 731 - 16000
            elif name == 'flat_scalars':
                assert value.signed_value == -12345 and value.voltage == 3.3125
            else:
                assert value.id == 1001 and value.value == -12346
    for name, wire in wires.items():
        print(f'PASS {name}: {len(wire)} identical wire bytes, round trip and short-buffer checks')
    print('PASS independent Python protobuf checks' if args.oracle else 'Oracle check not requested')


if __name__ == '__main__':
    main()
