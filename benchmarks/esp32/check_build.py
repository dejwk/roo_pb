"""Verify actual compilation commands, not just requested PlatformIO flags."""
import json
from pathlib import Path
import re
import shlex


def check_commands(database, backend):
    """Reject unexpected optimization/language/runtime flags in every unit."""
    entries = json.loads(Path(database).read_text())
    seen = set()
    for entry in entries:
        args = entry.get('arguments') or shlex.split(entry['command'])
        source = Path(entry['file'])
        seen.add(source.name)
        optimization = [arg for arg in args if re.fullmatch(r'-O(?:[0-3sgz]|fast)?', arg)]
        if not optimization or optimization[-1] != '-Os':
            raise RuntimeError(f'{source}: expected effective -Os, got {optimization}')
        lto = [arg for arg in args if arg == '-fno-lto' or arg.startswith('-flto')]
        if not lto or lto[-1] != '-fno-lto':
            raise RuntimeError(f'{source}: expected effective -fno-lto')
        if source.suffix in ('.cpp', '.cc', '.cxx'):
            standards = [arg for arg in args if arg.startswith('-std=')]
            if not standards or standards[-1] not in ('-std=c++17', '-std=gnu++17'):
                raise RuntimeError(f'{source}: expected C++17, got {standards}')
            for feature in ('exceptions', 'rtti'):
                flags = [arg for arg in args if arg in ('-f' + feature, '-fno-' + feature)]
                if not flags or flags[-1] != '-fno-' + feature:
                    raise RuntimeError(f'{source}: expected disabled {feature}')
        if backend == 'nanopb' and source.name in ('pb_common.c', 'pb_encode.c', 'pb_decode.c', 'main.cpp'):
            for flag in ('-DPB_BUFFER_ONLY', '-DPB_VALIDATE_UTF8'):
                if flag not in args:
                    raise RuntimeError(f'{source}: missing {flag}')
    required = {'main.cpp', 'wire.cpp'} if backend == 'roo' else {
        'main.cpp', 'pb_common.c', 'pb_encode.c', 'pb_decode.c'}
    if not required <= seen:
        raise RuntimeError(f'Compilation database missing {required - seen}')
    return len(entries)
