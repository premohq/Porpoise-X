/* Porpoise - the DualSense controllers, as libretro joypad and analog state.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console's pad ABI (scePadInit/Open/Read, the 120-byte sample and its
 * button bits) is the one Mihawk's PS5 RetroArch uses, verified on hardware by
 * ProsperoLight (GPL-3.0-or-later). Rumble uses the compatible dual-motor mode
 * (scePadSetVibrationMode 2), the path both of those projects fall back to.
 *
 * Players: a PS5 controller belongs to a signed-in user, so each player is a
 * user. Player 1 is the user who started Porpoise; the signed-in users are
 * looked at again about once a second, and a newly signed-in user's controller
 * becomes the next free player (the way ProsperoEden's pad.cpp does it). A
 * user who signs out frees their player. */
#include "porpoise_pad.hpp"
#include "porpoise_debuglog.hpp"
#include "porpoise_aim.hpp"
#include "porpoise_move.hpp"
#include "porpoise_moveyaw.hpp"
#include "porpoise_move_bridge.hpp"
#ifdef PORPOISE_DESKTOP
#include "porpoise_platform.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <iterator>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <time.h>

#include "libretro.h"
#include "trace.hpp"

namespace aim = porpoise::aim;

namespace
{
/* Steady microseconds (3.1: how long a rumble lasts). */
std::uint64_t monotonic_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return std::uint64_t(ts.tv_sec) * 1000000ull + std::uint64_t(ts.tv_nsec) / 1000ull;
}
} // namespace

namespace
{
struct ScePadVibrationParam
{
    std::uint8_t largeMotor;
    std::uint8_t smallMotor;
};
struct ScePadLightBarParam
{
    std::uint8_t r, g, b;
};
/* The adaptive triggers (scePadSetTriggerEffect): one command per trigger,
 * the mode's parameters in data (ProsperoEden's ps5_pad.hpp has the layout). */
struct ScePadTriggerEffectCommand
{
    std::int32_t mode; /* 0 off, 1 feedback, 2 weapon, 3 vibration */
    std::int32_t reserved;
    std::uint8_t data[48];
};
struct ScePadTriggerEffectParam
{
    std::uint8_t trigger_mask; /* 1 L2, 2 R2 */
    std::uint8_t reserved[7];
    ScePadTriggerEffectCommand l2, r2;
};
static_assert(sizeof(ScePadTriggerEffectCommand) == 56 && sizeof(ScePadTriggerEffectParam) == 120,
              "the console's trigger effect layout");
} // namespace

extern "C"
{
    std::int32_t scePadInit();
    std::int32_t scePadOpen(std::int32_t user_id, std::int32_t port_type, std::int32_t index,
                            const void *params);
    std::int32_t scePadGetHandle(std::int32_t user_id, std::int32_t port_type, std::int32_t index);
    std::int32_t scePadRead(std::int32_t handle, void *samples, std::int32_t capacity);
    std::int32_t scePadClose(std::int32_t handle);
    std::int32_t scePadSetVibration(std::int32_t handle, const ScePadVibrationParam *param);
    std::int32_t scePadSetVibrationMode(std::int32_t handle, std::int32_t mode);
    std::int32_t scePadSetLightBar(std::int32_t handle, const ScePadLightBarParam *param);
    std::int32_t scePadResetLightBar(std::int32_t handle);
    std::int32_t scePadSetMotionSensorState(std::int32_t handle, bool enable);
    std::int32_t scePadSetTriggerEffect(std::int32_t handle, const ScePadTriggerEffectParam *param);
    std::int32_t sceUserServiceInitialize(const void *params);
    std::int32_t sceUserServiceGetInitialUser(std::int32_t *user_id);
    /* The signed-in users: four ids, -1 for none (room to spare is kept). */
    std::int32_t sceUserServiceGetLoginUserIdList(std::int32_t *user_ids);
    std::int32_t sceKernelUsleep(std::uint32_t microseconds);
}

namespace
{
using namespace porpoise::pad;

constexpr std::uint32_t pad_l3 = 0x000002u;
constexpr std::uint32_t pad_r3 = 0x000004u;
constexpr std::uint32_t pad_options = 0x000008u;
constexpr std::uint32_t pad_up = 0x000010u;
constexpr std::uint32_t pad_right = 0x000020u;
constexpr std::uint32_t pad_down = 0x000040u;
constexpr std::uint32_t pad_left = 0x000080u;
constexpr std::uint32_t pad_l2 = 0x000100u;
constexpr std::uint32_t pad_r2 = 0x000200u;
constexpr std::uint32_t pad_l1 = 0x000400u;
constexpr std::uint32_t pad_r1 = 0x000800u;
constexpr std::uint32_t pad_triangle = 0x001000u;
constexpr std::uint32_t pad_circle = 0x002000u;
constexpr std::uint32_t pad_cross = 0x004000u;
constexpr std::uint32_t pad_square = 0x008000u;
constexpr std::uint32_t pad_touch_pad = 0x100000u;
constexpr std::uint32_t pad_intercepted = UINT32_C(0x80000000);

struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t left_x;
    std::uint8_t left_y;
    std::uint8_t right_x;
    std::uint8_t right_y;
    std::uint8_t left_trigger;
    std::uint8_t right_trigger;
    std::uint8_t padding[2];
    float orientation[4];      /* a quaternion: x, y, z, w */
    float acceleration[3];     /* in g, gravity included: x right, y up out of the face, z toward the player */
    float angular_velocity[3]; /* rad/s, the same axes (read on a PS5: 1.03 g up when flat, ~0 rad/s when still) */
    std::uint8_t touch_count;
    std::uint8_t touch_reserved[7];
    struct
    {
        std::uint16_t x, y;
        std::uint8_t id;
        std::uint8_t reserved[3];
    } touch[2];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t remaining[15];
};
static_assert(sizeof(PadSample) == 120, "the console's pad samples are 120 bytes");
static_assert(offsetof(PadSample, left_x) == 0x04, "the stick bytes follow the button word");
static_assert(offsetof(PadSample, connected) == 0x4c, "connection state sits at 0x4c");
static_assert(offsetof(PadSample, timestamp_us) == 0x50, "the timestamp sits at 0x50");
static_assert(offsetof(PadSample, orientation) == 0x0c, "the motion data follows the triggers");
static_assert(offsetof(PadSample, acceleration) == 0x1c, "acceleration follows the orientation");
static_assert(offsetof(PadSample, angular_velocity) == 0x28, "angular velocity sits at 0x28");
static_assert(offsetof(PadSample, touch) == 0x3c, "the touches sit at 0x3c");

constexpr int sample_capacity = 64;
constexpr unsigned polls_per_scan = 60; /* about once a second */

/* The console's button bit for each Control, in Control order. */
constexpr std::uint32_t kControlPadBit[CtlCount] = {
    pad_cross, pad_circle, pad_square,  pad_triangle,  pad_l1, pad_r1,   pad_l2,   pad_r2,
    pad_l3,    pad_r3,     pad_options, pad_touch_pad, pad_up, pad_down, pad_left, pad_right,
};
/* Porpoise's Button bit for each Control. */
constexpr std::uint32_t kControlButton[CtlCount] = {
    BtnCross, BtnCircle, BtnSquare,  BtnTriangle, BtnL1, BtnR1,   BtnL2,   BtnR2,
    BtnL3,    BtnR3,     BtnOptions, BtnTouch,    BtnUp, BtnDown, BtnLeft, BtnRight,
};
/* The libretro button Dolphin reads for each GameCube input
 * (Source/Core/DolphinLibretro/Input.cpp: A A, B B, X X, Y Y, Z R, L L2, R R2). */
constexpr unsigned kGcRetro[GcCount] = {
    RETRO_DEVICE_ID_JOYPAD_A,     RETRO_DEVICE_ID_JOYPAD_B,  RETRO_DEVICE_ID_JOYPAD_X,    RETRO_DEVICE_ID_JOYPAD_Y,
    RETRO_DEVICE_ID_JOYPAD_R,     RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,   RETRO_DEVICE_ID_JOYPAD_START,
    RETRO_DEVICE_ID_JOYPAD_UP,    RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
};

struct Slot
{
    /* Whose it is (0..3), and whether it's a Move's (its motion only: the
     * player's own controller is in g_slots). */
    int player = 0;
    bool is_move = false;
    std::int32_t user = -1;
    std::int32_t handle = -1;
    std::uint8_t motor_large = 0, motor_small = 0;
    State state;
    State base; /* the controller's own reading, before the Move (player 1) goes on top */
    /* The pointer and its bookkeeping. */
    float pointer_x = 0, pointer_y = 0;
    std::uint64_t last_motion_us = 0;
    int shake_frames = 0;
    /* 3.1: the controller's own rumble shakes it, and its accelerometer read
     * that as a flick (two-controller play: the Remote's rumble is felt in
     * the Nunchuk's hand too, and shook the Nunchuk). Until when a motor ran,
     * the polls in a row over the flick's threshold while it did, and the
     * acceleration passed on, eased while it rumbles. */
    std::uint64_t rumble_until_us = 0;
    int flick_run = 0;
    float eased[3] = {0, 0, 0};
    bool eased_valid = false;
    bool centre_was = false;
    /* The sample's last 32 bytes as last logged (read_slot): looking for where
     * a DualSense tells its charge. */
    std::uint8_t tail[32] = {};
    std::uint64_t tail_at_us = 0;
    /* The controller's orientation, and where the middle of the screen is. */
    aim::Fusion fusion;
    aim::Angles centre;
    bool centred = false;
    /* How it is held (WiiPose; -1 not yet read), and how long a different
     * hold has been seen, steadily, before switching to it. */
    int pose = -1;
    int pose_seen = -1;
    float pose_wait = 0;
    /* The grip as set at the last centring, made level for that hold. */
    aim::Basis basis = aim::kGripNormal;
    /* The accelerometer smoothed (about a sixth of a second): gravity's up. */
    float up[3] = {0, 0, 0};
    unsigned centre_seq = 0; /* the Remote's centrings seen (two-controller play's Nunchuk) */
    float centre_hold = 0;   /* how long R1 (L1) has been held, calmly */
    bool centre_request = false; /* centre_now */
    aim::Smoother smoother;
    unsigned centrings = 0;  /* for the "centered" note on screen */
    /* From REXCRAFT88's Move Studio work (github.com/REXCRAFT88/Porpoise-Dolphin-
     * Emulator-for-PS5, branch feature/move-nav-integration): the last 32 gyroscope
     * readings, so a centring made holding still also takes the gyroscope's drift
     * from them; and the centre as last set (with the hold it was set for), so the
     * centre from the setup carries into the game that starts after it. */
    float gyro_hist[32][3] = {};
    unsigned gyro_hist_n = 0, gyro_hist_next = 0;
    std::uint64_t centred_at_us = 0;
    aim::Angles kept_centre;
    aim::Basis kept_basis = aim::kGripNormal;
    int kept_pose = -1, kept_controller = -1, kept_grip = -1;
    bool kept_move = false;
    /* Fast forward on the touch pad (touch pad + R1, touch pad + R2): the
     * buttons last frame, whether this touch pad press was a combo, and the
     * touch pad's own press (Minus on a Wii Remote) still to be sent. */
    std::uint32_t raw_prev = 0;
    bool touch_combo = false;
    int touch_pulse = 0;
    unsigned turbo_tick = 0; /* turbo: polls since the turbo control went down */
    int triggers = -1;       /* the trigger feel this controller has (-1 not set yet) */
};

/* ---- the GBA ---- */
GbaConfig g_gba[kMaxPlayers];

/* ---- the Wii Remote ---- */
WiiConfig g_wii;
bool g_nunchuk_motion = false; /* the core has the Nunchuk device's motion (set_nunchuk_motion) */
unsigned g_centre_seq = 0;     /* bumped when player 1 centres: two-controller play's Nunchuk follows */

/* Each Wii controller's layout: DualSense control -> Wii input. The same
 * tables drive the game and the Controls tab's drawing. */
