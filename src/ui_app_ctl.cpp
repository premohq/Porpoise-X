/* Porpoise UI - the controllers: the PlayStation Move drawn with what each of its
 * buttons is, notices as controllers come and go, and each player's controller
 * by the clock.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "porpoise_move.hpp"
#include "porpoise_pad.hpp"
#include "ui_app.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* Each player's light bar colour (porpoise_pad.cpp's kLightColours, in order). */
constexpr std::uint32_t kLight[10] = {0x2F6FFF, 0xFF3A48, 0x10C860, 0xFF4CC0, 0x8C4CFF,
                                      0xFF8A1A, 0xFFC81A, 0x1AC8FF, 0xFFFFFF, 0x50546A};
constexpr float kToastFor = 4.5f;
} // namespace

std::uint32_t App::player_hex(int player) const
{
    if (!settings_)
        return kLight[std::clamp(player, 0, 3)];
    const int lights[4] = {settings_->light_1, settings_->light_2, settings_->light_3, settings_->light_4};
    return kLight[std::clamp(lights[std::clamp(player, 0, 3)], 0, 9)];
}

std::uint32_t App::ball_hex() const
{
    return porpoise::move::ball_rgb(settings_ ? settings_->move_color : 0);
}

void App::draw_move_ball(float cx, float cy, float side, float alpha)
{
    Gfx &g = *g_;
    /* (the ball's ring sits 0.336 of the cell above its middle, 0.075 across inside) */
    const float bx = cx, by = cy - 0.336f * side, r = 0.075f * side;
    const Color ball = rgba(ball_hex(), alpha);
    in_true_color([&] {
        g.blob(bx, by, r * 7, r * 7, with_alpha(ball, 0.5f * alpha));
        g.panel(bx - r, by - r, 2 * r, 2 * r, ball, 1, r);
    });
}

/* ---- what the PS3 Eye sees ------------------------------------------------------------------ */

