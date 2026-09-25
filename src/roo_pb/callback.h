#pragma once

#include "roo_pb/wire.h"

namespace roo_pb {

/// Borrows an encoder and its application context.
/// Encode prepends complete occurrences in reverse desired wire order.
/// Clear on a generated message preserves the binding.
struct EncodeCallback {
  using Encode = Status (*)(void*, Output&, uint32_t);
  void* context = nullptr;
  Encode encode = nullptr;

  /// Encodes complete occurrences, or emits nothing without a binding.
  Status write(Output& output, uint32_t number) const {
    if (encode == nullptr || output.status() != Status::kOk) {
      return output.status();
    }
    return output.fail(encode(context, output, number));
  }
};

/// Borrows a decoder and its independent application context.
/// Known length-delimited and fixed-width fields receive an exact payload
/// reader; other wire types receive the enclosing reader. Consume the entire
/// occurrence. Binding a decoder does not establish generated field presence.
struct DecodeCallback {
  using Decode = Status (*)(void*, Input&, uint32_t);
  void* context = nullptr;
  Decode decode = nullptr;

  /// Dispatches an unknown occurrence with its length prefix still unread.
  Status readUnknown(Input& input, uint32_t tag) const {
    if (input.status() != Status::kOk) {
      return input.status();
    }
    if (decode == nullptr) {
      return input.skip(tag);
    }
    size_t start = input.position();
    Status result = decode(context, input, tag);
    if (input.status() != Status::kOk) {
      result = input.status();
    }
    if (result == Status::kOk && input.position() == start) {
      result = Status::kCallback;
    }
    return input.fail(result);
  }

  /// Decodes a bounded payload, enforcing complete consumption and depth
  /// limits.
  Status read(Input& input, uint32_t tag) const {
    if (input.status() != Status::kOk) {
      return input.status();
    }
    if (decode == nullptr) {
      return input.skip(tag);
    }
    WireType wire = GetWireType(tag);
    if (wire != WireType::kLengthDelimited && wire != WireType::kFixed64 &&
        wire != WireType::kFixed32) {
      return readUnknown(input, tag);
    }
    size_t size = wire == WireType::kFixed64 ? 8 : 4;
    if (wire == WireType::kLengthDelimited &&
        input.length(size) != Status::kOk) {
      return input.status();
    }
    auto payload = input.child(size);
    if (payload.status() != Status::kOk) {
      return payload.status();
    }
    Status result = decode(context, payload, tag);
    if (payload.status() != Status::kOk) {
      result = payload.status();
    }
    if (result == Status::kOk && payload.remaining() != 0) {
      result = Status::kSizeMismatch;
    }
    return input.fail(result);
  }
};

}  // namespace roo_pb
