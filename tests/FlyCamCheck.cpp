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
    float straight[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -20, -250, 1};
    FlyCam wide;
    wide.setup(nullptr, 60);
    wide.setFromViewMatrix(straight, 40);
    wide.position(x, y, z);
    const float expected = 250 * std::tan(20 * pi / 180) / std::tan(30 * pi / 180);
    const bool framed = approx(x, 0, 1e-3f) && approx(y, 20, 1e-3f) && approx(z, expected, 1e-2f);
    std::printf("%s: a 40 deg world view at distance 250 becomes a 60 deg view at %.2f\n",
                framed ? "PASS" : "FAIL", z);
    return pass && framed;
}

// pitch: keys fly level with the ground even while looking down.
static bool checkHeldKey(int code, bool fast, float pitch, float expectX, float expectY,
                         float expectZ, const char *name)
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 30, 0, 0, pitch);
    camera.setSpeed(20);
    if (fast)
        key(camera, FL_KEYDOWN, FL_Control_L);
    const double held = holdKey(camera, code, .5);
    if (fast)
        key(camera, FL_KEYUP, FL_Control_L);
    float x, y, z;
    camera.position(x, y, z);
    pump(.1);
    const bool stopped = !camera.moving() && distanceFrom(camera, x, y, z) < 1e-4f;

    const float travelled = distanceFrom(camera, 0, 30, 0);
    const float speed = 20 * (fast ? FlyCam::boost : 1.0f);
    const float expected = speed * (float)held;
    const float slack = speed * lateSeconds;
    const float dot = ((x - 0) * expectX + (y - 30) * expectY + (z - 0) * expectZ) /
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
    camera.setPose(0, 50, 0, 0, 0);
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
    camera.setPose(0, 50, 0, 0, 0);
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

static bool checkMouseLook()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 50, 0, 0, 0);
    // Captured mouse: moving right turns right, moving up looks up.
    camera.mouseLook(40, -20);
    camera.mouseLook(60, 0);
    const bool pass = approx(camera.yaw(), 100 * FlyCam::mouseDegreesPerPixel, 1e-3f) &&
                      approx(camera.pitch(), 20 * FlyCam::mouseDegreesPerPixel, 1e-3f) &&
                      distanceFrom(camera, 0, 50, 0) < 1e-5f;
    std::printf("%s: mouse look turns %.2f deg right and %.2f deg up\n", pass ? "PASS" : "FAIL",
                camera.yaw(), camera.pitch());
    return pass;
}

