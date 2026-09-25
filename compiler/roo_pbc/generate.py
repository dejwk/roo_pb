"""Generate readable C++ value classes from validated roo_pb schemas."""
import json
import re
from .schema import Enum, Message, SchemaError, integer, ProtoLiteral

CPP_TYPES = {
    'double': 'double', 'float': 'float', 'int32': 'int32_t', 'int64': 'int64_t',
    'uint32': 'uint32_t', 'uint64': 'uint64_t', 'sint32': 'int32_t', 'sint64': 'int64_t',
    'fixed32': 'uint32_t', 'fixed64': 'uint64_t', 'sfixed32': 'int32_t', 'sfixed64': 'int64_t', 'bool': 'bool',
}
KEYWORDS = set('alignas alignof and asm auto bitand bitor bool break case catch char class compl concept const constexpr consteval constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor'.split())


def ident(name):
    return name + '_' if name in KEYWORDS else name


def camel(name):
    return ''.join(part[:1].upper() + part[1:] for part in name.split('_'))


def local_name(definition):
    prefix = definition.source.package
    relative = definition.full[len(prefix) + 1:] if prefix else definition.full
    return '_'.join(ident(p) for p in relative.split('.'))


def cpp_name(definition):
    package = definition.source.package
    return '::' + '::'.join([ident(p) for p in package.split('.') if p] + [local_name(definition)])


def kind(value):
    if isinstance(value.resolved, Message):
        return 'kMessage'
    if isinstance(value.resolved, Enum):
        return 'kEnum'
    return 'k' + value.type[0].upper() + value.type[1:]


def type_name(value):
    if isinstance(value.resolved, (Message, Enum)):
        return cpp_name(value.resolved)
    if value.type in ('string', 'bytes'):
        if value.storage.get('storage') == 'dynamic':
            return '::roo_pb::DynamicString'
        return f'::roo_pb::BoundedString<{value.storage["max_bytes"]}>'
    result = CPP_TYPES[value.type]
    if 'integer_bits' in value.storage:
        result = ('uint' if result.startswith('u') else 'int') + str(value.storage['integer_bits']) + '_t'
    return result


def container(value):
    element = type_name(value)
    if value.storage.get('storage') == 'dynamic':
        return f'::roo_pb::DynamicArray<{element}>'
    return f'::roo_pb::BoundedArray<{element}, {value.storage["max_count"]}>'


def callback(value):
    return value.storage.get('storage') == 'callback'


def presence(owner, value):
    return value.label != 'repeated' and (owner.source.syntax == 'proto2' or value.label == 'optional' or value.oneof or isinstance(value.resolved, Message))


def string_literal(value):
    # Byte escapes have fixed width so following digits cannot join an escape.
    return '"' + ''.join('\\%03o' % b for b in value) + '"'


def default(value):
    specified = value.options.get('default')
    if isinstance(value.resolved, Message):
        return '{}'
    if isinstance(value.resolved, Enum):
        name = specified if specified is not None else value.resolved.values[0][0]
        if name not in dict(value.resolved.values):
            raise SchemaError(value.name + ': unknown enum default ' + name)
        return type_name(value) + '::' + ident(name)
    if value.type in ('string', 'bytes'):
        text = specified or ''
        encoded = text.wire if isinstance(text, ProtoLiteral) else text.encode('utf-8')
        if value.type == 'string':
            try:
                encoded.decode('utf-8')
            except UnicodeDecodeError as error:
                raise SchemaError(value.name + ': string default is not UTF-8') from error
        if value.storage.get('storage', 'bounded') == 'bounded' and len(encoded) > value.storage['max_bytes']:
            raise SchemaError(value.name + ': default exceeds capacity')
        return string_literal(encoded) + ', ' + str(len(encoded))
    if value.type == 'bool':
        if specified is not None and specified not in ('true', 'false'):
            raise SchemaError(value.name + ': invalid bool default')
        return specified or 'false'
    if value.type in ('float', 'double'):
        text = specified or '0'
        if text in ('inf', '+inf', '-inf', 'nan'):
            op = 'quiet_NaN()' if text == 'nan' else 'infinity()'
            return ('-' if text == '-inf' else '') + f'std::numeric_limits<{value.type}>::{op}'
        try:
            float(text)
        except ValueError as error:
            raise SchemaError(value.name + ': invalid floating default') from error
        return f'static_cast<{value.type}>({text})'
    number = integer(specified) if specified is not None else 0
    bits = int(re.search(r'(\d+)', type_name(value)).group(1))
    signed = not type_name(value).startswith('u')
    low = -(1 << (bits - 1)) if signed else 0
    high = (1 << (bits - (1 if signed else 0))) - 1
    if not low <= number <= high:
        raise SchemaError(value.name + ': default exceeds storage range')
    if number == -(1 << 63):
        return 'INT64_MIN'
    return str(number) + ('ULL' if number > (1 << 63) - 1 else 'LL' if abs(number) > (1 << 31) - 1 else '')


