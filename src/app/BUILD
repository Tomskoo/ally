load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

cc_library(
    name = "nav_state",
    hdrs = ["NavState.hpp"],
    visibility = ["//src:__subpackages__"],
)

cc_library(
    name = "app_context",
    hdrs = [
        "AppContext.hpp",
        "FocusState.hpp",
        "Navigator.hpp",
    ],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":nav_state",
        "//src/models",
        "//src/opencode:state",
        "//src/providers/artifact",
        "//src/providers/repo_file",
        "//src/providers/workflow",
        "//src/services/watcher:watcher_events",
        "//src/services/watcher:watcher_queues",
        "@nlohmann_json//:json",
    ],
)

cc_binary(
    name = "main",
    srcs = [
        "AppContext.hpp",
        "FocusState.hpp",
        "NavState.hpp",
        "Navigator.hpp",
        "main.cpp",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        "//src/configuration",
        "//src/commands/storage",
        "//src/commands/task",
        "//src/components/bottom_bar",
        "//src/components/nav_bar",
        "//src/components/scrollable",
        "//src/components/splash",
        "//src/opencode:error",
        "//src/opencode:lifecycle",
        "//src/opencode:service",
        "//src/opencode:sse",
        "//src/opencode:state",
        "//src/providers/artifact",
        "//src/providers/repo_file",
        "//src/providers/workflow",
        "//src/views/board",
        "//src/views/create_task",
        "//src/views/create_thread",
        "//src/views/task_detail",
        "//src/views/workflow_form",
        "//src/services/watcher:artifact_watcher",
        "//src/services/watcher:commands_watcher",
        "//src/views/stage_view",
        "//src/views/workflow_list",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

pkg_tar(
    name = "ally_release",
    srcs = [":main"],
    extension = "tar.gz",
    package_dir = "ally",
    remap_paths = {"/main": "/ally"},
    visibility = ["//visibility:public"],
)
