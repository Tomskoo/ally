load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bottom_bar",
    srcs = ["BottomBar.cpp"],
    hdrs = ["BottomBar.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/auth",
        "//src/components/command_bar",
        "//src/components/command_bar:command_bar_state",
        "//src/components/command_bar:command_registry",
        "//src/components/health_monitor",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