void App::draw_eye_view(float x, float y, float w, float h, float alpha)
{
    Gfx &g = *g_;
    const porpoise::move::Status ms = porpoise::move::status();
    auto A = [&](Color c) { return with_alpha(c, c.a * alpha); };
    g.panel(x, y, w, h, A(rgba(0x05070D, 0.92f)), 1, 14, A(rgba(0x3D4F9E)), 1.4f);
    /* The picture, when the helper sends one (it does while this is drawn). */
    eye_px_.resize(std::size_t(porpoise::move::kPicW * porpoise::move::kPicH * 4));
    if (porpoise::move::camera_picture(eye_px_.data(), eye_seq_))
    {
        if (!eye_tex_)
            eye_tex_ = g.stream_texture(porpoise::move::kPicW, porpoise::move::kPicH);
        if (eye_tex_)
            g.stream_update(eye_tex_, eye_px_.data());
    }
    const bool live = ms.camera_state == 4 && eye_tex_ && eye_seq_ > 0;
    if (live)
    {
        /* Mirrored: the camera faces the player. */
        const float uv[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        in_true_color([&] { g.image_part(eye_tex_, x + 3, y + 3, w - 6, h - 6, uv, rgba(0xFFFFFF, alpha), 11); });
    }
    if (ms.ball)
    {
        const float bx = x + (1.0f - ms.ball_x) * w, by = y + ms.ball_y * h;
        in_true_color([&] {
            g.panel(bx - 16, by - 16, 32, 32, rgba(0xFFFFFF, 0.0f), 0, 16, rgba(0xFFFFFF, 0.95f * alpha), 2.5f);
            if (!live)
                g.panel(bx - 9, by - 9, 18, 18, rgba(ball_hex(), alpha), 1, 9);
        });
    }
    else if (!live)
        g.text_mid(Font::SemiBold, ts(17), x + w * 0.5f, y + h * 0.5f, A(with_alpha(kLavender, 0.85f)), Align::Center,
                   ms.camera_state == 4 ? tr("Looking for the ball") : tr("The PS3 Eye is off"));
    /* What it says, on a band along the bottom. */
    const float bh = 30;
    g.panel(x + 3, y + h - bh - 3, w - 6, bh, A(rgba(0x05070D, 0.7f)), 1, 10);
    g.panel(x + 16, y + h - bh * 0.5f - 3 - 5, 10, 10, A(ms.ball ? rgba(0x6BE3A8) : rgba(0xFFC85C)), 1, 5);
    g.text_mid(Font::SemiBold, ts(15), x + 34, y + h - bh * 0.5f - 3, A(kWhite), Align::Left,
               ms.camera_state != 4 ? tr("PS3 Eye off") : ms.ball ? tr("Sees the ball") : tr("Can't see the ball"));
}

/* ---- what each of the Move's buttons is ---------------------------------------------------- */

void App::draw_move_pad(float x, float y, float w, float h, float alpha, bool labels)
{
    Gfx &g = *g_;
    const float s = std::min(h / 640.0f, w / 700.0f);
    const float cx = x + w * 0.5f, top = y + (h - 600 * s) * 0.5f;
    auto A = [&](Color c) { return with_alpha(c, alpha); };
    const Color body = rgba(0x262B42), rim = rgba(0x66719C), key = rgba(0x3A4163);

    /* The ball, lit; the neck; the body. */
    in_true_color([&] { g.blob(cx, top + 70 * s, 260 * s, 260 * s, A(rgba(ball_hex(), 0.35f))); });
    g.panel(cx - 34 * s, top + 118 * s, 68 * s, 34 * s, A(rgba(0x1C2032)), 1, 8 * s);
    in_true_color([&] {
        g.panel(cx - 60 * s, top + 10 * s, 120 * s, 120 * s, A(rgba(ball_hex())), 0.85f, 60 * s,
                A(with_alpha(kWhite, 0.8f)), 2.0f * s, 0, 0.25f);
    });
    g.panel(cx - 52 * s, top + 146 * s, 104 * s, 450 * s, A(body), 0.85f, 48 * s, A(rim), 2.0f * s, 0, 0.1f);
    /* T, on the back: its edge shows on the right. */
    g.panel(cx + 46 * s, top + 400 * s, 20 * s, 76 * s, A(rgba(0x31375A)), 1, 9 * s, A(rim), 1.5f * s);

    struct Spot
    {
        float bx, by; /* the button, from the middle line and the top */
        float r;
        int glyph; /* -1: none */
        const char *what;
        bool left; /* its label on the left */
    };
    const std::string none = tr("Not used");
    const Spot spots[] = {
        {-24, 190, 17, int(Glyph::Square), nullptr, true},
        {24, 190, 17, int(Glyph::Triangle), nullptr, false},
        {-24, 238, 17, int(Glyph::Cross), "1 Button", true},
        {24, 238, 17, int(Glyph::Circle), "2 Button", false},
        {0, 300, 30, -1, "A Button", false},
        {-56, 330, 6, -1, "Minus", true},
        {56, 352, 6, -1, "Plus", false},
        {0, 382, 13, -1, "HOME Button", true},
        {56, 440, 0, -1, "B Button", false},
    };
    const char *const kNames[] = {"Square", "Triangle", "Cross", "Circle", "Move", "Select", "Start", "PS", "T"};
    float last_l = -1e9f, last_r = -1e9f;
    for (int i = 0; i < 9; ++i)
    {
        const Spot &p = spots[i];
        const float bx = cx + p.bx * s, by = top + p.by * s;
        if (i == 4)
            g.panel(bx - 36 * s, by - 26 * s, 72 * s, 52 * s, A(key), 0.9f, 24 * s, A(rgba(0x8E98C8)), 1.6f * s);
        else if (i == 5 || i == 6)
            g.panel(bx - 5 * s, by - 18 * s, 10 * s, 36 * s, A(key), 1, 4 * s, A(rgba(0x8E98C8)), 1.2f * s);
        else if (i == 7)
            g.panel(bx - p.r * s, by - p.r * s, 2 * p.r * s, 2 * p.r * s, A(key), 1, p.r * s, A(rgba(0x8E98C8)),
                    1.2f * s);
        else if (p.glyph >= 0)
        {
            g.panel(bx - p.r * s, by - p.r * s, 2 * p.r * s, 2 * p.r * s, A(key), 1, p.r * s, A(rgba(0x8E98C8)),
                    1.2f * s);
            g.glyph(Glyph(p.glyph), bx, by, 20 * s, A(rgba(0xE8ECFF)));
        }
        if (!labels)
            continue; /* (the picture alone: the hub's) */
        /* The line out to its label; labels that would overlap (a smaller
         * picture) step down, the line bending to reach them. */
        float &last = p.left ? last_l : last_r;
        const float ly = std::max(by, last + 48.0f);
        last = ly;
        const float edge = p.left ? cx - 175 * s : cx + 175 * s;
        const float from = bx + (p.left ? -1 : 1) * (p.r + 4) * s;
        const Color line = A(rgba(0x8BD9FF, 0.75f));
        auto hline = [&](float a, float b, float y) { g.panel(std::min(a, b), y - 1, std::fabs(b - a), 2, line, 1, 1); };
        if (ly - by < 0.5f)
            hline(from, edge, by);
        else
        {
            const float knee = p.left ? edge + 30 * s : edge - 30 * s;
            hline(from, knee, by);
            g.panel(knee - 1, by - 1, 2, ly - by + 2, line, 1, 1);
            hline(knee, edge, ly);
        }
        g.panel(edge - 4, ly - 4, 8, 8, A(kCyan), 1, 4);
        const std::string what = i == 0 ? tr("Re-center (hold)") : p.what ? tr(p.what) : none;
        const std::string name = tr(kNames[i]);
        const float tx = p.left ? edge - 16 : edge + 16;
        const Align al = p.left ? Align::Right : Align::Left;
        g.text_mid(Font::Bold, ts(25), tx, ly - 11, A(p.what || i == 0 ? kWhite : kSoft), al, what);
        g.text_mid(Font::Regular, ts(18), tx, ly + 14, A(kLavender), al, name);
    }
}

/* ---- notices: a controller came or went, the Move's battery ------------------------------ */

void App::ctl_toast(int icon, const std::string &title, const std::string &sub)
{
    ctl_toasts_.push_back({icon, title, sub, time_});
    if (ctl_toasts_.size() > 3)
        ctl_toasts_.erase(ctl_toasts_.begin());
}

void App::ctl_watch()
{
    const porpoise::move::Status ms = porpoise::move::status();
    const bool use_move = settings_ && settings_->wii_move;
    /* The first look sets what is there now: no notice for what was there at the start. */
    if (!ctl_seen_)
    {
        ctl_seen_ = true;
        ctl_move_was_ = ms.connected;
        for (int i = 0; i < 4; ++i)
            ctl_pad_was_[i] = porpoise::pad::connected(i);
        return;
    }
    if (ms.connected != ctl_move_was_)
    {
        ctl_move_was_ = ms.connected;
        if (ms.connected)
            ctl_toast(0, tr("PlayStation Move connected"),
                      use_move ? tr("Player 1 \xC2\xB7 Wii Remote") : tr("Turn it on in Wii Controllers"));
        else
        {
            ctl_toast(3, tr("PlayStation Move disconnected"),
                      use_move ? tr("Player 1 is the DualSense again") : std::string());
            ctl_low_told_ = false;
        }
    }
    if (ms.connected && ms.battery >= 0 && ms.battery <= 1 && !ctl_low_told_)
    {
        ctl_low_told_ = true;
        ctl_toast(2, tr("Move battery low"), tr("Player 1 \xC2\xB7 charge it soon"));
    }
    ctl_battery_ = ms.battery;
    for (int i = 1; i < 4; ++i)
    {
        const bool on = porpoise::pad::connected(i);
        if (on != ctl_pad_was_[i])
        {
            ctl_pad_was_[i] = on;
            const std::string who = trf("Player {n}", {{"n", std::to_string(i + 1)}});
            ctl_toast(on ? 1 : 4, on ? tr("DualSense connected") : tr("DualSense disconnected"), who);
        }
    }
    while (!ctl_toasts_.empty() && time_ - ctl_toasts_.front().at > kToastFor + 0.5)
        ctl_toasts_.erase(ctl_toasts_.begin());
}

/* A controller's little picture: 0 the Move, 1 the DualSense, 2 a battery, 3 the
 * Move struck through, 4 the DualSense struck through. */
void App::ctl_icon(int icon, float cx, float cy, float size, Color ink, Color ball)
{
    Gfx &g = *g_;
    const float u = size / 40.0f;
    ctl_art(-1, 0, 0, 0, kClear); /* (loads the pictures) */
    if (gl_ctl_ && icon >= 0 && icon <= 4 && icon != 2)
    {
        /* Every look: the controllers' own pictures (assets/ui/controllers.png). */
        const bool move = icon == 0 || icon == 3;
        ctl_art(move ? 2 : 0, cx, cy, size * (move ? 1.25f : 1.1f), ink);
        if (icon == 0 && ball.a > 0)
            draw_move_ball(cx, cy, size * 1.25f, ball.a); /* lit in its color (Settings: Move Ball Color) */
        if (icon == 3 || icon == 4)
        {
            const bool solid = g.solid();
            g.set_solid(true);
            for (int k = -8; k <= 8; ++k)
                g.panel(cx + k * 1.6f * u - 1.5f * u, cy + k * 2.0f * u - 1.5f * u, 3 * u, 3 * u, ink, 1, 1.5f * u);
            g.set_solid(solid);
        }
        return;
    }
    if (glass())
        g.set_solid(true); /* the Porpoise look: an icon, not glass */
    if (icon == 0 || icon == 3)
    {
        const bool gone = icon == 3;
        g.panel(cx - 8 * u, cy - 19 * u, 16 * u, 16 * u, gone ? with_alpha(ink, 0.0f) : ball, 1, 8 * u, ink, 2.2f * u);
        g.panel(cx - 5 * u, cy - 2 * u, 10 * u, 22 * u, with_alpha(ink, 0.0f), 1, 3 * u, ink, 2.2f * u);
    }
    else if (icon == 1 || icon == 4)
    {
        g.panel(cx - 17 * u, cy - 9 * u, 34 * u, 18 * u, with_alpha(ink, 0.0f), 1, 8 * u, ink, 2.2f * u);
        g.panel(cx - 9 * u, cy - 2 * u, 4 * u, 4 * u, ink, 1, 2 * u);
        g.panel(cx + 5 * u, cy - 2 * u, 4 * u, 4 * u, ink, 1, 2 * u);
    }
    else
    {
        g.panel(cx - 17 * u, cy - 10 * u, 31 * u, 20 * u, with_alpha(ink, 0.0f), 1, 4 * u, ink, 2.2f * u);
        g.panel(cx + 15 * u, cy - 4 * u, 4 * u, 8 * u, ink, 1, 1.5f * u);
        g.panel(cx - 13 * u, cy - 6 * u, 7 * u, 12 * u, ink, 1, 1.5f * u);
    }
    if (icon == 3 || icon == 4)
    {
        /* Struck through: a short slanted bar. */
        for (int k = -8; k <= 8; ++k)
            g.panel(cx + k * 1.6f * u - 1.5f * u, cy + k * 2.0f * u - 1.5f * u, 3 * u, 3 * u, ink, 1, 1.5f * u);
    }
    if (glass())
        g.set_solid(false);
}

void App::draw_ctl_toasts()
{
    if (ctl_toasts_.empty() || !settings_)
        return;
    return; /* 4.5: the island brings them, in every theme (draw_island) */
    Gfx &g = *g_;
    const bool rv = revolution(), sc = starcube();
    const float w = 560, h = 92, x0 = 1920 - 60 - w;
    float y = 120;
    for (const CtlToast &t : ctl_toasts_)
    {
        const float age = float(time_ - t.at);
        if (age > kToastFor + 0.4f)
            continue;
        /* In from the right, out the same way. */
        const float in = std::clamp(age / 0.25f, 0.0f, 1.0f), out = std::clamp((kToastFor + 0.4f - age) / 0.35f, 0.0f, 1.0f);
        const float k = settings_->reduced_motion ? 1.0f : ease_out(std::min(in, out));
        const float a = std::min(in, out);
        const float x = x0 + (1.0f - k) * 120;
        const bool warn = t.icon == 2;
        const bool gone = t.icon == 3 || t.icon == 4;
        Color fill, rim, title, sub, ink, tile;
        float radius = 18;
        if (rv)
        {
            fill = rgba(0xFFFFFF);
            rim = warn || gone ? rgba(0xD3D6DD) : rgba(0x4AB2E6);
            title = rgba(0x3B3F4A);
            sub = rgba(0x6B7080);
            ink = warn ? rgba(0x9A4A12) : gone ? rgba(0x7A7F8C) : rgba(0x1F6496);
            tile = warn ? rgba(0xFBF1E6) : rgba(0xE8F5FC);
            radius = h * 0.5f;
        }
        else if (sc)
        {
            fill = rgba(0x221C35, 0.96f);
            rim = warn || gone ? rgba(0x4A3F70) : rgba(0x8C6CF0);
            title = rgba(0xFFFFFF);
            sub = rgba(0xC3B8F2);
            ink = warn ? rgba(0xF0B467) : gone ? rgba(0xA89FC7) : rgba(0xFFFFFF);
            tile = rgba(0x2E2548);
            radius = 14;
        }
        else
        {
            fill = rgba(0x18224D, 0.96f);
            rim = warn || gone ? rgba(0x3A4C8C) : rgba(0x5A78E0);
            title = kWhite;
            sub = rgba(0xA9B8F0);
            ink = warn ? rgba(0xF0B467) : gone ? rgba(0xA9B8F0) : kWhite;
            tile = warn ? rgba(0x3A2A1A) : rgba(0x24316A);
        }
        g.panel(x, y, w, h, with_alpha(fill, a), 0.95f, radius, with_alpha(rim, a), 2.0f, sc && !gone ? 10 : 0,
                rv ? 0.0f : 0.08f);
        const float ix = x + 18 + 28, iy = y + h * 0.5f;
        g.panel(ix - 28, iy - 28, 56, 56, with_alpha(tile, a), 1, rv ? 28 : sc ? 12 : 16);
        ctl_icon(t.icon, ix, iy, 40, with_alpha(ink, a), with_alpha(rgba(ball_hex()), a));
        if (!gone && !warn)
        {
            const std::uint32_t c = kLight[std::clamp(settings_->light_1, 0, 9)];
            if (!rv)
                g.panel(ix + 14, iy + 14, 18, 18, with_alpha(rgba(c), a), 1, 9, with_alpha(fill, a), 3);
        }
        const float tx = ix + 46;
        if (t.sub.empty())
            g.text_mid(Font::Bold, ts(24), tx, iy, with_alpha(title, a), Align::Left,
                       fit(g, Font::Bold, ts(24), t.title, w - 110));
        else
        {
            g.text_mid(Font::Bold, ts(24), tx, iy - 13, with_alpha(title, a), Align::Left,
                       fit(g, Font::Bold, ts(24), t.title, w - 110));
            g.text_mid(Font::Regular, ts(19), tx, iy + 16, with_alpha(sub, a), Align::Left,
                       fit(g, Font::Regular, ts(19), t.sub, w - 110));
        }
        y += h + 14;
    }
}

/* ---- each player's controller, by the clock ---------------------------------------------- */

float App::draw_ctl_badges(float right_x, float cy, int style)
{
    if (!settings_)
        return 0;
    Gfx &g = *g_;
    const int lights[4] = {settings_->light_1, settings_->light_2, settings_->light_3, settings_->light_4};
    struct Badge
    {
        int player, icon, battery; /* battery -1: not shown */
    };
    Badge badges[5];
    int n = 0;
    const bool move = settings_->wii_move && ctl_move_was_;
    if (move)
        badges[n++] = {0, 0, ctl_battery_};
    for (int i = 0; i < 4; ++i)
        if (porpoise::pad::connected(i))
            badges[n++] = {i, 1, -1};
#ifdef PORPOISE_HOST_PREVIEW
    if (getenv("PV_CTL"))
    {
        /* tools/ui-preview: a Move and two DualSenses. */
        n = 0;
        badges[n++] = {0, 0, 4};
        badges[n++] = {0, 1, -1};
        badges[n++] = {1, 1, -1};
    }
#endif
    if (n == 0)
        return 0;
    const bool icons_only = style == 2;
    const float h = 44, gap = 10;
    /* Widths first, then right to left. */
    float widths[5], total = 0;
    for (int i = 0; i < n; ++i)
    {
        const bool battery = badges[i].battery >= 0 && badges[i].battery <= 6;
        widths[i] = icons_only ? 58 : 16 + 12 + 10 + 36 + (battery ? 10 + 33 : 0) + 14;
        total += widths[i] + (i ? gap : 0);
    }
    float x = right_x - total;
    for (int i = 0; i < n; ++i)
    {
        const Badge &b = badges[i];
        const float w = widths[i];
        Color fill, rim, ink;
        if (style == 2)
        {
            fill = rgba(0xE8F5FC);
            rim = rgba(0x4AB2E6);
            ink = rgba(0x1F6496);
        }
        else if (style == 3)
        {
            fill = rgba(0x221C35, 0.9f);
            rim = rgba(0x4A3F70);
            ink = kWhite;
        }
        else
        {
            fill = rgba(0x1A2350, 0.9f);
            rim = rgba(0x3A4C8C);
            ink = kWhite;
        }
        g.panel(x, cy - h * 0.5f, w, h, fill, 0.95f, h * 0.5f, rim, 2.0f);
        float ix = x + w * 0.5f;
        if (!icons_only)
        {
            const std::uint32_t c = kLight[std::clamp(lights[b.player], 0, 9)];
            g.panel(x + 16, cy - 6, 12, 12, rgba(c), 1, 6);
            ix = x + 16 + 12 + 10 + 18;
        }
        ctl_icon(b.icon, ix, cy, 34, ink, rgba(ball_hex()));
        if (!icons_only && b.battery >= 0)
        {
            /* Five bars; charging shows all five, lit. */
            const int lit = b.battery >= 6 ? 5 : b.battery;
            const float bx = ix + 18 + 10;
            for (int k = 0; k < 5; ++k)
                g.panel(bx + k * 7, cy - 7, 5, 14, k < lit ? (lit <= 1 ? rgba(0xF0B467) : ink) : rgba(0x4A5788), 1, 2);
        }
        x += w + gap;
    }
    return total;
}

} // namespace porpoise::ui
