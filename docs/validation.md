# Validation

Validation runs on the local Linux host; physical MCU behavior is not measured.

## Portable harness

From the repository root, with sibling roo libraries available:

```sh
python3 tools/test.py --roo-root /path/to/roo --sanitize
```

The harness generates schemas, verifies the checked-in telemetry header, runs
22 Python compiler/build-contract tests and compiles eight C++ tests and both examples.
Compilation uses C++17, -Wall -Wextra -Werror, -fno-exceptions and -fno-rtti,
linking the buffer runtime and roo_io's Unicode implementation. Dependency
headers are system includes.

The tests and examples pass with ASan and UBSan. LeakSanitizer needs to be disabled
in ptrace sandboxes where it cannot operate. This is not an allocator-failure
injection campaign or a substitute for coverage-guided fuzzing.

Coverage includes:

- Scalar extrema, wire-type encodings and reserved-value rejection, signed
  encodings, UTF-8, defaults and explicit presence.
- Required-field merges, packed/unpacked repetition, duplicate map keys,
  oneof switching, capacity limits and dynamic storage ownership.
- Independent callback contexts and rebinding, decoder registration without
  presence or oneof changes, required-field presence, and directional extension
  fallbacks. Callback invocation and payload consumption, inherited depth, sticky errors,
  extensions, unknown payload/group preservation and storage exhaustion.
- Suffix and buffer-start results, exact and short capacities, null buffers,
  length boundaries at 127/128 and 16383/16384, and counting overflow.
- A 25-level nesting chain and generated nested/repeated callback order.
- Presence flags spanning three bytes, including copy, clear and parse behavior.
- Nested construction/reset allocation counts at depths 0, 4, 8 and 16, schema
  default restoration and preservation of resident child callback bindings.
- Varint word boundaries, overlong encodings, all possible tenth bytes and
  20,000 deterministic cases compared with an independent scalar decoder.
- Resident string failure atomicity, presence, terminators, aliasing and dynamic
  buffer reuse, plus unchanged general-array reset behavior.
- Benchmark compiler-command auditing and preprocessing guards that reject
  missing optimization and incompatible C++/exception/RTTI settings.
- Oneof union size, shared addresses, inactive defaults, member lifetimes,
  copy/move and self-assignment, independent groups, nested containers, and
  callback bindings retained across selection changes.

The malformed-input test feeds 20,000 deterministic arbitrary byte sequences to
two generated parsers in addition to focused malformed-tag, length and varint
tests.

## External interoperability oracle

`tools/conformance.py` uses independently constructed Google Python descriptors,
not descriptors derived from roo_pbc. Google encodes input, the C++ driver
parses and re-encodes it, and Google checks semantic equality. All 302 comparisons
pass, covering randomized scalar and telemetry messages and explicit merge/map
vectors. Legal field ordering differences are not treated as incompatibility.

Run the portable harness to build the oracle driver, then use a separate Python
environment with protobuf installed:

```sh
/path/to/oracle/python tools/conformance.py
```

Google protobuf is a test-only dependency; the compiler and runtime do not use it.

## Build integration and resources

All eight C++ tests pass through Bazel's roo_testing ESP-IDF host profile, and
both host examples build. The Arduino telemetry sketch compiles through the
roo_testing Arduino ESP32 host profile. This checks integration with Arduino
headers, not MCU cross-compilation or hardware execution.

```sh
bazel test --config=roo_testing_idf_esp32 //:wire_test //:buffer_test \
  //:storage_test //:generated_test //:oneof_test //:semantics_test \
  //:malformed_test //:resource_test
bazel build --config=roo_testing_idf_esp32 //:telemetry_example //:callbacks_example
bazel build --config=roo_testing_arduino_esp32 //:arduino_example_compile
```

Bazel commands run serially with the global resource limits, persistent disk
cache and stable workspace output root.

On the x86-64 validation host, telemetry Device occupies 224 bytes and has a
conservative encoded bound of 204 bytes. The allocation test observes zero heap
allocations during construction, bounded mutation and buffer encoding/decoding.
The presence fixture occupies 20 bytes for 17 boolean values and their 17
presence bits. These are host ABI measurements, not universal layout guarantees.

## Buffer benchmark

Run the host benchmark with:

```sh
python3 tools/benchmark.py --roo-root /path/to/roo
```

The script compiles `tools/buffer_benchmark.cpp` with GCC C++17 -O2,
exceptions/RTTI disabled and unused-section elimination. It reports executable
text size and three runs of flat-message and nested-chain timings. Flat workloads
use 200,000 messages per run; nested workloads use 1,000 encodes and record leaf
visits. Serialization includes the move to the buffer start.

Measurements depend on host load and compiler. They do not establish MCU flash
usage, latency or worst-case stack consumption.

## ESP32 comparison with nanopb

The [on-device benchmark](../benchmarks/esp32/README.md) uses eight shared-schema
workloads and compile-time backend selection. Its host validator compares exact
wire bytes and can check against Google protobuf; the runner captures repeated
serialization/deserialization measurements, individual batch durations, object
sizes, firmware hashes and toolchain/board metadata on the connected ESP32.

## Validation limits

The tests do not establish exhaustive nanopb feature parity, constant-time
behavior, binary compatibility with Google's C++ runtime, or stack safety for
application callbacks. Floating-point support requires IEEE754 representations.
Nothrow dynamic growth reports failure; deep copies of dynamic fields have a
release-active success precondition. Buffer errors can leave modified bytes;
applications control error recovery and callback side effects.
