#include "roo_pb/debug.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace roo_pb {
namespace internal {
size_t EscapeDebugByte(unsigned char value, char* output) {
  output[0] = '\\';
  switch (value) {
    case 0:
      output[1] = '0';
      return 2;
    case '\n':
      output[1] = 'n';
      return 2;
    case '\r':
      output[1] = 'r';
      return 2;
    case '\t':
      output[1] = 't';
      return 2;
    case '\\':
      output[1] = '\\';
      return 2;
    case '"':
      output[1] = '"';
      return 2;
    default:
      break;
  }
  if (value < 32 || value == 127) {
    constexpr char hex[] = "0123456789abcdef";
    output[1] = 'x';
    output[2] = hex[value >> 4];
    output[3] = hex[value & 15];
    return 4;
  }
  output[0] = static_cast<char>(value);
  return 1;
}
}  // namespace internal

void DebugWriter::truncate() {
  if (stopped_) return;
  stopped_ = true;
  const size_t dots = std::min(size_t{3}, options_.max_bytes);
  if (output_.size() > options_.max_bytes - dots) {
    output_.resize(options_.max_bytes - dots);
  }
  output_.append(dots, '.');
}

void DebugWriter::text(roo::string_view value) {
  if (stopped_) return;
  const size_t available = options_.max_bytes - output_.size();
  if (value.size() > available) {
    output_.append(value.data(), available);
    truncate();
  } else if (!value.empty()) {
    output_.append(value.data(), value.size());
  }
}

bool DebugWriter::beginMessage(roo::string_view name) {
  if (stopped_) return false;
  if (depth_ >= options_.max_depth) {
    truncate();
    return false;
  }
  ++depth_;
  text(name);
  text(" {");
  return true;
}

void DebugWriter::endMessage() {
  text(" }");
  --depth_;
}

bool DebugWriter::element(bool& first, roo::string_view name) {
  if (stopped_) return false;
  if (elements_ >= options_.max_elements) {
    truncate();
    return false;
  }
  ++elements_;
  if (!first)
    text(", ");
  else if (!name.empty())
    text(" ");
  first = false;
  if (!name.empty()) {
    text(name);
    text(": ");
  }
  return !stopped_;
}

void DebugWriter::string(roo::string_view value, bool quoted) {
  if (quoted) text("\"");
  for (unsigned char c : value) {
    if (stopped_) break;
    char escaped[4];
    const size_t size = internal::EscapeDebugByte(c, escaped);
    text(roo::string_view(escaped, size));
  }
  if (quoted) text("\"");
}

void DebugWriter::bytes(roo::string_view value) {
  text("0x");
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (stopped_) break;
    const char pair[] = {hex[c >> 4], hex[c & 15]};
    text(roo::string_view(pair, 2));
  }
}

void DebugWriter::number(int64_t value) {
  char buffer[32];
  const int size = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
  text(roo::string_view(buffer, size));
}

void DebugWriter::number(uint64_t value) {
  char buffer[32];
  const int size = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
  text(roo::string_view(buffer, size));
}

void DebugWriter::number(double value) {
  char buffer[32];
  const int size = snprintf(buffer, sizeof(buffer), "%.17g", value);
  if (size > 0)
    text(roo::string_view(buffer, std::min(size_t(size), sizeof(buffer) - 1)));
}
}  // namespace roo_pb
