load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "stage_view",
    srcs = [
        "StageView.cpp",
        "StageViewTypes.hpp",
    ],
    hdrs = ["StageView.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/commands/storage",
        "//src/components/autocomplete:artifact_autocomplete",
        "//src/components/autocomplete:file_autocomplete",
        "//src/components/autocomplete:skill_autocomplete",
        "//src/components/scrollable",
        "//src/opencode:service",
        "//src/opencode:types",
        "//src/providers/artifact",
        "//src/providers/workflow",
        "//src/rendering:highlight_theme",
        "//src/rendering:treesitter_renderer",
        "//src/services:skill_service",
        "//src/utils:time_format",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
