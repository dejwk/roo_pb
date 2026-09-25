#include <cassert>
#include <cstring>
#include <type_traits>
#include <vector>

#include "modern.pb.h"
#include "roo_pb/callback.h"
#include "telemetry.pb.h"

namespace {
using roo_pb::Input;
using roo_pb::Output;
using roo_pb::Status;

// Counts visits at each nesting level, catching recursive size replays.
struct Node {
  const Node* child = nullptr;
  mutable unsigned visits = 0;
  bool IsInitialized() const { return true; }
  Status serialize(Output& output) const {
    ++visits;
    if (child != nullptr) {
      return roo_pb::WriteField<roo_pb::Kind::kMessage>(output, 1, *child);
    }
    return roo_pb::WriteField<roo_pb::Kind::kUint32>(output, 1, uint32_t(42));
  }
};

// Verifies serialization and counting each visit all 25 levels exactly once.
void SingleTraversal() {
  Node nodes[25];
  for (size_t i = 0; i < 24; ++i) {
    nodes[i].child = &nodes[i + 1];
  }
  unsigned char bytes[128];
  size_t written = 0;
  assert(roo_pb::Serialize(nodes[0], bytes, sizeof(bytes), written) ==
         Status::kOk);
  assert(written == 50);
  for (const Node& n : nodes) {
    assert(n.visits == 1);
  }
  Output counter;
  assert(nodes[0].serialize(counter) == Status::kOk &&
         counter.size() == written);
  for (const Node& n : nodes) {
    assert(n.visits == 2);
  }
  Input input(bytes, written);
  for (size_t i = 0; i < 24; ++i) {
    uint32_t tag = 0;
    size_t length = 0;
    assert(input.tag(tag) == Status::kOk && tag == 10);
    assert(input.length(length) == Status::kOk && length == input.remaining());
  }
  uint32_t tag = 0;
  uint64_t value = 0;
  assert(input.tag(tag) == Status::kOk && tag == 8);
  assert(input.varint(value) == Status::kOk && value == 42);
}

// Verifies minimal lengths across varint boundaries and exact buffer
// capacities.
void LengthBoundaries() {
  for (size_t length : {size_t(0), size_t(1), size_t(127), size_t(128),
                        size_t(16383), size_t(16384)}) {
    test::Modern message;
    message.add_pair(1);
    message.add_pair(2);
    std::vector<char> payload(length, 'x');
    assert(message.try_set_dynamic(payload.data(), length));
    size_t size = message.ByteSizeLong();
    std::vector<unsigned char> bytes(size + 2, 0xa5);
    size_t written = 999;
    assert(roo_pb::Serialize(message, bytes.data() + 1, size, written) ==
           Status::kOk);
    assert(written == size && bytes.front() == 0xa5 && bytes.back() == 0xa5);
    test::Modern decoded;
    assert(decoded.ParseFromArray(bytes.data() + 1, written));
    assert(decoded.dynamic().size() == length && decoded.pair(0) == 1 &&
           decoded.pair(1) == 2);
    assert(roo_pb::Serialize(message, bytes.data() + 1, size - 1, written) ==
           Status::kCapacity);
    assert(written == 0 && bytes.front() == 0xa5 && bytes.back() == 0xa5);
  }
}

// Verifies suffix and start-of-buffer APIs emit identical bytes.
void BufferResults() {
  example::Device message;
  message.set_id(150);
  message.set_name("test");
  message.add_readings(-3);
  message.add_readings(1000);
  unsigned char suffix[128], start[128];
  Output output(suffix, sizeof(suffix));
  assert(roo_pb::Serialize(message, output) == Status::kOk);
  size_t written = 0;
  assert(roo_pb::Serialize(message, start, sizeof(start), written) ==
         Status::kOk);
  assert(written == output.size() && written == message.ByteSizeLong());
  assert(std::memcmp(start, output.data(), written) == 0);
  example::Device decoded;
  assert(roo_pb::Parse(output.data(), output.size(), decoded) == Status::kOk);
  assert(decoded.readings(0) == -3 && decoded.readings(1) == 1000);
  example::Device empty;
  assert(empty.SerializeToArray(nullptr, 0) &&
         empty.ParseFromArray(nullptr, 0));
  assert(!empty.SerializeToArray(nullptr, 1) &&
         !empty.ParseFromArray(nullptr, 1));
  test::Required required;
  written = 123;
  assert(roo_pb::Serialize(required, start, sizeof(start), written) ==
         Status::kMissingRequired);
  assert(written == 0);
}

Status FailEncode(void*, Output& output, uint32_t) {
  output.fail(Status::kCallback);
  return Status::kOk;
}

Status FailDecode(void*, Input& input, uint32_t) {
  input.fail(Status::kCallback);
  return Status::kOk;
}

Status NestedCallback(void*, Input& input, uint32_t) {
  auto child = input.child(input.remaining());
  return child.status();
}

// Verifies callbacks share sticky errors and respect inherited input depth.
void CallbackErrors() {
  test::Modern message;
  message.set_stream_encoder({nullptr, FailEncode});
  message.set_stream_decoder({nullptr, FailDecode});
  Output counter;
  assert(message.serialize(counter) == Status::kCallback);
  const unsigned char bytes[] = {42, 1, 0};
  Input input(bytes, sizeof(bytes));
  assert(message.mergeFrom(input) == Status::kCallback &&
         input.position() == 2);
  message.set_stream_decoder({nullptr, NestedCallback});
  Input bounded(bytes, sizeof(bytes), {sizeof(bytes), 1});
  assert(message.mergeFrom(bounded) == Status::kDepth &&
         bounded.position() == 2);
}

// Verifies grouped presence bits across byte boundaries, copy, clear and
// parsing.
void PresencePacking() {
  static_assert(sizeof(test::Presence) == 20,
                "17 bool values plus 3 presence bytes");
  test::Presence message;
  using Set = void (test::Presence::*)(bool);
  using Has = bool (test::Presence::*)() const;
  const Set setters[] = {&test::Presence::set_f0,  &test::Presence::set_f1,
                         &test::Presence::set_f2,  &test::Presence::set_f3,
                         &test::Presence::set_f4,  &test::Presence::set_f5,
                         &test::Presence::set_f6,  &test::Presence::set_f7,
                         &test::Presence::set_f8,  &test::Presence::set_f9,
                         &test::Presence::set_f10, &test::Presence::set_f11,
                         &test::Presence::set_f12, &test::Presence::set_f13,
                         &test::Presence::set_f14, &test::Presence::set_f15,
                         &test::Presence::set_f16};
  const Has getters[] = {&test::Presence::has_f0,  &test::Presence::has_f1,
                         &test::Presence::has_f2,  &test::Presence::has_f3,
                         &test::Presence::has_f4,  &test::Presence::has_f5,
                         &test::Presence::has_f6,  &test::Presence::has_f7,
                         &test::Presence::has_f8,  &test::Presence::has_f9,
                         &test::Presence::has_f10, &test::Presence::has_f11,
                         &test::Presence::has_f12, &test::Presence::has_f13,
                         &test::Presence::has_f14, &test::Presence::has_f15,
                         &test::Presence::has_f16};
  for (size_t i = 0; i < 17; ++i) {
    assert(!(message.*getters[i])());
    (message.*setters[i])(false);
    for (size_t j = 0; j < 17; ++j) {
      assert((message.*getters[j])() == (j <= i));
    }
  }
  test::Presence copy = message;
  message.Clear();
  for (Has has : getters) {
    assert(!(message.*has)() && (copy.*has)());
  }
  unsigned char bytes[64];
  size_t written = 0;
  assert(roo_pb::Serialize(copy, bytes, sizeof(bytes), written) == Status::kOk);
  assert(message.ParseFromArray(bytes, written));
  for (Has has : getters) {
    assert((message.*has)());
  }
  message.clear_f8();
  assert(!message.has_f8() && message.has_f7() && message.has_f9());
}

// A one-byte external payload records invocation count and occurrence identity.
struct PayloadState {
  unsigned char value;
  unsigned calls = 0;
};

Status EncodePayload(void* context, Output& output, uint32_t number) {
  PayloadState& state = *static_cast<PayloadState*>(context);
  ++state.calls;
  output.bytes(&state.value, 1);
  output.varint(1);
  return output.tag(number, roo_pb::WireType::kLengthDelimited);
}

struct ReceivedPayload {
  unsigned char value = 0;
  unsigned calls = 0;
};

// Consumes an exact one-byte payload into a distinct receiver context.
Status DecodePayload(void* context, Input& input, uint32_t) {
  ReceivedPayload& state = *static_cast<ReceivedPayload*>(context);
  ++state.calls;
  return input.bytes(&state.value, 1);
}

// Verifies each direction retains its own context through binding, clear and
// parse.
void IndependentCallbacks() {
  static_assert(!std::is_convertible<roo_pb::EncodeCallback,
                                     roo_pb::DecodeCallback>::value);
  PayloadState source{42}, replacement_source{7};
  ReceivedPayload destination, replacement_destination;
  test::CallbackHolder message;
  message.set_payload_decoder({&destination, DecodePayload});
  assert(!message.has_payload() && message.ByteSizeLong() == 0);
  message.set_payload_encoder({&source, EncodePayload});
  assert(message.has_payload());
  assert(message.payload_decoder().context == &destination);
  unsigned char bytes[16];
  size_t written = 0;
  assert(roo_pb::Serialize(message, bytes, sizeof(bytes), written) ==
         Status::kOk);
  assert(source.calls == 1 && destination.calls == 0);
  assert(message.ParseFromArray(bytes, written));
  assert(message.has_payload() && destination.value == 42 &&
         destination.calls == 1);
  assert(message.payload_encoder().context == &source && source.calls == 1);
  message.set_payload_decoder({&replacement_destination, DecodePayload});
  assert(message.has_payload() && message.payload_encoder().context == &source);
  message.set_payload_encoder({&replacement_source, EncodePayload});
  assert(message.payload_decoder().context == &replacement_destination);
  assert(roo_pb::Serialize(message, bytes, sizeof(bytes), written) ==
         Status::kOk);
  assert(message.ParseFromArray(bytes, written));
  assert(replacement_source.calls == 1 && replacement_destination.value == 7);
  assert(destination.calls == 1 && source.calls == 1);
  message.clear_payload();
  assert(!message.has_payload());
  assert(message.payload_encoder().context == &replacement_source);
  assert(message.payload_decoder().context == &replacement_destination);
  assert(message.ParseFromArray(nullptr, 0) && !message.has_payload());
  // Either direction can be unbound without altering the other binding.
  message.set_payload_encoder({});
  assert(message.payload_decoder().context == &replacement_destination);
  message.set_payload_decoder({});
  assert(message.payload_encoder().encode == nullptr);
  assert(message.payload_decoder().decode == nullptr);
}

// Verifies decoder registration neither selects a oneof nor satisfies required
// presence.
void DecoderPresence() {
  ReceivedPayload payload, other;
  PayloadState source{7};
  test::CallbackChoice message;
  message.set_payload_decoder({&payload, DecodePayload});
  assert(message.selection_case() ==
         test::CallbackChoice::SelectionCase::kNotSet);
  message.set_number(99);
  message.set_other_decoder({&other, DecodePayload});
  assert(message.has_number() && message.number() == 99);
  assert(!message.has_payload() && !message.has_other());
  message.set_payload_encoder({&source, EncodePayload});
  assert(message.has_payload() && !message.has_number());
  message.set_number(100);
  assert(message.payload_encoder().context == &source);
  const unsigned char wire[] = {10, 1, 42};
  assert(message.mergeFrom(wire, sizeof(wire)) == Status::kOk);
  assert(message.has_payload() && !message.has_number());
  assert(payload.calls == 1 && payload.value == 42 && other.calls == 0);
  message.Clear();
  assert(message.selection_case() ==
         test::CallbackChoice::SelectionCase::kNotSet);
  assert(message.payload_encoder().context == &source);
  assert(message.payload_decoder().context == &payload);
  assert(message.other_decoder().context == &other);

  test::RequiredCallback required;
  required.set_payload_decoder({&payload, DecodePayload});
  assert(!required.has_payload() && !required.IsInitialized());
  assert(!required.ParseFromArray(nullptr, 0));
  assert(required.ParseFromArray(wire, sizeof(wire)));
  assert(required.has_payload() && required.IsInitialized());
  required.Clear();
  assert(!required.IsInitialized() &&
         required.payload_decoder().context == &payload);
}

// Verifies extension handlers and unknown fallbacks use independent directional
// contexts.
void DirectionalExtensions() {
  test::ExtraExtension sender, receiver;
  sender.set_extra(17);
  roo_pb::ExtensionBinding binding{100, sender.binding().encoder,
                                   receiver.binding().decoder};
  roo_pb::UnknownFields<16> outgoing_unknown, incoming_unknown;
  const unsigned char unknown_wire[] = {0xb0, 9, 23};
  Input seed(unknown_wire, sizeof(unknown_wire));
  uint32_t tag = 0;
  assert(seed.tag(tag) == Status::kOk);
  assert(outgoing_unknown.read(seed, tag) == Status::kOk);
  roo_pb::ExtensionSet extensions(&binding, 1, outgoing_unknown.encoder(),
                                  incoming_unknown.decoder());
  test::Legacy message;
  message.set_unknown_fields_encoder(extensions.encoder());
  message.set_unknown_fields_decoder(extensions.decoder());
  unsigned char bytes[32];
  size_t written = 0;
  assert(roo_pb::Serialize(message, bytes, sizeof(bytes), written) ==
         Status::kOk);
  assert(!receiver.has_value() && incoming_unknown.size() == 0);
  assert(message.ParseFromArray(bytes, written));
  assert(receiver.value() == 17 && sender.value() == 17);
  assert(incoming_unknown.size() == sizeof(unknown_wire));
  Output copied(bytes, sizeof(bytes));
  assert(incoming_unknown.write(copied) == Status::kOk);
  assert(std::memcmp(copied.data(), unknown_wire, sizeof(unknown_wire)) == 0);
  receiver.set_extra(99);
  assert(sender.value() == 17);
}

// Verifies generated nested/repeated callbacks run once and retain wire order.
void NestedCallbacks() {
  test::CallbackEnvelope message;
  PayloadState states[] = {{7}, {8}, {9}};
  message.mutable_child()->set_payload_encoder({&states[0], EncodePayload});
  message.add_children()->set_payload_encoder({&states[1], EncodePayload});
  message.add_children()->set_payload_encoder({&states[2], EncodePayload});
  unsigned char bytes[32];
  size_t written = 0;
  assert(roo_pb::Serialize(message, bytes, sizeof(bytes), written) ==
         Status::kOk);
  const unsigned char golden[] = {10, 3, 10, 1, 7,  18, 3, 10,
                                  1,  8, 18, 3, 10, 1,  9};
  assert(written == sizeof(golden) && std::memcmp(bytes, golden, written) == 0);
  for (const PayloadState& state : states) {
    assert(state.calls == 1);
  }
  assert(message.ByteSizeLong() == written);
  for (const PayloadState& state : states) {
    assert(state.calls == 2);
  }
  test::CallbackEnvelope copy;
  assert(copy.ParseFromArray(bytes, written));
  assert(copy.has_child() && copy.children_size() == 2);
  assert(copy.child().has_payload() && copy.children(0).has_payload());
  test::CallbackHolder holder;
  assert(!holder.has_payload());
  holder.set_payload_encoder({&states[0], EncodePayload});
  holder.Clear();
  assert(!holder.has_payload() &&
         holder.payload_encoder().encode == EncodePayload);
  assert(holder.ByteSizeLong() == 0 && states[0].calls == 2);
}

// Verifies replacement parsing and explicit clears retain bindings inside a
// resident singular child, even when the child's presence is cleared.
void NestedCallbackReset() {
  test::CallbackEnvelope message;
  ReceivedPayload received;
  PayloadState source{7};
  message.mutable_child()->set_payload_encoder({&source, EncodePayload});
  message.mutable_child()->set_payload_decoder({&received, DecodePayload});
  const unsigned char wire[] = {10, 3, 10, 1, 42};
  assert(message.ParseFromArray(wire, sizeof(wire)));
  assert(received.calls == 1 && received.value == 42);
  message.clear_child();
  assert(!message.has_child() && !message.child().has_payload());
  assert(message.child().payload_encoder().context == &source);
  assert(message.child().payload_decoder().context == &received);
  assert(message.ParseFromArray(wire, sizeof(wire)));
  assert(received.calls == 2);
  message.Clear();
  assert(!message.has_child() && !message.child().has_payload());
  assert(message.child().payload_decoder().context == &received);
}

// Verifies unknown payload fidelity, group checks, encounter order and
// capacity.
void UnknownPayloads() {
  // Unknown varint with nonminimal payload, bytes, group, then fixed32.
  const unsigned char wire[] = {0xa0, 6, 0x81, 0, 0xaa, 6, 2, 9, 8, 0xb3, 6,
                                8,    1, 0xb4, 6, 0xbd, 6, 1, 2, 3, 4};
  roo_pb::UnknownFields<sizeof(wire)> unknown;
  test::Legacy message;
  message.set_unknown_fields_encoder(unknown.encoder());
  message.set_unknown_fields_decoder(unknown.decoder());
  assert(message.ParseFromArray(wire, sizeof(wire)));
  unsigned char bytes[sizeof(wire)];
  size_t written = 0;
  assert(roo_pb::Serialize(message, bytes, sizeof(bytes), written) ==
         Status::kOk);
  assert(written == sizeof(wire) && std::memcmp(bytes, wire, written) == 0);
  roo_pb::UnknownFields<sizeof(wire) - 1> small;
  message.set_unknown_fields_encoder(small.encoder());
  message.set_unknown_fields_decoder(small.decoder());
  assert(!message.ParseFromArray(wire, sizeof(wire)));
  const unsigned char invalid_group[] = {0xb3, 6, 8, 1, 0xbc, 6};
  unknown.clear();
  message.set_unknown_fields_encoder(unknown.encoder());
  message.set_unknown_fields_decoder(unknown.decoder());
  assert(!message.ParseFromArray(invalid_group, sizeof(invalid_group)));
  assert(unknown.size() == 0);
}

}  // namespace

