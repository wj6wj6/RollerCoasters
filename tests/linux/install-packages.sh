#!/bin/sh
# Installs what the Linux checks need on Ubuntu 24.04 (clang, FLTK and fluid,
# which its CMake config requires, GLU, xvfb,
# MinGW-w64, and clang-format at the version pinned in tools.json).
#
#   tests/linux/install-packages.sh [--only core|mingw] [deb-directory]
#
# --only core: what the build, the CTest checks and check.ps1 need (no
# MinGW); --only mingw: just MinGW-w64, for win32-syntax.sh. Without it,
# both. CI installs them in separate jobs, so the main checks do not wait
# for MinGW (most of the download).
#
# With a directory, .deb files already there are installed without going to
# the network, and freshly downloaded ones are kept there. CI caches that
# directory: a slow Ubuntu mirror once took 15 minutes over these downloads.
set -eu
cd "$(dirname "$0")/../.."
CORE="clang ninja-build libfltk1.3-dev fluid libglu1-mesa-dev xvfb xauth"
MINGW="g++-mingw-w64-x86-64-posix"
ONLY=
if [ "${1:-}" = "--only" ]; then
    ONLY=${2:-}
    shift 2
fi
case "$ONLY" in
    core) PACKAGES=$CORE ;;
    mingw) PACKAGES=$MINGW ;;
    "") PACKAGES="$CORE $MINGW" ;;
    *) echo "--only takes core or mingw" >&2; exit 2 ;;
esac
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
    # dpkg, not apt: apt would still go to the network (and a slow mirror)
    # to look at the dependencies; the cache holds every package the first
    # run downloaded, so dpkg needs nothing else
    if [ -n "$DEBS" ] && ls "$DEBS"/*.deb >/dev/null 2>&1 &&
        $SUDO dpkg -i "$DEBS"/*.deb >/dev/null && installed; then
        echo "Installed from cached packages in $DEBS"
    else
        $SUDO apt-get update -q
        if [ -n "$DEBS" ]; then
            mkdir -p "$DEBS/partial"
            # shellcheck disable=SC2086
            $APT -o Dir::Cache::archives="$(cd "$DEBS" && pwd)" install -f $PACKAGES
            $SUDO rm -rf "$DEBS/partial" "$DEBS/lock"
        else
            # shellcheck disable=SC2086
            $APT install -f $PACKAGES
        fi
    fi
fi

[ "$ONLY" = mingw ] && exit 0 # clang-format is for the core checks
version=$(sed -n '/"clang-format"/,/}/s/.*"version": *"\([^"]*\)".*/\1/p' tools.json)
if ! clang-format --version 2>/dev/null | grep -q "version $version\$"; then
    if command -v pipx >/dev/null; then
        pipx install --force "clang-format==$version"
    else
        python3 -m pip install --user "clang-format==$version"
    fi
fi
clang-format --version
