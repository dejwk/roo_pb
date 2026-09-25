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
  size_t size = device.ByteSizeLong();
  assert(device.SerializeToArray(bytes, sizeof(bytes)));
  example::Device copy;
  assert(copy.ParseFromArray(bytes, size));
  assert(copy.id() == 42);
  assert(copy.has_name());
  assert(std::strcmp(copy.name().c_str(), "kitchen") == 0);
  assert(copy.readings_size() == 2 && copy.readings(0) == -12);
  assert(copy.has_online() && !copy.online());
  copy.clear_name();
  assert(!copy.has_name());
  assert(!copy.try_set_name("this string has more than thirty two bytes"));
  // Unknown fields are skipped without changing known values.
  const unsigned char unknown[] = {8, 1, 0xa0, 6, 5};
  assert(copy.ParseFromArray(unknown, sizeof(unknown)));
  assert(copy.id() == 1);
}
