#include <Arduino.h>

#include "telemetry.pb.h"

void setup() {
  Serial.begin(115200);
  example::Device device;
  device.set_id(42);
  if (!device.try_set_name("kitchen") || !device.try_add_readings(-12)) {
    Serial.println("Field capacity exceeded");
    return;
  }
  device.set_online(true);
  unsigned char bytes[example::Device::kMaxEncodedSize];
  size_t size = 0;
  if (roo_pb::Serialize(device, bytes, sizeof(bytes), size) !=
      roo_pb::Status::kOk) {
    Serial.println("Encoding failed");
    return;
  }
  example::Device received;
  if (!received.ParseFromArray(bytes, size)) {
    Serial.println("Decoding failed");
    return;
  }
  Serial.printf("id=%u name=%s reading=%d bytes=%u\n", received.id(),
                received.name().c_str(), received.readings(0),
                static_cast<unsigned>(size));
}

void loop() {}
