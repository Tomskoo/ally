load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "colour",
    srcs = ["Colour.cpp"],
    hdrs = ["Colour.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@ftxui//:screen",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "stage_colours",
    srcs = ["StageColours.cpp"],
    hdrs = ["StageColours.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
