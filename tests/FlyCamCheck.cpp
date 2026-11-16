#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include "FlyCam.H"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

static const float pi = 3.14159265f;

static bool approx(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

static float distanceFrom(const FlyCam &camera, float x, float y, float z)
{
    float px, py, pz;
    camera.position(px, py, pz);
    return std::sqrt((px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z));
}

static void key(FlyCam &camera, int event, int code)
{
    Fl::e_keysym = code;
    camera.handle(event);
}

// Press a key, run the event loop, release it. Returns the time from just
// before the press to just after the release: an upper bound on how long the
// camera can have moved, since it measures time from the press and stops by the release.
static double pump(double seconds);
static double holdKey(FlyCam &camera, int code, double seconds)
{
    const auto start = std::chrono::steady_clock::now();
    key(camera, FL_KEYDOWN, code);
    pump(seconds);
    key(camera, FL_KEYUP, code);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Movement is applied on timer ticks, so the time after the last tick before
// the release is not moved yet. Windows timers can be ~16 ms coarse and a busy
// CI machine may skip a tick, so allow 0.1 s of missing motion.
static const float lateSeconds = .1f;

// Run the real FLTK event loop (and its timeouts) for a while.
static double pump(double seconds)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        Fl::check();
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= seconds)
            return elapsed;
    }
}

static bool checkViewMatrix()
{
    // Build the view matrix of a camera at (10, 20, 30), yaw 30, pitch -20.
    const float yaw = 30 * pi / 180, pitch = -20 * pi / 180;
    const float f[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                        -std::cos(yaw) * std::cos(pitch)};
    const float r[3] = {std::cos(yaw), 0, std::sin(yaw)};
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2],
                        r[0] * f[1] - r[1] * f[0]};
    const float eye[3] = {10, 20, 30};
    const float *rows[3] = {r, u, f};
    float m[16] = {0};
    for (int row = 0; row < 3; ++row)
    {
        const float sign = row == 2 ? -1.0f : 1.0f;
        for (int column = 0; column < 3; ++column)
            m[column * 4 + row] = sign * rows[row][column];
        m[12 + row] =
            -sign * (rows[row][0] * eye[0] + rows[row][1] * eye[1] + rows[row][2] * eye[2]);
    }
    m[15] = 1;

    FlyCam camera;
    camera.setFromViewMatrix(m);
    float x, y, z;
    camera.position(x, y, z);
    bool pass = approx(x, 10, 1e-3f) && approx(y, 20, 1e-3f) && approx(z, 30, 1e-3f) &&
                approx(camera.yaw(), 30, 1e-2f) && approx(camera.pitch(), -20, 1e-2f);
    std::printf("%s: starts at the world view (%.2f %.2f %.2f, yaw %.2f, pitch %.2f)\n",
                pass ? "PASS" : "FAIL", x, y, z, camera.yaw(), camera.pitch());

    // From a narrower world view, the camera moves closer so the origin keeps its size.
    float straight[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -250, 1};
    FlyCam wide;
    wide.setup(nullptr, 60);
    wide.setFromViewMatrix(straight, 40);
    wide.position(x, y, z);
    const float expected = 250 * std::tan(20 * pi / 180) / std::tan(30 * pi / 180);
    const bool framed = approx(x, 0, 1e-3f) && approx(y, 0, 1e-3f) && approx(z, expected, 1e-2f);
    std::printf("%s: a 40 deg world view at distance 250 becomes a 60 deg view at %.2f\n",
                framed ? "PASS" : "FAIL", z);
    return pass && framed;
}

