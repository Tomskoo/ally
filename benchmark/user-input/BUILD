load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "user_input_bench",
    srcs = ["main.cpp"],
    deps = [
        "//src/commands/storage",
        "//src/components/autocomplete:file_autocomplete",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
