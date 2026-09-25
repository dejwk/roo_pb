#include <cstdio>

#include "transfer.pb.h"

namespace {

/// Tracks received bytes without retaining the complete payload.
struct Payload {
  size_t received = 0;
  uint32_t checksum = 0;
};

// Prepends payload bytes, then length and tag, with one callback invocation.
roo_pb::Status Encode(void*, roo_pb::Output& output, uint32_t number) {
  for (unsigned i = 128; i != 0; --i) {
    unsigned char byte = static_cast<unsigned char>(i - 1);
    output.bytes(&byte, 1);
  }
  output.varint(128);
  output.tag(number, roo_pb::WireType::kLengthDelimited);
  return output.status();
}

// Consumes the exact bounded payload; the length prefix is already consumed.
roo_pb::Status Decode(void* context, roo_pb::Input& input, uint32_t) {
  Payload& payload = *static_cast<Payload*>(context);
  while (input.status() == roo_pb::Status::kOk && input.remaining() != 0) {
    unsigned char byte = 0;
    if (input.bytes(&byte, 1) != roo_pb::Status::kOk) {
      return input.status();
    }
    payload.checksum += byte;
    ++payload.received;
  }
  return input.status();
}

}  // namespace

// Demonstrates buffer callbacks and explicitly owned extension state.
int main() {
  transfer::Packet outgoing;
  outgoing.set_id(7);
  outgoing.set_payload_encoder({nullptr, Encode});
  transfer::ChecksumExtension checksum;
  checksum.set_checksum(127 * 128 / 2);
  roo_pb::ExtensionBinding binding = checksum.binding();
  roo_pb::ExtensionSet extensions(&binding, 1);
  outgoing.set_unknown_fields_encoder(extensions.encoder());

  unsigned char bytes[256];
  size_t length = 0;
  if (roo_pb::Serialize(outgoing, bytes, sizeof(bytes), length) !=
      roo_pb::Status::kOk) {
    return 1;
  }

  Payload payload;
  transfer::ChecksumExtension received_checksum;
  roo_pb::ExtensionBinding receiver_binding = received_checksum.binding();
  roo_pb::ExtensionSet receiver_extensions(&receiver_binding, 1);
  transfer::Packet incoming;
  incoming.set_payload_decoder({&payload, Decode});
  incoming.set_unknown_fields_decoder(receiver_extensions.decoder());
  if (!incoming.ParseFromArray(bytes, length)) {
    return 2;
  }
  if (!incoming.has_payload() || !received_checksum.has_value() ||
      payload.checksum != received_checksum.value()) {
    return 3;
  }
  std::printf("received=%zu checksum=%u\n", payload.received, payload.checksum);
  return 0;
}
