# Nanopb annotation compatibility

roo_pbc accepts a subset of nanopb's inline `.proto` annotations. This lets a
shared schema describe bounded storage for both generators while roo_pb keeps
its C++ value types and callback API. It does not make generated C++ source or
object layouts interchangeable with nanopb.

```proto
syntax = "proto3";
import "nanopb.proto";

option (nanopb_fileopt).max_count = 8;

message Sample {
  option (nanopb_msgopt).max_size = 17;
  string name = 1;  // Up to 16 payload bytes.
  repeated int32 readings = 2 [(nanopb).int_size = IS_16];
  bytes token = 3 [(nanopb) = { max_size: 4 fixed_length: true }];
  bytes stream = 4 [(nanopb).type = FT_CALLBACK];
}
```

Compile it with the usual `python3 -m roo_pbc` command or `roo_pb_library` rule.
The exact import `nanopb.proto` is built into roo_pbc: no nanopb installation,
`protoc`, Google protobuf package, descriptor schema, or Bazel dependency is
needed. roo_pbc does not read an on-disk file with that import name and does not
emit or include `nanopb.pb.h`. The built-in import has no depfile input. Other
imports still resolve normally. A public re-export of `nanopb.proto` also makes
its annotations available to importers.

## Supported options

| Nanopb option | roo_pb interpretation |
|---|---|
| `max_size = N` | String: `max_bytes = N - 1`. Bytes: `max_bytes = N`. |
| `max_length = N` | String: `max_bytes = N`. |
| `max_count = N` | Repeated field capacity. |
| `int_size = IS_8`, `IS_16`, `IS_32`, `IS_64` | `integer_bits`, subject to roo's existing integer validation. |
| `int_size = IS_DEFAULT` | Use the schema's integer width. |
| `type = FT_STATIC` | Bounded resident storage; all required bounds must exist. |
| `type = FT_CALLBACK` | roo_pb encode/decode callback bindings. |
| `fixed_length = true` | Existing roo_pb exact-length constraint for bytes. |
| `fixed_count = true` | Existing roo_pb exact-count constraint for repeated fields. |

Nanopb counts the terminating NUL in `max_size` for strings. roo_pb stores no
terminator, but the bridge subtracts one to preserve the same *payload limit*.
`max_size = 1` therefore permits an empty string; `max_size = 0` is invalid for
bounded strings. `max_length = 0` and bytes `max_size = 0` allow empty payloads.
Lengths are byte counts, not Unicode character counts. Nanopb's `max_length`
takes precedence over `max_size` for strings when both are present, even when
inherited from different scopes.

Both dotted options and flat aggregate options are supported. Aggregates accept
whitespace, commas, or semicolons between `key: value` entries. Fully qualified
extension names such as `(.nanopb).max_count` work too. Duplicate settings at the
same scope are errors, including duplicates across aggregate and dotted syntax.

## Defaults and TOML overrides

Precedence is file options, then the field's own message options, then field
options. Each nested message starts with its file's defaults; an enclosing
message's options do not propagate into nested message declarations. Imported
files have their own defaults. Declared extensions use their declaring file's
defaults and their inline field options.

File/message defaults apply only to relevant field kinds: for example,
`max_count` applies to repeated fields and `max_size` to strings/bytes. An
inapplicable option placed directly on a field is an error. Unsupported options
and invalid option values are rejected even if a later scope would override them.
Enum annotations and options on enum values are unsupported.

The `.roo_pb.toml` sidecar has final precedence, using roo option names:

```toml
[fields."Sample.name"]
max_bytes = 32

[fields."Sample.stream"]
storage = "dynamic"
```

A sidecar storage override to `dynamic` or `callback` drops nanopb allocation
bounds. Nanopb bounds are also ignored for `FT_CALLBACK`; callback code owns its
resource limits. Explicit TOML capacity settings still require bounded storage.
Fixed-size constraints require bounded storage too; use a sidecar `false` value
to disable an inherited constraint when changing storage mode.

## Scope and limitations

This is an annotation bridge, not a nanopb runtime emulation layer:

- Unbounded strings/repeated fields still need explicit callback/dynamic storage.
  Importing `nanopb.proto` does not enable nanopb's automatic callback fallback.
- Explicit `FT_DEFAULT`, `FT_POINTER`, `FT_IGNORE`, `FT_INLINE`, `fallback_type`,
  descriptor options, custom callback functions, naming/layout options, and other
  nanopb options are rejected. Use roo TOML settings for dynamic storage.
- Fixed-size options use roo_pb's existing validation and containers. In
  particular, `fixed_count` requires exactly the configured number of elements
  for an initialized message; it does not remove the container's count member.
- Integer widths cannot widen a schema's 32-bit integer to 64 bits. Decode
  overflow follows roo_pb's checked-capacity behavior.
- Callback signatures, oneof behavior, views, embedded-NUL handling and generated
  C++ APIs remain roo_pb's. Field numbering and protobuf wire encoding are unchanged.
- Nanopb `.options` files and command-line generator settings are not read.

See [field storage](storage.md) for ownership and failure behavior, and
[nanopb's option definitions](https://github.com/nanopb/nanopb/blob/master/generator/proto/nanopb.proto)
for the upstream annotation names.
