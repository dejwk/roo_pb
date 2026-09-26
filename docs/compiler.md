# Standalone compiler

Requires Python 3.11 or later (standard library only). No protoc or Google
protobuf package is used. From the repository root:

```sh
PYTHONPATH=compiler python3 -m roo_pbc \
  -I examples/telemetry --out build/generated telemetry.proto
clang-format -i build/generated/telemetry.pb.h
```

Include `telemetry.pb.h` in your C++ application and add `src` and the output
folder to include paths. The compiler emits the entire imported schema graph.
Pass `--direct-only` to emit only explicitly named schemas while still loading
and validating their imports. Bazel uses this mode to keep imported headers in
their owning library targets; see [Bazel integration](bazel.md).
Use `--depfile output.d` for a Make-style dependency file including sidecars.
Output is deterministic for identical sources and options. Generation validates
the complete graph before writing files. Diagnostics identify schema locations
for syntax errors and qualified names for resolution/storage errors.

A `.roo_pb.toml` file beside each schema configures fields:

```toml
[fields."example.Device.name"]
max_bytes = 32

[fields."example.Device.readings"]
max_count = 16
```

`storage = "dynamic"` explicitly enables heap storage. `storage = "callback"`
generates independent EncodeCallback and DecodeCallback bindings instead of
resident data, with field_encoder()/field_decoder() getters and matching
set_field_encoder()/set_field_decoder() setters. Registering a decoder does not
change field presence or oneof selection. Recursive schema
references require callback storage on the recursive edge. Bounded repeated
strings need both max_count and max_bytes. Map key/value bounds belong to the
synthetic `Message.FieldEntry.key` / `.value` fields (capitalize the first letter
of the map field name). Use integer_bits to narrow resident integer storage;
decoding out-of-range values reports capacity failure.

Generated accessors preserve protobuf naming. Bounded strings expose data(),
size(), c_str(), and assign(); they are not std::string. Repeated containers
expose size(), indexing, iteration, add(), and push_back(). Indexed access and
unchecked generated convenience mutations use release-active preconditions.

Proto2/3 messages, nested declarations, imports, enums, oneofs, repeated and map
fields are supported. Editions, service generation and declared groups are
rejected. Unknown wire groups can be skipped. Custom aggregate options are
rejected; use the sidecar for roo options. Reflection and Google descriptor APIs
are not generated. Storage changes alter the C++ type layout, not wire tags.

Capacity and fixed-size options apply to bounded storage; combining them with
callback/dynamic storage is rejected. Explicit message-level `unknown_fields`
configuration enables callback state only on messages that need forwarding.
Generated headers are intentionally self-contained; multiple output headers can
be included in different translation units without separate generated .cpp files.
Use clang-format with the project's Google-based configuration for emitted code.

Generated codecs use concrete Input/Output contexts. Compile the shared runtime
src/roo_pb/wire.cpp and roo_io/text/unicode.cpp into the application. All
non-oneof has flags are grouped one-bit fields; their binary layout is ABI-specific
and is not a serialization format.
