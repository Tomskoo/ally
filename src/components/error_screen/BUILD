load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "error_screen",
    srcs = ["ErrorScreen.cpp"],
    hdrs = ["ErrorScreen.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/configuration:config_error",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
