/* Porpoise UI - the library's views: cover flow, wheel, disc flow, shelf and
 * box, chosen in Settings > Interface > Library view.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>

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
float ease_inout(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

std::string App::game_meta(const Game &g) const
{
    /* A WAD or an app says what it is (WiiWare, Virtual Console, ...). */
    std::string meta = (g.kind.empty() ? g.platform : tr(g.kind)) + "   \xE2\x80\xA2   " +
                       relative_time(g.last_played, (long long)std::time(nullptr));
    if (g.play_seconds >= 60)
        meta += "   \xE2\x80\xA2   " + play_time_text(g.play_seconds);
    return meta;
}

/* Art far from the selection gives its memory back; it reloads when it
 * comes into view again. */
void App::release_far_art(int keep)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    const int view = starcube() ? (settings_->sc_games == 0 ? 2 : 0) : lib_view_now();
    const int disc_keep = starcube() ? keep : 5;
    const std::vector<int> docked = dock_shown() ? recent_games() : std::vector<int>(); /* Recently Played's */
    /* The Porpoise look keeps the covers it has, shown or not, up to a budget:
     * a switch between GameCube and Wii shows them at once. Over it, the ones
     * furthest away go first - those in no filter shown now, then by distance.
     * (Round 3 counted every cover against 48 and, with more than that loaded,
     * let every far one go: a library of 55 reloaded at each switch.) */
    constexpr int kKeepCovers = 84;
    std::vector<char> drop(games.size(), 0);
    if (glass())
    {
        std::vector<int> spare;
        int loaded = 0;
        for (int i = 0; i < int(games.size()); ++i)
        {
            const Game &game = games[std::size_t(i)];
            if (!game.cover)
                continue;
            ++loaded;
            if ((std::abs(i - selected_) > keep || i >= shown) &&
                std::find(docked.begin(), docked.end(), i) == docked.end())
                spare.push_back(i);
        }
        std::sort(spare.begin(), spare.end(), [&](int a, int b) {
            const bool ha = a >= shown, hb = b >= shown;
            if (ha != hb)
                return ha;
            return std::abs(a - selected_) > std::abs(b - selected_);
        });
        for (int i : spare)
        {
            if (loaded <= kKeepCovers)
                break;
            drop[std::size_t(i)] = 1;
            --loaded;
        }
    }
    for (int i = 0; i < int(games.size()); ++i)
    {
        Game &game = games[std::size_t(i)];
        bool far = (std::abs(i - selected_) > keep || i >= shown) &&
                   std::find(docked.begin(), docked.end(), i) == docked.end();
        if (glass())
            far = drop[std::size_t(i)] != 0;
        if (far && game.cover)
        {
            g.free_texture(game.cover);
            game.cover = nullptr;
            game.cover_tried = false;
        }
        if (far && game.cover_old)
        {
            g.free_texture(game.cover_old);
            game.cover_old = nullptr;
        }
        /* Discs, backs and spines are only kept near the selection in the
         * views that show them (Details keeps the chosen game's). */
        const bool near_disc = (view == 2 && std::abs(i - selected_) <= disc_keep) ||
                               (settings_->recent_style == 3 &&
                                std::find(docked.begin(), docked.end(), i) != docked.end()); /* Recently Played's discs */
        /* The Porpoise look's cases show their spines along the flow and the
         * rows: kept near the selection (freed and asked for again each frame,
         * they flickered). */
        const bool near_spine = (view == 8 && std::abs(i - selected_) <= 22 && i < shown) ||
                                (glass() && !drop[std::size_t(i)]); /* with its cover */
        if (!near_disc && i != selected_ && game.disc)
        {
            g.free_texture(game.disc);
            game.disc = nullptr;
            game.disc_tried = false;
        }
        /* Replaced pictures still waiting on their new ones go with them. */
        if (i != selected_ && !near_disc && game.disc_old)
        {
            g.free_texture(game.disc_old);
            game.disc_old = nullptr;
        }
        if (i != selected_ && !near_spine && game.spine_old)
        {
            g.free_texture(game.spine_old);
            game.spine_old = nullptr;
        }
        if (i != selected_ && game.back_old && screen_ == Screen::Main && !(glass() && std::abs(i - selected_) <= 1))
        {
            g.free_texture(game.back_old);
            game.back_old = nullptr;
        }
        if (i != selected_ && !near_spine && game.spine)
        {
            g.free_texture(game.spine);
            game.spine = nullptr;
            game.spine_tried = false;
        }
        if (i != selected_ && game.back && screen_ == Screen::Main && !(glass() && std::abs(i - selected_) <= 1))
        {
            g.free_texture(game.back);
            game.back = nullptr;
            game.back_tried = false;
        }
    }
}

/* ---- cover flow ------------------------------------------------------------------------- */

void App::draw_cover_flow(double time)
{
    auto &games = lib_->games();
    const int shown = lib_->shown();
    /* Layout of a tile at offset k from the selection (k may be fractional
     * while sliding): x, scale and yaw. */
    auto layout = [](float k, float &x, float &scale, float &yaw) {
        const float a = std::fabs(k), s = k < 0 ? -1.0f : 1.0f;
        if (a <= 1.0f)
        {
            x = kCx + s * a * 372.0f;
            scale = 1.0f - 0.14f * a;
            yaw = s * a * 0.22f;
        }
        else
        {
            x = kCx + s * (372.0f + (a - 1.0f) * 284.0f);
            scale = std::max(0.5f, 0.86f - 0.10f * (a - 1.0f));
            yaw = s * 0.22f;
        }
    };

    /* Back to front: far tiles first, the selection last. */
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
        if (std::fabs(float(i) - scroll_) < 4.2f)
            order.push_back(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return std::fabs(float(a) - scroll_) > std::fabs(float(b) - scroll_); });
    for (int pass = 0; pass < 2; ++pass) /* reflections, then tiles */
        for (int i : order)
        {
            const float k = float(i) - scroll_;
            float x, s, yaw;
            layout(k, x, s, yaw);
            const float a = std::fabs(k);
            const float alpha = std::clamp(1.0f - (a - 3.0f), 0.0f, 1.0f);
            const bool focused = i == selected_ && a < 0.5f;
            const float lift = focused ? (lift_ * 6.0f) : 0.0f;
            /* The focused block turns gently, showing its glass edge. */
            const float sway =
                settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.9f) * 0.06f * (1.0f - std::min(a * 2.0f, 1.0f));
            draw_tile(&games[std::size_t(i)], x, kCy - lift, kTileW * s, kTileH * s, yaw + sway, alpha, focused,
                      pass == 0);
        }
}

/* ---- wheel ------------------------------------------------------------------------------ */

/* The boxes stand around a big wheel lying on the floor, facing out; the
 * wheel turns to bring the chosen one to the front. */
