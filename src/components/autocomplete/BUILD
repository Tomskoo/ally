load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "autocomplete_types",
    hdrs = ["Types.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "skill_types",
    hdrs = ["SkillTypes.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "artifact_types",
    hdrs = ["ArtifactTypes.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "artifact_autocomplete",
    srcs = ["ArtifactAutocomplete.cpp"],
    hdrs = ["ArtifactAutocomplete.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":artifact_types",
        ":file_autocomplete",
        "//src/commands/storage",
        "//src/models",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "skill_autocomplete",
    srcs = ["SkillAutocomplete.cpp"],
    hdrs = ["SkillAutocomplete.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_autocomplete",
        ":skill_types",
        "//src/services:skill_service",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "file_autocomplete",
    srcs = ["FileAutocomplete.cpp"],
    hdrs = ["FileAutocomplete.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":autocomplete_types",
        "//src/commands/storage",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
