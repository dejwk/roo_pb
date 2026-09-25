"""Select only the requested runtime and generated schema for PlatformIO."""
from pathlib import Path

Import('env')

env.Append(CXXFLAGS=['-fno-exceptions', '-fno-rtti'])

project = Path(env['PROJECT_DIR'])
# Check every compiled translation unit, including the separately built codecs.
env.Append(CCFLAGS=['-include', str(project / 'build_contract.h')])
root = project.parents[1]
build = root / 'build/esp32_benchmark'
backend = env['PIOENV']
if backend not in ('roo', 'nanopb'):
    raise RuntimeError('Use the roo or nanopb environment')
generated = build / 'generated' / backend
if not (generated / 'benchmark.pb.h').exists():
    raise RuntimeError('Run python3 prepare.py before building')
env.Append(CPPPATH=[str(generated)])
if backend == 'roo':
    env.Append(CPPPATH=[str(root / 'src')])
    for name in ('roo_io', 'roo_backport', 'roo_logging', 'roo_flags', 'roo_time', 'roo_threads'):
        env.Append(CPPPATH=[str(root.parent / name / 'src')])
    env.BuildSources('$BUILD_DIR/roo_pb', str(root / 'src/roo_pb'),
                     src_filter='+<wire.cpp>')
    env.BuildSources('$BUILD_DIR/unicode', str(root.parent / 'roo_io/src/roo_io/text'),
                     src_filter='+<unicode.cpp>')
else:
    nanopb = build / 'nanopb-0.4.9.1-linux-x86'
    env.Append(CPPPATH=[str(nanopb)])
    env.BuildSources('$BUILD_DIR/nanopb_runtime', str(nanopb),
                     src_filter='+<pb_common.c> +<pb_encode.c> +<pb_decode.c>')
    env.BuildSources('$BUILD_DIR/nanopb_schema', str(generated),
                     src_filter='+<benchmark.pb.c>')
