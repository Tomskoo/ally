load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "nav_bar",
    srcs = ["NavBar.cpp"],
    hdrs = ["NavBar.hpp"],
    visibility = ["//src/app:__pkg__"],
    deps = [
        "//src/app:app_context",
        "//src/models",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)
