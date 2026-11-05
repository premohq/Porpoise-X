/* Porpoise - the boot: Sonar. Pings pulse out from the middle of a dark sonar
 * screen; each finds part of the wordmark, and the last one opens a circle
 * into the menus while the wordmark lands where the look draws its own. A hum
 * rises under it (assets/sounds/boot-sonar*.ogg, tools/make-boot-sound.py) into
 * the menu music. Chosen by Ruben from the boot concepts (Oct 24, 2026).
 *
 * On a first start the welcome is underneath: the dolphin lands at its top
 * and the name fades.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "porpoise_debuglog.hpp"
#include "porpoise_pad.hpp"
#include "ui_app.hpp"
#include "ui_app_common.hpp"

namespace porpoise::ui
{
namespace
{
/* The timeline, in seconds (the concepts page's own). */
constexpr float kPings[6] = {0.3f, 1.15f, 1.85f, 2.45f, 2.95f, 3.4f};
constexpr float kLand = 4.3f;  /* the wordmark sets off for its place */
constexpr float kSet = 5.6f;   /* ...and is there; the circle has opened */
constexpr float kMusic = 4.05f; /* the menu music comes in under the peak */
constexpr float kEnd = 5.85f;
constexpr float kSpeed = 1000.0f; /* design pixels a second, a ping's ring */
constexpr float kRingLife = 2.4f;
constexpr float kOx = 960, kOy = 500; /* the logo's middle while it boots */
constexpr float kBootW = 860;        /* the artwork's width while it boots */

/* The wordmark's artwork in its SVG's units (tools/brand/porp-logo-wordmark-color.svg,
 * 2172 x 724: the PNGs cover all of it). Parts: the dolphin, then each letter. */
constexpr float kArtW = 2172, kArtH = 724;
constexpr float kArtX0 = 63, kArtX1 = 2093, kArtCx = 1078, kArtCy = 356;
constexpr float kMarkX0 = 63, kMarkX1 = 657, kMarkY0 = 158, kMarkY1 = 554;
struct Part
{
    float u0, u1;  /* its strip of the image, in the SVG's units */
    float cx, cy;  /* its middle */
};
constexpr Part kParts[9] = {
    {0, 700, 360, 356},       /* the dolphin */
    {700, 930, 834, 358},     /* P */
    {930, 1141, 1034, 390},   /* o */
    {1141, 1270, 1208, 390},  /* r */
    {1270, 1468, 1369, 421},  /* p */
    {1468, 1673, 1568, 390},  /* o */
    {1673, 1738, 1705, 353},  /* i (and its dot) */
    {1738, 1903, 1822, 390},  /* s */
    {1903, 2172, 2002, 390},  /* e */
};

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
float smooth(float k) { return k * k * (3 - 2 * k); }
float e_out(float k) { return 1 - (1 - k) * (1 - k) * (1 - k); }
float e_in(float k) { return k * k * k; }
float e_io(float k) { return k < 0.5f ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3.0f) / 2; }
Color mix(Color a, Color b, float k)
{
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
}

double now_s()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/* A ring: thin quads around it. */
void ring(Gfx &g, float cx, float cy, float r, float width, Color c)
{
    if (r <= 0.5f || c.a <= 0.002f)
        return;
    const int n = std::clamp(int(r / 24.0f), 40, 140);
    const float r0 = std::max(0.0f, r - width * 0.5f), r1 = r + width * 0.5f;
    const float step = 6.2831853f / float(n);
    for (int i = 0; i < n; ++i)
    {
        const float a0 = i * step, a1 = (i + 1) * step; /* (shared corners: no seams) */
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        /* off screen: skipped */
        const float mx = cx + (c0 + c1) * 0.5f * r, my = cy + (s0 + s1) * 0.5f * r;
        if (mx < -40 || mx > 1960 || my < -40 || my > 1120)
            continue;
        const Corner q[4] = {{cx + c0 * r0, cy + s0 * r0, 1}, {cx + c1 * r0, cy + s1 * r0, 1},
                             {cx + c1 * r1, cy + s1 * r1, 1}, {cx + c0 * r1, cy + s0 * r1, 1}};
        g.quad3d(nullptr, q, 1e5f, width, c, 0, false, false); /* (softened across, not at the joins) */
    }
}

