load("@rules_cc//cc:defs.bzl", "cc_library")

genrule(
    name = "gen_embedded_tokyonight",
    srcs = ["tokyonight.yaml"],
    outs = ["embedded_tokyonight.hpp"],
    cmd = """
    echo '#pragma once' > $@
    echo '#include <string_view>' >> $@
    echo 'namespace ally::embedded {' >> $@
    echo 'constexpr std::string_view kTokyonightTheme = R"YAML(' >> $@
    cat $< >> $@
    echo ')YAML";' >> $@
    echo '}  // namespace ally::embedded' >> $@
    """,
)

cc_library(
    name = "embedded_theme",
    hdrs = [":gen_embedded_tokyonight"],
    include_prefix = "data/themes",
    visibility = ["//src:__subpackages__"],
)
