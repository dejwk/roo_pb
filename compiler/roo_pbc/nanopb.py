"""Translate the supported nanopb annotation subset into roo storage options."""
from .schema import Enum, ProtoLiteral, SchemaError, integer, NANOPB_OPTIONS

CAPACITIES = {'max_size', 'max_length', 'max_count'}
FIXED = {'fixed_length', 'fixed_count'}
INTEGERS = {'int32', 'int64', 'uint32', 'uint64', 'sint32', 'sint64',
            'fixed32', 'fixed64', 'sfixed32', 'sfixed64'}


def options(raw, prefix, location, imported):
    """Validate explicit annotations even if a narrower scope overrides them."""
    result = {}
    for name, value in raw.items():
        root, _, key = name.partition('.')
        if root not in NANOPB_OPTIONS:
            continue
        if not imported:
            raise SchemaError(location + ': nanopb options require import "nanopb.proto"')
        if root != prefix or prefix == '(nanopb_enumopt)':
            raise SchemaError(location + ': unsupported nanopb option scope: ' + name)
        if not key and value == {}:
            continue
        if key not in CAPACITIES | FIXED | {'int_size', 'type'}:
            raise SchemaError(location + ': unsupported nanopb option: ' + name)
        if isinstance(value, ProtoLiteral):
            raise SchemaError(location + ': invalid nanopb option value: ' + name)
        if key in CAPACITIES:
            try:
                number = integer(value)
            except ValueError:
                raise SchemaError(location + ': nanopb ' + key + ' requires an integer') from None
            if not 0 <= number <= 0x7fffffff:
                raise SchemaError(location + ': nanopb ' + key + ' requires a nonnegative int32')
            result[key] = number
        elif key in FIXED:
            if value not in ('true', 'false'):
                raise SchemaError(location + ': nanopb ' + key + ' requires a boolean')
            result[key] = value == 'true'
        elif key == 'int_size':
            sizes = {'IS_DEFAULT': None, 'IS_8': 8, 'IS_16': 16, 'IS_32': 32, 'IS_64': 64}
            if value not in sizes:
                raise SchemaError(location + ': unsupported nanopb int_size: ' + value)
            result[key] = sizes[value]
        elif key == 'type':
            if value not in ('FT_STATIC', 'FT_CALLBACK'):
                raise SchemaError(location + ': unsupported nanopb type: ' + value +
                                  '; use FT_STATIC, FT_CALLBACK, or a roo TOML storage setting')
            result[key] = value
    return result


def field_storage(value, inherited, explicit, sidecar, location):
    """Apply applicable defaults and let TOML override the translated settings."""
    merged = dict(inherited, **explicit)
    applicable = {'type'}
    if value.type in ('string', 'bytes'):
        applicable.add('max_size')
    if value.type == 'string':
        applicable.add('max_length')
    if value.type == 'bytes':
        applicable.add('fixed_length')
    if value.label == 'repeated':
        applicable.update(('max_count', 'fixed_count'))
    if value.type in INTEGERS:
        applicable.add('int_size')
    invalid = set(explicit) - applicable
    if invalid:
        raise SchemaError(location + ': nanopb option does not apply to this field: ' + ', '.join(sorted(invalid)))
    merged = {k: v for k, v in merged.items() if k in applicable}
    result = {}
    if 'type' in merged:
        result['storage'] = 'callback' if merged['type'] == 'FT_CALLBACK' else 'bounded'
    mode = sidecar.get('storage', result.get('storage', 'bounded'))
    # Nanopb allocation bounds are not runtime constraints for callback fields.
    # Explicit TOML bounds still undergo the usual roo validation below.
    if mode == 'bounded':
        if value.type == 'string' and 'max_length' in merged:
            result['max_bytes'] = merged['max_length']
        elif 'max_size' in merged:
            result['max_bytes'] = merged['max_size']
            if value.type == 'string':
                result['max_bytes'] -= 1
        if 'max_count' in merged:
            result['max_count'] = merged['max_count']
    for key in FIXED:
        if merged.get(key):
            result[key] = True
    if merged.get('int_size') is not None:
        result['integer_bits'] = merged['int_size']
    result.update(sidecar)
    return result


def storage_options(schema):
    """Resolve file, message, and field defaults before validation."""
    effective = dict(schema.options)
    file_defaults = {}

    def has_import(source, public_only=False):
        return any((not public_only or mode == 'public') and
                   (name == 'nanopb.proto' or has_import(schema.sources[name], True))
                   for name, mode in source.imports)

    for source in schema.sources.values():
        file_defaults[source.logical] = options(source.options, '(nanopb_fileopt)',
                                                source.logical, has_import(source))

    def defaults(message):
        # Nanopb treats nested messages independently of their lexical parent.
        inherited = file_defaults[message.source.logical]
        own = options(message.options, '(nanopb_msgopt)', message.full, has_import(message.source))
        return dict(inherited, **own)

    def apply(source, scope, value, inherited):
        location = scope + '.' + value.name
        own = options(value.options, '(nanopb)', location, has_import(source))
        effective[location] = field_storage(value, inherited, own,
                                            schema.options.get(location, {}), location)

    for definition in schema.symbols.values():
        if isinstance(definition, Enum):
            options(definition.options, '(nanopb_enumopt)', definition.full, has_import(definition.source))
        else:
            inherited = defaults(definition)
            for value in definition.fields:
                apply(definition.source, definition.full, value, inherited)
    for source in schema.sources.values():
        for scope, _, value in source.extends:
            inherited = file_defaults[source.logical]
            apply(source, scope, value, inherited)
    return effective
