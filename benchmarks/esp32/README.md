# ESP32: roo_pb versus nanopb

This standalone PlatformIO project benchmarks serialization and deserialization
on an actual ESP32. The two backends use the same [schema](benchmark.proto),
values, capacities, compiler, optimization setting and timing loop. Nanopb is
a benchmark-only dependency; production roo_pb does not depend on it.

Measured results and raw data: [verified optimized comparison](OPTIMIZED_RESULTS.md),
ESP32-D0WD-V3, 2026-09-26, including firmware binary sizes and post-suite
free loop-task stack for both backends.

## Run

Requirements: Linux x86-64, Python 3.12 or newer, clang-format, GCC/G++, PlatformIO
with its Python environment (including pyserial), and the sibling Roo libraries.
The preparation step downloads nanopb 0.4.9.1, verifies its published SHA-256, and
uses its bundled protoc/generator. The MCU platform is pinned to pioarduino
55.03.37 (Arduino 3.3.7).

From the roo_pb repository root:

```sh
python3 benchmarks/esp32/prepare.py
python3 benchmarks/esp32/validate.py --sanitize
# Optional: use a Python environment with protobuf >= 5.28 for the oracle check.
python3 benchmarks/esp32/validate.py --sanitize --oracle

# Replaces the connected board's firmware, once per backend.
~/.platformio/penv/bin/python benchmarks/esp32/run.py \
  --port /dev/ttyUSB0 --upload --runs 3 \
  --pio ~/.platformio/penv/bin/pio
```

The default board is `esp32dev`: a classic ESP32 with 4 MB flash, running at
240 MHz. Change `board` in [platformio.ini](platformio.ini) for another chip.
On WSL, attach the USB device with usbipd first. Do not point the upload command
at an unrelated device. The benchmark leaves the last selected firmware installed.

Select a backend at compile time with exactly one of `PROTO_BENCH_ROO` or
`PROTO_BENCH_NANOPB`. The PlatformIO environments set these flags and select the
matching generated headers and runtime:

```sh
pio run -d benchmarks/esp32 -e roo
pio run -d benchmarks/esp32 -e nanopb
```

`run.py --backend roo --upload ...` runs only roo_pb. After uploading,
`--backend roo` without `--upload` captures more runs of the existing firmware.
The same applies to `nanopb`. The sketch also accepts `r` at 115200 baud to
repeat the complete suite.

Generated code, downloaded dependencies, firmware, host tests and timestamped
results live in `build/esp32_benchmark/`. Each capture saves raw serial logs
(including every unsorted `BATCH` duration in microseconds),
build/upload logs, `results.csv`, and a manifest containing source/firmware
hashes and board/toolchain metadata. An explicit `--output` directory must not
already exist, preventing accidental replacement of earlier results.

Before uploading, the runner generates and audits the actual compilation
database for every translation unit, then saves it with the capture. It rejects
wrong optimization, C++ mode, exceptions/RTTI, LTO and nanopb feature flags.
The build also force-includes a preprocessing guard in runtime and application
sources, so direct PlatformIO builds fail on incorrect optimization or C++
settings. Do not put desired flags such as `-Os` in `build_unflags`: PlatformIO
removes matching explicit flags too, not just framework defaults.

## Workloads

| Case | Contents | Encoded bytes |
|---|---|---:|
| sample | Three scalars: varint, zigzag, fixed64 | 16 |
| flat_scalars | Eight mixed scalars, including negative int32, float and double | 52 |
| telemetry | String, 128-byte payload, 64 packed readings, message oneof | 347 |
| wide_tree | Four named branches, each with eight sample messages | 643 |
| chain_1 | One wrapper around a sample | 20 |
| chain_4 | Four nested wrappers | 32 |
| chain_8 | Eight nested wrappers | 48 |
| chain_16 | Sixteen nested wrappers | 80 |

All variable-sized fields use bounded inline storage; neither backend uses
application callbacks or heap-backed message fields. Nanopb uses
`PB_BUFFER_ONLY` and `PB_VALIDATE_UTF8` to match buffer-only I/O and UTF-8
checking in roo_pb. Nanopb options use `max_size = max_bytes + 1` for
strings and the same payload/repeated capacities as roo_pb.

## Measurement contract

- Both builds use C++17, `-Os`, no exceptions/RTTI and no LTO; nanopb runtime
  sources are compiled as C.
- Encoding starts with a populated resident message and ends with wire bytes
  at buffer offset zero. It includes roo_pb's final buffer compaction and
  each backend's normal encoding validation.
- Decoding replaces an existing resident destination, including each API's
  normal field reset/default initialization. It does not include allocation of
  the destination object. The same immutable encoded input is reused.
- Filling, correctness checks, serial output and task yielding are outside the
  timed regions. Function dispatch, status checks and a small volatile checksum
  are inside; timings are end-to-end API latency, not isolated instruction cost.
- A batch starts at one iteration and doubles until it takes at least 40 ms
  (capped at 65,536 iterations). Calibration also warms the code/data caches.
  Nine timed batches report median, minimum and maximum microseconds per message.
  The runner repeats the suite three times; its table takes the median of those
  three medians.
- Timing uses `esp_timer_get_time()` on Arduino's loop task. Wi-Fi and Bluetooth
  are not started. Interrupts and the scheduler stay enabled; the task yields
  between batches. The loop stack is 32 KB to accommodate deep codec recursion.
- The firmware checks round trips, deterministic output, truncated-input
  rejection and short-output-buffer failure before each case. Host validation
  compares every encoded byte between backends; `--oracle` additionally decodes
  and re-encodes through independent Google protobuf and checks representative
  values. Captures reject missing cases and inconsistent wire size/hash.

Results apply to these values, APIs, generated layouts and optimization settings,
not every protobuf workload. `object_bytes` reports the MCU's actual
`sizeof(message)`; representation/alignment overhead may differ. Firmware size
includes Arduino, all eight workloads, adapters and the measurement harness:
it is not a measurement of standalone library size.

Nanopb's API/options are described in its
[official reference](https://jpa.kapsi.fi/nanopb/docs/reference.html);
the pinned download/checksum comes from its
[0.4.9.1 release](https://github.com/nanopb/nanopb/releases/tag/nanopb-0.4.9.1).
