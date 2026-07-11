// Phase 1 stub implementation of the OpenTouch Portable* API.
// Only PortableInit does anything: it hands control to the engine's main().
// The rest get filled in for real in Phase 2 (touch controls, input, etc).

#include "SDL.h"
#include "game_interface.h"

extern int main(int argc, const char **argv);

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main(argc, argv);
}

void PortableBackButton(void)
{
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    return 0;
}

void PortableAction(int state, int action)
{
}

void PortableMoveFwd(float fwd)
{
}

void PortableMoveSide(float strafe)
{
}

void PortableMove(float fwd, float strafe)
{
}

void PortableLookPitch(int mode, float pitch)
{
}

void PortableLookYaw(int mode, float yaw)
{
}

void PortableMouse(float dx, float dy)
{
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
}

void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return false;
}

touchscreemode_t PortableGetScreenMode()
{
    return TS_GAME;
}

void PortableSetMouseTapMode(int enable)
{
}

int PortableGetMouseTapMode(void)
{
    return 0;
}
