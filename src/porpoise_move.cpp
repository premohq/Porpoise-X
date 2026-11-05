/* Porpoise - the PlayStation Move as player 1's Wii Remote (Move test builds).
 * See porpoise_move.hpp and docs/move-notes.md.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_move.hpp"
#include "porpoise_debuglog.hpp"
#include "porpoise_moveyaw.hpp"

#include <algorithm>
#include <cstdlib>

namespace porpoise::move
{
namespace
{
/* The ball's colors: what the Move's light is sent, how Porpoise draws it, and
 * the hue the PS3 Eye looks for (degrees). Blue, red, green and magenta are the
 * ones REXCRAFT88's Move Studio tracked well on a PS3 Eye; the others are to try. */
struct BallColor
{
    std::uint8_t r, g, b;
    std::uint32_t ui;
    const char *name;
};
constexpr BallColor kBalls[kBallColors] = {
    {255, 0, 255, 0xC64FA0, "Magenta"}, {0, 40, 255, 0x2F6FFF, "Blue"},   {255, 0, 0, 0xFF3A48, "Red"},
    {0, 255, 0, 0x10C860, "Green"},     {0, 255, 255, 0x1AC8FF, "Cyan"},  {255, 200, 0, 0xFFC81A, "Yellow"},
    {255, 80, 0, 0xFF8A1A, "Orange"},   {110, 0, 255, 0x8C4CFF, "Violet"},
};
} // namespace

std::uint32_t ball_rgb(int index)
{
    return kBalls[std::clamp(index, 0, kBallColors - 1)].ui;
}
const char *ball_name(int index)
{
    return kBalls[std::clamp(index, 0, kBallColors - 1)].name;
}
} // namespace porpoise::move

#ifdef PORPOISE_DESKTOP
/* The desktop build has no Move: nothing to put on top. */
namespace porpoise::move
{
void start() {}
void set_ball_color(int) {}
void ignore_lights() {}
void forget_lights() {}
bool take(Sample *, int, int &count, std::uint32_t &buttons)
{
    count = 0;
    buttons = 0;
    return false;
}
void buttons(porpoise::pad::State &, const porpoise::pad::WiiConfig &, const int *, std::uint32_t) {}
void set_rumble(std::uint8_t) {}
void set_camera(bool) {}
#ifdef PORPOISE_HOST_PREVIEW
/* tools/ui-preview, PV_EYE=1: a camera that sees a ball (a made-up picture). */
Status status()
{
    Status s;
    if (std::getenv("PV_EYE"))
    {
        s.camera_state = 4;
        s.camera = s.ball = true;
        s.ball_x = 0.62f;
        s.ball_y = 0.38f;
    }
    return s;
}
bool camera_picture(std::uint8_t *rgba, unsigned &seq)
{
    if (!std::getenv("PV_EYE") || seq == 1)
        return false;
    for (int y = 0; y < kPicH; ++y)
        for (int x = 0; x < kPicW; ++x)
        {
            std::uint8_t *o = rgba + (y * kPicW + x) * 4;
            const float dx = float(x) - 0.38f * kPicW, dy = float(y) - 0.38f * kPicH;
            const bool ball = dx * dx + dy * dy < 36;
            o[0] = ball ? 40 : std::uint8_t(60 + y / 3);
            o[1] = ball ? 90 : std::uint8_t(50 + x / 4);
            o[2] = ball ? 255 : std::uint8_t(45 + y / 4);
            o[3] = 255;
        }
    seq = 1;
    return true;
}
#else
Status status() { return {}; }
bool camera_picture(std::uint8_t *, unsigned &) { return false; }
#endif
void output_check_start() {}
void output_check_answer() {}
void output_check_stop() {}
} // namespace porpoise::move
#else
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "porpoise_notify.hpp"

extern "C"
{
    int sceSysmoduleLoadModuleInternal(std::uint32_t id);
    std::int32_t sceUserServiceGetInitialUser(std::int32_t *user_id);
    std::int32_t sceUserServiceGetLoginUserIdList(std::int32_t *ids);
    /* The device bus, linked (tooling/ps5-stubs/mbus_link_stub.c). */
    int sceMbusInit(void);
    int sceMbusResolveByUserId(int type, int user, int index);
    int sceMbusAddHandle(int type, int user, int handle);
    int sceMbusRemoveHandle(int type, int handle);
}

