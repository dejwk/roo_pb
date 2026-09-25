# Field storage

`BoundedString<N>` retains at most N bytes plus a convenience NUL terminator.
`BoundedArray<T,N>` stores N elements inline, including inactive capacity.
`assign`, `push_back`, `add`, and `resize` report capacity failure. String lengths
are byte counts, preserve embedded NULs, and do not count the terminator.

`DynamicString` and `DynamicArray<T>` allocate explicitly with nothrow new.
Checked growth reports allocation failure; ordinary C++ copying enforces success
with a release-active contract check. Copying nested dynamic objects can also
fail that contract. Move operations transfer ownership without allocation.

`EncodeCallback` and `DecodeCallback` each borrow a function pointer and a context.
Generated callback fields store both bindings (four pointers total); each direction
has independent state. Binding a decoder does not change field presence. Encoding
prepends complete occurrences in reverse desired wire order. ByteSizeLong also
invokes encoders. Decoding consumes one wire occurrence; an absent decoder skips
it. Contexts must outlive their operations. Parse errors cannot roll back callback
side effects. Clear retains the bindings and resets explicit presence.

Known length-delimited callback fields receive an exact payload reader after the
length prefix. Unknown/extension handlers use readUnknown and receive the raw
occurrence after its tag, including the length prefix. See [semantics](semantics.md).

Generated `kMaxEncodedSize` conservatively bounds resident bounded messages.
SIZE_MAX means a static bound cannot be established (dynamic/callback storage or
unknown-field handlers). This is a wire-size bound, not sizeof(message).
