#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "roo_io/data/zigzag.h"
#include "roo_io/text/unicode.h"

namespace roo_pb {

/// Reports protocol or resource failure without exceptions.
enum class Status {
  kOk,
  kMalformed,
  kTruncated,
  kCapacity,
  kDepth,
  kMissingRequired,
  kInvalidUtf8,
  kCallback,
  kSizeMismatch,
};

/// Protobuf's numeric wire encodings, independent of schema scalar kinds.
enum class WireType : uint8_t {
  kVarint = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kStartGroup = 3,
  kEndGroup = 4,
  kFixed32 = 5,
};

/// Extracts a tag's wire bits; Input::tag rejects the reserved values 6 and 7.
constexpr WireType GetWireType(uint32_t tag) {
  return static_cast<WireType>(tag & 7);
}

/// Enforces generated convenience API preconditions in release builds too.
inline void Require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

/// Limits input consumption and recursive parsing.
struct Limits {
  size_t max_bytes = 1024 * 1024;
  unsigned max_depth = 32;
};

/// Borrows a byte buffer; children share cursor/errors and finish before
/// parents.
class Input {
 public:
  /// Borrows exactly @p size bytes; nullptr is valid only for empty input.
  Input(const void* data, size_t size, Limits limits = {});

  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  /// Creates a bounded child sharing its parent's cursor and first error.
  Input(Input& parent, size_t size);

  /// Returns a child window that must be consumed before resuming this input.
  Input child(size_t size) { return Input(*this, size); }

  /// Returns nesting depth.
  unsigned depth() const { return depth_; }

  /// Returns resource limits.
  Limits limits() const { return limits_; }

  /// Returns the first error shared by the entire parse operation.
  Status status() const { return root_->error_; }

  /// Returns the absolute consumed byte count.
  size_t position() const { return root_->position_; }

  /// Returns bytes left within this window.
  size_t remaining() const {
    return position() <= end_ ? end_ - position() : 0;
  }

  /// Returns borrowed unread bytes; empty input can return nullptr.
  const unsigned char* data() const {
    return data_ == nullptr ? nullptr : data_ + position();
  }

  /// Records the first error.
  Status fail(Status status);

  /// Copies exactly @p size bytes, rejecting a short buffer before copying.
  Status bytes(void* output, size_t size);

  /// Advances by a checked byte count without copying.
  Status advance(size_t size);

  /// Reads a checked varint, leaving the destination unchanged on failure.
  Status varint(uint64_t& value);

  /// Reads a little-endian 32-bit value.
  Status fixed32(uint32_t& value);

  /// Reads a little-endian 64-bit value.
  Status fixed64(uint64_t& value);

  /// Reads a length checked against this window's remaining bytes.
  Status length(size_t& size);

  /// Reads a legal nonzero tag, or zero at this window's end.
  Status tag(uint32_t& tag);

  /// Skips and validates an unknown occurrence, including matching groups.
  Status skip(uint32_t tag, unsigned groups = 0);

 private:
  const unsigned char* data_;
  Input* root_ = this;
  size_t position_ = 0;
  size_t end_;
  unsigned depth_ = 0;
  Limits limits_;
  Status error_ = Status::kOk;
};

/// Prepends encoded values into a borrowed buffer, or counts without storage.
/// Each primitive retains normal wire byte order; call primitives/fields in
/// reverse wire order. The writer uses no heap allocation.
class Output {
 public:
  /// Creates a counter; bytes() accepts nullptr and never reads payload
  /// storage.
  Output() = default;

  /// Borrows @p capacity bytes; nullptr is valid only for zero capacity.
  Output(void* data, size_t capacity);

  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;

  /// Returns the first error.
  Status status() const { return error_; }

  /// Returns the encoded byte count, meaningful on success.
  size_t size() const { return size_; }

  /// Returns the encoded suffix; nullptr for a counter or empty null buffer.
  const unsigned char* data() const {
    return data_ == nullptr ? nullptr : data_ + capacity_ - size_;
  }

  /// Records the first error.
  Status fail(Status status);

  /// Prepends a block in original byte order. Source must not overlap output.
  Status bytes(const void* data, size_t size);

  /// Prepends a minimally encoded varint.
  Status varint(uint64_t value);

  /// Prepends a little-endian 32-bit value.
  Status fixed32(uint32_t value);

  /// Prepends a little-endian 64-bit value.
  Status fixed64(uint64_t value);

  /// Prepends a field tag, after writing its payload.
  Status tag(uint32_t number, WireType wire);

  /// Finishes by moving the suffix to the buffer start once.
  /// Further nonempty writes fail with kCapacity. Counters are unchanged.
  Status finish();

 private:
  bool reserve(size_t size);

  unsigned char* data_ = nullptr;
  size_t capacity_ = SIZE_MAX;
  size_t size_ = 0;
  Status error_ = Status::kOk;
};

/// Checks UTF-8 including overlong sequences, surrogates and codepoint limits.
inline bool IsUtf8(const char* data, size_t size) {
  return roo_io::IsValidUtf8(roo::string_view(data, size));
}

/// Computes ZigZag without signed overflow or signed shifts.
inline uint64_t ZigZag(int64_t value) { return roo_io::ZigZagEncode64(value); }

/// Decodes ZigZag without unsigned-to-signed overflow.
inline int64_t UnZigZag(uint64_t value) {
  return roo_io::ZigZagDecode64(value);
}

/// Replaces a message from an exact buffer; failure leaves partial state.
template <typename Message>
Status Parse(const void* data, size_t size, Message& message,
             Limits limits = {}) {
  Input input(data, size, limits);
  message.Clear();
  Status result = message.mergeFrom(input);
  return result == Status::kOk && !message.IsInitialized()
             ? Status::kMissingRequired
             : result;
}

/// Prepends a validated message without moving the writer's encoded suffix.
template <typename Message>
Status Serialize(const Message& message, Output& output) {
  if (!message.IsInitialized()) {
    return output.fail(Status::kMissingRequired);
  }
  return output.fail(message.serialize(output));
}

/// Writes at the buffer start and returns the encoded length.
/// Failure sets @p written to zero and can leave modified bytes in the buffer.
template <typename Message>
Status Serialize(const Message& message, void* data, size_t capacity,
                 size_t& written) {
  written = 0;
  Output output(data, capacity);
  Status result = Serialize(message, output);
  if (result != Status::kOk) {
    return result;
  }
  output.finish();
  written = output.size();
  return output.status();
}

}  // namespace roo_pb