/* The circle the menus show through: the backdrop everywhere but inside it. */
Texture *g_hole = nullptr;
constexpr int kHoleSize = 512;
constexpr float kHoleR = 248;
void backdrop(Gfx &g, float hole_r, Color c)
{
    if (!g_hole)
    {
        std::vector<std::uint8_t> px(std::size_t(kHoleSize) * kHoleSize * 4, 255);
        for (int y = 0; y < kHoleSize; ++y)
            for (int x = 0; x < kHoleSize; ++x)
            {
                const float d = std::hypot(x + 0.5f - kHoleSize * 0.5f, y + 0.5f - kHoleSize * 0.5f);
                px[(std::size_t(y) * kHoleSize + x) * 4 + 3] = std::uint8_t(255 * clamp01(d - kHoleR + 0.5f));
            }
        g_hole = g.texture_rgba(px.data(), kHoleSize, kHoleSize);
    }
    if (!g_hole)
    {
        if (hole_r <= 0.5f)
            g.panel(-20, -20, 1960, 1120, c, 1.0f, 0);
        return;
    }
    /* All of it through the same picture (its solid corner for the plain
     * parts), so the parts match exactly. */
    const float solid[4] = {0.5f / kHoleSize, 0.5f / kHoleSize, 3.5f / kHoleSize, 3.5f / kHoleSize};
    if (hole_r <= 0.5f)
    {
        g.image_part(g_hole, -20, -20, 1960, 1120, solid, c);
        return;
    }
    const float size = kHoleSize * hole_r / kHoleR;
    const float x0 = kOx - size * 0.5f, y0 = kOy - size * 0.5f, x1 = x0 + size, y1 = y0 + size;
    if (y0 > -20)
        g.image_part(g_hole, -20, -20, 1960, y0 + 20, solid, c);
    if (y1 < 1100)
        g.image_part(g_hole, -20, y1, 1960, 1100 - y1, solid, c);
    if (x0 > -20)
        g.image_part(g_hole, -20, y0, x0 + 20, size, solid, c);
    if (x1 < 1940)
        g.image_part(g_hole, x1, y0, 1940 - x1, size, solid, c);
    if (size < 6000)
        g.image(g_hole, x0, y0, size, size, c);
}

/* The sonar screen's dots, lit as rings pass. The dim ones are a row of dots
 * drawn down the screen (few quads: the menus under it have theirs to draw). */
Texture *g_dot_row = nullptr;
void dots(Gfx &g, float t, float hole_r, float alpha)
{
    if (!g_dot_row)
    {
        std::vector<std::uint8_t> px(std::size_t(1920) * 8 * 4, 255);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 1920; ++x)
            {
                const float dx = std::fabs(float((x % 40)) + 0.5f - 20.0f), dy = std::fabs(y + 0.5f - 4.0f);
                const float cov = clamp01(2.0f - dx) * clamp01(2.0f - dy);
                px[(std::size_t(y) * 1920 + x) * 4 + 3] = std::uint8_t(255 * cov);
            }
        g_dot_row = g.texture_rgba(px.data(), 1920, 8);
    }
    if (hole_r <= 0.5f && g_dot_row)
        for (float gy = 20; gy < 1080; gy += 40)
            g.image(g_dot_row, 0, gy - 4, 1920, 8, rgba(0x35D2FF, 0.06f * alpha));
    for (float gy = 20; gy < 1080; gy += 40)
        for (float gx = 20; gx < 1920; gx += 40)
        {
            const float dist = std::hypot(gx - kOx, gy - kOy);
            if (dist < hole_r + 6)
                continue;
            float lit = hole_r > 0.5f || !g_dot_row ? 0.06f : 0.0f; /* (the row has the dim part) */
            for (float p : kPings)
            {
                const float age = t - p;
                if (age <= 0)
                    continue;
                const float d = (dist - age * kSpeed) / 40.0f;
                lit += std::exp(-d * d) * 0.9f * std::exp(-age * 0.8f);
            }
            if (lit < 0.01f)
                continue;
            g.panel(gx - 1.5f, gy - 1.5f, 3, 3, rgba(0x35D2FF, std::min(1.0f, lit) * alpha), 1.0f, 0);
        }
}
} // namespace

void forget_boot_textures()
{
    g_hole = g_dot_row = nullptr;
}

/* Before the menus are up (main's first frames): the sonar screen at rest. */
void draw_boot_backdrop(Gfx &g)
{
    backdrop(g, 0, rgba(0x010609));
    dots(g, 0, 0, 1);
}

