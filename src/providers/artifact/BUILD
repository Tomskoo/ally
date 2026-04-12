load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "artifact",
    srcs = ["ArtifactService.cpp"],
    hdrs = ["ArtifactService.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/commands/storage",
    ],
)
