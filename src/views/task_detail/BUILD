load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "task_detail",
    srcs = ["TaskDetail.cpp"],
    hdrs = ["TaskDetail.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/models",
        "//src/style/colour:stage_colours",
        "//src/utils:time_format",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
