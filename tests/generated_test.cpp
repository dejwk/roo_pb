#include <cassert>
#include <cstring>

#include "telemetry.pb.h"

// Verifies generated presence, packed repetition and oneof switching round
// trips.
int main() {
  example::Device device;
  device.set_id(42);
  device.set_name("kitchen");
  device.add_readings(-12);
  device.add_readings(0);
  device.set_reason("offline");
  device.set_online(false);
  assert(!device.has_reason());
  assert(device.has_online());
  unsigned char bytes[256];
  size_t size = 0;
  assert(device.SerializeToArray(bytes, sizeof(bytes), size));
  example::Device copy;
  assert(copy.ParseFromArray(bytes, size));
  assert(copy.id() == 42);
  assert(copy.has_name());
  assert(copy.name().view() == "kitchen");
  assert(copy.readings_size() == 2 && copy.readings(0) == -12);
  assert(copy.has_online() && !copy.online());
  // Verifies views preserve embedded NULs at full capacity on the wire.
  char full[32];
  std::memset(full, 'x', sizeof(full));
  full[1] = 0;
  device.set_name(roo::string_view(full, sizeof(full)));
  assert(device.SerializeToArray(bytes, sizeof(bytes), size));
  assert(copy.ParseFromArray(bytes, size));
  assert(copy.name().view() == roo::string_view(full, sizeof(full)));
  assert(!copy.try_set_name(
      roo::string_view("012345678901234567890123456789012", 33)));
  assert(copy.name().view() == device.name().view());
  device.set_reason(roo::string_view("a\0b", 3));
  device.set_reason(device.reason().view().substr(1));
  assert(device.reason().view() == roo::string_view("\0b", 2));
  copy.clear_name();
  assert(!copy.has_name());
  assert(!copy.try_set_name("this string has more than thirty two bytes"));
  // Unknown fields are skipped without changing known values.
  const unsigned char unknown[] = {8, 1, 0xa0, 6, 5};
  assert(copy.ParseFromArray(unknown, sizeof(unknown)));
  assert(copy.id() == 1);
}
