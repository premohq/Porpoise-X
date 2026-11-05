/* Porpoise - the PS3 Eye keeping the Move's aim from drifting (porpoise_moveyaw.hpp).
 *
 * The camera's picture gives the ball's place: across (from the middle of the
 * picture) and away from the camera (from its size). Over a quarter of a
 * second, how the ball's speed changed as the camera saw it, and as the
 * accelerometer felt it (turned into the world by the Move's orientation,
 * gravity left out), are the same change seen from two frames: flat on the
 * floor, one turned from the other by the drift. A weighted fit over the last
 * few dozen such changes finds that turn; once centring has said where
 * "ahead" is, any turn after it is drift, and it's eased back out.
 *
 * The camera's distance (from the ball's size) is far noisier than across, so
 * it counts for less. Small changes, ones that disagree in size, and fits from
 * motion all one way are left out.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_moveyaw.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace porpoise::moveyaw
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kG = 9.81f;
constexpr float kBallR = 0.0225f;  /* metres: the Move's ball */
constexpr float kFocal = 560.0f;   /* pixels, the PS3 Eye at 640 x 480 (between its two lens settings) */
constexpr double kCamDelay = 0.05; /* seconds from the picture to its position reaching here */
/* The ball from the motion sensors, metres along the Move toward the TV (its
 * axes as the DualSense's held flat: -z). A swing turns the Move about the
 * wrist or the elbow, and the ball, out at its end, moves faster than the
 * sensors in its middle. */
constexpr float kLever = 0.11f;
constexpr float kAcrossW = 1.0f, kAwayW = 0.3f;
constexpr int kImuN = 512, kCamN = 64, kEventsN = 96;

struct Imu
{
    double t = -1;
    float x = 0, y = 0;   /* the world's flat acceleration, m/s^2 */
    float lx = 0, ly = 0; /* how much faster the ball moves than the sensors, turning (m/s) */
};
struct Cam
{
    double t = -1;
    float X = 0, Y = 0; /* across, away (metres) */
};
struct Event
{
    float x, y, X, Y; /* the change in speed: felt (the Move's world), seen (the camera's) */
};

std::mutex g_lock;
Imu g_imu[kImuN];
int g_imu_at = 0;
Cam g_cam[kCamN];
int g_cam_at = 0, g_cam_count = 0;
Event g_events[kEventsN];
int g_ev_at = 0, g_ev_count = 0;
bool g_anchored = false;
float g_ref = 0;
float g_pending = 0, g_corrected = 0, g_conf = 0, g_error = 0;
float g_ref_sum_c = 0, g_ref_sum_s = 0; /* the fits since centring, toward "ahead" */
int g_ref_n = 0;
int g_pairs = 0;

float wrap(float a)
{
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return a;
}

/* q (x, y, z, w) applied to v. */
void rotate(const float q[4], const float v[3], float out[3])
{
    const float ux = q[0], uy = q[1], uz = q[2], s = q[3];
    const float d = ux * v[0] + uy * v[1] + uz * v[2];
    const float uu = ux * ux + uy * uy + uz * uz;
    const float cx = uy * v[2] - uz * v[1], cy = uz * v[0] - ux * v[2], cz = ux * v[1] - uy * v[0];
    out[0] = 2 * d * ux + (s * s - uu) * v[0] + 2 * s * cx;
    out[1] = 2 * d * uy + (s * s - uu) * v[1] + 2 * s * cy;
    out[2] = 2 * d * uz + (s * s - uu) * v[2] + 2 * s * cz;
}

/* The ball's extra speed from turning, at the sample nearest t. */
bool lever_at(double t, float &x, float &y)
{
    double best = 1e9;
    for (int k = 0; k < kImuN; ++k)
    {
        const Imu &a = g_imu[k];
        if (a.t >= 0 && std::fabs(a.t - t) < best)
        {
            best = std::fabs(a.t - t);
            x = a.lx;
            y = a.ly;
        }
    }
    return best < 0.02;
}

/* The ball's change in speed between two times, as the sensors felt it (the
 * samples' trapezoids, and the turning's part). */
bool felt(double t0, double t1, float &dx, float &dy)
{
    dx = dy = 0;
    float l0x = 0, l0y = 0, l1x = 0, l1y = 0;
    if (!lever_at(t0, l0x, l0y) || !lever_at(t1, l1x, l1y))
        return false;
    int used = 0;
    double covered = 0;
    /* oldest to newest */
    for (int k = 1; k < kImuN; ++k)
    {
        const Imu &a = g_imu[(g_imu_at + k - 1) % kImuN], &b = g_imu[(g_imu_at + k) % kImuN];
        if (a.t < 0 || b.t <= a.t || b.t - a.t > 0.05)
            continue;
        const double lo = std::max(a.t, t0), hi = std::min(b.t, t1);
        if (hi <= lo)
            continue;
        const float w = float(hi - lo);
        dx += 0.5f * (a.x + b.x) * w;
        dy += 0.5f * (a.y + b.y) * w;
        covered += hi - lo;
        ++used;
    }
    dx += l1x - l0x;
    dy += l1y - l0y;
    return used > 4 && covered > 0.85 * (t1 - t0);
}