namespace porpoise::move
{
namespace
{
#include "eye_helper_elf.inc" /* kEyeHelper, kEyeHelperSize (tools/build-porpoise.sh) */

using namespace porpoise::pad;

/* ---- the log: /data/porpoise-move.txt ------------------------------------------------ */

std::mutex g_log_lock;
FILE *g_log = nullptr;

double now()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
const double g_t0 = now();

/* (usleep, not sleep: this title imports usleep already) */
void nap(int seconds)
{
    for (int i = 0; i < seconds * 10; ++i)
        usleep(100000);
}

void say(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_log_lock);
    if (!g_log)
        g_log = std::fopen("/data/porpoise-move.txt", "w");
    if (!g_log)
        return;
    std::fprintf(g_log, "%8.2f  ", now() - g_t0);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

/* ---- what the threads read, for take() --------------------------------------------- */

struct Shared
{
    /* The motion samples not taken yet (a ring). */
    Sample ring[512];
    int head = 0, count = 0;
    bool connected = false;
    std::uint32_t buttons = 0;
    int trigger = 0;
    float accel[3] = {0, 0, 1}; /* g, the Move's axes: x right, y toward the ball, z out of its face */
    float gyro[3] = {0, 0, 0};  /* rad/s, the same axes, its drift taken off */
    double at = -10;
    bool ball = false;
    float bx = 0, by = 0, br = 0; /* the ball in the camera's picture, 640 x 480 */
    double ball_at = -10;
    int battery = -1;
    bool camera = false;
    std::uint8_t pic[kPicW * kPicH * 4] = {}; /* the camera's picture, small */
    unsigned pic_seq = 0;
};
std::mutex g_lock;
Shared g;
std::atomic<int> g_rumble{0};
std::atomic<bool> g_started{false};
/* When a screen last showed the camera's picture (camera_picture). */
std::atomic<double> g_pic_at{-10.0};
/* When Porpoise's screens last asked for status(): while they're up they show the
 * notices themselves, in the look in use. */
std::atomic<double> g_ui_at{-10.0};
bool ui_up()
{
    return now() - g_ui_at.load(std::memory_order_relaxed) < 1.0;
}

/* The Move's battery byte: 0..5, 0xEE charging, 0xEF charged. */
int battery_level(std::uint8_t b)
{
    if (b == 0xEE)
        return 6;
    if (b == 0xEF)
        return 5;
    return b <= 5 ? int(b) : -1;
}

/* ---- the Move: the device bus and /dev/hid -------------------------------------------- */

/* The ball's color now (set_ball_color), and the light off a moment while the
 * PS3 Eye notes the room's other lights (ignore_lights). */
std::atomic<int> g_ball{0};
std::atomic<double> g_dark_until{-10.0};
std::atomic<double> g_ignore_at{-10.0};
std::atomic<int> g_ignore_seq{0}, g_forget_seq{0};
std::atomic<int> g_ignored{-1};
struct Rgb
{
    std::uint8_t r, g, b;
};
Rgb ball_now()
{
    if (now() < g_dark_until.load(std::memory_order_relaxed))
        return {0, 0, 0};
    const BallColor &c = kBalls[std::clamp(g_ball.load(std::memory_order_relaxed), 0, kBallColors - 1)];
    return {c.r, c.g, c.b};
}

/* Light and rumble. The console accepts every way below (each returns 0) but the
 * Move doesn't light or buzz for the first, libSceMove's own; which one it obeys is
 * found with the player (Test & Tune's light and rumble check) and kept in
 * /data/porpoise-move-output.txt.
 *   1  0x80104839 {handle, &buf}: the light {1, r, g, b, 0, 0, 0, 0}, the rumble
 *      {0, 0, 0, 0, 1, strength, 0, 0}, apart (libSceMove's SetLightSphere and
 *      SetVibration)
 *   2  the same, after the handle is added to the device bus as type 0 (the type
 *      the Move answered to; type 5, libSceMove's, is refused here)
 *   3  the same with the device bus's number for the handle
 *   4  0x80104839, both at once: {1, r, g, b, 1, strength, 0, 0}
 *   5  0x80104837 {handle, &report}: the Move's own output report, 0x02 (PS3 Move)
 *   6  the same, 0x06 */
constexpr int kWays = 6;
std::atomic<int> g_way{1};
std::atomic<int> g_check{0};             /* the check: the way being shown now, 0 none */
std::atomic<bool> g_check_start{false}, g_check_stop{false}, g_check_yes{false};
std::atomic<int> g_check_result{0};      /* the last check: the way that worked, -1 none, 0 not run */

struct Output
{
    int fd = -1, hid = -1, dev = -1, user = -1;
    bool added = false; /* way 2's bus add done */
};

int out_ioctl(int fd, unsigned long request, int handle, void *buffer)
{
    struct
    {
        std::int32_t handle, pad;
        void *buffer;
    } req{handle, 0, buffer};
    return ::ioctl(fd, request, &req);
}

/* The light and the rumble, the given way. light / buzz: which to send (the ways
 * that send both at once always send both). The console's code (the worse one). */
int send_way(Output &o, int way, std::uint8_t r, std::uint8_t gr, std::uint8_t b, int rumble, bool light, bool buzz)
{
    const std::uint8_t s = static_cast<std::uint8_t>(std::clamp(rumble, 0, 255));
    if (way == 2 && !o.added)
    {
        o.added = true;
        say("move: bus add (type 0) -> %#x", unsigned(sceMbusAddHandle(0, o.user, o.hid)));
    }
    switch (way)
    {
    case 4:
    {
        std::uint8_t buf[8] = {1, r, gr, b, 1, s, 0, 0};
        return out_ioctl(o.fd, 0x80104839UL, o.hid, buf);
    }
    case 5:
    case 6:
    {
        std::uint8_t report[49] = {};
        report[0] = way == 5 ? 0x02 : 0x06;
        report[2] = r;
        report[3] = gr;
        report[4] = b;
        report[6] = s;
        return out_ioctl(o.fd, 0x80104837UL, o.hid, report);
    }
    default:
    {
        const int handle = way == 3 ? o.dev : o.hid;
        int worst = 0;
        if (light)
        {
            std::uint8_t buf[8] = {1, r, gr, b, 0, 0, 0, 0};
            worst = out_ioctl(o.fd, 0x80104839UL, handle, buf);
        }
        if (buzz)
        {
            std::uint8_t buf[8] = {0, 0, 0, 0, 1, s, 0, 0};
            const int k = out_ioctl(o.fd, 0x80104839UL, handle, buf);
            if (k < 0)
                worst = k;
        }
        return worst;
    }
    }
}

/* The light in the ball's color now (off while the camera notes the other lights). */
int send_ball(Output &o, int way, int rumble, bool light, bool buzz)
{
    const Rgb c = ball_now();
    return send_way(o, way, c.r, c.g, c.b, rumble, light, buzz);
}
/* Which light is lit: the color, or 99 for off. */
int ball_key()
{
    return now() < g_dark_until.load(std::memory_order_relaxed) ? 99 : g_ball.load(std::memory_order_relaxed);
}

void load_way()
{
    if (FILE *f = std::fopen("/data/porpoise-move-output.txt", "r"))
    {
        int way = 0;
        if (std::fscanf(f, "way %d", &way) == 1 && way >= 1 && way <= kWays)
            g_way = way;
        std::fclose(f);
    }
}
void save_way(int way)
{
    if (FILE *f = std::fopen("/data/porpoise-move-output.txt", "w"))
    {
        std::fprintf(f, "way %d\n", way);
        std::fclose(f);
    }
}

/* The gyroscope's counts to rad/s as learnt before (its fit), kept between runs. */
/* (one file for each Move model: their gyroscopes count differently) */
const char *gyro_file(bool zcm2)
{
    return zcm2 ? "/data/porpoise-move-gyro2.txt" : "/data/porpoise-move-gyro.txt";
}
float load_gyro(float fallback, bool zcm2)
{
    float k = fallback;
    if (FILE *f = std::fopen(gyro_file(zcm2), "r"))
    {
        float v = 0;
        if (std::fscanf(f, "%f", &v) == 1 && v > 0.3f && v < 4.0f)
            k = v;
        std::fclose(f);
    }
    return k;
}
void save_gyro(float ratio, bool zcm2)
{
    if (FILE *f = std::fopen(gyro_file(zcm2), "w"))
    {
        std::fprintf(f, "%.4f\n", ratio);
        std::fclose(f);
    }
}

/* A report's buttons: the Move's own input report starts at byte 9 of each 64. */
std::uint32_t decode(const std::uint8_t *m)
{
    std::uint32_t b = 0;
    if (m[1] & 0x01)
        b |= kSelect;
    if (m[1] & 0x08)
        b |= kStart;
    if (m[2] & 0x10)
        b |= kTriangle;
    if (m[2] & 0x20)
        b |= kCircle;
    if (m[2] & 0x40)
        b |= kCross;
    if (m[2] & 0x80)
        b |= kSquare;
    if (m[3] & 0x01)
        b |= kPS;
    if (m[4] & 0x40)
        b |= kMove;
    if (m[4] & 0x80)
        b |= kT;
    return b;
}

/* Sony made two Moves. The first (CECH-ZCM1, mini-USB) sends two motion samples a
 * report, each count offset by 0x8000; the newer one (CECH-ZCM2, micro-USB, sold
 * with PS VR) sends one, as signed counts, and has no magnetometer (psmoveapi,
 * psmove_get_half_frame). Buttons, trigger and battery sit in the same places. */
float count_of(const std::uint8_t *m, int at, bool zcm2)
{
    const int raw = int(m[at] | m[at + 1] << 8);
    return zcm2 ? float(std::int16_t(std::uint16_t(raw))) : float(raw - 0x8000);
}

/* Which Move a report (from its byte 9) reads as: its accelerometer, read the
 * right way, is a few g; read the other way, every axis is about 7 g or more.
 * 1 the first Move, 2 the newer one, 0 neither. */
int model_of(const std::uint8_t *m)
{
    float a1 = 0, a2 = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float x1 = count_of(m, 13 + 2 * i, false), x2 = count_of(m, 13 + 2 * i, true);
        a1 += x1 * x1;
        a2 += x2 * x2;
    }
    a1 = std::sqrt(a1);
    a2 = std::sqrt(a2);
    if (a1 > 1000 && a1 < 24000 && a2 > 40000)
        return 1;
    if (a2 > 1000 && a2 < 24000 && a1 > 40000)
        return 2;
    return 0;
}

/* A report as hex, for the log (the first n bytes). */
std::string hex_of(const std::uint8_t *r, int n)
{
    std::string out;
    char b[4];
    for (int i = 0; i < n; ++i)
    {
        std::snprintf(b, sizeof b, "%02x", r[i]);
        out += b;
        if (i % 8 == 7 && i + 1 < n)
            out += ' ';
    }
    return out;
}

/* A report from an opened device, into buf (64 bytes each); how many, or <= 0. */
int read_reports(int fd, int hid, std::uint8_t *buf, std::int32_t *flag)
{
    struct
    {
        std::int32_t handle, pad0;
        void *buffer;
        std::uint32_t count, pad1;
        std::int32_t *flag;
    } rd{hid, 0, buf, 16, 0, flag};
    const int r = ::ioctl(fd, 0xC0204834UL, &rd);
    return r <= 16 ? r : r / 0x40;
}

void close_device(int fd, int hid)
{
    struct
    {
        std::int32_t kind, handle;
    } cl{3, hid};
    ::ioctl(fd, 0x80084803UL, &cl);
}

/* The Move among the player's devices: every signed-in user, slot and type the
 * device bus answers for, each opened and kept only if its reports are a Move's
 * (no sticks: the four stick bytes sit at 0x7f). */
bool find_move(int fd, int &user_out, int &hid_out, int &dev_out, bool &zcm2_out, std::string &table)
{
    std::int32_t users[17], nu = 0, u = -1;
    if (sceUserServiceGetInitialUser(&u) == 0 && u != -1)
        users[nu++] = u;
    std::int32_t ids[16];
    for (auto &id : ids)
        id = -1;
    if (sceUserServiceGetLoginUserIdList(ids) == 0)
        for (int i = 0; i < 16; ++i)
            if (ids[i] != -1 && std::find(users, users + nu, ids[i]) == users + nu)
                users[nu++] = ids[i];
    struct Candidate
    {
        int dev, user;
    } cand[32];
    int nc = 0;
    table.clear();
    char part[64];
    for (int i = 0; i < nu; ++i)
        for (int type : {5, 0})
            for (int idx = 0; idx < 4; ++idx)
            {
                const int dev = sceMbusResolveByUserId(type, users[i], idx);
                std::snprintf(part, sizeof part, " %x/%d/%d:%x", unsigned(users[i]), type, idx, unsigned(dev));
                table += part;
                if (dev < 0 || nc >= 32)
                    continue;
                bool seen = false;
                for (int k = 0; k < nc; ++k)
                    seen |= cand[k].dev == dev;
                if (!seen)
                    cand[nc++] = {dev, users[i]};
            }
    /* Devices just found not to be a Move (the DualSense) aren't opened again for
     * 30 s (a Move caught waking up gets another look then). */
    static int not_move[16];
    static double not_move_at[16];
    static int n_not_move = 0;
    static std::uint8_t buf[0x400];
    {
        int keep = 0;
        for (int i = 0; i < n_not_move; ++i)
            if (now() - not_move_at[i] < 30)
            {
                not_move[keep] = not_move[i];
                not_move_at[keep++] = not_move_at[i];
            }
        n_not_move = keep;
    }
    std::string dumps;
    for (int k = 0; k < nc; ++k)
    {
        if (std::find(not_move, not_move + n_not_move, cand[k].dev) != not_move + n_not_move)
            continue;
        struct
        {
            std::int32_t device, kind, one;
        } req{cand[k].dev, 3, 1};
        const int hid = ::ioctl(fd, 0x800c4802UL, &req);
        std::snprintf(part, sizeof part, " | open %x: %x", unsigned(cand[k].dev), unsigned(hid));
        table += part;
        if (hid < 0)
            continue;
        /* The first Move: no sticks (the four stick bytes sit at 0x7f). The newer
         * one: its motion reads as signed counts, its battery byte is a battery's,
         * three reports running. */
        bool move = false, any = false, zcm2 = false;
        int newer = 0;
        std::int32_t flag = 0;
        for (int tries = 0; tries < 40 && !move && (!any || newer > 0); ++tries)
        {
            const int n = read_reports(fd, hid, buf, &flag);
            if (n > 0)
            {
                const std::uint8_t *r = buf + 0x40 * (n - 1);
                if (!any)
                    dumps += " | " + std::to_string(cand[k].dev) + ": " + hex_of(r, 48);
                any = true;
                const int model = model_of(r + 9);
                if (r[0x10] == 0x7f && r[0x11] == 0x7f && r[0x12] == 0x7f && r[0x13] == 0x7f)
                {
                    move = true;
                    zcm2 = model == 2;
                }
                else if (model == 2 && battery_level(r[9 + 12]) >= 0)
                {
                    if (++newer >= 3)
                        move = zcm2 = true;
                    else
                        usleep(15000);
                }
                else
                    newer = 0;
            }
            else
                usleep(10000);
        }
        table += any ? (move ? (zcm2 ? " Move (newer)" : " Move") : " not a Move") : " no reports";
        if (any && !move && n_not_move < 16)
        {
            not_move_at[n_not_move] = now();
            not_move[n_not_move++] = cand[k].dev;
        }
        if (move)
        {
            user_out = cand[k].user;
            hid_out = hid;
            dev_out = cand[k].dev;
            zcm2_out = zcm2;
            return true;
        }
        close_device(fd, hid);
    }
    table += dumps;
    return false;
}

void *hid_main(void *)
{
    load_way();
    say("move: the device bus: module service %#x %#x, init %#x", sceSysmoduleLoadModuleInternal(0x8000001d),
        sceSysmoduleLoadModuleInternal(0x8000001e), sceMbusInit());
    std::string last_table;
    double next_scan_log = 0;
    for (;;)
    {
        const int fd = ::open("/dev/hid", O_RDONLY);
        if (fd < 0)
        {
            say("move: /dev/hid didn't open (errno %d)", errno);
            nap(10);
            continue;
        }
        int user = -1, hid = -1, dev = -1;
        bool zcm2 = false;
        std::string table;
        if (!find_move(fd, user, hid, dev, zcm2, table))
        {
            /* (each new answer, and the same one again every 30 s) */
            if (table != last_table || now() >= next_scan_log)
            {
                say("move: no Move yet:%s", table.c_str());
                last_table = table;
                next_scan_log = now() + 30;
            }
            ::close(fd);
            nap(2);
            continue;
        }
        say("move: found it:%s", table.c_str());
        say("move: %s", zcm2 ? "the newer Move (CECH-ZCM2, micro-USB)" : "the first Move (CECH-ZCM1, mini-USB)");
        say("move: opened as %#x for user %#x (device %#x); bus add %#x", unsigned(hid), unsigned(user),
            unsigned(dev), unsigned(sceMbusAddHandle(5, user, hid)));
        Output out;
        out.fd = fd;
        out.hid = hid;
        out.dev = dev;
        out.user = user;
        {
            /* A short buzz: the Move says hello (the way kept from the check, else the first). */
            const int way = g_way.load(std::memory_order_relaxed);
            const int r = send_ball(out, way, 160, true, true);
            say("move: light and rumble, way %d -> %#x", way, unsigned(r));
            usleep(300000);
            send_ball(out, way, 0, false, true);
        }
        /* (No notice of its own: Porpoise's island shows it, and the console's
         * plain notices came up as "Debug:".) */
        bool low_told = false;

        static std::uint8_t buf[0x400];
        std::int32_t flag = 0;
        int errors = 0, sent_rumble = -1;
        bool rumble_told = false;
        double next_light = now() + 1.5, next_stats = now() + 10, last_read = now();
        int lit_key = ball_key();
        double period = 1.0 / 88; /* between reports (about 88 a second), learnt */
        long reports = 0;
        /* The gyroscope's scale and direction, learnt from how fast gravity turns in
         * the accelerometer against what the gyroscope says (least squares over the
         * calm turns): raw counts to rad/s. It starts from what was learnt before (a
         * Move's measured 1.6 of the guess the first time) and is kept for next time. */
        constexpr float kNominal = 3.14159265f / 180.0f / 16.4f;
        double fit_xy = 0, fit_yy = 0;
        float gyro_scale = kNominal * load_gyro(1.6f, zcm2), last_up[3] = {0, 0, 0};
        /* Motion samples a report, where the newest one starts, and the
         * accelerometer's counts for 1 g (the first Move's 4096; the newer one's
         * learnt from it held still, starting from the same). */
        const int frames = zcm2 ? 1 : 2;
        const int newest_at = zcm2 ? 0 : 6;
        float g_counts = 4096.0f;
        float saved_ratio = gyro_scale / kNominal;
        bool have_up = false, scale_told = false;
        double last_up_t = 0;
        /* The light and rumble check (Test & Tune): each way in turn, green and a buzz. */
        int check_way = 0, check_phase = 0;
        double check_at = 0;
        float peak = 0;
        for (;;)
        {
            const int n = read_reports(fd, hid, buf, &flag);
            const double t = now();
            if (n < 0)
            {
                if (++errors > 100)
                {
                    say("move: reads stopped (%#x); looking for it again", unsigned(n));
                    break;
                }
                usleep(20000);
                continue;
            }
            errors = 0;
            if (g_check_start.exchange(false))
            {
                check_way = 1;
                check_phase = 0;
                check_at = t;
                g_check = 1;
                g_check_result = 0;
                g_check_yes = false;
                g_check_stop = false;
                say("move: the light and rumble check");
            }
            if (check_way > 0)
            {
                if (g_check_stop.exchange(false))
                {
                    send_way(out, check_way, 0, 0, 0, 0, true, true);
                    say("move: check stopped at way %d", check_way);
                    check_way = 0;
                    g_check = 0;
                    sent_rumble = -1;
                    next_light = t;
                }
                else if (g_check_yes.exchange(false))
                {
                    g_way = check_way;
                    save_way(check_way);
                    g_check_result = check_way;
                    say("move: the player saw way %d: kept", check_way);
                    send_ball(out, check_way, 0, true, true);
                    check_way = 0;
                    g_check = 0;
                    sent_rumble = -1;
                    next_light = t + 1.5;
                }
                else
                {
                    const double e = t - check_at;
                    if (check_phase == 0)
                    {
                        const int r = send_way(out, check_way, 0, 255, 0, 200, true, true);
                        say("move: check way %d -> %#x", check_way, unsigned(r));
                        check_phase = 1;
                    }
                    else if (check_phase == 1 && e > 0.7)
                    {
                        send_way(out, check_way, 0, 255, 0, 0, false, true);
                        check_phase = 2;
                    }
                    else if (check_phase == 2 && e > 2.8)
                    {
                        send_way(out, check_way, 0, 0, 0, 0, true, true);
                        check_phase = 3;
                    }
                    else if (check_phase == 3 && e > 3.5)
                    {
                        if (check_way < kWays)
                        {
                            ++check_way;
                            g_check = check_way;
                            check_at = t;
                            check_phase = 0;
                        }
                        else
                        {
                            say("move: check: the player saw none of the ways");
                            check_way = 0;
                            g_check = 0;
                            g_check_result = -1;
                            sent_rumble = -1;
                            next_light = t;
                        }
                    }
                }
            }
            else
            {
                /* The light keeps lit (the Move turns it off without one), and the rumble. */
                const int way = g_way.load(std::memory_order_relaxed);
                const int rumble = g_rumble.load(std::memory_order_relaxed);
                if (rumble != sent_rumble)
                {
                    const int r = send_ball(out, way, rumble, false, true);
                    if (sent_rumble < 0 || (rumble > 0 && !rumble_told))
                    {
                        say("move: game rumble %d, way %d -> %#x", rumble, way, unsigned(r));
                        rumble_told = rumble > 0;
                    }
                    sent_rumble = rumble;
                }
                const int key = ball_key();
                if (t >= next_light || key != lit_key)
                {
                    send_ball(out, way, sent_rumble < 0 ? 0 : sent_rumble, true, false);
                    next_light = t + 1.5;
                    lit_key = key;
                }
            }
            if (n == 0)
            {
                usleep(2000);
                continue;
            }
            reports += n;
            period += ((t - last_read) / n - period) * 0.05;
            period = std::clamp(period, 0.004, 0.03);
            last_read = t;
            const std::uint8_t *newest = buf + 0x40 * (n - 1) + 9;

            /* The fit: the newest sample against the one from the last read. */
            {
                auto v = [&](int at) { return count_of(newest, at, zcm2); };
                const float w[3] = {v(25 + newest_at), v(27 + newest_at), v(29 + newest_at)};
                if (zcm2)
                {
                    const float c[3] = {v(13), v(15), v(17)};
                    const float counts = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
                    const float turn = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
                    if (turn < 200 && counts > 1000 && counts < 24000)
                        g_counts += (counts - g_counts) * 0.01f;
                }
                const float a[3] = {v(13 + newest_at) / g_counts, v(15 + newest_at) / g_counts,
                                    v(17 + newest_at) / g_counts};
                const float mag = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                const float spin = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
                if (mag > 0.9f && mag < 1.1f)
                {
                    const float up[3] = {a[0] / mag, a[1] / mag, a[2] / mag};
                    const double dt = t - last_up_t;
                    if (have_up && dt > 0.002 && dt < 0.05 && spin > 300 && spin < 6000)
                    {
                        /* d(up)/dt = -w x up, w in counts */
                        const float pred[3] = {-(w[1] * up[2] - w[2] * up[1]) * float(dt),
                                               -(w[2] * up[0] - w[0] * up[2]) * float(dt),
                                               -(w[0] * up[1] - w[1] * up[0]) * float(dt)};
                        for (int i = 0; i < 3; ++i)
                        {
                            fit_xy += double(up[i] - last_up[i]) * pred[i];
                            fit_yy += double(pred[i]) * pred[i];
                        }
                        if (fit_yy > 0)
                        {
                            const float k = float(fit_xy / fit_yy);
                            /* (enough turning seen, and a believable answer) */
                            if (fit_yy * kNominal * kNominal > 2.0 && std::fabs(k) > kNominal * 0.2f &&
                                std::fabs(k) < kNominal * 5.0f)
                            {
                                gyro_scale = k;
                                if (!scale_told)
                                {
                                    say("move: the gyroscope: %.3f of the guess (%s)", k / kNominal,
                                        k < 0 ? "turned round" : "as is");
                                    scale_told = true;
                                }
                            }
                        }
                    }
                    for (int i = 0; i < 3; ++i)
                        last_up[i] = up[i];
                    have_up = true;
                    last_up_t = t;
                }
                else
                    have_up = false;
            }

            /* Every sample (two a report), oldest first, in a DualSense's axes held flat:
             * the Move's x right, y toward the ball, z out of its face become x right,
             * y up out of the face, z toward the player. */
            {
                std::lock_guard<std::mutex> lock(g_lock);
                const int samples = n * frames;
                for (int r = 0; r < n; ++r)
                {
                    const std::uint8_t *m = buf + 0x40 * r + 9;
                    auto v = [&](int at) { return count_of(m, at, zcm2); };
                    for (int f = 0; f < frames; ++f)
                    {
                        const int k = r * frames + f;
                        const float ax = v(13 + 6 * f) / g_counts, ay = v(15 + 6 * f) / g_counts,
                                    az = v(17 + 6 * f) / g_counts;
                        const float wx = v(25 + 6 * f) * gyro_scale, wy = v(27 + 6 * f) * gyro_scale,
                                    wz = v(29 + 6 * f) * gyro_scale;
                        Sample sm;
                        sm.t_us = std::uint64_t((t - (samples - 1 - k) * period / frames) * 1e6);
                        sm.accel[0] = ax;
                        sm.accel[1] = az;
                        sm.accel[2] = -ay;
                        sm.gyro[0] = wx;
                        sm.gyro[1] = wz;
                        sm.gyro[2] = -wy;
                        g.ring[(g.head + g.count) % 512] = sm;
                        if (g.count < 512)
                            ++g.count;
                        else
                            g.head = (g.head + 1) % 512;
                    }
                }
                g.connected = true;
                g.buttons = decode(newest);
                g.trigger = newest[5];
                g.battery = battery_level(newest[12]);
                g.at = t;
                if (g.battery >= 0 && g.battery <= 1 && !low_told)
                {
                    low_told = true;
                    if (!ui_up() && porpoise::notify::rich_state() == 1)
                        porpoise::notify::rich("PlayStation Move", "Battery low", "", porpoise::notify::Sound::Default);
                }
            }
            {
                /* The newest sample, in the Move's own axes. */
                auto v = [&](int at) { return count_of(newest, at, zcm2); };
                const float a[3] = {v(13 + newest_at) / g_counts, v(15 + newest_at) / g_counts,
                                    v(17 + newest_at) / g_counts};
                const float mag = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                peak = std::max(peak, mag);
            }
            if (t >= next_stats)
            {
                std::uint32_t b;
                {
                    std::lock_guard<std::mutex> lock(g_lock);
                    b = g.buttons;
                }
                say("move: %ld reports in 10 s (every %.1f ms); buttons %03x; gyroscope %.3f of the guess; "
                    "strongest swing %.1f g; 1 g is %.0f counts",
                    reports, period * 1000, b, gyro_scale / kNominal, peak, g_counts);
                reports = 0;
                peak = 0;
                next_stats = t + 10;
                const float ratio = gyro_scale / kNominal;
                if (ratio > 0.3f && std::fabs(ratio - saved_ratio) > saved_ratio * 0.01f)
                {
                    save_gyro(ratio, zcm2);
                    saved_ratio = ratio;
                }
            }
            usleep(2000);
        }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g.connected = false;
        }
        sceMbusRemoveHandle(5, hid);
        close_device(fd, hid);
        ::close(fd);
        nap(2);
    }
    return nullptr;
}

