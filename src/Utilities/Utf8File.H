/************************************************************************
     File:        Utf8File.H

     Comment:     Opening a file whose name is UTF-8, as every path in the
                  program is (main() turns the command line into UTF-8,
                  the folder lists and dropped files are UTF-8). On
                  Windows fopen() reads a name in the ANSI code page (Big5
                  on a Traditional Chinese system), so a track with a
                  Chinese name would not open; the wide-character call
                  takes any name.

*************************************************************************/
#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <exception>

// The open file, or null when it cannot be opened (or the name is not UTF-8).
inline FILE *openUtf8(const char *path, const char *mode)
{
#ifdef _WIN32
    try
    {
        const std::wstring wideMode(mode, mode + std::strlen(mode));
        FILE *fp = nullptr;
        if (_wfopen_s(&fp, std::filesystem::u8path(path).c_str(), wideMode.c_str()) != 0)
            return nullptr;
        return fp;
    }
    catch (const std::exception &)
    {
        return nullptr; // u8path() refuses bytes that are not UTF-8
    }
#else
    return std::fopen(path, mode);
#endif
}
