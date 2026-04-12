load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "board",
    srcs = ["Board.cpp"],
    hdrs = ["Board.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/commands/task",
        "//src/models",
        "//src/style/colour:stage_colours",
        "//src/utils:time_format",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