void App::draw_wheel(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    const float step = 0.36f, radius = 1150.0f, reach = 1.75f;

    /* The wheel's rim on the floor, glowing faintly. */
    const float floor_y = kCy + kTileH * 0.5f + 34;
    g.panel(kCx - 860, floor_y - 46, 1720, 92, kClear, 1, 46, rgba(0x5CD3FF, 0.22f), 1.6f, 10);
    g.panel(kCx - 560, floor_y - 30, 1120, 60, kClear, 1, 30, rgba(0x8BD9FF, 0.16f), 1.2f);

    struct Item
    {
        int i;
        float a, z;
    };
    std::vector<Item> items;
    for (int i = 0; i < shown; ++i)
    {
        const float a = (float(i) - scroll_) * step;
        if (std::fabs(a) < reach)
            items.push_back({i, a, radius * (1.0f - std::cos(a))});
    }
    std::sort(items.begin(), items.end(), [](const Item &x, const Item &y) { return x.z > y.z; });
    for (int pass = 0; pass < 2; ++pass)
        for (const Item &it : items)
        {
            const float s = kFocal / (kFocal + it.z);
            const float x = kCx + radius * std::sin(it.a) * s;
            const float y = kCy + 24 - it.z * 0.16f;
            const float alpha = std::clamp((reach - std::fabs(it.a)) / 0.45f, 0.0f, 1.0f);
            const bool focused = it.i == selected_ && std::fabs(it.a) < step * 0.5f;
            const float lift = focused ? lift_ * 8.0f : 0.0f;
            const float sway = settings_->reduced_motion || !focused ? 0.0f : std::sin(float(time) * 0.9f) * 0.05f;
            draw_tile(&games[std::size_t(it.i)], x, y - lift, kTileW * 0.92f * s, kTileH * 0.92f * s,
                      it.a * 0.9f + sway, alpha, focused, pass == 0);
        }
}

/* ---- discs ------------------------------------------------------------------------------ */

/* One disc: its label art (or a silver disc printed with the cover), the
 * hub, a spinning sheen. spin turns it in its plane; yaw turns it in the
 * room. */
void App::draw_disc(Game *game, float cx, float cy, float d, float spin, float yaw, float alpha, bool focused)
{
    Gfx &g = *g_;
    auto corners = [&](float size, Corner out[4]) {
        const float h = size * 0.5f;
        const float pts[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
        const float c = std::cos(spin), s = std::sin(spin);
        for (int i = 0; i < 4; ++i)
            out[i] = project(cx, cy, pts[i][0] * c - pts[i][1] * s, pts[i][0] * s + pts[i][1] * c, yaw);
    };
    if (focused)
        g.blob(cx, cy, d * 1.5f, d * 1.5f, rgba(0x5CD3FF, 0.18f * alpha));
    /* A soft shadow below. */
    g.blob(cx, cy + d * 0.52f, d * 0.9f, d * 0.12f, rgba(0x000000, 0.45f * alpha));

    Corner c[4];
    corners(d, c);
    Texture *art = game ? disc_of(*game) : nullptr;
    if (art)
        g.quad3d(art, c, d, d, with_alpha(kWhite, alpha), d * 0.5f, false, false);
    else
    {
        /* Silver, with the cover printed on the label. */
        g.quad3d(nullptr, c, d, d, rgba(0xC9D2E0, alpha), d * 0.5f, false, false);
        Corner l[4];
        corners(d * 0.80f, l);
        Texture *cover = game ? cover_of(*game) : nullptr;
        if (cover)
        {
            float uv[4];
            cover_uv(cover, 1, 1, uv);
            g.quad3d(cover, l, d * 0.80f, d * 0.80f, with_alpha(kWhite, alpha), d * 0.40f, false, false, uv);
        }
        else
            g.quad3d(nullptr, l, d * 0.80f, d * 0.80f, rgba(0x16328F, alpha), d * 0.40f, false, false);
    }
    /* The clear ring and the hole. */
    Corner hub[4];
    corners(d * 0.30f, hub);
    g.quad3d(nullptr, hub, d * 0.30f, d * 0.30f, rgba(0xD8E2F0, 0.85f * alpha), d * 0.15f, false, false);
    corners(d * 0.11f, hub);
    g.quad3d(nullptr, hub, d * 0.11f, d * 0.11f, rgba(0x05070D, alpha), d * 0.055f, false, false);
    /* The sheen: it doesn't turn with the disc, the light stays put. */
    Corner flat[4];
    {
        const float h = d * 0.5f;
        flat[0] = project(cx, cy, -h, -h, yaw);
        flat[1] = project(cx, cy, h, -h, yaw);
        flat[2] = project(cx, cy, h, h, yaw);
        flat[3] = project(cx, cy, -h, h, yaw);
    }
    gloss_over(g, flat, d, d, d * 0.5f, cx / 1920.0f + spin * 0.05f, alpha);
}

/* The discs in a row; the chosen one slides out of its box and spins. */
void App::draw_disc_flow(double time)
{
    auto &games = lib_->games();
    const int shown = lib_->shown();
    const float spin = settings_->reduced_motion ? 0.0f : float(time) * 0.55f;
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
        if (std::fabs(float(i) - scroll_) < 5.2f)
            order.push_back(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return std::fabs(float(a) - scroll_) > std::fabs(float(b) - scroll_); });
    for (int i : order)
    {
        const float k = float(i) - scroll_;
        const float a = std::fabs(k), side = k < 0 ? -1.0f : 1.0f;
        Game *game = &games[std::size_t(i)];
        if (a < 1.0f)
        {
            /* Near the middle: the box behind, the disc sliding out to its
             * right; as it moves off, the box fades and the disc takes its
             * place in the row. */
            const float out = ease_inout(1.0f - a) * std::min(1.0f, lift_ * 1.4f);
            const float bx = kCx - 120.0f * out + side * a * 420.0f;
            const float box_alpha = ease_inout(1.0f - a * 1.6f);
            if (box_alpha > 0.01f)
                draw_tile(game, bx, kCy, kTileW * 0.92f, kTileH * 0.92f, -0.20f * out + side * a * 0.3f, box_alpha,
                          false, false);
            const float d = 250.0f + 160.0f * (1.0f - a) * std::min(1.0f, lift_ * 1.4f);
            const float dx = kCx + 130.0f * out + side * a * (480.0f - 130.0f * out);
            const float dy = kCy + 10.0f + 30.0f * a;
            draw_disc(game, dx, dy, d, spin * out + (1.0f - out) * (spin * 0.25f + float(i)), side * 0.55f * a, 1.0f,
                      i == selected_ && a < 0.5f);
            continue;
        }
        const float x = kCx + side * (480.0f + (a - 1.0f) * 250.0f);
        const float d = std::max(150.0f, 250.0f - (a - 1.0f) * 22.0f);
        const float alpha = std::clamp(1.0f - (a - 4.0f), 0.0f, 1.0f);
        draw_disc(game, x, kCy + 40, d, spin * 0.25f + float(i), side * 0.55f, alpha, false);
    }
}

/* ---- shelf ------------------------------------------------------------------------------ */

