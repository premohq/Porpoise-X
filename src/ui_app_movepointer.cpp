/* Porpoise - the PlayStation Move's pointer in Porpoise's own menus.
 *
 * Oct 24 (Ruben): "just pointing and pulling T is fine. It's more natural."
 * Where the Move points is where the pointer is; what it's over lights as the
 * D-pad would; T pulls press it, let go where it was pulled (a hand drifting a
 * little as the finger squeezes still presses what it was on). T held and
 * swept goes along, a step for every so far (less touchy than the pointer
 * itself). Nothing happens just by pointing.
 *
 * The screens say where their buttons are as they draw them (mp_hit); a frame
 * later the pointer uses them.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_app.hpp"
#include "ui_gfx.hpp"
#include "porpoise_pad.hpp"

#include <algorithm>
#include <cmath>

namespace porpoise::ui
{
using namespace porpoise::pad;
void App::mp_hit(float x, float y, float w, float h, int id)
{
    if (mp_live_ && w > 0 && h > 0)
        mp_next_.push_back({x, y, w, h, id});
}

void App::mp_frame(std::uint32_t &held, bool active, bool vertical, float step_px)
{
    const porpoise::pad::State st = porpoise::pad::snapshot(0);
    mp_click_ = -1;
    mp_moved_to_ = -1;
    mp_hold_ = false;
    if (!mp_next_.empty())
    {
        mp_hits_.swap(mp_next_); /* (kept when no frame was drawn since) */
        mp_next_.clear();
    }
    if (!mp_holds_seen_)
    {
        mp_holds_seen_ = true;
        mp_holds_ = st.move_holds;
    }
    if (st.move_holds != mp_holds_)
    {
        mp_holds_ = st.move_holds;
        mp_hold_ = st.move;
    }
    const porpoise::pad::Motion &m = st.motion;
    mp_live_ = active && st.move && m.valid && m.centred;
#ifdef PORPOISE_HOST_PREVIEW
    if (preview_mp_x_ >= 0)
        mp_live_ = active;
#endif
    if (!mp_live_)
    {
        mp_t_ = mp_dragged_ = false;
        mp_hover_ = mp_hover_was_ = -1;
        mp_steps_ = 0;
        mp_hits_.clear();
        return;
    }
    mp_x_ = 960 + std::clamp(m.aim_x, -1.02f, 1.02f) * 960;
    mp_y_ = 540 + std::clamp(m.aim_y, -1.02f, 1.02f) * 540;
#ifdef PORPOISE_HOST_PREVIEW
    if (preview_mp_x_ >= 0)
    {
        mp_x_ = preview_mp_x_;
        mp_y_ = preview_mp_y_;
    }
#endif
    /* T is the pointer's while it's here (in the menus it's R2). */
    const bool t = (held & BtnR2) != 0;
    held &= ~std::uint32_t(BtnR2);

    if (!(mp_t_ && mp_dragged_))
    {
        mp_hover_ = -1;
        for (const MpHit &h : mp_hits_) /* (the last drawn is on top) */
            if (mp_x_ >= h.x && mp_x_ < h.x + h.w && mp_y_ >= h.y && mp_y_ < h.y + h.h)
                mp_hover_ = h.id;
        if (mp_hover_ != mp_hover_was_ && mp_hover_ >= 0)
            mp_moved_to_ = mp_hover_;
        mp_hover_was_ = mp_hover_;
    }
    if (t && !mp_t_)
    {
        mp_t_ = true;
        mp_dragged_ = false;
        mp_t_x_ = mp_x_;
        mp_t_y_ = mp_y_;
        mp_press_on_ = mp_hover_;
    }
    else if (t)
    {
        const float d = vertical ? mp_y_ - mp_t_y_ : mp_x_ - mp_t_x_;
        if (!mp_dragged_)
        {
            if (std::fabs(d) > 48)
            {
                mp_dragged_ = true; /* a sweep, not a press: from here */
                mp_t_x_ = mp_x_;
                mp_t_y_ = mp_y_;
            }
        }
        else
        {
            const int n = int(d / step_px);
            if (n != 0)
            {
                mp_steps_ = std::clamp(mp_steps_ + n, -8, 8);
                (vertical ? mp_t_y_ : mp_t_x_) += float(n) * step_px;
            }
        }
    }
    else if (mp_t_)
    {
        mp_t_ = false;
        if (!mp_dragged_)
            mp_click_ = mp_press_on_ >= 0 ? mp_press_on_ : mp_hover_;
        mp_dragged_ = false;
        mp_hover_was_ = -1; /* (what it's over lights again) */
    }
}

int App::mp_take_step()
{
    if (mp_steps_ == 0)
        return 0;
    const int s = mp_steps_ > 0 ? 1 : -1;
    mp_steps_ -= s;
    return s;
}

void App::draw_move_pointer()
{
    if (!mp_live_)
        return;
    Gfx &g = *g_;
    g.set_layer();
    const bool toned = g.toned();
    g.set_tone(false);
    const float x = mp_x_, y = mp_y_;
    const bool over = mp_hover_ >= 0 || mp_t_;
    const float r = mp_t_ ? 13.0f : over ? 17.0f : 15.0f;
    g.blob(x + 3, y + 5, r * 3.2f, r * 3.2f, rgba(0x000000, 0.28f));
    g.panel(x - r, y - r, r * 2, r * 2, rgba(0xFFFFFF, over ? 0.95f : 0.85f), 1, r, rgba(0x1A2230, 0.55f), 2.5f,
            over ? 10.0f : 0.0f);
    g.panel(x - 4, y - 4, 8, 8, rgba(0x1A2230, 0.85f), 1, 4);
    g.set_tone(toned);
}
} // namespace porpoise::ui
