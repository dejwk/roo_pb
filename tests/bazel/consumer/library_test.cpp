#include <cassert>
#include <cstring>

#include "base.pb.h"
#include "mid.pb.h"
#include "nested/consumer.pb.h"
#include "nested/other.pb.h"

// Verifies cross-package diamond imports, sidecars, nested paths, multiple
// sources, and transitive linkage of the runtime through roo_pb_library.
int main() {
  bazel_test::Consumer message;
  assert(message.mutable_base()->try_set_name("1234567"));
  assert(!message.mutable_base()->try_set_name("12345678"));
  message.mutable_mid()->mutable_base()->set_name("nested");
  unsigned char buffer[bazel_test::Consumer::kMaxEncodedSize];
  size_t size = 0;
  assert(roo_pb::Serialize(message, buffer, sizeof(buffer), size) ==
         roo_pb::Status::kOk);
  bazel_test::Consumer parsed;
  assert(parsed.ParseFromArray(buffer, size));
  assert(std::strcmp(parsed.base().name().c_str(), "1234567") == 0);
  assert(std::strcmp(parsed.mid().base().name().c_str(), "nested") == 0);
  bazel_test::Other other;
  other.set_id(42);
  assert(other.id() == 42);
}