static bool checkScroll()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    camera.setPose(0, 50, 0, 0, 0);
    camera.setSpeed(20);
    const float notch = 20 * FlyCam::scrollSeconds;

    // Touchpad two-finger scroll looks around in small exact steps.
    for (int event = 0; event < 4; ++event)
        camera.scroll(.25f, .25f, 0, true);
    const bool look = approx(camera.yaw(), FlyCam::scrollDegrees, 1e-3f) &&
                      approx(camera.pitch(), -FlyCam::scrollDegrees, 1e-3f) &&
                      distanceFrom(camera, 0, 50, 0) < 1e-5f;

    // Spreading two fingers arrives as Ctrl + wheel up and flies forward.
    camera.setPose(0, 50, 0, 0, 0);
    camera.scroll(0, -.5f, FL_CTRL, true);
    float x, y, z;
    camera.position(x, y, z);
    const bool pinch = approx(z, -.5f * notch, 1e-4f) && approx(x, 0, 1e-5f);

    // Alt + scroll slides sideways and up without turning.
    camera.setPose(0, 50, 0, 0, 0);
    camera.scroll(1, -1, FL_ALT, true);
    camera.position(x, y, z);
    const bool slide = approx(x, notch, 1e-4f) && approx(y, 50 + notch, 1e-4f) &&
                       approx(z, 0, 1e-5f) && camera.yaw() == 0;

    // Like a spectator, a mouse wheel notch up flies faster (and stays put).
    camera.setPose(0, 50, 0, 0, 0);
    Fl::e_dx = 0;
    Fl::e_dy = -1;
    Fl::e_state = 0;
    const bool taken = camera.handle(FL_MOUSEWHEEL) == 1;
    const bool faster = approx(camera.speed(), 20 * FlyCam::wheelSpeedStep, 1e-3f);
    Fl::e_dy = 2;
    camera.handle(FL_MOUSEWHEEL);
    const bool slower = approx(camera.speed(), 20 / FlyCam::wheelSpeedStep, 1e-3f);
    const bool wheel =
        taken && faster && slower && distanceFrom(camera, 0, 50, 0) < 1e-5f && camera.pitch() == 0;
    camera.setSpeed(20);

    // Shift is the sink key, so Shift + touchpad scroll still looks around.
    camera.setPose(0, 50, 0, 0, 0);
    camera.scroll(1, 0, FL_SHIFT, true);
    const bool shiftLooks = approx(camera.yaw(), FlyCam::scrollDegrees, 1e-3f) &&
                            distanceFrom(camera, 0, 50, 0) < 1e-5f;

    // Direct Manipulation: fingers moving right drag the scene right (turn left)
    // while a 10% spread flies forward by one wheel notch in the same update.
    camera.setPose(0, 50, 0, 0, 0);
    camera.touchpadGesture(40, 0, 1.1f, false);
    camera.position(x, y, z);
    const float turned = camera.yaw();
    const bool gesture = approx(turned, -40 * FlyCam::dragDegreesPerPixel, 1e-3f) &&
                         approx(distanceFrom(camera, 0, 50, 0), notch, 1e-3f);
    // With Alt or Shift the fingers slide the camera instead of turning it.
    camera.setPose(0, 50, 0, 0, 0);
    camera.touchpadGesture(10, -10, 1, true);
    camera.position(x, y, z);
    const float perPixel = 20 * FlyCam::slideSecondsPerPixel;
    const bool gestureSlide = approx(x, -10 * perPixel, 1e-4f) &&
                              approx(y, 50 - 10 * perPixel, 1e-4f) && camera.yaw() == 0;

    const bool pass = look && pinch && slide && wheel && shiftLooks && gesture && gestureSlide;
    std::printf(
        "%s: touchpad scroll and gestures look, pinch flies, wheel sets speed, Alt slides\n",
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
    camera.setPose(0, 50, 0, 0, 0);
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

static bool checkBounds()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    // Sinking below the floor stops at eye height; flying far stops at the edge.
    camera.setPose(0, 5, 0, 0, -60);
    camera.scroll(0, -100, FL_CTRL, true); // a big pinch dives along the view
    float x, y, z;
    camera.position(x, y, z);
    const bool ground = approx(y, FlyCam::minHeight, 1e-4f);
    // A track under the sea lowers the ground; raising it again waits for the
    // next move instead of making the view jump.
    camera.setGround(-50);
    camera.scroll(0, -100, FL_CTRL, true);
    camera.position(x, y, z);
    const bool seaFloor = approx(y, -50 + FlyCam::minHeight, 1e-4f);
    camera.setGround(0);
    float x2, y2, z2;
    camera.position(x2, y2, z2);
    const bool noJump = approx(y2, y, 1e-4f);
    camera.scroll(0, -1, FL_CTRL, true);
    camera.position(x2, y2, z2);
    const bool raised = approx(y2, FlyCam::minHeight, 1e-4f);
    camera.setPose(1000, 1000, -1000, 0, 0);
    camera.position(x, y, z);
    const bool box = approx(x, FlyCam::maxDistance, 1e-4f) && approx(y, FlyCam::maxHeight, 1e-4f) &&
                     approx(z, -FlyCam::maxDistance, 1e-4f);
    const bool pass = ground && seaFloor && noJump && raised && box;
    std::printf("%s: the camera stays above the (adjustable) ground and inside the scene box\n",
                pass ? "PASS" : "FAIL");
    return pass;
}

static bool checkFieldOfView()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window, 90);
    const bool start = approx(camera.fieldOfViewDegrees(), 90, 1e-4f);
    camera.setFieldOfView(70);
    const bool set = approx(camera.fieldOfViewDegrees(), 70, 1e-4f);
    camera.setFieldOfView(500);
    const bool high = approx(camera.fieldOfViewDegrees(), FlyCam::maxFieldOfView, 1e-4f);
    camera.setFieldOfView(1);
    const bool low = approx(camera.fieldOfViewDegrees(), FlyCam::minFieldOfView, 1e-4f);
    const bool pass = start && set && high && low;
    std::printf("%s: field of view starts at 90, follows the slider and is clamped\n",
                pass ? "PASS" : "FAIL");
    return pass;
}

