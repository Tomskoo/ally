load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "bench",
    srcs = ["main.cpp"],
    data = ["//test/chat/data:session.json"],
    deps = [
        "//src/rendering:highlight_theme",
        "//src/rendering:treesitter_renderer",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@nlohmann_json//:json",
    ],
)
