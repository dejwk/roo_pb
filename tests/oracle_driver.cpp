#include <cstdio>
#include <cstring>
#include <vector>

#include "modern.pb.h"
#include "telemetry.pb.h"

namespace {

// Parses Google-produced bytes and returns roo_pb's encoding for comparison.
template <typename Message>
int RoundTrip() {
  std::vector<unsigned char> bytes;
  unsigned char buffer[256];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof(buffer), stdin)) != 0) {
    bytes.insert(bytes.end(), buffer, buffer + count);
  }
  Message message;
  if (!message.ParseFromArray(bytes.data(), bytes.size())) {
    return 2;
  }
  bytes.resize(message.ByteSizeLong());
  if (!message.SerializeToArray(bytes.data(), bytes.size())) {
    return 3;
  }
  return std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size() ? 0
                                                                            : 4;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 1;
  }
  if (std::strcmp(argv[1], "scalars") == 0) {
    return RoundTrip<test::Scalars>();
  }
  if (std::strcmp(argv[1], "legacy") == 0) {
    return RoundTrip<test::Legacy>();
  }
  if (std::strcmp(argv[1], "device") == 0) {
    return RoundTrip<example::Device>();
  }
  if (std::strcmp(argv[1], "modern") == 0) {
    return RoundTrip<test::Modern>();
  }
  return 1;
}
