load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "error",
    hdrs = ["Error.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "types",
    hdrs = ["Types.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":error",
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "client",
    srcs = ["Client.cpp"],
    hdrs = ["Client.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":error",
        ":types",
        "@cpp-httplib//:cpp-httplib",
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "state",
    hdrs = ["State.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":client",
        ":types",
    ],
)

cc_library(
    name = "service",
    srcs = ["Service.cpp"],
    hdrs = ["Service.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":client",
        ":state",
        "@nlohmann_json//:json",
    ],
)

cc_library(
    name = "lifecycle",
    srcs = ["Lifecycle.cpp"],
    hdrs = ["Lifecycle.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":client",
        ":error",
        ":state",
        ":types",
    ],
)

cc_library(
    name = "sse",
    srcs = ["Sse.cpp"],
    hdrs = ["Sse.hpp"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":types",
        "@cpp-httplib//:cpp-httplib",
        "@nlohmann_json//:json",
    ],
)