def maximum_size(owner, active=None):
    """Return a conservative finite bound, or None for externally sized data."""
    active = set() if active is None else active
    if owner.full in active or owner.retain_unknown:
        return None
    active = active | {owner.full}
    def varint_size(n):
        return max(1, (n.bit_length() + 6) // 7)
    total = 0
    for value in owner.fields:
        if callback(value) or value.storage.get('storage') == 'dynamic':
            return None
        if isinstance(value.resolved, Message):
            payload = maximum_size(value.resolved, active)
            if payload is None:
                return None
            size = varint_size(payload) + payload
        elif value.type in ('string', 'bytes'):
            payload = value.storage['max_bytes']
            size = varint_size(payload) + payload
        else:
            size = {'bool': 1, 'uint32': 5, 'sint32': 5, 'float': 4,
                    'fixed32': 4, 'sfixed32': 4, 'double': 8,
                    'fixed64': 8, 'sfixed64': 8}.get(value.type, 10)
        tag = varint_size(value.number << 3)
        count = value.storage['max_count'] if value.label == 'repeated' else 1
        # The unpacked bound covers normal emission and is conservative for
        # short packed fields except the additional length prefix.
        if value.label == 'repeated' and value.type not in ('string', 'bytes') and not isinstance(value.resolved, Message):
            total += max((tag + size) * count, tag + varint_size(size * count) + size * count)
        else:
            total += (tag + size) * count
    return total


class Emitter:
    def __init__(self):
        self.lines = []

    def line(self, value=''):
        self.lines.append(value)

    def method(self, comment, declaration, body=None):
        self.line('/// ' + comment)
        self.line(declaration + (' {' if body is not None else ''))
        if body is not None:
            self.lines.extend(body)
            self.line('}')
        self.line()

    def text(self):
        return '\n'.join(self.lines) + '\n'


def emit_enum(e, definition):
    e.line('/// Schema enum; proto3 permits unknown numeric values.')
    e.line(f'enum class {local_name(definition)} : int32_t {{')
    for name, value in definition.values:
        e.line(f'{ident(name)} = {value},')
    e.line('};\n')


def emit_accessors(e, owner, value):
    name = ident(value.name)
    member = name + '_'
    if callback(value):
        activate = []
        if value.oneof:
            group = ident(value.oneof)
            activate = [f'if ({group}_case_ != {value.number}) clear_{group}();', f'{group}_case_ = {value.number};']
        elif presence(owner, value):
            activate = [f'has_{name}_ = true;']
        e.method('Returns the borrowed encoding binding.', f'const ::roo_pb::EncodeCallback& {name}_encoder() const', [f'return {name}_encoder_;'])
        e.method('Returns the borrowed decoding binding.', f'const ::roo_pb::DecodeCallback& {name}_decoder() const', [f'return {name}_decoder_;'])
        e.method('Binds an encoder and marks explicit presence; state remains borrowed.', f'void set_{name}_encoder(::roo_pb::EncodeCallback binding)', [*activate, f'{name}_encoder_ = binding;'])
        e.method('Binds a decoder without changing presence or the selected oneof.', f'void set_{name}_decoder(::roo_pb::DecodeCallback binding)', [f'{name}_decoder_ = binding;'])
        clear = []
        if value.oneof:
            check = f'{ident(value.oneof)}_case_ == {value.number}'
            clear = [f'if ({check}) {ident(value.oneof)}_case_ = 0;']
        elif presence(owner, value):
            check = f'has_{name}_'
            clear = [f'has_{name}_ = false;']
        if presence(owner, value):
            e.method('Reports explicit callback field presence.', f'bool has_{name}() const', [f'return {check};'])
        e.method('Clears presence while retaining both borrowed bindings.', f'void clear_{name}()', clear)
        return
    t = type_name(value)
    repeated = value.label == 'repeated'
    message = isinstance(value.resolved, Message)
    text = value.type in ('string', 'bytes')
    activate = []
    if value.oneof:
        group = ident(value.oneof)
        initial = '' if message or text else default(value)
        activate = [f'if ({group}_case_ != {value.number}) {{',
                    f'clear_{group}();',
                    f'::new (static_cast<void*>(&{member})) {t}{{{initial}}};',
                    f'{group}_case_ = {value.number};', '}']
    elif presence(owner, value):
        activate = [f'has_{name}_ = true;']
    if repeated:
        c = container(value)
        e.method('Returns the number of live elements.', f'size_t {name}_size() const', [f'return {member}.size();'])
        e.method('Returns a checked indexed element.', f'const {t}& {name}(size_t index) const', [f'return {member}[index];'])
        e.method('Returns the resident repeated container.', f'const {c}& {name}() const', [f'return {member};'])
        e.method('Returns mutable repeated storage; callers must respect bounds.', f'{c}* mutable_{name}()', [f'return &{member};'])
        e.method('Returns a mutable checked indexed element.', f'{t}* mutable_{name}(size_t index)', [f'return &{member}[index];'])
        e.method('Appends a default element or returns nullptr on capacity failure.', f'{t}* try_add_{name}()', [f'return {member}.add();'])
        e.method('Appends a default element; capacity is a release-checked precondition.', f'{t}* add_{name}()', [f'{t}* result = try_add_{name}();', '::roo_pb::Require(result != nullptr);', 'return result;'])
        e.method('Appends a value, reporting capacity failure.', f'bool try_add_{name}(const {t}& value)', [f'return {member}.push_back(value);'])
        e.method('Appends a value; capacity is a release-checked precondition.', f'void add_{name}(const {t}& value)', [f'::roo_pb::Require(try_add_{name}(value));'])
        e.method('Replaces an indexed element; the index must be valid.', f'void set_{name}(size_t index, const {t}& value)', [f'{member}[index] = value;'])
        if value.map:
            key = type_name(value.resolved.fields[0])
            val = type_name(value.resolved.fields[1])
            e.method('Finds a map value by key, returning nullptr when absent.', f'const {val}* find_{name}(const {key}& key) const', [f'for (const {t}& entry : {member}) {{ if (entry.key() == key) return &entry.value(); }}', 'return nullptr;'])
            e.method('Inserts or replaces a map entry, reporting capacity failure.', f'bool try_insert_{name}(const {t}& entry)', [f'for ({t}& current : {member}) {{ if (current.key() == entry.key()) {{ current = entry; return true; }} }}', f'return {member}.push_back(entry);'])
    else:
        read = [f'return {member};']
        if value.oneof:
            initial = '' if message or text else default(value)
            read = [f'if (has_{name}()) return {member};',
                    f'static const {t} default_value{{{initial}}};',
                    'return default_value;']
        e.method('Returns the current field value or its schema default.', f'const {t}& {name}() const', read)
        if presence(owner, value):
            check = f'{ident(value.oneof)}_case_ == {value.number}' if value.oneof else f'has_{name}_'
            e.method('Reports explicit field presence.', f'bool has_{name}() const', [f'return {check};'])
        if text:
            e.method('Assigns bytes without changing the field on capacity failure.', f'bool try_set_{name}(const char* value, size_t size)', [f'{t} temporary;', 'if (!temporary.assign(value, size)) return false;', *activate, f'{member} = std::move(temporary);', 'return true;'])
            e.method('Assigns a NUL-terminated string, reporting capacity failure.', f'bool try_set_{name}(const char* value)', [f'return value != nullptr && try_set_{name}(value, std::strlen(value));'])
            e.method('Assigns bytes; capacity is a release-checked precondition.', f'void set_{name}(const char* value, size_t size)', [f'::roo_pb::Require(try_set_{name}(value, size));'])
            e.method('Assigns a NUL-terminated string with checked capacity.', f'void set_{name}(const char* value)', [f'::roo_pb::Require(try_set_{name}(value));'])
        if message or text:
            e.method('Marks the field present and returns mutable owned storage.', f'{t}* mutable_{name}()', [*activate, f'return &{member};'])
        else:
            e.method('Assigns the field and updates explicit presence when applicable.', f'void set_{name}({t} value)', [*activate, f'{member} = value;'])
    body = []
    if repeated:
        body.append(f'{member}.clear();')
    elif message:
        body.append(f'{member}.Clear();')
    elif text:
        initial = default(value)
        body.append(f'{member}.clear();' if initial == '"", 0' else
                    f'::roo_pb::Require({member}.assign({initial}));')
    else:
        body.append(f'{member} = {default(value)};')
    if value.oneof:
        body = [f'if (has_{name}()) clear_{ident(value.oneof)}();']
    elif presence(owner, value):
        body.append(f'has_{name}_ = false;')
    e.method('Restores the schema default and clears presence or repeated contents.', f'void clear_{name}()', body)


def emit_read_one(e, owner, value, reader='input'):
    name = ident(value.name)
    member = name + '_'
    k = '::roo_pb::Kind::' + kind(value)
    t = type_name(value)
    closed = isinstance(value.resolved, Enum) and value.resolved.source.syntax == 'proto2'
    if value.label == 'repeated':
        e.line(f'{t} item{{}};')
        e.line(f'::roo_pb::ReadValue<{k}>({reader}, item);')
        e.line(f'if ({reader}.status() != ::roo_pb::Status::kOk) return {reader}.status();')
        if value.storage.get('fixed_length'):
            e.line(f'if (item.size() != {value.storage["max_bytes"]}) return {reader}.fail(::roo_pb::Status::kCapacity);')
        if closed:
            allowed = ' || '.join(f'item == {t}::{ident(n)}' for n, _ in value.resolved.values)
            e.line(f'if ({allowed}) {{')
        if value.map:
            e.line('bool replaced = false;')
            e.line(f'for (size_t i = 0; i < {member}.size(); ++i) {{')
            e.line(f'if ({member}[i].key() == item.key()) {{ {member}[i] = std::move(item); replaced = true; break; }}')
            e.line('}')
            e.line('if (!replaced) {')
        e.line(f'{t}* added = {member}.add();')
        e.line(f'if (added == nullptr) return {reader}.fail(::roo_pb::Status::kCapacity);')
        e.line('*added = std::move(item);')
        if value.map:
            e.line('}')
        if closed:
            if owner.retain_unknown:
                e.line(f'}} else {{ ::roo_pb::ForwardUnknownEnum(unknown_fields_decoder_, {reader}, {value.number}, static_cast<int32_t>(item));')
            e.line('}')
    else:
        if isinstance(value.resolved, Message):
            e.line(f'::roo_pb::ReadValue<{k}>({reader}, *mutable_{name}());')
        elif value.type in ('string', 'bytes') and not value.oneof and not value.storage.get('fixed_length'):
            # String reads validate before committing; retain resident capacity.
            e.line(f'::roo_pb::ReadValue<{k}>({reader}, {member});')
            e.line(f'if ({reader}.status() != ::roo_pb::Status::kOk) return {reader}.status();')
            if presence(owner, value):
                e.line(f'has_{name}_ = true;')
        else:
            e.line(f'{t} item{{}};')
            e.line(f'::roo_pb::ReadValue<{k}>({reader}, item);')
            e.line(f'if ({reader}.status() != ::roo_pb::Status::kOk) return {reader}.status();')
            if closed:
                allowed = ' || '.join(f'item == {t}::{ident(n)}' for n, _ in value.resolved.values)
                e.line(f'if ({allowed}) {{')
            if value.type in ('string', 'bytes'):
                if value.storage.get('fixed_length'):
                    e.line(f'if (item.size() != {value.storage["max_bytes"]}) return {reader}.fail(::roo_pb::Status::kCapacity);')
                e.line(f'*mutable_{name}() = std::move(item);')
            else:
                e.line(f'set_{name}(item);')
            if closed:
                if owner.retain_unknown:
                    e.line(f'}} else {{ ::roo_pb::ForwardUnknownEnum(unknown_fields_decoder_, {reader}, {value.number}, static_cast<int32_t>(item));')
                e.line('}')


def emit_oneof_special_members(e, owner):
    """Manage active union lifetimes while keeping ordinary fields value-like."""
    if not any(v.oneof and not callback(v) for v in owner.fields):
        return
    cls = local_name(owner)
    e.method('Destroys active oneof values and their owned resources.',
             f'~{cls}()', [f'clear_{ident(g)}();' for g in owner.oneofs])
    for move in (False, True):
        argument = f'{cls}&& other' if move else f'const {cls}& other'
        source = 'std::move(other)' if move else 'other'
        verb = 'Moves' if move else 'Copies'
        suffix = ' noexcept' if move else ''
        e.method(f'{verb} owned values and borrowed callback bindings from @p other.',
                 f'{cls}({argument}){suffix} : {cls}()', [f'*this = {source};'])
        body = ['if (this == &other) return *this;']
        for value in owner.fields:
            n = ident(value.name)
            if callback(value):
                body.extend(f'{n}_{direction}_ = other.{n}_{direction}_;'
                            for direction in ('encoder', 'decoder'))
            elif not value.oneof:
                rhs = f'std::move(other.{n}_)' if move else f'other.{n}_'
                body.append(f'{n}_ = {rhs};')
            if presence(owner, value) and not value.oneof:
                body.append(f'has_{n}_ = other.has_{n}_;')
        for group in owner.oneofs:
            n = ident(group)
            body.extend([f'clear_{n}();', f'switch (other.{n}_case_) {{'])
            for value in owner.fields:
                if value.oneof != group or callback(value):
                    continue
                member = ident(value.name) + '_'
                rhs = f'std::move(other.{member})' if move else f'other.{member}'
                body.extend([f'case {value.number}:',
                             f'::new (static_cast<void*>(&{member})) {type_name(value)}({rhs});',
                             'break;'])
            body.extend(['default: break;', '}', f'{n}_case_ = other.{n}_case_;'])
        if owner.retain_unknown:
            body.extend(f'unknown_fields_{direction}_ = other.unknown_fields_{direction}_;'
                        for direction in ('encoder', 'decoder'))
        e.method(f'{verb} values and bindings; self-assignment leaves the message unchanged.',
                 f'{cls}& operator=({argument}){suffix}', body + ['return *this;'])


def emit_message(e, owner):
    cls = local_name(owner)
    e.line('/// Owned protobuf value; borrowed callback bindings survive Clear().')
    e.line(f'class {cls} {{\npublic:')
    bound = maximum_size(owner)
    e.line('/// Conservative encoded-size bound; SIZE_MAX means not statically bounded.')
    e.line(f'static constexpr size_t kMaxEncodedSize = {bound if bound is not None else "SIZE_MAX"};\n')
    for nested in owner.nested:
        e.line(f'using {ident(nested.name)} = {cpp_name(nested)};')
    e.line()
    flags = [ident(v.name) for v in owner.fields if presence(owner, v) and not v.oneof]
    initializers = ' : ' + ', '.join(f'has_{n}_(0)' for n in flags) if flags else ''
    # Member initializers construct children and scalar defaults exactly once.
    # Only nonempty string/byte defaults need work in the constructor body.
    defaults = [f'::roo_pb::Require({ident(v.name)}_.assign({default(v)}));'
                for v in owner.fields if not callback(v) and not v.oneof
                and v.label != 'repeated' and v.type in ('string', 'bytes')
                and v.options.get('default')]
    e.method('Creates a message initialized to schema defaults.', f'{cls}(){initializers}', defaults)
    emit_oneof_special_members(e, owner)
    for group in owner.oneofs:
        n = ident(group)
        e.line(f'/// Identifies the active alternative in {group}.')
        e.line(f'enum class {camel(group)}Case : uint32_t {{')
        e.line('kNotSet = 0,')
        for value in owner.fields:
            if value.oneof == group:
                e.line(f'k{camel(value.name)} = {value.number},')
        e.line('};\n')
        e.method('Returns the active oneof alternative.', f'{camel(group)}Case {n}_case() const', [f'return static_cast<{camel(group)}Case>({n}_case_);'])
        clear = [f'switch ({n}_case_) {{']
        for value in owner.fields:
            if value.oneof == group and not callback(value):
                clear.extend([f'case {value.number}: {{',
                              f'using Value = {type_name(value)};',
                              f'{ident(value.name)}_.~Value();', 'break;', '}'])
        clear.extend(['default: break;', '}', f'{n}_case_ = 0;'])
        e.method('Destroys the active value and clears selection; callback bindings survive.', f'void clear_{n}()', clear)
    for value in owner.fields:
        emit_accessors(e, owner, value)
    if owner.retain_unknown:
        e.method('Binds an unknown-field or extension encoder; state is borrowed.', 'void set_unknown_fields_encoder(::roo_pb::EncodeCallback binding)', ['unknown_fields_encoder_ = binding;'])
        e.method('Binds an unknown-field or extension decoder; state is borrowed.', 'void set_unknown_fields_decoder(::roo_pb::DecodeCallback binding)', ['unknown_fields_decoder_ = binding;'])
    e.method('Restores schema defaults; callback bindings and external state survive.', 'void Clear()', [f'clear_{ident(v.name)}();' for v in owner.fields if not v.oneof] + [f'clear_{ident(g)}();' for g in owner.oneofs])
    checks = []
    for value in owner.fields:
        n = ident(value.name)
        if callback(value):
            if value.label == 'required':
                checks.append(f'if (!has_{n}()) return false;')
            continue
        if value.label == 'required':
            checks.append(f'if (!has_{n}()) return false;')
        if value.storage.get('fixed_count'):
            checks.append(f'if ({n}_.size() != {value.storage["max_count"]}) return false;')
        if value.storage.get('fixed_length') and value.label != 'repeated':
            condition = f'has_{n}()' if presence(owner, value) else f'!{n}_.empty()'
            checks.append(f'if ({condition} && {n}_.size() != {value.storage["max_bytes"]}) return false;')
        if isinstance(value.resolved, Message):
            if value.label == 'repeated':
                checks.append(f'for (const {type_name(value)}& item : {n}_) {{ if (!item.IsInitialized()) return false; }}')
            else:
                checks.append(f'if (has_{n}() && !{n}_.IsInitialized()) return false;')
    e.method('Checks required fields and fixed-size constraints recursively.', 'bool IsInitialized() const', checks + ['return true;'])
    e.line('/// Merges one bounded message; failure leaves a valid partial value.')
    e.line('::roo_pb::Status mergeFrom(::roo_pb::Input& input) {')
    e.line('while (input.status() == ::roo_pb::Status::kOk && input.remaining() != 0) {')
    e.line('uint32_t tag = 0;')
    e.line('if (input.tag(tag) != ::roo_pb::Status::kOk) return input.status();')
    e.line('switch (tag >> 3) {')
    for value in owner.fields:
        n = ident(value.name)
        e.line(f'case {value.number}: {{')
        if callback(value):
            if value.oneof:
                group = ident(value.oneof)
                e.line(f'if ({group}_case_ != {value.number}) clear_{group}();')
                e.line(f'{group}_case_ = {value.number};')
            elif presence(owner, value):
                e.line(f'has_{n}_ = true;')
            e.line(f'{n}_decoder_.read(input, tag);')
        else:
            k = '::roo_pb::Kind::' + kind(value)
            packed = value.label == 'repeated' and value.type not in ('string', 'bytes') and not isinstance(value.resolved, Message)
            if packed:
                e.line('if (::roo_pb::GetWireType(tag) == ::roo_pb::WireType::kLengthDelimited) {')
                e.line('size_t length = 0;')
                e.line('if (input.length(length) != ::roo_pb::Status::kOk) return input.status();')
                e.line('auto packed = input.child(length);')
                e.line('while (packed.status() == ::roo_pb::Status::kOk && packed.remaining() != 0) {')
                emit_read_one(e, owner, value, 'packed')
                e.line('}\n} else {')
            unknown_read = 'unknown_fields_decoder_.readUnknown(input, tag)' if owner.retain_unknown else 'input.skip(tag)'
            e.line(f'if (::roo_pb::GetWireType(tag) != ::roo_pb::GetWireType({k})) {{ {unknown_read}; break; }}')
            emit_read_one(e, owner, value)
            if packed:
                e.line('}')
        e.line('break;\n}')
    unknown_read = 'unknown_fields_decoder_.readUnknown(input, tag)' if owner.retain_unknown else 'input.skip(tag)'
    e.line('default: ' + unknown_read + '; break;\n}\n}\nreturn input.status();\n}\n')
    e.line('/// Encodes resident contents; callers validate required fields separately.')
    e.line('::roo_pb::Status serialize(::roo_pb::Output& output) const {')
    if owner.retain_unknown:
        e.line('unknown_fields_encoder_.write(output, 0);')
    for value in sorted(owner.fields, key=lambda f: f.number, reverse=True):
        e.line('if (output.status() != ::roo_pb::Status::kOk) return output.status();')
        n = ident(value.name)
        k = '::roo_pb::Kind::' + kind(value)
        if callback(value):
            condition = f'if (has_{n}()) ' if presence(owner, value) else ''
            e.line(condition + f'{n}_encoder_.write(output, {value.number});')
        elif value.label == 'repeated':
            packable = value.type not in ('string', 'bytes') and not isinstance(value.resolved, Message)
            packed = packable and value.options.get('packed', 'true' if owner.source.syntax == 'proto3' else 'false') == 'true'
            e.line(f'::roo_pb::WriteRepeated<{k}>(output, {value.number}, {n}_, {str(packed).lower()});')
        else:
            condition = f'has_{n}()' if presence(owner, value) else f'!{n}_.empty()' if value.type in ('string', 'bytes') else f'{n}_ != {default(value)}'
            e.line(f'if ({condition}) ::roo_pb::WriteField<{k}>(output, {value.number}, {n}_);')
    e.line('return output.status();\n}\n')
    e.method('Merges an exact borrowed byte buffer; failure leaves partial state.',
             '::roo_pb::Status mergeFrom(const void* data, size_t size, ::roo_pb::Limits limits = {})',
             ['::roo_pb::Input input(data, size, limits);', 'return mergeFrom(input);'])
    e.method('Parses and replaces contents; false indicates a protocol/resource error.',
             'bool ParseFromArray(const void* data, size_t size)',
             ['return ::roo_pb::Parse(data, size, *this) == ::roo_pb::Status::kOk;'])
    e.method('Writes at the buffer start; failure can leave modified bytes.',
             'bool SerializeToArray(void* data, size_t capacity) const',
             ['size_t written = 0;', 'return ::roo_pb::Serialize(*this, data, capacity, written) == ::roo_pb::Status::kOk;'])
    e.method('Computes encoded size; SIZE_MAX indicates callback or encoding failure.',
             'size_t ByteSizeLong() const',
             ['::roo_pb::Output output;', 'return serialize(output) == ::roo_pb::Status::kOk ? output.size() : SIZE_MAX;'])
    e.line('private:')
    for group in owner.oneofs:
        e.line(f'uint32_t {ident(group)}_case_ = 0;')
        alternatives = [v for v in owner.fields if v.oneof == group and not callback(v)]
        if alternatives:
            e.line('// Only the selected alternative has a live object in this storage.')
            e.line('union {')
            for value in alternatives:
                e.line(f'{type_name(value)} {ident(value.name)}_;')
            e.line('};')
    for value in owner.fields:
        n = ident(value.name)
        if callback(value):
            e.line(f'::roo_pb::EncodeCallback {n}_encoder_{{}};')
            e.line(f'::roo_pb::DecodeCallback {n}_decoder_{{}};')
        elif not value.oneof:
            t = container(value) if value.label == 'repeated' else type_name(value)
            initial = '' if value.label == 'repeated' or isinstance(value.resolved, Message) or value.type in ('string', 'bytes') else default(value)
            e.line(f'{t} {n}_{{{initial}}};')

    if flags:
        e.line('// Grouped presence bits; oneof presence uses its case discriminator.')
        for n in flags:
            e.line(f'uint8_t has_{n}_ : 1;')
    if owner.retain_unknown:
        e.line('::roo_pb::EncodeCallback unknown_fields_encoder_{};')
        e.line('::roo_pb::DecodeCallback unknown_fields_decoder_{};')
    e.line('};\n')


def generate(schema, source):
    e = Emitter()
    e.line('// Generated by roo_pbc 0.1.0. Do not edit.\n#pragma once\n')
    e.line('#include "roo_pb/message.h"')
    if source.extends:
        e.line('#include "roo_pb/extension.h"')
    for imported, _ in source.imports:
        e.line('#include ' + json.dumps(imported[:-6] + '.pb.h'))
    e.line()
    if source.package:
        e.line('namespace ' + '::'.join(ident(p) for p in source.package.split('.')) + ' {\n')
    emitted, active = set(), set()
    def emit(definition):
        if definition.full in emitted or definition.source != source:
            return
        if definition.full in active:
            raise SchemaError(definition.full + ': cyclic nested declaration aliases are unsupported')
        active.add(definition.full)
        if isinstance(definition, Message):
            for nested in definition.nested:
                emit(nested)
            for value in definition.fields:
                if isinstance(value.resolved, (Enum, Message)) and not callback(value):
                    emit(value.resolved)
            emit_message(e, definition)
        else:
            emit_enum(e, definition)
        active.remove(definition.full)
        emitted.add(definition.full)
    for definition in source.definitions:
        emit(definition)
    for owner_source, owner, value in schema.extensions:
        if owner_source != source:
            continue
        name = camel(value.name)
        t = container(value) if value.label == 'repeated' else type_name(value)
        e.line('/// Extension field identifier; bind through set_unknown_fields_encoder/decoder().')
        e.line(f'inline constexpr uint32_t k{name}FieldNumber = {value.number};')
        e.line(f'using {name}Value = {t};')
        e.line(f'inline constexpr ::roo_pb::Kind k{name}Kind = ::roo_pb::Kind::{kind(value)};')
        # Generate a small value holder with the same schema-aware field logic.
        holder = Message(name + 'Extension', source.package + '.' + name + 'Extension',
                         fields=[value], source=source)
        # Emit the holder normally, then add a binding method and raw occurrence
        # decoder before its private state. This reuses validated accessor logic.
        extension = Emitter()
        emit_message(extension, holder)
        text = extension.text()
        binding = Emitter()
        binding.method('Returns a binding borrowing this extension value holder.',
                       '::roo_pb::ExtensionBinding binding()',
                       [f'return {{{value.number}, {{this, EncodeExtension}}, {{this, DecodeExtension}}}};'])
        n = ident(value.name)
        if value.label != 'repeated':
            binding.method('Reports extension presence.', 'bool has_value() const', [f'return has_{n}();'])
            binding.method('Returns the extension value or its schema default.', f'const {t}& value() const', [f'return {n}_;'])
            activation = [f'has_{n}_ = true;']
        else:
            binding.method('Returns resident extension elements.', f'const {t}& value() const', [f'return {n}_;'])
            activation = []
        binding.method('Returns mutable extension storage and marks singular presence.', f'{t}* mutable_value()', activation + [f'return &{n}_;'])
        binding.method('Restores extension defaults and clears presence.', 'void clear()', ['Clear();'])
        binding.line('private:')
        binding.line(f'static ::roo_pb::Status EncodeExtension(void* context, ::roo_pb::Output& output, uint32_t) {{')
        binding.line(f'const {name}Extension& self = *static_cast<{name}Extension*>(context);')
        binding.line('if (!self.IsInitialized()) return output.fail(::roo_pb::Status::kMissingRequired);')
        binding.line('return self.serialize(output);\n}')
        binding.line(f'static ::roo_pb::Status DecodeExtension(void* context, ::roo_pb::Input& input, uint32_t tag) {{')
        binding.line(f'return static_cast<{name}Extension*>(context)->readExtension(input, tag);\n}}')
        binding.line('::roo_pb::Status readExtension(::roo_pb::Input& input, uint32_t tag) {')
        k = '::roo_pb::Kind::' + kind(value)
        packed = value.label == 'repeated' and value.type not in ('string', 'bytes') and not isinstance(value.resolved, Message)
        if packed:
            binding.line('if (::roo_pb::GetWireType(tag) == ::roo_pb::WireType::kLengthDelimited) {')
            binding.line('size_t length = 0;')
            binding.line('if (input.length(length) != ::roo_pb::Status::kOk) return input.status();')
            binding.line('auto packed = input.child(length);')
            binding.line('while (packed.status() == ::roo_pb::Status::kOk && packed.remaining() != 0) {')
            emit_read_one(binding, holder, value, 'packed')
            binding.line('}\nreturn input.status();\n}')
        binding.line(f'if (::roo_pb::GetWireType(tag) != ::roo_pb::GetWireType({k})) return input.skip(tag);')
        emit_read_one(binding, holder, value)
        binding.line('return input.status();\n}\n')
        text = text.replace('private:', binding.text(), 1)
        e.line(text)
    if source.package:
        e.line('}  // namespace ' + source.package)
    return e.text()
