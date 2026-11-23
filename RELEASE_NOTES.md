# roo_pb 1.0.0

- Bounded and dynamic strings and bytes now use length-delimited storage without a trailing NUL. Replace `c_str()` with `view()` or pass `data()` with `size()`. Bounded and dynamic strings support implicit, allocation-free conversion to `roo::string_view`, plus view assignment and generated field setters. Views preserve embedded NULs and borrow storage until mutation or destruction. `BoundedString<N>` and `max_bytes = N` still allow N payload bytes. Wire encoding stays compatible. Rebuild consumers for the new object layouts.
- Generated `SerializeToArray(data, capacity, written)` now requires a `size_t&` output parameter. It writes at the start of the buffer and reports the encoded length on success. On failure, it sets `written` to zero and may leave modified bytes in the buffer.
- Add standalone support for nanopb inline annotations for capacity bounds, integer widths, fixed-size constraints, and static or callback storage. The built-in `nanopb.proto` import supports dotted and aggregate syntax, file and message defaults, and final TOML overrides. Unsupported options produce compiler errors. [Nanopb compatibility details](docs/nanopb.md)
- Add generated `DebugString()` diagnostics with configurable byte, element, and depth limits. Formatting inspects resident fields without invoking callbacks. The compiler rejects schema names that collide with generated diagnostic methods. Strings and bytes also support escaped, allocation-free insertion into `roo_logging` streams and Linux standard streams. Direct C++ builds must link `src/roo_pb/debug.cpp`. [Diagnostic formatting](docs/debug.md)
- Derive Python package metadata from `roo_pbc.__version__` so the wheel version matches the version in generated headers.
- Set Bazel dependencies and PlatformIO minimums to `roo_backport` 1.3.0, `roo_logging` 1.6.1, and `roo_io` 2.5.0. Update the Bazel development dependency and CI to `roo_testing` 2.7.0 and fix ESP32 host linkage.
- Add Bazel profile definitions for ESP32-S3, ESP32-C3, and RP2040 and list all nine profiles in `test_all_profiles`. The root `.bazelrc` currently imports only the two original ESP32 profiles.

---

# roo_pb 0.1.1

- Upgrade `roo_io` to 2.4.1 in Bazel and require 2.4.1 or newer in PlatformIO.
- Upgrade the Bazel development dependency `roo_testing` to 2.3.1.
- Update Bazel setup documentation and library descriptions to reflect published modules and remove obsolete local-checkout requirements.

---

# roo_pb 0.1.0

MIT-licensed C++17 protocol buffers with a standalone Python compiler and
caller-owned byte buffers. The runtime uses roo_io for UTF-8 and ZigZag utilities.

Generated messages support proto2/proto3 presence, scalar and repeated fields,
maps, oneofs, bounded and explicit dynamic storage, callback fields, typed
extension holders and optional unknown-field retention. Non-oneof presence
flags are grouped and bit-packed.

`Serialize(message, data, capacity, written)` writes at the buffer start and
returns the encoded length. `Serialize(message, output)` exposes the encoded
suffix through `Output::data()` and `size()`. `ByteSizeLong` computes encoded
size. EncodeCallback and DecodeCallback have separate contexts and generated
setters. Decoder registration does not change presence or oneof selection.
Encoders prepend payload, length when applicable, and tag; decoders consume
bounded input. `Clear` resets callback presence while retaining both bindings.

Applications link `src/roo_pb/wire.cpp` and roo_io's Unicode implementation.
The standalone compiler generates message headers from schemas and sidecars.

See the [design](docs/design.md), [feature matrix](README.md#features-and-scope),
and [validation](docs/validation.md) for contracts, supported features and checks.
