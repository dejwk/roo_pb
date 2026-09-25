#pragma once

#include <type_traits>

#include "roo_pb/callback.h"
#include "roo_pb/storage.h"

namespace roo_pb {

/// Identifies schema scalar encodings independently of their C++ storage type.
enum class Kind {
  kInt32,
  kInt64,
  kUint32,
  kUint64,
  kSint32,
  kSint64,
  kFixed32,
  kFixed64,
  kSfixed32,
  kSfixed64,
  kBool,
  kEnum,
  kFloat,
  kDouble,
  kString,
  kBytes,
  kMessage,
};

/// Returns the wire type associated with a schema kind.
constexpr WireType GetWireType(Kind kind) {
  return kind == Kind::kDouble || kind == Kind::kFixed64 ||
                 kind == Kind::kSfixed64
             ? WireType::kFixed64
         : kind == Kind::kString || kind == Kind::kBytes ||
                 kind == Kind::kMessage
             ? WireType::kLengthDelimited
         : kind == Kind::kFloat || kind == Kind::kFixed32 ||
                 kind == Kind::kSfixed32
             ? WireType::kFixed32
             : WireType::kVarint;
}

/// Converts unsigned bits to a signed value without overflowing a signed cast.
inline int64_t SignedValue(uint64_t bits) {
  return bits <= INT64_MAX ? static_cast<int64_t>(bits)
                           : -1 - static_cast<int64_t>(~bits);
}

/// Reads one untagged scalar, bytes field or nested message.
template <Kind K, typename T>
Status ReadValue(Input& input, T& value) {
  if constexpr (K == Kind::kString || K == Kind::kBytes ||
                K == Kind::kMessage) {
    size_t length = 0;
    if (input.length(length) != Status::kOk) {
      return input.status();
    }
    if constexpr (K == Kind::kMessage) {
      auto child = input.child(length);
      Status result = value.mergeFrom(child);
      if (result == Status::kOk && child.remaining() != 0) {
        return input.fail(Status::kSizeMismatch);
      }
      return input.fail(result);
    } else {
      return value.read(input, length, K == Kind::kString);
    }
  } else {
    uint64_t raw = 0;
    if constexpr (GetWireType(K) == WireType::kFixed64) {
      input.fixed64(raw);
    } else if constexpr (GetWireType(K) == WireType::kFixed32) {
      uint32_t bits = 0;
      input.fixed32(bits);
      raw = bits;
    } else {
      input.varint(raw);
    }
    if (input.status() != Status::kOk) {
      return input.status();
    }
    if constexpr (K == Kind::kDouble) {
      static_assert(
          sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
          "IEEE754 double required");
      std::memcpy(&value, &raw, 8);
    } else if constexpr (K == Kind::kFloat) {
      static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                    "IEEE754 float required");
      uint32_t bits = static_cast<uint32_t>(raw);
      std::memcpy(&value, &bits, 4);
    } else if constexpr (K == Kind::kBool) {
      value = raw != 0;
    } else if constexpr (K == Kind::kEnum) {
      uint32_t low = static_cast<uint32_t>(raw);
      int64_t number = low <= INT32_MAX
                           ? low
                           : static_cast<int64_t>(low) - (INT64_C(1) << 32);
      value = static_cast<T>(number);
    } else if constexpr (K == Kind::kSint32 || K == Kind::kSint64 ||
                         K == Kind::kInt32 || K == Kind::kInt64 ||
                         K == Kind::kSfixed32 || K == Kind::kSfixed64) {
      int64_t number;
      if constexpr (K == Kind::kSint32) {
        number = UnZigZag(static_cast<uint32_t>(raw));
      } else if constexpr (K == Kind::kSint64) {
        number = UnZigZag(raw);
      } else if constexpr (K == Kind::kInt32 || K == Kind::kSfixed32) {
        uint32_t low = static_cast<uint32_t>(raw);
        number = low <= INT32_MAX
                     ? low
                     : static_cast<int64_t>(low) - (INT64_C(1) << 32);
      } else {
        number = SignedValue(raw);
      }
      if (number < std::numeric_limits<T>::min() ||
          number > std::numeric_limits<T>::max()) {
        return input.fail(Status::kCapacity);
      }
      value = static_cast<T>(number);
    } else {
      if constexpr (K == Kind::kUint32) {
        raw = static_cast<uint32_t>(raw);
      }
      if (raw > std::numeric_limits<T>::max()) {
        return input.fail(Status::kCapacity);
      }
      value = static_cast<T>(raw);
    }
    return input.status();
  }
}

