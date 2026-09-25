#include "roo_pb/wire.h"

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace {
using roo_pb::Input;
using roo_pb::Output;
using roo_pb::Status;
using roo_pb::WireType;

// Verifies enum values retain the protocol encoding and reject reserved types.
void WireTypes() {
  static_assert(std::is_same<std::underlying_type_t<WireType>, uint8_t>::value);
  static_assert(!std::is_convertible<unsigned, WireType>::value);
  const WireType types[] = {WireType::kVarint,          WireType::kFixed64,
                            WireType::kLengthDelimited, WireType::kStartGroup,
                            WireType::kEndGroup,        WireType::kFixed32};
  for (unsigned i = 0; i < 6; ++i) {
    unsigned char byte = 0;
    Output output(&byte, 1);
    assert(output.tag(1, types[i]) == Status::kOk);
    assert(byte == (8 | i));
    Input input(&byte, 1);
    uint32_t tag = 0;
    assert(input.tag(tag) == Status::kOk);
    assert(roo_pb::GetWireType(tag) == types[i]);
  }
  for (unsigned i : {6, 7, 255}) {
    Output output;
    assert(output.tag(1, static_cast<WireType>(i)) == Status::kMalformed);
    assert(output.size() == 0);
  }
  for (unsigned char byte : {14, 15}) {
    Input input(&byte, 1);
    uint32_t tag = 123;
    assert(input.tag(tag) == Status::kMalformed && tag == 0);
  }
}

// Verifies child windows cannot borrow sibling bytes and share sticky errors.
void ChildBoundary() {
  const unsigned char data[] = {0x80, 1, 42};
  Input parent(data, sizeof(data));
  auto child = parent.child(1);
  uint64_t value = 123;
  assert(child.varint(value) == Status::kTruncated);
  assert(value == 123 && parent.position() == 1);
  assert(parent.varint(value) == Status::kTruncated);
  assert(parent.position() == 1);
  Input other(data, sizeof(data));
  auto complete = other.child(2);
  assert(complete.varint(value) == Status::kOk && value == 128);
  assert(other.varint(value) == Status::kOk && value == 42);
  Input root(data, sizeof(data), {sizeof(data), 1});
  auto first = root.child(2);
  auto second = first.child(1);
  assert(second.status() == Status::kDepth && root.status() == Status::kDepth);
  Input bounded(data, sizeof(data));
  auto overflow = bounded.child(SIZE_MAX);
  assert(overflow.status() == Status::kTruncated);
}

// Verifies varint extrema, invalid tenth bytes, truncation, and nonminimal
// input.
void Varints() {
  const uint64_t values[] = {0,     1,     127,        128,
                             16383, 16384, UINT32_MAX, UINT64_MAX};
  for (uint64_t expected : values) {
    unsigned char bytes[12] = {};
    Output output(bytes + 1, 10);
    assert(output.varint(expected) == Status::kOk);
    assert(bytes[0] == 0 && bytes[11] == 0);
    Input input(output.data(), output.size());
    uint64_t actual = 0;
    assert(input.varint(actual) == Status::kOk && actual == expected);
    assert(input.remaining() == 0);
    for (size_t n = 0; n < output.size(); ++n) {
      Input truncated(output.data(), n);
      actual = 42;
      assert(truncated.varint(actual) == Status::kTruncated && actual == 42);
    }
  }
  unsigned char bytes[11];
  std::memset(bytes, 0xff, sizeof(bytes));
  Input malformed(bytes, sizeof(bytes));
  uint64_t value = 123;
  assert(malformed.varint(value) == Status::kMalformed);
  assert(value == 123 && malformed.position() == 10);
  const unsigned char nonminimal[] = {0x80, 0};
  Input accepted(nonminimal, sizeof(nonminimal));
  assert(accepted.varint(value) == Status::kOk && value == 0);
}

// Independent bit-at-a-time oracle for status, consumed bytes and value.
Status ReferenceVarint(const unsigned char* bytes, size_t size,
                       size_t& consumed, uint64_t& value) {
  uint64_t decoded = 0;
  for (unsigned i = 0; i < 10; ++i) {
    if (consumed == size) {
      return Status::kTruncated;
    }
    unsigned char byte = bytes[consumed++];
    if (i == 9 && byte > 1) {
      return Status::kMalformed;
    }
    decoded |= uint64_t(byte & 127) << (7 * i);
    if ((byte & 128) == 0) {
      value = decoded;
      return Status::kOk;
    }
  }
  return Status::kMalformed;
}

// Verifies both accumulator words, overlong forms, and sticky error/cursor
// behavior against an independent decoder, including every tenth byte.
void VarintWordBoundaries() {
  for (unsigned bit = 0; bit < 64; ++bit) {
    for (uint64_t value : {(UINT64_C(1) << bit) - 1, UINT64_C(1) << bit}) {
      unsigned char bytes[10];
      Output output(bytes, sizeof(bytes));
      assert(output.varint(value) == Status::kOk);
      assert(output.finish() == Status::kOk);
      size_t canonical = output.size();
      for (size_t width = canonical; width <= 10; ++width) {
        if (width > canonical) {
          bytes[width - 2] |= 128;
          bytes[width - 1] = 0;
        }
        Input input(bytes, width);
        uint64_t actual = ~value;
        assert(input.varint(actual) == Status::kOk && actual == value);
        assert(input.position() == width);
      }
    }
  }
  uint32_t random = 0x12345678;
  for (unsigned trial = 0; trial < 20000; ++trial) {
    unsigned char bytes[11];
    for (unsigned char& byte : bytes) {
      random = random * 1664525u + 1013904223u;
      byte = static_cast<unsigned char>(random >> 24);
    }
    size_t size = trial % 12;
    if (trial < 256) {
      std::memset(bytes, 0xff, 9);
      bytes[9] = static_cast<unsigned char>(trial);
      size = 11;
    }
    size_t consumed = 0;
    uint64_t expected = 42, actual = 42;
    Status status = ReferenceVarint(bytes, size, consumed, expected);
    Input parent(bytes, sizeof(bytes));
    auto input = parent.child(size);
    assert(input.varint(actual) == status);
    assert(actual == expected && parent.position() == consumed);
    if (status != Status::kOk) {
      assert(parent.varint(actual) == status && parent.position() == consumed);
    }
  }
}

// Verifies prepend order, little-endian bytes, exact capacities and atomic
// errors.
void FixedWidth() {
  unsigned char bytes[14] = {};
  Output output(bytes + 1, 12);
  assert(output.fixed64(UINT64_C(0x0807060504030201)) == Status::kOk);
  assert(output.fixed32(0x12345678) == Status::kOk);
  const unsigned char golden[] = {0x78, 0x56, 0x34, 0x12, 1, 2,
                                  3,    4,    5,    6,    7, 8};
  assert(std::memcmp(output.data(), golden, 12) == 0);
  assert(bytes[0] == 0 && bytes[13] == 0);
  Input input(output.data(), output.size());
  uint32_t small = 0;
  uint64_t large = 0;
  assert(input.fixed32(small) == Status::kOk && small == 0x12345678);
  assert(input.fixed64(large) == Status::kOk &&
         large == UINT64_C(0x0807060504030201));
  for (size_t capacity = 0; capacity < 8; ++capacity) {
    unsigned char short_bytes[8] = {};
    Output short_output(short_bytes, capacity);
    assert(short_output.fixed64(1) == Status::kCapacity);
    assert(short_output.size() == 0);
    for (unsigned char b : short_bytes) {
      assert(b == 0);
    }
    Input short_input(short_bytes, capacity);
    large = 123;
    assert(short_input.fixed64(large) == Status::kTruncated && large == 123);
    assert(short_input.position() == 0);
    if (capacity < 4) {
      Input narrow(short_bytes, capacity);
      small = 123;
      assert(narrow.fixed32(small) == Status::kTruncated && small == 123);
      Output narrow_output(short_bytes, capacity);
      assert(narrow_output.fixed32(1) == Status::kCapacity &&
             narrow_output.size() == 0);
    }
  }
}

// Verifies golden tag order, finishing, null buffers, and counting overflow.
void OutputContracts() {
  unsigned char bytes[32] = {};
  Output output(bytes, sizeof(bytes));
  output.varint(150);
  output.tag(1, WireType::kVarint);
  const unsigned char golden[] = {8, 0x96, 1};
  assert(output.size() == 3 && output.data() == bytes + 29);
  assert(std::memcmp(output.data(), golden, 3) == 0);
  assert(output.finish() == Status::kOk && output.data() == bytes);
  assert(std::memcmp(bytes, golden, 3) == 0);
  assert(output.varint(1) == Status::kCapacity);
  Output counter;
  assert(counter.bytes(nullptr, SIZE_MAX) == Status::kOk);
  assert(counter.size() == SIZE_MAX && counter.data() == nullptr);
  assert(counter.varint(1) == Status::kCapacity && counter.size() == SIZE_MAX);
  Output empty(nullptr, 0);
  assert(empty.bytes(nullptr, 0) == Status::kOk);
  assert(empty.varint(1) == Status::kCapacity);
  Output invalid(nullptr, 1);
  assert(invalid.status() == Status::kMalformed);
  Input bad(nullptr, 1);
  assert(bad.status() == Status::kMalformed);
  Input empty_input(nullptr, 0);
  uint32_t tag = 1;
  assert(empty_input.tag(tag) == Status::kOk && tag == 0);
  Output null_payload(bytes, sizeof(bytes));
  assert(null_payload.bytes(nullptr, 1) == Status::kMalformed);
  Output invalid_tag(bytes, sizeof(bytes));
  assert(invalid_tag.tag(0, WireType::kVarint) == Status::kMalformed);
}
}  // namespace

int main() {
  WireTypes();
  ChildBoundary();
  Varints();
  VarintWordBoundaries();
  FixedWidth();
  OutputContracts();
  assert(roo_pb::ZigZag(INT64_MIN) == UINT64_MAX);
  assert(roo_pb::UnZigZag(UINT64_MAX) == INT64_MIN);
  assert(roo_pb::IsUtf8("\xf0\x9f\x98\x80", 4));
  assert(!roo_pb::IsUtf8("\xc0\x80", 2));
  assert(!roo_pb::IsUtf8("\xed\xa0\x80", 3));
  assert(!roo_pb::IsUtf8("\xf4\x90\x80\x80", 4));
}
