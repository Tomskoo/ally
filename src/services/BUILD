load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "command_service",
    srcs = ["CommandService.cpp"],
    hdrs = ["CommandService.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/components/autocomplete:command_types",
        "@yaml-cpp",
    ],
)
