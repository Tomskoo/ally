load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "workflow",
    srcs = ["WorkflowService.cpp"],
    hdrs = ["WorkflowService.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/storage",
        "//src/models",
        "@yaml-cpp",
    ],
)