/* Rows of boxes, six to a row: many at once. The chosen one comes forward. */
void App::draw_shelf(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    const int cols = kShelfCols;
    /* With Recently Played below, the boxes a little smaller: two rows still fit. */
    const bool dock = dock_shown();
    const float tw = dock ? 180.0f : 214.0f, th = dock ? 252.0f : 300.0f, gap_x = dock ? 60.0f : 50.0f;
    const float row_h = dock ? 300.0f : 352.0f;
    const float x0 = kCx - (float(cols) * tw + float(cols - 1) * gap_x) * 0.5f + tw * 0.5f;
    const float y0 = dock ? 400.0f : 420.0f;
    const float bottom = dock ? 980.0f : 1040.0f;

    /* The chosen game's name, top right. */
    if (shown > 0)
    {
        Game &sel = games[std::size_t(selected_)];
        g.text(Font::Bold, ts(40), 1830, 128, kWhite, Align::Right, fit(g, Font::Bold, ts(40), sel.title, 1000));
        g.text(Font::SemiBold, ts(26), 1830, 192, kLavender, Align::Right, game_meta(sel));
    }

    /* Shelves under each row. */
    const int first = std::max(0, int(std::floor(row_scroll_)) - 1);
    const int rows = (shown + cols - 1) / cols;
    for (int r = first; r < std::min(rows, first + 4); ++r)
    {
        const float y = y0 + (float(r) - row_scroll_) * row_h;
        if (y < 140 || y > 1150)
            continue;
        const float fade = std::clamp((bottom - y) / 160.0f, 0.0f, 1.0f) * std::clamp((y - 150) / 120.0f, 0.0f, 1.0f);
        g.panel(x0 - tw * 0.5f - 30, y + th * 0.5f + 14, float(cols) * (tw + gap_x) - gap_x + 60, 16,
                rgba(0x0A1236, 0.65f * fade), 0.7f, 8, rgba(0x5CD3FF, 0.35f * fade), 1.2f);
    }
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
    {
        const float y = y0 + (float(i / cols) - row_scroll_) * row_h;
        if (y > 140 && y < 1150)
            order.push_back(i);
    }
    /* The chosen one last, over its neighbours. */
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return a != selected_ && b == selected_; });
    for (int i : order)
    {
        const int r = i / cols, c = i % cols;
        const float x = x0 + float(c) * (tw + gap_x);
        const float y = y0 + (float(r) - row_scroll_) * row_h;
        const bool focused = i == selected_;
        const float fade = std::clamp((bottom - y) / 160.0f, 0.0f, 1.0f) * std::clamp((y - 150) / 120.0f, 0.0f, 1.0f);
        const float grow = focused ? 1.0f + 0.10f * std::min(1.0f, lift_) : 1.0f;
        const float sway = focused && !settings_->reduced_motion ? std::sin(float(time) * 0.9f) * 0.05f : 0.0f;
        draw_tile(&games[std::size_t(i)], x, y - (focused ? 14.0f * lift_ : 0.0f), tw * grow, th * grow, sway, fade,
                  focused, false);
    }
}

/* ---- box -------------------------------------------------------------------------------- */

/* A game case: black plastic, with the printed paper insert under its clear
 * sleeve - the cover on the front, the spine down the left, the back behind -
 * and plastic on the open side, top and bottom. A face is drawn when its
 * corners, as projected, turn clockwise (it faces you), so at any angle each
 * shows cleanly and none bleeds through another. */
