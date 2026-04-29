load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "vim_mode",
    srcs = ["VimMode.cpp"],
    hdrs = [
        "VimMode.hpp",
        "CursorOverlayNode.hpp",
    ],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/configuration:input_config",
        "//src/opencode:types",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
