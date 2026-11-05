/* Porpoise - the debug folder: logs kept in order, for finding what went wrong
 * and tuning games and controllers. Nothing is sent anywhere: the player
 * copies the files and sends them.
 *
 *   <data>/debug/README.txt
 *   <data>/debug/app/app-<date>-<time>.log          each time Porpoise starts: screens, choices, events
 *   <data>/debug/crashes/crash-<date>-<time>.txt    a crash's report, with the app log's last lines before it
 *   <data>/debug/controllers/hub-<date>-<time>.csv  the Controller Hub's motion, frame by frame
 *   <data>/debug/games/<game> [<id>]/<date>-<time>/ each time a game is played:
 *       game.log      what it was started with, and what happened
 *       performance.txt  what held it back (the CPU, the graphics...), in words, and what to try
 *       performance.csv  a line a second: frame rate, speed, stutters, Dolphin's threads
 *       motion-*.csv  what the controller felt and what the game was given, a line a frame
 *       move.csv      the Move's own readings, every one (about 175 a second)
 *       camera.csv    where the PS3 Eye saw the ball
 *       core.log      Dolphin's log for the game
 *       trace.txt     Porpoise's trace while it ran
 *
 * Older ones are removed past a count (each game keeps its last 10 plays).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace porpoise::debuglog
{
/* At start: where Porpoise keeps its data, and whether Settings has debug logs
 * on. A crash in the last run (in trace.txt) is filed in crashes/. */
void start(const std::string &data_dir, const std::string &trace_path, bool on);
void set_on(bool on);
bool on();

/* A line in the app log (and the game's, while one runs). */
void app(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* A game starts: its own folder for this play. Returns it ("" when off). */
std::string begin_game(const std::string &title, const std::string &id, const std::string &platform);
/* A line in the game's log. */
void game(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The game ends: a last line; Dolphin's log and the trace copied in. */
void end_game(const std::string &core_log_path, const char *summary);

/* The Move's readings (any thread): while a game runs, or the Controller Hub is open. */
void move_sample(std::uint64_t t_us, const float accel[3], const float gyro[3], const float out[3],
                 std::uint32_t buttons);
/* The camera (its thread): the ball, or none. */
/* frame: the camera's count; shot_us: when it was taken, on the Move readings' clock (move.csv's t_us). */
void camera(bool found, float x, float y, float radius, int pixels, unsigned frame = 0, double shot_us = 0);

/* The Controller Hub opens or closes: its own motion file while open. */
void hub(bool open);
/* A line in the hub's file (the header first: hub_line("#..."), then one a frame). */
void hub_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool hub_open();
/* Performance, for each play (performance.csv, a line a second, and at its end
 * performance.txt: what held the game back, in words, and what to try). */
struct PerfContext
{
    int resolution = 0;  /* Settings::resolution (the internal resolution's scale) */
    int output_res = 0;  /* Settings::output_res */
    bool dual_core = true, own_cores = true, fast_float = false, wait_shaders = true, frame_gen = false, vsync = true;
    int shader_mode = -1; /* Settings::shader_mode (-1: not told) */
    int audio_buffer = -1;
};
struct PerfSecond
{
    float t = 0;          /* seconds since the game started */
    float fps = 0;        /* frames shown */
    float game_hz = 60;   /* the game's own rate */
    float speed = 100;    /* the emulation's speed, percent */
    float worst_ms = 0;   /* the longest frame */
    int stutters = 0;     /* frames over one and a half of the game's frame time */
    float cpu_ms = 0, cpu_max_ms = 0; /* Dolphin's emulated CPU thread, a frame (average, longest) */
    float gpu_ms = 0;     /* Dolphin's video thread, busy a frame */
    float run_ms = 0;     /* the core's run, a frame (what Porpoise waited for) */
    float present_ms = 0, pace_ms = 0; /* showing the frame, and waiting for the next */
    int misses = 0;       /* vblanks missed */
    float audio_wait_ms = 0; /* waited for the speakers, a second */
    bool fast = false;    /* fast forward */
    float gpu_wait_ms = 0; /* the video thread waiting for the PS5's GPU to finish, a frame */
    float shader_ms = 0;  /* shaders made on the spot (the game waiting), this second */
    int shaders = 0;      /* how many */
    bool single_core = false; /* the CPU and the graphics on one thread (Dolphin's choice for the game) */
};
void perf_context(const PerfContext &c);
void perf_second(const PerfSecond &s);

/* For Save a Report: the newest app log, the last crashes, the Controller Hub's
 * newest file and the last game's newest play, copied into to/debug. Returns
 * the files written (relative to to). */
std::vector<std::string> gather(const std::string &to);
} // namespace porpoise::debuglog
