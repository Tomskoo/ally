load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "skill_autocomplete_bench",
    srcs = ["main.cpp"],
    deps = [
        "//src/components/autocomplete:skill_autocomplete",
        "//src/services:skill_service",
        "//src/services/watcher:skills_watcher",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
