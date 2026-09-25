#include <cassert>
#include <cmath>
#include <cstring>

#include "modern.pb.h"

using roo_pb::Status;

namespace {

// Verifies absent defaults differ from explicitly present default values.
void DefaultsAndMerge() {
  test::Legacy message;
  assert(!message.has_count() && message.count() == -7);
  assert(!message.has_label() &&
         std::strcmp(message.label().data(), "hello") == 0);
  const unsigned char merged[] = {0x1a, 2, 8, 1, 0x1a, 2, 16, 2};
  assert(message.ParseFromArray(merged, sizeof(merged)));
  assert(message.child().x() == 1 && message.child().y() == 2);
  assert(!message.ParseFromArray(merged, 4));
  message.Clear();
  message.set_count(-7);
  assert(message.has_count() && message.ByteSizeLong() == 11);
  const unsigned char enum_values[] = {32, 99, 42, 3, 0, 99, 1};
  assert(message.ParseFromArray(enum_values, sizeof(enum_values)));
  assert(!message.has_state() && message.state() == test::State::READY);
  assert(message.states_size() == 2);
}

// Verifies every scalar kind at integer extrema and floating point values.
void Scalars() {
  test::Scalars original;
  original.set_i32(INT32_MIN);
  original.set_i64(INT64_MIN);
  original.set_u32(UINT32_MAX);
  original.set_u64(UINT64_MAX);
  original.set_s32(INT32_MIN);
  original.set_s64(INT64_MIN);
  original.set_f32(UINT32_MAX);
  original.set_f64(UINT64_MAX);
  original.set_sf32(INT32_MIN);
  original.set_sf64(INT64_MIN);
  original.set_real32(1.25f);
  original.set_real64(-3.5);
  original.set_flag(true);
  original.set_payload("a\0b", 3);
  unsigned char bytes[256];
  assert(original.SerializeToArray(bytes, sizeof(bytes)));
  test::Scalars copy;
  assert(copy.ParseFromArray(bytes, original.ByteSizeLong()));
  assert(copy.i32() == INT32_MIN && copy.i64() == INT64_MIN);
  assert(copy.u32() == UINT32_MAX && copy.u64() == UINT64_MAX);
  assert(copy.s32() == INT32_MIN && copy.s64() == INT64_MIN);
  assert(copy.f32() == UINT32_MAX && copy.f64() == UINT64_MAX);
  assert(copy.sf32() == INT32_MIN && copy.sf64() == INT64_MIN);
  assert(copy.real32() == 1.25f && copy.real64() == -3.5 && copy.flag());
  assert(copy.payload().size() == 3 && copy.payload().data()[2] == 'b');
}

// Verifies mixed packed input, map replacement and same-member oneof merge.
void Collections() {
  const unsigned char bytes[] = {8,   1,  10, 2,  2,  3,  8,  4,   18, 5, 10, 1,
                                 'a', 16, 1,  18, 5,  10, 1,  'a', 16, 2, 26, 2,
                                 8,   1,  26, 2,  16, 2,  82, 2,   5,  6};
  test::Modern message;
  assert(message.ParseFromArray(bytes, sizeof(bytes)));
  assert(message.values_size() == 4 && message.values(3) == 4);
  assert(message.counts_size() == 1 && message.counts(0).value() == 2);
  assert(message.child().x() == 1 && message.child().y() == 2);
  assert(message.pair_size() == 2);
  message.set_dynamic("allocated");
  message.add_big(UINT64_MAX);
  unsigned char output[256];
  assert(message.SerializeToArray(output, sizeof(output)));
  test::Modern copy;
  assert(copy.ParseFromArray(output, message.ByteSizeLong()));
  assert(copy.big(0) == UINT64_MAX);
  assert(std::strcmp(copy.dynamic().data(), "allocated") == 0);
  message.set_note("other");
  assert(!message.has_child() && message.has_note());
  assert(!message.child().has_x());
  const unsigned char narrow[] = {64, 128, 1};
  roo_pb::Input reader(narrow, sizeof(narrow));
  message.Clear();
  assert(message.mergeFrom(reader) == Status::kCapacity);
  const unsigned char short_token[] = {74, 1, 0};
  roo_pb::Input token_reader(short_token, sizeof(short_token));
  message.Clear();
  assert(message.mergeFrom(token_reader) == Status::kCapacity);
}

// Counts encoded/decoded bytes without retaining the complete streamed field.
struct StreamState {
  unsigned encoded = 0;
  unsigned decoded = 0;
};

Status EncodeStream(void* context, roo_pb::Output& output, uint32_t number) {
  StreamState& state = *static_cast<StreamState*>(context);
  ++state.encoded;
  for (unsigned i = 100; i != 0; --i) {
    char byte = static_cast<char>(i - 1);
    output.bytes(&byte, 1);
  }
  output.varint(100);
  output.tag(number, roo_pb::WireType::kLengthDelimited);
  return output.status();
}

Status DecodeStream(void* context, roo_pb::Input& input, uint32_t tag) {
  StreamState& state = *static_cast<StreamState*>(context);
  if (roo_pb::GetWireType(tag) != roo_pb::WireType::kLengthDelimited) {
    return input.fail(Status::kMalformed);
  }
  size_t length = input.remaining();
  for (size_t i = 0; i < length && input.status() == Status::kOk; ++i) {
    char byte;
    input.bytes(&byte, 1);
    assert(static_cast<unsigned char>(byte) == i);
    ++state.decoded;
  }
  return input.status();
}

// Verifies replayable callbacks and bounded unknown-field forwarding.
void CallbacksAndUnknowns() {
  StreamState state;
  test::Modern message;
  message.add_pair(1);
  message.add_pair(2);
  message.set_stream_encoder({&state, EncodeStream});
  message.set_stream_decoder({&state, DecodeStream});
  unsigned char bytes[256];
  size_t size = message.ByteSizeLong();
  assert(message.SerializeToArray(bytes, sizeof(bytes)));
  assert(state.encoded == 2);
  assert(message.ParseFromArray(bytes, size));
  assert(state.decoded == 100);
  roo_pb::UnknownFields<16> unknown;
  test::Legacy legacy;
  legacy.set_unknown_fields_encoder(unknown.encoder());
  legacy.set_unknown_fields_decoder(unknown.decoder());
  const unsigned char extended[] = {0xa0, 6, 42};
  assert(legacy.ParseFromArray(extended, sizeof(extended)));
  assert(unknown.size() == 3);
  assert(legacy.SerializeToArray(bytes, sizeof(bytes)));
  assert(legacy.ByteSizeLong() == 3 && std::memcmp(bytes, extended, 3) == 0);
  test::ExtraExtension extra;
  roo_pb::ExtensionBinding binding = extra.binding();
  roo_pb::ExtensionSet extensions(&binding, 1);
  legacy.set_unknown_fields_encoder(extensions.encoder());
  legacy.set_unknown_fields_decoder(extensions.decoder());
  assert(legacy.ParseFromArray(extended, sizeof(extended)));
  assert(extra.has_value() && extra.value() == 42);
  *extra.mutable_value() = 99;
  assert(legacy.SerializeToArray(bytes, sizeof(bytes)));
  assert(bytes[2] == 99);
  test::ExtraLabelExtension label;
  assert(!label.has_value() &&
         std::strcmp(label.value().data(), "extension") == 0);
  test::ExtraValuesExtension values;
  test::ExtraStateExtension enum_value;
  roo_pb::ExtensionBinding bindings[] = {label.binding(), values.binding(),
                                         enum_value.binding()};
  roo_pb::ExtensionSet more_extensions(bindings, 3);
  legacy.set_unknown_fields_encoder(more_extensions.encoder());
  legacy.set_unknown_fields_decoder(more_extensions.decoder());
  const unsigned char extension_bytes[] = {0xb2, 6, 2, 1, 4, 0xb8, 6, 99};
  assert(legacy.ParseFromArray(extension_bytes, sizeof(extension_bytes)));
  assert(values.value().size() == 2 && values.value()[0] == -1 &&
         values.value()[1] == 2);
  assert(!enum_value.has_value() && enum_value.value() == test::State::READY);
}

}  // namespace

int main() {
  DefaultsAndMerge();
  Scalars();
  Collections();
  CallbacksAndUnknowns();
}