void App::start_boot(bool legacy_music)
{
    boot_on_ = true;
    boot_started_ = false;
    boot_t0_ = now_s();
    boot_legacy_ = legacy_music;
    boot_sound_ = false;
    boot_events_ = 0;
    boot_music_ = false;
    boot_prev_ = 0xFFFFFFFFu; /* a button held from before isn't a skip */
    brand_seen_ = welcome_mark_seen_ = false;
    preview_boot_t_ = -1;
    /* Its pictures, now, before its clock starts: nothing loads mid-way. */
    Gfx &g = *g_;
    const std::string dir = g.asset_dir() + "/brand/";
    if (!boot_big_)
        boot_big_ = g.texture_file(dir + "boot/wordmark.png", 2048);
    for (int i = 0; i < kTraceSteps; ++i)
        if (!boot_trace_[i])
        {
            char name[32];
            std::snprintf(name, sizeof name, "boot/trace-%02d.png", i + 1);
            boot_trace_[i] = g.texture_file(dir + name, 2048);
        }
    if (!boot_glow_)
        boot_glow_ = g.texture_file(dir + "wordmark-glow.png");
    if (!wordmark_tried_)
    {
        wordmark_tried_ = true;
        wordmark_ = g.texture_file(dir + "wordmark.png");
        wordmark_mark_ = g.texture_file(dir + "wordmark-mark.png");
    }
}

float App::boot_time() const
{
    if (preview_boot_t_ >= 0)
        return float(preview_boot_t_);
    if (!boot_started_)
        return 0;
    return float(now_s() - boot_t0_);
}

bool App::boot_frame(const Input &in, bool ready)
{
    if (!boot_on_)
        return false;
    /* It starts once frames come steadily (the menus' first pictures are in):
     * until then, the sonar screen at rest. Its clock is the real one, so the
     * hum and the picture stay together whatever a frame takes. */
    if (!boot_started_ && (ready || preview_boot_t_ >= 0))
    {
        boot_started_ = true;
        boot_t0_ = now_s() + 0.05; /* (the speakers are about that far behind) */
    }
    if (!boot_started_)
    {
        boot_prev_ = in.held;
        set_intro(0, 0);
        return true;
    }
    float t = boot_time();
    if (!boot_sound_)
    {
        boot_sound_ = true;
        sfx(boot_legacy_ ? Sound::BootSonarLegacy : Sound::BootSonar);
        porpoise::debuglog::app("boot: started (%s hum)", boot_legacy_ ? "the original music's" : "Soundscape's");
    }
    /* A face button or Options pressed (not a stick, the Move or the touch pad,
     * which come and go by themselves): straight to the landing. */
    using namespace porpoise::pad;
    const std::uint32_t skip = BtnCross | BtnCircle | BtnSquare | BtnTriangle | BtnOptions;
    const std::uint32_t fresh = in.held & ~boot_prev_ & skip;
    boot_prev_ = in.held;
    if (fresh && t > 0.4f && t < kLand - 0.35f)
    {
        porpoise::debuglog::app("boot: skipped at %.2f s (buttons %x)", t, fresh);
        boot_t0_ -= (kLand - 0.35f) - t;
        t = kLand - 0.35f;
        boot_events_ |= kBootSkipped;
    }
    if (!boot_music_ && t >= kMusic - ((boot_events_ & kBootSkipped) ? 0.3f : 0.0f))
    {
        boot_music_ = true;
        boot_events_ |= kBootMusic;
    }
    /* The menus come in as the circle opens (all there before it's done). */
    const float ui = smooth(seg(t, kLand - 0.15f, kSet - 0.35f));
    set_intro(ui, 18.0f * (1.0f - ui));
    if (t >= kEnd)
    {
        boot_on_ = false;
        set_intro(1, 0);
        swallow_held(); /* a button held through it doesn't press anything */
        /* its own pictures go */
        Gfx &g = *g_;
        g.free_texture(boot_big_);
        boot_big_ = nullptr;
        for (Texture *&tex : boot_trace_)
        {
            g.free_texture(tex);
            tex = nullptr;
        }
        return false;
    }
    return true;
}

int App::take_boot_events()
{
    const int e = boot_events_;
    boot_events_ = 0;
    return e;
}

float App::brand_alpha() const
{
    if (!boot_on_)
        return 1;
    return boot_time() >= kSet ? 1.0f : 0.0f;
}

