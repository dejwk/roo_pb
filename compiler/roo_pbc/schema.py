"""Original protobuf lexer, parser and schema resolver. No protobuf dependencies."""
from dataclasses import dataclass, field
from pathlib import Path
import re
import os
import tomllib

SCALARS = {'double', 'float', 'int32', 'int64', 'uint32', 'uint64', 'sint32',
           'sint64', 'fixed32', 'fixed64', 'sfixed32', 'sfixed64', 'bool', 'string', 'bytes'}
TOKEN = re.compile(r'''\s+|//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|(?:0[xX][0-9a-fA-F]+|(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)|[{}\[\]();=<>.,:+-]''')


class SchemaError(ValueError):
    """A source-located schema or configuration error."""


@dataclass
class Field:
    name: str
    number: int
    type: str
    label: str = ''
    options: dict = field(default_factory=dict)
    oneof: str = ''
    map: bool = False
    storage: dict = field(default_factory=dict)
    resolved: object = None


@dataclass
class Enum:
    name: str
    full: str
    values: list = field(default_factory=list)
    options: dict = field(default_factory=dict)
    source: object = None
    reserved_numbers: list = field(default_factory=list)
    reserved_names: set = field(default_factory=set)


@dataclass
class Message:
    name: str
    full: str
    fields: list = field(default_factory=list)
    nested: list = field(default_factory=list)
    oneofs: list = field(default_factory=list)
    reserved_numbers: list = field(default_factory=list)
    reserved_names: set = field(default_factory=set)
    extensions: list = field(default_factory=list)
    retain_unknown: bool = False
    source: object = None


@dataclass
class Source:
    path: Path
    logical: str
    syntax: str = 'proto2'
    package: str = ''
    imports: list = field(default_factory=list)
    definitions: list = field(default_factory=list)
    extends: list = field(default_factory=list)


def integer(text):
    """Parse protobuf decimal, octal or hexadecimal integer literals."""
    sign = -1 if text.startswith('-') else 1
    text = text.lstrip('+-')
    base = 16 if text.lower().startswith('0x') else 8 if len(text) > 1 and text[0] == '0' else 10
    return sign * int(text, base)


class ProtoLiteral(str):
    """Retains wire bytes independently of the source's Unicode spelling."""
    def __new__(cls, wire):
        result = super().__new__(cls, wire.decode('utf-8', errors='surrogateescape'))
        result.wire = wire
        return result

    def __add__(self, other):
        return ProtoLiteral(self.wire + other.wire)


def literal(text):
    """Decode protobuf quoted strings while retaining escaped binary bytes."""
    content = text[1:-1]
    result = bytearray()
    escapes = {'a': 7, 'b': 8, 'f': 12, 'n': 10, 'r': 13, 't': 9, 'v': 11,
               '\\': 92, '?': 63, "'": 39, '"': 34}
    i = 0
    while i < len(content):
        ch = content[i]
        i += 1
        if ch != '\\':
            if ch in '\r\n':
                raise SchemaError('unescaped newline in string literal')
            result.extend(ch.encode('utf-8'))
            continue
        if i == len(content):
            raise SchemaError('unterminated string escape')
        ch = content[i]
        i += 1
        if ch in escapes:
            result.append(escapes[ch])
        elif ch in '01234567':
            digits = ch
            while len(digits) < 3 and i < len(content) and content[i] in '01234567':
                digits += content[i]
                i += 1
            value = int(digits, 8)
            if value > 255:
                raise SchemaError('octal escape exceeds byte range')
            result.append(value)
        elif ch in 'xX':
            digits = ''
            while len(digits) < 2 and i < len(content) and content[i] in '0123456789abcdefABCDEF':
                digits += content[i]
                i += 1
            if not digits:
                raise SchemaError('empty hexadecimal escape')
            result.append(int(digits, 16))
        else:
            raise SchemaError('unsupported string escape: ' + ch)
    return ProtoLiteral(bytes(result))


