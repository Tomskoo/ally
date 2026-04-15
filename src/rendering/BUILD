load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "plain_renderer",
    srcs = ["PlainRenderer.cpp"],
    hdrs = ["PlainRenderer.hpp"],
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
    deps = [
        "@cmark-gfm",
        "@cmark-gfm//:cmark-gfm-extensions",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "highlight_theme",
    srcs = ["HighlightTheme.cpp"],
    hdrs = ["HighlightTheme.hpp"],
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
    deps = [
        "//data/themes:embedded_theme",
        "//src/configuration:paths",
        "@ftxui//:screen",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "query_store",
    srcs = ["QueryStore.cpp"],
    hdrs = ["QueryStore.hpp"],
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
    deps = [
        ":highlight_theme",
        "//src/configuration:paths",
        "@tree-sitter",
        "@tree-sitter-cpp",
        "@tree-sitter-javascript",
        "@tree-sitter-python",
        "@tree-sitter-rust",
    ],
)

cc_library(
    name = "treesitter_renderer",
    srcs = ["TreeSitterRenderer.cpp"],
    hdrs = ["TreeSitterRenderer.hpp"],
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
    deps = [
        ":highlight_theme",
        ":plain_renderer",
        ":query_store",
        "@ftxui//:dom",
        "@tree-sitter",
    ],
)

cc_library(
    name = "rendering",
    visibility = ["//src:__subpackages__", "//benchmark:__subpackages__"],
    deps = [
        ":highlight_theme",
        ":plain_renderer",
        ":query_store",
        ":treesitter_renderer",
    ],
)
