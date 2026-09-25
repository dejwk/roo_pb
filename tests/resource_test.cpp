#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "modern.pb.h"
#include "telemetry.pb.h"

namespace {
size_t allocations = 0;
}

// Counts allocations to verify the bounded generated path requires no heap.
void* operator new(size_t size) {
  ++allocations;
  void* result = std::malloc(size == 0 ? 1 : size);
  if (result == nullptr) {
    std::abort();
  }
  return result;
}

void* operator new[](size_t size) { return ::operator new(size); }

void* operator new(size_t size, const std::nothrow_t&) noexcept {
  ++allocations;
  return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](size_t size, const std::nothrow_t& tag) noexcept {
  return ::operator new(size, tag);
}

void operator delete(void* data) noexcept { std::free(data); }

void operator delete[](void* data) noexcept { std::free(data); }

void operator delete(void* data, size_t) noexcept { std::free(data); }

void operator delete[](void* data, size_t) noexcept { std::free(data); }

void operator delete(void* data, const std::nothrow_t&) noexcept {
  std::free(data);
}

void operator delete[](void* data, const std::nothrow_t&) noexcept {
  std::free(data);
}

namespace {

test::ResetLeaf& MutableLeaf(test::ResetLeaf& value) { return value; }

template <typename T>
test::ResetLeaf& MutableLeaf(T& value) {
  return MutableLeaf(*value.mutable_child());
}

void CheckDefaults(const test::ResetLeaf& value) {
  assert(!value.has_text() && !value.has_number() && !value.has_state());
  assert(std::strcmp(value.text().c_str(), "seed") == 0);
  assert(value.number() == -7 && value.state() == test::State::READY);
}

template <typename T>
void CheckDefaults(const T& value) {
  assert(!value.has_child());
  CheckDefaults(value.child());
}

// Verifies a nested dynamic default is constructed once, and subsequent
// Clear/replacement parse reuse the resident child and its allocated storage.
template <typename T>
void NestedReset(unsigned depth) {
  size_t before = allocations;
  T value;
  std::fprintf(stderr, "depth=%u construction_allocations=%zu\n", depth,
               allocations - before);
  assert(allocations - before == 1);
  CheckDefaults(value);
  test::ResetLeaf& leaf = MutableLeaf(value);
  leaf.set_text("longer than the default");
  leaf.set_number(42);
  leaf.set_state(test::State::UNKNOWN);
  const char* storage = leaf.text().data();
  before = allocations;
  value.Clear();
  CheckDefaults(value);
  assert(leaf.text().data() == storage);
  assert(allocations == before);
  leaf.set_number(99);
  assert(value.ParseFromArray(nullptr, 0));
  CheckDefaults(value);
  assert(allocations == before);

  // Parsing an existing full chain also resets without allocating replacements.
  MutableLeaf(value).set_number(123);
  unsigned char wire[256];
  size_t written = 0;
  assert(roo_pb::Serialize(value, wire, sizeof(wire), written) ==
         roo_pb::Status::kOk);
  assert(value.ParseFromArray(wire, written));
  assert(MutableLeaf(value).number() == 123);
  assert(allocations == before);
}

}  // namespace

// Verifies repeated replacement parsing reuses dynamic string capacity.
void ResidentStringAllocations() {
  test::Modern message;
  message.set_dynamic("a longer resident allocation");
  message.add_pair(1);
  message.add_pair(2);
  const char* storage = message.dynamic().data();
  unsigned char wire[128];
  size_t size = 0;
  assert(roo_pb::Serialize(message, wire, sizeof(wire), size) ==
         roo_pb::Status::kOk);
  size_t before = allocations;
  for (unsigned i = 0; i < 10; ++i) {
    assert(message.ParseFromArray(wire, size));
    assert(message.dynamic().data() == storage && allocations == before);
  }
}

// Verifies construction, bounded mutation and buffer round trip allocate
// nothing.
int main() {
  ResidentStringAllocations();
  size_t before = allocations;
  example::Device device;
  device.set_name("bounded");
  device.add_readings(-1);
  device.set_online(true);
  unsigned char buffer[example::Device::kMaxEncodedSize];
  assert(device.SerializeToArray(buffer, sizeof(buffer)));
  example::Device copy;
  assert(copy.ParseFromArray(buffer, device.ByteSizeLong()));
  assert(allocations == before);
  std::printf("Device sizeof=%zu max_wire=%zu; bounded path allocations=0\n",
              sizeof(device), example::Device::kMaxEncodedSize);
  NestedReset<test::Reset16>(16);
  NestedReset<test::Reset8>(8);
  NestedReset<test::Reset4>(4);
  NestedReset<test::ResetLeaf>(0);
}