constexpr WiiBinding kDPadAsDPad[] = {{CtlUp, WiUp}, {CtlDown, WiDown}, {CtlLeft, WiLeft}, {CtlRight, WiRight}};
constexpr WiiBinding kLayRemote[] = {
    /* Held either way on the right: the index finger on R2 (B), the thumb on
     * Cross (A), 1 and 2 beside it. */
    {CtlCross, WiA},    {CtlR2, WiB},       {CtlSquare, WiOne}, {CtlTriangle, WiTwo},
    {CtlCircle, WiShake}, {CtlTouch, WiMinus}, {CtlOptions, WiPlus}, {CtlR3, WiHome},
    {CtlR1, WiCentre},
};
constexpr WiiBinding kLayRemoteLeft[] = {
    /* The mirror image for the left hand: L2 B, the D-pad as the face buttons. */
    {CtlDown, WiA},     {CtlL2, WiB},       {CtlRight, WiOne},  {CtlUp, WiTwo},
    {CtlLeft, WiShake}, {CtlTouch, WiMinus}, {CtlOptions, WiPlus}, {CtlL3, WiHome},
    {CtlL1, WiCentre},
};
constexpr WiiBinding kLaySideways[] = {
    /* Held like an NES pad: 1 on Square, 2 on Cross; A, B above them. */
    {CtlSquare, WiOne}, {CtlCross, WiTwo},  {CtlTriangle, WiA}, {CtlCircle, WiB},
    {CtlTouch, WiMinus}, {CtlOptions, WiPlus}, {CtlR3, WiHome}, {CtlR1, WiShake},
    {CtlR2, WiShake},
};
constexpr WiiBinding kLayNunchuk[] = {
    /* The Remote on the right (A, B on R2), the Nunchuk on the left (C on L1, Z on L2). */
    {CtlCross, WiA},     {CtlR2, WiB},        {CtlL1, WiC},        {CtlL2, WiZ},
    {CtlTouch, WiMinus}, {CtlOptions, WiPlus}, {CtlSquare, WiOne}, {CtlTriangle, WiTwo},
    {CtlR3, WiHome},     {CtlCircle, WiShake}, {CtlL3, WiNunchukShake}, {CtlR1, WiCentre},
};
constexpr WiiBinding kLayClassic[] = {
    /* By position: a Circle, b Cross, x Triangle, y Square. */
    {CtlCircle, WiClA}, {CtlCross, WiClB},  {CtlTriangle, WiClX}, {CtlSquare, WiClY},
    {CtlL1, WiClZL},    {CtlR1, WiClZR},    {CtlL2, WiClL},       {CtlR2, WiClR},
    {CtlOptions, WiPlus}, {CtlTouch, WiMinus}, {CtlR3, WiHome},
};
constexpr WiiBinding kLayTwoNunchuk[] = {
    /* The second controller is the Nunchuk: Z on its triggers, C on its shoulders or Cross. */
    {CtlL2, WiZ}, {CtlR2, WiZ}, {CtlL1, WiC}, {CtlR1, WiC}, {CtlCross, WiC}, {CtlCircle, WiNunchukShake},
};

/* The libretro button the Dolphin core reads for each Wii input, per device
 * (Source/Core/DolphinLibretro/Input.cpp, retro_set_controller_port_device_wii);
 * -1: none. */
