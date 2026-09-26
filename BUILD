load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "roo_pb",
    hdrs = glob(["src/**/*.h"]),
    srcs = glob(["src/**/*.cpp"]),
    includes = ["src"],
    visibility = ["//visibility:public"],
    deps = ["@roo_io"],
)

filegroup(
    name = "compiler_sources",
    visibility = ["//visibility:public"],
    srcs = glob(["compiler/roo_pbc/*.py"]),
)

exports_files(["tools/generate.py"])

# The independent compiler requires host Python >= 3.11, with no pip packages.
genrule(
    name = "telemetry_codegen",
    srcs = glob(["examples/telemetry/*.proto", "examples/telemetry/*.toml"]),
    outs = ["generated/telemetry.pb.h"],
    cmd = "python3 $(location tools/generate.py) -I examples/telemetry --out $(RULEDIR)/generated telemetry.proto",
    tools = ["tools/generate.py", ":compiler_sources"],
)

genrule(
    name = "semantics_codegen",
    srcs = glob(["tests/schemas/*.proto", "tests/schemas/*.toml"]),
    outs = ["generated/modern.pb.h", "generated/semantics.pb.h"],
    cmd = "python3 $(location tools/generate.py) -I tests/schemas --out $(RULEDIR)/generated modern.proto",
    tools = ["tools/generate.py", ":compiler_sources"],
)

cc_library(
    name = "test_messages",
    hdrs = [":telemetry_codegen", ":semantics_codegen"],
    includes = ["generated"],
    deps = [":roo_pb"],
)

[
    cc_test(
        name = name,
        srcs = ["tests/" + name + ".cpp"],
        copts = ["-std=c++17", "-fno-exceptions", "-fno-rtti", "-UNDEBUG"],
        deps = [":test_messages"],
    )
    for name in [
        "wire_test",
        "buffer_test",
        "storage_test",
        "generated_test",
        "oneof_test",
        "semantics_test",
        "malformed_test",
        "resource_test",
    ]
]

cc_binary(
    name = "telemetry_example",
    target_compatible_with = select({"@roo_testing//roo_testing/platforms:is_arduino": ["@platforms//:incompatible"], "//conditions:default": []}),
    srcs = ["examples/telemetry/main.cpp", "examples/telemetry/telemetry.pb.h"],
    copts = ["-std=c++17", "-fno-exceptions", "-fno-rtti"],
    deps = [":test_messages"],
)

genrule(
    name = "callbacks_codegen",
    srcs = glob(["examples/callbacks/*.proto", "examples/callbacks/*.toml"]),
    outs = ["generated/transfer.pb.h"],
    cmd = "python3 $(location tools/generate.py) -I examples/callbacks --out $(RULEDIR)/generated transfer.proto",
    tools = ["tools/generate.py", ":compiler_sources"],
)

cc_library(
    name = "callbacks_messages",
    hdrs = [":callbacks_codegen"],
    includes = ["generated"],
    deps = [":roo_pb"],
)

cc_binary(
    name = "callbacks_example",
    srcs = ["examples/callbacks/main.cpp"],
    copts = ["-std=c++17", "-fno-exceptions", "-fno-rtti"],
    deps = [":callbacks_messages"],
)

cc_library(
    name = "arduino_example_compile",
    srcs = [":arduino_example_source"],
    copts = [ "-std=c++17", "-fno-exceptions", "-fno-rtti"],
    target_compatible_with = ["@roo_testing//roo_testing/platforms:arduino"],
    deps = [":test_messages", "@roo_testing//:arduino"],
)

genrule(
    name = "arduino_example_source",
    srcs = ["examples/telemetry/telemetry.ino"],
    outs = ["generated/telemetry_sketch.cpp"],
    cmd = "cp $(location examples/telemetry/telemetry.ino) $@",
)
