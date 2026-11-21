// Checks the folder helpers behind the menu's Load and Save screens
// (src/Folders.*): which files the track list shows and in what order, which
// names Save accepts, and the .txt that Save adds, in an isolated folder.
#include "Folders.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

void touch(const fs::path &path)
{
    std::ofstream(path) << "4\n";
}
} // namespace

int main()
{
    std::error_code error;
    const fs::path folder = fs::temp_directory_path(error) / "rollercoasters-folders-check";
    fs::remove_all(folder, error);
    fs::create_directories(folder / "sub.txt", error); // a folder, not a file
    touch(folder / "loop.txt");
    touch(folder / "Big Drop.TXT");
    touch(folder / "notes.md");
    touch(folder / "apple.txt");

    const std::vector<std::string> names = Folders::list(folder.u8string(), ".txt");
    check(names == std::vector<std::string>{"apple.txt", "Big Drop.TXT", "loop.txt"},
          "the list has the .txt files only (any case), sorted ignoring case");
    check(Folders::list((folder / "missing").u8string(), ".txt").empty(),
          "a missing folder lists nothing");

    check(Folders::validName("my track") && Folders::validName("loop-2.txt"),
          "plain names are accepted");
    const char *const bad[] = {"",     "a/b", "a\\b",    "c:x",  "what?",   "star*",
                               "x\"y", "<a>", "pipe|",   "end.", "end ",    ".",
                               "..",   "CON", "nul.txt", "com1", "LPT9.txt"};
    bool allRefused = true;
    for (const char *name : bad)
        if (Folders::validName(name))
        {
            std::printf("  accepted: \"%s\"\n", name);
            allRefused = false;
        }
    check(allRefused, "separators, wildcards, trailing dots and device names are refused");
    check(Folders::validName("console") && Folders::validName("com"),
          "names that only start like a device are fine");

    check(Folders::withExtension("loop", ".txt") == "loop.txt", ".txt is added to a bare name");
    check(Folders::withExtension("loop.txt", ".txt") == "loop.txt" &&
              Folders::withExtension("Loop.TXT", ".txt") == "Loop.TXT",
          "a name that ends in the extension, in any case, keeps it");
    check(Folders::withExtension("loop v1.2", ".txt") == "loop v1.2.txt" &&
              Folders::withExtension("loop.dat", ".txt") == "loop.dat.txt",
          "a dot in the name is not taken for the extension");

    check(Folders::exists((folder / "loop.txt").u8string()) &&
              !Folders::exists((folder / "none.txt").u8string()),
          "exists() tells saved tracks from new names");

    const fs::path deeper = folder / "made" / "TrackFiles";
    check(Folders::make(deeper.u8string()) && fs::is_directory(deeper, error) &&
              Folders::make(deeper.u8string()),
          "make() creates a missing folder and is fine when it is there");

    fs::remove_all(folder, error);
    return failures == 0 ? 0 : 1;
}
