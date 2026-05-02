load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "navigation",
    srcs = ["Navigation.cpp"],
    hdrs = ["Navigation.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/app:nav_state",
        "//src/components/command_bar:command_registry",
        "//src/models",
        "//src/providers/repo_file",
    ],
)
