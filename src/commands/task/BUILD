load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "task",
    srcs = ["Task.cpp"],
    hdrs = ["Task.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/models",
        "//src/providers/repo_file",
    ],
)
