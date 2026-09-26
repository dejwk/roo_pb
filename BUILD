load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load(":defs.bzl", "roo_pb_library")

cc_library(
    name = "roo_pb",
    srcs = glob(["src/**/*.cpp"]),
    hdrs = glob(["src/**/*.h"]),
    includes = ["src"],
    visibility = ["//visibility:public"],
    deps = ["@roo_io"],
)

filegroup(
    name = "compiler_sources",
    srcs = glob(["compiler/roo_pbc/*.py"]),
    visibility = ["//visibility:public"],
)

exports_files([
    "defs.bzl",
    "tools/generate.py",
])

roo_pb_library(
    name = "telemetry_codegen",
    srcs = ["examples/telemetry/telemetry.proto"],
    options = ["examples/telemetry/telemetry.roo_pb.toml"],
    strip_import_prefix = "examples/telemetry",
)

roo_pb_library(
    name = "semantics_codegen",
    srcs = ["tests/schemas/semantics.proto"],
    options = ["tests/schemas/semantics.roo_pb.toml"],
    strip_import_prefix = "tests/schemas",
)

roo_pb_library(
    name = "modern_codegen",
    srcs = ["tests/schemas/modern.proto"],
    options = ["tests/schemas/modern.roo_pb.toml"],
    strip_import_prefix = "tests/schemas",
    deps = [":semantics_codegen"],
)

roo_pb_library(
    name = "test_messages",
    deps = [
        ":modern_codegen",
        ":telemetry_codegen",
    ],
)

[
    cc_test(
        name = name,
        srcs = ["tests/" + name + ".cpp"],
        copts = [
            "-std=c++17",
            "-fno-exceptions",
            "-fno-rtti",
            "-UNDEBUG",
        ],
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
    srcs = [
        "examples/telemetry/main.cpp",
        "examples/telemetry/telemetry.pb.h",
    ],
    copts = [
        "-std=c++17",
        "-fno-exceptions",
        "-fno-rtti",
    ],
    target_compatible_with = select({
        "@roo_testing//roo_testing/platforms:is_arduino": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    deps = [":test_messages"],
)

roo_pb_library(
    name = "callbacks_messages",
    srcs = ["examples/callbacks/transfer.proto"],
    options = ["examples/callbacks/transfer.roo_pb.toml"],
    strip_import_prefix = "examples/callbacks",
)

cc_binary(
    name = "callbacks_example",
    srcs = ["examples/callbacks/main.cpp"],
    copts = [
        "-std=c++17",
        "-fno-exceptions",
        "-fno-rtti",
    ],
    deps = [":callbacks_messages"],
)

cc_library(
    name = "arduino_example_compile",
    srcs = [":arduino_example_source"],
    copts = [
        "-std=c++17",
        "-fno-exceptions",
        "-fno-rtti",
    ],
    target_compatible_with = ["@roo_testing//roo_testing/platforms:arduino"],
    deps = [
        ":test_messages",
        "@roo_testing//:arduino",
    ],
)

genrule(
    name = "arduino_example_source",
    srcs = ["examples/telemetry/telemetry.ino"],
    outs = ["generated/telemetry_sketch.cpp"],
    cmd = "cp $(location examples/telemetry/telemetry.ino) $@",
)
