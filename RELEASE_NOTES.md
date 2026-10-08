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