void App::draw_box3d(Game &game, float cx, float cy, float w, float h, float depth, float yaw, float alpha,
                     bool load_art, bool spine_only)
{
    Gfx &g = *g_;
    const bool toned = g.toned();
    g.set_tone(false); /* the case is black in every theme */
    /* ...and in the Porpoise look, solid: its colours given as they are. (Its
     * dark fills are made lighter and see-through for the menus' glass - the
     * cases were grey, with the next case's art showing through their sides.) */
    const Look look_was = g.look();
    if (glass())
    {
        Look plain = look_was;
        plain.hue = -1;
        plain.saturation = 1;
        plain.dark = 1;
        plain.fill_alpha = 1;
        plain.mono = false;
        g.set_look(plain);
    }
    const float hw = w * 0.5f, hh = h * 0.5f;
    struct P
    {
        float x, y, z;
    };
    auto corner = [&](P p) { return project(cx, cy, p.x, p.y, yaw, p.z); };
    auto seen = [](const Corner q[4]) {
        float area = 0;
        for (int i = 0; i < 4; ++i)
        {
            const Corner &a = q[i], &b = q[(i + 1) % 4];
            area += a.x * b.y - b.x * a.y;
        }
        return area > 1.0f;
    };
    /* A face's corners (top left, top right, bottom right, bottom left as
     * seen from outside), and a rectangle inset into it by (iu, iv). */
    auto face = [&](const P f[4], float iu, float iv, Corner out[4]) {
        const P &tl = f[0], &tr = f[1], &bl = f[3];
        const P du{tr.x - tl.x, tr.y - tl.y, tr.z - tl.z}, dv{bl.x - tl.x, bl.y - tl.y, bl.z - tl.z};
        const float lu = std::sqrt(du.x * du.x + du.y * du.y + du.z * du.z);
        const float lv = std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
        const float u0 = lu > 0 ? iu / lu : 0, v0 = lv > 0 ? iv / lv : 0;
        const float uv[4][2] = {{u0, v0}, {1 - u0, v0}, {1 - u0, 1 - v0}, {u0, 1 - v0}};
        for (int i = 0; i < 4; ++i)
            out[i] = corner(P{tl.x + du.x * uv[i][0] + dv.x * uv[i][1], tl.y + du.y * uv[i][0] + dv.y * uv[i][1],
                              tl.z + du.z * uv[i][0] + dv.z * uv[i][1]});
    };
    /* The Porpoise look: a Wii game's case in white plastic, and the ones off
     * to the side a little darker (box_bright_). */
    const bool white = box_white_wii_ && game.platform == "Wii" && game.kind.empty();
    const float lit = std::clamp(box_bright_, 0.0f, 1.0f);
    /* The Porpoise look: the printed sleeve runs round the case in one piece -
     * back, spine, front - with no plastic showing at the folds, only a hairline
     * at the outer edges. */
    const bool sleeve = glass();
    auto face2 = [&](const P f[4], float iu0, float iu1, float iv, Corner out[4]) {
        const P &tl = f[0], &tr = f[1], &bl = f[3];
        const P du{tr.x - tl.x, tr.y - tl.y, tr.z - tl.z}, dv{bl.x - tl.x, bl.y - tl.y, bl.z - tl.z};
        const float lu = std::sqrt(du.x * du.x + du.y * du.y + du.z * du.z);
        const float lv = std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
        const float u0 = lu > 0 ? iu0 / lu : 0, u1 = lu > 0 ? 1 - iu1 / lu : 1, v0 = lv > 0 ? iv / lv : 0;
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, 1 - v0}, {u0, 1 - v0}};
        for (int i = 0; i < 4; ++i)
            out[i] = corner(P{tl.x + du.x * uv[i][0] + dv.x * uv[i][1], tl.y + du.y * uv[i][0] + dv.y * uv[i][1],
                              tl.z + du.z * uv[i][0] + dv.z * uv[i][1]});
    };
    const float edge = 2.5f; /* the plastic's hairline round the sleeve */
    /* 4.5: the plastic showing round the insert, as a real case has it (the
     * insert runs from the open side's frame round the spine): a band at the
     * top and the bottom, a wider one down the open side. */
    const float et = sleeve ? std::max(edge, h * 0.012f) : edge, eb = sleeve ? std::max(edge, h * 0.0145f) : edge,
                er = sleeve ? std::max(3.0f, w * 0.026f) : edge;
    auto face3 = [&](const P f[4], float iu0, float iu1, float iv0, float iv1, Corner out[4]) {
        const P &tl = f[0], &tr = f[1], &bl = f[3];
        const P du{tr.x - tl.x, tr.y - tl.y, tr.z - tl.z}, dv{bl.x - tl.x, bl.y - tl.y, bl.z - tl.z};
        const float lu = std::sqrt(du.x * du.x + du.y * du.y + du.z * du.z);
        const float lv = std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
        const float u0 = lu > 0 ? iu0 / lu : 0, u1 = lu > 0 ? 1 - iu1 / lu : 1;
        const float v0 = lv > 0 ? iv0 / lv : 0, v1 = lv > 0 ? 1 - iv1 / lv : 1;
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        for (int i = 0; i < 4; ++i)
            out[i] = corner(P{tl.x + du.x * uv[i][0] + dv.x * uv[i][1], tl.y + du.y * uv[i][0] + dv.y * uv[i][1],
                              tl.z + du.z * uv[i][0] + dv.z * uv[i][1]});
    };
    /* Where the plastic lies over the insert's edge: a hairline of shade along it. */
    auto tuck = [&](const P f[4], float iu0, float iu1, bool left_fold, float fa) {
        if (!sleeve)
            return;
        const Color sh = white ? Color{0.0f, 0.0f, 0.0f, 0.16f * fa} : Color{0.0f, 0.0f, 0.0f, 0.45f * fa};
        Corner l[4];
        const P &tl = f[0], &tr = f[1];
        const float lu = std::sqrt((tr.x - tl.x) * (tr.x - tl.x) + (tr.z - tl.z) * (tr.z - tl.z));
        face3(f, iu0, iu1, et, h - et - 1.4f, l); /* along the top */
        g.quad3d(nullptr, l, lu - iu0 - iu1, 1.4f, sh, 0, false, false);
        face3(f, iu0, iu1, h - eb - 1.4f, eb, l); /* along the bottom */
        g.quad3d(nullptr, l, lu - iu0 - iu1, 1.4f, sh, 0, false, false);
        if (left_fold)
            face3(f, lu - iu1 - 1.4f, iu1, et, eb, l); /* down the open side's frame */
        else
            face3(f, iu0, lu - iu0 - 1.4f, et, eb, l);
        g.quad3d(nullptr, l, 1.4f, h - et - eb, sh, 0, false, false);
    };
    Color black = white ? Color{0.90f * lit, 0.91f * lit, 0.93f * lit, alpha}
                        : Color{0.035f * lit, 0.038f * lit, 0.045f * lit, alpha};
    if (white && sleeve)
    {
        /* A Wii case coloured to match its label (New Super Mario Bros. Wii's
         * red): the plastic takes the spine's own colour, down its edges. */
        const Texture *sp = game.spine ? game.spine : game.spine_old;
        if (sp)
        {
            const float r = sp->edge[0], gg = sp->edge[1], b = sp->edge[2];
            const float mx = std::max({r, gg, b}), mn = std::min({r, gg, b});
            if (mx > 0.25f && (mx - mn) / mx > 0.45f)
                black = Color{r * 0.92f * lit, gg * 0.92f * lit, b * 0.92f * lit, alpha};
        }
    }
    const Color art_tint = Color{lit, lit, lit, alpha};
    /* spine_only: a case standing in a row of them, where its neighbours hide
     * its sides: the spine alone (a side face, seen past the next case's
     * spine, showed as a cover sliver across it). */
    Texture *const cover_art = spine_only ? nullptr : load_art ? cover_of(game) : game.cover ? game.cover : game.cover_old; /* far: no new loads */
    const Color edge_light = white ? Color{0.78f * lit, 0.80f * lit, 0.83f * lit, alpha}
                                   : Color{0.16f * lit, 0.17f * lit, 0.19f * lit, alpha};

    /* Its shadow on the floor. */
    g.blob(cx, cy + hh + 26, w * 1.3f, 60,
           rgba(0x000000, (glass() && settings_->ui_mode == 2 ? 0.26f : 0.5f) * alpha)); /* (softer on Light) */

    const P front[4] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    const P back[4] = {{hw, -hh, depth}, {-hw, -hh, depth}, {-hw, hh, depth}, {hw, hh, depth}};
    const P spine[4] = {{-hw, -hh, depth}, {-hw, -hh, 0}, {-hw, hh, 0}, {-hw, hh, depth}};
    const P open_side[4] = {{hw, -hh, 0}, {hw, -hh, depth}, {hw, hh, depth}, {hw, hh, 0}};
    const P top[4] = {{-hw, -hh, depth}, {hw, -hh, depth}, {hw, -hh, 0}, {-hw, -hh, 0}};
    const P bottom[4] = {{-hw, hh, 0}, {hw, hh, 0}, {hw, hh, depth}, {-hw, hh, depth}};
    const float paper = 7; /* the plastic showing round the insert */

    Corner q[4];
    /* The Porpoise look: the plastic's faces each a hair larger, so where two
     * meet no light shows through their edges, and lit as plastic is - the
     * top catching the light, the bottom in shade. */
    auto shell = [&](const P f[4], Corner out[4]) {
        if (sleeve)
            face2(f, -0.8f, -0.8f, -0.8f, out);
        else
            for (int i = 0; i < 4; ++i)
                out[i] = corner(f[i]);
    };
    auto shade = [&](float k) {
        return Color{std::clamp(black.r + k, 0.0f, 1.0f), std::clamp(black.g + k, 0.0f, 1.0f),
                     std::clamp(black.b + k, 0.0f, 1.0f), alpha};
    };
    for (const P *f : {top, bottom})
    {
        if (spine_only)
            break;
        for (int i = 0; i < 4; ++i)
            q[i] = corner(f[i]);
        if (seen(q))
        {
            Corner sq[4];
            shell(f, sq);
            g.quad3d(nullptr, sq, w, depth, sleeve ? shade(f == top ? 0.07f : -0.03f) : black, 2, false, false);
        }
    }
    for (int i = 0; i < 4; ++i)
        q[i] = corner(open_side[i]);
    if (!spine_only && seen(q))
    {
        Corner sq[4];
        shell(open_side, sq);
        if (sleeve)
        {
            /* The open side: smooth plastic, a soft sheen down it. */
            g.quad3d(nullptr, sq, depth, h, shade(0.03f), 3, false, false);
            gloss_over(g, sq, depth, h, 3, cx / 1920.0f + yaw * 0.15f + 0.3f, alpha * 0.5f);
        }
        else
        {
            /* The open side: plastic, with the ridge of its clasp catching the light. */
            g.quad3d(nullptr, q, depth, h, black, 3, false, false);
            Corner r[4];
            face(open_side, depth * 0.40f, h * 0.30f, r);
            g.quad3d(nullptr, r, depth * 0.2f, h * 0.4f, edge_light, 1, false, false);
        }
    }
    for (int i = 0; i < 4; ++i)
        q[i] = corner(spine[i]);
    if (seen(q))
    {
        Corner sq[4];
        shell(spine, sq);
        g.quad3d(nullptr, sq, depth, h, black, 2, false, false);
        Corner in[4];
        if (sleeve)
            face3(spine, 0, 0, et, eb, in);
        else
            face(spine, 3, paper, in);
        if (Texture *art = spine_of(game))
        {
            /* A hair in from each fold: a scan's fold is often a line of the
             * neighbouring side's colour. Top and bottom, what the plastic
             * covers of it on the front covers here too. */
            const float cut_v0 = sleeve ? et / h : 0.0f, cut_v1 = sleeve ? 1.0f - eb / h : 1.0f;
            const float spine_uv[4] = {0.035f, cut_v0, 0.965f, cut_v1};
            g.quad3d(art, in, sleeve ? depth : depth - 6, sleeve ? h - et - eb : h - paper * 2, art_tint,
                     sleeve ? 0.0f : 1.0f, false, false, sleeve ? spine_uv : nullptr);
        }
        else
        {
            /* No spine art: a printed spine of its own - a Wii game's white,
             * a GameCube game's dark - with the title down it, top to bottom.
             * (A strip of the cover squeezed in looked like noise.) */
            const bool wii = game.platform == "Wii";
            g.quad3d(nullptr, in, depth - 6, h - paper * 2,
                     wii ? Color{0.90f, 0.91f, 0.93f, alpha} : Color{0.10f, 0.11f, 0.13f, alpha}, 1, false, false);
            const float size = std::clamp(depth * 0.52f, 9.0f, 26.0f), margin = h * 0.06f;
            const std::string title =
                fit(g, Font::SemiBold, size, game.title.empty() ? game.file : game.title, h - paper * 2 - margin * 2);
            auto down_spine = [&](float x, float y) {
                return corner(P{-hw - 0.5f, -hh + paper + margin + x, depth * 0.5f + y});
            };
            g.text_mapped(Font::SemiBold, size, title,
                          wii ? Color{0.16f, 0.17f, 0.20f, alpha} : Color{0.86f, 0.88f, 0.92f, alpha}, Align::Left,
                          down_spine);
        }
        gloss_over(g, q, depth, h, 2, cx / 1920.0f + yaw * 0.1f, alpha * 0.6f);
    }
    for (int i = 0; i < 4; ++i)
        q[i] = corner(back[i]);
    if (!spine_only && seen(q))
    {
        {
            Corner sq[4];
            shell(back, sq);
            g.quad3d(nullptr, sq, w, h, black, 6, false, false);
        }
        Corner in[4];
        const float bw = sleeve ? w - er : w - paper * 2, bh = sleeve ? h - et - eb : h - paper * 2;
        if (sleeve)
            face3(back, er, 0, et, eb, in); /* its right edge, seen from behind, is the spine's fold */
        else
            face(back, paper, paper, in);
        if (Texture *art = load_art ? back_of(game) : game.back ? game.back : game.back_old)
        {
            float uv[4];
            cover_uv(art, bw, bh, uv);
            g.quad3d(art, in, bw, bh, art_tint, sleeve ? 1.5f : 3.0f, false, false, uv);
            tuck(back, er, 0, false, alpha);
        }
        else if (Texture *mark = logo_ ? logo_ : g.brand_mask())
        {
            const float lw = w * 0.42f, lh = lw * float(mark->height) / float(std::max(1, mark->width));
            Corner l[4] = {corner(P{lw * 0.5f, -lh * 0.5f, depth}), corner(P{-lw * 0.5f, -lh * 0.5f, depth}),
                           corner(P{-lw * 0.5f, lh * 0.5f, depth}), corner(P{lw * 0.5f, lh * 0.5f, depth})};
            g.quad3d(mark, l, lw, lh, with_alpha(logo_ ? kWhite : kCyan, 0.85f * alpha), 0, false, false);
        }
        gloss_over(g, q, w, h, 6, cx / 1920.0f + yaw * 0.1f, alpha);
    }
    for (int i = 0; i < 4; ++i)
        q[i] = corner(front[i]);
    if (!spine_only && seen(q))
    {
        {
            Corner sq[4];
            shell(front, sq);
            g.quad3d(nullptr, sq, w, h, black, 6, false, false);
        }
        Corner in[4];
        const float fw = sleeve ? w - er : w - paper * 2, fh = sleeve ? h - et - eb : h - paper * 2;
        if (sleeve)
            face3(front, 0, er, et, eb, in); /* its left edge is the spine's fold */
        else
            face(front, paper, paper, in);
        if (Texture *cover = cover_art)
        {
            float uv[4];
            cover_uv(cover, fw, fh, uv);
            g.quad3d(cover, in, fw, fh, art_tint, sleeve ? 1.5f : 3.0f, false, false, uv);
            tuck(front, 0, er, true, alpha);
        }
        else
        {
            g.quad3d(nullptr, in, w - paper * 2, h - paper * 2, rgba(0x16328F, alpha), 3, false, false);
            draw_mark(cx, cy - h * 0.12f, w * 0.42f, with_alpha(rgba(0x6FD8FF), 0.9f * alpha));
        }
        gloss_over(g, q, w, h, 6, cx / 1920.0f + yaw * 0.1f, alpha);
        if (sleeve)
        {
            /* The clear sleeve over the cover: a second, fainter sheen further
             * along, and its top edge catching the light. */
            gloss_over(g, q, w, h, 6, cx / 1920.0f + yaw * 0.1f + 0.45f, alpha * 0.35f);
            Corner lip[4] = {corner(P{-hw + 1, -hh + et, -0.3f}), corner(P{hw - er, -hh + et, -0.3f}),
                             corner(P{hw - er, -hh + et + 1.6f, -0.3f}), corner(P{-hw + 1, -hh + et + 1.6f, -0.3f})};
            g.quad3d(nullptr, lip, w - er - 1, 1.6f, Color{1, 1, 1, 0.16f * alpha * lit}, 0, false, false);
            /* The case's own top edge catching the light, a hairline along the plastic. */
            Corner rim[4] = {corner(P{-hw + 2, -hh + 0.6f, -0.35f}), corner(P{hw - 3, -hh + 0.6f, -0.35f}),
                             corner(P{hw - 3, -hh + 1.6f, -0.35f}), corner(P{-hw + 2, -hh + 1.6f, -0.35f})};
            g.quad3d(nullptr, rim, w - 5, 1.0f, Color{1, 1, 1, (white ? 0.5f : 0.22f) * alpha * lit}, 0, false, false);
        }
        if (box_reflect_ > 0)
        {
            /* Its reflection on the floor: the bottom of the front, upside
             * down, faint and gone within a third of its height. */
            const float gap = 12, part = 0.34f;
            auto below = [&](float x, float y) { return corner(P{x, hh + gap + (hh - y), 0}); };
            const float top_y = hh - h * part;
            Corner r[4] = {below(-hw, hh), below(hw, hh), below(hw, top_y), below(-hw, top_y)};
            const float ra = box_reflect_ * alpha;
            g.quad3d(nullptr, r, w, h * part, Color{black.r, black.g, black.b, ra}, 0, true, false);
            if (Texture *cover = cover_art)
            {
                const float l0 = sleeve ? -hw : -hw + paper, r0 = sleeve ? hw - er : hw - paper;
                const float b0 = sleeve ? hh - eb : hh - paper;
                Corner ri[4] = {below(l0, b0), below(r0, b0), below(r0, top_y), below(l0, top_y)};
                float uv[4];
                cover_uv(cover, r0 - l0, b0 - (-hh + (sleeve ? et : paper)), uv);
                /* Only the bottom part of the art: v from (1 - share) to 1 of its rectangle. */
                const float share = (b0 - top_y) / (b0 - (-hh + (sleeve ? et : paper)));
                const float uvp[4] = {uv[0], uv[3] - (uv[3] - uv[1]) * share, uv[2], uv[3]};
                g.quad3d(cover, ri, r0 - l0, b0 - top_y, Color{lit, lit, lit, ra}, 0, true, true, uvp);
            }
        }
    }
    g.set_tone(toned);
    g.set_look(look_was);
}