static bool checkHeldKey(int code, bool fast, float expectX, float expectY, float expectZ,
                         const char *name)
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 10, 0, 0, 0);
    camera.setSpeed(20);
    if (fast)
        key(camera, FL_KEYDOWN, FL_Shift_L);
    const double held = holdKey(camera, code, .5);
    if (fast)
        key(camera, FL_KEYUP, FL_Shift_L);
    float x, y, z;
    camera.position(x, y, z);
    pump(.1);
    const bool stopped = !camera.moving() && distanceFrom(camera, x, y, z) < 1e-4f;

    const float travelled = distanceFrom(camera, 0, 10, 0);
    const float speed = 20 * (fast ? FlyCam::boost : 1.0f);
    const float expected = speed * (float)held;
    const float slack = speed * lateSeconds;
    const float dot = ((x - 0) * expectX + (y - 10) * expectY + (z - 0) * expectZ) /
                      (travelled > 0 ? travelled : 1);
    const bool pass =
        travelled <= expected + 1e-3f && travelled >= expected - slack && dot > .999f && stopped;
    std::printf("%s: holding %s for %.3f s moves %.2f (at most %.2f) and stops on release\n",
                pass ? "PASS" : "FAIL", name, held, travelled, expected);
    return pass;
}

static bool checkTurnKeysAndPitchLimit()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 0, 0, 0, 0);
    const double held = holdKey(camera, FL_Right, .3);
    pump(.05);
    const float expected = FlyCam::turnDegreesPerSecond * (float)held;
    const bool turn = camera.yaw() <= expected + 1e-3f &&
                      camera.yaw() >= expected - FlyCam::turnDegreesPerSecond * lateSeconds;

    holdKey(camera, FL_Up, 1.5);
    pump(.05);
    float fx, fy, fz;
    camera.forward(fx, fy, fz);
    const bool limit = approx(camera.pitch(), 89, 1e-3f) && fy < 1;
    std::printf("%s: right arrow turns %.2f deg in %.3f s; up arrow stops at %.2f deg pitch\n",
                turn && limit ? "PASS" : "FAIL", camera.yaw(), held, camera.pitch());
    return turn && limit;
}

static bool checkDragLook()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 0, 0, 0, 0);
    Fl::e_x = 300;
    Fl::e_y = 300;
    Fl::e_state = FL_BUTTON1;
    camera.beginDrag();
    Fl::e_x = 400;
    Fl::e_y = 260;
    const bool taken = camera.handle(FL_DRAG) == 1;
    const float yaw = camera.yaw(), pitch = camera.pitch();
    const bool released = camera.handle(FL_RELEASE) == 1 && camera.handle(FL_DRAG) == 0;
    Fl::e_state = 0;
    const bool pass = taken && released && approx(yaw, 100 * FlyCam::dragDegreesPerPixel, 1e-3f) &&
                      approx(pitch, 40 * FlyCam::dragDegreesPerPixel, 1e-3f);
    std::printf("%s: dragging right/up turns right/up (yaw %.2f, pitch %.2f) and ends on release\n",
                pass ? "PASS" : "FAIL", yaw, pitch);
    return pass;
}

