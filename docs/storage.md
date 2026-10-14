# Field storage

`BoundedString<N>` retains at most N payload bytes, without a NUL terminator.
Its object footprint also includes a length counter and alignment padding.
`view()` returns a borrowed `roo::string_view`, valid until mutation or destruction;
Both bounded and dynamic strings implicitly convert to `roo::string_view` with
the same borrowing lifetime. Keep `view()` when template deduction or overload
selection requires an explicit view.
`data()` and `size()` expose the same length-delimited bytes. There is no `c_str()`.
`BoundedArray<T,N>` stores N elements inline, including inactive capacity.
`assign`, `push_back`, `add`, and `resize` report capacity failure. String lengths
are byte counts, preserve embedded NULs, and have no terminator overhead.

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

## Logging strings

Bounded and dynamic strings support direct insertion into `roo_logging::Stream`
and, on Linux, standard C++ output streams:

```cpp
LOG(INFO) << "Device: " << descriptor.name();
```

Insertion uses the stored length and emits unquoted text. Embedded NULs and
ASCII control bytes are escaped, as are quotes and backslashes, so a string
cannot introduce extra log lines. It creates no temporary string. Insertion uses the public `roo_logging.h`
stream interface; backend selection and buffer truncation belong to roo_logging.
A full log buffer can truncate an escape. Standard streams use the same escaping
and obey the stream's error state. This also
applies to standalone bytes fields, which share the string storage type;
`message.DebugString()` uses hexadecimal formatting for schema bytes fields.

Direct builds using this operator must link `src/roo_pb/debug.cpp`, which
provides the shared escape routine. Calls to `.view()` remain available when
raw, unescaped output is intentional.