/* One game at a time: its box, big, on the left - the right stick turns it
 * round - and what's known about it on the right. */
void App::draw_box_view(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    if (shown == 0)
        return;
    const float k = scroll_ - float(selected_); /* -1..1 while sliding */
    const float bw = 400, bh = bw * 190.0f / 135.0f, depth = 58;
    const float sway = settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.6f) * 0.10f;
    const float yaw = view_yaw_ + (std::fabs(view_yaw_) < 0.05f ? sway : 0.0f) + 0.48f;
    /* Neighbours, small and faint, to show there are more. */
    for (int n : {selected_ - 1, selected_ + 1})
        if (n >= 0 && n < shown)
        {
            const float side = n < selected_ ? -1.0f : 1.0f;
            const float x = 740 + side * 560 - k * 560;
            if (side > 0 && x > 1000)
                continue; /* the details panel is there */
            draw_tile(&games[std::size_t(n)], x, kCy + 40, kTileW * 0.5f, kTileH * 0.5f, side * 0.5f, 0.35f, false,
                      false);
        }
    Game &sel = games[std::size_t(selected_)];
    draw_box3d(sel, 740 - k * 560, kCy + 20, bw, bh, depth, yaw, std::clamp(1.0f - std::fabs(k), 0.0f, 1.0f));

    /* What's known about it. */
    const float px = 1150, py = 250, pw = 690, ph = 640;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    float y = py + 40;
    for (const std::string &line : wrap(g, Font::Bold, ts(40), sel.title, pw - 80, 2))
    {
        g.text(Font::Bold, ts(40), px + 40, y, kWhite, Align::Left, line);
        y += ts(40) * 1.18f;
    }
    if (sel.favourite)
        g.glyph(Glyph::Star, px + pw - 40, py + 60, 34, rgba(0xFFD45C));
    y += 8;
    g.text(Font::SemiBold, ts(25), px + 40, y, kLavender, Align::Left, game_meta(sel));
    y += 56;
    std::vector<std::pair<std::string, std::string>> facts;
    if (!sel.developer.empty())
        facts.push_back({tr("Developer"), sel.developer});
    if (!sel.publisher.empty() && sel.publisher != sel.developer)
        facts.push_back({tr("Publisher"), sel.publisher});
    if (!sel.released.empty())
        facts.push_back({tr("Released"), sel.released});
    if (sel.players > 0)
        facts.push_back({tr("Players"), std::to_string(sel.players)});
    for (const auto &f : facts)
    {
        g.text(Font::SemiBold, ts(24), px + 40, y, kSoft, Align::Left, f.first);
        g.text(Font::Regular, ts(24), px + 250, y, kWhite, Align::Left, fit(g, Font::Regular, ts(24), f.second, pw - 290));
        y += 38;
    }
    if (!sel.synopsis.empty())
    {
        y += 14;
        const int room = std::max(1, int((py + ph - 40 - y) / (ts(24) * 1.35f)));
        for (const std::string &line : wrap(g, Font::Regular, ts(24), sel.synopsis, pw - 80, std::size_t(room)))
        {
            g.text(Font::Regular, ts(24), px + 40, y, kSoft, Align::Left, line);
            y += ts(24) * 1.35f;
        }
    }
    else if (facts.empty())
        g.text(Font::Regular, ts(24), px + 40, y, kSoft, Align::Left,
               fit(g, Font::Regular, ts(24), tr("Turn the box around with the right stick."), pw - 80));
}

