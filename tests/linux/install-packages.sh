#!/bin/sh
# Installs what the Linux checks need on Ubuntu 24.04 (clang, FLTK, GLU, xvfb,
# MinGW-w64, and clang-format at the version pinned in tools.json).
#
#   tests/linux/install-packages.sh [deb-directory]
#
# With a directory, .deb files already there are installed without going to
# the network, and freshly downloaded ones are kept there. CI caches that
# directory: a slow Ubuntu mirror once took 15 minutes over these downloads.
set -eu
cd "$(dirname "$0")/../.."
PACKAGES="clang ninja-build libfltk1.3-dev libglu1-mesa-dev xvfb xauth g++-mingw-w64-x86-64-posix"
DEBS=${1:-}
SUDO=
[ "$(id -u)" -eq 0 ] || SUDO=sudo
APT="$SUDO apt-get -y -q --no-install-recommends -o Acquire::Retries=3 -o Acquire::http::Timeout=30"

installed() {
    # shellcheck disable=SC2086
    dpkg-query -W -f '${Status}\n' $PACKAGES 2>/dev/null | grep -vqx 'install ok installed' && return 1
    [ "$(dpkg-query -W -f '${Status}\n' $PACKAGES 2>/dev/null | wc -l)" -eq "$(echo $PACKAGES | wc -w)" ]
}

if ! installed; then
    if [ -n "$DEBS" ] && ls "$DEBS"/*.deb >/dev/null 2>&1 && $APT install "$DEBS"/*.deb && installed; then
        echo "Installed from cached packages in $DEBS"
    else
        $SUDO apt-get update -q
        if [ -n "$DEBS" ]; then
            mkdir -p "$DEBS/partial"
            # shellcheck disable=SC2086
            $APT -o Dir::Cache::archives="$(cd "$DEBS" && pwd)" install $PACKAGES
            $SUDO rm -rf "$DEBS/partial" "$DEBS/lock"
        else
            # shellcheck disable=SC2086
            $APT install $PACKAGES
        fi
    fi
fi

version=$(sed -n '/"clang-format"/,/}/s/.*"version": *"\([^"]*\)".*/\1/p' tools.json)
if ! clang-format --version 2>/dev/null | grep -q "version $version\$"; then
    if command -v pipx >/dev/null; then
        pipx install --force "clang-format==$version"
    else
        python3 -m pip install --user "clang-format==$version"
    fi
fi
clang-format --version
