# Diagnostic formatting

`message.DebugString()` returns an owning `std::string` for logs, UI text, and
tests. It inspects resident data without serializing or validating the message.
Partially populated messages can therefore be inspected after a parse failure.

```cpp
roo_pb::DebugOptions options;
options.max_bytes = 256;
options.max_elements = 32;
options.max_depth = 4;
LOG(INFO) << "Received: " << message.DebugString(options);
```

Defaults are 1024 output bytes, 128 elements, and 8 message levels. The root
counts as one level. Each emitted field and each repeated element consumes the
shared element budget, including fields inside nested messages and map entries.
Limits apply to the entire traversal, not separately to each child. Reaching a
limit stops output and adds `...` within the byte budget, replacing the final
bytes when necessary. Budgets of 0, 1, or 2 bytes produce respectively empty
output, `.`, or `..` when truncated. An output that exactly fits is retained.
Truncation can leave unmatched delimiters or a partial escape or UTF-8 sequence;
this is diagnostic text, not a serialization format.

Fields appear in field-number order. Explicit presence is respected, including
present default values and only the active oneof alternative. Implicit proto3
defaults and empty repeated fields are omitted. Nested messages include their
message names. Repeated fields use lists; maps use lists of entry messages in
storage order, without sorting. Known enums use the first declared name for a
number; unknown numbers print in decimal. Booleans use `true` and `false`.
Integers print in decimal and floating-point values use round-trip double
precision with the C library's current numeric locale. Strings are quoted;
NUL, newline, carriage return, tab, quotes, backslashes and other ASCII control
bytes are escaped. Non-ASCII bytes are preserved. Bytes fields use `0x` followed
by lowercase hex pairs.

Callback fields print `<callback>` when explicitly present, or when a binding
exists for an implicit/repeated field. Decoder binding alone does not establish
explicit presence. Unknown-field and extension bindings print
`unknown_fields: <callback>`; their externally stored contents are not inspected.
No encoder or decoder is invoked, and no message state changes.

Formatting maintains one growing output string, which can allocate and reallocate
as it grows. The byte limit bounds the string's length, not allocator overhead
or reserved capacity. Nested messages append to the same formatter without
creating intermediate strings. With exceptions disabled, allocation failure
follows the standard library's fatal allocation behavior. Message construction,
parsing, and serialization retain their existing allocation contracts.

Generated headers contain field traversal and the `DebugString()` wrapper.
Shared escaping, numeric formatting, and limit handling live in
`src/roo_pb/debug.cpp`; direct builds must compile that source. This adds no
per-message state. Generated `debugTo(DebugWriter&)` is the composition hook
used to share budgets and output across nested messages. The diagnostic format
is neither protobuf TextFormat nor JSON and is not intended for parsing.
