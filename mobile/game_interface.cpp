// OpenTouch <-> UE1 bridge.
//
// Menu navigation, the on-screen keyboard passthrough, the generic custom
// buttons and mouse-look are synthesized as real SDL key/mouse events
// (sendKey/MouseMove/MouseButton), exactly what NSDLViewport would see from a
// real keyboard/mouse. This is deliberate, and safe from key-rebinding, for
// all four: Unreal's menu/console UI (Console.uc) reads raw EInputKey codes
// directly, bypassing the player's key bindings entirely (see
// UEngine::InputEvent, which asks the Console first and only falls through to
// the rebindable Input->Process() path if it declines); the custom buttons
// are *meant* to be freely rebindable via UE1's own control options, same as
// any other key; and mouse axis motion isn't exposed in that control-options
// UI at all (only Bindings[key] "which key does what" is), so there's nothing
// to rebind out from under it.
//
// Movement/turning/fire/jump/weapon switching etc, though, DO go through the
// player-editable Bindings[key] -> Aliases[] table when driven by a real key.
// To stay correct no matter what the player rebinds, these call the engine's
// real actions directly instead: UViewport::Exec() with the exact alias name
// or raw Axis/Button command a bound key would reach (e.g. "Fire",
// "MoveForward", "Axis aBaseY Speed=+300.0") - the underlying action, not the
// physical-key-to-action mapping a player can edit. Movement/turning
// additionally need UE1's own per-tick input pump (UInput::ReadInput() /
// TickInput()'s joystick-axis loop, Engine/Src/UnIn.cpp +
// NSDLDrv/Src/NSDLViewport.cpp) to accumulate smoothly and proportionally to
// how far a stick is pushed, which only happens via UInput::Process() against
// a real EInputKey - so those are routed through a handful of reserved,
// otherwise-unreachable key slots (IK_UnknownXX; no real keyboard/mouse/
// gamepad can ever generate these - see NSDLViewport::InitKeyMap) bound once
// to the real command, then driven every tick exactly like TickInput() drives
// a real analog joystick axis. See RESERVED KEYS below.
//
// Threading: UE1 is single-threaded and not reentrant, but Portable* is called
// from the Android UI/touch thread while the engine runs on its own thread
// (started from PortableInit -> main() -> MainLoop(), which never returns).
// SDL_SendKeyboardKey/MouseButton/MouseMove are safe to call from any thread
// (they queue a real SDL event, consumed on the engine thread). Everything
// else below - anything that calls Process()/Exec() directly - is NOT: it
// only runs from UE1_TickPortableActions() (declared here, called once per
// tick from MainLoop() in Unreal/Src/SDLLaunch.cpp), which only ever runs on
// the engine's own thread. PortableAction/PortableMove* etc themselves only
// ever set plain volatile flags/floats or push into a small ring buffer.

#include "SDL.h"
#include "Engine.h"
#include "game_interface.h"

extern int main(int argc, const char **argv);

// SDL's internal keyboard/mouse injection (same approach TFE/OpenJK use):
// pushes an event into SDL's queue under SDL's lock, so it's safe to call
// from any thread and flows through NSDLViewport::TickInput() exactly like
// real hardware input.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

// Defined in NSDLViewport.cpp (Android-only).
extern "C" int UE1_IsMenuActive();
extern "C" UViewport *UE1_GetViewport();

static void sendKey(int state, SDL_Scancode scancode)
{
    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, scancode);
}

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main(argc, argv);
}

void PortableBackButton(void)
{
    LOGI("PortableBackButton");
    sendKey(1, SDL_SCANCODE_ESCAPE);
    sendKey(0, SDL_SCANCODE_ESCAPE);
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    sendKey(state, (SDL_Scancode)code);
    return 0;
}

// --- RESERVED KEYS -----------------------------------------------------------
//
// EInputKey slots no real keyboard/mouse/gamepad can ever generate
// (NSDLViewport's SDL_Scancode -> EInputKey table never targets them), and
// which never appear in UE1's own "press a key to bind" control options - so
// nothing the player does in-game can retarget or interfere with them.
//
// Each is bound to a *signed* raw Axis command (matching how a real analog
// joystick axis is wired - see IK_JoyX etc in NSDLViewport::TickInput()) and
// driven with a signed magnitude every tick: positive = MoveForward/TurnRight/
// StrafeRight's direction, negative = the opposite (MoveBackward/TurnLeft/
// StrafeLeft), by our own choice of sign in UE1_TickPortableActions() - one
// key does both directions, no separate left/right pair needed.
enum
{
    RK_MOVE_AXIS   = IK_Unknown88, // +forward / -back
    RK_TURN_AXIS   = IK_Unknown89, // +right   / -left
    RK_STRAFE_AXIS = IK_Unknown8A, // +right   / -left
};

