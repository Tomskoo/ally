load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "workflow_form",
    srcs = ["WorkflowForm.cpp"],
    hdrs = ["WorkflowForm.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//src/app:app_context",
        "//src/commands/storage",
        "//src/models",
        "//src/style/form:form_style",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
