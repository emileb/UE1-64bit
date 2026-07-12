// Phase 1 stub implementation of the OpenTouch Portable* API.
// Only PortableInit does anything: it hands control to the engine's main().
// The rest get filled in for real in Phase 2 (touch controls, input, etc).

#include "SDL.h"
#include "game_interface.h"

extern int main(int argc, const char **argv);
// SDL's internal keyboard injection (same approach iortcw / TFE use): pushes a
// key event into SDL's queue under SDL's lock. OpenJK reads it through the normal
// SDL_KEYDOWN/UP path in sdl_input.cpp, so remappable binds keep working.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main(argc, argv);
}

void PortableBackButton(void)
{
    LOGI("PortableBackButton");
    SDL_SendKeyboardKey(SDL_PRESSED,  SDL_SCANCODE_ESCAPE);
    SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_ESCAPE);
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)code);
    return 0;
}
static void sendKey( int state, SDL_Scancode scancode )
{
    SDL_SendKeyboardKey( state ? SDL_PRESSED : SDL_RELEASED, scancode );
}
void PortableAction(int state, int action)
{
    switch (action)
    {
        case PORT_ACT_MENU_UP:      sendKey(state, SDL_SCANCODE_UP);      return;
        case PORT_ACT_MENU_DOWN:    sendKey(state, SDL_SCANCODE_DOWN);    return;
        case PORT_ACT_MENU_LEFT:    sendKey(state, SDL_SCANCODE_LEFT);    return;
        case PORT_ACT_MENU_RIGHT:   sendKey(state, SDL_SCANCODE_RIGHT);   return;
        case PORT_ACT_MENU_SELECT:  sendKey(state, SDL_SCANCODE_RETURN);  return;
        case PORT_ACT_MENU_CONFIRM: sendKey(state, SDL_SCANCODE_Y);       return;
        case PORT_ACT_MENU_BACK:
        case PORT_ACT_MENU_ABORT:
        case PORT_ACT_MENU_SHOW:    sendKey(state, SDL_SCANCODE_ESCAPE);  return;

        case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY);   return;
        case PORT_ACT_MOUSE_RIGHT:  MouseButton(state, BUTTON_SECONDARY); return;
    }
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
    return TS_MENU;
}

void PortableSetMouseTapMode(int enable)
{
}

int PortableGetMouseTapMode(void)
{
    return 0;
}
