"""Command-line entry point for standalone schema generation."""
import argparse
from pathlib import Path
import sys
from .schema import Schema, SchemaError
from .generate import generate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('schemas', nargs='+')
    parser.add_argument('-I', '--proto-path', action='append', default=[])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--depfile', type=Path)
    args = parser.parse_args()
    try:
        schema = Schema(args.proto_path or ['.'])
        for name in args.schemas:
            schema.load(name)
        schema.validate()
        # Validate generation for the complete graph before writing any files.
        outputs = {source.logical[:-6] + '.pb.h': generate(schema, source)
                   for source in schema.sources.values()}
        for name, contents in outputs.items():
            path = args.out / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        if args.depfile:
            def escape(path):
                return str(path).replace('\\', '\\\\').replace(' ', '\\ ').replace('#', '\\#').replace('$', '$$')
            inputs = []
            for source in schema.sources.values():
                inputs.append(source.path)
                sidecar = source.path.with_suffix('.roo_pb.toml')
                if sidecar.exists():
                    inputs.append(sidecar)
            args.depfile.write_text(' '.join(escape(args.out / n) for n in outputs) + ': ' +
                                    ' '.join(escape(p) for p in inputs) + '\n')
    except (SchemaError, ValueError, OSError, KeyError) as error:
        parser.exit(1, f'roo_pbc: {error}\n')


if __name__ == '__main__':
    main()
