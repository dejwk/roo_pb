# roo_pb design

## Objective

Provide MIT-licensed embedded C++ protocol buffers with a standalone Python
compiler, caller-owned byte buffers, and predictable resource use.

## Background

Firmware needs interoperable structured messages with bounded storage and
explicit ownership. Protobuf encodes numbered fields with varint tags and six
wire types. Strings, bytes, submessages and packed repeated values carry a
length prefix. Proto2 tracks presence and supports explicit defaults and required
fields; proto3 normally omits default scalar values. Schemas alone do not bound
memory. See the [wire specification](https://protobuf.dev/programming-guides/encoding/)
and [presence rules](https://protobuf.dev/programming-guides/field_presence/).

## Architecture

The compiler resolves a schema graph, validates it, applies sidecar storage
options, and emits C++ headers. Generated classes own typed values and presence
bits. Shared typed helpers implement field operations without a polymorphic
message base.

Concrete `Input` and `Output` contexts borrow byte buffers and enforce protocol
boundaries. Primitive implementations in [wire.cpp](../src/roo_pb/wire.cpp)
are shared by generated messages; callbacks borrow the same contexts directly.
`WireType : uint8_t` names the protobuf wire encodings. `Status` reports protocol
and resource failures without exceptions or RTTI.

The runtime uses roo_io's UTF-8 validation and ZigZag utilities. Buffer access,
primitive wire encoding and size accounting are implemented in roo_pb.
Transport and message framing belong to the application.

## Buffer encoding and parsing

### Output

`Output` prepends values into the buffer suffix. Each primitive retains normal
wire byte order; the order of calls determines where it appears in the result.
Generated serializers traverse fields and repeated elements in reverse so the
finished wire retains ascending field numbers and the original repeated order.

Each submessage records the byte count before encoding its contents, then
prepends the measured length and tag. Packed repeated fields use the same
approach for their payload. Protobuf length prefixes vary in width; writing them
after the payload lets the writer prepend exactly the required bytes.

For a field containing uint32 value 150, the writer prepends `96 01` (hex),
then tag `08`. `data()` points at the suffix `08 96 01` and `size()` returns 3.

`Serialize(message, output)` validates the message and leaves the suffix in
place. The overload taking data, capacity and written calls `finish()` to move
the suffix to the buffer beginning. `SerializeToArray` uses that overload.
Exact capacity works; extra capacity affects the initial suffix offset.
`finish()` seals a non-null buffer against further nonempty writes.

A default-constructed `Output` counts instead of writing. `ByteSizeLong` uses
this mode to compute the encoded size, returning SIZE_MAX on encoding or callback
failure. Counting does not copy payloads but still validates UTF-8.

### Input and errors

`Input` reads forward from an exact byte buffer. Child windows share the cursor
and first error, and must finish before the parent resumes. Lengths are checked
before copying, and unknown groups are validated for matching start/end tags.
The default input limits are 1 MiB and nesting depth 32; callers can supply
different limits.

Empty null buffers are accepted; nonempty null buffers are malformed. Output
checks arithmetic overflow and capacity before each primitive write. Errors can
leave modified output bytes, which callers must discard. Source payloads and
output storage must not overlap.

Parse clears resident contents first; merge preserves existing values and appends
repeated elements. Failures leave a valid partial message. Required-field
validation occurs after the complete merge, allowing separate occurrences of a
submessage to supply different required fields.

## Storage and presence

Bounded strings retain a byte length and a trailing NUL; embedded NUL bytes are
preserved. Repeated storage tracks live size separately from capacity. Dynamic
string and repeated storage uses explicit nothrow allocation. Recursive resident
ownership is rejected; recursive schema edges use callbacks.

String/byte storage writes the complete replacement contents and terminator
before publishing their length; discarded bytes are not erased. This avoids
resetting bytes that will immediately be overwritten, without changing general
array element initialization or resource release. Ordinary singular string/bytes
fields read into resident storage after input validation and reuse dynamic
capacity. Oneof and fixed-length fields stage their values before committing.
Failed string/bytes field reads preserve the previous value and presence; the
message as a whole can still contain earlier successfully decoded fields.

Failed checked mutation retains the previous field value. Convenience setters
enforce capacity contracts by aborting on failure, including in release builds.
For example, assigning a name longer than a Device's configured 32-byte capacity
through `try_set_name` returns false without changing the field.

Presence follows the schema, not whether the value differs from its default.
All non-oneof presence flags are adjacent uint8_t bit-fields, one bit per flag,
after resident field storage. Constructors initialize every flag, including
callback presence. `Clear` resets presence while retaining callback bindings.
Bit-field layout is compiler/ABI-dependent; wire encoding and public accessors
do not depend on its bit order. C++17 bit-fields are initialized through the
constructor.

Scalar schema defaults are member initializers. Child messages and repeated
storage construct their resident values once; constructor bodies only assign
nonempty string/byte defaults. Clearing a non-oneof singular child calls its
`Clear()` in place, preserving nested callback bindings and reusable dynamic
capacity. Construction and reset therefore do not recursively build replacement
subtrees. Empty string/byte defaults clear storage without allocating.

Each oneof stores its owned alternatives in a union and tracks presence with a
case discriminator. Its per-message storage is the largest owned alternative,
plus the discriminator and alignment padding. Switching destroys the previous
value and constructs the selected one; clearing destroys only the active value.
Generated copy/move operations handle the active value and ordinary fields.
Inactive getters return shared immutable schema defaults, never inactive union
members. References into the active value expire when it is cleared or replaced.

Callback bindings remain outside the union: every decoder must be available
before the incoming field selects an alternative, and bindings survive `Clear`.
A callback-only oneof needs no union. See [message semantics](semantics.md) for
selection and merge behavior.

Sidecar TOML uses fully qualified field names and `storage`, `max_bytes`,
`max_count`, `fixed_length`, `fixed_count` and `integer_bits` settings.
Bounds are local resource constraints and do not change wire types. Unsupported
schema/storage combinations produce compiler diagnostics. See
[compiler configuration](compiler.md) and [field storage](storage.md).

## Callbacks, extensions and unknown fields

`EncodeCallback` and `DecodeCallback` hold independent function pointers and
application contexts. Generated fields expose separate encoder/decoder getters
and setters. Decoder registration leaves presence and oneof selection unchanged;
encoder registration establishes presence. Parsing establishes presence when it
encounters the field. Clear retains both bindings while resetting presence.

Callbacks take `Input&` or `Output&`, borrowed only during the call. Encoders
prepend payload, length when applicable, and tag. Multiple occurrences must be
emitted in reverse desired wire order. Each binding is invoked once per encoding
traversal; an explicit `ByteSizeLong` call invokes encoders as well. External
data must support the application's chosen access pattern.

Decoders for known length-delimited or fixed-width callback fields receive an
exact payload window and must consume it completely. Unknown/extension handlers
receive the enclosing input after the tag, with the length prefix still unread.
Callback errors are sticky, and side effects cannot be rolled back.

Unknown fields are skipped by default. Extension ranges enable a per-message
encoder and decoder bindings; other messages opt in through sidecar configuration. Ordinary
messages pay no unknown-handler pointer cost. Generated extension holders expose
typed values, identifiers and bindings without a global registry.

`UnknownFields` validates each occurrence, retains its payload bytes including
any length prefix, and normalizes its outer tag. Captured occurrences keep their
encounter order, though their ordering relative to known fields is not retained.
`ExtensionSet` traverses bindings in reverse so its finished output retains
binding order. Extension bindings and unknown-field fallbacks have separate
encoder/decoder contexts. External holders and callback state remain caller-owned.

Maps use bounded entry storage and replace matching keys on decode. Closed
proto2 enums discard unknown numeric values from typed fields; enabled unknown
handlers receive those values. Proto3 enums retain numeric values. Full contracts
and callback examples are in [message semantics](semantics.md).

## Resources and ownership

Bounded fields and core I/O use no heap allocation. Dynamic fields allocate
explicitly; application callbacks control their own resources. For capacity C,
inline string RAM includes C+1 bytes and a length; repeated RAM includes
C*sizeof(element) and a length, including inactive elements. Callback bindings
each store a borrowed function pointer and a context. Each generated callback
field stores both directional bindings, totaling four pointers. Messages without
callback fields or unknown-field handling pay no binding storage cost.

For F visited fields/elements, B encoded bytes and nesting depth D, resident
serialization takes O(F+B) time and O(D) stack space. Validation is a separate
linear traversal. Moving the result to the buffer start adds at most one O(B)
copy. The core retains no scratch size table or message-size cache.

Encoding stack depth follows the resident schema structure. Callback recursion
is application-controlled and must be bounded by the caller; output has no depth
budget. Forward-only producers need buffering or a suitable access pattern for
backward encoding. There is no public thread-safety guarantee for concurrent
mutation of a message.

## API and integration

```cpp
example::Device device;
device.set_id(42);
unsigned char buffer[example::Device::kMaxEncodedSize];
size_t written = 0;
roo_pb::Status status =
    roo_pb::Serialize(device, buffer, sizeof(buffer), written);

// Alternatively, consume the encoded suffix directly.
roo_pb::Output output(buffer, sizeof(buffer));
status = roo_pb::Serialize(device, output);
if (status == roo_pb::Status::kOk) {
  example::Device received;
  status = roo_pb::Parse(output.data(), output.size(), received);
}
```

Generated APIs include getters/setters, clear/has accessors, mutable submessages,
repeated accessors, `Clear`, `IsInitialized`, `ByteSizeLong`, `ParseFromArray`
and `SerializeToArray`. Top-level Parse/Serialize validate required fields;
low-level mergeFrom/serialize defer that validation.

Generated classes live in headers with no generated .cpp files. Applications
link `src/roo_pb/wire.cpp` and roo_io's `text/unicode.cpp`. The Bazel module
selects the sibling roo_io checkout through a local path override.

## Validation and scope

Portable tests cover compiler output, wire primitives, scalar extremes,
proto2/proto3 semantics, maps, oneofs, extensions, callbacks, malformed input,
unknown groups, capacity boundaries, presence packing and bounded allocation.
The external oracle checks protobuf semantic interoperability. See
[validation](validation.md) for commands, coverage and resource measurements.

Editions, declared groups, JSON/TextFormat, RPC generation, public reflection and
full Google C++ API emulation are outside the library's scope. Unknown wire
groups can be skipped or retained. Floating-point codecs require IEEE754 float
and double representations. The [feature matrix](../README.md#features-and-scope)
records supported combinations and limitations.
