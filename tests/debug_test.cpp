#include <cassert>
#include <climits>
#include <string>

#include "modern.pb.h"

namespace {
roo_pb::Status ForbiddenEncode(void*, roo_pb::Output&, uint32_t) {
  assert(false && "DebugString must not invoke callbacks");
  return roo_pb::Status::kCallback;
}
roo_pb::Status ForbiddenDecode(void*, roo_pb::Input&, uint32_t) {
  assert(false && "DebugString must not invoke callbacks");
  return roo_pb::Status::kCallback;
}

// Verifies presence, escaping, repeated values, maps and selected oneofs.
void Fields() {
  test::Modern value;
  assert(value.DebugString() == "Modern { }");
  value.add_values(-12);
  value.add_values(0);
  value.set_note(roo::string_view("a\0\n\"\\", 5));
  value.set_narrow(0);
  value.set_token(roo::string_view("\x00\xff", 2));
  auto* entry = value.add_counts();
  entry->set_key("k");
  entry->set_value(7);
  assert(value.DebugString() ==
         "Modern { values: [-12, 0], counts: [CountsEntry { key: \"k\", value: "
         "7 }], "
         "note: \"a\\0\\n\\\"\\\\\", narrow: 0, token: 0x00ff }");
  value.mutable_child()->set_x(1);
  assert(value.DebugString().find("note:") == std::string::npos);
  assert(value.DebugString().find("child: Required { x: 1 }") !=
         std::string::npos);
}

// Verifies proto2 defaults are omitted until present, and enum fallback is
// numeric.
void DefaultsAndScalars() {
  test::Legacy legacy;
  assert(legacy.DebugString() == "Legacy { }");
  legacy.set_count(-7);
  legacy.set_state(test::State::READY);
  legacy.add_states(static_cast<test::State>(99));
  assert(legacy.DebugString() ==
         "Legacy { count: -7, state: READY, states: [99] }");
  test::Scalars scalars;
  scalars.set_i64(INT64_MIN);
  scalars.set_u64(UINT64_MAX);
  scalars.set_real64(1.25);
  scalars.set_flag(false);
  assert(scalars.DebugString() ==
         "Scalars { i64: -9223372036854775808, u64: 18446744073709551615, "
         "real64: 1.25, flag: false }");
}

// Verifies callbacks, unknown fields and extensions stay opaque and untouched.
void Callbacks() {
  test::Modern value;
  value.set_stream_encoder({nullptr, ForbiddenEncode});
  value.set_stream_decoder({nullptr, ForbiddenDecode});
  assert(value.DebugString() == "Modern { stream: <callback> }");
  test::Legacy legacy;
  legacy.set_unknown_fields_encoder({nullptr, ForbiddenEncode});
  assert(legacy.DebugString() == "Legacy { unknown_fields: <callback> }");
  test::CallbackChoice choice;
  choice.set_payload_decoder({nullptr, ForbiddenDecode});
  assert(choice.DebugString() == "CallbackChoice { }");
  choice.set_payload_encoder({nullptr, ForbiddenEncode});
  assert(choice.DebugString() == "CallbackChoice { payload: <callback> }");
}

// Verifies limits apply to the entire traversal and include truncation markers.
void Limits() {
  test::Modern value;
  value.set_dynamic(std::string(2000, 'x'));
  for (size_t limit = 0; limit != 40; ++limit) {
    roo_pb::DebugOptions options;
    options.max_bytes = limit;
    const std::string text = value.DebugString(options);
    assert(text.size() == limit);
    assert(text.substr(limit - (limit < 3 ? limit : 3)) ==
           std::string(limit < 3 ? limit : 3, '.'));
  }
  assert(value.DebugString().size() == 1024);
  value.Clear();
  value.add_values(1);
  value.add_values(2);
  value.set_note("hidden");
  roo_pb::DebugOptions options;
  options.max_elements = 2;
  assert(value.DebugString(options) == "Modern { values: [1...");
  value.Clear();
  value.mutable_child()->set_x(2);
  options = {};
  options.max_depth = 1;
  assert(value.DebugString(options) == "Modern { child: ...");
  options.max_depth = 0;
  assert(value.DebugString(options) == "...");
  options = {};
  const std::string exact = value.DebugString();
  options.max_bytes = exact.size();
  assert(value.DebugString(options) == exact);
}
}  // namespace

int main() {
  Fields();
  DefaultsAndScalars();
  Callbacks();
  Limits();
}
