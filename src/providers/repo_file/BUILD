load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "repo_file",
    srcs = ["FileTaskProvider.cpp"],
    hdrs = ["FileTaskProvider.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/commands/storage",
        "//src/models",
    ],
)
