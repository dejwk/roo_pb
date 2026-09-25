#include <cassert>
#include <cstdint>

#include "modern.pb.h"
#include "telemetry.pb.h"

namespace {

// Produces deterministic bytes for adversarial parser smoke coverage.
uint32_t Next(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Verifies errors stay bounded for arbitrary tags, lengths and varint payloads.
void RandomInput() {
  uint32_t state = 7319;
  unsigned char bytes[128];
  for (unsigned trial = 0; trial < 20000; ++trial) {
    size_t length = Next(state) % sizeof(bytes);
    for (size_t i = 0; i < length; ++i) {
      bytes[i] = Next(state) & 255;
    }
    example::Device device;
    device.ParseFromArray(bytes, length);
    test::Legacy legacy;
    legacy.ParseFromArray(bytes, length);
  }
}

// Verifies reader budgets, nested groups and packed truncation fail cleanly.
void Limits() {
  const unsigned char nested[] = {11, 11, 11, 12, 12, 12};
  roo_pb::Limits limits;
  limits.max_depth = 2;
  roo_pb::Input input(nested, sizeof(nested), limits);
  uint32_t tag;
  input.tag(tag);
  assert(input.skip(tag) == roo_pb::Status::kDepth);
  limits.max_bytes = 1;
  roo_pb::Input oversized(nested, sizeof(nested), limits);
  assert(oversized.status() == roo_pb::Status::kCapacity);
  const unsigned char incomplete[] = {26, 1, 128};
  example::Device device;
  assert(!device.ParseFromArray(incomplete, sizeof(incomplete)));
  const unsigned char zero_tag[] = {0};
  assert(!device.ParseFromArray(zero_tag, sizeof(zero_tag)));
  const unsigned char wrong_group[] = {11, 20};
  assert(!device.ParseFromArray(wrong_group, sizeof(wrong_group)));
  const unsigned char invalid_string[] = {18, 2, 0xc0, 0x80};
  assert(!device.ParseFromArray(invalid_string, sizeof(invalid_string)));
}

// Deliberately leaves a streamed field unread to test exact payload boundaries.
roo_pb::Status LeavePayload(void*, roo_pb::Input&, uint32_t) {
  return roo_pb::Status::kOk;
}

// Verifies callback underconsumption cannot desynchronize subsequent tags.
void CallbackBoundary() {
  test::Modern message;
  message.set_stream_decoder({nullptr, LeavePayload});
  const unsigned char bytes[] = {42, 2, 1, 2, 64, 1};
  roo_pb::Input input(bytes, sizeof(bytes));
  assert(message.mergeFrom(input) == roo_pb::Status::kSizeMismatch);
  assert(input.position() == 2);
}

}  // namespace

int main() {
  RandomInput();
  Limits();
  CallbackBoundary();
}
