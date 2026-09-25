#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(PROTO_BENCH_ROO) == defined(PROTO_BENCH_NANOPB)
#error "Define exactly one of PROTO_BENCH_ROO or PROTO_BENCH_NANOPB"
#endif

#include "benchmark.pb.h"
#ifdef PROTO_BENCH_NANOPB
#include "pb_decode.h"
#include "pb_encode.h"
#endif

namespace benchmark {

constexpr size_t kCapacity = 2048;

#ifdef PROTO_BENCH_ROO
constexpr const char* kBackend = "roo_pb";
using Sample = bench::Sample;
using Flat = bench::Flat;
using Telemetry = bench::Telemetry;
using Tree = bench::Tree;
using Chain1 = bench::Chain1;
using Chain4 = bench::Chain4;
using Chain8 = bench::Chain8;
using Chain16 = bench::Chain16;
#define BENCH_SET(object, field, value) (object).set_##field(value)
#else
constexpr const char* kBackend = "nanopb-0.4.9.1";
using Sample = bench_Sample;
using Flat = bench_Flat;
using Telemetry = bench_Telemetry;
using Tree = bench_Tree;
using Chain1 = bench_Chain1;
using Chain4 = bench_Chain4;
using Chain8 = bench_Chain8;
using Chain16 = bench_Chain16;
#define BENCH_SET(object, field, value) (object).field = (value)

template <typename T>
const pb_msgdesc_t* Descriptor();

#define BENCH_DESCRIPTOR(type)                    \
  template <>                                     \
  inline const pb_msgdesc_t* Descriptor<type>() { \
    return bench_##type##_fields;                 \
  }
BENCH_DESCRIPTOR(Sample)
BENCH_DESCRIPTOR(Flat)
BENCH_DESCRIPTOR(Telemetry)
BENCH_DESCRIPTOR(Tree)
BENCH_DESCRIPTOR(Chain1)
BENCH_DESCRIPTOR(Chain4)
BENCH_DESCRIPTOR(Chain8)
BENCH_DESCRIPTOR(Chain16)
#undef BENCH_DESCRIPTOR
#endif

inline void Fill(Sample& value, unsigned seed = 1) {
  BENCH_SET(value, id, 1000 + seed);
  BENCH_SET(value, value, -12345 - static_cast<int32_t>(seed));
  BENCH_SET(value, timestamp, UINT64_C(1700000000123456) + seed);
}

inline void Fill(Flat& value, unsigned seed = 1) {
  BENCH_SET(value, id, 123456 + seed);
  BENCH_SET(value, signed_value, -12345);
  BENCH_SET(value, delta, INT64_C(-123456789012));
  BENCH_SET(value, mask, UINT32_C(0xa5a5f00f));
  BENCH_SET(value, timestamp, UINT64_C(1700000000123456));
  BENCH_SET(value, temperature, 23.75f);
  BENCH_SET(value, voltage, 3.3125);
  BENCH_SET(value, enabled, true);
}

inline void Fill(Telemetry& value, unsigned seed = 1) {
  BENCH_SET(value, sequence, seed + 500);
  char payload[128];
  for (size_t i = 0; i < sizeof(payload); ++i) {
    payload[i] = static_cast<char>(i ^ 0xa5);
  }
#ifdef PROTO_BENCH_ROO
  value.set_label("boiler-room/sensor-03");
  value.set_payload(payload, sizeof(payload));
  for (int i = 0; i < 64; ++i) {
    value.add_readings(i * 731 - 16000);
  }
  Fill(*value.mutable_sample(), seed);
#else
  std::strcpy(value.label, "boiler-room/sensor-03");
  value.payload.size = sizeof(payload);
  std::memcpy(value.payload.bytes, payload, sizeof(payload));
  value.readings_count = 64;
  for (int i = 0; i < 64; ++i) {
    value.readings[i] = i * 731 - 16000;
  }
  value.which_state = bench_Telemetry_sample_tag;
  Fill(value.state.sample, seed);
#endif
}

inline void Fill(Tree& value, unsigned seed = 1) {
  BENCH_SET(value, sequence, seed + 900);
#ifndef PROTO_BENCH_ROO
  value.branches_count = 4;
#endif
  for (unsigned i = 0; i < 4; ++i) {
#ifdef PROTO_BENCH_ROO
    bench::Branch* branch = value.add_branches();
    branch->set_name("sensor-bank");
    for (unsigned j = 0; j < 8; ++j) {
      Fill(*branch->add_samples(), seed + i * 8 + j);
    }
#else
    bench_Branch* branch = &value.branches[i];
    std::strcpy(branch->name, "sensor-bank");
    branch->samples_count = 8;
    for (unsigned j = 0; j < 8; ++j) {
      Fill(branch->samples[j], seed + i * 8 + j);
    }
#endif
  }
}

// Each level adds a present submessage and a scalar; the leaf is a Sample.
template <typename T>
void Fill(T& value, unsigned seed = 1) {
  BENCH_SET(value, id, seed);
#ifdef PROTO_BENCH_ROO
  Fill(*value.mutable_child(), seed + 1);
#else
  value.has_child = true;
  Fill(value.child, seed + 1);
#endif
}

#undef BENCH_SET

// Both backends produce a contiguous result at buffer[0]. The roo path includes
// its final buffer compaction; neither path precomputes ByteSizeLong().
template <typename T>
__attribute__((noinline)) bool Encode(const T& value, uint8_t* buffer,
                                      size_t capacity, size_t& size) {
#ifdef PROTO_BENCH_ROO
  return roo_pb::Serialize(value, buffer, capacity, size) ==
         roo_pb::Status::kOk;
#else
  pb_ostream_t output = pb_ostream_from_buffer(buffer, capacity);
  bool ok = pb_encode(&output, Descriptor<T>(), &value);
  size = output.bytes_written;
  return ok;
#endif
}

// Both calls replace an existing resident object and include default/presence
// reset in their normal parse API. Object allocation is not part of the timing.
template <typename T>
__attribute__((noinline)) bool Decode(T& value, const uint8_t* buffer,
                                      size_t size) {
#ifdef PROTO_BENCH_ROO
  bool ok = value.ParseFromArray(buffer, size);
#else
  pb_istream_t input = pb_istream_from_buffer(buffer, size);
  bool ok = pb_decode(&input, Descriptor<T>(), &value) && input.bytes_left == 0;
#endif
  // Make decoded stores observable even in highly optimized benchmark builds.
  asm volatile("" : : "g"(&value) : "memory");
  return ok;
}

/// Holds one workload's resident source/destination and backend adapter.
template <typename T>
struct Workload {
  inline static T source{};
  inline static T destination{};

