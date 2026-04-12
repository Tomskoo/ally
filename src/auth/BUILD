load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "auth",
    srcs = ["Auth.cpp"],
    hdrs = ["Auth.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/opencode:error",
        "//src/opencode:service",
        "@ftxui//:component",
        "@ftxui//:screen",
    ],
)