void App::draw_boot()
{
    if (!boot_on_)
        return;
    Gfx &g = *g_;
    const float t = boot_time();
    g.set_layer();
    g.set_intro(1, 0);
    const float s = kBootW / (kArtX1 - kArtX0); /* design pixels per SVG unit, while it boots */

    in_true_color([&] {
        /* The screen, then the circle opening into the menus. */
        const float rk = e_in(seg(t, kLand - 0.15f, kSet));
        const float hole = rk * 2300.0f;
        backdrop(g, hole, rgba(0x010609));
        dots(g, t, hole, 1);
        for (float p : kPings)
        {
            const float age = t - p;
            if (age <= 0 || age > kRingLife)
                continue;
            const float r = age * kSpeed;
            if (r < hole)
                continue;
            const float a = 0.6f * std::exp(-age * 1.4f);
            ring(g, kOx, kOy, r, (3 + age * 6) * 3.0f, rgba(0x35D2FF, a * 0.12f)); /* its glow */
            ring(g, kOx, kOy, r, 3 + age * 6, rgba(0x6EE1FF, a));
        }
        if (rk > 0 && rk < 1)
        {
            ring(g, kOx, kOy, hole, 56, rgba(0x35D2FF, 0.16f * (1 - rk)));
            ring(g, kOx, kOy, hole, 22, rgba(0x5CDCFF, 0.32f * (1 - rk)));
            ring(g, kOx, kOy, hole, 10, rgba(0x9AEEFF, 0.9f * (1 - rk)));
        }

        /* The wordmark. */
        if (!boot_big_ || t >= kSet + 0.02f)
            return;
        const Color cyan = rgba(0x35D2FF), white = rgba(0xFFFFFF);
        const float solid = smooth(seg(t, 3.4f, 3.8f));
        const float st_glow = 0.3f + 0.9f * std::exp(-std::pow((std::min(t, kLand) - 3.75f) / 0.35f, 2.0f));
        auto strip = [&](Texture *tex, int part, float left, float top, float sc, Color c) {
            /* one part's strip of a picture of the whole wordmark */
            const Part &p = kParts[part];
            const float uv[4] = {p.u0 / kArtW, 0, p.u1 / kArtW, 1};
            g.image_part(tex, left + p.u0 * sc, top, (p.u1 - p.u0) * sc, kArtH * sc, uv, c);
        };
        if (t < kLand)
        {
            /* Each part, found by its ping as the ring passes it: its outline
             * drawn in, a flash of fill, settling dim until the last ping. */
            const float left = kOx - kArtCx * s, top = kOy - kArtCy * s;
            float sp[9], fill[9];
            bool seen[9];
            for (int i = 0; i < 9; ++i)
            {
                const Part &p = kParts[i];
                const float ping = i == 0 ? kPings[0] : kPings[1 + (i - 1) / 2];
                const float ex = kOx + (p.cx - kArtCx) * s, ey = kOy + (p.cy - kArtCy) * s;
                const float age = t - (ping + std::hypot(ex - kOx, ey - kOy) / kSpeed);
                seen[i] = age >= 0;
                if (!seen[i])
                    continue;
                sp[i] = e_out(clamp01(age / 0.45f));
                fill[i] = clamp01(0.18f + 0.6f * std::exp(-age * 5) + solid);
            }
            /* the glow under the found parts */
            if (boot_glow_)
                for (int i = 0; i < 9; ++i)
                    if (seen[i])
                        strip(boot_glow_, i, left, top, s,
                              rgba(0x35D2FF, std::min(1.0f, std::max(fill[i], sp[i] * 0.7f) * st_glow * 0.8f)));
            for (int i = 0; i < 9; ++i)
            {
                if (!seen[i])
                    continue;
                const Color c = i == 0 ? cyan : white;
                strip(boot_big_, i, left, top, s, Color{c.r, c.g, c.b, fill[i]});
                /* the outline, drawn in: the step it's at, and the next coming in */
                const float f = sp[i] / kTraceFull * kTraceSteps;
                const int k = std::min(kTraceSteps, int(f));
                const float frac = k >= kTraceSteps ? 0.0f : f - float(k);
                if (k >= 1 && boot_trace_[k - 1])
                    strip(boot_trace_[k - 1], i, left, top, s, c);
                if (frac > 0.01f && k < kTraceSteps && boot_trace_[k])
                    strip(boot_trace_[k], i, left, top, s, Color{c.r, c.g, c.b, frac});
            }
            return;
        }

        /* Landing: to where the look draws its own wordmark, in its colors. */
        const float k = e_io(seg(t, kLand, kSet));
        float tx = kOx, ty = kOy, ts = s, text_a = 1;
        Color tm = cyan, tt = white;
        if (brand_seen_)
        {
            /* the look's own: x its left, cy its middle, h its height */
            ts = brand_box_[2] / kArtH;
            tx = brand_box_[0] + kArtCx * ts;
            ty = brand_box_[1] - brand_box_[2] * 0.5f + kArtCy * ts;
            if (brand_style_ == 1)
                tm = white;
            else if (brand_style_ == 2)
                tm = tt = rgba(0x15181F);
        }
        else if (welcome_mark_seen_)
        {
            /* the welcome's dolphin: its middle and width; the name fades */
            ts = welcome_mark_[2] / (kMarkX1 - kMarkX0);
            tx = welcome_mark_[0] + (kArtCx - (kMarkX0 + kMarkX1) * 0.5f) * ts;
            ty = welcome_mark_[1] + (kArtCy - (kMarkY0 + kMarkY1) * 0.5f) * ts;
            if (welcome_mark_style_ == 2)
                tm = rgba(0x15181F);
            text_a = 1 - smooth(seg(t, kLand, kLand + 0.6f));
        }
        else
            text_a = 1 - smooth(seg(t, kLand + 0.2f, kSet)); /* nowhere to land: it fades */
        const float lift = std::sin(k * 3.14159265f) * 30.0f;
        const float px = kOx + (tx - kOx) * k, py = kOy + (ty - kOy) * k - lift;
        const float sc = s * std::pow(ts / s, k);
        const Color mc = mix(cyan, tm, smooth(k)), tc = mix(white, tt, smooth(k));
        const float left = px - kArtCx * sc, top = py - kArtCy * sc;
        const float glow = st_glow * (1 - k) * (brand_seen_ || welcome_mark_seen_ ? 1.0f : text_a);
        if (boot_glow_ && glow > 0.01f)
            for (int i = 0; i < 9; ++i)
                strip(boot_glow_, i, left, top, sc, rgba(0x35D2FF, std::min(1.0f, glow * 0.8f) * (i ? text_a : 1.0f)));
        /* the big picture, then (as it gets small) the one the look draws */
        const float small = wordmark_ ? smooth(seg(k, 0.72f, 0.98f)) : 0.0f;
        for (int i = 0; i < 9; ++i)
        {
            const Color c = i == 0 ? mc : tc;
            const float a = i ? text_a : 1.0f;
            if (a <= 0.003f)
                continue;
            if (small < 1)
                strip(boot_big_, i, left, top, sc, Color{c.r, c.g, c.b, a * (1 - small)});
            if (small > 0)
                strip(wordmark_, i, left, top, sc, Color{c.r, c.g, c.b, a * small});
        }
    });
    brand_seen_ = welcome_mark_seen_ = false; /* (the next frame's draw notes them again) */
}

