/************************************************************************
     File:        AppControl.h

     Comment:     The command line for checking the program without
                  clicking through it (scripts/control-app.ps1 drives it):

                    --version            print the version
                    --doctor             check the folders, texture packs,
                                         OpenGL and the shader path
                    --screenshot <png>   draw a few frames, save the last
                                         one and say whether it looks drawn

                  plus options that set up the scene first (--track,
                  --camera, --scenery, --time, ...; see usage()).

                  Parsing, the track file check, the picture check and the
                  PNG writer have no window or OpenGL, so
                  tests/AppControlCheck.cpp tests them. TrainWindow applies
                  the options; TrainView::finishAutomatedFrame() reads the
                  frame back and ends the run.

                  Exit codes: 0 every check passed, 1 a check failed,
                  2 the command line is wrong.

*************************************************************************/
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace AppControl
{
enum class Mode
{
    Gui,
    Version,
    Help,
    Doctor,
    Screenshot
};

enum class Camera
{
    Keep,
    World,
    Train,
    Chase,
    Top,
    Fly
};

// An on/off panel button: left as the program starts it, or set.
enum class Switch
{
    Keep,
    On,
    Off
};

struct Options
{
    Mode mode = Mode::Gui;
    std::string output;      // --screenshot: the PNG to write
    std::string track;       // --track: a track file to load instead of the Grand Tour
    std::string texturePack; // --pack: a texture pack (.zip or folder)
    Camera camera = Camera::Keep;
    Switch scenery = Switch::Keep;
    Switch light = Switch::Keep; // Light+
    Switch water = Switch::Keep;
    Switch rain = Switch::Keep;
    Switch shadows = Switch::Keep;
    float time = -1.0f;   // --time: hour of the day, 0..24; negative keeps it
    float trainU = -1.0f; // --u: where the train is; negative keeps it
    int frames = 5;       // frames drawn before the screenshot is taken
    int width = 0;        // --size: the window, 0 keeps it
    int height = 0;
};

// Exit codes.
const int kPass = 0;
const int kFail = 1;
const int kUsage = 2;

// Reads the command line. False, with a message for the user, if it is wrong.
bool parse(int argc, const char *const *argv, Options &options, std::string &error);
const char *usage();

// Does the file have the layout CTrack::readPoints() reads: a point count of
// 4 to 65535, then that many lines of at least three numbers? Checked here so
// a bad file fails with a message instead of readPoints()'s dialog.
bool checkTrackFile(const std::string &path, std::string &error);

struct Check
{
    std::string name;
    bool ok;
    std::string detail;
};

// The checks that need no window: the folder Load and Save use can be written
// and the texture packs that are found can be opened.
std::vector<Check> environmentChecks();

// Prints "PASS name: detail" or "FAIL name: detail" lines; kPass if all passed.
int report(const std::vector<Check> &checks, FILE *out);

// What a frame read back from the window looks like. rgb: 3 bytes a pixel,
// rows top first.
struct ImageStats
{
    int width = 0;
    int height = 0;
    double coverage = 0; // share of pixels unlike the most common colour
    int colours = 0;     // distinct colours at 4 bits a channel
    double meanLuma = 0; // 0..255
};
ImageStats measure(const std::vector<unsigned char> &rgb, int width, int height);
// A cleared but undrawn window is one colour; a drawn scene has many.
bool looksDrawn(const ImageStats &stats);
std::string describe(const ImageStats &stats);

// Writes rgb (3 bytes a pixel, rows top first) as an uncompressed PNG.
bool writePng(const std::string &path, const std::vector<unsigned char> &rgb, int width, int height,
              std::string &error);
} // namespace AppControl
