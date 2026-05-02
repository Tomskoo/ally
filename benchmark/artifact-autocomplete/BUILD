load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "artifact_autocomplete_bench",
    srcs = ["main.cpp"],
    deps = [
        "//src/components/autocomplete:artifact_autocomplete",
        "//src/storage",
        "//src/services/watcher:artifact_watcher",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