static bool s_reservedKeysBound = false;

static void ensureReservedKeysBound(UViewport *vp)
{
    if (s_reservedKeysBound)
        return;

    // Same raw commands MoveForward/TurnRight/StrafeRight resolve to in
    // Default.ini (just the positive-Speed half - our own sign supplies the
    // direction), so these keep tracking Default.ini's speeds if ever tuned,
    // same as a real key would.
    vp->Input->Bindings[RK_MOVE_AXIS]   = "Axis aBaseY Speed=+300.0";
    vp->Input->Bindings[RK_TURN_AXIS]   = "Axis aBaseX Speed=+150.0";
    vp->Input->Bindings[RK_STRAFE_AXIS] = "Axis aStrafe Speed=+300.0";

    s_reservedKeysBound = true;
}

// --- Cross-thread state ------------------------------------------------------
//
// Set from PortableAction/PortableMove*/PortableLookYaw/Pitch (touch thread).
// Only ever read from UE1_TickPortableActions() (engine thread).

// Movement/turn, each -1..+1 (see the RK_*_AXIS comment above for sign).
// Digital (D-pad-style) input sets these to a fixed +-1; analog (joystick)
// input sets them to the actual stick fraction, so a soft push moves slower -
// whichever the current touch layout uses just writes on top of the other.
static volatile float s_moveAxis = 0.0f, s_turnAxis = 0.0f, s_strafeAxis = 0.0f;

// Held real buttons, diffed against the last-applied state each tick.
static volatile bool s_wantFire = false, s_wantAltFire = false, s_wantDuck = false;
static volatile bool s_wantWalk = false, s_wantStrafeMod = false;

// One-shot commands (Jump, Grab, weapon switches, ...). Single-producer
// (touch thread) / single-consumer (engine thread) ring buffer, same pattern
// OpenJK's postCommand/CL_AndroidMove use.
#define CMD_QUEUE_LEN 32
static char s_cmdQueue[CMD_QUEUE_LEN][32];
static volatile int s_cmdAvail = 0;
static volatile int s_cmdUsed = 0;

static void postCommand(const char *cmd)
{
    if (s_cmdAvail >= s_cmdUsed + CMD_QUEUE_LEN)
        return; // full, drop
    int slot = s_cmdAvail & (CMD_QUEUE_LEN - 1);
    strncpy(s_cmdQueue[slot], cmd, sizeof(s_cmdQueue[0]) - 1);
    s_cmdQueue[slot][sizeof(s_cmdQueue[0]) - 1] = 0;
    s_cmdAvail++;
}

// Starting points, not measured - in the same ballpark as TFE/OpenJK's own
// mouse scales. Tune to taste.
static const float LOOK_MOUSE_YAW_SCALE = 3500.0f;
static const float LOOK_MOUSE_PITCH_SCALE = 1000.0f;
static const float LOOK_JOY_YAW_SCALE = 40.0f;
static const float LOOK_JOY_PITCH_SCALE = 30.0f;