  /// Populates the source outside the timed region.
  static void Initialize() {
    source = T{};
    Fill(source);
  }

  /// Encodes the populated source into a caller-owned buffer.
  static bool Serialize(uint8_t* buffer, size_t capacity, size_t& size) {
    return Encode(source, buffer, capacity, size);
  }

  /// Replaces the destination from a caller-owned buffer.
  static bool Parse(const uint8_t* buffer, size_t size) {
    return Decode(destination, buffer, size);
  }

  /// Re-encodes decoded state for an untimed semantic round-trip check.
  static bool Reencode(uint8_t* buffer, size_t capacity, size_t& size) {
    return Encode(destination, buffer, capacity, size);
  }
};

/// Describes a workload without coupling the timing loop to a generated type.
struct Case {
  const char* name;
  size_t object_size;
  void (*initialize)();
  bool (*encode)(uint8_t*, size_t, size_t&);
  bool (*decode)(const uint8_t*, size_t);
  bool (*reencode)(uint8_t*, size_t, size_t&);
};

template <typename T>
constexpr Case MakeCase(const char* name) {
  return {name,
          sizeof(T),
          Workload<T>::Initialize,
          Workload<T>::Serialize,
          Workload<T>::Parse,
          Workload<T>::Reencode};
}

inline constexpr Case kCases[] = {
    MakeCase<Sample>("sample"),       MakeCase<Flat>("flat_scalars"),
    MakeCase<Telemetry>("telemetry"), MakeCase<Tree>("wide_tree"),
    MakeCase<Chain1>("chain_1"),      MakeCase<Chain4>("chain_4"),
    MakeCase<Chain8>("chain_8"),      MakeCase<Chain16>("chain_16"),
};

// Hashes exact wire bytes so captured MCU and host runs can be compared.
inline uint32_t Hash(const uint8_t* bytes, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * 16777619u;
  }
  return hash;
}

// Checks determinism, full round trips and resource failures before timing.
inline bool Validate(const Case& test, uint8_t* wire, size_t& size) {
  uint8_t scratch[kCapacity];
  size_t second = 0;
  test.initialize();
  if (!test.encode(wire, kCapacity, size) || size == 0 ||
      !test.decode(wire, size) ||
      !test.reencode(scratch, sizeof(scratch), second) || second != size ||
      std::memcmp(wire, scratch, size) != 0) {
    return false;
  }
  if (!test.encode(scratch, sizeof(scratch), second) || second != size ||
      std::memcmp(wire, scratch, size) != 0) {
    return false;
  }
  if (test.encode(scratch, size - 1, second)) {
    return false;
  }
  if (test.decode(wire, size - 1)) {
    return false;
  }
  return test.decode(wire, size);
}

}  // namespace benchmark
