#include "roo_pb/wire.h"

namespace roo_pb {

Input::Input(const void* data, size_t size, Limits limits)
    : data_(static_cast<const unsigned char*>(data)),
      end_(size),
      limits_(limits) {
  if (data == nullptr && size != 0) {
    fail(Status::kMalformed);
  }
  if (size > limits.max_bytes) {
    fail(Status::kCapacity);
  }
}

Input::Input(Input& parent, size_t size)
    : data_(parent.data_),
      root_(parent.root_),
      end_(parent.position()),
      depth_(parent.depth_ + 1),
      limits_(parent.limits_) {
  if (size > parent.remaining()) {
    fail(Status::kTruncated);
  } else {
    end_ += size;
  }
  if (depth_ > limits_.max_depth) {
    fail(Status::kDepth);
  }
}

Status Input::fail(Status status) {
  if (root_->error_ == Status::kOk) {
    root_->error_ = status;
  }
  return root_->error_;
}

Status Input::advance(size_t size) {
  if (status() != Status::kOk) {
    return status();
  }
  if (size > remaining()) {
    return fail(Status::kTruncated);
  }
  root_->position_ += size;
  return status();
}

Status Input::bytes(void* output, size_t size) {
  if (status() != Status::kOk) {
    return status();
  }
  if (size > remaining()) {
    return fail(Status::kTruncated);
  }
  if (size != 0) {
    if (output == nullptr) {
      return fail(Status::kMalformed);
    }
    std::memcpy(output, data(), size);
  }
  return advance(size);
}

Status Input::varint(uint64_t& value) {
  if (status() != Status::kOk) {
    return status();
  }
  // Keep common short values and all variable shifts in 32-bit registers.
  // The fifth byte straddles the low/high words; the tenth has only one bit.
  uint32_t low = 0;
  for (unsigned shift = 0; shift < 28; shift += 7) {
    if (root_->position_ >= end_) {
      return fail(Status::kTruncated);
    }
    unsigned char byte = data_[root_->position_++];
    low |= uint32_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      value = low;
      return status();
    }
  }
  if (root_->position_ >= end_) {
    return fail(Status::kTruncated);
  }
  unsigned char byte = data_[root_->position_++];
  low |= uint32_t(byte & 0x0f) << 28;
  uint32_t high = (byte & 0x70) >> 4;
  if ((byte & 0x80) == 0) {
    value = (uint64_t(high) << 32) | low;
    return status();
  }
  for (unsigned shift = 3; shift <= 31; shift += 7) {
    if (root_->position_ >= end_) {
      return fail(Status::kTruncated);
    }
    byte = data_[root_->position_++];
    if (shift == 31 && byte > 1) {
      return fail(Status::kMalformed);
    }
    high |= uint32_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      value = (uint64_t(high) << 32) | low;
      return status();
    }
  }
  return fail(Status::kMalformed);
}

Status Input::fixed32(uint32_t& value) {
  if (status() != Status::kOk) {
    return status();
  }
  if (remaining() < 4) {
    return fail(Status::kTruncated);
  }
  const unsigned char* p = data();
  value = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
          (uint32_t(p[3]) << 24);
  return advance(4);
}

Status Input::fixed64(uint64_t& value) {
  if (status() != Status::kOk) {
    return status();
  }
  if (remaining() < 8) {
    return fail(Status::kTruncated);
  }
  uint64_t decoded = 0;
  for (unsigned i = 0; i < 8; ++i) {
    decoded |= uint64_t(data()[i]) << (8 * i);
  }
  value = decoded;
  return advance(8);
}

Status Input::length(size_t& size) {
  uint64_t value = 0;
  if (varint(value) != Status::kOk) {
    return status();
  }
  if (value > remaining()) {
    return fail(Status::kTruncated);
  }
  size = static_cast<size_t>(value);
  return status();
}

Status Input::tag(uint32_t& tag) {
  tag = 0;
  if (status() != Status::kOk || remaining() == 0) {
    return status();
  }
  uint64_t value = 0;
  if (varint(value) != Status::kOk) {
    return status();
  }
  if (value > UINT32_MAX || (value >> 3) == 0 ||
      GetWireType(static_cast<uint32_t>(value)) > WireType::kFixed32) {
    return fail(Status::kMalformed);
  }
  tag = static_cast<uint32_t>(value);
  return status();
}

