# roo_pb

MIT-licensed C++17 protocol buffers for embedded applications, with an independent
Python compiler, byte-buffer codecs, and UTF-8/ZigZag utilities from **roo_io**.

Neither the compiler nor the runtime depends on Google's protobuf implementation.
An optional external Google Python package is used only for interoperability tests.

## Quick start

Requires Python 3.11+, a C++17 compiler, and roo_io with its dependencies.
The runtime uses `roo_io/data/zigzag.h` and `roo_io/text/unicode.h`.
MODULE.bazel selects the sibling `../roo_io` checkout. Direct C++ builds must
compile/link `src/roo_pb/wire.cpp` and `roo_io/text/unicode.cpp`.

Generate a schema without installing any Python packages:

```sh
python3 tools/generate.py -I examples/telemetry --out build/generated telemetry.proto
clang-format -i build/generated/telemetry.pb.h
```

Add `src`, `build/generated`, and the roo dependency include directories to your
build. Include the generated header:

```cpp
#include "telemetry.pb.h"

example::Device device;
device.set_id(42);
if (!device.try_set_name("kitchen")) {
  // The configured name capacity is 32 bytes.
}
device.add_readings(-12);  // Capacity is a checked precondition.
device.set_online(true);

unsigned char buffer[example::Device::kMaxEncodedSize];
size_t size = 0;
if (roo_pb::Serialize(device, buffer, sizeof(buffer), size) !=
    roo_pb::Status::kOk) {
  // Invalid required/fixed fields, encoding failure, or insufficient space.
}
example::Device received;
bool ok = received.ParseFromArray(buffer, size);
```

Use roo_pb::Serialize(message, buffer, capacity, written) to write at the
buffer start and obtain the encoded length. Parse(buffer, length, message,
limits) replaces a message; message.mergeFrom(buffer, length,
limits) preserves existing fields. Parse/Serialize check required fields; the
low-level generated mergeFrom(Input&) and serialize(Output&) defer that check.

For output without the final move to the buffer start:

```cpp
roo_pb::Output output(buffer, sizeof(buffer));
roo_pb::Status status = roo_pb::Serialize(device, output);
if (status == roo_pb::Status::kOk) {
  // Consume exactly output.size() bytes beginning at output.data().
}
```

Output prepends each payload into the buffer suffix, followed by its length and
tag. ByteSizeLong computes the encoded size. Presence flags are grouped one-bit
fields; oneofs reuse their case discriminator.

Callback fields expose separate EncodeCallback and DecodeCallback bindings with
independent contexts: set_field_encoder() establishes presence, while
set_field_decoder() only registers a handler. Callbacks take Input&/Output&.
Encoders prepend payload, length and
tag in that order, and visit repeated occurrences in reverse order. Transport
and framing are the application's responsibility. See the [design](docs/design.md)
and [semantics](docs/semantics.md) for ownership and error contracts.

## Features and scope

| Capability | Implementation |
|---|---|
| Proto2 / proto3 | Scalars, enums, defaults, required/optional presence |
| Composition | Nested messages, imports, oneof, maps |
| Repeated fields | Packed/unpacked input, mixed occurrences, configurable bounds |
| Storage | Bounded inline, explicit nothrow dynamic arrays/strings, callbacks |
| Resource controls | Byte/depth limits, narrowed integers, fixed lengths/counts |
| Extensions | Generated typed holders and borrowed handler sets |
| Unknown fields | Skip by default; opt-in bounded retention/forwarding |
| C++ API | Familiar getters/setters, presence, mutable messages, checked mutations |
| Generator | Standard-library Python, deterministic headers, depfiles |
| Static bounds | Conservative kMaxEncodedSize; SIZE_MAX for unbounded configurations |

Supported features and limitations:

- Oneof values share union storage sized for the largest alternative, plus the
  case discriminator and alignment padding. Only the selected value is live.
  Callback registrations remain independent of the selected value.
- Recursive message edges require callback storage; resident recursive ownership
  is rejected. Dynamic storage covers strings and repeated arrays.
- Maps expose entry containers plus find/try_insert helpers, not Google's map type.
- Dynamic C++ copies enforce allocation success; checked growth reports failure.
- Callback/external extension state is caller-owned and survives Clear().
- Editions, declared groups, services, reflection, arenas, JSON/TextFormat and
  well-known-type convenience APIs are not implemented. Unknown wire groups are
  safely skipped. Ordinary well-known-shaped messages can be declared locally.
- No hardware timing, flash-size parity or exhaustive protocol conformance claim
  is made. See [validation](docs/validation.md) for actual checks and measurements.

## Documentation

- [Library design](docs/design.md)
- [Compiler and storage configuration](docs/compiler.md)
- [Storage ownership and capacity](docs/storage.md)
- [Message semantics, callbacks and extensions](docs/semantics.md)
- [Validation and limitations](docs/validation.md)
- [Telemetry example](examples/telemetry/main.cpp)
- [Callback and extensions example](examples/callbacks/README.md)

## Bazel code generation

```starlark
load("@roo_pb//:defs.bzl", "roo_pb_library")

roo_pb_library(
    name = "messages",
    srcs = ["device.proto"],
    options = ["device.roo_pb.toml"],
)
```

Add `:messages` to a C++ target's `deps` and include `"device.pb.h"`. The rule
supplies generated headers and the runtime, supports imported schemas through
`deps`, and rebuilds when schemas, sidecars, or compiler sources change.
See [Bazel protobuf libraries](docs/bazel.md) for paths, imports, and local setup.

## Build and test

For neighboring roo repositories under one directory:

```sh
python3 tools/test.py --roo-root /path/to/roo --sanitize
```

This runs Python compiler tests, builds C++ tests with exceptions and RTTI
disabled, runs sanitizer coverage and builds/runs both examples. The host
harness selects roo_logging's ESP-IDF-compatible header backend because current
roo_logging headers have no native Linux stream backend. This is host validation,
not an ESP32 hardware run. Under ptrace sandboxes, use `ASAN_OPTIONS=detect_leaks=0`;
address and undefined-behavior checks remain enabled.

Bazel integration uses the same roo_testing profiles as neighboring libraries:

```sh
bazel test --config=roo_testing_idf_esp32 //:wire_test //:storage_test \
  //:generated_test //:semantics_test //:malformed_test //:resource_test //:buffer_test
bazel run --config=roo_testing_idf_esp32 //:telemetry_example
bazel build --config=roo_testing_arduino_esp32 //:arduino_example_compile
```

The checked-in `local_path_override` selects `../roo_io`. For a different layout,
use `--override_module=roo_io=/path/to/roo_io`; roo_testing can similarly be
selected with `--override_module=roo_testing=/path/to/roo_testing`.
Bazel uses the normal persistent output/cache locations and global resource limits.

Install roo_io and its dependencies in the Arduino library directory. To use
a local checkout with PlatformIO, set `lib_deps = symlink:///absolute/path/to/roo_io`
in `platformio.ini`. Run the Python compiler on the host and place the generated
headers in your sketch/project. Enable C++17 in the target build.

`pip install .` installs the `roo-pbc` command; running `tools/generate.py`
directly needs no packaging tools.

## On-device benchmarks

The [ESP32 comparison](benchmarks/esp32/README.md) builds selectable roo_pb and
nanopb backends from the same schemas, validates their wire output, and captures
serialization/deserialization timings for flat, repeated and nested workloads.

## License

[MIT](LICENSE). Generated schema-specific code contains no Google protobuf
implementation code.
