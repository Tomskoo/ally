load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bottom_bar",
    srcs = ["BottomBar.cpp"],
    hdrs = ["BottomBar.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/auth",
        "//src/components/health_monitor",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
