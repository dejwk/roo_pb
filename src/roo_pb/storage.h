#pragma once

#include <array>
#include <cstring>
#include <new>
#include <utility>

#include "roo_pb/wire.h"

namespace roo_pb {

/// Owns at most N default-constructed elements without allocation.
template <typename T, size_t N>
class BoundedArray {
 public:
  /// Returns the live element count.
  size_t size() const { return size_; }

  /// Returns the maximum live element count.
  constexpr size_t capacity() const { return N; }

  /// Returns whether the array has no live elements.
  bool empty() const { return size_ == 0; }

  /// Returns contiguous storage, including unused capacity.
  T* data() { return values_.data(); }

  /// Returns contiguous storage, including unused capacity.
  const T* data() const { return values_.data(); }

  /// Returns a live element; the index must be in range.
  T& operator[](size_t index) {
    Require(index < size_);
    return values_[index];
  }

  /// Returns a live element; the index must be in range.
  const T& operator[](size_t index) const {
    Require(index < size_);
    return values_[index];
  }

  /// Returns the first live element iterator.
  T* begin() { return data(); }

  /// Returns the first live element iterator.
  const T* begin() const { return data(); }

  /// Returns the end of live elements.
  T* end() { return size_ == 0 ? data() : data() + size_; }

  /// Returns the end of live elements.
  const T* end() const { return size_ == 0 ? data() : data() + size_; }

  /// Checks capacity without changing live elements.
  bool reserve(size_t count) { return count <= N; }

  /// Changes size within capacity; removed elements release their resources.
  bool resize(size_t count) {
    if (count > N) {
      return false;
    }
    while (size_ > count) {
      values_[--size_] = T{};
    }
    while (size_ < count) {
      values_[size_++] = T{};
    }
    return true;
  }

  /// Appends a default element, returning nullptr on capacity exhaustion.
  T* add() {
    if (size_ == N) {
      return nullptr;
    }
    values_[size_] = T{};
    return &values_[size_++];
  }

  /// Appends @p value, returning false without change on capacity exhaustion.
  bool push_back(const T& value) {
    if (size_ == N) {
      return false;
    }
    values_[size_++] = value;
    return true;
  }

  /// Clears live elements and their owned resources.
  void clear() { resize(0); }

 private:
  // BasicString overwrites byte contents before publishing their live size.
  template <typename Storage>
  friend class BasicString;

  std::array<T, N> values_{};
  size_t size_ = 0;
};

/// Owns an explicitly heap-backed array using nothrow allocation.
/// Copy convenience operations enforce allocation success; reserve/add report
/// it.
template <typename T>
class DynamicArray {
 public:
  /// Creates empty storage without allocating.
  DynamicArray() = default;

  /// Copies @p other, enforcing successful allocation.
  DynamicArray(const DynamicArray& other) { Require(copyFrom(other)); }

  /// Transfers storage from @p other without allocating.
  DynamicArray(DynamicArray&& other) noexcept { swap(other); }

  /// Releases owned storage and elements.
  ~DynamicArray() { delete[] data_; }

  /// Copies @p other, enforcing successful allocation.
  DynamicArray& operator=(const DynamicArray& other) {
    if (this != &other) {
      Require(copyFrom(other));
    }
    return *this;
  }

  /// Transfers storage from @p other without allocating.
  DynamicArray& operator=(DynamicArray&& other) noexcept {
    if (this != &other) {
      DynamicArray temporary(std::move(other));
      swap(temporary);
    }
    return *this;
  }

  /// Exchanges ownership with @p other.
  void swap(DynamicArray& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
  }

  /// Copies @p other, leaving this array unchanged on allocation failure.
  bool copyFrom(const DynamicArray& other) {
    DynamicArray temporary;
    if (!temporary.resize(other.size_)) {
      return false;
    }
    for (size_t i = 0; i < other.size_; ++i) {
      temporary.data_[i] = other.data_[i];
    }
    swap(temporary);
    return true;
  }

  /// Returns the live element count.
  size_t size() const { return size_; }

  /// Returns allocated element capacity.
  size_t capacity() const { return capacity_; }

  /// Returns whether the array is empty.
  bool empty() const { return size_ == 0; }

  /// Returns contiguous storage.
  T* data() { return data_; }

  /// Returns contiguous storage.
  const T* data() const { return data_; }

  /// Returns an element with a checked index precondition.
  T& operator[](size_t index) {
    Require(index < size_);
    return data_[index];
  }

  /// Returns an element with a checked index precondition.
  const T& operator[](size_t index) const {
    Require(index < size_);
    return data_[index];
  }

  /// Returns the first live element iterator.
  T* begin() { return data_; }

  /// Returns the first live element iterator.
  const T* begin() const { return data_; }

  /// Returns the end of live elements.
  T* end() { return size_ == 0 ? data_ : data_ + size_; }

  /// Returns the end of live elements.
  const T* end() const { return size_ == 0 ? data_ : data_ + size_; }