/* The ball's speed as seen at camera sample i (a line through it and its
 * neighbours, all close together in time). */
bool seen_speed(int i, float &vx, float &vy, double &t)
{
    if (g_cam_count < 5)
        return false;
    double st = 0, sx = 0, sy = 0, stt = 0, stx = 0, sty = 0;
    int n = 0;
    double prev = -1;
    for (int d = -2; d <= 2; ++d)
    {
        const Cam &c = g_cam[(i + d + kCamN) % kCamN];
        if (c.t < 0 || (prev >= 0 && (c.t <= prev || c.t - prev > 0.06)))
            return false;
        prev = c.t;
        st += c.t;
        sx += c.X;
        sy += c.Y;
        stt += c.t * c.t;
        stx += c.t * c.X;
        sty += c.t * c.Y;
        ++n;
    }
    const double den = n * stt - st * st;
    if (den <= 1e-9)
        return false;
    vx = float((n * stx - st * sx) / den);
    vy = float((n * sty - st * sy) / den);
    t = g_cam[i % kCamN].t;
    return true;
}

/* The weighted fit: the turn from the felt to the seen, and how well it fits.
 * mirror: the camera's picture taken as a mirror's (across the other way). */
bool fit_one(float mirror, float &theta, float &conf)
{
    double m11 = 0, m12 = 0, m22 = 0, b1 = 0, b2 = 0, sse0 = 0, wsum = 0;
    double w = 1;
    for (int k = 0; k < g_ev_count; ++k, w *= 0.95) /* newest first (the last ~20 count: drift moves on) */
    {
        const Event &e = g_events[(g_ev_at - 1 - k + kEventsN) % kEventsN];
        const double wx = w * kAcrossW, wy = w * kAwayW;
        const double X = mirror * e.X, Y = e.Y;
        /* X = c x - s y, Y = s x + c y */
        m11 += wx * e.x * e.x + wy * e.y * e.y;
        m12 += -wx * e.x * e.y + wy * e.x * e.y;
        m22 += wx * e.y * e.y + wy * e.x * e.x;
        b1 += wx * e.x * X + wy * e.y * Y;
        b2 += -wx * e.y * X + wy * e.x * Y;
        sse0 += wx * X * X + wy * Y * Y;
        wsum += w;
    }
    const double det = m11 * m22 - m12 * m12, tr = m11 + m22;
    if (wsum < 8 || det <= 0 || det < 0.04 * tr * tr)
        return false; /* too few, or all one way */
    const double c = (b1 * m22 - b2 * m12) / det, s = (m11 * b2 - m12 * b1) / det;
    const double scale = std::sqrt(c * c + s * s);
    if (scale < 0.35 || scale > 3.0)
        return false; /* the camera's sizes far off (a wrong ball, a reflection) */
    const double sse = sse0 - 2 * (c * b1 + s * b2) + (c * c * m11 + 2 * c * s * m12 + s * s * m22);
    conf = float(std::clamp(1.0 - sse / std::max(1e-9, sse0), 0.0, 1.0));
    theta = float(std::atan2(s, c));
    return true;
}

/* Which way round the picture is gets settled by the first clear fits, and
 * kept (a mirror is a fit no turn can match). */
int g_mirror_votes = 0; /* + as is, - mirrored */
bool fit(float &theta, float &conf)
{
    float ta = 0, ca = -1, tb = 0, cb = -1;
    const bool a = fit_one(1.0f, ta, ca), b = fit_one(-1.0f, tb, cb);
    if (std::abs(g_mirror_votes) < 6)
    {
        if (a && (!b || ca > cb + 0.2f))
            ++g_mirror_votes;
        else if (b && (!a || cb > ca + 0.2f))
            --g_mirror_votes;
        return false; /* (not yet) */
    }
    if (g_mirror_votes > 0)
    {
        theta = ta;
        conf = ca;
        return a;
    }
    theta = tb;
    conf = cb;
    return b;
}