constexpr int R(unsigned id) { return int(id); }
constexpr int kRetroRemote[WiInputCount] = {
    R(RETRO_DEVICE_ID_JOYPAD_A), R(RETRO_DEVICE_ID_JOYPAD_B), R(RETRO_DEVICE_ID_JOYPAD_X), R(RETRO_DEVICE_ID_JOYPAD_Y),
    R(RETRO_DEVICE_ID_JOYPAD_SELECT), R(RETRO_DEVICE_ID_JOYPAD_START), R(RETRO_DEVICE_ID_JOYPAD_R3),
    R(RETRO_DEVICE_ID_JOYPAD_UP), R(RETRO_DEVICE_ID_JOYPAD_DOWN), R(RETRO_DEVICE_ID_JOYPAD_LEFT), R(RETRO_DEVICE_ID_JOYPAD_RIGHT),
    R(RETRO_DEVICE_ID_JOYPAD_R2), -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr int kRetroSideways[WiInputCount] = {
    R(RETRO_DEVICE_ID_JOYPAD_X), R(RETRO_DEVICE_ID_JOYPAD_Y), R(RETRO_DEVICE_ID_JOYPAD_B), R(RETRO_DEVICE_ID_JOYPAD_A),
    R(RETRO_DEVICE_ID_JOYPAD_SELECT), R(RETRO_DEVICE_ID_JOYPAD_START), R(RETRO_DEVICE_ID_JOYPAD_R3),
    R(RETRO_DEVICE_ID_JOYPAD_UP), R(RETRO_DEVICE_ID_JOYPAD_DOWN), R(RETRO_DEVICE_ID_JOYPAD_LEFT), R(RETRO_DEVICE_ID_JOYPAD_RIGHT),
    R(RETRO_DEVICE_ID_JOYPAD_R2), -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr int kRetroNunchuk[WiInputCount] = {
    R(RETRO_DEVICE_ID_JOYPAD_A), R(RETRO_DEVICE_ID_JOYPAD_B), R(RETRO_DEVICE_ID_JOYPAD_START), R(RETRO_DEVICE_ID_JOYPAD_SELECT),
    R(RETRO_DEVICE_ID_JOYPAD_L), R(RETRO_DEVICE_ID_JOYPAD_R), R(RETRO_DEVICE_ID_JOYPAD_R3),
    R(RETRO_DEVICE_ID_JOYPAD_UP), R(RETRO_DEVICE_ID_JOYPAD_DOWN), R(RETRO_DEVICE_ID_JOYPAD_LEFT), R(RETRO_DEVICE_ID_JOYPAD_RIGHT),
    R(RETRO_DEVICE_ID_JOYPAD_R2), R(RETRO_DEVICE_ID_JOYPAD_X), R(RETRO_DEVICE_ID_JOYPAD_Y), R(RETRO_DEVICE_ID_JOYPAD_L2),
    -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr int kRetroClassic[WiInputCount] = {
    -1, -1, -1, -1, R(RETRO_DEVICE_ID_JOYPAD_SELECT), R(RETRO_DEVICE_ID_JOYPAD_START), R(RETRO_DEVICE_ID_JOYPAD_R3),
    R(RETRO_DEVICE_ID_JOYPAD_UP), R(RETRO_DEVICE_ID_JOYPAD_DOWN), R(RETRO_DEVICE_ID_JOYPAD_LEFT), R(RETRO_DEVICE_ID_JOYPAD_RIGHT),
    -1, -1, -1, -1,
    R(RETRO_DEVICE_ID_JOYPAD_A), R(RETRO_DEVICE_ID_JOYPAD_B), R(RETRO_DEVICE_ID_JOYPAD_X), R(RETRO_DEVICE_ID_JOYPAD_Y),
    R(RETRO_DEVICE_ID_JOYPAD_L), R(RETRO_DEVICE_ID_JOYPAD_R), R(RETRO_DEVICE_ID_JOYPAD_L2), R(RETRO_DEVICE_ID_JOYPAD_R2), -1};
static_assert(WiCentre == 23 && WiInputCount == 24, "the retro tables list every WiiInput");

const int *retro_table(int controller)
{
    switch (controller)
    {
    case WiiRemote: return kRetroRemote;
    case WiiSideways: return kRetroSideways;
    case WiiClassic: return kRetroClassic;
    default: return kRetroNunchuk; /* the Nunchuk device, also for two controllers */
    }
}

/* Sticks pushed past half way are the D-pad. */
std::uint16_t stick_dpad(std::int16_t x, std::int16_t y, const int *retro)
{
    constexpr int kPush = 16000;
    std::uint16_t out = 0;
    auto set = [&](int input) {
        if (retro[input] >= 0)
            out |= static_cast<std::uint16_t>(1u << retro[input]);
    };
    if (y < -kPush)
        set(WiUp);
    if (y > kPush)
        set(WiDown);
    if (x < -kPush)
        set(WiLeft);
    if (x > kPush)
        set(WiRight);
    return out;
}

const aim::Basis &pose_basis(int pose)
{
    switch (pose)
    {
    case PoseTriggerRight: return aim::kGripUprightRight;
    case PoseTriggerLeft: return aim::kGripUprightLeft;
    case PoseFacingRight: return aim::kGripFacingRight;
    case PoseFacingLeft: return aim::kGripFacingLeft;
    default: return aim::kGripNormal;
    }
}

/* The hold gravity shows, for the grip setting: -1 when it isn't clear (or
 * the setting doesn't allow what is seen). */
int classify_pose(const float accel[3], const WiiConfig &c, bool second)
{
    if (c.controller == WiiSideways || c.controller == WiiClassic || c.controller == WiiGameCube ||
        c.grip == GripBothHands)
        return PoseFlat; /* always held in both hands (Dolphin turns a sideways Remote itself) */
    const float n = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (n < 0.6f || n > 1.4f)
        return -1;
    const float ux = accel[0] / n, uy = accel[1] / n;
    const bool facing = c.grip == GripUprightFacing || (c.grip == GripAuto && c.controller == WiiTwoControllers);
    if (ux > 0.75f) /* its right side up: in the right hand */
        return facing ? PoseFacingRight : PoseTriggerRight;
    if (ux < -0.75f)
        return facing ? PoseFacingLeft : PoseTriggerLeft;
    if (uy > 0.75f && c.grip == GripAuto)
        return PoseFlat;
    (void)second;
    return -1;
}


Slot g_slots[kMaxPlayers];
/* Each player's own set-up (for_player of g_wii), made in set_wii: what their
 * controller plays as, and whether they hold a Move. */
WiiConfig g_wii_for[kMaxPlayers];
/* A player's PlayStation Move (4.5 Beta 4: any player's). Player 1's is the one the
 * PS5 itself reads (porpoise_move.cpp), or else one from REXCRAFT88's Move
 * Management, as players 2 to 4's always are: it runs its own Bluetooth host and
 * sends each player's Move (UDP 9976 + player) and Navigation controller (9980 +
 * player) over local UDP. His client (porpoise_move_bridge.hpp) checks every
 * packet and drops stale ones; its sockets are open only while a player is set
 * up to use them. The Move's motion goes through read_motion in its own slot, as
 * a DualSense's would. */
struct MoveRun
{
    Slot slot;
    Motion motion;
    /* Gravity as the Move feels it (g), learnt while it isn't swung: its swings
     * are scaled about it (WiiConfig::swing). */
    float gravity[3] = {0, 0, 1};
    bool live = false;         /* read last poll (a new connection starts afresh) */
    bool native = false;       /* and from the PS5 itself (player 1's) */
    std::uint64_t live_us = 0; /* when it last was */
    /* Its Start: held a second, the in-game menu (or, in Porpoise's menus, the
     * pointer); a tap, Start itself, given as it's let go. */
    std::uint64_t start_us = 0, start_pulse_us = 0;
    bool start_long = false;
    unsigned holds = 0;
    porpoise::move_bridge::Client mm, nav;
    bool mm_open = false, mm_was = false, nav_open = false, nav_was = false;
    std::uint64_t mm_tried_us = 0, nav_tried_us = 0; /* a port that wouldn't open is tried again later */
    std::uint64_t native_us = 0; /* player 1: when the PS5's own Move was last read */
    /* The rumble last sent to Move Management, and when: its packet lasts a
     * quarter second, so a running motor is sent again before that. */
    std::uint8_t sent_large = 0, sent_small = 0;
    std::uint64_t rumble_sent_us = 0;
};
MoveRun g_mv[kMaxPlayers];
/* Until when the Controller Hub is open (watching_players): players 2 to 4's
 * Moves are listened to outside Wii games then too, so it can show them. */
std::atomic<std::uint64_t> g_hub_until_us{0};
Slot &g_move_slot = g_mv[0].slot; /* player 1's Move */
/* Each slot knows whose it is (set again whenever a slot is emptied). */
const bool g_slots_named = [] {
    for (int p = 0; p < kMaxPlayers; ++p)
    {
        g_slots[p].player = p;
        g_mv[p].slot.player = p;
        g_mv[p].slot.is_move = true;
    }
    return true;
}();
PadSample g_samples[sample_capacity];
/* The Move's samples as read (g_samples carries them with the swing curve on):
 * what the camera's drift fix compares with the ball (porpoise_moveyaw.hpp). */
float g_move_true_accel[sample_capacity][3];
std::uint64_t g_yaw_report_us = 0;
const State g_none{};
Mapping g_mapping = preset(LayoutPlayStation);
std::atomic<bool> g_rumble_enabled{true};
unsigned g_polls_since_scan = 0;
bool g_ready = false;
std::int32_t g_initial_user = -1; /* player 1, always */
/* Each player's light bar (set_light_colours): blue, red, green, pink to begin with. */
constexpr ScePadLightBarParam kLightColours[kLightColourCount] = {
    {0, 96, 255}, {255, 36, 48}, {0, 210, 80}, {255, 60, 190}, {140, 60, 255},
    {255, 120, 0}, {255, 200, 0}, {0, 200, 255}, {255, 255, 255}, {0, 0, 0}};
int g_light[kMaxPlayers] = {0, 1, 2, 3};
std::atomic<int> g_trigger_feel{0}; /* set_trigger_feel */
std::atomic<int> g_turbo{-1};       /* set_turbo: the control, or -1 */
std::atomic<bool> g_quick_buttons{false};
std::atomic<bool> g_shot_buttons{true};
/* Dolphin reads input and sets rumble from its own CPU thread while the main
 * thread polls and players come and go: slots change under this lock. */
std::recursive_mutex g_lock;

std::int16_t stick(std::uint8_t value)
{
    int v = (static_cast<int>(value) - 128) * 256;
    return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
}

/* The trigger feel (set_trigger_feel) on this controller, where L2 / R2 are
 * the GameCube's L and R: Dolphin presses L or R all the way at about nine
 * tenths of the travel, so the click (the "weapon" effect: resistance that
 * gives way) comes just before it. */
void push_triggers(Slot &slot)
{
    if (slot.handle < 0)
        return;
    const int feel = g_trigger_feel.load(std::memory_order_relaxed);
    const bool l2 = g_mapping.control[GcL] == CtlL2 || g_mapping.control[GcR] == CtlL2;
    const bool r2 = g_mapping.control[GcL] == CtlR2 || g_mapping.control[GcR] == CtlR2;
    const int want = feel > 0 ? feel * 4 + (l2 ? 1 : 0) + (r2 ? 2 : 0) : 0;
    if (want == slot.triggers)
        return;
    slot.triggers = want;
    ScePadTriggerEffectParam p{};
    p.trigger_mask = 1 | 2;
    auto click = [&](ScePadTriggerEffectCommand &c, bool on) {
        if (!on || feel <= 0)
            return; /* mode 0: off */
        c.mode = 2;
        c.data[0] = feel >= 3 ? 5 : 6;    /* the resistance starts six tenths down (Extra Firm: five) */
        c.data[1] = 8;                    /* and gives way at eight */
        c.data[2] = feel >= 3 ? 8 : feel >= 2 ? 6 : 3; /* strength 0..8: Light 3, Firm 6, Extra Firm 8 */
    };
    click(p.l2, l2);
    click(p.r2, r2);
    const std::int32_t result = scePadSetTriggerEffect(slot.handle, &p);
    char line[96];
    std::snprintf(line, sizeof line, "pad: trigger feel %d (L2 %d, R2 %d): %#x", feel, l2, r2, unsigned(result));
    ps5::debug::mark(line);
}

void push_rumble(Slot &slot)
{
    if (slot.handle < 0)
        return;
    ScePadVibrationParam param{};
    /* The Move is the Remote: the game's rumble is the Move's alone (Oct 25:
     * the DualSense, the Nunchuk, buzzed along with it). */
    const WiiConfig &w = g_wii_for[slot.player];
    const bool move_has_it = !slot.is_move && w.active && w.move;
    if (g_rumble_enabled.load(std::memory_order_relaxed) && !move_has_it)
    {
        param.largeMotor = slot.motor_large;
        param.smallMotor = slot.motor_small;
    }
    (void)scePadSetVibration(slot.handle, &param);
}

void open_slot(int player, std::int32_t user, int attempts)
{
    std::int32_t handle = -1;
    /* A title started from the shell can arrive before the pad service has
     * published the device, so the first open is retried, as ProsperoLight does. */
    for (int attempt = 0; attempt < attempts && handle < 0; ++attempt)
    {
        handle = scePadOpen(user, 0, 0, nullptr);
        if (handle < 0)
            handle = scePadGetHandle(user, 0, 0); /* already open in this process */
        if (handle < 0 && attempt + 1 < attempts)
            (void)sceKernelUsleep(100000);
    }
    char line[96];
    if (handle < 0)
    {
        std::snprintf(line, sizeof line, "pad: player %d (user %d) could not open: %#x", player + 1, int(user),
                      unsigned(handle));
        ps5::debug::mark(line);
        return;
    }
    Slot &slot = g_slots[player];
    slot = Slot{};
    slot.player = player;
    slot.user = user;
    slot.handle = handle;
    (void)scePadSetVibrationMode(handle, 2);
    /* The gyroscope and accelerometer, for the Wii Remote. */
    const std::int32_t motion = scePadSetMotionSensorState(handle, true);
    /* Each player's light bar has their colour, so everyone knows which
     * controller is theirs (Settings > Controls): blue, red, green, pink. */
    (void)scePadSetLightBar(handle, &kLightColours[std::clamp(g_light[player], 0, kLightColourCount - 1)]);
    push_triggers(slot);
    std::snprintf(line, sizeof line, "pad: player %d is user %d, handle %d, motion %#x", player + 1, int(user),
                  int(handle), unsigned(motion));
    ps5::debug::mark(line);
}

void close_slot(int player)
{
    Slot &slot = g_slots[player];
    if (slot.handle < 0)
        return;
    slot.motor_large = slot.motor_small = 0;
    push_rumble(slot);
    (void)scePadResetLightBar(slot.handle);
    if (slot.triggers > 0)
    {
        const ScePadTriggerEffectParam off{1 | 2, {}, {}, {}};
        (void)scePadSetTriggerEffect(slot.handle, &off);
    }
    (void)scePadClose(slot.handle);
    char line[64];
    std::snprintf(line, sizeof line, "pad: player %d left", player + 1);
    ps5::debug::mark(line);
    slot = Slot{};
    slot.player = player;
}

/* Users who signed in join as the next free player; users who signed out leave. */
void rescan(const std::int32_t *ids)
{
    auto signed_in = [&](std::int32_t user) {
        for (int i = 0; i < 4; ++i)
            if (ids[i] == user)
                return true;
        return false;
    };
    for (int player = 1; player < kMaxPlayers; ++player)
        if (g_slots[player].handle >= 0 && !signed_in(g_slots[player].user))
            close_slot(player);
    /* Player 1 is the user who started Porpoise, even if the pad service was
     * late for them at start. */
    if (g_slots[0].handle < 0 && g_initial_user >= 0)
        open_slot(0, g_initial_user, 1);
    for (int i = 0; i < 4; ++i)
    {
        const std::int32_t user = ids[i];
        if (user < 0 || user == g_initial_user)
            continue;
        bool taken = false;
        for (const Slot &s : g_slots)
            taken |= s.handle >= 0 && s.user == user;
        if (taken)
            continue;
        for (int player = 1; player < kMaxPlayers; ++player)
            if (g_slots[player].handle < 0)
            {
                open_slot(player, user, 1);
                break;
            }
    }
}

/* The motion in this poll's readings: each one fused into the controller's
 * orientation, in order; the newest as it is for the readout and the core. */
/* From REXCRAFT88's Move Studio work (feature/move-nav-integration): a centring
 * made holding the controller still also takes the gyroscope's drift from its last
 * readings (their average), as long as they barely varied - a centring made while
 * turning leaves the drift as it was. */
void relearn_drift(Slot &slot)
{
    if (slot.gyro_hist_n < 16)
        return;
    const float n = float(slot.gyro_hist_n);
    float mean[3] = {0, 0, 0}, var[3] = {0, 0, 0};
    for (unsigned i = 0; i < slot.gyro_hist_n; ++i)
        for (int a = 0; a < 3; ++a)
            mean[a] += slot.gyro_hist[i][a] / n;
    for (unsigned i = 0; i < slot.gyro_hist_n; ++i)
        for (int a = 0; a < 3; ++a)
        {
            const float d = slot.gyro_hist[i][a] - mean[a];
            var[a] += d * d / n;
        }
    /* (the Move's gyroscope reads ~0.1 rad/s at rest: a real drift for it) */
    const float most = slot.is_move ? 0.3f : 0.08f;
    for (int a = 0; a < 3; ++a)
        if (!std::isfinite(mean[a]) || std::fabs(mean[a]) > most || var[a] > 0.0004f)
            return;
    for (int a = 0; a < 3; ++a)
        slot.fusion.bias[a] = mean[a];
}

/* The centre just set, and the hold it was set for: a game that starts soon after,
 * held the same way, keeps it (set_wii). */
void keep_centre(Slot &slot)
{
    slot.centred_at_us = monotonic_us();
    slot.kept_centre = slot.centre;
    slot.kept_basis = slot.basis;
    slot.kept_pose = slot.pose;
    const WiiConfig &w = g_wii_for[slot.player];
    slot.kept_controller = w.controller;
    slot.kept_grip = w.grip;
    slot.kept_move = w.move;
}

/* A controller that comes back (the Move off and on again): its motion starts
 * afresh - drift, orientation, smoothing and centre - so nothing stale carries
 * over. From REXCRAFT88's Move Studio work: reset on a new connection. */
void fresh_motion(Slot &slot)
{
    slot.last_motion_us = 0;
    slot.fusion = {};
    slot.smoother = {};
    slot.centred = false;
    slot.pose = -1;
    slot.pose_wait = 0;
    slot.up[0] = slot.up[1] = slot.up[2] = 0;
    slot.centre_hold = 0;
    slot.centre_was = false;
    slot.pointer_x = slot.pointer_y = 0;
    slot.gyro_hist_n = slot.gyro_hist_next = 0;
    slot.shake_frames = 0;
    slot.centred_at_us = 0;
}

Motion read_motion(Slot &slot, std::int32_t count)
{
    const WiiConfig &w = g_wii_for[slot.player];
    Motion m;
    const PadSample *order[sample_capacity];
    int n = 0;
    for (std::int32_t i = 0; i < count; ++i)
        if (g_samples[i].connected)
            order[n++] = &g_samples[i];
    std::sort(order, order + n, [](const PadSample *a, const PadSample *b) { return a->timestamp_us < b->timestamp_us; });
    if (n == 0)
        return m;
    float elapsed = 0;
    /* The strongest moment since the last poll: a swing's peak lasts a few
     * hundredths of a second and falls between polls, so the game is given the
     * sample furthest from gravity, not just the newest (Wii tennis missed swings). */
    const PadSample *peak = nullptr;
    float peak_dev = -1;
    for (int i = 0; i < n; ++i)
    {
        const PadSample &p = *order[i];
        if (slot.last_motion_us && p.timestamp_us <= slot.last_motion_us)
            continue; /* already seen */
        {
            float d2 = 0;
            for (int a = 0; a < 3; ++a)
            {
                const float d = p.acceleration[a] - slot.up[a];
                d2 += d * d;
            }
            if (d2 > peak_dev)
            {
                peak_dev = d2;
                peak = &p;
            }
        }
        const float dt = slot.last_motion_us ? float(p.timestamp_us - slot.last_motion_us) * 1e-6f : 0.0f;
        slot.fusion.steady = slot.is_move; /* (the Move's drift, learnt its own way) */
        aim::fuse(slot.fusion, p.angular_velocity, p.acceleration, dt);
        if (&slot == &g_move_slot && g_mv[0].native && slot.fusion.started)
        {
            const float turning[3] = {p.angular_velocity[0] - slot.fusion.bias[0],
                                      p.angular_velocity[1] - slot.fusion.bias[1],
                                      p.angular_velocity[2] - slot.fusion.bias[2]};
            porpoise::moveyaw::imu(double(p.timestamp_us) * 1e-6, slot.fusion.q, g_move_true_accel[&p - g_samples],
                                   turning);
        }
        for (int a = 0; a < 3; ++a)
            slot.gyro_hist[slot.gyro_hist_next][a] = p.angular_velocity[a];
        slot.gyro_hist_next = (slot.gyro_hist_next + 1) % 32;
        slot.gyro_hist_n = std::min(slot.gyro_hist_n + 1, 32u);
        slot.last_motion_us = p.timestamp_us;
        elapsed += std::min(dt, 0.05f);
        const bool first = slot.up[0] == 0 && slot.up[1] == 0 && slot.up[2] == 0;
        const float k = first ? 1.0f : std::min(1.0f, std::min(dt, 0.05f) / 0.16f);
        for (int a = 0; a < 3; ++a)
            slot.up[a] += (p.acceleration[a] - slot.up[a]) * k;
    }
    const PadSample &last = *order[n - 1];
    const PadSample &strongest = peak && peak_dev > 0.04f ? *peak : last; /* (at rest: the newest) */
    m.valid = true;
    m.samples = unsigned(n);
    for (int i = 0; i < 3; ++i)
    {
        m.raw_accel[i] = strongest.acceleration[i];
        m.raw_gyro[i] = last.angular_velocity[i];
        m.gyro_bias[i] = slot.fusion.bias[i];
    }
    for (int i = 0; i < 4; ++i)
        m.orientation[i] = last.orientation[i];
    const float mag = std::sqrt(m.raw_accel[0] * m.raw_accel[0] + m.raw_accel[1] * m.raw_accel[1] +
                                m.raw_accel[2] * m.raw_accel[2]);

    /* How it is held. Read when the pointer is centred; after that a
     * different hold has to be seen steadily for a few seconds (so a punch or
     * a swing never flips it). */
    const int index = slot.player;
    const bool second = w.controller == WiiTwoControllers && index == 1;
    float g[3];
    for (int i = 0; i < 3; ++i)
        g[i] = m.raw_gyro[i] - slot.fusion.bias[i];
    const float turning = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    const int seen = classify_pose(slot.up, w, second);
    bool reposed = false;
    if (slot.pose < 0)
        slot.pose = seen >= 0 ? seen : expected_pose(w, second);
    else if (slot.centred && seen >= 0 && seen != slot.pose && turning < 1.0f)
    {
        if (seen != slot.pose_seen)
            slot.pose_wait = 0;
        slot.pose_seen = seen;
        slot.pose_wait += elapsed;
        if (slot.pose_wait > 3.0f)
        {
            slot.pose = seen;
            slot.pose_wait = 0;
            reposed = true;
        }
    }
    else
        slot.pose_wait = 0;

    /* Centring: half a second in (once gravity has settled, and the
     * controller is fairly still - or after three seconds regardless), when
     * the hold changes, and on R1 (L1 in the left hand). It makes the hold of
     * that moment the Remote held level and pointing at the middle of the
     * screen, so a natural grip reads as one. In two-controller play the
     * Remote's R1 centres the Nunchuk too. */
    /* R1 has to be held a moment with the controller fairly still: a squeeze
     * of the grip mid-swing (R1 sits just above R2) never centres. */
    const bool centre_held = (last.buttons & (pose_left_hand(slot.pose) ? pad_l1 : pad_r1)) != 0;
    const bool centre_button = w.controller != WiiSideways && w.controller != WiiClassic &&
                               w.controller != WiiGameCube && !second && !w.menu;
    bool pressed_centre = false;
    if (!centre_held || !centre_button)
    {
        slot.centre_hold = 0;
        slot.centre_was = false; /* here: centred during this hold already */
    }
    else if (!slot.centre_was)
    {
        slot.centre_hold = turning < 1.2f ? slot.centre_hold + elapsed : 0.0f;
        if (slot.centre_hold >= 0.3f)
            pressed_centre = slot.centre_was = true;
    }
    const bool settled = slot.fusion.started && slot.fusion.age > 0.5f && (turning < 0.8f || slot.fusion.age > 3.0f);
    const bool follow = second && slot.centre_seq != g_centre_seq;
    if (slot.centre_request)
    {
        slot.centre_request = false;
        pressed_centre = true;
    }
    if ((!slot.centred && settled) || reposed || pressed_centre || follow)
    {
        if ((pressed_centre || follow) && seen >= 0)
            slot.pose = seen; /* centring also reads the hold again */
        if (pressed_centre)
            relearn_drift(slot); /* REXCRAFT88: held still, the drift is what it reads */
        aim::level_grip(pose_basis(slot.pose), slot.up, slot.basis);
        if (pressed_centre && &slot == &g_move_slot && slot.centred && porpoise::debuglog::on())
        {
            /* How far the pointer had wandered since it was last centred (the
             * player points at the middle of the screen to centre). */
            const aim::Angles now = aim::remote_angles(slot.fusion, slot.basis);
            float yaw = now.yaw - slot.centre.yaw;
            while (yaw > 3.14159265f)
                yaw -= 6.2831853f;
            while (yaw < -3.14159265f)
                yaw += 6.2831853f;
            porpoise::debuglog::app("move: centred again: it had wandered %.1f deg across, %.1f deg up/down in %.0f s "
                                    "(drift %.3f %.3f %.3f rad/s)",
                                    yaw * 57.2958f, (now.pitch - slot.centre.pitch) * 57.2958f,
                                    double(monotonic_us() - slot.centred_at_us) * 1e-6, slot.fusion.bias[0],
                                    slot.fusion.bias[1], slot.fusion.bias[2]);
        }
        slot.centre = aim::remote_angles(slot.fusion, slot.basis);
        slot.centred = true;
        slot.pose_wait = 0;
        slot.centre_seq = g_centre_seq;
        /* REXCRAFT88: the pointer lands in the middle at once, not eased there. */
        slot.smoother = {};
        if (pressed_centre && w.controller == WiiTwoControllers && index == 0)
            ++g_centre_seq;
        if (&slot == &g_move_slot && g_mv[0].native)
            porpoise::moveyaw::centred(); /* (here is "ahead": the camera's fix starts from it) */
        if (pressed_centre)
        {
            ++slot.centrings;
            keep_centre(slot);
        }
    }
    m.centrings = slot.centrings;
    if (!slot.centred)
        slot.basis = pose_basis(slot.pose);
    m.pose = slot.pose;

    /* For the core: the Remote's (or the Nunchuk's) own axes, the gyroscope
     * without its drift. */
    aim::Basis motion_basis = slot.basis;
    if (w.controller == WiiSideways)
    {
        /* Held sideways (like an NES pad: the D-pad under the left thumb, so
         * the Remote's pointing end to the left) the Remote's own axes are a
         * quarter turn round: x (its left side) toward the player, y (its
         * back) to the right, z up.
         * The core and Dolphin's "Sideways Wii Remote" turn a sideways
         * Remote's motion a quarter each way (Input.cpp UpdateAccelerometer,
         * WiimoteEmu GetOrientation), cancelling out, so the core wants
         * exactly these axes. (Test 7 had the pointing end to the right: the
         * steering came out reversed.) */
        for (int c = 0; c < 3; ++c)
        {
            motion_basis.m[0][c] = slot.basis.m[1][c];  /* the Remote's left: toward the player */
            motion_basis.m[1][c] = -slot.basis.m[0][c]; /* its back: to the right */
            motion_basis.m[2][c] = slot.basis.m[2][c];  /* its top: up */
        }
    }
    /* Rumble turned off in Settings: the controller stays still, nothing to ease. */
    const bool rumbling = g_rumble_enabled.load(std::memory_order_relaxed) &&
                          (slot.motor_large > 0 || slot.motor_small > 0 || monotonic_us() < slot.rumble_until_us);
    {
        /* While it rumbles the motors' buzz rides on the reading: eased, so a
         * game reading the Remote's motion for a shake doesn't take the buzz
         * for one. As read otherwise. */
        float accel[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!slot.eased_valid || !rumbling)
                slot.eased[i] = m.raw_accel[i];
            else
                slot.eased[i] += (m.raw_accel[i] - slot.eased[i]) * 0.35f;
            accel[i] = slot.eased[i];
        }
        slot.eased_valid = true;
        aim::to_remote(motion_basis, accel, m.accel);
    }
    aim::to_remote(motion_basis, g, m.gyro);

    /* Where it points, against the centre. */
    const aim::Angles now = aim::remote_angles(slot.fusion, slot.basis);
    m.roll = now.roll;
    m.centred = slot.centred;
    if (slot.centred)
    {
        float dyaw = now.yaw - slot.centre.yaw;
        while (dyaw > 3.14159265f)
            dyaw -= 6.2831853f;
        while (dyaw < -3.14159265f)
            dyaw += 6.2831853f;
        m.rel_yaw = dyaw;
        m.rel_pitch = now.pitch - slot.centre.pitch;
    }
    aim::Scale scale = aim::scale_for(w.speed, w.half_x, w.half_y);
    const float reach = float(std::clamp(w.reach, 50, 200)) / 100.0f;
    scale.half_x /= reach;
    scale.half_y /= reach;
    if (w.pointer == PointerGyro)
    {
        float x = 0, y = 0;
        if (slot.centred)
        {
            /* Held calmly against an edge, a drifted pointer eases back. */
            if (turning < 0.5f)
                aim::ease_edge(now, slot.centre, scale, elapsed);
            aim::pointer(now, slot.centre, scale, x, y);
            slot.smoother.step(x, y, elapsed, w.smooth);
        }
        m.aim_x = w.invert_x ? -x : x;
        m.aim_y = w.invert_y ? -y : y;
    }
    m.touching = (last.touch_count & 0x7f) > 0;
    if (m.touching)
    {
        m.touch_x = last.touch[0].x;
        m.touch_y = last.touch[0].y;
    }
    if (w.pointer == PointerTouch)
    {
        if (m.touching)
        {
            slot.pointer_x = float(m.touch_x) / 1919.0f * 2.0f - 1.0f;
            slot.pointer_y = float(m.touch_y) / 1030.0f * 2.0f - 1.0f; /* the pad reads 0..~1030 down */
        }
        m.aim_x = slot.pointer_x;
        m.aim_y = slot.pointer_y;
    }
    m.pointer_x = std::clamp(m.aim_x, -1.0f, 1.0f);
    m.pointer_y = std::clamp(m.aim_y, -1.0f, 1.0f);
    /* A flick: well over 1 g. While the controller rumbles, its buzz alone
     * can pass that: then it takes the motion beyond gravity's (slot.up) of
     * over 2 g, two polls in a row. */
    bool flick = mag > 2.0f;
    if (rumbling)
    {
        float d2 = 0;
        for (int i = 0; i < 3; ++i)
        {
            const float d = m.raw_accel[i] - slot.up[i];
            d2 += d * d;
        }
        slot.flick_run = d2 > 4.0f ? slot.flick_run + 1 : 0;
        flick = slot.flick_run >= 2;
    }
    else
        slot.flick_run = 0;
    if (w.shake && flick)
        slot.shake_frames = 8;
    m.shaking = slot.shake_frames > 0;
    if (slot.shake_frames > 0)
        --slot.shake_frames;
    return m;
}

bool g_ff_buttons = true; /* set_fast_forward_buttons */

/* Player 1 with the GBA's modifier held: the controls are the GBA's buttons,
 * and nothing goes to the game (its pad reads as let go). Ahead of the touch
 * pad's shortcuts, so the touch pad can be the modifier, or a GBA button. */
/* late: called after the touch pad's shortcuts (fast forward, quick saves, the screenshot, the
 * menu) have taken their buttons, for a player who is the GBA (direct): they keep those, and the
 * rest of the controller is the GBA. Early (late false): a player holding their GBA button, ahead
 * of the shortcuts. */
bool read_gba(Slot &slot, std::uint32_t raw, const PadSample &sample, State &next, bool late = false)
{
    const long index = &slot - g_slots;
    if (index < 0 || index >= kMaxPlayers)
        return false;
    const GbaConfig &g_gba_ = g_gba[index];
    if (g_gba_.direct != late)
        return false;
    const bool held = g_gba_.direct || (g_gba_.modifier >= 0 && g_gba_.modifier < CtlCount &&
                                        (raw & kControlPadBit[g_gba_.modifier]));
    if (!g_gba_.active || !held)
        return false;
    next.connected = true;
    next.gba_held = true;
    if (!late)
        next.ps_menu_combo = (raw & (pad_options | pad_touch_pad)) == (pad_options | pad_touch_pad);
    for (int c = 0; c < CtlCount; ++c)
        if (raw & kControlPadBit[c])
            next.buttons |= kControlButton[c];
    for (int i = 0; i < GbaCount; ++i)
    {
        const int c = g_gba_.control[i];
        if (c < 0 || c >= CtlCount || (!g_gba_.direct && c == g_gba_.modifier) || !(raw & kControlPadBit[c]))
            continue;
        if (i < kGbaKeys)
            next.gba_keys |= static_cast<std::uint16_t>(1u << i);
        else if (i == GbaShowScreen)
            next.gba_show = true;
        else if (i == GbaMoveScreen)
            next.gba_moving = true;
        else if (i == GbaAdjustOpacity)
            next.gba_opacity = true;
        else if (i == GbaReset)
            next.gba_reset = true;
        else if (i == GbaMute)
            next.gba_mute = true;
        else if (i == GbaVolume)
            next.gba_volume = true;
        else if (i == GbaDisconnect)
            next.gba_disconnect = true;
        else if (i == GbaQuickSave)
            next.gba_quick_save = true;
        else if (i == GbaQuickLoad)
            next.gba_quick_load = true;
    }
    if (next.gba_moving || next.gba_opacity || next.gba_volume)
    {
        next.gba_move_x = stick(sample.left_x);
        next.gba_move_y = stick(sample.left_y);
    }
    else if (g_gba_.stick_dpad)
    {
        constexpr int kPush = 16000; /* past half way, as the Wii's stick D-pad */
        const std::int16_t x = stick(sample.left_x), y = stick(sample.left_y);
        if (y < -kPush)
            next.gba_keys |= 1u << GbaUp;
        if (y > kPush)
            next.gba_keys |= 1u << GbaDown;
        if (x < -kPush)
            next.gba_keys |= 1u << GbaLeft;
        if (x > kPush)
            next.gba_keys |= 1u << GbaRight;
    }
    if (late)
        return true; /* the shortcuts keep their own bookkeeping */
    /* The touch pad pressed as part of this isn't the game's Select (Minus)
     * when it is let go. */
    slot.touch_combo = true;
    slot.touch_pulse = 0;
    slot.raw_prev = raw;
    return true;
}

State read_slot(Slot &slot)
{
    const WiiConfig &w = g_wii_for[slot.player];
    const std::int32_t count = scePadRead(slot.handle, g_samples, sample_capacity);
    if (count == 0)
    {
        slot.base.ff_step = false; /* a press counts once, not again on every repeat of the state */
        slot.base.quick_save = slot.base.quick_load = slot.base.screenshot = false;
        return slot.base; /* nothing new: keep the last state */
    }
    if (count < 0 || count > sample_capacity)
        return State{}; /* refused, or the pad went away: nothing rather than a stuck button */
    const PadSample *newest = nullptr;
    for (std::int32_t i = 0; i < count; ++i)
        if (newest == nullptr || g_samples[i].timestamp_us > newest->timestamp_us)
            newest = &g_samples[i];
    if (newest == nullptr || !newest->connected || (newest->buttons & pad_intercepted) != 0)
        return State{}; /* off, or the system menu has the pad: release everything */
    {
        /* A DualSense's charge isn't in what Porpoise reads, as far as is
         * known: the sample's last bytes go to the log when they change (at
         * most every 20 seconds, 60 lines a run), to find out whether it's
         * there after all. */
        static int lines = 0;
        static_assert(sizeof(PadSample) - offsetof(PadSample, extension) == sizeof slot.tail, "the sample's tail");
        const auto *tail = reinterpret_cast<const std::uint8_t *>(newest) + offsetof(PadSample, extension);
        if (lines < 60 && std::memcmp(tail, slot.tail, sizeof slot.tail) != 0 &&
            newest->timestamp_us - slot.tail_at_us > 20000000ull)
        {
            std::memcpy(slot.tail, tail, sizeof slot.tail);
            slot.tail_at_us = newest->timestamp_us;
            ++lines;
            char line[160];
            int k = std::snprintf(line, sizeof line, "pad: tail of user %d:", int(slot.user));
            for (std::size_t i = 0; i < sizeof slot.tail && k > 0 && k < int(sizeof line) - 4; ++i)
                k += std::snprintf(line + k, sizeof line - std::size_t(k), " %02x", tail[i]);
            ps5::debug::mark(line);
        }
    }
    const Motion motion = read_motion(slot, count);

    /* The touch pad is the fast-forward modifier: touch pad + R1 steps fast
     * forward, touch pad + R2 holds it. R1 and R2 don't reach the game while
     * the touch pad is down, and the touch pad's own press (Minus on a Wii
     * Remote) goes on its release, unless it was a combo. */
    std::uint32_t b = newest->buttons;
    std::uint8_t right_trigger = newest->right_trigger;
    std::uint8_t left_trigger = newest->left_trigger;
    State next;
    if (read_gba(slot, b, *newest, next))
        return next;
    const bool quick = g_quick_buttons.load(std::memory_order_relaxed);
    const bool shot = g_shot_buttons.load(std::memory_order_relaxed);
    const bool touch_shortcuts = g_ff_buttons || quick || shot;
    if (touch_shortcuts && (b & pad_options) && (b & pad_touch_pad))
    {
        /* Options + touch pad (the menu): the touch pad's own press doesn't
         * follow when it's let go, whichever is let go first. */
        slot.touch_combo = true;
        b &= ~pad_touch_pad;
    }
    else if (touch_shortcuts)
    {
        const bool touch = (b & pad_touch_pad) != 0, was = (slot.raw_prev & pad_touch_pad) != 0;
        if (touch)
        {
            if (!was)
                slot.touch_combo = false;
            if (g_ff_buttons)
            {
                if ((b & pad_r1) && !(slot.raw_prev & pad_r1))
                {
                    next.ff_step = true;
                    slot.touch_combo = true;
                }
                if (b & pad_r2)
                {
                    next.ff_hold = true;
                    slot.touch_combo = true;
                }
                b &= ~(pad_r1 | pad_r2);
                right_trigger = 0;
            }
            if (quick)
            {
                /* Touch pad + L1 saves to the quick slot, touch pad + L2 loads it. */
                if ((b & pad_l1) && !(slot.raw_prev & pad_l1))
                {
                    next.quick_save = true;
                    slot.touch_combo = true;
                }
                if ((b & pad_l2) && !(slot.raw_prev & pad_l2))
                {
                    next.quick_load = true;
                    slot.touch_combo = true;
                }
                b &= ~(pad_l1 | pad_l2);
                left_trigger = 0;
            }
            if (shot)
            {
                /* Touch pad + Square: a screenshot. */
                if ((b & pad_square) && !(slot.raw_prev & pad_square))
                {
                    next.screenshot = true;
                    slot.touch_combo = true;
                }
                b &= ~pad_square;
            }
            b &= ~pad_touch_pad;
        }
        else if (was && !slot.touch_combo)
            slot.touch_pulse = 4; /* a plain press: the touch pad's own button now, for a few frames */
        if (slot.touch_pulse > 0)
        {
            --slot.touch_pulse;
            b |= pad_touch_pad;
        }
    }
    slot.raw_prev = newest->buttons;
    /* Turbo: the chosen control, while held, presses for two polls and lets
     * go for two (about fifteen presses a second at 60 frames). */
    const int turbo = g_turbo.load(std::memory_order_relaxed);
    if (turbo >= 0 && turbo < CtlCount && (b & kControlPadBit[turbo]))
    {
        if ((slot.turbo_tick++ / 2) % 2 == 1)
        {
            b &= ~kControlPadBit[turbo];
            if (turbo == CtlL2)
                left_trigger = 0;
            if (turbo == CtlR2)
                right_trigger = 0;
        }
    }
    else
        slot.turbo_tick = 0;
    /* A player who is the GBA: what the shortcuts left is the GBA's (the game gets nothing). */
    if (read_gba(slot, b, *newest, next, true))
    {
        next.ps_menu_combo = (newest->buttons & (pad_options | pad_touch_pad)) == (pad_options | pad_touch_pad);
        return next;
    }
    next.connected = true;
    for (int c = 0; c < CtlCount; ++c)
        if (b & kControlPadBit[c])
            next.buttons |= kControlButton[c];
    bool mapped[CtlCount] = {};
    for (int gc = 0; gc < GcCount; ++gc)
    {
        const int c = g_mapping.control[gc];
        if (c < 0 || c >= CtlCount)
            continue;
        mapped[c] = true;
        if (b & kControlPadBit[c])
            next.joypad |= static_cast<std::uint16_t>(1u << kGcRetro[gc]);
    }
    /* Controls no GameCube button uses keep the libretro meaning Dolphin gives
     * them: the Wii's minus (Select) and Home (R3), the GameCube's soft L / R
     * (L3 / R3) and the Triforce's test and coin (L, Select). */
    struct Pass
    {
        int control;
        unsigned retro;
    };
    constexpr Pass kPass[] = {{CtlL1, RETRO_DEVICE_ID_JOYPAD_L},
                              {CtlTouch, RETRO_DEVICE_ID_JOYPAD_SELECT},
                              {CtlL3, RETRO_DEVICE_ID_JOYPAD_L3},
                              {CtlR3, RETRO_DEVICE_ID_JOYPAD_R3}};
    for (const Pass &pass : kPass)
        if (!mapped[pass.control] && (b & kControlPadBit[pass.control]))
            next.joypad |= static_cast<std::uint16_t>(1u << pass.retro);
    next.left_x = stick(newest->left_x);
    next.left_y = stick(newest->left_y);
    next.right_x = stick(newest->right_x);
    next.right_y = stick(newest->right_y);
    if (w.active && w.nunchuk_right)
    {
        /* The Nunchuk's DualSense in the right hand: its right stick is the Nunchuk's. */
        std::swap(next.left_x, next.right_x);
        std::swap(next.left_y, next.right_y);
    }
    /* The GameCube's L and R are analog: on L2 / R2 they follow the trigger,
     * on any other control they are all or nothing. */
    auto analog = [&](int gc) -> std::int16_t {
        const int c = g_mapping.control[gc];
        if (c == CtlL2)
            return static_cast<std::int16_t>(left_trigger * 0x7fff / 255);
        if (c == CtlR2)
            return static_cast<std::int16_t>(right_trigger * 0x7fff / 255);
        return (c >= 0 && c < CtlCount && (b & kControlPadBit[c])) ? 0x7fff : 0;
    };
    next.l2 = analog(GcL);
    next.r2 = analog(GcR);
    next.ps_menu_combo = (newest->buttons & (pad_options | pad_touch_pad)) == (pad_options | pad_touch_pad);
    next.motion = motion;
    if (w.active && w.controller != WiiGameCube)
    {
        /* A Wii game: the Wii controller's buttons instead of the GameCube's
         * (a GameCube controller in a Wii game keeps the GameCube's). */
        auto bits = [&](const WiiLayout &lay, const int *retro) {
            std::uint16_t out = 0;
            for (int k = 0; k < lay.count; ++k)
            {
                const WiiBinding &w = lay.binds[k];
                if (w.control >= 0 && w.control < CtlCount && (b & kControlPadBit[w.control]) && retro[w.input] >= 0)
                    out |= static_cast<std::uint16_t>(1u << retro[w.input]);
            }
            if (lay.left_stick == StickDPad)
                out |= stick_dpad(next.left_x, next.left_y, retro);
            if (lay.right_stick == StickDPad)
                out |= stick_dpad(next.right_x, next.right_y, retro);
            return out;
        };
        const int *retro = retro_table(w.controller);
        next.joypad = bits(wii_layout(w, false, slot.pose), retro);
        /* A flick is sent as a shake only where the game doesn't feel the
         * motion itself. */
        const bool with_nunchuk = w.controller == WiiRemoteNunchuk || w.controller == WiiTwoControllers;
        const bool remote_feels = w.motion && (!with_nunchuk || g_nunchuk_motion);
        if (motion.shaking && w.controller != WiiClassic && !remote_feels)
            next.joypad |= static_cast<std::uint16_t>(1u << RETRO_DEVICE_ID_JOYPAD_R2);
        if (w.controller == WiiClassic)
        {
            next.l2 = static_cast<std::int16_t>(left_trigger * 0x7fff / 255);
            next.r2 = static_cast<std::int16_t>(right_trigger * 0x7fff / 255);
        }
        if (w.controller == WiiTwoControllers)
        {
            /* The same controller as the second one: its Nunchuk buttons. */
            next.nunchuk = bits(wii_layout(w, true, slot.pose), kRetroNunchuk);
            if (motion.shaking && !(w.motion && g_nunchuk_motion))
                next.nunchuk |= static_cast<std::uint16_t>(1u << RETRO_DEVICE_ID_JOYPAD_L2);
        }
    }
    return next;
}

/* Move Management's buttons (its wire's bits, docs/PROTOCOL.md in REXCRAFT88's
 * Move-Management-PS5) as porpoise::move's. T is L2 and its analog byte. */
std::uint32_t managed_buttons(const std::uint8_t *pad)
{
    std::uint32_t b = 0;
    std::memcpy(&b, pad, sizeof b);
    std::uint32_t out = 0;
    if (b & 0x1000u)
        out |= porpoise::move::kTriangle;
    if (b & 0x2000u)
        out |= porpoise::move::kCircle;
    if (b & 0x4000u)
        out |= porpoise::move::kCross;
    if (b & 0x8000u)
        out |= porpoise::move::kSquare;
    if (b & 0x1u)
        out |= porpoise::move::kSelect;
    if (b & 0x8u)
        out |= porpoise::move::kStart;
    if (b & 0x10000u)
        out |= porpoise::move::kPS;
    if (b & 0x20000u)
        out |= porpoise::move::kMove;
    if ((b & 0x100u) || pad[8] > 40)
        out |= porpoise::move::kT;
    return out;
}

/* A player's Move from Move Management (UDP 9976 + player): its samples since the
 * last poll, in a DualSense's axes (its stream already is: x, y up out of the
 * face, z toward the player), and its buttons. False while it isn't sending. A
 * port that can't be opened (another app has it) is tried again every two
 * seconds, not every frame. */
bool take_managed_move(int player, MoveRun &mv, std::uint64_t now, porpoise::move::Sample *ms, int &n,
                       std::uint32_t &pressed)
{
    n = 0;
    pressed = 0;
    if (!mv.mm_open)
    {
        if (mv.mm_tried_us && now - mv.mm_tried_us < 2000000ull)
            return false;
        mv.mm_tried_us = now;
        mv.mm_open = mv.mm.open(static_cast<std::uint16_t>(9976 + player));
        porpoise::debuglog::app("move: player %d: %s (UDP %d)", player + 1,
                                mv.mm_open ? "listening for Move Management's Move" : "Move Management's port couldn't be opened",
                                9976 + player);
        if (!mv.mm_open)
            return false;
    }
    mv.mm.poll(now);
    const bool here = mv.mm.fresh(now);
    if (here != mv.mm_was)
        porpoise::debuglog::app("move: player %d: %s", player + 1,
                                here ? "Move Management's Move connected" : "Move Management's Move gone");
    mv.mm_was = here;
    std::uint8_t pads[porpoise::move_bridge::Client::kRecent][120];
    const int got = mv.mm.take_pads(pads, std::min(porpoise::move_bridge::Client::kRecent, sample_capacity));
    if (!here)
        return false;
    for (int i = 0; i < got && n < sample_capacity; ++i)
    {
        const std::uint8_t *pad = pads[i];
        std::uint32_t connected = 0;
        std::memcpy(&connected, pad + 76, sizeof connected);
        if (connected != 1)
            continue;
        porpoise::move::Sample &out = ms[n];
        std::memcpy(&out.t_us, pad + 80, sizeof out.t_us);
        std::memcpy(out.accel, pad + 28, sizeof out.accel);
        std::memcpy(out.gyro, pad + 40, sizeof out.gyro);
        if (n > 0 && out.t_us <= ms[n - 1].t_us)
            continue; /* (never backwards: the fusion's step would be negative) */
        ++n;
    }
    pressed = managed_buttons(mv.mm.packet().pad);
    return true;
}

/* A player's Move, read this poll, onto their controller: its swings scaled to
 * what a Remote can feel, its motion through read_motion in its own slot, then
 * its buttons on top of the player's DualSense (the Nunchuk, if they hold one). */
void feed_move(int player, MoveRun &mv, const WiiConfig &w, const porpoise::move::Sample *ms, int n,
               std::uint32_t pressed, std::uint64_t now)
{
    const bool native = mv.native;
    for (int i = 0; i < n; ++i)
    {
        PadSample &p = g_samples[i];
        std::memset(&p, 0, sizeof p);
        p.connected = 1;
        p.timestamp_us = ms[i].t_us;
        p.orientation[3] = 1.0f;
        const float *a3 = ms[i].accel;
        if (native)
            for (int a = 0; a < 3; ++a)
                g_move_true_accel[i][a] = a3[a];
        const float mag = std::sqrt(a3[0] * a3[0] + a3[1] * a3[1] + a3[2] * a3[2]);
        if (mag > 0.85f && mag < 1.15f)
            for (int a = 0; a < 3; ++a)
                mv.gravity[a] += (a3[a] / mag - mv.gravity[a]) * 0.08f;
        /* The swing (what's past gravity) into what a Wii Remote can feel: a
         * Move reads up to 8 g a side, a Remote about 3.5. As it is (scaled by
         * the swing strength setting) up to a knee, so light motions stay light;
         * past it, eased toward the Remote's limit, so medium and hard swings
         * still differ and both reach what games count as a swing (the old
         * curve stopped short of it: tennis missed swings). */
        /* (x 0.85, Oct 24: hits came out too hard; the knee lowered with it) */
        const float k = std::clamp(w.swing, 20, 200) * 0.01f * 0.85f;
        float lin[3], l2 = 0;
        for (int a = 0; a < 3; ++a)
        {
            lin[a] = a3[a] - mv.gravity[a];
            l2 += lin[a] * lin[a];
        }
        const float len = std::sqrt(l2);
        constexpr float kKnee = 1.4f, kCap = 3.4f; /* g past gravity */
        const float x = len * k;
        float out = x;
        if (x > kKnee)
        {
            /* tanh(u) as 1 - 2 / (e^2u + 1): expf is imported already, tanhf isn't. */
            const float u = std::min((x - kKnee) / (kCap - kKnee), 20.0f);
            out = kKnee + (kCap - kKnee) * (1.0f - 2.0f / (std::exp(2.0f * u) + 1.0f));
        }
        const float scale = len > 1e-4f ? out / len : k;
        for (int a = 0; a < 3; ++a)
        {
            p.acceleration[a] = mv.gravity[a] + lin[a] * scale;
            p.angular_velocity[a] = ms[i].gyro[a];
        }
        if (native && porpoise::debuglog::on())
            porpoise::debuglog::move_sample(ms[i].t_us, a3, ms[i].gyro, p.acceleration, pressed);
        p.buttons = (pressed & porpoise::move::kSquare) ? pad_r1 : 0;
    }
    if (n > 0)
    {
        if (native)
        {
            /* The camera's drift fix: turned in before the pointer is read. */
            const float turn = porpoise::moveyaw::take_correction();
            if (turn != 0)
                aim::turn_world(mv.slot.fusion, turn);
        }
        mv.motion = read_motion(mv.slot, n);
        if (native && porpoise::debuglog::on() && now >= g_yaw_report_us)
        {
            g_yaw_report_us = now + 5000000ull;
            const porpoise::moveyaw::Report r = porpoise::moveyaw::report();
            porpoise::debuglog::app("move: camera drift fix: %s, sure %.2f, %d swings, drift seen %.1f deg, %.1f deg taken out since centring%s",
                                    r.anchored ? "on" : "learning", r.confidence, r.pairs,
                                    r.error * 57.2958f, r.corrected * 57.2958f, r.mirrored ? " (picture mirrored)" : "");
        }
    }
    State &s = g_slots[player].state;
    s.motion = mv.motion;
    s.connected = true;
    std::uint32_t given = pressed & ~porpoise::move::kStart;
    if (pressed & porpoise::move::kStart)
    {
        if (!mv.start_us)
            mv.start_us = now;
        if (!mv.start_long && now - mv.start_us >= 1000000ull)
        {
            mv.start_long = true;
            ++mv.holds;
        }
    }
    else if (mv.start_us)
    {
        if (!mv.start_long)
            mv.start_pulse_us = now + 90000ull; /* (a few frames: games read it) */
        mv.start_us = 0;
        mv.start_long = false;
    }
    if (now < mv.start_pulse_us)
        given |= porpoise::move::kStart;
    porpoise::move::buttons(s, w, retro_table(w.controller), given);
    /* Held: the in-game menu opens (as Options + touch pad does): player 1's. */
    if (player == 0 && mv.start_long)
        s.ps_menu_combo = true;
    s.move = true;
    s.move_holds = mv.holds;
}

/* The player's motors to their Move through Move Management: when they change,
 * and again every 150 ms while one runs (each message lasts 250 ms there). */
void managed_rumble(int player, MoveRun &mv, std::uint64_t now)
{
    const Slot &slot = g_slots[player];
    const std::uint8_t large = slot.motor_large, small = slot.motor_small;
    const bool changed = large != mv.sent_large || small != mv.sent_small;
    if (!changed && !((large || small) && now - mv.rumble_sent_us >= 150000ull))
        return;
    if (mv.mm.rumble(large, small, now))
    {
        mv.sent_large = large;
        mv.sent_small = small;
        mv.rumble_sent_us = now;
    }
}

/* A player's Navigation controller from Move Management (UDP 9980 + player), as
 * the Nunchuk next to their Move. In Porpoise's own menus player 1's moves
 * around like a D-pad. */
void feed_nav(int player, MoveRun &mv, const WiiConfig &w, std::uint64_t now)
{
    if (!mv.nav_open)
    {
        if (mv.nav_tried_us && now - mv.nav_tried_us < 2000000ull)
            return;
        mv.nav_tried_us = now;
        mv.nav_open = mv.nav.open(static_cast<std::uint16_t>(9980 + player));
        porpoise::debuglog::app("nav: player %d: %s (UDP %d)", player + 1,
                                mv.nav_open ? "listening for Move Management's Navigation controller"
                                            : "the Navigation controller's port couldn't be opened",
                                9980 + player);
        if (!mv.nav_open)
            return;
    }
    mv.nav.poll(now);
    std::uint8_t drop[porpoise::move_bridge::Client::kRecent][120];
    (void)mv.nav.take_pads(drop, porpoise::move_bridge::Client::kRecent); /* (its newest is enough) */
    const bool here = mv.nav.fresh(now);
    if (here != mv.nav_was)
        porpoise::debuglog::app("nav: player %d: %s", player + 1,
                                here ? "Navigation controller connected" : "Navigation controller gone");
    mv.nav_was = here;
    if (!here)
        return;
    PadSample nav;
    std::memcpy(&nav, mv.nav.packet().pad, sizeof nav);
    State &s = g_slots[player].state;
    const std::int16_t nx = stick(nav.left_x), ny = stick(nav.left_y);
    if (std::abs(nx) > std::abs(s.left_x))
        s.left_x = nx;
    if (std::abs(ny) > std::abs(s.left_y))
        s.left_y = ny;
    s.connected = true;
    if (player == 0 && (!g_wii.active || g_wii.menu))
    {
        /* Porpoise's own menus: its buttons as the DualSense's, the stick as
         * the D-pad. */
        for (int control = 0; control < CtlCount; ++control)
            if (nav.buttons & kControlPadBit[control])
                s.buttons |= kControlButton[control];
        if (s.left_x < -12000)
            s.buttons |= BtnLeft;
        if (s.left_x > 12000)
            s.buttons |= BtnRight;
        if (s.left_y < -12000)
            s.buttons |= BtnUp;
        if (s.left_y > 12000)
            s.buttons |= BtnDown;
    }
    else if (w.active && w.controller == WiiRemoteNunchuk)
    {
        /* The Nunchuk (docs/controller-hub.md): the stick, L1 C, L2 Z; and
         * what the Remote's D-pad, minus, plus and Home are on it. */
        const int *retro = retro_table(w.controller);
        auto press = [&](int input) {
            if (retro[input] >= 0)
                s.joypad = static_cast<std::uint16_t>(s.joypad | (1u << retro[input]));
        };
        if (nav.buttons & pad_l1)
            press(WiC);
        if ((nav.buttons & pad_l2) || nav.left_trigger > 20)
            press(WiZ);
        if (nav.buttons & pad_up)
            press(WiUp);
        if (nav.buttons & pad_down)
            press(WiDown);
        if (nav.buttons & pad_left)
            press(WiLeft);
        if (nav.buttons & pad_right)
            press(WiRight);
        if (nav.buttons & pad_cross)
            press(WiMinus);
        if (nav.buttons & pad_circle)
            press(WiPlus);
        if (nav.buttons & 0x10000u) /* PS */
            press(WiHome);
    }
}
} // namespace

namespace porpoise::pad
{
Mapping preset(int layout)
{
    Mapping m{};
    if (layout == LayoutPlayStation)
    {
        /* Cross confirms, as on PlayStation. */
        m.control[GcA] = CtlCross;
        m.control[GcB] = CtlCircle;
        m.control[GcX] = CtlSquare;
        m.control[GcY] = CtlTriangle;
    }
    else
    {
        /* Dolphin's own: A on Circle, B on Cross. */
        m.control[GcA] = CtlCircle;
        m.control[GcB] = CtlCross;
        m.control[GcX] = CtlSquare;
        m.control[GcY] = CtlTriangle;
    }
    m.control[GcZ] = CtlR1;
    m.control[GcL] = CtlL2;
    m.control[GcR] = CtlR2;
    m.control[GcStart] = CtlOptions;
    m.control[GcUp] = CtlUp;
    m.control[GcDown] = CtlDown;
    m.control[GcLeft] = CtlLeft;
    m.control[GcRight] = CtlRight;
    return m;
}

std::uint32_t control_bit(int control)
{
    return control >= 0 && control < CtlCount ? kControlButton[control] : 0;
}

bool open()
{
    (void)sceUserServiceInitialize(nullptr);
    std::int32_t user_id = -1;
    if (sceUserServiceGetInitialUser(&user_id) < 0)
    {
        ps5::debug::mark("pad: no initial user; no pad this run");
        return false;
    }
    if (scePadInit() < 0)
    {
        ps5::debug::mark("pad: scePadInit failed");
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_ready = true;
    g_initial_user = user_id;
    open_slot(0, user_id, 10);
    std::int32_t ids[16];
    for (std::int32_t &id : ids)
        id = -1;
    if (sceUserServiceGetLoginUserIdList(ids) == 0)
        rescan(ids);
    return g_slots[0].handle >= 0;
}

void close()
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (int player = 0; player < kMaxPlayers; ++player)
        close_slot(player);
    g_ready = false;
}

void set_mapping(const Mapping &mapping)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_mapping = mapping;
    for (Slot &slot : g_slots)
        push_triggers(slot); /* L2 / R2 may be other buttons now */
}

void set_trigger_feel(int feel)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_trigger_feel.store(std::clamp(feel, 0, 3), std::memory_order_relaxed);
    for (Slot &slot : g_slots)
        push_triggers(slot);
}

