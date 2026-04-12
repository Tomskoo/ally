load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "skill_service",
    srcs = ["SkillService.cpp"],
    hdrs = ["SkillService.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/components/autocomplete:skill_types",
        "@yaml-cpp",
    ],
)
