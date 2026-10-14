#include "roo_pb/storage.h"

#include <cassert>
#include <cstring>
#include <type_traits>

namespace {

// Verifies overwrite/clear retain capacity, embedded NULs and failure
// atomicity.
template <typename String>
void StringReads() {
  String text;
  assert(text.assign("original"));
  const char* storage = text.data();
  text.clear();
  assert(text.view().empty());
  const unsigned char bytes[] = {'a', 0, 'b'};
  roo_pb::Input input(bytes, sizeof(bytes));
  assert(text.read(input, sizeof(bytes), true) == roo_pb::Status::kOk);
  assert(text.data() == storage && text.size() == 3);
  assert(text.view() == roo::string_view("a\0b", 3));
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
  assert(text.view().empty());
}

// Returns its borrowed argument to exercise implicit function-argument
// conversion.
roo::string_view Borrow(roo::string_view value) { return value; }

// Verifies both storage types implicitly expose their complete bytes without
// copying.
template <typename String>
void ImplicitView() {
  static_assert(std::is_convertible<const String&, roo::string_view>::value);
  static_assert(!std::is_convertible<const String&, const char*>::value);
  String text;
  assert(Borrow(text).empty());
  assert(text.assign("a\0b", 3));
  const String& source = text;
  roo::string_view view = source;
  assert(view.data() == source.data());
  assert(view == roo::string_view("a\0b", 3));
  assert(Borrow(source) == view);
  roo_pb::BoundedString<3> copy;
  assert(copy.assign(source));
  assert(copy.view() == view);
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
  ImplicitView<roo_pb::BoundedString<3>>();
  ImplicitView<roo_pb::DynamicString>();
  // Verifies the bounded buffer has no extra terminator slot.
  static_assert(sizeof(roo_pb::BoundedString<8>) ==
                sizeof(roo_pb::BoundedArray<char, 8>));
  roo_pb::BoundedString<0> zero;
  assert(zero.assign(roo::string_view()));
  assert(zero.view().empty());
  assert(!zero.assign("x"));
  roo_pb::BoundedString<3> exact;
  assert(exact.assign(roo::string_view("a\0b", 3)));
  assert(exact.view() == roo::string_view("a\0b", 3));
  assert(!exact.assign(roo::string_view("four", 4)));
  assert(exact.view() == roo::string_view("a\0b", 3));
  assert(exact.assign(exact.view().substr(1)));
  assert(exact.view() == roo::string_view("\0b", 2));
  roo_pb::BoundedString<5> text;
  assert(text.assign("hello"));
  assert(!text.assign("longer"));
  assert(text.size() == 5);
  assert(text.assign(text.data() + 2, 3));
  assert(text.view() == "llo");
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
  assert(large.view() == "bcde");
  roo_pb::BoundedArray<int, 0> empty;
  assert(empty.add() == nullptr);
}