void set_light_colours(const int *colours)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (int player = 0; player < kMaxPlayers; ++player)
    {
        g_light[player] = std::clamp(colours[player], 0, kLightColourCount - 1);
        if (g_slots[player].handle >= 0)
            (void)scePadSetLightBar(g_slots[player].handle, &kLightColours[g_light[player]]);
    }
}

void set_turbo(int control)
{
    g_turbo.store(control >= 0 && control < CtlCount ? control : -1, std::memory_order_relaxed);
}

void set_quick_buttons(bool enabled)
{
    g_quick_buttons.store(enabled, std::memory_order_relaxed);
}

void set_shot_buttons(bool enabled)
{
    g_shot_buttons.store(enabled, std::memory_order_relaxed);
}

std::int32_t user_of(int player)
{
    if (player < 0 || player >= kMaxPlayers || g_slots[player].handle < 0)
        return -1;
    return g_slots[player].user;
}

void set_fast_forward_buttons(bool enabled)
{
    g_ff_buttons = enabled;
}

/* 4.5: a light tick under the finger in the menus (moving through games,
 * dragging a menu): the small motor, briefly; poll() stops it. */
std::uint64_t g_tick_until_us = 0;
bool g_ticking = false;

void menu_tick(std::uint8_t level, int ms)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Slot &slot = g_slots[0];
    if (slot.handle < 0)
        return;
    ScePadVibrationParam param{};
    param.largeMotor = slot.motor_large;
    param.smallMotor = std::max(slot.motor_small, level);
    (void)scePadSetVibration(slot.handle, &param);
    g_tick_until_us = monotonic_us() + std::uint64_t(std::max(5, ms)) * 1000;
    g_ticking = true;
}

