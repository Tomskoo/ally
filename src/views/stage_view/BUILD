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
        "//src/storage",
        "//src/components/autocomplete:artifact_autocomplete",
        "//src/components/autocomplete:file_autocomplete",
        "//src/components/autocomplete:command_autocomplete",
        "//src/components/scrollable",
        "//src/components/vim_mode",
        "//src/opencode:service",
        "//src/opencode:types",
        "//src/providers/artifact",
        "//src/providers/workflow",
        "//src/rendering:highlight_theme",
        "//src/rendering:treesitter_renderer",
        "//src/services:command_service",
        "//src/utils:time_format",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
