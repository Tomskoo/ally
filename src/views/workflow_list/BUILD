load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "workflow_list",
    srcs = ["WorkflowList.cpp"],
    hdrs = ["WorkflowList.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/models",
        "//src/style/form:form_style",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