void PortableAction(int state, int action)
{
    // Generic user-bindable buttons: KP_1-KP_0 for 0-9, A-P for 10-25 (same
    // scheme OpenJK uses). These are meant to be rebindable via UE1's own
    // control options like any other key, so they go through real SDL keys.
    if (action >= PORT_ACT_CUSTOM_0 && action <= PORT_ACT_CUSTOM_25)
    {
        if (action <= PORT_ACT_CUSTOM_9)
            sendKey(state, (SDL_Scancode)(SDL_SCANCODE_KP_1 + action - PORT_ACT_CUSTOM_0));
        else
            sendKey(state, (SDL_Scancode)(SDL_SCANCODE_A + action - PORT_ACT_CUSTOM_10));
        return;
    }

    switch (action)
    {
        // --- Menu navigation: Console.uc reads these EInputKeys directly,
        // bypassing Bindings/Aliases entirely - real SDL keys are correct here.
        case PORT_ACT_MENU_UP:      sendKey(state, SDL_SCANCODE_UP);      return;
        case PORT_ACT_MENU_DOWN:    sendKey(state, SDL_SCANCODE_DOWN);    return;
        case PORT_ACT_MENU_LEFT:    sendKey(state, SDL_SCANCODE_LEFT);    return;
        case PORT_ACT_MENU_RIGHT:   sendKey(state, SDL_SCANCODE_RIGHT);   return;
        case PORT_ACT_MENU_SELECT:  sendKey(state, SDL_SCANCODE_RETURN);  return;
        case PORT_ACT_MENU_CONFIRM: sendKey(state, SDL_SCANCODE_Y);       return;
        case PORT_ACT_MENU_BACK:
        case PORT_ACT_MENU_ABORT:
        case PORT_ACT_MENU_SHOW:    sendKey(state, SDL_SCANCODE_ESCAPE);  return;
        case PORT_ACT_CONSOLE:      sendKey(state, SDL_SCANCODE_GRAVE);   return; // hardcoded IK_Tilde check in Console.uc

        case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY);   return;
        case PORT_ACT_MOUSE_RIGHT:  MouseButton(state, BUTTON_SECONDARY); return;

        // --- Digital movement/turn: same -1/0/+1 axis PortableMove*/analog
        // sticks use (see s_moveAxis/s_turnAxis/s_strafeAxis above), so both
        // input styles reach the engine through the exact same path.
        case PORT_ACT_FWD:         s_moveAxis = state ? 1.0f : 0.0f;   return;
        case PORT_ACT_BACK:        s_moveAxis = state ? -1.0f : 0.0f;  return;
        case PORT_ACT_LEFT:        s_turnAxis = state ? -1.0f : 0.0f;  return; // turn left
        case PORT_ACT_RIGHT:       s_turnAxis = state ? 1.0f : 0.0f;   return; // turn right
        case PORT_ACT_MOVE_LEFT:   s_strafeAxis = state ? -1.0f : 0.0f; return;
        case PORT_ACT_MOVE_RIGHT:  s_strafeAxis = state ? 1.0f : 0.0f;  return;

        // --- Held real buttons/aliases ---
        case PORT_ACT_STRAFE:      s_wantStrafeMod = state != 0;   return; // hold: Left/Right turn -> strafe
        case PORT_ACT_ATTACK:      s_wantFire = state != 0;        return;
        case PORT_ACT_ALT_ATTACK:
        case PORT_ACT_ALT_FIRE:    s_wantAltFire = state != 0;     return;
        case PORT_ACT_DOWN:
        case PORT_ACT_CROUCH:      s_wantDuck = state != 0;        return;
        case PORT_ACT_SPEED:
        case PORT_ACT_SPRINT:
        case PORT_ACT_SMART_TOGGLE_RUN:
        case PORT_ACT_ALWAYS_RUN:  s_wantWalk = state != 0;        return; // UE1 runs by default; this walks instead

        // --- One-shot real commands, fire on press only ---
        case PORT_ACT_JUMP:
        case PORT_ACT_UP:          if (state) postCommand("Jump");            return;
        case PORT_ACT_USE:         if (state) postCommand("Grab");            return; // closest thing UE1 has to a generic "use"
        case PORT_ACT_NEXT_WEP:    if (state) postCommand("NextWeapon");      return;
        case PORT_ACT_WEAP0:       if (state) postCommand("SwitchWeapon 10"); return;
        case PORT_ACT_WEAP1:       if (state) postCommand("SwitchWeapon 1");  return;
        case PORT_ACT_WEAP2:       if (state) postCommand("SwitchWeapon 2");  return;
        case PORT_ACT_WEAP3:       if (state) postCommand("SwitchWeapon 3");  return;
        case PORT_ACT_WEAP4:       if (state) postCommand("SwitchWeapon 4");  return;
        case PORT_ACT_WEAP5:       if (state) postCommand("SwitchWeapon 5");  return;
        case PORT_ACT_WEAP6:       if (state) postCommand("SwitchWeapon 6");  return;
        case PORT_ACT_WEAP7:       if (state) postCommand("SwitchWeapon 7");  return;
        case PORT_ACT_WEAP8:       if (state) postCommand("SwitchWeapon 8");  return;
        case PORT_ACT_WEAP9:       if (state) postCommand("SwitchWeapon 9");  return;
        case PORT_ACT_INVUSE:      if (state) postCommand("ActivateItem");    return;
        case PORT_ACT_INVNEXT:     if (state) postCommand("NextItem");        return;
        case PORT_ACT_INVPREV:     if (state) postCommand("PrevItem");        return;
        case PORT_ACT_QUICKSAVE:   if (state) postCommand("QuickSave");       return;
        case PORT_ACT_QUICKLOAD:   if (state) postCommand("QuickLoad");       return;
        case PORT_ACT_SHOW_KBRD:   if (state) postCommand("Talk");            return; // chat text entry

        // Note: PORT_ACT_PREV_WEP has no default alias (only GreyMinus, a key
        // UE1's KeyMap doesn't translate) and PORT_ACT_INVEN has no equivalent
        // (no Quake2-style inventory screen) - left unmapped.
    }
}