/* The welcome's dolphin: the wordmark's own, at the top. */
void App::draw_welcome_mark(float cx, float cy, float width, bool ink, float alpha)
{
    Gfx &g = *g_;
    welcome_mark_[0] = cx;
    welcome_mark_[1] = cy;
    welcome_mark_[2] = width;
    welcome_mark_style_ = ink ? 2 : 0;
    welcome_mark_seen_ = true;
    if (!wordmark_tried_)
    {
        wordmark_tried_ = true;
        wordmark_ = g.texture_file(g.asset_dir() + "/brand/wordmark.png");
        wordmark_mark_ = g.texture_file(g.asset_dir() + "/brand/wordmark-mark.png");
    }
    alpha *= brand_alpha();
    if (!wordmark_ || alpha <= 0.003f)
        return;
    const float sc = width / (kMarkX1 - kMarkX0);
    const float left = cx - (kMarkX0 + kMarkX1) * 0.5f * sc, top = cy - (kMarkY0 + kMarkY1) * 0.5f * sc;
    const float uv[4] = {0, 0, 700 / kArtW, 1};
    in_true_color([&] {
        g.image_part(wordmark_, left, top, 700 * sc, kArtH * sc, uv, ink ? rgba(0x15181F, alpha) : rgba(0x35D2FF, alpha));
    });
}
} // namespace porpoise::ui