/* ---- the PS3 Eye, through the helper ---------------------------------------------------- */

struct EyePacket
{
    std::uint32_t magic, type, frame;
    std::int16_t found, x, y, radius;
    std::uint16_t count;
    std::uint8_t r, g, b, pad;
    char text[200];
};

bool read_exact(int fd, void *to, std::size_t n, int wait_ms)
{
    auto *b = static_cast<std::uint8_t *>(to);
    for (std::size_t got = 0; got < n;)
    {
        pollfd pf{fd, POLLIN, 0};
        if (poll(&pf, 1, wait_ms) <= 0)
            return false;
        const ssize_t k = recv(fd, b + got, n - got, 0);
        if (k <= 0)
            return false;
        got += std::size_t(k);
    }
    return true;
}

bool send_helper()
{
    const int tcp = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in loader{};
    loader.sin_family = AF_INET;
    loader.sin_port = htons(9021);
    loader.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool sent = tcp >= 0 && connect(tcp, reinterpret_cast<sockaddr *>(&loader), sizeof loader) == 0;
    for (std::size_t off = 0; sent && off < kEyeHelperSize;)
    {
        const ssize_t n = send(tcp, kEyeHelper + off, kEyeHelperSize - off, 0);
        sent = n > 0;
        if (sent)
            off += std::size_t(n);
    }
    if (tcp >= 0)
    {
        shutdown(tcp, SHUT_WR);
        ::close(tcp);
    }
    return sent;
}

