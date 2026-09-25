#pragma once

#include "roo_pb/callback.h"

namespace roo_pb {

/// Associates an extension's field number with borrowed encode/decode state.
struct ExtensionBinding {
  uint32_t number;
  EncodeCallback encoder;
  DecodeCallback decoder;
};

/// Routes extension occurrences by field number and optionally retains
/// unknowns. All bindings and their contexts are borrowed. Clear external state
/// explicitly before parsing a replacement message; merge operations retain it.
class ExtensionSet {
 public:
  /// Borrows @p count bindings and independent optional fallback handlers.
  ExtensionSet(const ExtensionBinding* bindings, size_t count,
               EncodeCallback fallback_encoder = {},
               DecodeCallback fallback_decoder = {})
      : bindings_(bindings),
        count_(count),
        fallback_encoder_(fallback_encoder),
        fallback_decoder_(fallback_decoder) {
    Require(bindings != nullptr || count == 0);
  }

  /// Returns the encoder to bind with set_unknown_fields_encoder().
  EncodeCallback encoder() { return {this, Encode}; }

  /// Returns the decoder to bind with set_unknown_fields_decoder().
  DecodeCallback decoder() { return {this, Decode}; }

 private:
  static Status Encode(void* context, Output& output, uint32_t) {
    ExtensionSet& self = *static_cast<ExtensionSet*>(context);
    self.fallback_encoder_.write(output, 0);
    for (size_t i = self.count_; i != 0 && output.status() == Status::kOk;
         --i) {
      self.bindings_[i - 1].encoder.write(output, self.bindings_[i - 1].number);
    }
    return output.status();
  }

  static Status Decode(void* context, Input& input, uint32_t tag) {
    ExtensionSet& self = *static_cast<ExtensionSet*>(context);
    for (size_t i = 0; i < self.count_; ++i) {
      if (self.bindings_[i].number == (tag >> 3)) {
        return self.bindings_[i].decoder.readUnknown(input, tag);
      }
    }
    return self.fallback_decoder_.readUnknown(input, tag);
  }

  const ExtensionBinding* bindings_;
  size_t count_;
  EncodeCallback fallback_encoder_;
  DecodeCallback fallback_decoder_;
};

}  // namespace roo_pb
