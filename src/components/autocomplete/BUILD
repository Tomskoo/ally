load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "autocomplete_types",
    hdrs = ["Types.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "command_types",
    hdrs = ["CommandTypes.hpp"],
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
        "//src/storage",
        "//src/configuration:input_config",
        "//src/models",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "command_autocomplete",
    srcs = ["CommandAutocomplete.cpp"],
    hdrs = ["CommandAutocomplete.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":command_types",
        ":file_autocomplete",
        "//src/configuration:input_config",
        "//src/services:command_service",
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
        "//src/storage",
        "//src/configuration:input_config",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
