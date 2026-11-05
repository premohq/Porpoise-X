/* Porpoise - the PlayStation Move as player 1's Wii Remote (Move test builds).
 *
 * The Move is read straight from the console's HID device (the device bus says
 * which one is the player's, /dev/hid opens and reads it), as the console's own
 * Move library does; an app can't load that library. Its motion goes through
 * the same pointer as a DualSense's gyroscope (porpoise_pad.cpp), so the Wii
 * Remote Setup measures the screen for it too.
 * Notes: docs/move-notes.md.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstdint>

#include "porpoise_pad.hpp"

namespace porpoise::move
{
/* The Move's buttons. */
enum : std::uint32_t
{
    kTriangle = 1u << 0,
    kCircle = 1u << 1,
    kCross = 1u << 2,
    kSquare = 1u << 3,
    kSelect = 1u << 4,
    kStart = 1u << 5,
    kPS = 1u << 6,
    kMove = 1u << 7,
    kT = 1u << 8,
};

/* One motion sample, in a DualSense's axes (x right, y up out of the face, z toward the
 * player) as if it were held flat: the Move pointing at the TV, its buttons up. */
struct Sample
{
    std::uint64_t t_us;
    float accel[3]; /* g */
    float gyro[3];  /* rad/s */
};

/* Starts the Move's thread (once). */
void start();
/* The samples read since the last call (up to max) and the buttons now; false while
 * no Move is open. */
bool take(Sample *out, int max, int &count, std::uint32_t &buttons);
/* The Move's buttons into player 1's state: the Remote's in a Wii game (retro: the
 * core's table; Move A, T B, Cross 1, Circle 2, Start +, Select -, PS Home), and
 * Porpoise's own for its menus. */
void buttons(porpoise::pad::State &state, const porpoise::pad::WiiConfig &wii, const int *retro,
             std::uint32_t pressed);
/* The game's rumble for player 1, 0..255. */
void set_rumble(std::uint8_t strength);
/* The PS3 Eye on or off (Settings: PS3 Eye Camera). On, it's looked for every few
 * seconds, so it can be plugged in or out at any time. */
void set_camera(bool on);

/* The ball's color (Settings: move_color, 0..kBallColors - 1): the Move lights it
 * and the PS3 Eye looks for that color only. */
constexpr int kBallColors = 8;
void set_ball_color(int index);
std::uint32_t ball_rgb(int index); /* as Porpoise's screens draw it, 0xRRGGBB */
const char *ball_name(int index);
/* Other lights the PS3 Eye could take for the ball (a lamp, a TV's LED): the
 * Move's light goes off for a moment while the camera notes every colored light
 * still in view, and leaves those places out from then on (kept between runs).
 * forget_lights() clears them. From REXCRAFT88's Move Studio work
 * (github.com/REXCRAFT88/Porpoise-Dolphin-Emulator-for-PS5, branch
 * feature/move-nav-integration: "ignored wall-light areas"). */
void ignore_lights();
void forget_lights();

/* What Porpoise's screens show: the Move, its battery and the PS3 Eye. Asking
 * also tells the Move's thread that Porpoise's screens are up, so it leaves the
 * connected / battery notices to them (it posts the console's own otherwise). */
struct Status
{
    bool connected = false;
    int battery = -1;      /* 0..5; 6 charging; -1 not known yet */
    bool camera = false;   /* the PS3 Eye is set up and tracking */
    /* The PS3 Eye, step by step: 0 off (Settings), 1 waiting for the jailbreak's
     * ELF loader, 2 its helper not answering, 3 no camera plugged in, 4 working. */
    int camera_state = 0;
    bool ball = false;     /* it saw the ball in the last half second */
    float ball_x = 0, ball_y = 0; /* 0..1 across its picture */
    int check = 0;        /* the light and rumble check: the way being shown now (1..check_ways), 0 none */
    int check_ways = 0;
    int check_result = 0; /* the last check: the way that worked, -1 none did, 0 not run */
    int way = 0;          /* the way in use */
    int ignored = -1;     /* lights the PS3 Eye leaves out (ignore_lights), -1 not known yet */
    bool learning = false; /* the light is off now while the camera notes the others */
};
Status status();
/* What the PS3 Eye sees, small (kPicW x kPicH, RGBA): true and copied into rgba
 * when there's a newer picture than seq (then seq is its number). Asking keeps
 * the pictures coming for half a second (the helper sends them only then). */
constexpr int kPicW = 160, kPicH = 120;
bool camera_picture(std::uint8_t *rgba, unsigned &seq);
/* The light and rumble check (Test & Tune): each way the Move might take them, in
 * turn, the ball green and a buzz; answer() when the player sees or feels it (that
 * way is kept), stop() to end it. */
void output_check_start();
void output_check_answer();
void output_check_stop();
} // namespace porpoise::move