/// Prepends one untagged value, including any payload length prefix.
template <Kind K, typename T>
Status WriteValue(Output& output, const T& value) {
  if (output.status() != Status::kOk) {
    return output.status();
  }
  if constexpr (K == Kind::kMessage) {
    size_t start = output.size();
    Status result = value.serialize(output);
    if (result != Status::kOk) {
      return output.fail(result);
    }
    return output.varint(output.size() - start);
  } else if constexpr (K == Kind::kString || K == Kind::kBytes) {
    if constexpr (K == Kind::kString) {
      if (!IsUtf8(value.data(), value.size())) {
        return output.fail(Status::kInvalidUtf8);
      }
    }
    output.bytes(value.data(), value.size());
    return output.varint(value.size());
  } else if constexpr (K == Kind::kFloat) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return output.fixed32(bits);
  } else if constexpr (K == Kind::kDouble) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    return output.fixed64(bits);
  } else if constexpr (K == Kind::kSint32 || K == Kind::kSint64) {
    return output.varint(ZigZag(value));
  } else if constexpr (GetWireType(K) == WireType::kFixed32) {
    return output.fixed32(static_cast<uint32_t>(value));
  } else if constexpr (GetWireType(K) == WireType::kFixed64) {
    return output.fixed64(static_cast<uint64_t>(value));
  } else {
    return output.varint(static_cast<uint64_t>(value));
  }
}

/// Writes one tagged value.
template <Kind K, typename T>
Status WriteField(Output& output, uint32_t number, const T& value) {
  WriteValue<K>(output, value);
  return output.tag(number, GetWireType(K));
}

/// Prepends repeated elements in reverse traversal to preserve wire order.
template <Kind K, typename Container>
Status WriteRepeated(Output& output, uint32_t number, const Container& values,
                     bool packed) {
  if (values.empty()) {
    return output.status();
  }
  size_t start = output.size();
  for (size_t i = values.size(); i != 0 && output.status() == Status::kOk;
       --i) {
    if (packed) {
      WriteValue<K>(output, values[i - 1]);
    } else {
      WriteField<K>(output, number, values[i - 1]);
    }
  }
  if (packed) {
    output.varint(output.size() - start);
    output.tag(number, WireType::kLengthDelimited);
  }
  return output.status();
}

/// Validates and prepends an unknown occurrence, preserving its payload bytes.
/// The tag is normalized; input and output storage must not overlap.
inline Status CopyUnknown(Input& input, uint32_t tag, Output& output) {
  if (input.status() != Status::kOk) {
    return input.status();
  }
  if (output.status() != Status::kOk) {
    return input.fail(output.status());
  }
  const unsigned char* begin = input.data();
  size_t start = input.position();
  if (input.skip(tag) != Status::kOk) {
    return input.status();
  }
  output.bytes(begin, input.position() - start);
  output.varint(tag);
  return input.fail(output.status());
}

/// Forwards an unrecognized closed-enum value as an unpacked unknown field.
inline Status ForwardUnknownEnum(const DecodeCallback& callback, Input& parent,
                                 uint32_t number, int32_t value) {
  unsigned char encoded[10];
  Output output(encoded, sizeof(encoded));
  output.varint(static_cast<uint64_t>(static_cast<int64_t>(value)));
  Input input(output.data(), output.size());
  return parent.fail(callback.readUnknown(input, number << 3));
}

/// Owns bounded unknown-field bytes for explicit round-trip forwarding.
template <size_t N>
class UnknownFields {
 public:
  /// Drops retained bytes before parsing a replacement message.
  void clear() { size_ = 0; }

  /// Returns retained encoded byte count.
  size_t size() const { return size_; }

  /// Captures an occurrence, reporting capacity exhaustion.
  Status read(Input& input, uint32_t tag) {
    Output output(data_.data() + size_, N - size_);
    Status result = CopyUnknown(input, tag, output);
    if (result == Status::kOk) {
      output.finish();
      size_ += output.size();
    }
    return result;
  }

  /// Emits previously retained occurrences.
  Status write(Output& output) const {
    return output.bytes(data_.data(), size_);
  }

  /// Returns an encoder borrowing the retained unknown fields.
  EncodeCallback encoder() { return {this, Encode}; }

  /// Returns a decoder borrowing the storage for unknown occurrences.
  DecodeCallback decoder() { return {this, Decode}; }

 private:
  static Status Encode(void* context, Output& output, uint32_t) {
    return static_cast<UnknownFields*>(context)->write(output);
  }

  static Status Decode(void* context, Input& input, uint32_t tag) {
    return static_cast<UnknownFields*>(context)->read(input, tag);
  }

  std::array<char, N + 1> data_{};
  size_t size_ = 0;
};

}  // namespace roo_pb
