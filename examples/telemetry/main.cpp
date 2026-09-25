#ifndef ARDUINO

#include <cstdio>

#include "telemetry.pb.h"

// Demonstrates bounded mutation and memory-buffer serialization without a heap.
int main() {
  example::Device device;
  device.set_id(42);
  if (!device.try_set_name("kitchen") || !device.try_add_readings(-12)) {
    return 1;
  }
  device.set_online(true);

  unsigned char buffer[example::Device::kMaxEncodedSize];
  size_t size = 0;
  if (roo_pb::Serialize(device, buffer, sizeof(buffer), size) !=
      roo_pb::Status::kOk) {
    return 2;
  }

  example::Device received;
  if (roo_pb::Parse(buffer, size, received) != roo_pb::Status::kOk) {
    return 3;
  }
  std::printf("id=%u name=%s reading=%d bytes=%zu\n", received.id(),
              received.name().c_str(), received.readings(0), size);
  return 0;
}

#endif  // ARDUINO
