load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//:defs.bzl", "roo_pb_library")

roo_pb_library(
    name = "messages",
    srcs = [
        "nested/consumer.proto",
        "nested/other.proto",
    ],
    deps = [
        "//tests/bazel/base",
        "//tests/bazel/mid",
    ],
)

cc_test(
    name = "library_test",
    srcs = ["library_test.cpp"],
    copts = [
        "-std=c++17",
        "-fno-exceptions",
        "-fno-rtti",
        "-UNDEBUG",
    ],
    deps = [":messages"],
)