static bool checkScanCodes()
{
    // Physical key positions (set 1 scan codes), whatever the layout or input method.
    const bool wasd =
        FlyCam::keyForScanCode(0x11, false) == 'w' && FlyCam::keyForScanCode(0x1E, false) == 'a' &&
        FlyCam::keyForScanCode(0x1F, false) == 's' && FlyCam::keyForScanCode(0x20, false) == 'd';
    const bool upDown = FlyCam::keyForScanCode(0x39, false) == ' ' &&
                        FlyCam::keyForScanCode(0x2A, false) == FL_Shift_L &&
                        FlyCam::keyForScanCode(0x36, false) == FL_Shift_R &&
                        FlyCam::keyForScanCode(0x12, false) == 'e' &&
                        FlyCam::keyForScanCode(0x10, false) == 'q';
    const bool other =
        FlyCam::keyForScanCode(0x1D, false) == FL_Control_L &&
        FlyCam::keyForScanCode(0x1D, true) == FL_Control_R &&
        FlyCam::keyForScanCode(0x21, false) == 'f' && FlyCam::keyForScanCode(0x13, false) == 'r' &&
        FlyCam::keyForScanCode(0x22, false) == 'g' && FlyCam::keyForScanCode(0x2E, false) == 'c' &&
        FlyCam::keyForScanCode(0x47, true) == FL_Home &&
        FlyCam::keyForScanCode(0x47, false) == 0 && // numpad 7: Top camera
        FlyCam::keyForScanCode(0x2D, false) == 0 && // X never reaches Fly
        FlyCam::keyForScanCode(0x2C, false) == 0;   // Z is not a flying key
    const bool pass = wasd && upDown && other;
    std::printf("%s: flying keys are read by physical position (scan code)\n",
                pass ? "PASS" : "FAIL");
    return pass;
}

static bool checkLookAt()
{
    Fl_Gl_Window window(600, 600);
    FlyCam camera;
    camera.setup(&window);
    // Facing -Z; the target is behind and below on the right: turn the short way.
    camera.setPose(0, 20, 0, 170, 0);
    camera.lookAt(10, 10, 10);
    const float startYaw = camera.yaw();
    pump(FlyCam::turnSeconds * .4);
    const float midYaw = camera.yaw();
    pump(FlyCam::turnSeconds);
    const float targetYaw = std::atan2(10.0f, -10.0f) * 180 / pi;
    const float targetPitch = std::atan2(-10.0f, std::sqrt(200.0f)) * 180 / pi;
    const bool smooth = startYaw == 170 && midYaw != 170 && midYaw != targetYaw;
    const bool arrived = !camera.turning() && approx(camera.yaw(), targetYaw, 1e-2f) &&
                         approx(camera.pitch(), targetPitch, 1e-2f);
    // Taking the short way, every step stays between 135 and 170 degrees.
    const bool shortWay = midYaw > targetYaw - .01f && midYaw < 170.01f;

    // Looking around during a turn ends it at once.
    camera.lookAt(0, 20, -100);
    pump(.05);
    camera.scroll(1, 0, 0, true);
    const float steered = camera.yaw();
    pump(FlyCam::turnSeconds);
    const bool cancelled = !camera.turning() && camera.yaw() == steered;
    const bool pass = smooth && arrived && shortWay && cancelled;
    std::printf("%s: lookAt turns smoothly the short way (%.1f -> %.1f -> %.1f) and yields to "
                "the user\n",
                pass ? "PASS" : "FAIL", startYaw, midYaw, camera.yaw());
    return pass;
}

int main()
{
    bool view = checkViewMatrix();
    bool forward = checkHeldKey('w', false, -30, 0, 0, -1, "W");
    bool back = checkHeldKey('s', false, -30, 0, 0, 1, "S");
    bool left = checkHeldKey('a', false, 0, -1, 0, 0, "A");
    bool up = checkHeldKey(' ', false, 0, 0, 1, 0, "Space");
    bool down = checkHeldKey(FL_Shift_L, false, 0, 0, -1, 0, "Shift");
    bool upAlt = checkHeldKey('e', false, 0, 0, 1, 0, "E");
    bool downAlt = checkHeldKey('q', false, 0, 0, -1, 0, "Q");
    bool fast = checkHeldKey('d', true, 0, 1, 0, 0, "Ctrl + D");
    bool turn = checkTurnKeysAndPitchLimit();
    bool drag = checkDragLook();
    bool scroll = checkScroll();
    bool mouse = checkMouseLook();
    bool speed = checkSpeedKeysAndUnfocus();
    bool bounds = checkBounds();
    bool lookAt = checkLookAt();
    bool fov = checkFieldOfView();
    bool scanCodes = checkScanCodes();
    return view && forward && back && left && up && down && upAlt && downAlt && fast && turn &&
                   drag && scroll && mouse && speed && bounds && lookAt && fov && scanCodes
               ? 0
               : 1;
}
