load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "paths",
    srcs = ["Paths.cpp"],
    hdrs = ["Paths.hpp"],
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
)

cc_library(
    name = "configuration",
    srcs = ["Configuration.cpp"],
    hdrs = ["Configuration.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@yaml-cpp",
    ],
)

cc_library(
    name = "input_config",
    srcs = ["InputConfig.cpp"],
    hdrs = ["InputConfig.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@ftxui//:component",
    ],
)

cc_library(
    name = "hotkey_parser",
    srcs = ["HotkeyParser.cpp"],
    hdrs = ["HotkeyParser.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":input_config",
        "@ftxui//:component",
        "@yaml-cpp",
    ],
)