static bool checkScroll()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 0, 0, 0, 0);
    camera.setSpeed(20);
    const float notch = 20 * FlyCam::scrollSeconds;

    // Touchpad two-finger scroll looks around in small exact steps.
    for (int event = 0; event < 4; ++event)
        camera.scroll(.25f, .25f, 0, true);
    const bool look = approx(camera.yaw(), FlyCam::scrollDegrees, 1e-3f) &&
                      approx(camera.pitch(), -FlyCam::scrollDegrees, 1e-3f) &&
                      distanceFrom(camera, 0, 0, 0) < 1e-5f;

    // Spreading two fingers arrives as Ctrl + wheel up and flies forward.
    camera.setPose(0, 0, 0, 0, 0);
    camera.scroll(0, -.5f, FL_CTRL, true);
    float x, y, z;
    camera.position(x, y, z);
    const bool pinch = approx(z, -.5f * notch, 1e-4f) && approx(x, 0, 1e-5f);

    // Alt + scroll slides sideways and up without turning.
    camera.setPose(0, 0, 0, 0, 0);
    camera.scroll(1, -1, FL_ALT, true);
    camera.position(x, y, z);
    const bool slide = approx(x, notch, 1e-4f) && approx(y, notch, 1e-4f) && approx(z, 0, 1e-5f) &&
                       camera.yaw() == 0;

    // A mouse wheel notch up flies forward instead of tilting the view.
    camera.setPose(0, 0, 0, 0, 0);
    Fl::e_dx = 0;
    Fl::e_dy = -1;
    Fl::e_state = 0;
    const bool taken = camera.handle(FL_MOUSEWHEEL) == 1;
    camera.position(x, y, z);
    const bool wheel = taken && approx(z, -notch, 1e-4f) && camera.pitch() == 0;

    // Direct Manipulation: fingers moving right drag the scene right (turn left)
    // while a 10% spread flies forward by one wheel notch in the same update.
    camera.setPose(0, 0, 0, 0, 0);
    camera.touchpadGesture(40, 0, 1.1f, false);
    camera.position(x, y, z);
    const float turned = camera.yaw();
    const bool gesture = approx(turned, -40 * FlyCam::dragDegreesPerPixel, 1e-3f) &&
                         approx(distanceFrom(camera, 0, 0, 0), notch, 1e-3f);
    // With Alt or Shift the fingers slide the camera instead of turning it.
    camera.setPose(0, 0, 0, 0, 0);
    camera.touchpadGesture(10, -10, 1, true);
    camera.position(x, y, z);
    const float perPixel = 20 * FlyCam::slideSecondsPerPixel;
    const bool gestureSlide =
        approx(x, -10 * perPixel, 1e-4f) && approx(y, -10 * perPixel, 1e-4f) && camera.yaw() == 0;

    const bool pass = look && pinch && slide && wheel && gesture && gestureSlide;
    std::printf("%s: touchpad scroll and gestures look, pinch and wheel fly, Alt slides\n",
                pass ? "PASS" : "FAIL");
    return pass;
}

static bool checkSpeedKeysAndUnfocus()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setSpeed(20);
    key(camera, FL_KEYDOWN, '=');
    const bool faster = approx(camera.speed(), 25, 1e-4f);
    key(camera, FL_KEYDOWN, '-');
    key(camera, FL_KEYDOWN, '-');
    const float slowed = camera.speed();
    const bool slower = approx(slowed, 16, 1e-4f);
    camera.setSpeed(1000);
    const bool clamped = camera.speed() == FlyCam::maxSpeed;

    // Losing focus while a key is held must not leave the camera drifting.
    camera.setPose(0, 0, 0, 0, 0);
    key(camera, FL_KEYDOWN, 'w');
    pump(.1);
    camera.handle(FL_UNFOCUS);
    float x, y, z;
    camera.position(x, y, z);
    pump(.1);
    const bool halted = !camera.moving() && distanceFrom(camera, x, y, z) < 1e-5f;
    const bool pass = faster && slower && clamped && halted;
    std::printf("%s: +/- change speed (%.1f), speed is clamped, unfocus stops motion\n",
                pass ? "PASS" : "FAIL", slowed);
    return pass;
}

int main()
{
    bool view = checkViewMatrix();
    bool forward = checkHeldKey('w', false, 0, 0, -1, "W");
    bool back = checkHeldKey('s', false, 0, 0, 1, "S");
    bool left = checkHeldKey('a', false, -1, 0, 0, "A");
    bool up = checkHeldKey('e', false, 0, 1, 0, "E");
    bool down = checkHeldKey('q', false, 0, -1, 0, "Q");
    bool fast = checkHeldKey('d', true, 1, 0, 0, "Shift + D");
    bool turn = checkTurnKeysAndPitchLimit();
    bool drag = checkDragLook();
    bool scroll = checkScroll();
    bool speed = checkSpeedKeysAndUnfocus();
    return view && forward && back && left && up && down && fast && turn && drag && scroll && speed
               ? 0
               : 1;
}