// Verifies direct resident reads commit presence only on success and retain
// previous values after malformed, over-capacity and fixed-length occurrences.
void ResidentStringFailures() {
  example::Device value;
  const unsigned char invalid[] = {18, 2, 0xc0, 0x80};
  assert(!value.ParseFromArray(invalid, sizeof(invalid)));
  assert(!value.has_name() && value.name().empty());
  value.set_name("retained");
  Input bad(invalid, sizeof(invalid));
  assert(value.mergeFrom(bad) == Status::kInvalidUtf8);
  assert(value.has_name() &&
         std::strcmp(value.name().c_str(), "retained") == 0);
  const unsigned char truncated[] = {18, 4, 'x'};
  Input short_input(truncated, sizeof(truncated));
  assert(value.mergeFrom(short_input) == Status::kTruncated);
  assert(std::strcmp(value.name().c_str(), "retained") == 0);
  unsigned char oversized[256];
  Output output(oversized, sizeof(oversized));
  char content[100];
  std::memset(content, 'x', sizeof(content));
  output.bytes(content, sizeof(content));
  output.varint(sizeof(content));
  output.tag(2, roo_pb::WireType::kLengthDelimited);
  Input capacity(output.data(), output.size());
  assert(value.mergeFrom(capacity) == Status::kCapacity);
  assert(std::strcmp(value.name().c_str(), "retained") == 0);
  test::Modern fixed;
  fixed.set_token("abcd");
  const unsigned char wrong_length[] = {74, 3, 'x', 'y', 'z'};
  Input fixed_input(wrong_length, sizeof(wrong_length));
  assert(fixed.mergeFrom(fixed_input) == Status::kCapacity);
  assert(fixed.token().size() == 4 &&
         std::memcmp(fixed.token().data(), "abcd", 4) == 0);
  value.set_online(true);
  const unsigned char bad_oneof[] = {42, 2, 0xc0, 0x80};
  Input oneof_input(bad_oneof, sizeof(bad_oneof));
  assert(value.mergeFrom(oneof_input) == Status::kInvalidUtf8);
  assert(value.has_online() && value.online());
  const unsigned char empty_name[] = {18, 0};
  assert(value.ParseFromArray(empty_name, sizeof(empty_name)));
  assert(value.has_name() && value.name().empty());
}

int main() {
  ResidentStringFailures();
  SingleTraversal();
  LengthBoundaries();
  BufferResults();
  CallbackErrors();
  PresencePacking();
  NestedCallbacks();
  NestedCallbackReset();
  IndependentCallbacks();
  DecoderPresence();
  DirectionalExtensions();
  UnknownPayloads();
}
