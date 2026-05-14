# Ally development workflows
# Usage: just <recipe>

# List available recipes
default:
    @just --justfile {{justfile()}} --list

# Format all C++ source files with clang-format
format:
    find src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' | xargs clang-format -i

# Run clang-tidy linter on all C++ source files
lint:
    find src -name '*.cpp' | xargs clang-tidy -p .

# Run clang-tidy and auto-fix issues
lint-fix:
    find src -name '*.cpp' | xargs clang-tidy -p . --fix

# Build the project with Bazel
build:
    bazel build //...

# Build optimized release tarball for the current host
release:
    bazel build --config=release //src/app:ally_release
    mkdir -p dist
    OS=$(uname -s | tr '[:upper:]' '[:lower:]'); \
    ARCH=$(uname -m); \
    case "$ARCH" in aarch64) ARCH=arm64 ;; esac; \
    cp bazel-bin/src/app/ally_release.tar.gz \
       dist/ally-$OS-$ARCH.tar.gz

# Remove release artifacts
release-clean:
    rm -rf dist