class Parser:
    def __init__(self, path, logical):
        self.source = Source(path, logical)
        self.text = path.read_text(encoding='utf-8')
        self.tokens = []
        offset = 0
        for match in TOKEN.finditer(self.text):
            if match.start() != offset:
                self.error_at(offset, 'invalid token')
            token = match.group()
            if not token.isspace() and not token.startswith(('//', '/*')):
                self.tokens.append((token, offset))
            offset = match.end()
        if offset != len(self.text):
            self.error_at(offset, 'invalid or unterminated token')
        self.tokens.append(('<eof>', len(self.text)))
        self.index = 0

    def error_at(self, offset, message):
        line = self.text.count('\n', 0, offset) + 1
        column = offset - self.text.rfind('\n', 0, offset)
        raise SchemaError(f'{self.source.path}:{line}:{column}: {message}')

    def error(self, message):
        self.error_at(self.tokens[self.index][1], message)

    def peek(self):
        return self.tokens[self.index][0]

    def take(self):
        result = self.peek()
        if result == '<eof>':
            self.error('unexpected end of file')
        self.index += 1
        return result

    def accept(self, token):
        if self.peek() != token:
            return False
        self.index += 1
        return True

    def expect(self, token):
        if not self.accept(token):
            self.error(f'expected {token!r}, got {self.peek()!r}')

    def name(self):
        value = self.take()
        if not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', value):
            self.error('expected identifier')
        return value

    def qualified(self):
        prefix = '.' if self.accept('.') else ''
        parts = [self.name()]
        while self.accept('.'):
            parts.append(self.name())
        return prefix + '.'.join(parts)

    def value(self):
        sign = self.take() if self.peek() in ('+', '-') else ''
        value = self.take()
        if value.startswith(('"', "'")):
            result = literal(value)
            while self.peek().startswith(('"', "'")):
                result += literal(self.take())
            return result
        while self.accept('.'):
            value += '.' + self.name()
        return sign + value

    def option(self):
        if self.accept('('):
            name = '(' + self.qualified() + ')'
            self.expect(')')
            if self.accept('.'):
                name += '.' + self.qualified()
        else:
            name = self.qualified()
        self.expect('=')
        if self.peek() == '{':
            self.error('aggregate custom options are unsupported; use a TOML sidecar')
        return name, self.value()

    def options(self):
        result = {}
        if self.accept('['):
            while True:
                key, value = self.option()
                if key in result:
                    self.error('duplicate option ' + key)
                result[key] = value
                if not self.accept(','):
                    break
            self.expect(']')
        return result

    def ranges(self):
        numbers, names = [], set()
        while True:
            if self.peek().startswith(('"', "'")):
                names.add(self.value())
            else:
                start = integer(self.value())
                end = start
                if self.accept('to'):
                    end = 0x1fffffff if self.accept('max') else integer(self.value())
                if start > end:
                    self.error('reversed range')
                numbers.append((start, end))
            if not self.accept(','):
                break
        self.expect(';')
        return numbers, names

    def parse_field(self, owner, oneof=''):
        label = self.take() if self.peek() in ('optional', 'required', 'repeated') else ''
        if oneof and label:
            self.error('oneof fields cannot have labels')
        type_name = self.qualified()
        if type_name == 'group':
            self.error('declared groups are unsupported (unknown wire groups can be skipped)')
        is_map = type_name == 'map'
        if is_map:
            if label or oneof:
                self.error('map cannot have a label or be in oneof')
            self.expect('<')
            key = self.qualified()
            self.expect(',')
            value_type = self.qualified()
            self.expect('>')
            if key not in SCALARS - {'float', 'double', 'bytes'}:
                self.error('invalid map key type')
        name = self.name()
        self.expect('=')
        number = integer(self.value())
        options = self.options()
        self.expect(';')
        if is_map:
            entry_name = name[0].upper() + name[1:] + 'Entry'
            entry = Message(entry_name, owner.full + '.' + entry_name,
                            fields=[Field('key', 1, key, 'optional' if self.source.syntax == 'proto2' else ''), Field('value', 2, value_type, 'optional' if self.source.syntax == 'proto2' else '')],
                            source=self.source)
            owner.nested.append(entry)
            self.source.definitions.append(entry)
            type_name = '.' + entry.full
            label = 'repeated'
        return Field(name, number, type_name, label, options, oneof, is_map)

    def enum(self, scope):
        name = self.name()
        result = Enum(name, '.'.join(filter(None, [scope, name])), source=self.source)
        self.source.definitions.append(result)
        self.expect('{')
        while not self.accept('}'):
            if self.accept(';'):
                continue
            if self.accept('option'):
                key, value = self.option()
                result.options[key] = value
                self.expect(';')
            elif self.accept('reserved'):
                numbers, names = self.ranges()
                result.reserved_numbers += numbers
                result.reserved_names |= names
            else:
                name = self.name()
                self.expect('=')
                value = self.value()
                self.options()
                self.expect(';')
                result.values.append((name, integer(value)))
        return result

    def message(self, scope):
        name = self.name()
        result = Message(name, '.'.join(filter(None, [scope, name])), source=self.source)
        self.source.definitions.append(result)
        self.expect('{')
        while not self.accept('}'):
            if self.accept(';'):
                continue
            if self.accept('message'):
                result.nested.append(self.message(result.full))
            elif self.accept('enum'):
                result.nested.append(self.enum(result.full))
            elif self.accept('oneof'):
                group = self.name()
                result.oneofs.append(group)
                self.expect('{')
                while not self.accept('}'):
                    result.fields.append(self.parse_field(result, group))
            elif self.accept('reserved'):
                numbers, names = self.ranges()
                result.reserved_numbers += numbers
                result.reserved_names |= names
            elif self.accept('extensions'):
                numbers, names = self.ranges()
                if names:
                    self.error('extension range must be numeric')
                result.extensions += numbers
            elif self.accept('extend'):
                self.extend(result.full)
            elif self.accept('option'):
                self.option()
                self.expect(';')
            else:
                result.fields.append(self.parse_field(result))
        return result

    def extend(self, scope):
        target = self.qualified()
        self.expect('{')
        owner = Message('', scope, source=self.source)
        while not self.accept('}'):
            self.source.extends.append((scope, target, self.parse_field(owner)))

    def parse(self):
        seen_syntax = False
        seen_package = False
        while self.peek() != '<eof>':
            if self.accept(';'):
                continue
            if self.accept('syntax'):
                if seen_syntax or self.source.definitions or self.source.imports or seen_package:
                    self.error('syntax must appear once before other declarations')
                seen_syntax = True
                self.expect('=')
                self.source.syntax = self.value()
                self.expect(';')
                if self.source.syntax not in ('proto2', 'proto3'):
                    self.error('supported syntaxes are proto2 and proto3')
            elif self.accept('package'):
                if seen_package or self.source.definitions:
                    self.error('package must appear once before type declarations')
                seen_package = True
                self.source.package = self.qualified()
                if self.source.package.startswith('.'):
                    self.error('package cannot start with a dot')
                self.expect(';')
            elif self.accept('import'):
                mode = self.take() if self.peek() in ('public', 'weak') else ''
                self.source.imports.append((self.value(), mode))
                self.expect(';')
            elif self.accept('message'):
                self.message(self.source.package)
            elif self.accept('enum'):
                self.enum(self.source.package)
            elif self.accept('extend'):
                self.extend(self.source.package)
            elif self.accept('option'):
                self.option()
                self.expect(';')
            else:
                self.error('unsupported declaration ' + self.peek())
        return self.source


