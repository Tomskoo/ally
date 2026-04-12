load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "sample_data",
    hdrs = ["sample_data.hpp"],
)

# Legacy local renderer targets kept for now — the benchmark binaries
# can use either local or src/rendering versions.

cc_library(
    name = "plain_renderer",
    srcs = ["plain_renderer.cpp"],
    hdrs = ["plain_renderer.hpp"],
    deps = [
        "@cmark-gfm",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "treesitter_renderer",
    srcs = ["treesitter_renderer.cpp"],
    hdrs = [
        "treesitter_renderer.hpp",
        "highlight_theme.hpp",
    ],
    deps = [
        ":plain_renderer",
        "@cmark-gfm",
        "@ftxui//:dom",
        "@tree-sitter",
        "@tree-sitter-cpp",
        "@tree-sitter-javascript",
        "@tree-sitter-python",
        "@tree-sitter-rust",
    ],
)

cc_binary(
    name = "bench",
    srcs = ["main.cpp"],
    deps = [
        ":plain_renderer",
        ":sample_data",
        ":treesitter_renderer",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_binary(
    name = "streaming_bench",
    srcs = ["streaming_bench.cpp"],
    deps = [
        ":plain_renderer",
        ":sample_data",
        ":treesitter_renderer",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

# Production rendering benchmark — uses src/rendering with disk-loaded
# themes and queries instead of embedded strings.
cc_binary(
    name = "prod_bench",
    srcs = ["prod_bench.cpp"],
    deps = [
        ":sample_data",
        "//src/rendering:plain_renderer",
        "//src/rendering:treesitter_renderer",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