void set_rumble_enabled(bool enabled)
{
    g_rumble_enabled.store(enabled, std::memory_order_relaxed);
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (Slot &slot : g_slots)
        push_rumble(slot);
}

const State &poll()
{
#ifdef PORPOISE_DESKTOP
    porpoise::platform::pump(); /* the window's and controllers' events, on the main thread */
#endif
    /* Who is signed in, asked outside the lock: the emulator's thread reads
     * the controllers under it and shouldn't wait on the user service. */
    bool scan = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        if (g_ticking && monotonic_us() >= g_tick_until_us)
        {
            g_ticking = false;
            push_rumble(g_slots[0]); /* the tick over: the motors as they were */
        }
        if (g_ready && ++g_polls_since_scan >= polls_per_scan)
        {
            g_polls_since_scan = 0;
            scan = true;
        }
    }
    std::int32_t ids[16];
    for (std::int32_t &id : ids)
        id = -1;
    if (scan)
        scan = sceUserServiceGetLoginUserIdList(ids) == 0;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (scan && g_ready)
        rescan(ids);
    for (Slot &slot : g_slots)
    {
        slot.base = slot.handle >= 0 ? read_slot(slot) : State{};
        slot.state = slot.base;
    }
    /* Each player's Move and Navigation controller (4.5 Beta 4: any player's):
     * the Move is their Wii Remote, its motion through read_motion in its own
     * slot, Square held as the centring button. Player 1's points in
     * Porpoise's menus too. */
    const std::uint64_t now = monotonic_us();
    for (int player = 0; player < kMaxPlayers; ++player)
    {
        MoveRun &mv = g_mv[player];
        const WiiConfig &w = g_wii_for[player];
        /* Players 2 to 4's only in a Wii game, or while the Controller Hub
         * shows them: in a GameCube game or the menus a Move of theirs would
         * press buttons on their port. Player 1's as always. */
        const bool want_move =
            w.move && (player == 0 || g_wii.active || now < g_hub_until_us.load(std::memory_order_relaxed));
        const bool want_nav = want_move && w.navigation;
        if (want_move)
        {
            porpoise::move::Sample ms[sample_capacity];
            int n = 0;
            std::uint32_t pressed = 0;
            /* Player 1's from the PS5 itself; else (and for players 2 to 4) Move
             * Management's stream for this player. */
            bool live = player == 0 && porpoise::move::take(ms, sample_capacity, n, pressed);
            const bool native = live;
            if (native)
                mv.native_us = now;
            /* (Player 1's Move Management stream only once the PS5's own Move
             * has been gone a while: a dropout of a moment doesn't open it.) */
            if (!live && (player != 0 || now - mv.native_us > 2000000ull))
                live = take_managed_move(player, mv, now, ms, n, pressed);
            else if (mv.mm_open)
            {
                /* The PS5 has player 1's Move: Move Management's isn't wanted. */
                mv.mm.close();
                mv.mm_open = mv.mm_was = false;
                mv.sent_large = mv.sent_small = 0;
            }
            if (live && (!mv.live || native != mv.native) && now - mv.live_us > 2000000ull)
            {
                /* Back after a while (or here for the first time): its motion
                 * starts afresh. (A dropout of a moment keeps its centre.) */
                fresh_motion(mv.slot);
                if (native)
                    porpoise::moveyaw::reset();
                mv.gravity[0] = mv.gravity[1] = 0;
                mv.gravity[2] = 1;
            }
            mv.live = live;
            mv.native = live && native;
            if (live)
            {
                mv.live_us = now;
                feed_move(player, mv, w, ms, n, pressed, now);
                if (!native)
                    managed_rumble(player, mv, now);
            }
        }
        else
        {
            mv.live = mv.native = false;
            if (mv.mm_open)
            {
                mv.mm.close();
                mv.mm_open = mv.mm_was = false;
                mv.sent_large = mv.sent_small = 0;
                porpoise::debuglog::app("move: player %d: stopped listening to Move Management", player + 1);
            }
        }
        if (want_nav)
            feed_nav(player, mv, w, now);
        else if (mv.nav_open)
        {
            mv.nav.close();
            mv.nav_open = mv.nav_was = false;
            porpoise::debuglog::app("nav: player %d: stopped listening", player + 1);
        }
    }
    return g_slots[0].state;
}

