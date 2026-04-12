load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "create_thread",
    srcs = ["CreateThread.cpp"],
    hdrs = ["CreateThread.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/models",
        "//src/style/form:form_style",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