Status Input::skip(uint32_t tag, unsigned groups) {
  if (status() != Status::kOk) {
    return status();
  }
  uint64_t ignored = 0;
  size_t count = 0;
  switch (GetWireType(tag)) {
    case WireType::kVarint:
      return varint(ignored);
    case WireType::kFixed64:
      return advance(8);
    case WireType::kLengthDelimited:
      if (length(count) != Status::kOk) {
        return status();
      }
      return advance(count);
    case WireType::kStartGroup:
      if (groups >= limits_.max_depth || depth_ >= limits_.max_depth - groups) {
        return fail(Status::kDepth);
      }
      while (status() == Status::kOk) {
        uint32_t nested = 0;
        if (this->tag(nested) != Status::kOk) {
          return status();
        }
        if (nested == 0) {
          return fail(Status::kTruncated);
        }
        if (GetWireType(nested) == WireType::kEndGroup) {
          return nested >> 3 == tag >> 3 ? status() : fail(Status::kMalformed);
        }
        skip(nested, groups + 1);
      }
      return status();
    case WireType::kFixed32:
      return advance(4);
    default:
      return fail(Status::kMalformed);
  }
}

Output::Output(void* data, size_t capacity)
    : data_(static_cast<unsigned char*>(data)), capacity_(capacity) {
  if (data == nullptr && capacity != 0) {
    fail(Status::kMalformed);
  }
}

Status Output::fail(Status status) {
  if (error_ == Status::kOk) {
    error_ = status;
  }
  return error_;
}

bool Output::reserve(size_t size) {
  if (error_ != Status::kOk) {
    return false;
  }
  if (size > capacity_ - size_) {
    fail(Status::kCapacity);
    return false;
  }
  size_ += size;
  return true;
}

Status Output::bytes(const void* data, size_t size) {
  if (error_ != Status::kOk) {
    return error_;
  }
  if (data_ != nullptr && data == nullptr && size != 0) {
    return fail(Status::kMalformed);
  }
  if (reserve(size) && data_ != nullptr && size != 0) {
    std::memcpy(data_ + capacity_ - size_, data, size);
  }
  return error_;
}

Status Output::varint(uint64_t value) {
  size_t width = 1;
  for (uint64_t rest = value; rest >= 128; rest >>= 7) {
    ++width;
  }
  if (reserve(width) && data_ != nullptr) {
    unsigned char* target = data_ + capacity_ - size_;
    for (size_t i = 0; i + 1 < width; ++i) {
      target[i] = static_cast<unsigned char>(value) | 0x80;
      value >>= 7;
    }
    target[width - 1] = static_cast<unsigned char>(value);
  }
  return error_;
}

Status Output::fixed32(uint32_t value) {
  if (reserve(4) && data_ != nullptr) {
    unsigned char* target = data_ + capacity_ - size_;
    for (unsigned i = 0; i < 4; ++i) {
      target[i] = static_cast<unsigned char>(value >> (8 * i));
    }
  }
  return error_;
}

Status Output::fixed64(uint64_t value) {
  if (reserve(8) && data_ != nullptr) {
    unsigned char* target = data_ + capacity_ - size_;
    for (unsigned i = 0; i < 8; ++i) {
      target[i] = static_cast<unsigned char>(value >> (8 * i));
    }
  }
  return error_;
}

Status Output::tag(uint32_t number, WireType wire) {
  if (number == 0 || number > 0x1fffffff || wire > WireType::kFixed32) {
    return fail(Status::kMalformed);
  }
  return varint((static_cast<uint64_t>(number) << 3) |
                static_cast<uint8_t>(wire));
}

Status Output::finish() {
  if (error_ == Status::kOk && data_ != nullptr) {
    if (size_ != 0) {
      std::memmove(data_, data(), size_);
    }
    capacity_ = size_;
  }
  return error_;
}

}  // namespace roo_pb