const State &state(int player)
{
    return player >= 0 && player < kMaxPlayers ? g_slots[player].state : g_none;
}

State snapshot(int player)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return player >= 0 && player < kMaxPlayers ? g_slots[player].state : State{};
}

bool connected(int player)
{
    return player >= 0 && player < kMaxPlayers && g_slots[player].handle >= 0 && g_slots[player].state.connected;
}

bool playing(int player)
{
    if (player < 0 || player >= kMaxPlayers)
        return false;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return (g_slots[player].handle >= 0 && g_slots[player].state.connected) ||
           (g_wii.active && g_wii_for[player].move && g_mv[player].live);
}

int move_link(int player)
{
    if (player < 0 || player >= kMaxPlayers)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const MoveRun &mv = g_mv[player];
    if (!g_wii_for[player].move)
        return 0;
    return !mv.live ? 1 : mv.native ? 2 : 3;
}

int nav_link(int player)
{
    if (player < 0 || player >= kMaxPlayers)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (!g_wii_for[player].move || !g_wii_for[player].navigation)
        return 0;
    return g_mv[player].nav_was ? 2 : 1;
}

int connected_count()
{
    int n = 0;
    for (int player = 0; player < kMaxPlayers; ++player)
        n += connected(player) ? 1 : 0;
    return n;
}

