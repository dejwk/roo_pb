#include <cassert>
#include <string>

#if defined(__linux__)
#include <sstream>
#endif

#include "roo_pb/storage.h"

namespace {
// Verifies embedded zeros, controls and full-capacity strings are length-based.
template <typename String>
void Logging() {
  String value;
  assert(value.assign("a\0b\n\t\r\\\"\x01\x7f", 10));
  const std::string expected = "a\\0b\\n\\t\\r\\\\\\\"\\x01\\x7f";
#if defined(__linux__)
  std::ostringstream host;
  host << value;
  assert(host.str() == expected);
#endif
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  roo_logging::NullStream disabled;
  disabled << value;
  char buffer[128];
  roo_logging::Stream embedded(buffer, sizeof(buffer));
  embedded << value;
  assert(std::string(buffer, embedded.pcount()) == expected);
  char short_buffer[3];
  roo_logging::Stream short_stream(short_buffer, sizeof(short_buffer));
  short_stream << value;
  assert(short_stream.pcount() == 2 && short_buffer[0] == 'a' &&
         short_buffer[1] == '\\');
#endif
  value.clear();
#if defined(__linux__)
  std::ostringstream empty;
  empty << value;
  assert(empty.str().empty());
#endif
}
}  // namespace

int main() {
  Logging<roo_pb::BoundedString<10>>();
  Logging<roo_pb::DynamicString>();
}