int connect_helper()
{
    sockaddr_in helper{};
    helper.sin_family = AF_INET;
    helper.sin_port = htons(47710);
    helper.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const double c0 = now();
    while (now() - c0 < 6.0)
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr *>(&helper), sizeof helper) == 0)
        {
            const int one = 1; /* (each position as it comes: not gathered a tenth of a second) */
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            return fd;
        }
        if (fd >= 0)
            ::close(fd);
        usleep(100000);
    }
    return -1;
}

std::atomic<bool> g_eye_on{false};
std::atomic<int> g_eye_state{0};
bool g_eye_forced = false;

void *eye_main(void *)
{
    bool told_missing = false;
    int failures = 0;
    for (;;)
    {
        if (!g_eye_on.load(std::memory_order_relaxed))
        {
            g_eye_state = 0;
            nap(1);
            continue;
        }
        if (!send_helper())
        {
            g_eye_state = 1;
            if (failures++ == 0)
                say("eye: no ELF loader on 9021; the camera needs one (trying again every 10 s)");
            nap(10);
            continue;
        }
        const int fd = connect_helper();
        if (fd < 0)
        {
            g_eye_state = 2;
            say("eye: the helper didn't answer");
            nap(10);
            continue;
        }
        if (g_eye_state.load() != 3)
            say("eye: connected to the helper");
        long positions = 0, seen = 0;
        bool shot_have = false;
        std::uint32_t shot_f0 = 0, shot_last = 0;
        double shot_t0 = 0, shot_off = 0, shot_period = 1 / 30.0;
        double next_stats = now() + 10;
        bool missing = false;
        /* What the helper has been told: the ball's color, and the requests to
         * note or forget the room's other lights. */
        int told_color = -1;
        int told_ignore = g_ignore_seq.load(), told_forget = g_forget_seq.load();
        bool told_video = false;
        static std::uint8_t rgb[kPicW * kPicH * 3];
        auto tell = [&](char c) { (void)send(fd, &c, 1, 0); };
        for (;;)
        {
            if (!g_eye_on.load(std::memory_order_relaxed))
                break; /* turned off: the helper ends with its connection */
            const int color = std::clamp(g_ball.load(std::memory_order_relaxed), 0, kBallColors - 1);
            if (color != told_color)
            {
                tell(char('a' + color));
                told_color = color;
            }
            if (told_ignore != g_ignore_seq.load() && now() >= g_ignore_at.load())
            {
                tell('I'); /* the light has been off a moment: what's lit now isn't the ball */
                told_ignore = g_ignore_seq.load();
            }
            if (told_forget != g_forget_seq.load())
            {
                tell('X');
                told_forget = g_forget_seq.load();
            }
            /* Its picture only while a screen shows it. */
            const bool video = now() - g_pic_at.load(std::memory_order_relaxed) < 0.5;
            if (video != told_video)
            {
                tell(video ? 'V' : 'v');
                told_video = video;
            }
            EyePacket p{};
            if (!read_exact(fd, &p, sizeof p, 3000) || p.magic != 0x45594550u)
            {
                say("eye: the helper went quiet");
                break;
            }
            if (p.type == 2)
            {
                p.text[sizeof p.text - 1] = 0;
                missing = std::strstr(p.text, "no PS3 Eye") != nullptr;
                const bool dropped = std::strstr(p.text, "video stopped") != nullptr;
                if (!missing || !told_missing)
                    say("eye: %s", p.text);
                told_missing = missing;
                if (missing || dropped)
                    g_eye_state = 3;
                else if (std::strstr(p.text, "camera set up"))
                    g_eye_state = 4;
                int lights = 0;
                if (std::sscanf(p.text, "helper: ignoring %d", &lights) == 1)
                    g_ignored = lights;
                continue;
            }
            if (p.type == 3)
                break;
            if (p.type == 4)
            {
                /* A picture: 160 x 120 RGB after the packet. */
                if (!read_exact(fd, rgb, sizeof rgb, 3000))
                {
                    say("eye: a picture was cut short");
                    break;
                }
                std::lock_guard<std::mutex> lock(g_lock);
                for (int i = 0; i < kPicW * kPicH; ++i)
                {
                    g.pic[i * 4] = rgb[i * 3];
                    g.pic[i * 4 + 1] = rgb[i * 3 + 1];
                    g.pic[i * 4 + 2] = rgb[i * 3 + 2];
                    g.pic[i * 4 + 3] = 255;
                }
                ++g.pic_seq;
                continue;
            }
            ++positions;
            g_eye_state = 4;
            /* When the picture was taken: from its frame count (the positions
             * can arrive a few at once). The camera's pace is learnt; the
             * line through the earliest arrivals is its clock. */
            const double arrived = now();
            if (!shot_have || p.frame <= shot_last || p.frame - shot_last > 300)
            {
                shot_have = true;
                shot_f0 = p.frame;
                shot_t0 = arrived;
                shot_off = arrived - p.frame * shot_period;
            }
            shot_last = p.frame;
            if (p.frame - shot_f0 >= 300)
                shot_period = std::clamp((arrived - shot_t0) / double(p.frame - shot_f0), 1 / 32.0, 1 / 26.0);
            shot_off = std::min(shot_off + 0.0005, arrived - p.frame * shot_period);
            const double shot = shot_off + p.frame * shot_period;
            porpoise::debuglog::camera(p.found != 0, p.x, p.y, p.radius, p.count, p.frame, shot * 1e6);
            if (p.found)
            {
                ++seen;
                std::lock_guard<std::mutex> lock(g_lock);
                g.ball = true;
                g.bx = p.x;
                g.by = p.y;
                g.br = p.radius;
                g.ball_at = now();
                porpoise::moveyaw::camera(shot, p.x, p.y, p.radius);
            }
            if (now() >= next_stats)
            {
                say("eye: %ld positions in 10 s, the ball in %ld", positions, seen);
                positions = seen = 0;
                next_stats = now() + 10;
            }
        }
        ::close(fd);
        if (g_eye_state.load() == 4)
            g_eye_state = 3; /* gone until it's found again */
        /* Not plugged in: looked for again in a few seconds (it can be plugged
         * in at any time). */
        nap(missing ? 4 : 2);
    }
    return nullptr;
}
} // namespace

