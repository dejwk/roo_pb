# Message semantics and callbacks

Parsing replaces resident fields; mergeFrom(data, length, limits) preserves
existing fields. Singular scalar occurrences replace previous values. Repeated
occurrences append, including mixed packed/unpacked input. Singular messages
merge, so separate occurrences can supply different required fields. Oneof
messages merge only while the same member stays selected; switching clears the
previous member. Parse failure leaves partial state and cannot undo callbacks.

Clearing a non-oneof singular submessage resets it in place, including its
presence flags and schema defaults. Callback bindings inside that resident child
survive both explicit clearing and replacement parsing; existing dynamic capacity
can be reused. This does not preserve bindings in oneof alternatives that are
destroyed or repeated elements that are removed/replaced.

Oneof owned values share storage. Switching members destroys the old value and
invalidates pointers and references into it. Clearing an inactive member leaves
the active member unchanged. Inactive getters return shared immutable schema
defaults without selecting a member. Copies own independent values; moves
transfer dynamic storage and leave the source valid for reuse or destruction.
Failed checked string assignment leaves the selected member and its value intact.

IsInitialized checks required fields recursively and configured fixed counts or
lengths. Full Parse/Serialize and array convenience functions call it. Low-level
mergeFrom(Input&) and serialize(Output&) defer validation so submessage fragments
can merge first. Fixed-count arrays must contain exactly max_count elements;
they are not automatically filled. Fixed-length bytes are checked when present.

Maps use bounded entry arrays. Decode replaces previous entries with the same
key. find_field(key) looks up a value; try_insert_field(entry) inserts or replaces
with checked capacity. Direct entry mutation can introduce duplicate keys.
Proto2 closed enums discard unrecognized values from typed fields; enabled unknown
handlers receive them as normalized unpacked occurrences. Proto3 enums retain them.

## Callback fields

The callback types are declared in `roo_pb/callback.h`.

Select storage = "callback" in the sidecar. Each field has independent
EncodeCallback and DecodeCallback bindings, each with its own borrowed context
and function pointer:

```cpp
outgoing.set_payload_encoder({&source, EncodePayload});
incoming.set_payload_decoder({&destination, DecodePayload});
```

The generated field_encoder()/field_decoder() getters expose the bindings.
Setting an encoder establishes explicit field presence and selects its oneof
alternative when applicable. Setting a decoder changes neither presence nor the
selected oneof, and does not satisfy a required field. Parsing an occurrence
establishes presence. Either setter leaves the opposite binding intact.

Encode receives Output& and prepends complete
tagged occurrences: payload first, then length (when applicable), then tag.
Emit repeated occurrences in reverse desired wire order. bytes() preserves the
order within each block:

```cpp
roo_pb::Status Encode(void*, roo_pb::Output& output, uint32_t number) {
  output.bytes("hello", 5);
  output.varint(5);
  return output.tag(number, roo_pb::WireType::kLengthDelimited);
}
```

Each binding is invoked once per serialization traversal, including within nested
messages. ByteSizeLong also invokes encoders. The encoded length is available
through Serialize's written result or Output::size(). A missing encoder emits
nothing; a missing decoder skips the occurrence. A oneof encodes only its selected
binding. clear_field() and Clear retain both bindings while clearing explicit
presence. Pass {} to a directional setter to unbind that direction; the encoder
setter still establishes presence, so use clear_field() to clear presence.

Decode receives Input&. For length-delimited fields the tag and length are already
consumed and the reader is bounded to the payload. Fixed-width fields receive
exactly four or eight bytes. Consume the whole payload. Varint/group fields receive
the enclosing reader and must consume one occurrence. Packed repeated scalar
callbacks receive the complete packed payload. Errors abort the operation and
remain sticky even if the callback returns kOk. Context and buffers must outlive
the operation; never retain the borrowed reader/writer.

## Extensions and unknown fields

Proto2 extension ranges expose set_unknown_fields_encoder() and
set_unknown_fields_decoder(). Other messages opt in:

```toml
[messages."example.Device"]
unknown_fields = true
```

This avoids four pointers of callback state in ordinary messages. Extension
holders expose schema accessors, defaults, validation and binding():

```cpp
test::ExtraExtension extra;
roo_pb::ExtensionBinding binding = extra.binding();
roo_pb::ExtensionSet extensions(&binding, 1);
message.set_unknown_fields_encoder(extensions.encoder());
message.set_unknown_fields_decoder(extensions.decoder());
```

Each ExtensionBinding contains a field number, an encoder and a decoder; their
contexts can refer to different holders. ExtensionSet also accepts independent
fallback encoder/decoder bindings. Bind only the direction needed by the message.
The set borrows the array and holders. Clear external state before replacement
parsing. Validate message-valued extension holders with IsInitialized after
parsing; the owning message cannot inspect caller-owned handler state.

UnknownFields<N> retains validated occurrences using bounded storage. Use its
encoder()/decoder() bindings directly or as ExtensionSet fallbacks. It normalizes the outer tag
and preserves payload bytes and encounter order; relative ordering against known
fields is not retained. Call clear before replacement parsing. Unknown handlers
receive the enclosing input after the tag, with the length prefix still unread.
CopyUnknown validates and prepends one such occurrence to a disjoint Output buffer.

## Buffers and limits

Parse(data, length, message, limits) consumes an exact message. Serialize(message,
data, capacity, written) writes at the beginning of the buffer and returns the
encoded length. Alternatively, Serialize(message, output) leaves the
encoded suffix at output.data(), with output.size() bytes and no final move.
Transport and framing belong to the application.

Input limits default to 1 MiB and depth 32. These are configurable local budgets,
not protobuf limits. Children share a cursor and first error and must finish before
the parent resumes. Output checks capacities and integer overflow; failure can
leave modified bytes, which must be discarded. Empty null buffers are accepted;
nonempty null buffers are rejected. Source fields and output must not overlap.
See the [design](design.md) for buffer mechanics and resource complexity.
