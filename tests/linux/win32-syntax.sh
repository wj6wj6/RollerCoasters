#!/bin/sh
# Syntax-check every first-party C++ file against the real Windows SDK headers
# with MinGW-w64, for the Linux CI job. It catches what the stub <windows.h>
# cannot (Win32 calls, SDK macros such as far/near/DUPLICATE in every header)
# without a Windows runner. It is not an MSVC build: warnings, /analyze and
# linking stay with scripts/check.ps1 and the Windows workflow.
#
#   sudo apt-get install g++-mingw-w64-x86-64-posix
#   tests/linux/win32-syntax.sh
set -eu
cd "$(dirname "$0")/../.."
CXX=${MINGW_CXX:-x86_64-w64-mingw32-g++-posix}
export CXX
find src tests -maxdepth 2 -name '*.cpp' -not -path 'tests/linux/*' | sort |
    xargs -P "$(nproc)" -I{} sh -c '
        "$CXX" -std=c++17 -fsyntax-only -D_XKEYCHECK_H "-DROLLERCOASTERS_VERSION=\"0\"" \
            -D__STDC_WANT_SECURE_LIB__=1 -include stdio.h \
            -Iinclude -Iinclude/glad4.6/include -isystem include/glm-0.9.8.5/glm \
            -isystem include/glfw-3.4/include \
            -Isrc -Isrc/Utilities "{}" || { echo "FAIL: {}"; exit 255; }'
echo "PASS: Win32 syntax of src/ and tests/ with MinGW-w64"
