// Linux stand-in for <windows.h>, used only by tests/linux (the Linux CI job).
// It declares the few Win32 names the platform-neutral sources and tests use,
// and defines the SDK macros that have broken MSVC builds before, so that an
// identifier such as `far`, `near` or `DUPLICATE` fails here too.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// The Windows SDK defines these as empty or as constants (minwindef.h, winnt.h).
#define far
#define near
#define FAR
#define NEAR
#define DUPLICATE 4
#define DELETE 0x00010000L
#define ERROR 0
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define TRUE 1
#define FALSE 0
// min/max are left out: MSVC's STL survives those macros, libstdc++ does not.

#define MAX_PATH 260
#define APIENTRY
#define WINAPI
#define CALLBACK

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned int UINT;

inline DWORD GetTempPathA(DWORD size, char *buffer)
{
    const char *directory = std::getenv("TMPDIR");
    if (!directory || !*directory)
        directory = "/tmp";
    const int written = std::snprintf(buffer, size, "%s/", directory);
    return written < 0 || static_cast<DWORD>(written) >= size ? 0 : static_cast<DWORD>(written);
}

inline UINT GetTempFileNameA(const char *directory, const char *prefix, UINT, char *name)
{
    std::snprintf(name, MAX_PATH, "%s%sXXXXXX", directory, prefix);
    const int fd = mkstemp(name);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

inline BOOL DeleteFileA(const char *name)
{
    return std::remove(name) == 0;
}