GbaConfig gba_defaults()
{
    return GbaConfig{};
}

void set_gba(int player, const GbaConfig &config)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (player >= 0 && player < kMaxPlayers)
        g_gba[player] = config;
}

GbaConfig gba(int player)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return player >= 0 && player < kMaxPlayers ? g_gba[player] : GbaConfig{};
}

WiiConfig for_player(const WiiConfig &config, int player)
{
    if (player <= 0 || player >= kMaxPlayers)
        return config;
    /* Two controllers: player 2's DualSense is player 1's Nunchuk. */
    if (config.controller == WiiTwoControllers && player == 1)
        return config;
    WiiConfig c = config;
    c.menu = false; /* (Porpoise's own screens point with player 1's) */
    c.nunchuk_right = false;
    const int hold = std::clamp(config.hold[player], 0, int(HoldCount) - 1);
    if (hold == HoldDualSense)
    {
        const int as = config.as[player];
        const int same = config.controller == WiiTwoControllers ? int(WiiRemoteNunchuk) : config.controller;
        c.controller = as >= 0 && as < WiiControllerCount && as != WiiTwoControllers ? as : same;
        c.move = false;
        c.navigation = false;
    }
    else
    {
        /* A Move is the Remote: alone, or with the Nunchuk (a Navigation
         * controller, or the player's DualSense). It points with its own motion. */
        c.controller = hold == HoldMove ? WiiRemote : WiiRemoteNunchuk;
        c.move = true;
        c.navigation = hold == HoldMoveNav;
        c.pointer = PointerGyro;
        c.grip = GripAuto;
    }
    return c;
}

