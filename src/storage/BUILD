load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "storage",
    srcs = ["Storage.cpp"],
    hdrs = ["Storage.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/components/autocomplete:artifact_types",
        "//src/components/autocomplete:autocomplete_types",
        "//src/configuration",
        "//src/models",
        "@yaml-cpp",
    ],
)