// --- Analog movement ---------------------------------------------------------

// touch_interface_base.cpp scales its analog stick up to roughly +-15 (fwd) /
// +-10 (strafe) at the default sensitivity - not a normalised -1..1 - so
// bring it back to a roughly -1..1 fraction of full speed, matching what a
// real analog joystick axis reports (and what s_moveAxis/s_strafeAxis expect).
static const float FWD_STICK_RANGE = 5.0f;
static const float STRAFE_STICK_RANGE = 3.0f;

void PortableMoveFwd(float fwd)
{
    s_moveAxis = fwd / FWD_STICK_RANGE;
}

void PortableMoveSide(float strafe)
{
    s_strafeAxis = strafe / STRAFE_STICK_RANGE;
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

// --- Look / mouse -------------------------------------------------------------
//
// Genuinely analog either way (a real mouse axis isn't exposed in UE1's
// control-rebinding UI at all), so this stays on the plain, thread-safe
// MouseMove() injection rather than the reserved-key mechanism above - it's
// just batched into one MouseMove() per tick in UE1_TickPortableActions()
// below, same as everything else, instead of injecting an SDL event per
// touch-thread call.
//
// _mouse accumulates per-swipe deltas, drained & zeroed each tick; _joy is a
// held magnitude, kept as-is until the touch layer sends a new value (goes to
// 0 on its own once the stick recentres) - same split TFE/OpenJK use.
static volatile float s_lookYawMouse = 0.0f, s_lookPitchMouse = 0.0f;
static volatile float s_lookYawJoy = 0.0f, s_lookPitchJoy = 0.0f;

void PortableLookPitch(int mode, float pitch)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_lookPitchJoy = pitch * LOOK_JOY_PITCH_SCALE;
    else
        s_lookPitchMouse += pitch * LOOK_MOUSE_PITCH_SCALE;
}

void PortableLookYaw(int mode, float yaw)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_lookYawJoy = yaw * LOOK_JOY_YAW_SCALE;
    else
        s_lookYawMouse += yaw * LOOK_MOUSE_YAW_SCALE;
}

void PortableMouse(float dx, float dy)
{
    // A direct touch swipe (no virtual stick) is treated as mouse-mode look.
    s_lookYawMouse += dx * LOOK_MOUSE_YAW_SCALE;
    s_lookPitchMouse += dy * LOOK_MOUSE_PITCH_SCALE;
}

void PortableMouseAbs(float x, float y)
{
    // Tap-to-position menu mouse isn't implemented for this engine yet (see
    // PortableSetMouseTapMode below) - menus use relative cursor drag instead.
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    MouseButton(state, button);
}

void PortableCommand(const char *cmd)
{
    postCommand(cmd);
}

void PortableAutomapControl(float zoom, float x, float y)
{
    // Unreal 1 has no automap.
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
    return UE1_IsMenuActive() ? TS_MENU : TS_GAME;
}

void PortableSetMouseTapMode(int enable)
{
}

int PortableGetMouseTapMode(void)
{
    return 0;
}

// --- Engine-thread drain (called once per tick from MainLoop(), see
// Unreal/Src/SDLLaunch.cpp) ---------------------------------------------------

