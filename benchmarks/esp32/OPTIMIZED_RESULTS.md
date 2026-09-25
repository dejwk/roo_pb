# ESP32 benchmark results — 2026-09-26

The raw capture retains the library's name at measurement time (`roo_proto`);
the report uses its current name, `roo_pb`. The rename does not constitute a
new benchmark run.

Measured on an ESP32-D0WD-V3 revision 3.1 at 240 MHz, using Arduino 3.3.7,
ESP-IDF v5.5.2-729-g87912cd291 and Xtensa GCC 14.2.0. Both backends use
`-Os`, C++17, exceptions/RTTI disabled and no LTO. Nanopb 0.4.9.1 uses
`PB_BUFFER_ONLY` and `PB_VALIDATE_UTF8`.

Both backends use the same schema, field capacities, input values and timing
loop. The runner verifies actual compiler commands before uploading; this
capture verified 60 roo_pb and 62 nanopb translation units.

## Serialization and deserialization

Times are microseconds per message, taking the median of three run medians;
each run measures nine calibrated batches. Lower is better. Encoding produces
contiguous bytes at buffer offset zero. Decoding replaces an existing message,
including its normal reset work.

| Workload | Encode roo_pb | Encode nanopb | Decode roo_pb | Decode nanopb |
|---|---:|---:|---:|---:|
| sample | 5.019 | 10.081 | 5.391 | 12.668 |
| flat_scalars | 12.531 | 24.705 | 14.104 | 34.233 |
| telemetry | 70.754 | 150.443 | 69.268 | 128.133 |
| wide_tree | 191.625 | 993.141 | 251.801 | 495.062 |
| chain_1 | 7.367 | 24.756 | 8.510 | 17.632 |
| chain_4 | 12.480 | 104.477 | 17.242 | 42.953 |
| chain_8 | 19.230 | 294.113 | 28.814 | 77.430 |
| chain_16 | 32.297 | 958.984 | 51.992 | 146.432 |

roo_pb is faster in all eight measured encode and decode cases. These are
workload-specific results, not a claim about every schema or platform.

## Binary size and stack

| Metric (bytes) | roo_pb | nanopb |
|---|---:|---:|
| Application firmware binary (`firmware.bin`) | 298,304 | 295,248 |
| Post-suite free loop-task stack, minimum across runs | 28,620 | 24,168 |

The roo_pb binary is 3,056 bytes larger. These sizes include Arduino, all
eight workloads and the benchmark harness; they are not standalone library
sizes. They exclude the separately flashed bootloader and partition table.
The [roo_pb upload log](results/20260926-optimized/roo-upload.log) and
[nanopb upload log](results/20260926-optimized/nanopb-upload.log) record the
uncompressed application binary sizes written at address `0x00010000`.

The loop-task stack allocation is 32,768 bytes. Each post-suite `END` record
reports `uxTaskGetStackHighWaterMark(nullptr)` in bytes: the minimum unused
stack observed over the task's lifetime, not instantaneous free stack.
roo_pb reports 28,836, 28,620 and 28,620 bytes across the three runs;
nanopb reports 24,168 bytes in every run. The table uses the minimum, giving
roo_pb 4,452 more bytes of observed stack headroom. These observations
include setup and validation and do not establish worst-case stack safety.

## Validation and reproduction

- All eight workloads pass byte-for-byte cross-backend comparison, round-trip,
  short-buffer/truncated-input and independent Google protobuf checks.
- All 96 result rows and 864 batch records were captured; wire sizes and hashes
  agree between backends.
- 22 Python compiler/build-contract tests and eight portable C++ tests pass;
  C++ tests run with ASan/UBSan, and both examples pass.
- All eight Bazel ESP-IDF host targets and 302 Google ↔ roo conformance
  comparisons pass.

Evidence: [per-run CSV](results/20260926-optimized/results.csv),
[roo_pb serial log](results/20260926-optimized/roo.log),
[nanopb serial log](results/20260926-optimized/nanopb.log),
[capture manifest](results/20260926-optimized/manifest.json), and compressed
[roo_pb compiler commands](results/20260926-optimized/roo-compile-commands.json.gz) /
[nanopb compiler commands](results/20260926-optimized/nanopb-compile-commands.json.gz).

Use the [rerun instructions](README.md#run) to reproduce the comparison.