void start()
{
    if (g_started.exchange(true))
        return;
    pthread_t t{};
    if (pthread_create(&t, nullptr, hid_main, nullptr) == 0)
        pthread_detach(t);
    /* The PS3 Eye isn't needed for the pointer (the Move's own motion is): it's
     * looked for while Settings has it on (set_camera), or the file
     * /data/porpoise-move-camera is there (the test builds' way). */
    if (access("/data/porpoise-move-camera", F_OK) == 0)
        g_eye_forced = true;
    if (pthread_create(&t, nullptr, eye_main, nullptr) == 0)
        pthread_detach(t);
    g_eye_on = g_eye_forced;
}

void set_camera(bool on)
{
    g_eye_on.store(on || g_eye_forced, std::memory_order_relaxed);
}

void set_ball_color(int index)
{
    g_ball = std::clamp(index, 0, kBallColors - 1);
}

void ignore_lights()
{
    /* The light off for a moment; the camera looks once it has been off a while. */
    const double t = now();
    g_dark_until = t + 1.6;
    g_ignore_at = t + 0.8;
    ++g_ignore_seq;
    say("eye: noting the room's other lights");
}

void forget_lights()
{
    ++g_forget_seq;
    say("eye: forgetting the room's lights");
}

void set_rumble(std::uint8_t strength)
{
    g_rumble.store(strength, std::memory_order_relaxed);
}

