#include <cassert>
#include <type_traits>

#include "nanopb_compat.pb.h"

namespace {

// Verifies string/bytes limits, narrowing, and wire encoding from annotations.
void BoundedFields() {
  nanopb_test::Sample value;
  static_assert(
      std::is_same<decltype(value.readings(0)), const int8_t&>::value);
  assert(value.try_set_name("abcd"));
  assert(!value.try_set_name("abcde"));
  assert(value.try_set_blob("abcde"));
  assert(!value.try_set_blob("abcdef"));
  assert(value.try_set_label("abcde"));
  assert(!value.try_set_label("abcdef"));
  assert(value.try_add_readings(-128));
  assert(value.try_add_readings(127));
  assert(!value.try_add_readings(0));
  unsigned char bytes[nanopb_test::Sample::kMaxEncodedSize];
  size_t written = 0;
  assert(value.SerializeToArray(bytes, sizeof(bytes), written));
  nanopb_test::Sample copy;
  assert(copy.ParseFromArray(bytes, written));
  assert(copy.name().view() == "abcd");
  assert(copy.blob().view() == "abcde");
  assert(copy.label().view() == "abcde");
  assert(copy.readings(0) == -128 && copy.readings(1) == 127);

  // A nanopb max_size of five admits only four string payload bytes.
  const unsigned char too_long[] = {10, 5, 'a', 'b', 'c', 'd', 'e'};
  assert(roo_pb::Parse(too_long, sizeof(too_long), copy) ==
         roo_pb::Status::kCapacity);
  const unsigned char too_many[] = {26, 3, 0, 0, 0};
  assert(roo_pb::Parse(too_many, sizeof(too_many), copy) ==
         roo_pb::Status::kCapacity);
  const unsigned char too_wide[] = {24, 128, 2};  // sint32 128
  assert(roo_pb::Parse(too_wide, sizeof(too_wide), copy) ==
         roo_pb::Status::kCapacity);
}

// Verifies fixed byte length and repeated count use roo's existing constraints.
void FixedFields() {
  nanopb_test::Fixed value;
  value.set_token("abc");
  value.add_pair(1);
  assert(!value.IsInitialized());
  value.add_pair(2);
  assert(value.IsInitialized());
  unsigned char bytes[nanopb_test::Fixed::kMaxEncodedSize];
  size_t written = 0;
  assert(value.SerializeToArray(bytes, sizeof(bytes), written));
  nanopb_test::Fixed copy;
  assert(copy.ParseFromArray(bytes, written));
  assert(copy.token().view() == "abc" && copy.pair_size() == 2);
  const unsigned char short_token[] = {10, 2, 'a', 'b'};
  assert(roo_pb::Parse(short_token, sizeof(short_token), copy) ==
         roo_pb::Status::kCapacity);
}

// Consumes a nested-message payload through roo's callback interface.
roo_pb::Status DecodeChild(void* context, roo_pb::Input& input, uint32_t) {
  *static_cast<bool*>(context) = true;
  return input.advance(input.remaining());
}

// Verifies inline callbacks break recursive storage and retain their bindings.
void CallbackFields() {
  nanopb_test::Node value;
  bool called = false;
  value.set_child_decoder({&called, DecodeChild});
  const unsigned char child[] = {10, 0};
  assert(value.ParseFromArray(child, sizeof(child)));
  assert(called);
  value.Clear();
  called = false;
  assert(value.ParseFromArray(child, sizeof(child)));
  assert(called);
}

}  // namespace

int main() {
  BoundedFields();
  FixedFields();
  CallbackFields();
}
