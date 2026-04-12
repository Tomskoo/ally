load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "health_monitor",
    srcs = ["HealthMonitor.cpp"],
    hdrs = ["HealthMonitor.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/opencode:error",
        "//src/opencode:service",
        "//src/opencode:state",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
