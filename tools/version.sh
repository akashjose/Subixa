#!/usr/bin/env bash
# The version, for everything that is not CMake.
#
# version.txt at the repository root is the source -- see the comment above
# project() in CMakeLists.txt, which reads the same file. release-please
# rewrites it on a release, so nothing here needs updating when the number
# moves.
#
# The packaging scripts need it before CMake has run, or without a build tree
# at all, because all three put it in a filename. They each used to run their
# own sed over the project() line; this is that, once, over a file that exists
# to be read.
#
# Not executable and not standalone: source it. Callers cd to the repository
# root first, which every script here already does.
#
#     source "$(dirname "$0")/version.sh"
#     VERSION=$(subixa_version)

subixa_version() {
    local version
    version=$(head -n1 version.txt 2>/dev/null | tr -d '[:space:]')
    if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "error: version.txt should hold one x.y.z version, and holds '${version}'." >&2
        echo "       run this from a checkout, and let release-please set the number." >&2
        return 1
    fi
    printf '%s\n' "$version"
}