void output_check_start()
{
    g_check_start = true;
}
void output_check_answer()
{
    g_check_yes = true;
}
void output_check_stop()
{
    g_check_stop = true;
}

Status status()
{
    g_ui_at.store(now(), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_lock);
    Status s;
    s.connected = g.connected && now() - g.at < 0.5;
    s.battery = s.connected ? g.battery : -1;
    s.camera_state = g_eye_state.load(std::memory_order_relaxed);
    s.camera = s.camera_state == 4;
    s.ignored = g_ignored.load(std::memory_order_relaxed);
    s.learning = now() < g_dark_until.load(std::memory_order_relaxed);
    s.ball = s.camera && now() - g.ball_at < 0.5;
    s.ball_x = std::clamp(g.bx / 640.0f, 0.0f, 1.0f);
    s.ball_y = std::clamp(g.by / 480.0f, 0.0f, 1.0f);
    s.check = g_check.load(std::memory_order_relaxed);
    s.check_ways = kWays;
    s.check_result = g_check_result.load(std::memory_order_relaxed);
    s.way = g_way.load(std::memory_order_relaxed);
    return s;
}

bool camera_picture(std::uint8_t *rgba, unsigned &seq)
{
    g_pic_at.store(now(), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_lock);
    if (g.pic_seq == 0 || g.pic_seq == seq || g_eye_state.load(std::memory_order_relaxed) != 4)
        return false;
    std::memcpy(rgba, g.pic, sizeof g.pic);
    seq = g.pic_seq;
    return true;
}

