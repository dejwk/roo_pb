// Host buffer-only comparison; both runtimes use the same message workload.
#include <chrono>
#include <cstdio>

#include "telemetry.pb.h"

namespace {
using Clock = std::chrono::steady_clock;
volatile uint64_t checksum = 0;

// Reports amortized host latency, not MCU timing.
double NsPerMessage(Clock::time_point start, size_t count) {
  return std::chrono::duration<double, std::nano>(Clock::now() - start)
             .count() /
         count;
}

// Counts recursive encoding visits to expose nested sizing work.
struct Node {
  const Node* child = nullptr;
  mutable size_t visits = 0;
  bool IsInitialized() const { return true; }
#if BUFFER_API
  roo_pb::Status serialize(roo_pb::Output& output) const {
#else
  template <typename O>
  roo_pb::Status serialize(O& iterator) const {
    decltype(auto) output = roo_pb::MakeOutput(iterator);
#endif
      ++visits;
  if (child != nullptr) {
    return roo_pb::WriteField<roo_pb::Kind::kMessage>(output, 1, *child);
  }
  return roo_pb::WriteField<roo_pb::Kind::kUint32>(output, 1, uint32_t(42));
}
};  // namespace

// Exercises the same nested chain on the old and new output contexts.
bool EncodeNode(const Node& node, unsigned char* bytes, size_t capacity) {
#if BUFFER_API
  size_t written = 0;
  return roo_pb::Serialize(node, bytes, capacity, written) ==
         roo_pb::Status::kOk;
#else
  roo_io::MemoryOutputIterator sink(
      reinterpret_cast<roo_io::byte*>(bytes),
      reinterpret_cast<roo_io::byte*>(bytes) + capacity);
  roo_pb::Output<roo_io::MemoryOutputIterator> output(sink, capacity);
  return node.serialize(output) == roo_pb::Status::kOk;
#endif
}
}  // namespace

int main() {
  constexpr size_t kIterations = 200000;
  example::Device message;
  message.set_id(150);
  message.set_name("kitchen");
  message.add_readings(-1000);
  message.add_readings(12345);
  message.set_online(true);
  unsigned char bytes[example::Device::kMaxEncodedSize];
  size_t size = message.ByteSizeLong();
  if (!message.SerializeToArray(bytes, sizeof(bytes))) {
    return 1;
  }
  Clock::time_point start = Clock::now();
  for (size_t i = 0; i < kIterations; ++i) {
    example::Device decoded;
    if (!decoded.ParseFromArray(bytes, size)) {
      return 2;
    }
    checksum += decoded.id();
  }
  double parse_ns = NsPerMessage(start, kIterations);
  start = Clock::now();
  for (size_t i = 0; i < kIterations; ++i) {
    message.set_id(static_cast<uint32_t>(128 + i % 128));
    if (!message.SerializeToArray(bytes, sizeof(bytes))) {
      return 3;
    }
    checksum += bytes[1];
  }
  double serialize_ns = NsPerMessage(start, kIterations);
  std::printf("parse_ns=%.1f serialize_ns=%.1f device_size=%zu\n", parse_ns,
              serialize_ns, sizeof(message));
  for (size_t depth : {size_t(0), size_t(4), size_t(8), size_t(12)}) {
    Node nodes[13];
    for (size_t i = 0; i < depth; ++i) {
      nodes[i].child = &nodes[i + 1];
    }
    if (!EncodeNode(nodes[0], bytes, sizeof(bytes))) {
      return 4;
    }
    std::printf("depth=%zu leaf_visits=%zu ", depth, nodes[depth].visits);
    start = Clock::now();
    for (unsigned i = 0; i < 1000; ++i) {
      if (!EncodeNode(nodes[0], bytes, sizeof(bytes))) {
        return 5;
      }
      checksum += bytes[1];
    }
    std::printf("serialize_ns=%.1f\n", NsPerMessage(start, 1000));
  }
  std::printf("checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