/* ---- list ------------------------------------------------------------------------------- */

/* Your games by name, down the right; the chosen one's box on the left. */
void App::draw_list_view(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    if (shown == 0)
        return;
    Game &sel = games[std::size_t(selected_)];
    const float sway = settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.6f) * 0.08f;
    const bool dock = dock_shown(); /* Recently Played below */
    draw_tile(&sel, 470, kCy + (dock ? 10.0f : 30.0f), kTileW * 1.05f, kTileH * 1.05f, 0.18f + sway, 1.0f, true, false);
    g.text(Font::Bold, ts(40), 470, dock ? 790.0f : 840.0f, kWhite, Align::Center, fit(g, Font::Bold, ts(40), sel.title, 640));
    g.text(Font::SemiBold, ts(24), 470, dock ? 846.0f : 900.0f, kLavender, Align::Center, game_meta(sel));

    const float lx = 860, ly = 250, lw = 980, row_h = 76;
    const int visible = dock ? 8 : 9;
    /* The list slides so the chosen row stays near the middle. */
    const float top = std::clamp(scroll_ - 3.5f, 0.0f, std::max(0.0f, float(shown - visible)));
    g.panel(lx, ly - 14, lw, row_h * visible + 28, rgba(0x0F1F63, 0.55f), 0.75f, kR, rgba(0x4C6FD8, 0.8f), 1.6f, 0, 0.1f);
    for (int i = std::max(0, int(top) - 1); i < std::min(shown, int(top) + visible + 1); ++i)
    {
        const float y = ly + (float(i) - top) * row_h;
        if (y < ly - row_h * 0.5f || y > ly + row_h * (visible - 0.5f))
            continue;
        const float edge = std::clamp(std::min(y - (ly - row_h * 0.5f), ly + row_h * (visible - 0.5f) - y) / row_h, 0.0f, 1.0f);
        Game &game = games[std::size_t(i)];
        const bool on = i == selected_;
        if (on)
            g.panel(lx + 12, y, lw - 24, row_h - 8, rgba(0x1F63F0, 0.9f), 0.62f, kR, rgba(0x7FD9FF), 1.6f, 6, 0.3f);
        if (Texture *cover = cover_of(game))
            g.image(cover, lx + 28, y + 4, 44, 60, with_alpha(kWhite, edge), 4);
        else
            g.panel(lx + 28, y + 4, 44, 60, with_alpha(kTileFill, 0.55f * edge), 0.6f, 4, with_alpha(kEdge, 0.6f * edge),
                    1.2f);
        g.text_mid(on ? Font::Bold : Font::SemiBold, ts(27), lx + 92, y + (row_h - 8) * 0.5f, with_alpha(kWhite, edge),
                   Align::Left, fit(g, Font::Bold, ts(27), game.title, lw - 380));
        if (game.favourite)
            g.glyph(Glyph::Star, lx + lw - 250, y + (row_h - 8) * 0.5f, 26, rgba(0xFFD45C, edge));
        g.text_mid(Font::Regular, ts(22), lx + lw - 36, y + (row_h - 8) * 0.5f, with_alpha(on ? kWhite : kLavender, edge),
                   Align::Right, game.kind.empty() ? game.platform : tr(game.kind));
    }
}

/* ---- stack ------------------------------------------------------------------------------ */

/* A deck: the chosen box in front, the next ones behind it, rising away; the
 * ones gone by drop forward out of sight. */
void App::draw_stack(double time)
{
    auto &games = lib_->games();
    const int shown = lib_->shown();
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
    {
        const float k = float(i) - scroll_;
        if (k > -1.2f && k < 6.0f)
            order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return a > b; }); /* far first */
    for (int i : order)
    {
        const float k = float(i) - scroll_;
        float x = kCx, y = kCy - 10, s = 1.0f, yaw = -0.12f, alpha = 1.0f;
        if (k >= 0)
        {
            x += k * 92.0f;
            y -= k * 26.0f;
            s = 1.0f - k * 0.075f;
            yaw = -0.12f - k * 0.04f;
            alpha = std::clamp(1.0f - (k - 3.5f) * 0.5f, 0.0f, 1.0f) * (1.0f - k * 0.08f);
        }
        else
        {
            /* Going: it tips forward and drops. */
            const float a = -k;
            y += a * 420.0f;
            x -= a * 120.0f;
            yaw = -0.12f - a * 0.6f;
            alpha = std::clamp(1.0f - a * 1.2f, 0.0f, 1.0f);
        }
        const bool focused = i == selected_ && std::fabs(k) < 0.5f;
        const float sway = focused && !settings_->reduced_motion ? std::sin(float(time) * 0.9f) * 0.05f : 0.0f;
        draw_tile(&games[std::size_t(i)], x, y, kTileW * s, kTileH * s, yaw + sway, alpha, focused, false);
    }
}

/* ---- helix ------------------------------------------------------------------------------ */