bool take(Sample *out, int max, int &count, std::uint32_t &pressed)
{
    std::lock_guard<std::mutex> lock(g_lock);
    count = 0;
    pressed = 0;
    if (!g.connected || now() - g.at > 0.5)
        return false;
    /* The newest ones when there are more than max. */
    while (g.count > max)
    {
        g.head = (g.head + 1) % 512;
        --g.count;
    }
    for (; g.count > 0; --g.count, g.head = (g.head + 1) % 512)
        out[count++] = g.ring[g.head];
    pressed = g.buttons;
    return true;
}

void buttons(State &s, const WiiConfig &wii, const int *retro, std::uint32_t pressed)
{
    /* Porpoise's own menus: the Move's buttons as the DualSense's of the same name,
     * the Move button and T as Cross and R2. */
    if (pressed & (kCross | kMove))
        s.buttons |= BtnCross;
    if (pressed & kCircle)
        s.buttons |= BtnCircle;
    if (pressed & kSquare)
        s.buttons |= BtnSquare;
    if (pressed & kTriangle)
        s.buttons |= BtnTriangle;
    if (pressed & kStart)
        s.buttons |= BtnOptions;
    if (pressed & kT)
        s.buttons |= BtnR2;
    if (!wii.active || !retro)
        return;
    if (wii.controller != WiiRemote && wii.controller != WiiRemoteNunchuk && wii.controller != WiiTwoControllers)
        return;
    auto press = [&](int input) {
        if (retro[input] >= 0)
            s.joypad = static_cast<std::uint16_t>(s.joypad | (1u << retro[input]));
    };
    if (pressed & kMove)
        press(WiA);
    if (pressed & kT)
        press(WiB);
    if (pressed & kCross)
        press(WiOne);
    if (pressed & kCircle)
        press(WiTwo);
    if (pressed & kStart)
        press(WiPlus);
    if (pressed & kSelect)
        press(WiMinus);
    if (pressed & kPS)
        press(WiHome);
}
} // namespace porpoise::move
#endif /* PORPOISE_DESKTOP */
