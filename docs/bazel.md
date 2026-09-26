# Bazel protobuf libraries

Load `roo_pb_library` from the module and use its target directly in C++ `deps`:

```starlark
load("@roo_pb//:defs.bzl", "roo_pb_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

roo_pb_library(
    name = "messages",
    srcs = ["proto/device.proto"],
    options = ["proto/device.roo_pb.toml"],
    strip_import_prefix = "proto",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "device",
    srcs = ["device.cpp"],
    deps = [":messages"],
)
```

`device.cpp` can include `"device.pb.h"`. The rule supplies generated headers,
include paths, and the `roo_pb` runtime, including its link dependencies. Build
C++ consumers with C++17 or newer. Generation requires Python 3.11+ on the build
machine's `PATH`; it uses the independent roo_pb compiler without protoc or pip
packages. This rule currently uses the host Python installation, not a registered
Python toolchain.

## Inputs and paths

- `srcs`: one or more `.proto` files in the target's Bazel package. Multiple
  sources may import one another. Generated sources in the same package are
  also supported.
- `options`: optional `.roo_pb.toml` sidecars beside those sources. List them
  explicitly; Bazel cannot discover undeclared files during generation.
- `strip_import_prefix`: a package-relative directory to remove from source
  paths. It defaults to the empty string. This determines both the protobuf
  import path and the generated header's include path.
- `deps`: other `roo_pb_library` targets supplying imported schemas and their
  generated C++ headers. A target with only `deps` aggregates libraries.

For example, `srcs = ["proto/acme/device.proto"]` with
`strip_import_prefix = "proto"` exposes `acme/device.proto` to importers and
`acme/device.pb.h` to C++ callers. Source paths default to being relative to the
Bazel package, rather than the repository root. The prefix must be a normalized
relative directory without `.` or `..` components.

Each source has one owning library. Put schemas from other packages or modules
in separate `roo_pb_library` targets and reference those through `deps`.
Transitive imports and diamond dependencies are supported. Distinct sources
with the same logical import path are rejected during analysis.

## Importing another library

Given a public `//common:types` library exposing `types.proto`:

```starlark
roo_pb_library(
    name = "messages",
    srcs = ["device.proto"],  # May contain: import "types.proto";
    deps = ["//common:types"],
)
```

Dependencies can also come from external modules, for example
`deps = ["@device_schemas//:types"]`. The compiler, runtime, and dependency
labels resolve to their owning modules.

The rule validates the full imported schema graph but emits only its own
headers. Imported headers come from dependencies, so a shared schema has one
C++ definition even when several libraries import it. Changes to schemas,
sidecars (including imported sidecars), or compiler sources invalidate the
relevant generation actions.

## Build outputs and checked-in headers

`bazel build //:messages` generates headers under
`bazel-bin/<package>/messages_generated/`. C++ callers use logical include paths
and do not need to know that output directory. Each target has its own output
and import directories to avoid collisions.

Bazel writes only build outputs. For Arduino or other consumers of checked-in
headers, continue using `tools/generate.py` or your library's regeneration
script. The standalone compiler normally emits all imported headers; its
`--direct-only` option, used by this rule, emits just the named input schemas.

## Module setup

Until roo_pb is published, the consuming root module can use sibling checkouts:

```starlark
bazel_dep(name = "roo_pb", version = "0.1.0")
local_path_override(module_name = "roo_pb", path = "../roo_pb")
local_path_override(module_name = "roo_io", path = "../roo_io")
```

Root-module overrides do not propagate from dependency modules. The local
roo_io override selects the binary primitives currently required by roo_pb.
