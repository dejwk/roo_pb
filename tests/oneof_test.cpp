#include <cassert>
#include <cstring>
#include <utility>

#include "modern.pb.h"

namespace {

using test::OneofStorage;

// Verifies storage scales with the largest alternative, not their sum.
static_assert(sizeof(OneofStorage) <= sizeof(roo_pb::BoundedString<256>) +
                                          sizeof(roo_pb::DynamicString) + 32);

// Verifies overlapping storage, safe inactive reads and non-destructive clears.
void SharedStorage() {
  OneofStorage value;
  assert(value.value_case() == OneofStorage::ValueCase::kNotSet);
  assert(value.large().empty() && value.small().empty());
  assert(value.dynamic().empty() && !value.child().has_x());
  assert(value.number() == 0 && value.state() == test::State::UNKNOWN);
  const void* large = value.mutable_large();
  value.set_large("payload");
  value.clear_small();
  assert(value.has_large() &&
         std::strcmp(value.large().c_str(), "payload") == 0);
  const void* small = value.mutable_small();
  assert(large == small);
  assert(value.large().empty() && value.small().empty());
  value.set_small("small");
  char oversized[257] = {};
  assert(!value.try_set_large(oversized, sizeof(oversized)));
  assert(value.has_small() && std::strcmp(value.small().c_str(), "small") == 0);
  value.set_large(value.small().data(), value.small().size());
  assert(value.has_large() && std::strcmp(value.large().c_str(), "small") == 0);
  value.clear_value();
  assert(!value.has_large() && value.large().empty());
  value.set_number(42);
  value.set_state(test::State::READY);
  assert(value.number() == 0 && value.state() == test::State::READY);
}

// Verifies deep copies, ownership transfers, independent groups and
// self-assign.
void CopyAndMove() {
  for (int alternative = 0; alternative != 7; ++alternative) {
    OneofStorage source;
    switch (alternative) {
      case 1:
        source.set_large("large");
        break;
      case 2:
        source.set_small("small");
        break;
      case 3:
        source.mutable_child()->set_x(11);
        source.mutable_child()->set_y(12);
        break;
      case 4:
        source.set_dynamic("heap-backed");
        break;
      case 5:
        source.set_number(123456789);
        break;
      case 6:
        source.set_state(test::State::READY);
        break;
    }
    source.set_label("other group");
    source.set_ordinary(99);
    OneofStorage copy(source);
    assert(copy.value_case() == source.value_case());
    assert(copy.label() == source.label());
    assert(copy.label().data() != source.label().data());
    assert(copy.has_ordinary() && copy.ordinary() == 99);
    if (source.has_dynamic()) {
      assert(copy.dynamic().data() != source.dynamic().data());
    }
    OneofStorage assigned;
    assigned.set_dynamic("discarded");
    assigned = source;
    const char* label = copy.label().data();
    OneofStorage moved(std::move(copy));
    assert(moved.label().data() == label);
    OneofStorage target;
    target.set_large("discarded");
    target.set_label("discarded");
    target = std::move(moved);
    assert(target.label().data() == label);
    OneofStorage* same = &target;
    target = *same;
    target = std::move(*same);
    assert(target.label().data() == label);
    unsigned char expected[512], actual[512];
    size_t expected_size = 0, actual_size = 0;
    assert(roo_pb::Serialize(source, expected, sizeof(expected),
                             expected_size) == roo_pb::Status::kOk);
    for (const OneofStorage* result : {&assigned, &target}) {
      assert(roo_pb::Serialize(*result, actual, sizeof(actual), actual_size) ==
             roo_pb::Status::kOk);
      assert(actual_size == expected_size);
      assert(std::memcmp(actual, expected, actual_size) == 0);
    }
    copy.set_number(7);
    moved.Clear();
    target.Clear();
    target.Clear();
    assert(target.value_case() == OneofStorage::ValueCase::kNotSet);
    assert(target.other_case() == OneofStorage::OtherCase::kNotSet);
    assert(!target.has_ordinary());
  }
}

// Verifies same-member messages merge, switching ends their lifetime, and
// malformed parsing leaves a safely destructible partial value.
void ParseAndMerge() {
  OneofStorage value;
  const unsigned char child_x[] = {0x1a, 2, 8, 17};
  const unsigned char child_y[] = {0x1a, 2, 16, 23};
  assert(value.mergeFrom(child_x, sizeof(child_x)) == roo_pb::Status::kOk);
  assert(!value.IsInitialized());
  assert(value.mergeFrom(child_y, sizeof(child_y)) == roo_pb::Status::kOk);
  assert(value.IsInitialized() && value.child().x() == 17 &&
         value.child().y() == 23);
  value.set_dynamic("release me");
  assert(value.mergeFrom(child_y, sizeof(child_y)) == roo_pb::Status::kOk);
  assert(!value.child().has_x() && value.child().has_y());
  const unsigned char malformed[] = {0x1a, 1, 8};
  assert(!value.ParseFromArray(malformed, sizeof(malformed)));
  value.set_label("independent");
  value.clear_value();
  assert(value.has_label());
  value.Clear();
}

// Verifies nested container operations and repeated destruction of heap-backed
// alternatives; sanitizers check for leaks, double frees and use-after-free.
void NestedOwnership() {
  test::OneofCollection collection;
  collection.add_items()->set_dynamic("first");
  collection.add_items()->set_label("second");
  test::OneofCollection copy(collection);
  collection.Clear();
  assert(std::strcmp(copy.items(0).dynamic().c_str(), "first") == 0);
  test::OneofCollection moved(std::move(copy));
  moved.Clear();
  for (int i = 0; i != 100; ++i) {
    OneofStorage value;
    value.set_dynamic("first allocation");
    value.set_number(1);
    value.set_dynamic("second allocation");
    value.set_label("independent allocation");
    value.clear_dynamic();
    value.set_dynamic("destroy with object");
  }
}

// Verifies inactive callback registrations survive union switching and copies.
void CallbackBindings() {
  int context = 0;
  test::CallbackChoice source;
  source.set_payload_encoder({&context, nullptr});
  source.set_other_decoder({&context, nullptr});
  source.set_number(42);
  test::CallbackChoice copy(source);
  assert(copy.has_number() && copy.number() == 42);
  assert(copy.payload_encoder().context == &context);
  assert(copy.other_decoder().context == &context);
  copy.set_payload_encoder(copy.payload_encoder());
  assert(copy.has_payload() && copy.number() == 0);
  test::CallbackChoice moved(std::move(copy));
  assert(moved.has_payload());
  moved.Clear();
  assert(moved.payload_encoder().context == &context);
  assert(moved.other_decoder().context == &context);
}

}  // namespace

int main() {
  SharedStorage();
  CopyAndMove();
  ParseAndMerge();
  NestedOwnership();
  CallbackBindings();
}
