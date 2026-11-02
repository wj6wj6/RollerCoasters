#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include "ArcBallCam.h"
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>

static double angle(const HMatrix a, const HMatrix b) {
    double trace = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) trace += a[i][j] * b[i][j];
    double c = (trace - 1) * .5;
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    return std::acos(c) * 180 / 3.141592653589793;
}

static bool checkMotion(int dx, int dy) {
    Fl_Gl_Window window(600, 600);
    ArcBallCam camera;
    camera.setup(&window, 40, 250, 0, 0, 0);
    HMatrix initial, previous, current;
    camera.getMatrix(initial);
    camera.getMatrix(previous);
    Fl::e_dx = dx; Fl::e_dy = dy; Fl::e_state = 0;
    camera.handle(FL_MOUSEWHEEL);
    camera.getMatrix(current);
    double maxJump = angle(previous, current);
    camera.getMatrix(previous);
    for (int frame = 0; frame < 40; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        Fl::check();
        camera.getMatrix(current);
        double jump = angle(previous, current);
        if (jump > maxJump) maxJump = jump;
        camera.getMatrix(previous);
    }
    double total = angle(initial, current);
    const double expected = .035 * std::sqrt(double(dx * dx + dy * dy)) * 180 / 3.141592653589793;
    std::printf("Wheel (%d,%d): largest jump %.3f deg, total %.3f deg\n", dx, dy, maxJump, total);
    bool pass = maxJump < 1.0 && std::fabs(total - expected) < .05;
    std::printf("%s: a wheel unit must spread across frames and retain total rotation\n", pass ? "PASS" : "FAIL");
    // Reset must stop any outstanding animation callbacks.
    Fl::e_dx = dx; Fl::e_dy = dy;
    camera.handle(FL_MOUSEWHEEL);
    camera.setup(&window, 40, 250, 0, 0, 0);
    camera.getMatrix(previous);
    for (int frame = 0; frame < 4; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        Fl::check();
    }
    camera.getMatrix(current);
    bool resetPass = angle(previous, current) < .01;
    std::printf("%s: reset cancels pending gesture\n", resetPass ? "PASS" : "FAIL");
    return pass && resetPass;
}

static bool checkLevelHorizon() {
    Fl_Gl_Window window(600, 600);
    ArcBallCam camera;
    camera.setup(&window, 40, 250, .2f, 0, 0);
    Fl::e_dx = 5; Fl::e_dy = 0; Fl::e_state = 0;
    camera.handle(FL_MOUSEWHEEL);
    double roll = 0;
    for (int frame = 0; frame < 40; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        Fl::check();
        HMatrix matrix;
        camera.getMatrix(matrix);
        if (std::fabs(matrix[1][0]) > roll) roll = std::fabs(matrix[1][0]);
    }
    bool pass = roll < .001;
    std::printf("%s: horizontal orbit keeps horizon level (up.x = %.4f)\n", pass ? "PASS" : "FAIL", roll);
    return pass;
}

static bool checkZoom(float steps) {
    Fl_Gl_Window window(600, 600);
    ArcBallCam camera;
    camera.setup(&window, 40, 250, .2f, .4f, 0);
    HMatrix initial, current;
    camera.getMatrix(initial);
    const float before = camera.cameraDistance();
    camera.zoomByWheel(steps);
    bool smooth = camera.cameraDistance() == before;
    for (int frame = 0; frame < 40; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        Fl::check();
    }
    const double expected = before * std::pow(1.1, -steps);
    camera.getMatrix(current);
    bool pass = smooth && std::fabs(camera.cameraDistance() - expected) < .1 && angle(initial, current) < .05;
    std::printf("%s: pinch zoom %.2f steps: distance %.3f -> %.3f, orientation unchanged\n",
        pass ? "PASS" : "FAIL", steps, before, camera.cameraDistance());
    return pass;
}

static bool checkPitchLimit() {
    Fl_Gl_Window window(600, 600);
    ArcBallCam camera;
    camera.setup(&window, 40, 250, 0, 0, 0);
    Fl::e_dx = 0; Fl::e_dy = 10; Fl::e_state = 0;
    for (int event = 0; event < 20; ++event) camera.handle(FL_MOUSEWHEEL);
    for (int frame = 0; frame < 50; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        Fl::check();
    }
    HMatrix matrix;
    camera.getMatrix(matrix);
    bool pass = matrix[1][1] > .087f && std::fabs(matrix[1][0]) < .001;
    std::printf("%s: large vertical swipe cannot flip the camera\n", pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    bool vertical = checkMotion(0, 1);
    bool horizontal = checkMotion(-1, 0);
    bool diagonal = checkMotion(1, 1);
    bool level = checkLevelHorizon();
    bool zoomIn = checkZoom(1);
    bool zoomOut = checkZoom(-1);
    bool fineZoom = checkZoom(.25f);
    bool pitchLimit = checkPitchLimit();
    return vertical && horizontal && diagonal && level && zoomIn && zoomOut && fineZoom && pitchLimit ? 0 : 1;
}