// Drives a reserved axis key exactly like NSDLViewport::TickInput()'s "hammer
// the input system" loop drives a real analog joystick axis: every tick,
// while non-zero, feed the *current* magnitude in as an IST_Axis delta scaled
// by real elapsed time. 100.0 is chosen so a full-deflection magnitude (+-1)
// produces the same rate as a real held key (IST_Hold applies
// DeltaSeconds*Speed directly; IST_Axis applies 0.01*Delta*Speed - so
// Delta must be ~100*DeltaSeconds for the two to match), and partial
// deflection scales down proportionally from there.
static void applyAnalogAxis(UViewport *vp, int reservedKey, float magnitude, float deltaSeconds)
{
    if (magnitude == 0.0f)
        return;
    vp->Input->Process(*GSystem, (EInputKey)reservedKey, IST_Axis, magnitude * 100.0f * deltaSeconds);
}

// Same transition-on-change idea, but for a real alias name (Fire/AltFire/
// Duck/Walking/Strafe) instead of a reserved key - these are simple flags
// with no continuous per-tick behaviour of their own, so UInput::Exec() can
// be called directly, bracketed by the same press/release input-action state
// Process() would set.
static void applyAliasHold(UViewport *vp, const char *aliasName, bool want, bool &held)
{
    if (want == held)
        return;
    vp->Input->SetInputAction(want ? IST_Press : IST_Release, 0.0f);
    vp->Exec(aliasName, GSystem);
    vp->Input->SetInputAction(IST_None, 0.0f);
    held = want;
}

extern "C" void UE1_TickPortableActions()
{
    UViewport *vp = UE1_GetViewport();
    if (!vp || !vp->Input)
        return;
    ensureReservedKeysBound(vp);

    static DOUBLE lastTime = 0.0;
    DOUBLE now = appSeconds();
    FLOAT deltaSeconds = lastTime > 0.0 ? (FLOAT)(now - lastTime) : 0.0f;
    lastTime = now;

    applyAnalogAxis(vp, RK_MOVE_AXIS, s_moveAxis, deltaSeconds);
    applyAnalogAxis(vp, RK_TURN_AXIS, s_turnAxis, deltaSeconds);
    applyAnalogAxis(vp, RK_STRAFE_AXIS, s_strafeAxis, deltaSeconds);

    static bool heldFire = false, heldAltFire = false, heldDuck = false;
    static bool heldWalk = false, heldStrafeMod = false;

    applyAliasHold(vp, "Fire", s_wantFire, heldFire);
    applyAliasHold(vp, "AltFire", s_wantAltFire, heldAltFire);
    applyAliasHold(vp, "Duck", s_wantDuck, heldDuck);
    applyAliasHold(vp, "Walking", s_wantWalk, heldWalk);
    applyAliasHold(vp, "Strafe", s_wantStrafeMod, heldStrafeMod);

    // _joy is a held magnitude re-sent every tick (not a discrete swipe delta
    // like _mouse), so without scaling it by elapsed time, turning would speed
    // up/slow down with the frame rate - normalised to a 60Hz reference so the
    // LOOK_JOY_*_SCALE constants keep roughly the same feel they were tuned at.
    float joyFrameScale = deltaSeconds * 60.0f;
    float yaw = s_lookYawMouse + s_lookYawJoy * joyFrameScale;
    float pitch = s_lookPitchMouse + s_lookPitchJoy * joyFrameScale;
    if (yaw != 0.0f || pitch != 0.0f)
        MouseMove(yaw, pitch);
    s_lookYawMouse = 0.0f;
    s_lookPitchMouse = 0.0f;

    while (s_cmdUsed != s_cmdAvail)
    {
        // IST_Press matters here even though these are one-shot commands: a
        // command that is *itself* an alias name (e.g. "Jump", whose alias
        // command is "Jump | Axis aUp Speed=+300.0" in Default.ini) only
        // cascades through Viewport->Exec() - which is what ultimately
        // reaches the real UnrealScript exec function - while the input
        // action is IST_Press; otherwise it's silently swallowed as
        // "handled" by UInput::Exec()'s own alias lookup. A real keypress
        // always has IST_Press set for the length of this call, so match it.
        vp->Input->SetInputAction(IST_Press, 0.0f);
        vp->Exec(s_cmdQueue[s_cmdUsed & (CMD_QUEUE_LEN - 1)], GSystem);
        vp->Input->SetInputAction(IST_None, 0.0f);
        s_cmdUsed++;
    }
}