/* The boxes climb round a column, turning to bring the chosen one round. */
void App::draw_helix(double time)
{
    Gfx &g = *g_;
    (void)time;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    /* Each game a little further round: never past the side of the column,
     * where a case would show its back (it showed as noise). */
    const float step = 0.42f, radius = 760.0f, rise = 118.0f;
    /* The column. */
    g.panel(kCx - 5, 200, 10, 560, rgba(0x5CD3FF, 0.14f), 1, 6, rgba(0x8BD9FF, 0.25f), 1.0f, 8);
    struct Item
    {
        int i;
        float a, z;
    };
    std::vector<Item> items;
    for (int i = 0; i < shown; ++i)
    {
        const float k = float(i) - scroll_;
        if (std::fabs(k) < 4.0f)
            items.push_back({i, k * step, radius * (1.0f - std::cos(k * step))});
    }
    std::sort(items.begin(), items.end(), [](const Item &x, const Item &y) { return x.z > y.z; });
    for (const Item &it : items)
    {
        const float k = float(it.i) - scroll_;
        const float s = kFocal / (kFocal + it.z);
        const float x = kCx + radius * std::sin(it.a) * s;
        const float y = kCy - 10 - k * rise * s;
        /* Fade before the top bar or the title below. */
        const float room = std::clamp(std::min(y - 250.0f, 700.0f - y) / 90.0f + 1.0f, 0.0f, 1.0f);
        const float round = std::clamp((1.3f - std::fabs(it.a)) / 0.3f, 0.0f, 1.0f); /* gone before side-on */
        const float alpha = room * round * std::clamp((4.0f - std::fabs(k)) / 1.2f, 0.0f, 1.0f);
        if (alpha <= 0.01f)
            continue;
        const bool focused = it.i == selected_ && std::fabs(k) < 0.5f;
        draw_tile(&games[std::size_t(it.i)], x, y, kTileW * 0.66f * s, kTileH * 0.66f * s,
                  std::clamp(it.a, -1.2f, 1.2f), alpha, focused, false);
    }
}

/* ---- spines ----------------------------------------------------------------------------- */

/* A collection on a shelf: every case side by side, spine out, the way they
 * stand at home. The chosen one is pulled out and turned to show its cover. */
void App::draw_spines(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    const float h = 430, w = h * 135.0f / 190.0f, depth = 40;
    const float pitch = 46, gap = 250; /* spine to spine, and the room either side of the one pulled out */
    const float cy = kCy - 20;
    const float spine_yaw = kPi * 0.5f;
    /* The shelf under them. */
    g.panel(-40, cy + h * 0.5f + 18, 2000, 20, rgba(0x0A1236, 0.7f), 0.7f, 6, rgba(0x5CD3FF, 0.30f), 1.2f);
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
        if (std::fabs(float(i) - scroll_) < 22.0f)
            order.push_back(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return std::fabs(float(a) - scroll_) > std::fabs(float(b) - scroll_); });
    const float sway = settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.7f) * 0.05f;
    for (int i : order)
    {
        const float k = float(i) - scroll_, a = std::fabs(k), side = k < 0 ? -1.0f : 1.0f;
        float x, yaw = spine_yaw, s = 1.0f, y = cy;
        if (a < 1.0f)
        {
            const float out = ease_inout(1.0f - a) * std::min(1.0f, 0.4f + lift_);
            x = kCx + k * gap;
            yaw = spine_yaw + (0.30f + sway - spine_yaw) * out;
            s = 1.0f + 0.10f * out;
            y = cy - 12.0f * out;
        }
        else
            x = kCx + side * (gap + (a - 1.0f) * pitch);
        if (x < -80 || x > 2000)
            continue;
        const float alpha = std::clamp((21.0f - a) / 3.0f, 0.0f, 1.0f);
        draw_box3d(games[std::size_t(i)], x, y, w * s, h * s, depth * s, yaw, alpha, a <= 8.0f, a >= 1.0f);
    }
}

/* ---- spotlight -------------------------------------------------------------------------- */

/* The chosen game's cover over the whole room, large and dim: drawn before
 * the top bar, fading in as the selection settles. */
void App::draw_spotlight_backdrop()
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    if (lib_->shown() == 0 || selected_ < 0 || selected_ >= lib_->shown())
        return;
    Texture *cover = cover_of(games[std::size_t(selected_)]);
    if (!cover)
        return;
    const float settle = std::clamp(1.0f - std::fabs(scroll_ - float(selected_)) * 1.5f, 0.0f, 1.0f);
    float uv[4];
    cover_uv(cover, 1920, 1080, uv);
    g.image_part(cover, 0, 0, 1920, 1080, uv, with_alpha(kWhite, 0.22f * settle));
    /* Darker toward the bottom, where the strip and the hints are. */
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.35f), 0.4f, 0, kClear, 0);
}

/* One game in the spotlight: its cover over the whole screen, softly, its box
 * and what's known about it in front, and the rest of the library in a strip
 * below to move through. */
void App::draw_spotlight(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    if (shown == 0)
        return;
    Game &sel = games[std::size_t(selected_)];

    /* The box, and what's known about it. */
    const float sway = settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.6f) * 0.06f;
    draw_tile(&sel, 470, 470, kTileW * 0.98f, kTileH * 0.98f, 0.16f + sway, 1.0f, true, false);
    const float px = 790, pw = 1040;
    float y = 268;
    for (const std::string &line : wrap(g, Font::Bold, ts(48), sel.title, pw, 2))
    {
        g.text(Font::Bold, ts(48), px, y, kWhite, Align::Left, line);
        y += ts(48) * 1.15f;
    }
    if (sel.favourite)
        g.glyph(Glyph::Star, px + pw - 20, 290, 34, rgba(0xFFD45C));
    y += 6;
    g.text(Font::SemiBold, ts(26), px, y, kLavender, Align::Left, game_meta(sel));
    y += 52;
    std::string facts;
    for (const std::string &f : {sel.developer, sel.released})
        if (!f.empty())
            facts += (facts.empty() ? "" : "   \xE2\x80\xA2   ") + f;
    if (sel.players > 0)
        facts += (facts.empty() ? "" : "   \xE2\x80\xA2   ") +
                 plural((long long)sel.players, "1 player", "{n} players");
    if (!facts.empty())
    {
        g.text(Font::SemiBold, ts(24), px, y, kSoft, Align::Left, fit(g, Font::SemiBold, ts(24), facts, pw));
        y += 48;
    }
    if (!sel.synopsis.empty())
        for (const std::string &line : wrap(g, Font::Regular, ts(25), sel.synopsis, pw, 5))
        {
            g.text(Font::Regular, ts(25), px, y, kSoft, Align::Left, line);
            y += ts(25) * 1.38f;
        }

    /* The strip: the games either side, the chosen one in the middle. */
    const float sw = 84, sh = sw * 7.0f / 5.0f, step = 104, sy = dock_shown() ? 812 : 860;
    for (int i = std::max(0, selected_ - 9); i < std::min(shown, selected_ + 10); ++i)
    {
        const float d = float(i) - scroll_;
        const float x = kCx + d * step;
        if (x < 40 || x > 1880)
            continue;
        const bool on = i == selected_;
        const float edge = std::clamp((900.0f - std::fabs(x - kCx)) / 120.0f, 0.0f, 1.0f);
        const float grow = on ? 1.18f : 1.0f;
        Game &game = games[std::size_t(i)];
        if (on)
            g.panel(x - sw * grow * 0.5f - 6, sy - sh * grow * 0.5f - 6, sw * grow + 12, sh * grow + 12, kClear, 1, 10,
                    with_alpha(kIcy, edge), 2.4f, 10);
        if (Texture *cover = cover_of(game))
        {
            float uv[4];
            cover_uv(cover, sw, sh, uv);
            g.image_part(cover, x - sw * grow * 0.5f, sy - sh * grow * 0.5f, sw * grow, sh * grow, uv,
                         with_alpha(kWhite, (on ? 1.0f : 0.7f) * edge), 6);
        }
        else
            g.panel(x - sw * 0.5f, sy - sh * 0.5f, sw, sh, with_alpha(kTileFill, 0.6f * edge), 0.7f, 6,
                    with_alpha(kEdge, 0.6f * edge), 1.2f);
    }
}

