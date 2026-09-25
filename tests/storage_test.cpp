#include "roo_pb/storage.h"

#include <cassert>
#include <cstring>

namespace {

// Verifies overwrite/clear retain capacity, terminators and failure atomicity.
template <typename String>
void StringReads() {
  String text;
  assert(text.assign("original"));
  const char* storage = text.data();
  text.clear();
  assert(text.empty() && text.c_str()[0] == '\0');
  const unsigned char bytes[] = {'a', 0, 'b'};
  roo_pb::Input input(bytes, sizeof(bytes));
  assert(text.read(input, sizeof(bytes), true) == roo_pb::Status::kOk);
  assert(text.data() == storage && text.size() == 3);
  assert(std::memcmp(text.data(), bytes, 3) == 0 && text.data()[3] == '\0');
  const unsigned char bad[] = {0xc0, 0x80};
  roo_pb::Input invalid(bad, sizeof(bad));
  assert(text.read(invalid, 2, true) == roo_pb::Status::kInvalidUtf8);
  assert(invalid.position() == 2 && text.size() == 3);
  assert(std::memcmp(text.data(), bytes, 3) == 0);
  roo_pb::Input short_input(bytes, 1);
  assert(text.read(short_input, 3, false) == roo_pb::Status::kTruncated);
  assert(short_input.position() == 0 && text.size() == 3);
  roo_pb::Input sticky(bytes, sizeof(bytes));
  sticky.fail(roo_pb::Status::kCallback);
  assert(text.read(sticky, 3, false) == roo_pb::Status::kCallback);
  assert(sticky.position() == 0 && text.size() == 3);
  roo_pb::Input aliased(text.data() + 1, 2);
  assert(text.read(aliased, 2, false) == roo_pb::Status::kOk);
  assert(text.size() == 2 && text.data()[0] == 0 && text.data()[1] == 'b');
  roo_pb::Input empty(nullptr, 0);
  assert(text.read(empty, 0, true) == roo_pb::Status::kOk);
  assert(text.empty() && text.c_str()[0] == '\0');
}

// Verifies string byte optimizations do not change general element resets.
void ArrayResets() {
  roo_pb::BoundedArray<roo_pb::DynamicString, 2> owned;
  assert(owned.add()->assign("owned"));
  owned.clear();
  assert(owned.data()[0].empty());
  assert(owned.add()->empty());
  roo_pb::BoundedArray<int, 2> integers;
  assert(integers.push_back(42));
  integers.clear();
  assert(integers.data()[0] == 0);
  assert(integers.resize(2) && integers[0] == 0 && integers[1] == 0);
}

}  // namespace

// Verifies capacity failures, embedded NULs, aliasing and dynamic ownership.
int main() {
  StringReads<roo_pb::BoundedString<8>>();
  StringReads<roo_pb::DynamicString>();
  ArrayResets();
  roo_pb::BoundedString<5> text;
  assert(text.assign("hello"));
  assert(!text.assign("longer"));
  assert(text.size() == 5);
  assert(text.assign(text.data() + 2, 3));
  assert(std::strcmp(text.c_str(), "llo") == 0);
  assert(text.assign("a\0b", 3));
  assert(text.size() == 3 && text.data()[2] == 'b');
  roo_pb::BoundedArray<int, 2> values;
  assert(values.push_back(4));
  assert(values.push_back(5));
  assert(!values.push_back(6));
  assert(values[0] == 4);
  roo_pb::DynamicArray<int> dynamic;
  assert(dynamic.push_back(42));
  assert(dynamic.push_back(dynamic[0]));
  roo_pb::DynamicArray<int> copy = dynamic;
  dynamic[0] = 7;
  assert(copy[0] == 42 && copy[1] == 42);
  assert(!dynamic.reserve(SIZE_MAX));
  roo_pb::DynamicString large;
  assert(large.assign("abcdef"));
  assert(large.assign(large.data() + 1, 4));
  assert(std::strcmp(large.data(), "bcde") == 0);
  roo_pb::BoundedArray<int, 0> empty;
  assert(empty.add() == nullptr);
}
