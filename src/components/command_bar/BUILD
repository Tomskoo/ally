load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "command_bar_state",
    hdrs = ["CommandBarState.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "command_registry",
    srcs = ["CommandRegistry.cpp"],
    hdrs = ["CommandRegistry.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "command_bar",
    srcs = ["CommandBar.cpp"],
    hdrs = ["CommandBar.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":command_bar_state",
        ":command_registry",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
