load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "command_autocomplete_bench",
    srcs = ["main.cpp"],
    deps = [
        "//src/components/autocomplete:command_autocomplete",
        "//src/services:command_service",
        "//src/services/watcher:commands_watcher",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