class Schema:
    def __init__(self, include_paths):
        self.includes = [Path(os.path.abspath(p)) for p in include_paths]
        self.sources = {}
        self.symbols = {}
        self.options = {}
        self.message_options = {}
        self.extensions = []

    def load(self, filename):
        if not filename.endswith('.proto'):
            raise SchemaError('schema filename must end in .proto: ' + filename)
        candidates = [p / filename for p in self.includes]
        if Path(filename).is_absolute():
            candidates = [Path(filename)]
        path = next((Path(os.path.abspath(p)) for p in candidates if p.is_file()), None)
        if path is None:
            raise SchemaError(f'import not found: {filename}')
        logical = None
        for root in self.includes:
            if path.is_relative_to(root):
                logical = path.relative_to(root).as_posix()
                break
        if logical is None:
            raise SchemaError(f'{path}: source is outside include paths')
        if logical in self.sources:
            if self.sources[logical] is None:
                raise SchemaError(f'{path}: cyclic import')
            return self.sources[logical]
        self.sources[logical] = None
        source = Parser(path, logical).parse()
        for imported, _ in source.imports:
            self.load(imported)
        self.sources[logical] = source
        sidecar = path.with_suffix('.roo_pb.toml')
        if sidecar.exists():
            config = tomllib.loads(sidecar.read_text())
            if set(config) - {'fields', 'messages'}:
                raise SchemaError(f'{sidecar}: only [fields] and [messages] are supported')
            self.options.update(config.get('fields', {}))
            self.message_options.update(config.get('messages', {}))
        for definition in source.definitions:
            if definition.full in self.symbols:
                raise SchemaError('duplicate declaration: ' + definition.full)
            self.symbols[definition.full] = definition
        return source

    def resolve(self, name, scope):
        if name in SCALARS:
            return name
        if name.startswith('.'):
            result = self.symbols.get(name[1:])
            if result is not None:
                return result
        else:
            parts = scope.split('.')
            while parts:
                result = self.symbols.get('.'.join(parts + [name]))
                if result is not None:
                    return result
                parts.pop()
            if name in self.symbols:
                return self.symbols[name]
        raise SchemaError(f'{scope}: unknown type {name}')

    def validate_field(self, owner, value, extension=False):
        full = owner.full + '.' + value.name
        if not 1 <= value.number <= 0x1fffffff or 19000 <= value.number <= 19999:
            raise SchemaError(full + ': invalid/reserved field number')
        if owner.source.syntax == 'proto2' and not value.label and not value.oneof:
            raise SchemaError(full + ': proto2 fields require a label')
        if owner.source.syntax == 'proto3' and value.label == 'required':
            raise SchemaError(full + ': required is proto2 only')
        if 'default' in value.options and (owner.source.syntax != 'proto2' or value.label == 'repeated' or value.oneof):
            raise SchemaError(full + ': default requires a singular proto2 field')
        value.resolved = self.resolve(value.type, owner.full)
        value.storage = dict(self.options.get(full, {}))
        valid = {'storage', 'max_bytes', 'max_count', 'fixed_length', 'fixed_count', 'integer_bits'}
        if set(value.storage) - valid:
            raise SchemaError(full + ': unknown storage option')
        mode = value.storage.get('storage', 'bounded')
        if extension and mode == 'callback':
            raise SchemaError(full + ': callback extensions use manual ExtensionBinding handlers')
        if extension and value.label == 'required':
            raise SchemaError(full + ': extensions cannot be required')
        if mode not in ('bounded', 'dynamic', 'callback'):
            raise SchemaError(full + ': invalid storage mode')
        for key in ('max_bytes', 'max_count'):
            if key in value.storage and (type(value.storage[key]) is not int or value.storage[key] < 0):
                raise SchemaError(full + ': capacity must be a nonnegative integer')
        if mode != 'bounded' and set(value.storage) & {'max_bytes', 'max_count', 'fixed_length', 'fixed_count'}:
            raise SchemaError(full + ': capacity/fixed options require bounded storage')
        for key in ('fixed_length', 'fixed_count'):
            if key in value.storage and type(value.storage[key]) is not bool:
                raise SchemaError(full + ': fixed options must be boolean')
        if mode == 'bounded':
            if value.type in ('string', 'bytes') and 'max_bytes' not in value.storage:
                raise SchemaError(full + ': specify max_bytes or dynamic/callback storage')
            if value.label == 'repeated' and 'max_count' not in value.storage:
                raise SchemaError(full + ': specify max_count or dynamic/callback storage')
        if value.storage.get('fixed_length') and (value.type != 'bytes' or 'max_bytes' not in value.storage):
            raise SchemaError(full + ': fixed_length requires bytes and max_bytes')
        if value.storage.get('fixed_count') and (value.label != 'repeated' or 'max_count' not in value.storage):
            raise SchemaError(full + ': fixed_count requires repeated and max_count')
        bits = value.storage.get('integer_bits')
        if bits is not None and (bits not in (8, 16, 32, 64) or value.type not in SCALARS - {'bool', 'string', 'bytes', 'float', 'double'}):
            raise SchemaError(full + ': invalid integer_bits')
        if bits is not None and '32' in value.type and bits > 32:
            raise SchemaError(full + ': integer_bits cannot widen the schema type')
        if isinstance(value.resolved, Message) and 'default' in value.options:
            raise SchemaError(full + ': message defaults are not allowed')
        if value.options.get('packed') not in (None, 'true', 'false'):
            raise SchemaError(full + ': invalid packed option')
        if 'packed' in value.options and (value.label != 'repeated' or isinstance(value.resolved, Message) or value.type in ('string', 'bytes')):
            raise SchemaError(full + ': packed requires repeated scalar or enum')

    def validate(self):
        used = set()
        # Reject name collisions introduced by C++ flattening and accessor APIs.
        from .generate import cpp_name, ident, camel
        emitted_names = set()
        for definition in self.symbols.values():
            emitted = cpp_name(definition)
            if emitted in emitted_names:
                raise SchemaError(definition.full + ': generated C++ name collision')
            emitted_names.add(emitted)
            if isinstance(definition, Message):
                api = {'Clear', 'IsInitialized', 'ParseFromArray', 'SerializeToArray',
                       'ByteSizeLong', 'mergeFrom', 'serialize',
                       'set_unknown_fields_encoder', 'set_unknown_fields_decoder',
                       'unknown_fields_encoder_', 'unknown_fields_decoder_', definition.name}
                for value in definition.fields:
                    n = ident(value.name)
                    names = {n, n + '_', 'set_' + n, 'clear_' + n, 'has_' + n,
                             'has_' + n + '_', 'mutable_' + n, 'try_set_' + n,
                             'add_' + n, 'try_add_' + n, n + '_size'}
                    if self.options.get(definition.full + '.' + value.name, {}).get('storage') == 'callback':
                        names |= {n + '_encoder', n + '_decoder',
                                  n + '_encoder_', n + '_decoder_',
                                  'set_' + n + '_encoder', 'set_' + n + '_decoder'}
                    if names & api:
                        raise SchemaError(definition.full + '.' + value.name + ': generated accessor collision')
                    api |= names
                for group in definition.oneofs:
                    names = {ident(group) + '_case', ident(group) + '_case_', 'clear_' + ident(group), camel(group) + 'Case'}
                    if names & api:
                        raise SchemaError(definition.full + ': oneof accessor collision')
                    api |= names
        # Only own declarations, imports and publicly re-exported imports are visible.
        def visible(source):
            result = {source.logical}
            def add(name):
                if name in result:
                    return
                result.add(name)
                for dependency, mode in self.sources[name].imports:
                    if mode == 'public':
                        add(dependency)
            for name, _ in source.imports:
                add(name)
            return result
        for definition in self.symbols.values():
            if isinstance(definition, Message):
                allowed = visible(definition.source)
                for value in definition.fields:
                    resolved = self.resolve(value.type, definition.full)
                    if isinstance(resolved, (Message, Enum)) and resolved.source.logical not in allowed:
                        raise SchemaError(definition.full + ': type is not imported: ' + value.type)
        for definition in self.symbols.values():
            if isinstance(definition, Enum):
                if not definition.values:
                    raise SchemaError(definition.full + ': empty enum')
                names = [n for n, _ in definition.values]
                values = [v for _, v in definition.values]
                if len(set(names)) != len(names):
                    raise SchemaError(definition.full + ': duplicate enum name')
                if len(set(values)) != len(values) and definition.options.get('allow_alias') != 'true':
                    raise SchemaError(definition.full + ': duplicate enum value requires allow_alias')
                if any(n in definition.reserved_names or any(a <= v <= b for a, b in definition.reserved_numbers) for n, v in definition.values):
                    raise SchemaError(definition.full + ': reserved enum name/value')
                if any(v < -2**31 or v >= 2**31 for v in values):
                    raise SchemaError(definition.full + ': enum value exceeds int32')
                if definition.source.syntax == 'proto3' and values[0] != 0:
                    raise SchemaError(definition.full + ': first proto3 enum value must be zero')
                continue
            config = self.message_options.get(definition.full, {})
            if set(config) - {'unknown_fields'} or type(config.get('unknown_fields', False)) is not bool:
                raise SchemaError(definition.full + ': invalid message options')
            definition.retain_unknown = config.get('unknown_fields', False) or bool(definition.extensions)
            names, numbers = set(), set()
            if definition.extensions and definition.source.syntax != 'proto2':
                raise SchemaError(definition.full + ': extensions require proto2')
            for value in definition.fields:
                full = definition.full + '.' + value.name
                used.add(full)
                if value.name in names or value.number in numbers:
                    raise SchemaError(full + ': duplicate field')
                if value.name in definition.reserved_names or any(a <= value.number <= b for a, b in definition.reserved_numbers + definition.extensions):
                    raise SchemaError(full + ': field overlaps reserved/extension range')
                names.add(value.name)
                numbers.add(value.number)
                self.validate_field(definition, value)
        extension_ids = set()
        for source in self.sources.values():
            for scope, target, value in source.extends:
                owner = self.resolve(target, scope)
                if not isinstance(owner, Message) or source.syntax != 'proto2':
                    raise SchemaError(target + ': extension requires a proto2 message')
                if not any(a <= value.number <= b for a, b in owner.extensions):
                    raise SchemaError(target + ': extension outside declared range')
                identity = (owner.full, value.number)
                if identity in extension_ids:
                    raise SchemaError(target + ': duplicate extension number')
                extension_ids.add(identity)
                temporary = Message('', scope, source=source)
                self.validate_field(temporary, value, True)
                used.add(scope + '.' + value.name)
                self.extensions.append((source, owner, value))
        unknown_messages = set(self.message_options) - {d.full for d in self.symbols.values() if isinstance(d, Message)}
        if unknown_messages:
            raise SchemaError('unknown message options: ' + ', '.join(sorted(unknown_messages)))
        unknown = set(self.options) - used
        if unknown:
            raise SchemaError('storage options refer to unknown fields: ' + ', '.join(sorted(unknown)))
        # Nested aliases need complete declarations; reject recursive ownership.
        active, done = set(), set()
        def visit(message):
            if message.full in active:
                raise SchemaError(message.full + ': recursive resident messages require callback storage')
            if message.full in done:
                return
            active.add(message.full)
            for value in message.fields:
                if isinstance(value.resolved, Message) and value.storage.get('storage') != 'callback':
                    visit(value.resolved)
            active.remove(message.full)
            done.add(message.full)
        for definition in self.symbols.values():
            if isinstance(definition, Message):
                visit(definition)
