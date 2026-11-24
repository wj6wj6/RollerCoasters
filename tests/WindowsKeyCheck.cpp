// WindowsKeyGate: the Windows key is kept from the system only while the
// world is played, and a release always goes the same way as its press.
#include "WindowsKey.h"
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    using Side = WindowsKeyGate::Side;
    {
        WindowsKeyGate gate;
        check(gate.swallow(Side::Left, true, true), "a press while playing is kept");
        check(gate.holding(), "the kept press waits for its release");
        check(gate.swallow(Side::Left, true, true), "its key repeat is kept");
        check(gate.swallow(Side::Left, false, true), "its release is kept");
        check(!gate.holding(), "nothing waits after the release");
    }
    {
        WindowsKeyGate gate;
        check(!gate.swallow(Side::Left, true, false), "a press in a menu goes to the system");
        check(!gate.holding(), "a press let through is not held");
        check(!gate.swallow(Side::Left, false, true),
              "its release goes too, even if playing began meanwhile");
    }
    {
        WindowsKeyGate gate;
        check(gate.swallow(Side::Right, true, true), "the right key while playing is kept");
        check(gate.swallow(Side::Right, true, false),
              "its key repeat stays kept after a menu opened");
        check(gate.swallow(Side::Right, false, false), "and so does its release");
        check(!gate.holding(), "nothing waits after the right release");
    }
    {
        WindowsKeyGate gate;
        check(gate.swallow(Side::Left, true, true), "left held while playing");
        check(!gate.swallow(Side::Right, true, false), "right pressed in a menu goes through");
        check(!gate.swallow(Side::Right, false, false), "right released goes through");
        check(gate.holding(), "left still waits");
        check(gate.swallow(Side::Left, false, false), "left released is kept");
    }
    check(!WindowsKeyGate().swallow(Side::Left, false, true), "a lone release goes through");
    if (failures == 0)
        std::printf("PASS: WindowsKeyCheck\n");
    return failures == 0 ? 0 : 1;
}
