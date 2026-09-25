# Callback payload and typed extension

The payload field uses a callback instead of a resident byte array. Its encoder
prepends bytes in reverse traversal order, then length and tag; its decoder
accumulates a checksum directly from the bounded payload reader. The checksum
extension lives in a separate generated holder, registered through a borrowed
ExtensionSet. No heap allocation is required.

The complete wire message lives in a caller-owned byte buffer. Serialize returns
the encoded length. The outgoing packet binds set_payload_encoder(); the incoming
packet binds set_payload_decoder(). The bindings have independent contexts, and
registering the decoder does not mark the payload present until parsing encounters
it. Extensions are bound through set_unknown_fields_encoder()/decoder() in the
corresponding direction. Callbacks borrow Input&/Output& only for the call.
ByteSizeLong also invokes encoders. Transport belongs to the application.

Generate with:

```sh
python3 tools/generate.py -I examples/callbacks --out build/generated transfer.proto
```

The portable test harness builds and runs main.cpp automatically.