void set_wii(const WiiConfig &config)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const bool was = g_wii.active;
    WiiConfig before[kMaxPlayers];
    for (int p = 0; p < kMaxPlayers; ++p)
        before[p] = g_wii_for[p];
    g_wii = config;
    if (g_wii.move)
    {
        /* The Move points with its own motion, held like a Remote. */
        g_wii.pointer = PointerGyro;
        g_wii.grip = GripAuto;
    }
    for (int p = 0; p < kMaxPlayers; ++p)
        g_wii_for[p] = for_player(g_wii, p);
    for (int p = 0; p < kMaxPlayers; ++p)
    {
        const WiiConfig &w = g_wii_for[p];
        const bool turned = w.grip != before[p].grip || w.controller != before[p].controller || w.move != before[p].move;
        if (!(config.active && !was) && !turned)
            continue;
        for (Slot *each : {&g_slots[p], &g_mv[p].slot})
        {
            Slot &slot = *each;
            /* A game starting held as it was centred in the setup, in the last five
             * minutes: that centre carries into the game (REXCRAFT88's Move Studio
             * work keeps the setup's centre). */
            const bool recent = slot.centred_at_us && monotonic_us() - slot.centred_at_us < 300000000ull;
            if (config.active && !was && recent && slot.kept_controller == w.controller &&
                slot.kept_grip == w.grip && slot.kept_move == w.move)
            {
                slot.centre = slot.kept_centre;
                slot.basis = slot.kept_basis;
                slot.pose = slot.kept_pose;
                slot.centred = true;
                slot.pointer_x = slot.pointer_y = 0;
                slot.smoother = {};
                slot.centre_seq = g_centre_seq;
                continue;
            }
            /* A new game or a new way of holding it: find the middle again. */
            slot.pointer_x = slot.pointer_y = 0;
            slot.centred = false;
            slot.pose = -1;
            slot.fusion.age = 0.0f;
            slot.centre_seq = g_centre_seq;
        }
    }
}

int expected_pose(const WiiConfig &config, bool second)
{
    if (config.controller == WiiSideways || config.controller == WiiClassic || config.controller == WiiGameCube)
        return PoseFlat;
    switch (config.grip)
    {
    case GripBothHands: return PoseFlat;
    case GripUprightTrigger: return second ? PoseTriggerLeft : PoseTriggerRight;
    case GripUprightFacing: return second ? PoseFacingLeft : PoseFacingRight;
    default:
        if (config.controller == WiiTwoControllers)
            return second ? PoseFacingLeft : PoseFacingRight;
        return config.controller == WiiRemote ? PoseTriggerRight : PoseFlat;
    }
}

WiiLayout wii_layout_plain(const WiiConfig &config, bool second, int pose);

/* The control in the same place on the DualSense's other half. */
int mirror_control(int c)
{
    switch (c)
    {
    case CtlCross: return CtlDown;
    case CtlDown: return CtlCross;
    case CtlCircle: return CtlLeft;
    case CtlLeft: return CtlCircle;
    case CtlSquare: return CtlRight;
    case CtlRight: return CtlSquare;
    case CtlTriangle: return CtlUp;
    case CtlUp: return CtlTriangle;
    case CtlL1: return CtlR1;
    case CtlR1: return CtlL1;
    case CtlL2: return CtlR2;
    case CtlR2: return CtlL2;
    case CtlL3: return CtlR3;
    case CtlR3: return CtlL3;
    default: return c; /* Options and the touch pad stay as they are */
    }
}

int g_wii_buttons[4][CtlCount] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

void set_wii_buttons(const int (*sets)[CtlCount])
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (int s = 0; s < 4; ++s)
        for (int c = 0; c < CtlCount; ++c)
            g_wii_buttons[s][c] = std::clamp(sets[s][c], 0, CtlCount - 1);
}

int wii_button_set(int controller)
{
    switch (controller)
    {
    case WiiRemote:
    case WiiTwoControllers: return 1;
    case WiiSideways: return 2;
    case WiiClassic: return 3;
    default: return 0;
    }
}

WiiLayout wii_layout(const WiiConfig &config, bool second, int pose)
{
    WiiLayout lay = wii_layout_plain(config, second, pose);
    /* In a game, the player's own buttons: each binding moves to the control
     * the player put it on (Settings > Controls > Customize buttons). Porpoise's
     * own menus (menu = true) keep Porpoise's. */
    if (!second && !config.menu)
    {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        const int *perm = g_wii_buttons[wii_button_set(config.controller)];
        /* The Remote held in the left hand is the right hand's layout in a
         * mirror (Cross <-> down, R2 <-> L2, ...), and the player's buttons
         * were set on the right hand's: they go through the same mirror, so
         * a button moved to R1 is on L1 in the left hand. */
        const bool mirrored = ((config.controller == WiiRemote || config.controller == WiiTwoControllers) &&
                               pose_left_hand(pose < 0 ? expected_pose(config, second) : pose)) ||
                              config.nunchuk_right; /* (the Nunchuk's DualSense in the right hand) */
        if (config.nunchuk_right)
            std::swap(lay.left_stick, lay.right_stick); /* (read_slot swaps the sticks themselves) */
        for (int i = 0; i < lay.count; ++i)
        {
            const int c = lay.binds[i].control;
            if (c < 0 || c >= CtlCount)
                continue;
            lay.binds[i].control = mirrored ? mirror_control(perm[mirror_control(c)]) : perm[c];
        }
    }
    return lay;
}

WiiLayout wii_layout_plain(const WiiConfig &config, bool second, int pose)
{
    WiiLayout lay;
    if (pose < 0)
        pose = expected_pose(config, second);
    auto add = [&](const WiiBinding *b, std::size_t n) {
        for (std::size_t i = 0; i < n && lay.count < int(std::size(lay.binds)); ++i)
            lay.binds[lay.count++] = b[i];
    };
    const bool stick_aims = config.pointer == PointerStick;
    if (second)
    {
        add(kLayTwoNunchuk, std::size(kLayTwoNunchuk));
        lay.left_stick = StickNunchuk;
        lay.second_controller = true;
        return lay;
    }
    switch (config.controller)
    {
    case WiiRemote:
    case WiiTwoControllers:
        if (pose_left_hand(pose))
            add(kLayRemoteLeft, std::size(kLayRemoteLeft));
        else
        {
            add(kLayRemote, std::size(kLayRemote));
            add(kDPadAsDPad, std::size(kDPadAsDPad));
        }
        lay.left_stick = StickDPad;
        lay.right_stick = stick_aims ? StickPointer : StickDPad;
        break;
    case WiiSideways:
        add(kLaySideways, std::size(kLaySideways));
        add(kDPadAsDPad, std::size(kDPadAsDPad));
        lay.left_stick = StickDPad;
        lay.right_stick = stick_aims ? StickPointer : StickNothing;
        break;
    case WiiClassic:
        add(kLayClassic, std::size(kLayClassic));
        add(kDPadAsDPad, std::size(kDPadAsDPad));
        lay.left_stick = StickClassicLeft;
        lay.right_stick = StickClassicRight;
        break;
    case WiiGameCube:
        break; /* no Wii layout: the GameCube buttons (Settings > Controls) */
    default:
        add(kLayNunchuk, std::size(kLayNunchuk));
        add(kDPadAsDPad, std::size(kDPadAsDPad));
        lay.left_stick = StickNunchuk;
        /* The core tilts the Remote with the right stick unless it aims. */
        lay.right_stick = stick_aims ? StickPointer : StickTilt;
        break;
    }
    return lay;
}

void centre_now(int player)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (player >= 0 && player < kMaxPlayers)
    {
        g_slots[player].centre_request = true;
        if (g_wii_for[player].move)
            g_mv[player].slot.centre_request = true;
    }
}

void shift_centre(int player, float yaw, float pitch)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (player < 0 || player >= kMaxPlayers)
        return;
    Slot &slot = g_wii_for[player].move ? g_mv[player].slot : g_slots[player];
    slot.centre.yaw += yaw;
    slot.centre.pitch += pitch;
    if (slot.centred_at_us)
        slot.kept_centre = slot.centre; /* the five points' middle, kept for the game */
}

void set_nunchuk_motion(bool on)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_nunchuk_motion = on;
}

WiiConfig wii()
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return g_wii;
}

unsigned wii_device(int controller)
{
    /* The Dolphin core's devices (Input.cpp): a plain joypad is the Remote. */
    switch (controller)
    {
    case WiiRemote: return RETRO_DEVICE_JOYPAD;
    case WiiSideways: return (2 << 8) | RETRO_DEVICE_JOYPAD;
    case WiiClassic: return (4 << 8) | RETRO_DEVICE_JOYPAD;
    case WiiGameCube: return (6 << 8) | RETRO_DEVICE_JOYPAD; /* the core's GameCube Controller in Wii mode */
    default: return (3 << 8) | RETRO_DEVICE_JOYPAD; /* with the Nunchuk */
    }
}

unsigned wii_device_with_motion_plus(int controller)
{
    /* The core's MotionPlus devices (Input.cpp): the five Remotes plus 6. */
    const unsigned base = wii_device(controller);
    switch (base >> 8)
    {
    case 0: return (7u << 8) | RETRO_DEVICE_JOYPAD;  /* Remote */
    case 2: return (8u << 8) | RETRO_DEVICE_JOYPAD;  /* sideways */
    case 3: return (9u << 8) | RETRO_DEVICE_JOYPAD;  /* with the Nunchuk */
    case 4: return (10u << 8) | RETRO_DEVICE_JOYPAD; /* with the Classic Controller */
    default: return base;                            /* a GameCube controller takes none */
    }
}

bool wants_motion_plus(int setting, const std::string &game_id)
{
    if (setting == 1)
        return true;
    if (setting != 0)
        return false;
    /* The games that won't play without one: Wii Sports Resort, Skyward
     * Sword, Red Steel 2 and Wii Play: Motion, every region. */
    static const char *const kNeedIt[] = {"RZT", "SOU", "RD2", "SC8"};
    for (const char *id : kNeedIt)
        if (game_id.compare(0, 3, id) == 0)
            return true;
    return false;
}

void recenter(int player)
{
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (player >= 0 && player < kMaxPlayers)
    {
        g_slots[player].pointer_x = g_slots[player].pointer_y = 0;
        g_slots[player].centred = false;
    }
    if (player >= 0 && player < kMaxPlayers && g_wii_for[player].move)
    {
        g_mv[player].slot.pointer_x = g_mv[player].slot.pointer_y = 0;
        g_mv[player].slot.centred = false;
    }
}

void set_rumble(int player, bool strong, std::uint16_t strength)
{
    if (player < 0 || player >= kMaxPlayers)
        return;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Slot &slot = g_slots[player];
    const std::uint8_t level = static_cast<std::uint8_t>(strength >> 8);
    std::uint8_t &motor = strong ? slot.motor_large : slot.motor_small;
    /* A motor running, and a moment after it stops (the controller settles). */
    if (level > 0 || slot.motor_large > 0 || slot.motor_small > 0)
        slot.rumble_until_us = monotonic_us() + 200000;
    if (motor == level)
        return;
    motor = level;
    push_rumble(slot);
    /* A Move holder's rumble is the Move's: player 1's through the PS5 (which
     * keeps the level for whenever its Move is here), and any player's through
     * Move Management, sent from poll (managed_rumble). */
    if (player == 0)
        porpoise::move::set_rumble(std::max(slot.motor_large, slot.motor_small));
    MoveRun &mv = g_mv[player];
    if (mv.mm_open && mv.live && !mv.native && g_wii_for[player].move)
        managed_rumble(player, mv, monotonic_us());
}

void watching_players()
{
    g_hub_until_us.store(monotonic_us() + 500000ull, std::memory_order_relaxed);
}
} // namespace porpoise::pad