void consider(double t_new)
{
    /* The newest sample with a speed: pair it with one a quarter-second before. */
    const int i_new = (g_cam_at - 3 + kCamN) % kCamN; /* (two after it for its line) */
    float vx1, vy1, vx0, vy0;
    double t1, t0;
    if (!seen_speed(i_new, vx1, vy1, t1) || t_new - t1 > 0.2)
        return;
    for (int back = 4; back < 16; ++back)
    {
        const int j = (i_new - back + kCamN) % kCamN;
        const double tj = g_cam[j].t;
        if (tj < 0 || t1 - tj > 0.4)
            break;
        if (t1 - tj < 0.18)
            continue;
        if (!seen_speed(j, vx0, vy0, t0))
            return;
        const float sX = vx1 - vx0, sY = vy1 - vy0;
        float fx, fy;
        if (!felt(t0, t1, fx, fy))
            return;
        const float fm = std::hypot(fx, fy), sm = std::hypot(sX, sY);
        if (fm < 0.6f || sm < 0.4f || sm > 3.0f * fm || fm > 3.0f * sm)
            return; /* too small to tell, or not the same motion */
        g_events[g_ev_at] = {fx, fy, sX, sY};
        g_ev_at = (g_ev_at + 1) % kEventsN;
        g_ev_count = std::min(g_ev_count + 1, kEventsN);
        ++g_pairs;
        float theta = 0, conf = 0;
        if (!fit(theta, conf))
            return;
        g_conf = conf;
        if (conf < 0.25f || g_ev_count < (g_anchored ? 30 : 60))
            return; /* (a fit from a handful of swings can be far out) */
        if (!g_anchored)
        {
            /* "Ahead": the average of the first few sure fits after centring. */
            g_ref_sum_c += conf * std::cos(theta);
            g_ref_sum_s += conf * std::sin(theta);
            if (++g_ref_n >= 20)
            {
                g_ref = std::atan2(g_ref_sum_s, g_ref_sum_c);
                g_anchored = true;
            }
            return;
        }
        const float e = wrap(theta - g_ref);
        g_error = e;
        if (std::fabs(e) < 0.035f) /* (the fits' own wobble: left alone) */
            return;
        /* A fifth of what's seen each fit, at most ~3 degrees (tuned on the
         * Oct 25 logs: the camera's heading and the Move's then disagreed half
         * as much); take_correction() hands it out a little a frame, so the
         * pointer glides rather than jumps. Turning the Move's world turns
         * what it felt: the events kept turn with it. */
        const float step = std::clamp(0.2f * e, -0.05f, 0.05f);
        g_pending += step;
        g_corrected += step;
        const float c = std::cos(step), s = std::sin(step);
        for (int k = 0; k < g_ev_count; ++k)
        {
            Event &ev = g_events[k];
            const float x = ev.x, y = ev.y;
            ev.x = c * x - s * y;
            ev.y = s * x + c * y;
        }
        return;
    }
}
} // namespace

void imu(double t, const float q[4], const float accel_g[3], const float gyro[3])
{
    float w[3];
    rotate(q, accel_g, w);
    /* gyro x (0, 0, -kLever) */
    const float lever[3] = {gyro[1] * -kLever, -gyro[0] * -kLever, 0.0f};
    float lw[3];
    rotate(q, lever, lw);
    std::lock_guard<std::mutex> lock(g_lock);
    g_imu[g_imu_at] = {t, w[0] * kG, w[1] * kG, lw[0], lw[1]};
    g_imu_at = (g_imu_at + 1) % kImuN;
}

void camera(double t, float x, float y, float radius)
{
    if (radius < 5.0f || radius > 120.0f)
        return; /* too small to tell its distance */
    const float d = kBallR / radius; /* metres a pixel, there */
    std::lock_guard<std::mutex> lock(g_lock);
    g_cam[g_cam_at] = {t - kCamDelay, (x - 320.0f) * d, kFocal * d};
    g_cam_at = (g_cam_at + 1) % kCamN;
    g_cam_count = std::min(g_cam_count + 1, kCamN);
    consider(t - kCamDelay);
}

float take_correction()
{
    std::lock_guard<std::mutex> lock(g_lock);
    const float c = std::clamp(g_pending, -0.003f, 0.003f); /* (~10 degrees a second at most) */
    g_pending -= c;
    return c;
}

void centred()
{
    /* The camera's "ahead" is kept: centring moves the pointer's middle, not
     * the Move's world, and starting over each time left the fix learning most
     * of a session (Oct 25 logs: on for two stretches in 25 minutes). */
    std::lock_guard<std::mutex> lock(g_lock);
    g_corrected = 0;
}

void reset()
{
    std::lock_guard<std::mutex> lock(g_lock);
    for (Imu &i : g_imu)
        i = {};
    for (Cam &c : g_cam)
        c = {};
    g_imu_at = g_cam_at = g_cam_count = g_ev_at = g_ev_count = 0;
    g_anchored = false;
    g_ref_sum_c = g_ref_sum_s = 0;
    g_ref_n = 0;
    g_pending = g_corrected = g_conf = g_error = 0;
    g_pairs = 0;
    g_mirror_votes = 0;
}

Report report()
{
    std::lock_guard<std::mutex> lock(g_lock);
    Report r;
    r.confidence = g_conf;
    r.pairs = g_pairs;
    r.corrected = g_corrected;
    r.anchored = g_anchored;
    r.error = g_error;
    r.mirrored = g_mirror_votes <= -6;
    return r;
}
} // namespace porpoise::moveyaw
