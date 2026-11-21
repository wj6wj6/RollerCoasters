/************************************************************************
     File:        Folders.cpp

     Comment:     See Folders.h.

*************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable
#endif
#include <windows.h>
#include <shellapi.h> // ShellExecuteW
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "Folders.h"

namespace fs = std::filesystem;

namespace
{
std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
} // namespace

namespace Folders
{

std::string besideProgram(const char *name)
{
    std::error_code error;
    const fs::path here = fs::current_path(error);
    return (error ? fs::u8path(name) : here / name).u8string();
}

std::vector<std::string> list(const std::string &folder, const char *extension)
{
    std::vector<std::string> names;
    std::error_code error;
    const std::string wanted = lower(extension);
    for (fs::directory_iterator it(fs::u8path(folder), error), end; !error && it != end;
         it.increment(error))
    {
        if (!it->is_regular_file(error))
            continue;
        const fs::path &path = it->path();
        if (lower(path.extension().u8string()) == wanted)
            names.push_back(path.filename().u8string());
    }
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        const std::string la = lower(a), lb = lower(b);
        return la != lb ? la < lb : a < b;
    });
    return names;
}

bool validName(const std::string &name)
{
    if (name.empty() || name.size() > 200 || name == "." || name == "..")
        return false;
    for (const char c : name)
        if (static_cast<unsigned char>(c) < 32 ||
            std::string("\\/:*?\"<>|").find(c) != std::string::npos)
            return false;
    if (name.back() == '.' || name.back() == ' ')
        return false;
    // Windows' device names, also with an extension: "nul.txt" is NUL
    const std::string stem = lower(name.substr(0, name.find('.')));
    static const char *const devices[] = {"con", "prn", "aux", "nul"};
    for (const char *device : devices)
        if (stem == device)
            return false;
    if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
        stem[3] >= '0' && stem[3] <= '9')
        return false;
    return true;
}

std::string withExtension(const std::string &name, const char *extension)
{
    const std::string ending = extension;
    if (name.size() >= ending.size())
    {
        bool same = true;
        for (size_t i = 0; i < ending.size(); ++i)
            same = same && std::tolower(
                               static_cast<unsigned char>(name[name.size() - ending.size() + i])) ==
                               std::tolower(static_cast<unsigned char>(ending[i]));
        if (same)
            return name;
    }
    return name + ending;
}

bool exists(const std::string &path)
{
    std::error_code error;
    return fs::exists(fs::u8path(path), error);
}

bool make(const std::string &folder)
{
    std::error_code error;
    fs::create_directories(fs::u8path(folder), error);
    return fs::is_directory(fs::u8path(folder), error);
}

bool show(const std::string &folder)
{
    std::error_code error;
    const fs::path where = fs::u8path(folder);
    fs::create_directories(where, error);
#ifdef _WIN32
    // Explorer, given the path as wide characters so any name works
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", where.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    return false; // the program runs on Windows; the Linux checks only build this
#endif
}

} // namespace Folders
