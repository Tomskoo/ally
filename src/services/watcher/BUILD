load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "watcher_events",
    hdrs = ["WatcherEvents.hpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "watcher_queues",
    hdrs = [
        "WatcherBroadcast.hpp",
        "WatcherQueues.hpp",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "artifact_watcher",
    srcs = ["ArtifactWatcher.cpp"],
    hdrs = ["ArtifactWatcher.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":watcher_events",
        ":watcher_queues",
        "@efsw",
    ],
)

cc_library(
    name = "commands_watcher",
    srcs = ["CommandsWatcher.cpp"],
    hdrs = ["CommandsWatcher.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":watcher_events",
        ":watcher_queues",
        "@efsw",
    ],
)
