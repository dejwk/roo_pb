#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "roo_backport/string_view.h"

namespace roo_pb {

/// Bounds diagnostic output and traversal across an entire message.
struct DebugOptions {
  size_t max_bytes =
      1024;  ///< Maximum output bytes, including truncation dots.
  size_t max_elements = 128;  ///< Total fields and repeated elements visited.
  size_t max_depth = 8;       ///< Maximum message nesting, counting the root.
};

namespace internal {
/// Encodes one byte for single-line diagnostics; returns bytes written (1–4).
size_t EscapeDebugByte(unsigned char value, char* output);
}  // namespace internal

/// Shared formatter used by generated messages; owns one output string.
/// Limits stop traversal and append dots; truncated output need not be
/// balanced.
class DebugWriter {
 public:
  /// Creates an empty formatter with the supplied traversal and output limits.
  explicit DebugWriter(DebugOptions options) : options_(options) {}

  /// Appends literal diagnostic syntax within the output budget.
  void text(roo::string_view value);

  /// Starts a message, or stops at the nesting limit.
  bool beginMessage(roo::string_view name);

  /// Ends a message previously entered with beginMessage().
  void endMessage();

  /// Starts a field or list element and charges the shared element budget.
  bool element(bool& first, roo::string_view name = {});

  /// Appends escaped text, optionally surrounded by double quotes.
  void string(roo::string_view value, bool quoted = true);

  /// Appends bytes as a hexadecimal string prefixed with 0x.
  void bytes(roo::string_view value);

  /// Formats a signed integer in decimal.
  void number(int64_t value);

  /// Formats an unsigned integer in decimal.
  void number(uint64_t value);

  /// Formats a floating-point value with round-trip precision.
  void number(double value);

  /// Formats an arithmetic value without depending on stream flags.
  template <typename T>
  void scalar(T value) {
    if constexpr (std::is_same<T, bool>::value) {
      text(value ? "true" : "false");
    } else if constexpr (std::is_floating_point<T>::value) {
      number(static_cast<double>(value));
    } else if constexpr (std::is_signed<T>::value) {
      number(static_cast<int64_t>(value));
    } else {
      number(static_cast<uint64_t>(value));
    }
  }

  /// Transfers the completed output without copying its buffer.
  std::string finish() { return std::move(output_); }

 private:
  void truncate();
  DebugOptions options_;
  std::string output_;
  size_t elements_ = 0;
  size_t depth_ = 0;
  bool stopped_ = false;
};

}  // namespace roo_pb