/* ---- a glass cube ---------------------------------------------------------------------- */

/* A cube of glass turned every way, each face it shows lit by where it faces;
 * a picture on its front face. lit: a brighter rim (chosen). */
void App::draw_glass_cube(float cx, float cy, float size, float yaw, float pitch, float roll, Color tint, Color rim,
                          Texture *front, float alpha, bool lit)
{
    Gfx &g = *g_;
    const float h = size * 0.5f;
    const V3 faces[6][4] = {
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}}, /* front */
        {{h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h}},     /* back */
        {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}}, /* left */
        {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}},     /* right */
        {{-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h}}, /* top */
        {{-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}},     /* bottom */
    };
    const float light[6] = {1.0f, 0.55f, 0.70f, 0.80f, 1.15f, 0.50f};
    for (int f = 0; f < 6; ++f)
    {
        Corner q[4];
        for (int i = 0; i < 4; ++i)
            q[i] = seen3(cx, cy, turn3(faces[f][i], yaw, pitch, roll));
        if (!facing_you(q))
            continue;
        Glass face;
        face.tint = Color{tint.r * light[f], tint.g * light[f], tint.b * light[f], tint.a};
        face.rim = rim;
        face.radius = size * 0.10f;
        face.rim_w = lit ? 2.6f : 1.6f;
        face.phase = cx / 1920.0f + float(f) * 0.13f;
        face.fade = alpha;
        g.glass(q, size, size, 0, face);
        if (f == 0 && front)
        {
            Corner p[4];
            const float in = h * 0.70f;
            const V3 pic[4] = {{-in, -in, -h}, {in, -in, -h}, {in, in, -h}, {-in, in, -h}};
            for (int i = 0; i < 4; ++i)
                p[i] = seen3(cx, cy, turn3(pic[i], yaw, pitch, roll));
            g.quad3d(front, p, in * 2, in * 2, with_alpha(kWhite, alpha), 4, false, false);
        }
    }
}

/* Moving through the library in the views that move differently: the shelf
 * goes by rows, the box turns with the right stick. True when it moved. */
namespace
{
/* The letter a title files under: A-Z, else '#'. */
char letter_of(const Game &g)
{
    for (char c : g.title)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalpha(u))
            return char(std::toupper(u));
        if (std::isdigit(u))
            return '#';
    }
    return '#';
}
} // namespace

bool App::update_view_nav(bool left, bool right, bool up, bool down, double dt)
{
    const int shown = lib_->shown();
    const int view = lib_view_now();
    const int shelf_cols = glass() ? 5 : kShelfCols; /* the Porpoise look's rows: five across */
    bool moved = false;

    /* Held, the scrolling speeds up: one game a step at first, then 3, then
     * 10 (a long library crossed in seconds). */
    constexpr std::uint32_t kDirs = BtnLeft | BtnRight | BtnUp | BtnDown;
    if ((held_ & kDirs) && (prev_ & kDirs))
        nav_held_ += dt;
    else
        nav_held_ = 0; /* a fresh press (or one held since another screen) starts slow */
    const int step = nav_held_ > 3.0 ? 10 : nav_held_ > 1.2 ? 3 : 1;

    /* L2 / R2: the previous / next letter, sorted by title; else a page. */
    auto jump = [&](std::uint32_t bit, float &timer) {
        if (!(held_ & bit))
        {
            timer = 0;
            return false;
        }
        if (!(prev_ & bit))
        {
            timer = 0.4f;
            return true;
        }
        if (timer == 0)
            return false; /* held since Details (its own L2 / R2): wait for a new press */
        timer -= float(dt);
        if (timer <= 0)
        {
            timer = 0.16f;
            return true;
        }
        return false;
    };
    const bool back = jump(BtnL2, lib_rep_l2_), ahead = jump(BtnR2, lib_rep_r2_);
    if ((back || ahead) && shown > 1)
    {
        const auto &games = lib_->games();
        const int before = selected_;
        if (lib_->sort_order() == Library::Sort::Title)
        {
            const char here = letter_of(games[std::size_t(selected_)]);
            if (ahead)
            {
                int i = selected_;
                while (i + 1 < shown && letter_of(games[std::size_t(i)]) == here)
                    ++i;
                selected_ = i;
            }
            else
            {
                /* The start of this letter, or of the one before when at it. */
                int i = selected_;
                if (i > 0 && letter_of(games[std::size_t(i - 1)]) != here)
                    --i;
                const char want = letter_of(games[std::size_t(i)]);
                while (i > 0 && letter_of(games[std::size_t(i - 1)]) == want)
                    --i;
                selected_ = i;
            }
        }
        else
        {
            const int page = view == 3 ? shelf_cols * 3 : 10;
            selected_ = std::clamp(selected_ + (ahead ? page : -page), 0, shown - 1);
        }
        if (selected_ != before)
        {
            moved = true;
            if (settings_->reduced_motion)
                scroll_ = float(selected_);
        }
    }

    if (left && selected_ > 0)
    {
        selected_ = std::max(0, selected_ - step);
        moved = true;
    }
    if (right && selected_ + 1 < shown)
    {
        selected_ = std::min(shown - 1, selected_ + step);
        moved = true;
    }
    if (view == 5)
    {
        /* The list: up and down too. */
        if (up && selected_ > 0)
        {
            selected_ = std::max(0, selected_ - step);
            moved = true;
        }
        if (down && selected_ + 1 < shown)
        {
            selected_ = std::min(shown - 1, selected_ + step);
            moved = true;
        }
    }
    if (view == 3)
    {
        const int rows = step > 1 ? step / 3 + 1 : 1; /* 1, 2 or 4 rows a step */
        if (up && selected_ >= shelf_cols)
        {
            selected_ = std::max(selected_ % shelf_cols, selected_ - shelf_cols * rows);
            moved = true;
        }
        if (down && selected_ + shelf_cols < shown)
        {
            int to = selected_ + shelf_cols * rows;
            while (to >= shown)
                to -= shelf_cols;
            selected_ = to;
            moved = true;
        }
        else if (down && selected_ / shelf_cols < (shown - 1) / shelf_cols)
        {
            selected_ = shown - 1; /* the last row is short */
            moved = true;
        }
        const float target = std::max(0.0f, float(selected_ / shelf_cols) - 0.0f);
        row_scroll_ = settings_->reduced_motion ? target : smooth(row_scroll_, target, dt, 12.0f);
    }
    if (view == 4)
    {
        /* The right stick turns the box; let go, and it comes back round
         * to its front after a while. */
        if (std::fabs(right_x_) > 0.15f)
        {
            view_yaw_ += right_x_ * float(dt) * 3.4f;
            view_idle_ = 0;
        }
        else
        {
            view_idle_ += dt;
            if (view_idle_ > 3.0)
            {
                const float front = std::round(view_yaw_ / (2.0f * kPi)) * 2.0f * kPi;
                view_yaw_ = smooth(view_yaw_, front, dt, 3.0f);
            }
        }
        if (moved)
        {
            view_yaw_ = 0;
            view_idle_ = 0;
        }
    }
    return moved;
}
} // namespace porpoise::ui