  /// Grows capacity, retaining contents on failure; never shrinks storage.
  bool reserve(size_t count) {
    if (count <= capacity_) {
      return true;
    }
    if (count > SIZE_MAX / sizeof(T)) {
      return false;
    }
    T* next = new (std::nothrow) T[count];
    if (next == nullptr) {
      return false;
    }
    for (size_t i = 0; i < size_; ++i) {
      next[i] = std::move(data_[i]);
    }
    delete[] data_;
    data_ = next;
    capacity_ = count;
    return true;
  }

  /// Changes live size, reporting allocation failure without changing size.
  bool resize(size_t count) {
    if (!reserve(count)) {
      return false;
    }
    while (size_ > count) {
      data_[--size_] = T{};
    }
    while (size_ < count) {
      data_[size_++] = T{};
    }
    return true;
  }

  /// Appends a default element or returns nullptr on allocation failure.
  T* add() {
    if (size_ == SIZE_MAX) {
      return nullptr;
    }
    if (size_ == capacity_) {
      size_t maximum = SIZE_MAX / sizeof(T);
      size_t next = capacity_ > maximum / 2 ? maximum
                    : capacity_ == 0        ? 1
                                            : capacity_ * 2;
      if (next <= capacity_ || !reserve(next)) {
        return nullptr;
      }
    }
    data_[size_] = T{};
    return &data_[size_++];
  }

  /// Appends @p value or returns false on allocation failure.
  bool push_back(const T& value) {
    // Copy before growing: value can refer to an element in this array.
    T copy(value);
    T* next = add();
    if (next == nullptr) {
      return false;
    }
    *next = std::move(copy);
    return true;
  }

  /// Clears live elements while retaining allocated capacity.
  void clear() { resize(0); }

 private:
  template <typename Storage>
  friend class BasicString;

  T* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
};

/// Owns bytes with an explicit length and a trailing NUL for string use.
/// Storage is BoundedArray<char, N> or DynamicArray<char>. Discarded bytes
/// are not erased; clearing retains capacity without resetting byte contents.
template <typename Storage>
class BasicString {
 public:
  /// Returns byte length, excluding the trailing NUL.
  size_t size() const { return data_.size() == 0 ? 0 : data_.size() - 1; }

  /// Returns whether the byte sequence is empty.
  bool empty() const { return size() == 0; }

  /// Returns bytes valid until the next mutation.
  const char* data() const { return data_.empty() ? "" : data_.data(); }

  /// Returns a NUL-terminated view; embedded NUL bytes are preserved.
  const char* c_str() const { return data(); }

  /// Assigns @p count bytes, retaining the previous value on capacity failure.
  bool assign(const char* source, size_t count) {
    if ((source == nullptr && count != 0) || count == SIZE_MAX) {
      return false;
    }
    // Preserve aliased input across growth or resize by checking its offset.
    size_t offset = SIZE_MAX;
    for (size_t i = 0; i < data_.size(); ++i) {
      if (source == data_.data() + i) {
        offset = i;
        break;
      }
    }
    if (offset != SIZE_MAX && count > data_.size() - offset) {
      return false;
    }
    if (!data_.reserve(count + 1)) {
      return false;
    }
    if (offset != SIZE_MAX) {
      source = data_.data() + offset;
    }
    if (count != 0) {
      if (offset != SIZE_MAX) {
        std::memmove(data_.data(), source, count);
      } else {
        std::memcpy(data_.data(), source, count);
      }
    }
    // Byte storage needs no element reset: every newly live byte is written.
    data_.data()[count] = '\0';
    data_.size_ = count + 1;
    return true;
  }

  /// Assigns a NUL-terminated string; nullptr is rejected.
  bool assign(const char* source) {
    return source != nullptr && assign(source, std::strlen(source));
  }

  /// Clears contents while retaining capacity.
  void clear() { data_.size_ = 0; }

  /// Reads a field payload, retaining the previous value on failure.
  /// Invalid UTF-8 consumes the payload before recording the error.
  Status read(Input& input, size_t count, bool utf8) {
    if (input.status() != Status::kOk) {
      return input.status();
    }
    if (count == SIZE_MAX) {
      return input.fail(Status::kCapacity);
    }
    if (count > input.remaining()) {
      return input.fail(Status::kTruncated);
    }
    const char* source = reinterpret_cast<const char*>(input.data());
    if (utf8 && !IsUtf8(source, count)) {
      input.advance(count);
      return input.fail(Status::kInvalidUtf8);
    }
    if (!assign(source, count)) {
      return input.fail(Status::kCapacity);
    }
    return input.advance(count);
  }

  /// Compares byte content, including embedded NUL bytes.
  bool operator==(const BasicString& other) const {
    return size() == other.size() &&
           std::memcmp(data(), other.data(), size()) == 0;
  }

  /// Compares byte content.
  bool operator!=(const BasicString& other) const { return !(*this == other); }

 private:
  Storage data_;
};

/// Bounded byte/string storage; N excludes the convenience terminator.
template <size_t N>
using BoundedString = BasicString<BoundedArray<char, N + 1>>;

/// Explicit dynamically allocated byte/string storage.
using DynamicString = BasicString<DynamicArray<char>>;

}  // namespace roo_pb
