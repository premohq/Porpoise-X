/* Porpoise UI - the Porpoise look: your games as real cases in a room lit by
 * the one you're on, the filters along the top, the island top right (what's
 * connected and the time), and one menu, from anywhere, that grows into
 * Settings and Memory Cards. Classic keeps its own screens and gets the island.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "porpoise_move.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_states.hpp"
#include "ui_app.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
constexpr float kToastFor = 4.5f; /* as ui_app_ctl.cpp's notices */
/* The menu's glass, and the whole screen it grows to. */
constexpr float kMenuX = 1280, kMenuY = 28, kMenuW = 612, kMenuH = 1024;
constexpr float kPageX = 28; /* its right edge stays the menu's: 1892 */
/* A real case is 136 x 191 x 15 mm: its depth, for its width. */
constexpr float kCaseDepth = 14.0f / 136.0f; /* a real case: 14 mm deep, 136 mm wide */

float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float smooth01(float a, float b, float v)
{
    const float t = clamp01((v - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

bool preview_kit()
{
#ifdef PORPOISE_HOST_PREVIEW
    return std::getenv("PV_CTL") || std::getenv("PV_GLASS");
#else
    return false;
#endif
}

const char *const kShowNames[5] = {"All", "GameCube", "Wii", "Channels", "Favorites"};
const char *const kSortNames[4] = {"Title A-Z", "Last Played", "Most Played", "Favorites First"};
const char *const kDayNames[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kDayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const kMonthNames[12] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};

/* When, short, for a "Last Played" row: "6 days ago", "Yesterday". */
std::string short_ago(long long then)
{
    if (then <= 0)
        return tr("Not yet");
    const long long d = (long long)std::time(nullptr) - then;
    if (d < 24 * 60 * 60)
        return tr("Today");
    if (d < 2 * 24 * 60 * 60)
        return tr("Yesterday");
    if (d < 14 * 24 * 60 * 60)
        return trf("{n} days ago", {{"n", std::to_string(d / (24 * 60 * 60))}});
    if (d < 30LL * 24 * 60 * 60)
        return trf("{n} weeks ago", {{"n", std::to_string(d / (7 * 24 * 60 * 60))}});
    return tr("A while ago");
}

std::string long_date()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const std::string month = tr(kMonthNames[std::clamp(tm.tm_mon, 0, 11)]), day = std::to_string(tm.tm_mday);
    if (language() == Language::English)
        return std::string(kDayNames[std::clamp(tm.tm_wday, 0, 6)]) + ", " + month + " " + day;
    return tr(kDayShort[std::clamp(tm.tm_wday, 0, 6)]) + " " + day + " " + month;
}
} // namespace

/* ---- what there is to choose ------------------------------------------------------------ */

void App::glass_filters(std::vector<int> &shows) const
{
    /* All, GameCube and Wii always; Channels and Favorites once there are some. */
    shows = {int(Library::Show::All), int(Library::Show::GameCube), int(Library::Show::Wii)};
    if (lib_->count_shown(Library::Show::Channels) > 0)
        shows.push_back(int(Library::Show::Channels));
    if (lib_->count_shown(Library::Show::Favourites) > 0)
        shows.push_back(int(Library::Show::Favourites));
}

std::vector<App::GlassItem> App::glass_items()
{
    /* The library's own first (this screen), then every Settings section with
     * what it's set to now. */
    std::vector<GlassItem> items;
    items.push_back({4, -1, tr("This Screen") + " \xC2\xB7 " + tr("Library"), ""});
    items.push_back({1, -1, tr("Sort By"), tr(kSortNames[std::clamp(int(lib_->sort_order()), 0, 3)])});
    {
        const int show = std::clamp(int(lib_->show()), 0, 4);
        items.push_back({6, -1, tr("Show"), show == 0 ? tr("All Games") : tr(kShowNames[show])});
    }
    if (lib_->shown() > 0)
    {
        /* The chosen game a favorite, or not (Options starts games now). */
        const Game &game = lib_->games()[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
        items.push_back({8, -1, game.favourite ? tr("Remove from Favorites") : tr("Add to Favorites"), ""});
    }
    items.push_back({3, -1, tr("Memory Cards"), ""});
    items.push_back({4, -1, tr("Settings"), ""});
    const int n = section_count();
    for (int i = 0; i < n; ++i)
    {
        const std::string name = section_name(i);
        items.push_back({5, i, tr(name), glass_section_value(name)});
    }
    /* And the whole of Settings, from its list of tabs. */
    items.push_back({7, -1, tr("Full Settings"), ""});
    return items;
}

void App::glass_open_menu()
{
    /* Not over the island: it and the menu never open together (Oct 24: closing
     * one, people opened the other). */
    if (isl_open_ || isl_dragging_)
        return;
    if (tab_ != Tab::Settings)
        build_settings(); /* the sections as they are now */
    gl_menu_ = true;
    const std::vector<GlassItem> items = glass_items();
    /* Settings' first tab (Games) to begin with: where people go first. */
    gl_row_ = 1;
    for (int i = int(items.size()) - 1; i >= 0; --i)
        if (items[std::size_t(i)].kind == 5 && items[std::size_t(i)].section == 0)
            gl_row_ = i;
    for (int i = 0; i < int(items.size()); ++i)
        if ((tab_ == Tab::MemoryCards && items[std::size_t(i)].kind == 3) ||
            (tab_ == Tab::Settings && items[std::size_t(i)].kind == 5 && items[std::size_t(i)].section == rail_))
            gl_row_ = i;
    sfx(Sound::DetailsFlip);
}

void App::glass_switch_filter()
{
    if (gl_fpend_ < 0)
        return;
    auto &games = lib_->games();
    const std::string key = lib_->shown() > 0 ? Library::key_of(games[std::size_t(selected_)]) : "";
    lib_->set_show(Library::Show(gl_fpend_));
    keep_selection(key);
    lib_->save();
    gl_fpend_ = -1;
    gl_fout_ = 0;
    gl_fin_ = settings_->reduced_motion ? 0.0f : 1.0f;
    /* The new row arrives from the side it's going to, gliding into place. */
    if (!settings_->reduced_motion && lib_->shown() > 0)
        scroll_ = float(selected_) - float(gl_fdir_) * 2.6f;
}

/* ---- motion ------------------------------------------------------------------------------ */

void App::glass_tick(double dt)
{
    const bool calm = settings_->reduced_motion;
    /* The room takes the colours of the game you're on, slowly. */
    float want[6] = {0.10f, 0.30f, 0.60f, 0.30f, 0.20f, 0.60f};
    if (lib_ && lib_->shown() > 0)
    {
        const Game &game = lib_->games()[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
        if (game.cover)
            std::memcpy(want, game.cover->tone, sizeof want);
    }
    for (int i = 0; i < 6; ++i)
        gl_amb_[i] = calm ? want[i] : smooth(gl_amb_[i], want[i], dt, 2.6f);

    /* The menu's glass: in from the right, grown to the screen for Settings and
     * Memory Cards, shrunk back to the menu when you leave them. One edge
     * moves - its left; its right stays at the screen's - on a spring, so it
     * eases off and eases in (it used to leap out and slow down, and the room
     * went pale before the glass had arrived). Dragged along the touch pad, it
     * follows the finger. */
    /* (Only the Porpoise look's grows into Settings; the other themes' is the menu alone.) */
    const bool page = glass() && !gl_menu_ &&
                      ((screen_ == Screen::Main && (tab_ == Tab::Settings || tab_ == Tab::MemoryCards)) ||
                       screen_ == Screen::GameSettings);
    float to = gl_menu_ ? kMenuX : page ? kPageX : 1960.0f;
    float omega = page || gl_slab_[0] < kMenuX - 1 ? 11.5f : 15.0f;
    if (gl_dragging_)
    {
        to = 1960.0f - (1960.0f - kMenuX) * clamp01(gl_drag_);
        omega = 34.0f;
    }
    if (gl_menu_)
        gl_menu_seen_ = true;
    else if (gl_slab_[0] > 1950)
        gl_menu_seen_ = false;
    if (calm || gl_snap_)
    {
        gl_slab_[0] = to;
        gl_slab_v_ = 0;
    }
    else
    {
        /* Critically damped: no overshoot. Small steps, steady at any rate. */
        const int steps = std::max(1, int(dt * 240.0 + 0.5));
        const float h = float(dt) / float(steps);
        for (int i = 0; i < steps; ++i)
        {
            const float a = omega * omega * (to - gl_slab_[0]) - 2.0f * omega * gl_slab_v_;
            gl_slab_v_ += a * h;
            gl_slab_[0] += gl_slab_v_ * h;
        }
        if (std::fabs(to - gl_slab_[0]) < 0.25f && std::fabs(gl_slab_v_) < 4.0f)
        {
            gl_slab_[0] = to;
            gl_slab_v_ = 0;
        }
    }
    gl_slab_[1] = kMenuY;
    gl_slab_[3] = kMenuH;
    gl_slab_[2] = gl_slab_[0] < kMenuX ? kMenuX + kMenuW - gl_slab_[0] : kMenuW;
    gl_slab_a_ = clamp01((1960.0f - gl_slab_[0]) / 60.0f);
    gl_page_ = clamp01((kMenuX - gl_slab_[0]) / (kMenuX - kPageX));

    /* L1 / R1: the cases slide off the way you're going, the filter changes,
     * and the new ones slide in from the other side. */
    if (gl_fpend_ >= 0)
    {
        gl_fout_ = calm ? 0.0f : gl_fout_ - float(dt) / 0.13f;
        if (gl_fout_ <= 0)
            glass_switch_filter();
    }
    gl_fin_ = std::max(0.0f, gl_fin_ - float(dt) / 0.32f);

    /* Details: the right stick turns the case round, as far as you like; let
     * go, and it comes back to its front after a while. */
    if (screen_ == Screen::Details)
    {
        if (std::fabs(right_x_) > 0.15f)
        {
            gl_turn_ += right_x_ * float(dt) * 3.4f;
            gl_turn_idle_ = 0;
        }
        else if ((gl_turn_idle_ += dt) > 2.5)
        {
            const float front = std::round(gl_turn_ / (2.0f * kPi)) * 2.0f * kPi;
            gl_turn_ = calm ? front : smooth(gl_turn_, front, dt, 3.0f);
        }
    }
    else
    {
        gl_turn_ = 0;
        gl_turn_idle_ = 0;
    }

    /* A game's achievements' list: in and out, and its scroll. */
    if (screen_ != Screen::Details && screen_ != Screen::Achievements)
        gl_ach_ = false;
    gl_ach_k_ = calm ? (gl_ach_ ? 1.0f : 0.0f) : smooth(gl_ach_k_, gl_ach_ ? 1.0f : 0.0f, dt, 9.0f);
    {
        const float target = std::max(0.0f, float(gl_ach_row_) - 3.0f);
        gl_ach_first_to_ = target;
        gl_ach_first_ = calm ? target : smooth(gl_ach_first_, target, dt, 12.0f);
    }
    /* What glides: the filter's line, the menu's highlight and scroll. */
    if (gl_line_tx_ >= 0 && gl_line_x_ >= 0)
    {
        gl_line_x_ = calm ? gl_line_tx_ : smooth(gl_line_x_, gl_line_tx_, dt, 16.0f);
        gl_line_w_ = calm ? gl_line_tw_ : smooth(gl_line_w_, gl_line_tw_, dt, 16.0f);
    }
    if (gl_hi_to_ >= 0 && gl_hi_ >= 0)
        gl_hi_ = calm ? gl_hi_to_ : smooth(gl_hi_, gl_hi_to_, dt, 18.0f);
    gl_menu_first_ = calm ? gl_first_to_ : smooth(gl_menu_first_, gl_first_to_, dt, 14.0f);
}

void App::island_tick(double dt)
{
    /* A swipe down the touch pad opens the island, a swipe up closes it: a
     * quarter of the pad's height, quickly. A slow drag starts over. */
    const Motion &m = porpoise::pad::state(0).motion;
    if (m.touching)
    {
        if (!isl_touch_)
        {
            isl_touch_ = true;
            isl_swiped_ = false;
            isl_y0_ = m.touch_y;
            isl_x0_ = m.touch_x;
            isl_t0_ = time_;
            gl_drag_speed_ = 0;
        }
        else if (isl_dragging_)
        {
            /* The island, on the finger: down opens it, up shuts it. */
            const float was = isl_drag_;
            isl_drag_ = clamp01(isl_drag_base_ + float(m.touch_y - isl_y0_) / 420.0f);
            if (dt > 0)
                isl_drag_speed_ = isl_drag_speed_ * 0.6f + 0.4f * (isl_drag_ - was) / float(dt);
        }
        else if (gl_dragging_)
        {
            /* The Porpoise look's menu, on the finger: across about half the
             * pad is all the way. */
            const float was = gl_drag_;
            const float dx = float(m.touch_x - isl_x0_);
            gl_drag_ = gl_menu_drag_in_ ? clamp01(-dx / 960.0f) : clamp01(1.0f + (-dx) / 960.0f);
            if (dt > 0)
                gl_drag_speed_ = gl_drag_speed_ * 0.6f + 0.4f * (gl_drag_ - was) / float(dt);
        }
        else if (!isl_swiped_)
        {
            const int dy = m.touch_y - isl_y0_, dx = m.touch_x - isl_x0_;
            /* The Porpoise look: sideways from the pad's right part, the menu
             * comes in with the finger; sideways to the right, it goes. (Not
             * with the island open: sideways there picks its shortcuts.) */
            if (std::abs(dx) > 36 && std::abs(dx) > std::abs(dy) * 1.5f)
            {
                isl_swiped_ = true;
                if (!isl_open_ && dx < 0 && isl_x0_ > 1150 && !gl_menu_)
                    gl_drag_start_ = +1;
                else if (!isl_open_ && dx > 0 && gl_menu_)
                    gl_drag_start_ = -1;
                gl_swipe_at_ = time_;
            }
            else if (std::abs(dy) > 30 && std::abs(dy) > std::abs(dx) * 1.5f && !gl_menu_)
            {
                /* Up and down: the island follows the finger from here. */
                isl_swiped_ = true;
                isl_dragging_ = true;
                isl_drag_base_ = isl_open_ ? 1.0f : 0.0f;
                isl_drag_ = isl_drag_base_;
                isl_drag_speed_ = 0;
            }
        }
    }
    else
    {
        if (isl_touch_ && gl_dragging_)
            gl_drag_let_go_ = true;
        if (isl_dragging_)
        {
            /* Let go: open past halfway, or flicked that way. */
            isl_dragging_ = false;
            const bool open = isl_drag_speed_ > 2.0f || (isl_drag_speed_ > -2.0f && isl_drag_ > 0.5f);
            if (open != isl_open_)
                sfx(open ? Sound::DetailsFlip : Sound::MenuScroll);
            isl_open_ = open;
        }
        isl_touch_ = false;
    }
    /* The shortcuts: the finger picks (where it is across the pad, a light tick
     * as it moves to another), a click of the pad opens it. */
    if (!isl_open_ || dialog_.open)
        isl_pick_ = -1;
    else
    {
        if (m.touching && !isl_dragging_)
        {
            const int pick = std::clamp(m.touch_x * 4 / 1920, 0, 3);
            if (pick != isl_pick_)
            {
                if (isl_pick_ >= 0 && settings_->menu_haptics)
                    porpoise::pad::menu_tick(30, 16);
                isl_pick_ = pick;
            }
        }
        if (pressed(BtnTouch) && isl_pick_ >= 0)
        {
            prev_ |= BtnTouch; /* (taken: not the Revolution look's pointer too) */
            porpoise::pad::menu_tick(70, 34);
            island_shortcut(isl_pick_);
        }
        else if (pressed(BtnCircle))
        {
            prev_ |= BtnCircle; /* (Circle closes it, nothing under it) */
            isl_open_ = false;
            sfx(Sound::MenuScroll);
        }
    }
    if (isl_w_ <= 0)
    {
        isl_w_ = isl_tw_;
        isl_h_ = isl_th_;
    }
    const bool calm = settings_->reduced_motion;
    const float rate = isl_dragging_ ? 30.0f : 13.0f; /* (on the finger: close behind it) */
    isl_w_ = calm ? isl_tw_ : smooth(isl_w_, isl_tw_, dt, rate);
    isl_h_ = calm ? isl_th_ : smooth(isl_h_, isl_th_, dt, rate);
}

/* ---- input ------------------------------------------------------------------------------- */

App::Action App::update_glass(bool left, bool right, bool up, bool down, double dt, bool &handled)
{
    (void)dt;
    auto &games = lib_->games();
    handled = false;
    {
        /* The touch pad: a drag in from the right brings the menu in on the
         * finger, one to the right takes it away (island_tick follows it). */
        const int start = time_ - gl_swipe_at_ < 0.3 ? gl_drag_start_ : 0; /* (not one left from another screen) */
        gl_drag_start_ = 0;
        if (start > 0 && !gl_menu_ && (screen_ == Screen::Main || screen_ == Screen::Details))
        {
            handled = true;
            bool ok = true;
            if (tab_ == Tab::Settings || tab_ == Tab::MemoryCards)
            {
                set_tab(int(Tab::Library), -1); /* asks first when changes wait */
                ok = tab_ == Tab::Library;
            }
            if (ok)
            {
                glass_open_menu();
                gl_dragging_ = true;
                gl_menu_drag_in_ = true;
                gl_drag_ = 0;
                return Action::None;
            }
        }
        if (start < 0 && gl_menu_)
        {
            handled = true;
            gl_dragging_ = true;
            gl_menu_drag_in_ = false;
            gl_drag_ = 1;
            return Action::None;
        }
        if (gl_drag_let_go_)
        {
            gl_drag_let_go_ = false;
            if (gl_dragging_)
            {
                gl_dragging_ = false;
                /* Let go: open past a third of the way, or flicked that way. */
                const bool open = gl_drag_speed_ > 1.6f || (gl_drag_speed_ > -1.6f && gl_drag_ > (gl_menu_drag_in_ ? 0.33f : 0.67f));
                if (!open && gl_menu_)
                {
                    gl_menu_ = false;
                    sfx(Sound::MenuScroll);
                }
                gl_slab_v_ = gl_drag_speed_ * -(1960.0f - kMenuX) * 0.5f; /* it carries on as it was going */
            }
        }
        if (gl_dragging_ && !gl_menu_)
            gl_dragging_ = false; /* closed some other way */
    }
    if (gl_menu_)
    {
        handled = true;
        return update_menu_rows(left, right, up, down);
    }

    /* A game's page: down to its achievements' list, and around in it. */
    if (screen_ == Screen::Details && lib_->shown() > 0)
    {
        Game &game = games[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
        const AchievementSet &ach = details_achievements(game);
        if (gl_ach_)
        {
            handled = true;
            const int n = int(ach.list.size());
            if (!ach.valid() || pressed(BtnCircle) || (up && gl_ach_row_ == 0))
            {
                gl_ach_ = false;
                sfx(Sound::MenuScroll);
                return Action::None;
            }
            if (up && gl_ach_row_ > 0)
            {
                --gl_ach_row_;
                sfx(Sound::MenuScroll);
            }
            if (down && gl_ach_row_ + 1 < n)
            {
                ++gl_ach_row_;
                sfx(Sound::MenuScroll);
            }
            /* (Cross opens nothing: everything is here already, Oct 24) */
            if (pressed(BtnTriangle))
            {
                gl_ach_ = false;
                glass_open_menu();
            }
            return Action::None;
        }
        if (pressed(BtnOptions))
        {
            /* Options - start - plays it, straight in (update below). */
            handled = false;
        }
        else if (pressed(BtnR1))
        {
            /* R1: a favourite, or not. */
            lib_->toggle_favourite(game);
            gl_heart_at_ = time_;
            sfx(game.favourite ? Sound::LaunchGame : Sound::MovingTab);
            handled = true;
            return Action::None;
        }
        if (down && ach.valid())
        {
            gl_ach_ = true;
            gl_ach_row_ = 0;
            gl_ach_first_to_ = 0;
            sfx(Sound::MenuScroll);
            handled = true;
            return Action::None;
        }
    }
    /* Triangle: the menu, from the library, a game's page and Settings (in
     * Memory Cards it deletes a save, as it always has). */
    if (pressed(BtnTriangle) && (screen_ == Screen::Main || screen_ == Screen::Details) &&
        tab_ != Tab::MemoryCards)
    {
        handled = true;
        if (tab_ == Tab::Settings)
        {
            set_tab(int(Tab::Library), -1); /* asks first when changes wait */
            if (tab_ != Tab::Library)
                return Action::None;
        }
        glass_open_menu();
        return Action::None;
    }
    /* Out of Settings' sections, or Memory Cards: back to the menu they came from. */
    if (screen_ == Screen::Main && pressed(BtnCircle) &&
        ((tab_ == Tab::Settings && on_rail_) || (tab_ == Tab::MemoryCards && mc_view() != 2)))
    {
        handled = true;
        const int section = rail_;
        const bool cards = tab_ == Tab::MemoryCards;
        set_tab(int(Tab::Library), -1);
        if (tab_ != Tab::Library)
            return Action::None; /* Apply or Discard first */
        glass_open_menu();
        const std::vector<GlassItem> items = glass_items();
        for (int i = 0; i < int(items.size()); ++i)
            if ((cards && items[std::size_t(i)].kind == 3) ||
                (!cards && items[std::size_t(i)].kind == 5 && items[std::size_t(i)].section == section))
                gl_row_ = i;
        return Action::None;
    }
    /* L1 / R1 in the library: the filters along the top. */
    if (tab_ == Tab::Library && screen_ == Screen::Main && (l1_tap_ || pressed(BtnR1)))
    {
        handled = true;
        std::vector<int> shows;
        glass_filters(shows);
        int at = 0;
        for (int i = 0; i < int(shows.size()); ++i)
            if (shows[std::size_t(i)] == int(lib_->show()))
                at = i;
        const int n = int(shows.size());
        if (gl_fpend_ >= 0)
            glass_switch_filter(); /* the one asked for before this: now */
        for (int i = 0; i < int(shows.size()); ++i)
            if (shows[std::size_t(i)] == int(lib_->show()))
                at = i;
        at = (at + (l1_tap_ ? n - 1 : 1)) % n;
        gl_fpend_ = shows[std::size_t(at)];
        gl_fdir_ = l1_tap_ ? -1 : 1;
        gl_fout_ = 1;
        sfx(Sound::MovingTab);
        return Action::None;
    }
    /* A filter with nothing left in it (the last favorite gone): All again. */
    if (tab_ == Tab::Library && lib_->show() != Library::Show::All && lib_->shown() == 0 && !games.empty())
    {
        lib_->set_show(Library::Show::All);
        keep_selection("");
        lib_->save();
    }
    if (screen_ == Screen::Details && (pressed(BtnL2) || pressed(BtnR2)))
        gl_turn_ = 0;
    return Action::None;
}

/* ---- pieces ------------------------------------------------------------------------------- */

namespace
{
/* The look's colours, drawn over the room: white on dark (toned to ink on
 * Light by the renderer), and a little less of it for what's quieter. */
inline Color ink(float a)
{
    return with_alpha(kWhite, a);
}
/* 4.5: the side menu, the island and the in-game menu in every theme, each in
 * its own colors: 0 the Porpoise look's glass, 1 Porpoise Legacy's sapphire,
 * 2 Revolution's white, 3 Star Cube's violet night (App::set_skin). */
int g_skin = 0;
const Color kInkDark = rgba(0x0B1220); /* on the chosen button's white */
const Color kGood = rgba(0x55E08E);
} // namespace

void App::glass_pill(float x, float y, float w, float h, float r, float alpha, bool bright)
{
    /* Thin glass (shaders/ui.frag frost_panel): a frosting of the room, a
     * hairline edge, a highlight inside the top edge, a soft shadow. */
    Gfx &g = *g_;
    const int mode = std::clamp(settings_->ui_mode, 0, 2);
    const bool toned = g.toned();
    g.set_tone(false);
    Color fill, edge;
    if (g_skin != 0)
    {
        /* The other themes' own panels. */
        const bool solid = g.solid();
        g.set_solid(true);
        if (g_skin == 1)
            g.panel(x, y, w, h, bright ? rgba(0x1F63F0, 0.9f * alpha) : rgba(0x0A1236, std::min(1.0f, 0.80f * alpha)), 1, r,
                    bright ? rgba(0x8BD9FF, alpha) : rgba(0x3D4F9E, std::min(1.0f, 0.9f * alpha)), 1.6f);
        else if (g_skin == 2)
            g.panel(x, y, w, h, bright ? rgba(0xEAF7FD, alpha) : rgba(0xFFFFFF, std::min(1.0f, 0.95f * alpha)), 1, r,
                    bright ? rgba(0x3DB8EC, alpha) : rgba(0xC6CCD4, std::min(1.0f, alpha)), bright ? 2.5f : 1.6f);
        else
            g.panel(x, y, w, h, bright ? rgba(0x3A2FD0, 0.85f * alpha) : rgba(0x0C0B22, std::min(1.0f, 0.84f * alpha)), 1,
                    r, bright ? rgba(0xD0C6FF, alpha) : rgba(0x6D6AF0, std::min(1.0f, 0.55f * alpha)), 1.4f);
        g.set_solid(solid);
        g.set_tone(toned);
        return;
    }
    if (bright)
    {
        fill = rgba(0xF6F8FB, (mode == 2 ? 1.0f : 0.92f) * alpha);
        edge = mode == 2 ? rgba(0xB8C2CF, 0.9f * alpha) : rgba(0xFFFFFF, 0.65f * alpha);
    }
    else if (mode == 2)
    {
        fill = rgba(0xFFFFFF, 0.56f * alpha);
        edge = rgba(0xFFFFFF, 0.95f * alpha);
    }
    else
    {
        fill = rgba(0xFFFFFF, (mode == 1 ? 0.065f : 0.085f) * alpha);
        edge = rgba(0xFFFFFF, (mode == 1 ? 0.16f : 0.20f) * alpha);
    }
    /* It comes and goes with alpha as a whole (the frosting alone was opaque
     * at any alpha: the help's glass and the like popped in). Most callers
     * give it 0.75 and up when it's fully there. */
    g.set_presence(alpha / 0.75f);
    g.panel(x, y, w, h, fill, 1, r, edge, 1.0f);
    g.set_presence(1.0f);
    g.set_tone(toned);
}

void App::camera_icon(float cx, float cy, float size, Color c)
{
    ctl_art(-1, 0, 0, 0, kClear); /* (loads the pictures) */
    if (gl_ctl_)
    {
        ctl_art(8, cx, cy, size * 1.08f, c); /* the PS3 Eye's own picture: a touch smaller than a controller */
        return;
    }
    /* The PlayStation Eye: a wide body with its lens, on a stand. */
    Gfx &g = *g_;
    const float u = size / 30.0f;
    g.set_solid(true);
    g.panel(cx - 13 * u, cy - 9 * u, 26 * u, 13 * u, with_alpha(c, 0.0f), 1, 3 * u, c, 1.8f * u);
    g.panel(cx - 6.5f * u, cy - 6 * u, 7 * u, 7 * u, with_alpha(c, 0.0f), 1, 3.5f * u, c, 1.8f * u);
    g.panel(cx + 1.5f * u, cy + 4 * u, 1.8f * u, 4 * u, c, 1, 0.9f * u);
    g.panel(cx - 4.5f * u, cy + 8 * u, 12 * u, 1.8f * u, c, 1, 0.9f * u);
    g.set_solid(false);
}

float App::draw_platform(const Game &game, float x, float cy, float h, float alpha)
{
    /* The console's mark, the GameCube's and the Wii's at the same height:
     * white on Dark and OLED, in their own colours on Light. */
    Gfx &g = *g_;
    if (!gl_icons_tried_)
    {
        gl_icons_tried_ = true;
        const std::string dir = g.asset_dir() + "/ui/";
        if (access((dir + "platform-wii.png").c_str(), R_OK) == 0)
            gl_wii_ = g.texture_file(dir + "platform-wii.png", 256);
        if (access((dir + "platform-gamecube.png").c_str(), R_OK) == 0)
            gl_gc_ = g.texture_file(dir + "platform-gamecube.png", 256);
    }
    const bool wii = game.platform == "Wii";
    Texture *t = wii ? gl_wii_ : gl_gc_;
    if (!wii)
        h *= 1.45f; /* the cube is square, the Wii's word wide: this way they look the same size */
    if (!game.kind.empty() || !t || t->height <= 0)
    {
        /* A channel, or no mark: its kind in small capitals. */
        const std::string name = game.kind.empty() ? game.platform : tr(game.kind);
        return g.text_mid(Font::ExtraBold, h * 0.62f, x, cy, ink(0.85f * alpha), Align::Left, name, 1.2f);
    }
    const float w = h * float(t->width) / float(t->height);
    const bool light = settings_->ui_mode == 2;
    const Color tint = light ? (wii ? rgba(0x2AA7E0) : rgba(0x6B4FD3)) : rgba(0xFFFFFF);
    g.image(t, x, cy - h * 0.5f, w, h, with_alpha(tint, alpha));
    return w;
}

void App::draw_glass_brand(float alpha)
{
    if (alpha <= 0.01f)
        return;
    /* Ruben's wordmark: black on Light, all white on OLED, the cyan dolphin on Dark. */
    const int mode = settings_->ui_mode;
    draw_wordmark(50, 72, 82, alpha, mode == 2 ? 2 : mode == 1 ? 1 : 0);
}

void App::draw_glass_filters(float alpha)
{
    /* Flat, along the top: L1 and R1 at the ends, each filter with its count,
     * the one you're on in bold over a line in the game's colour. */
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    std::vector<int> shows;
    glass_filters(shows);
    const float cy = 72, size = ts(24), csize = ts(18), gap = 44;
    struct Item
    {
        std::string name, count;
        float w, nw;
        bool on;
    };
    std::vector<Item> items;
    float total = 0;
    for (int s : shows)
    {
        Item it;
        it.on = s == (gl_fpend_ >= 0 ? gl_fpend_ : int(lib_->show())); /* the one asked for, at once */
        it.name = tr(kShowNames[std::clamp(s, 0, 4)]);
        it.count = std::to_string(lib_->count_shown(Library::Show(s)));
        it.nw = g.measure(it.on ? Font::Bold : Font::SemiBold, size, it.name);
        it.w = it.nw + 8 + g.measure(Font::Bold, csize, it.count);
        total += it.w + (items.empty() ? 0.0f : gap);
        items.push_back(it);
    }
    float x = 960 - total * 0.5f;
    for (int i = 0; i < 2; ++i)
    {
        /* The keycaps: outlined, as on the controller. */
        const float kw = 52, kx = i == 0 ? x - 36 - kw : x + total + 36;
        g.glyph(i == 0 ? Glyph::L1 : Glyph::R1, kx + kw * 0.5f, cy, 38, ink(0.95f * alpha));
    }
    float line_x = -1, line_w = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const Item &it = items[i];
        /* The chosen one, when it's GameCube or Wii: in its logo's color (the
         * others, and All, in ink). */
        const int show = shows[i];
        /* (Light only: on Dark and OLED the consoles' marks are white, so are the tabs.) */
        const bool brand = it.on && (show == 1 || show == 2) && settings_->ui_mode == 2;
        if (brand)
        {
            const std::uint32_t hue = show == 1 ? 0x6A5ACD : 0x2CA6E0;
            const bool toned = g.toned();
            g.set_tone(false);
            const Look was_look = g.look();
            Look full = was_look; /* (the colors as they are, not the look's) */
            full.hue = -1;
            full.saturation = 1;
            full.dark = 1;
            full.fill_alpha = 1;
            g.set_look(full);
            g.text_mid(it.on ? Font::Bold : Font::SemiBold, size, x, cy, rgba(hue, (it.on ? 1.0f : 0.82f) * alpha),
                       Align::Left, it.name);
            g.text_mid(Font::Bold, csize, x + it.nw + 8, cy + 1, rgba(hue, (it.on ? 0.62f : 0.48f) * alpha),
                       Align::Left, it.count);
            g.set_look(was_look);
            g.set_tone(toned);
        }
        else
        {
            g.text_mid(it.on ? Font::Bold : Font::SemiBold, size, x, cy, ink((it.on ? 1.0f : 0.70f) * alpha),
                       Align::Left, it.name);
            g.text_mid(Font::Bold, csize, x + it.nw + 8, cy + 1, ink((it.on ? 0.60f : 0.42f) * alpha), Align::Left,
                       it.count);
        }
        if (it.on)
        {
            line_x = x;
            line_w = it.w;
        }
        x += it.w + gap;
    }
    if (line_x < 0)
        return;
    gl_line_tx_ = line_x; /* it glides there (glass_tick) */
    gl_line_tw_ = line_w;
    if (gl_line_x_ < 0 || settings_->reduced_motion || gl_snap_)
    {
        gl_line_x_ = line_x;
        gl_line_w_ = line_w;
    }
    /* The line: ink - white, or the slate of Light (it took the game's color,
     * and turned brown and back as you went along). */
    const bool light = settings_->ui_mode == 2;
    const Color c = light ? rgba(0x1B2230, alpha) : rgba(0xFFFFFF, 0.92f * alpha);
    const bool toned = g.toned();
    g.set_tone(false);
    const Look was = g.look();
    Look full = was;
    full.saturation = 1;
    full.dark = 1;
    full.fill_alpha = 1;
    full.panel_style = 0;
    g.set_look(full);
    g.panel(gl_line_x_, cy + 23, gl_line_w_, 3, c, 1, 1.5f);
    g.set_look(was);
    g.set_tone(toned);
}

void App::draw_glass_prompts(const std::vector<std::pair<Glyph, std::string>> &items, float alpha, bool over_dialog)
{
    /* Bottom left, nothing else, small: the button and a word or two. */
    Gfx &g = *g_;
    if (alpha <= 0.01f || ((dialog_.open || acct_.open || share_.open || dns_.open || hub_.open || sys_.open ||
                            code_.open || screen_ == Screen::Achievements) &&
                           !over_dialog))
        return;
    /* (4.5: the size the other themes' hints are, as they are in every theme) */
    const float sc = (settings_->big_prompts ? 1.3f : 1.0f) * 1.12f;
    const float cy = 1014, gs = 29 * sc, size = ts(21) * sc;
    float x = 56;
    for (const auto &p : items)
    {
        if (int(p.first) >= int(kKeyL2R2))
        {
            /* Two keycaps, outlined. */
            const char *a = p.first == kKeyL1R1 ? "L1" : "L2", *b = p.first == kKeyL1R1 ? "R1" : "R2";
            for (int i = 0; i < 2; ++i)
            {
                g.glyph(i ? (b[1] == '1' ? Glyph::R1 : Glyph::R2) : (a[1] == '1' ? Glyph::L1 : Glyph::L2),
                        x + 20 * sc, cy, 30 * sc, ink(alpha));
                x += 44 * sc;
            }
            x += 6 * sc;
        }
        else if (p.first == Glyph::L1 || p.first == Glyph::R1 || p.first == Glyph::L2 || p.first == Glyph::R2)
        {
            /* A keycap: wider than a face button. */
            g.glyph(p.first, x + 20 * sc, cy, 30 * sc, ink(alpha));
            x += 44 * sc + 6 * sc;
        }
        else
        {
            g.glyph(p.first, x + gs * 0.5f, cy, gs, ink(alpha));
            x += gs + 10 * sc;
        }
        x += g.text_mid(Font::Bold, size, x, cy, ink(alpha), Align::Left, title_case(tr(p.second))) + 34 * sc;
    }
}

/* ---- the library ------------------------------------------------------------------------- */

void App::draw_glass_title(Game &game, float cy, float alpha)
{
    /* The chosen game, floating under the cases: its name, then its console's
     * mark and when it was last played, the time played on the right. */
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    const std::string title = game.db_title.empty() ? (game.title.empty() ? game.file : game.title) : game.db_title;
    const std::string shown = fit(g, Font::ExtraBold, ts(56), title, 1500);
    g.text_mid(Font::ExtraBold, ts(56), 960, cy, ink(alpha), Align::Center, shown);
    /* One line, centred together: the console's mark, when, and how long. */
    std::string line = relative_time(game.last_played, (long long)std::time(nullptr));
    if (game.play_seconds >= 60)
        line += "  \xC2\xB7  " + play_time_text(game.play_seconds);
    const float my = cy + 56, size = ts(25), ih = 24;
    const float icon_w = draw_platform(game, -4000, my, ih, 0.0f); /* measured */
    const float total = icon_w + 14 + g.measure(Font::SemiBold, size, line);
    const float x = 960 - total * 0.5f;
    draw_platform(game, x, my, ih, 0.9f * alpha);
    g.text_mid(Font::SemiBold, size, x + icon_w + 14, my, ink(0.82f * alpha), Align::Left, line);
}

namespace
{
/* A soft pool of colour, not toned and not muted: the room's light. */
void pool(Gfx &g, float cx, float cy, float w, float h, Color c)
{
    const bool toned = g.toned();
    g.set_tone(false);
    const Look was = g.look();
    Look full = was;
    full.saturation = 1;
    full.dark = 1;
    full.fill_alpha = 1;
    g.set_look(full);
    g.blob(cx, cy, w, h, c);
    g.set_look(was);
    g.set_tone(toned);
}
} // namespace

void App::draw_glass_flow(double time, float alpha)
{
    /* The cases in a row, the chosen one in front facing you, the others
     * turned toward it; their reflections on the floor,
     * and the room's light pooled behind the chosen one. */
    auto &games = lib_->games();
    const int shown = lib_->shown();
    constexpr float kSelW = 340, kSelH = kSelW * 190.0f / 135.0f, kSideW = 272, kSideH = kSideW * 190.0f / 135.0f;
    constexpr float kMidY = 470, kTurn = 1.047f; /* 60 degrees */
    constexpr float kRestYaw = 0.30f;            /* the chosen case, at rest: about 17 degrees */
    auto layout = [&](float k, float &x, float &w, float &h, float &yaw, float &lit) {
        const float a = std::fabs(k), s = k < 0 ? -1.0f : 1.0f;
        const float t = std::min(a, 1.0f);
        const float e = t * t * (3.0f - 2.0f * t);
        x = 960 + s * (a <= 1.0f ? a * 252.0f : 252.0f + (a - 1.0f) * 112.0f);
        w = lerp(kSelW, kSideW, e);
        h = lerp(kSelH, kSideH, e);
        /* The chosen one rests turned a little, its spine and its depth showing:
         * square on, it looked like a flat picture, not a case in your hand. */
        yaw = -s * kTurn * e + kRestYaw * (1.0f - e);
        /* The others lit nearly as the chosen one: all of them easy to see. */
        lit = a <= 1.0f ? lerp(1.0f, 0.95f, e) : std::max(0.86f, 0.95f - 0.015f * a);
    };
    /* L1 / R1: the row slides off the way you're going (and the new one
     * glides in from the other side: glass_switch_filter). */
    const float out = gl_fpend_ >= 0 ? clamp01(1.0f - gl_fout_) : 0.0f;
    const float shove = -float(gl_fdir_) * 420.0f * out * out;
    alpha *= (1.0f - 0.9f * out * out) * (1.0f - 0.55f * gl_fin_ * gl_fin_);
    pool(*g_, 960, 672, 760, 300,
         Color{gl_amb_[0], gl_amb_[1], gl_amb_[2],
               (settings_->ui_mode == 2 ? 0.12f : settings_->ui_mode == 1 ? 0.0f : 0.30f) * alpha});
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
        if (std::fabs(float(i) - scroll_) < 9.5f)
            order.push_back(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return std::fabs(float(a) - scroll_) > std::fabs(float(b) - scroll_); });
    box_white_wii_ = true;
    box_reflect_ = settings_->ui_mode == 2 ? 0.27f : 0.18f;
    for (int i : order)
    {
        const float k = float(i) - scroll_;
        float x, w, h, yaw, lit;
        layout(k, x, w, h, yaw, lit);
        const float a = std::fabs(k);
        const float fade = clamp01(8.5f - a) * clamp01((1960 - x) / 120.0f) * clamp01((x + 40) / 120.0f) * alpha;
        if (fade <= 0.01f)
            continue;
        const float sway =
            settings_->reduced_motion ? 0.0f : std::sin(float(time) * 0.7f) * 0.045f * (1.0f - std::min(a, 1.0f));
        box_bright_ = lit;
        if (gl_exit_ > 0)
        {
            /* Play: the chosen case is the launch screen's now (it flips over
             * into it); the others part, off to their sides. */
            if (i == selected_)
                continue;
            x += (k < 0 ? -1.0f : 1.0f) * gl_exit_ * (900.0f + 140.0f * std::min(a, 4.0f));
        }
        if (gl_part_ > 0)
        {
            /* Details: the chosen one is on its page; the others ease apart. */
            if (i == selected_)
                continue;
            x += (k < 0 ? -1.0f : 1.0f) * gl_part_ * 160.0f;
        }
        draw_box3d(games[std::size_t(i)], x + shove, kMidY, w, h, w * kCaseDepth, yaw + sway,
                   fade * (1.0f - 0.5f * gl_exit_), a < 6.5f);
        if (screen_ == Screen::Main && gl_exit_ <= 0 && gl_part_ <= 0 && fade > 0.5f)
            mp_hit(x + shove - w * 0.5f, kMidY - h * 0.5f, w, h, kMpGame + i); /* (the Move's pointer) */
    }
    box_bright_ = 1;
    box_reflect_ = 0;
    box_white_wii_ = false;
    if (shown > 0 && gl_part_ < 0.35f) /* (fading as the others part, or close in again) */
        draw_glass_title(games[std::size_t(selected_)], 850 + 90.0f * gl_exit_,
                         alpha * (1.0f - gl_exit_) * (1.0f - gl_part_ / 0.35f));
}

void App::draw_glass_rows(double time, float alpha)
{
    /* Rows of cases on the left; the chosen game's name, play and details on
     * the right. */
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    constexpr int cols = 5;
    const float cw = 196, ch = 276, gapx = 33, pitch = ch + 30;
    const float x0 = 120 + cw * 0.5f, y0 = 172 + ch * 0.5f + 12;
    std::vector<int> order;
    for (int i = 0; i < shown; ++i)
    {
        const float y = y0 + (float(i / cols) - row_scroll_) * pitch;
        if (y > -ch && y < 1080 + ch)
            order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return a != selected_ && b == selected_; });
    {
        const float out = gl_fpend_ >= 0 ? clamp01(1.0f - gl_fout_) : 0.0f;
        alpha *= (1.0f - 0.95f * out) * (1.0f - 0.8f * gl_fin_);
    }
    box_white_wii_ = true;
    for (int i : order)
    {
        const int r = i / cols, c = i % cols;
        const float x = x0 + float(c) * (cw + gapx);
        const float y = y0 + (float(r) - row_scroll_) * pitch;
        /* Rows fade out before the buttons at the bottom, and under the top. */
        const float fade = clamp01((960 - (y + ch * 0.5f)) / 170.0f + 0.15f) * clamp01((y + ch * 0.5f - 120) / 100.0f) *
                           alpha;
        if (fade <= 0.01f)
            continue;
        const bool on = i == selected_;
        const float grow = on ? 1.06f : 1.0f;
        const float sway = on && !settings_->reduced_motion ? std::sin(float(time) * 0.8f) * 0.05f : 0.0f;
        if (gl_exit_ > 0 || gl_part_ > 0)
        {
            /* Play: the chosen one flips into the launch screen; the rest
             * move away from it (Details: a little). */
            if (on)
                continue;
            const int sc = selected_ % cols, sr = selected_ / cols;
            const float vx = float(c - sc), vy = float(r - sr), len = std::max(0.5f, std::sqrt(vx * vx + vy * vy));
            const float push = gl_exit_ * 1300.0f + gl_part_ * 140.0f;
            const float px2 = x + vx / len * push, py2 = y + vy / len * push;
            box_bright_ = 0.95f;
            draw_box3d(games[std::size_t(i)], px2, py2, cw, ch, cw * kCaseDepth, 0, fade * (1.0f - 0.5f * gl_exit_),
                       false);
            continue;
        }
        if (on)
        {
            /* A ring of light round it. */
            const float rw = cw * grow + 18, rh = ch * grow + 18;
            g.panel(x - rw * 0.5f, y - 10 - rh * 0.5f, rw, rh, kClear, 1, 16, ink(0.9f * fade), 3.0f, 12);
        }
        box_bright_ = on ? 1.0f : 0.95f; /* (4.5: the others lit nearly as well) */
        draw_box3d(games[std::size_t(i)], x, y - (on ? 10.0f : 0.0f), cw * grow, ch * grow, cw * grow * kCaseDepth, sway, fade,
                   std::fabs(float(r) - row_scroll_) < 3.5f);
    }
    box_bright_ = 1;
    box_white_wii_ = false;
    if (shown == 0)
        return;
    /* The chosen game. */
    Game &sel = games[std::size_t(selected_)];
    const float px = 1330, pw = 500;
    float y = 214;
    draw_platform(sel, px, y, 22, alpha);
    y += 30;
    const std::string title = sel.db_title.empty() ? (sel.title.empty() ? sel.file : sel.title) : sel.db_title;
    for (const std::string &line : wrap(g, Font::ExtraBold, ts(56), title, pw, 3))
    {
        g.text_mid(Font::ExtraBold, ts(56), px, y + 30, ink(alpha), Align::Left, line);
        y += 58;
    }
    y += 26;
    {
        /* Play and Details, as Cross and Square. */
        const float bh = 60;
        const std::string play = title_case(tr("Play")), more = title_case(tr("Details"));
        const float pw1 = 30 + 26 + 10 + g.measure(Font::ExtraBold, ts(21), play) + 30;
        glass_pill(px, y, pw1, bh, bh * 0.5f, alpha, true);
        const bool toned = g.toned();
        g.set_tone(false);
        g.glyph(Glyph::Cross, px + 30 + 13, y + bh * 0.5f, 28, with_alpha(kInkDark, alpha));
        g.text_mid(Font::ExtraBold, ts(21), px + 30 + 26 + 10, y + bh * 0.5f, with_alpha(kInkDark, alpha), Align::Left,
                   play);
        g.set_tone(toned);
        const float pw2 = 24 + 26 + 10 + g.measure(Font::Bold, ts(19), more) + 24;
        glass_pill(px + pw1 + 12, y, pw2, bh, bh * 0.5f, alpha);
        g.glyph(Glyph::Square, px + pw1 + 12 + 24 + 13, y + bh * 0.5f, 28, ink(alpha));
        g.text_mid(Font::Bold, ts(19), px + pw1 + 12 + 24 + 26 + 10, y + bh * 0.5f, ink(alpha), Align::Left, more);
        y += bh + 34;
    }
    const std::pair<std::string, std::string> rows[2] = {
        {tr("Last Played"), short_ago(sel.last_played)},
        {tr("Play Time"), sel.play_seconds >= 60 ? play_time_text(sel.play_seconds) : tr("None yet")}};
    for (const auto &r : rows)
    {
        g.panel(px, y, pw, 1, ink(0.16f * alpha), 1, 0);
        g.text_mid(Font::SemiBold, ts(23), px, y + 27, ink(0.72f * alpha), Align::Left, r.first);
        g.text_mid(Font::Bold, ts(23), px + pw, y + 27, ink(alpha), Align::Right,
                   fit(g, Font::Bold, ts(23), r.second, pw * 0.6f));
        y += 52;
    }
    g.panel(px, y, pw, 1, ink(0.16f * alpha), 1, 0);
}

/* ---- a game's page ------------------------------------------------------------------------ */

namespace
{
/* The chosen row's glass: brighter, with a firmer edge. */
void select_glass(Gfx &g, int mode, float x, float y, float w, float h, float r, float a)
{
    const bool toned = g.toned();
    g.set_tone(false);
    if (g_skin == 1)
        g.panel(x, y, w, h, rgba(0x1F63F0, 0.85f * a), 1, r, rgba(0x7FD9FF, a), 2.0f);
    else if (g_skin == 2)
        g.panel(x, y, w, h, rgba(0xEAF7FD, a), 1, r, rgba(0x3DB8EC, a), 3.0f);
    else if (g_skin == 3)
        g.panel(x, y, w, h, rgba(0x4A50F0, 0.55f * a), 1, r, rgba(0xB4A8FF, a), 1.8f);
    else
    {
        g.set_presence(a); /* (fades as a whole: glass_pill) */
        if (mode == 2)
            g.panel(x, y, w, h, rgba(0xFFFFFF, 0.97f * a), 1, r, rgba(0x9AA6B6, 0.55f * a), 1.0f);
        else
            g.panel(x, y, w, h, rgba(0xFFFFFF, 0.21f * a), 1, r, rgba(0xFFFFFF, 0.52f * a), 1.0f);
        g.set_presence(1.0f);
    }
    g.set_tone(toned);
}
} // namespace

namespace
{
/* A game's achievements: the latest unlocked first, then the ones still to get. */
std::vector<const Achievement *> by_unlock(const AchievementSet &ach)
{
    std::vector<const Achievement *> list;
    for (const Achievement &x : ach.list)
        list.push_back(&x);
    std::stable_sort(list.begin(), list.end(), [](const Achievement *l, const Achievement *r) {
        if (l->unlocked != r->unlocked)
            return l->unlocked;
        return l->unlock_time > r->unlock_time;
    });
    return list;
}
} // namespace

void App::draw_glass_details(double time, float alpha)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    if (games.empty() || lib_->shown() == 0)
        return;
    Game &game = games[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
    back_of(game); /* ready for when the case turns round */
    const bool calm = settings_->reduced_motion;

    /* The case flies over to the left, turning once on the way (box_yaw_),
     * and turns with the right stick; its disc slides out behind it. */
    /* Closing (gl_back_): the same way back, easing in and out. */
    const bool backing = gl_back_ > 0 && screen_ == Screen::Main;
    const float p = backing ? 1.0f - smooth01(0.0f, 1.0f, 1.0f - gl_back_) : ease_out(1.0f - flip_anim_);
    const bool rows = settings_->glass_layout == 1;
    const float from_w = rows ? 196.0f : 340.0f, from_h = from_w * 190.0f / 135.0f;
    const float from_x = rows ? 1100.0f : 960.0f, from_y = rows ? 330.0f : 470.0f;
    const float to_w = 372, to_h = to_w * 190.0f / 135.0f, to_x = 412, to_y = 500;
    const bool swiping = swipe_anim_ > 0 && swipe_from_ >= 0 && swipe_from_ < int(games.size());
    const float st = swiping ? ease_out(1.0f - swipe_anim_) : 1.0f;
    const float cx = lerp(from_x, to_x, p) + (swiping ? float(swipe_dir_) * 560.0f * (1.0f - st) : 0.0f);
    const float cy = lerp(from_y, to_y, p);
    const float cw = lerp(from_w, to_w, p), chh = lerp(from_h, to_h, p);
    pool(g, cx + 40, cy + chh * 0.32f, 700, 300,
         Color{gl_amb_[0], gl_amb_[1], gl_amb_[2],
               (settings_->ui_mode == 2 ? 0.12f : settings_->ui_mode == 1 ? 0.0f : 0.28f) * alpha});
    box_white_wii_ = true;
    box_reflect_ = settings_->ui_mode == 2 ? 0.27f : 0.18f;
    if (swiping)
    {
        Game &old = games[std::size_t(swipe_from_)];
        draw_box3d(old, to_x - float(swipe_dir_) * 560.0f * st, to_y, to_w, to_h, to_w * kCaseDepth, -float(swipe_dir_) * 0.9f * st,
                   (1.0f - st) * alpha);
    }
    /* At rest it's turned a little, so you see it's a case. */
    if (gl_exit_ <= 0) /* (Play: it flips over into the launch screen) */
        draw_box3d(game, cx, cy, cw, chh, cw * kCaseDepth,
                   box_yaw_ + 0.16f * p + (rows ? 0.0f : 0.30f * (1.0f - p)) /* (from, and back to, the row's rest) */,
                   (swiping ? st : 1.0f) * alpha);
    box_reflect_ = 0;
    box_white_wii_ = false;

    /* What's known about it, coming in from the right after the case. */
    const float in = calm      ? 1.0f
                     : backing ? clamp01((gl_back_ - 0.55f) / 0.45f)
                               : ease_out(clamp01((1.0f - flip_anim_ - 0.20f) / 0.80f));
    const float a = in * alpha * (swiping ? 0.2f + 0.8f * st : 1.0f) * (1.0f - gl_exit_);
    const float px = 800 + (1.0f - in) * 90.0f + gl_exit_ * 260.0f, pw = 1060;
    /* Down from the buttons: the page slides up and the achievements' list
     * comes in under it (gl_ach_k_). */
    /* (It goes all the way up and the list follows it, a screen below: the
     * two never overlap - halfway, they ran into each other.) */
    const float sk = gl_ach_k_, off = -sk * 1080.0f, keep = 1.0f - 0.5f * sk;
    g.set_layer(0, off, keep);
    {
        /* The disc, top right, turning slowly. */
        /* A GameCube disc is the smaller one, as it is in your hand. */
        const float full = 236, d = game.platform == "Wii" ? full : full * 0.80f;
        const float spin = calm ? 0.0f : float(time) * 0.7f;
        const float dcx = 1860 - full * 0.5f + (1.0f - in) * 90.0f, dcy = 274; /* clear of the island */
        if (a > 0.01f)
            draw_disc(&game, dcx, dcy, d, spin, 0.0f, a, false);
    }
    float y = 196;
    {
        const float pw0 = draw_platform(game, px, y, 28, a);
        /* A favourite: a heart beside it (R1), with a little beat when it's set. */
        ctl_art(-1, 0, 0, 0, kClear);
        const float since = float(time_ - gl_heart_at_);
        const float beat = since >= 0 && since < 0.45f && !calm ? 1.0f + 0.35f * std::sin(since / 0.45f * kPi) : 1.0f;
        if (game.favourite && gl_ctl_)
            ctl_art(6, px + pw0 + 26, y, 30 * beat, rgba(0xFF4D6D, a));
        else if (since >= 0 && since < 0.3f && gl_ctl_)
            ctl_art(7, px + pw0 + 26, y, 30, ink(a * (1.0f - since / 0.3f)));
    }
    y += 26;
    const std::string title = game.db_title.empty() ? (game.title.empty() ? game.file : game.title) : game.db_title;
    for (const std::string &line : wrap(g, Font::ExtraBold, ts(72), title, pw - 290, 2))
    {
        g.text_mid(Font::ExtraBold, ts(72), px, y + 40, ink(a), Align::Left, line);
        y += 76;
    }
    {
        std::vector<std::string> bits;
        if (game.players > 0)
            bits.push_back(game.players == 1 ? tr("1 player")
                                             : trf("1 to {n} players", {{"n", std::to_string(game.players)}}));
        if (!game.region.empty())
            bits.push_back(tr(game.region));
        if (!game.id.empty())
            bits.push_back(game.id);
        std::string meta;
        for (const std::string &b : bits)
            meta += (meta.empty() ? "" : "  \xC2\xB7  ") + b;
        if (!meta.empty())
            g.text_mid(Font::SemiBold, ts(25), px, y + 18, ink(0.82f * a), Align::Left, meta);
        y += 54;
    }

    /* The buttons, in a row: left and right choose, Cross presses. */
    {
        const int count = details_shots_ > 0 ? 5 : 4;
        const std::string labels[5] = {title_case(tr("Play")), title_case(tr("Save States")),
                                       title_case(tr("Game Settings")), title_case(tr("Save Data")),
                                       title_case(tr("Screenshots"))};
        const float bh = 64, bgap = 12;
        float bx = px;
        for (int i = 0; i < count; ++i)
        {
            const bool on = details_row_ == i;
            const Font f = on ? Font::ExtraBold : Font::Bold;
            const float size = ts(i == 0 ? 24 : 22);
            const float pad = i == 0 ? 32.0f : 26.0f;
            const float icon = i == 0 ? 26.0f + 10.0f : 0.0f;
            const float w = pad * 2 + icon + g.measure(f, size, labels[i]);
            glass_pill(bx, y, w, bh, bh * 0.5f, a, on);
            const bool toned = g.toned();
            if (on)
                g.set_tone(false);
            const Color c = on ? with_alpha(kInkDark, a) : ink(a);
            if (i == 0)
                g.glyph(Glyph::Cross, bx + pad + 13, y + bh * 0.5f, 30, c);
            g.text_mid(f, size, bx + pad + icon, y + bh * 0.5f, c, Align::Left, labels[i]);
            g.set_tone(toned);
            mp_hit(bx, y, w, bh, kMpDetails + i);
            bx += w + bgap;
        }
        y += bh + 46;
    }

    /* Two columns: your play, and the game. */
    const float col = (pw - 48) * 0.5f;
    {
        std::vector<std::pair<std::string, std::string>> mine, about;
        mine.push_back({tr("Last Played"), short_ago(game.last_played)});
        mine.push_back({tr("Play Time"), game.play_seconds >= 60 ? play_time_text(game.play_seconds) : tr("None yet")});
        if (details_states_ > 0)
            mine.push_back({tr("Save States"), plural(details_states_, "1 saved", "{n} saved")});
        if (!game.developer.empty())
            about.push_back({tr("Developer"), game.developer});
        if (!game.publisher.empty() && game.publisher != game.developer)
            about.push_back({tr("Publisher"), game.publisher});
        if (!game.genre.empty())
            about.push_back({tr("Genre"), game.genre});
        if (!game.released.empty())
            about.push_back({tr("Released"), game.released});
        auto column = [&](float x, const char *heading, const std::vector<std::pair<std::string, std::string>> &list) {
            if (list.empty())
                return 0.0f;
            g.text_mid(Font::ExtraBold, ts(16), x, y, ink(0.62f * a), Align::Left, tr(heading), 1.5f);
            float ry = y + 18;
            for (const auto &r : list)
            {
                g.panel(x, ry, col, 1, ink(0.16f * a), 1, 0);
                g.text_mid(Font::SemiBold, ts(23), x, ry + 29, ink(0.72f * a), Align::Left, r.first);
                g.text_mid(Font::Bold, ts(23), x + col, ry + 29, ink(a), Align::Right,
                           fit(g, Font::Bold, ts(23), r.second, col * 0.62f));
                ry += 58;
            }
            g.panel(x, ry, col, 1, ink(0.16f * a), 1, 0);
            return ry - y;
        };
        const float h1 = column(px, "YOUR PLAY", mine);
        const float h2 = column(px + col + 48, "ABOUT THE GAME", about);
        y += std::max(h1, h2) + 46;
    }

    /* What it's about, a few lines, in the room left above the achievements. */
    const AchievementSet &ach = details_achievements(game);
    if (!game.synopsis.empty())
    {
        const float stop = ach.valid() ? 852.0f : 960.0f;
        const int room = std::min(3, int((stop - y) / 31.0f));
        if (room > 0)
        {
            for (const std::string &l : wrap(g, Font::SemiBold, ts(22), game.synopsis, pw, std::size_t(room)))
            {
                g.text_mid(Font::SemiBold, ts(22), px, y + 4, ink(0.70f * a), Align::Left, l);
                y += 33;
            }
            y += 30;
        }
    }
    /* Achievements, when the game has them: how far along, and the latest badges. */
    if (ach.valid() && y < 900)
    {
        g.text_mid(Font::ExtraBold, ts(14), px, y, ink(0.62f * a), Align::Left, tr("ACHIEVEMENTS"), 1.5f);
        const std::string count = trf("{done} of {total}", {{"done", std::to_string(ach.unlocked)},
                                                            {"total", std::to_string(ach.total)}}) +
                                  "  \xC2\xB7  " +
                                  trf("{n} of {all} points", {{"n", std::to_string(ach.points)},
                                                              {"all", std::to_string(ach.points_total)}});
        g.text_mid(Font::Bold, ts(18), px + pw, y, ink(0.85f * a), Align::Right, count);
        const float bar_y = y + 18;
        g.panel(px, bar_y, pw, 6, ink(0.16f * a), 1, 3);
        const float done = ach.total > 0 ? float(ach.unlocked) / float(ach.total) : 0.0f;
        if (done > 0)
            g.panel(px, bar_y, std::max(6.0f, pw * done), 6, ink(0.92f * a), 1, 3);
        /* The badges: the latest unlocked first, then the ones still to get. */
        const std::vector<const Achievement *> list = by_unlock(ach);
        const float bs = 58, bg = 12;
        const int fits = int((pw + bg) / (bs + bg));
        float bx = px;
        g.set_layer(0, off, a * keep);
        for (int i = 0; i < std::min(fits, int(list.size())); ++i)
        {
            const Achievement &x = *list[std::size_t(i)];
            draw_badge(x.unlocked || x.badge_locked_url.empty() ? x.badge_url : x.badge_locked_url, x.unlocked, bx,
                       bar_y + 24, bs);
            bx += bs + bg;
        }
        g.set_layer(0, off, keep);
        if (sk < 0.5f && !list.empty())
        {
            /* More below: a small arrow under the badges, nodding down. */
            const float bob = calm ? 0.0f : 3.0f * std::sin(float(time) * 2.6f);
            g.glyph(Glyph::Arrow, px + pw * 0.5f, bar_y + 24 + bs + 24 + bob, 15, ink(0.50f * a * (1.0f - 2.0f * sk)),
                    kPi);
        }
    }
    g.set_layer();

    /* The list, when the page has slid up. */
    if (sk > 0.01f && ach.valid())
    {
        const float list_dy = (1.0f - sk) * 1080.0f;
        g.set_layer(0, list_dy, std::min(1.0f, sk * 1.6f) * alpha);
        const float lx = 800, lw = 1060, top = 170;
        g.text_mid(Font::ExtraBold, ts(16), lx, top, ink(0.62f), Align::Left, tr("ACHIEVEMENTS"), 1.5f);
        g.text_mid(Font::Bold, ts(19), lx + lw, top,
                   ink(0.85f), Align::Right,
                   trf("{done} of {total}", {{"done", std::to_string(ach.unlocked)}, {"total", std::to_string(ach.total)}}) +
                       "  \xC2\xB7  " +
                       trf("{n} of {all} points",
                           {{"n", std::to_string(ach.points)}, {"all", std::to_string(ach.points_total)}}));
        g.panel(lx, top + 20, lw, 6, ink(0.16f), 1, 3);
        if (ach.total > 0 && ach.unlocked > 0)
            g.panel(lx, top + 20, std::max(6.0f, lw * float(ach.unlocked) / float(ach.total)), 6, ink(0.92f), 1, 3);
        const std::vector<const Achievement *> list = by_unlock(ach);
        const int n = int(list.size());
        const float rtop = top + 56, pitch = 86;
        const int visible = int((1000 - rtop) / pitch);
        const float first = gl_ach_first_;
        const int mode = std::clamp(settings_->ui_mode, 0, 2);
        for (int i = std::max(0, int(first) - 1); i < n && float(i) < first + float(visible) + 1; ++i)
        {
            const float ry = rtop + (float(i) - first) * pitch;
            const float fa = clamp01(std::min((ry + 78 - rtop) / 40.0f, (rtop + float(visible) * pitch - ry) / 40.0f)) *
                             clamp01((990.0f - (ry + list_dy + 78.0f)) / 50.0f); /* (clear of the buttons as it rises) */
            if (fa <= 0.01f)
                continue;
            const Achievement &x = *list[std::size_t(i)];
            const bool on = i == gl_ach_row_;
            if (on)
                select_glass(g, mode, lx - 18, ry, lw + 36, 78, 22, fa);
            g.set_layer(0, list_dy, std::min(1.0f, sk * 1.6f) * alpha * fa);
            draw_badge(x.unlocked || x.badge_locked_url.empty() ? x.badge_url : x.badge_locked_url, x.unlocked, lx,
                       ry + 7, 64);
            g.set_layer(0, list_dy, std::min(1.0f, sk * 1.6f) * alpha);
            const std::string pts = trf("{n} points", {{"n", std::to_string(x.points)}});
            const float pw2 = g.measure(Font::Bold, ts(19), pts);
            g.text_mid(Font::Bold, ts(22), lx + 84, ry + 27, ink(fa), Align::Left,
                       fit(g, Font::Bold, ts(22), x.title, lw - 84 - pw2 - 40));
            g.text_mid(Font::Regular, ts(18), lx + 84, ry + 55, ink(0.70f * fa), Align::Left,
                       fit(g, Font::Regular, ts(18), x.description, lw - 84 - 200));
            g.text_mid(Font::Bold, ts(19), lx + lw, ry + 27, ink(0.85f * fa), Align::Right, pts);
            g.text_mid(Font::SemiBold, ts(17), lx + lw, ry + 55, ink((x.unlocked ? 0.70f : 0.45f) * fa), Align::Right,
                       x.unlocked ? short_ago(x.unlock_time) : (x.progress.empty() ? tr("Locked") : x.progress));
        }
        g.set_layer();
    }
}

/* ---- the menu ---------------------------------------------------------------------------- */


std::string App::glass_row_value(const SettingRow &r, int *index) const
{
    std::string value;
    int vi = 0;
    const int count = int(r.values.size());
    if (r.bool_value)
        vi = *r.bool_value ? 1 : 0;
    else if (r.int_value && !r.order.empty())
        vi = int(std::find(r.order.begin(), r.order.end(), *r.int_value) - r.order.begin());
    else if (r.int_value)
        vi = *r.int_value - r.min;
    if (vi >= 0 && vi < count)
        value = r.values[std::size_t(vi)];
    if (r.key == "players")
    {
        const int n = porpoise::pad::connected_count();
        value = n <= 1 ? tr("1 player") : trf("{n} players", {{"n", std::to_string(n)}});
    }
    if (r.action == kRowUpdate)
        value = update_row_value();
    if (index)
        *index = vi;
    return value;
}

std::string App::glass_section_value(const std::string &section) const
{
    /* What a section is set to, in a word or two, for the menu. */
    struct Pick
    {
        const char *section, *a, *b;
    };
    static const Pick kPicks[] = {
        {"Video and Audio", "output_res", "resolution"},
        {"Video and Graphics", "wide", "resolution"},
        {"Graphics", "shader_mode", nullptr},
        {"Audio", "volume", nullptr},
        {"Controls", "button_layout", "wii_controller"},
        {"Saves and Screenshots", "quick_resume", nullptr},
        {"Console and Online", "language", nullptr},
        {"Interface", "ui_mode", nullptr},
    };
    if (section == "About")
        return kVersion;
    if (section == "Games")
        return settings_->folders.empty()
                   ? std::string()
                   : plural((long long)settings_->folders.size(), "1 folder", "{n} folders");
    for (const Pick &p : kPicks)
    {
        if (section != p.section)
            continue;
        std::string out;
        for (const char *key : {p.a, p.b})
        {
            if (!key)
                continue;
            for (const SettingRow &r : rows_)
                if (r.key == key && r.section == section)
                {
                    const std::string v = glass_row_value(r);
                    if (!v.empty())
                        out += (out.empty() ? "" : " \xC2\xB7 ") + v;
                    break;
                }
        }
        return out;
    }
    return "";
}

void App::draw_glass_menu(float alpha)
{
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    const int mode = std::clamp(settings_->ui_mode, 0, 2);
    const float x = gl_slab_[0], y = gl_slab_[1], w = kMenuW, h = kMenuH;
    const float l = x + 42, r = x + w - 42;
    std::vector<GlassItem> items = glass_items();
    const int n = int(items.size());
    gl_row_ = std::clamp(gl_row_, 0, std::max(0, n - 1));

    g.text_mid(Font::ExtraBold, ts(34), l, y + 72, ink(alpha), Align::Left, tr("Menu"));
    {
        const std::string close = title_case(tr("Close"));
        const float cw = g.text_mid(Font::SemiBold, ts(18), r, y + 72, ink(0.82f * alpha), Align::Right, close);
        g.glyph(Glyph::Triangle, r - cw - 20, y + 72, 30, ink(0.82f * alpha));
    }

    /* What's connected, and the camera. */
    const porpoise::move::Status ms = porpoise::move::status();
    bool move = ms.connected, camera = ms.camera, ball = ms.ball;
    int pads = 0;
    for (int i = 0; i < 4; ++i)
        pads += porpoise::pad::connected(i) ? 1 : 0;
    if (preview_kit())
    {
        move = camera = ball = true;
        pads = 1;
    }
    const float cy0 = y + 122, ch = 100, cw = (r - l - 14) * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        const float cx = l + float(c) * (cw + 14);
        glass_pill(cx, cy0, cw, ch, 18, 0.8f * alpha);
        g.text_mid(Font::ExtraBold, ts(13), cx + 20, cy0 + 28, ink(0.62f * alpha), Align::Left,
                   c == 0 ? tr("CONTROLLERS") : tr("CAMERA"), 1.5f);
        const float my = cy0 + 66;
        float mx = cx + 20;
        if (c == 0)
        {
            const int lights[4] = {settings_->light_1, settings_->light_2, settings_->light_3, settings_->light_4};
            static const std::uint32_t kLights[10] = {0x2F6FFF, 0xFF3A48, 0x10C860, 0xFF4CC0, 0x8C4CFF,
                                                      0xFF8A1A, 0xFFC81A, 0x1AC8FF, 0xFFFFFF, 0x50546A};
            for (int i = 0; i < std::max(1, pads) && i < 4; ++i)
            {
                g.panel(mx, my - 4, 8, 8, rgba(kLights[std::clamp(lights[i], 0, 9)], alpha), 1, 4);
                mx += 13;
            }
            mx += 4;
            ctl_art(-1, 0, 0, 0, kClear);
            /* The DualSense first, then the Move. */
            if (gl_ctl_)
                ctl_art(0, mx + 17, my, 34, ink(alpha));
            else
                ctl_icon(1, mx + 16, my, 28, ink(alpha), rgba(ball_hex(), alpha));
            mx += 38;
            if (move)
            {
                if (gl_ctl_)
                {
                    ctl_art(2, mx + 8, my, 30, ink(alpha));
                    draw_move_ball(mx + 8, my, 30, alpha);
                }
                else
                    ctl_icon(0, mx + 8, my, 24, ink(alpha), rgba(ball_hex(), alpha));
                mx += 24;
            }
            mx += 4;
            const int total = pads + (move ? 1 : 0);
            g.text_mid(Font::Bold, ts(17), mx, my, ink(alpha), Align::Left,
                       fit(g, Font::Bold, ts(17), trf("{n} connected", {{"n", std::to_string(std::max(1, total))}}),
                           cx + cw - 14 - mx));
        }
        else
        {
            camera_icon(mx + 13, my, 26, ink(alpha));
            mx += 36;
            if (camera)
            {
                /* Working: a green dot says so (the docs say what it means). */
                mx += g.text_mid(Font::Bold, ts(17), mx, my, ink(alpha), Align::Left, tr("PS3 Eye")) + 12;
                g.panel(mx, my - 4, 8, 8, with_alpha(kGood, alpha), 1, 4);
            }
            else
            {
                const int st = preview_kit() ? 4 : ms.camera_state;
                const char *what = st == 0   ? "Off in Settings"
                                   : st == 1 ? "Needs the ELF loader"
                                   : st == 2 ? "Starting"
                                             : "Not plugged in";
                g.text_mid(Font::SemiBold, ts(17), mx, my, ink(0.62f * alpha), Align::Left,
                           fit(g, Font::SemiBold, ts(17), tr(what), cx + cw - 12 - mx));
            }
        }
    }

    /* The rows: this screen's, then Settings', scrolling to keep the chosen one in view. */
    const float top = cy0 + ch + 26, pitch = 54, item_h = 50;
    const float bottom = y + h - 104;
    const int fits = std::max(3, int((bottom - top) / pitch));
    float first = gl_first_to_;
    if (gl_row_ < first + 1)
        first = float(gl_row_ - 1);
    if (gl_row_ > first + fits - 2)
        first = float(gl_row_ - fits + 2);
    first = std::clamp(first, 0.0f, float(std::max(0, n - fits)));
    gl_first_to_ = first; /* it glides there (glass_tick) */
    if (settings_->reduced_motion || gl_snap_ || std::fabs(gl_menu_first_ - first) > 30)
        gl_menu_first_ = first;
    auto fade_at = [&](float iy) {
        return clamp01(std::min((iy + item_h - top) / (pitch * 0.6f), (top + float(fits) * pitch - iy) / (pitch * 0.6f)));
    };
    gl_hi_to_ = float(gl_row_);
    if (gl_hi_ < 0 || settings_->reduced_motion || gl_snap_ || std::fabs(gl_hi_ - float(gl_row_)) > 12)
        gl_hi_ = float(gl_row_);
    {
        const float hy = top + (gl_hi_ - gl_menu_first_) * pitch;
        const float fa = fade_at(hy) * alpha;
        if (fa > 0.01f)
            select_glass(g, mode, x + 28, hy, w - 56, item_h, item_h * 0.5f, fa);
    }
    for (int i = 0; i < n; ++i)
    {
        const GlassItem &it = items[std::size_t(i)];
        const float iy = top + (float(i) - gl_menu_first_) * pitch;
        const float fa = fade_at(iy) * alpha;
        if (fa <= 0.01f)
            continue;
        const float cy = iy + item_h * 0.5f;
        if (it.kind == 4)
        {
            std::string head = it.label;
            for (char &c : head)
                c = char(std::toupper((unsigned char)c));
            g.text_mid(Font::ExtraBold, ts(15), l + 4, cy + 8, ink(0.60f * fa), Align::Left, head, 1.5f);
            continue;
        }
        const bool on = i == gl_row_;
        const bool dark_text = on && mode == 2;
        (void)dark_text;
        const Font f = on ? Font::Bold : Font::SemiBold;
        const bool arrows = (it.kind == 1 || it.kind == 6) && on;
        float vr = r - 10 - (arrows ? 20.0f : 0.0f);
        float vw = 0;
        if (!it.value.empty())
            vw = g.text_mid(Font::Bold, ts(21), vr, cy, ink((on ? 0.9f : 0.70f) * fa), Align::Right,
                            fit(g, Font::Bold, ts(21), it.value, 250)) +
                 18;
        if (arrows)
        {
            /* Left and right change it. */
            g.glyph(Glyph::Arrow, r - 12, cy, 12, ink(0.8f * fa), kPi * 0.5f);
            g.glyph(Glyph::Arrow, vr - vw + 2, cy, 12, ink(0.8f * fa), -kPi * 0.5f);
            vw += 18;
        }
        g.text_mid(f, ts(23), l + 10, cy, ink(fa), Align::Left, fit(g, f, ts(23), it.label, vr - vw - l - 20));
    }

    /* Its own buttons, along the bottom. */
    g.panel(l, y + h - 88, r - l, 1, ink(0.16f * alpha), 1, 0);
    float px = l;
    for (const auto &p : {std::pair<Glyph, const char *>{Glyph::Cross, "Open"}, {Glyph::Circle, "Back"}})
    {
        g.glyph(p.first, px + 16, y + h - 46, 32, ink(0.9f * alpha));
        px += 40;
        px += g.text_mid(Font::Bold, ts(21), px, y + h - 46, ink(0.9f * alpha), Align::Left,
                         title_case(tr(p.second))) +
              28;
    }
}

/* ---- Settings, grown out of the menu ------------------------------------------------------ */

void App::draw_glass_settings(float alpha)
{
    Gfx &g = *g_;
    if (rows_.empty() || alpha <= 0.01f)
        return;
    const int mode = std::clamp(settings_->ui_mode, 0, 2);
    const bool game = screen_ == Screen::GameSettings;
    settle_settings_row();
    const std::string current = on_rail_ ? section_name(rail_) : rows_[std::size_t(settings_row_)].section;
    const float glide_dt = float(std::clamp(time_ - glide_time_, 0.0, 0.1));
    glide_time_ = time_;
    auto glide = [&](float &at, float target, float jump) {
        if (at < 0 || settings_->reduced_motion || std::fabs(target - at) > jump)
            at = target;
        else
            at += (target - at) * std::min(1.0f, glide_dt * 16.0f);
        return at;
    };

    /* The heading, top left. */
    {
        const std::string heading = game && game_for_ ? (game_for_->db_title.empty() ? game_for_->title
                                                                                     : game_for_->db_title)
                                                      : tr("Settings");
        const float hw = g.text_mid(Font::ExtraBold, ts(34), 72, 90, ink(alpha), Align::Left,
                                    fit(g, Font::ExtraBold, ts(34), heading, 900));
        if (game)
            g.text_mid(Font::Bold, ts(18), 72 + hw + 18, 92, ink(0.62f * alpha), Align::Left, tr("Game Settings"));
    }

    /* The sections, down the left. */
    const float nx = 68, ny = 148, nw = 360, pitch = 58, item_h = 54;
    int sections = 0, here = 0;
    for (const SettingRow &r : rows_)
        if (r.header)
        {
            if (r.section == current)
                here = sections;
            ++sections;
        }
    const float nb = 1000;
    const int fits = std::max(1, int((nb - ny) / pitch));
    float first_t = 0;
    if (sections > fits)
    {
        float f = rail_scroll_ < 0 ? 0.0f : std::round(rail_scroll_);
        if (here < f + 1)
            f = float(here - 1);
        else if (here > f + float(fits) - 2)
            f = float(here - fits + 2);
        first_t = std::clamp(f, 0.0f, float(sections - fits));
    }
    const float nfirst = glide(rail_scroll_, first_t, 1000);
    auto nav_fade = [&](float iy) {
        return clamp01(std::min((iy + item_h - ny) / (pitch * 0.5f), (ny + float(fits) * pitch - iy) / (pitch * 0.5f)));
    };
    {
        const float hy = glide(rail_glide_, ny + (float(here) - nfirst) * pitch, 2000);
        const float fa = nav_fade(hy) * alpha;
        if (fa > 0.01f)
        {
            if (on_rail_)
                select_glass(g, mode, nx, hy, nw, item_h, item_h * 0.5f, fa);
            else
                glass_pill(nx, hy, nw, item_h, item_h * 0.5f, fa);
        }
    }
    {
        int k = 0;
        for (const SettingRow &r : rows_)
        {
            if (!r.header)
                continue;
            const float iy = ny + (float(k++) - nfirst) * pitch;
            const float fa = nav_fade(iy) * alpha;
            if (fa <= 0.01f)
                continue;
            const bool on = r.section == current;
            g.text_mid(on ? Font::Bold : Font::SemiBold, ts(23), nx + 24, iy + item_h * 0.5f,
                       ink((on ? 1.0f : 0.78f) * fa), Align::Left,
                       fit(g, Font::Bold, ts(23), tr(r.section), nw - 48));
        }
    }

    /* The section: its name, what it's for, and its rows. */
    const float px = 488, py = 148, pw = 1340;
    const std::vector<std::string> pending = pending_keys();
    g.text_mid(Font::ExtraBold, ts(40), px + 22, py + 26, ink(alpha), Align::Left, tr(current));
    {
        std::string scope = game ? tr("For this game only") : tr("Changes apply the next time a game starts");
        if (current == "About")
            scope = tr("created by @elripalda") + " \xC2\xB7 ripalda.dev";
        else if (current == "Games")
            scope = tr("Your games, their covers, and where Porpoise keeps things");
        else if (current == "Interface")
            scope = tr("How Porpoise looks and reads");
        else if (current == "Accessibility")
            scope = tr("Easier to see, read and follow");
        Color sc = ink(0.62f * alpha);
        if (!pending.empty())
        {
            scope = plural((long long)pending.size(), "1 change waiting \xC2\xB7 Square applies it",
                           "{n} changes waiting \xC2\xB7 Square applies them");
            sc = with_alpha(rgba(0xFFC857), alpha);
        }
        g.text_mid(Font::SemiBold, ts(20), px + pw - 22, py + 30, sc, Align::Right, scope);
    }
    std::vector<int> section_rows;
    for (int i = 0; i < int(rows_.size()); ++i)
        if (!rows_[std::size_t(i)].header && rows_[std::size_t(i)].section == current)
            section_rows.push_back(i);
    const float ry0 = py + 78, row_h = 68, rpitch = 76, sub_pitch = 50;
    const float room = 8 * rpitch; /* down to the help */
    if (current == "Cheats and Patches" && !cheats_.empty())
    {
        g.set_layer(layer_dx_, 0, alpha);
        draw_cheat_cards(section_rows, px, py - 10, pw, 820);
        g.set_layer(layer_dx_, 0, 1);
    }
    else
    {
        /* Where each row sits down the tab: a part's heading takes less room. */
        std::vector<float> at(section_rows.size());
        float total = 0;
        for (std::size_t k = 0; k < section_rows.size(); ++k)
        {
            const SettingRow &r = rows_[std::size_t(section_rows[k])];
            if (r.subhead && k)
                total += 12; /* a little air before a new part */
            at[k] = total;
            total += r.subhead ? sub_pitch : rpitch;
        }
        /* Scrolled to keep the chosen row in view, with its part's heading
         * when there's room; gliding there. */
        float want = on_rail_ || current != set_scroll_tab_ ? 0.0f : set_scroll_to_;
        for (std::size_t k = 0; k < section_rows.size(); ++k)
            if (!on_rail_ && section_rows[k] == settings_row_)
            {
                float top = at[k];
                if (k > 0 && rows_[std::size_t(section_rows[k - 1])].subhead)
                    top = at[k - 1] - 4;
                if (top < want)
                    want = top;
                if (at[k] + rpitch > want + room)
                    want = at[k] + rpitch - room;
            }
        want = std::clamp(want, 0.0f, std::max(0.0f, total - room));
        set_scroll_to_ = want;
        if (current != set_scroll_tab_)
            set_scroll_ = want;
        set_scroll_tab_ = current;
        const float scroll = glide(set_scroll_, want, 4000);
        auto fade_at = [&](float iy) {
            /* Rows leaving the tab's room fade before they reach its title or the help. */
            return clamp01(std::min((iy - ry0 + 26) / 26.0f, (ry0 + room - iy - row_h + 26) / 26.0f));
        };
        if (on_rail_)
            row_glide_ = -1;
        for (std::size_t k = 0; k < section_rows.size(); ++k)
        {
            const SettingRow &r = rows_[std::size_t(section_rows[k])];
            const float iy = ry0 + at[k] - scroll;
            const float fa = fade_at(iy) * alpha;
            if (fa <= 0.01f || r.subhead)
                continue;
            const bool on = !on_rail_ && section_rows[k] == settings_row_;
            const float ix = px + (r.indent ? 36.0f : 0.0f), iw = pw - (r.indent ? 36.0f : 0.0f);
            if (on)
            {
                const float hy = glide(row_glide_, iy, rpitch * 3.5f);
                select_glass(g, mode, ix, hy, iw, row_h, row_h * 0.5f, fa);
            }
            else
            {
                /* The others: a whisper of glass, no shadow. */
                const bool toned = g.toned();
                g.set_tone(false);
                g.set_solid(true);
                g.panel(ix, iy, iw, row_h, mode == 2 ? rgba(0xFFFFFF, 0.42f * fa) : rgba(0xFFFFFF, 0.04f * fa), 1,
                        row_h * 0.5f, mode == 2 ? rgba(0xFFFFFF, 0.8f * fa) : rgba(0xFFFFFF, 0.09f * fa), 1.0f);
                g.set_solid(false);
                g.set_tone(toned);
            }
        }
        for (std::size_t k = 0; k < section_rows.size(); ++k)
        {
            const int i = section_rows[k];
            const SettingRow &r = rows_[std::size_t(i)];
            const float iy = ry0 + at[k] - scroll;
            const float alpha_row = fade_at(iy) * alpha;
            if (alpha_row <= 0.01f)
                continue;
            const float alpha = alpha_row; /* (this row's, faded at the edges) */
            const float cy = iy + row_h * 0.5f;
            if (r.subhead)
            {
                std::string head = r.label;
                for (char &c : head)
                    c = char(std::toupper((unsigned char)c));
                g.text_mid(Font::ExtraBold, ts(16), px + 24, iy + 28, ink(0.62f * alpha), Align::Left, head, 1.5f);
                continue;
            }
            const bool on = !on_rail_ && i == settings_row_;
            const float ox = r.indent ? 36.0f : 0.0f;
            const bool own = game && !r.key.empty() &&
                             std::find(game_keys_.begin(), game_keys_.end(), r.key) != game_keys_.end();
            const bool waiting = row_pending(r, pending);
            /* On Light the chosen row is white glass: its words stay ink (toned). */
            const Color text = ink((on ? 1.0f : 0.88f) * alpha);
            float lx = px + ox + 28;
            if (waiting)
            {
                g.panel(px + ox + 12, cy - 4, 8, 8, with_alpha(rgba(0xFFC857), alpha), 1, 4);
                lx += 6;
            }
            const bool bug = r.action == kRowSendReport;
            const Font lf = on || bug ? Font::Bold : Font::SemiBold;
            const float lsize = ts(r.indent ? 22 : 24);
            const std::string label = fit(g, lf, lsize, r.label, pw - 540);
            const float lw = g.text_mid(lf, lsize, lx, cy, text, Align::Left, label);
            if (r.beta)
            {
                const char *badge = r.alpha ? "ALPHA" : "BETA";
                const float bw = g.measure(Font::ExtraBold, 12, badge, 1.2f) + 16;
                g.panel(lx + lw + 12, cy - 11, bw, 22, with_alpha(rgba(r.alpha ? 0xFF7A8A : 0xFFB347), 0.9f * alpha),
                        1, 11);
                const bool toned = g.toned();
                g.set_tone(false);
                g.text_mid(Font::ExtraBold, 12, lx + lw + 12 + bw * 0.5f, cy, rgba(0x2A1600, alpha), Align::Center,
                           badge, 1.2f);
                g.set_tone(toned);
            }
            int vi = 0;
            const std::string value = glass_row_value(r, &vi);
            const float right = px + pw - 26;
            const Color vc = waiting ? with_alpha(rgba(0xFFC857), alpha)
                             : own   ? with_alpha(rgba(0x7FD4FF), alpha)
                                     : ink((on ? 0.95f : 0.80f) * alpha);
            if (bug)
            {
                /* Report a Bug: a button of its own, easy to find. */
                const float vw = g.measure(Font::Bold, ts(22), value);
                const float bw = vw + 84, bh = 50, bx = right - bw + 8;
                const bool toned = g.toned();
                g.set_tone(false);
                g.panel(bx, cy - bh * 0.5f, bw, bh, rgba(0x2F7BFF, (on ? 0.95f : 0.75f) * alpha), 1, bh * 0.5f,
                        rgba(0xBFD8FF, (on ? 0.9f : 0.5f) * alpha), 1.4f);
                g.glyph(Glyph::Cross, bx + 30, cy, 26, rgba(0xFFFFFF, alpha));
                g.text_mid(Font::Bold, ts(22), bx + 52, cy, rgba(0xFFFFFF, alpha), Align::Left, value);
                g.set_tone(toned);
            }
            else if (r.action == kRowCredits)
            {
                /* Opens and folds away: a chevron that turns. */
                g.text_mid(Font::Bold, ts(22), right - 30, cy, vc, Align::Right, value);
                g.glyph(Glyph::Arrow, right - 8, cy, 13, ink(0.75f * alpha), credits_open_ ? 0.0f : kPi);
            }
            else if (r.toggle >= 0)
            {
                /* A switch. */
                const bool lit = r.toggle == 1;
                const float tw = 62, th = 34, tx = right - tw;
                const bool toned = g.toned();
                g.set_tone(false);
                g.panel(tx, cy - th * 0.5f, tw, th,
                        lit ? rgba(0x34C77B, alpha) : (mode == 2 ? rgba(0x9AA6B6, 0.45f * alpha) : rgba(0xFFFFFF, 0.16f * alpha)),
                        1, th * 0.5f, mode == 2 ? rgba(0x9AA6B6, 0.4f * alpha) : rgba(0xFFFFFF, 0.28f * alpha), 1.0f);
                const float kx = lit ? tx + tw - th * 0.5f : tx + th * 0.5f;
                g.panel(kx - 13, cy - 13, 26, 26, rgba(0xFFFFFF, alpha), 1, 13);
                g.set_tone(toned);
                if (!r.tag.empty())
                    g.text_mid(Font::SemiBold, ts(16), tx - 16, cy, ink(0.62f * alpha), Align::Right, r.tag);
            }
            else if (r.action)
            {
                const bool danger = r.action == kRowResetAll || r.action == kRowResetGame || r.action == kRowReinitialize;
                const float aw = g.text_mid(Font::Bold, ts(22), right - 26, cy,
                                            danger ? with_alpha(rgba(0xFF8A9E), alpha) : vc, Align::Right, value);
                (void)aw;
                g.glyph(Glyph::Arrow, right - 6, cy, 13, ink(0.7f * alpha), kPi * 0.5f);
            }
            else if (!r.bool_value && !r.int_value)
                g.text_mid(Font::SemiBold, ts(22), right, cy, ink(0.70f * alpha), Align::Right,
                           fit(g, Font::SemiBold, ts(22), value, pw * 0.45f));
            else
            {
                const int count = int(r.values.size());
                const float vw = g.text_mid(Font::Bold, ts(23), right - (on ? 24 : 0), cy, vc, Align::Right, value);
                if (on)
                {
                    const bool at_min = vi <= 0, at_max = vi >= count - 1;
                    g.glyph(Glyph::Arrow, right - 4, cy, 13, ink((at_max ? 0.25f : 0.85f) * alpha), kPi * 0.5f);
                    g.glyph(Glyph::Arrow, right - 24 - vw - 18, cy, 13, ink((at_min ? 0.25f : 0.85f) * alpha),
                            -kPi * 0.5f);
                }
            }
        }
        if (scroll > 1)
            g.glyph(Glyph::Arrow, px + pw * 0.5f, ry0 - 12, 14, ink(0.6f * alpha), 0);
        if (scroll + room < total - 1)
            g.glyph(Glyph::Arrow, px + pw * 0.5f, ry0 + room - 2, 14, ink(0.6f * alpha), kPi);
    }

    /* What the chosen row does, in its own glass at the bottom. */
    std::string help = on_rail_ ? tr("Choose a section with up and down, then press Right or Cross to go into it.")
                                : rows_[std::size_t(settings_row_)].help;
    if (!on_rail_ &&
        (rows_[std::size_t(settings_row_)].action == kRowUpdate || rows_[std::size_t(settings_row_)].key == "creator") &&
        !update_note_.empty() && time_ - update_note_time_ < 8.0)
        help = update_note_;
    if (!applied_note_.empty() && time_ - applied_note_time_ < 4.0)
        help = applied_note_;
    const bool qr = !on_rail_ && (rows_[std::size_t(settings_row_)].label == tr("Report a Bug") ||
                                  rows_[std::size_t(settings_row_)].key == "discord");
    const float hy = 852, hh = 104;
    glass_pill(px, hy, pw, hh, 22, 0.75f * alpha);
    {
        const float tw = pw - 52 - (qr ? 130 : 0);
        const auto lines = wrap(g, Font::SemiBold, ts(22), help, tw, 2);
        float ty = hy + hh * 0.5f - float(lines.size() - 1) * 16.0f;
        for (const std::string &line : lines)
        {
            g.text_mid(Font::SemiBold, ts(22), px + 26, ty, ink(0.85f * alpha), Align::Left, line);
            ty += 32;
        }
    }
    if (qr)
    {
        if (!qr_tried_)
        {
            qr_tried_ = true;
            qr_ = g.texture_file(g.asset_dir() + "/ui/report-qr.png");
        }
        if (qr_)
            g.image(qr_, px + pw - 14 - 80, hy + 12, 80, 80, rgba(0xFFFFFF, alpha), 8);
    }

    /* The buttons. */
    std::vector<std::pair<Glyph, std::string>> p;
    if (on_rail_)
        p = {{Glyph::DPad, "Sections"}, {Glyph::Cross, "Open"}, {Glyph::Circle, game ? "Details" : "Menu"}};
    else
    {
        const SettingRow &focus = rows_[std::size_t(settings_row_)];
        if (focus.toggle >= 0)
            p.push_back({Glyph::Cross, focus.toggle ? "Turn Off" : "Turn On"});
        else if (focus.action == kRowUpdate)
            p.push_back({Glyph::Cross, update_available() ? "Install" : "Check Now"});
        else if (focus.action)
        {
            std::string verb = focus.values.empty() ? std::string("Select") : focus.values.front();
            const std::string dots = "\xE2\x80\xA6";
            if (verb.size() >= dots.size() && verb.compare(verb.size() - dots.size(), dots.size(), dots) == 0)
                verb.resize(verb.size() - dots.size());
            p.push_back({Glyph::Cross, verb.empty() ? std::string("Select") : verb});
        }
        else if (!focus.bool_value && !focus.int_value)
            p.push_back({Glyph::DPad, "Browse"});
        else
            p.push_back({Glyph::DPad, "Change"});
        p.push_back({Glyph::Circle, "Sections"});
    }
    if (!pending.empty())
        p.push_back({Glyph::Square, "Apply"});
    if (!on_rail_)
        p.push_back({Glyph::Triangle, "Menu"});
    draw_glass_prompts(p, alpha);
}

/* ---- 4.5: the side menu in every theme ---------------------------------------------------- */

void App::set_skin()
{
    g_skin = !settings_ || glass() ? 0 : revolution() ? 2 : starcube() ? 3 : 1;
}

void App::skin_select(float x, float y, float w, float h, float r, float a)
{
    select_glass(*g_, std::clamp(settings_->ui_mode, 0, 2), x, y, w, h, r, a);
}

void App::open_menu_tab(int tab)
{
    /* From the side menu: Star Cube opens its cube's face for it. */
    screen_ = Screen::Main;
    if (starcube())
        sc_open(tab == int(Tab::MemoryCards) ? 2 : tab == int(Tab::Settings) ? 3 : 0, true);
    else
        set_tab(tab, +1);
}

App::Action App::update_menu_rows(bool left, bool right, bool up, bool down)
{
    /* The side menu's rows (every theme's). */
    auto &games = lib_->games();
    std::vector<GlassItem> items = glass_items();
    const int n = int(items.size());
    gl_row_ = std::clamp(gl_row_, 0, n - 1);
    auto step = [&](int dir) {
        int r = gl_row_ + dir;
        while (r >= 0 && r < n && items[std::size_t(r)].kind == 4)
            r += dir;
        if (r >= 0 && r < n)
        {
            gl_row_ = r;
            sfx(Sound::MenuScroll);
        }
    };
    if (up)
        step(-1);
    if (down)
        step(+1);
    const GlassItem &it = items[std::size_t(gl_row_)];
    if (it.kind == 1 && (left || right || pressed(BtnCross)))
    {
        /* Sort By: left and right (or Cross) for the next order. */
        const std::string key = lib_->shown() > 0 ? Library::key_of(games[std::size_t(selected_)]) : "";
        lib_->sort(Library::Sort((int(lib_->sort_order()) + (left ? 3 : 1)) % 4));
        keep_selection(key);
        lib_->save();
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    if (it.kind == 6 && (left || right || pressed(BtnCross)))
    {
        /* Show: the filters, as L1 and R1 go through them. */
        std::vector<int> shows;
        glass_filters(shows);
        int at = 0;
        for (int i = 0; i < int(shows.size()); ++i)
            if (shows[std::size_t(i)] == int(lib_->show()))
                at = i;
        const int count = int(shows.size());
        at = (at + (left ? count - 1 : 1)) % count;
        const std::string key = lib_->shown() > 0 ? Library::key_of(games[std::size_t(selected_)]) : "";
        lib_->set_show(Library::Show(shows[std::size_t(at)]));
        keep_selection(key);
        lib_->save();
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    if (pressed(BtnCircle) || pressed(BtnTriangle))
    {
        gl_menu_ = false;
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    if (it.kind == 8 && pressed(BtnCross) && lib_->shown() > 0)
    {
        /* A favorite, or not: the menu stays, its row says which. */
        Game &game = games[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
        lib_->toggle_favourite(game);
        gl_heart_at_ = time_;
        sfx(game.favourite ? Sound::LaunchGame : Sound::MovingTab);
        return Action::None;
    }
    if (!pressed(BtnCross))
        return Action::None;
    gl_menu_ = false;
    switch (it.kind)
    {
    case 2:
        sfx(Sound::LaunchGame);
        open_screen(Screen::Main);
        set_tab(int(Tab::Library), -1);
        return Action::FetchCovers;
    case 3:
        open_menu_tab(int(Tab::MemoryCards));
        break;
    case 5:
    {
        const std::string want = section_name(it.section);
        open_menu_tab(int(Tab::Settings));
        /* Straight into the section's rows (Circle goes up to the sections). */
        for (int i = 0; i < section_count(); ++i)
            if (section_name(i) == want)
                rail_ = i;
        const int first = first_row_of(section_name(rail_));
        if (first >= 0)
        {
            settings_row_ = first;
            on_rail_ = false;
        }
        break;
    }
    case 7:
        open_menu_tab(int(Tab::Settings));
        rail_ = 0;
        on_rail_ = true;
        break;
    default:
        break;
    }
    return Action::None;
}

App::Action App::update_side_menu(bool left, bool right, bool up, bool down, bool &handled)
{
    /* The other themes: Triangle (or a swipe in from the pad's right) brings
     * the side menu in over what's showing; it works as the Porpoise look's. */
    handled = false;
    const bool where = screen_ == Screen::Main &&
                       ((starcube() && sc_home_) ||
                        (tab_ == Tab::Library && !(starcube() && sc_calendar_)) ||
                        (tab_ == Tab::Settings && on_rail_));
    const int start = time_ - gl_swipe_at_ < 0.3 ? gl_drag_start_ : 0;
    gl_drag_start_ = 0;
    if (!gl_menu_ && where && (pressed(BtnTriangle) || start > 0))
    {
        handled = true;
        glass_open_menu();
        if (start > 0)
        {
            gl_dragging_ = true;
            gl_menu_drag_in_ = true;
            gl_drag_ = 0;
        }
        return Action::None;
    }
    if (start < 0 && gl_menu_)
    {
        handled = true;
        gl_dragging_ = true;
        gl_menu_drag_in_ = false;
        gl_drag_ = 1;
        return Action::None;
    }
    if (gl_drag_let_go_)
    {
        gl_drag_let_go_ = false;
        if (gl_dragging_)
        {
            gl_dragging_ = false;
            const bool open = gl_drag_speed_ > 1.6f || (gl_drag_speed_ > -1.6f && gl_drag_ > (gl_menu_drag_in_ ? 0.33f : 0.67f));
            if (!open && gl_menu_)
            {
                gl_menu_ = false;
                sfx(Sound::MenuScroll);
            }
            gl_slab_v_ = gl_drag_speed_ * -(1960.0f - kMenuX) * 0.5f;
        }
    }
    if (gl_dragging_ && !gl_menu_)
        gl_dragging_ = false;
    if (!gl_menu_)
        return Action::None;
    handled = true;
    return update_menu_rows(left, right, up, down);
}

void App::draw_side_menu()
{
    /* The other themes: the side menu over the page, in the theme's colors. */
    if (glass() || gl_slab_a_ <= 0.01f || gl_slab_[0] >= 1920)
        return;
    Gfx &g = *g_;
    const bool toned = g.toned();
    const float open_k = clamp01((1960.0f - gl_slab_[0]) / (1960.0f - kMenuX));
    g.set_layer();
    g.set_tone(false);
    g.panel(-20, -20, 1960, 1120, revolution() ? rgba(0xF2F4F7, 0.45f * open_k) : rgba(0x02040C, 0.45f * open_k), 1, 0);
    glass_pill(gl_slab_[0], gl_slab_[1], gl_slab_[2], gl_slab_[3], 30, gl_slab_a_);
    g.set_tone(revolution()); /* (white words become Revolution's ink) */
    if (gl_menu_seen_)
        draw_glass_menu(gl_slab_a_);
    g.set_tone(toned);
}

void App::menu_haptics()
{
    /* A light tick as a menu or the island follows the finger on the touch
     * pad, every tenth of the way (Oct 24: not as the choice moves). */
    if (!settings_)
        return;
    const int sel = screen_ == Screen::Main && tab_ == Tab::Library ? selected_ : -1;
    const int row = gl_menu_ ? gl_row_ : -1;
    const int set = (tab_ == Tab::Settings || screen_ == Screen::GameSettings) && !on_rail_ ? settings_row_ : -1;
    const int rail = (tab_ == Tab::Settings || screen_ == Screen::GameSettings) && on_rail_ ? rail_ : -1;
    const int drag = gl_dragging_ ? int(clamp01(gl_drag_) * 10.0f) : -1;
    const int isl = isl_dragging_ ? int(clamp01(isl_drag_) * 10.0f) : -1;
    if (settings_->menu_haptics)
    {
        if ((drag >= 0 && hap_drag_ >= 0 && drag != hap_drag_) || (isl >= 0 && hap_isl_ >= 0 && isl != hap_isl_))
            porpoise::pad::menu_tick(30, 16); /* (on the finger: lighter still) */
    }
    hap_sel_ = sel;
    hap_row_ = row;
    hap_set_ = set;
    hap_rail_ = rail;
    hap_drag_ = drag;
    hap_isl_ = isl;
}

/* ---- the island --------------------------------------------------------------------------- */

void App::ctl_art(int which, float cx, float cy, float size, Color c)
{
    Gfx &g = *g_;
    if (!gl_ctl_tried_)
    {
        gl_ctl_tried_ = true;
        gl_ctl_ = g.texture_file(g.asset_dir() + "/ui/controllers.png");
    }
    if (!gl_ctl_ || which < 0 || which > 9)
        return;
    const float uv[4] = {float(which) / 10.0f, 0.0f, float(which + 1) / 10.0f, 1.0f};
    /* A picture's tint isn't toned for Light by itself: ink is, here. */
    g.image_part(gl_ctl_, cx - size * 0.5f, cy - size * 0.5f, size, size, uv, g.toned() ? g.tone(c) : c,
                 0.01f); /* (as a cover: filtered when drawn small) */
}

float App::battery_icon(float right, float cy, int level, Color c)
{
    /* A battery on its side, filled as far as it's charged: amber when low,
     * the fill breathing while it charges. */
    Gfx &g = *g_;
    const float w = 30, h = 15, nub = 3;
    const float x = right - w - nub;
    const bool charging = level >= 6;
    const int lit = charging ? 5 : std::clamp(level, 0, 5);
    const Color fill = !charging && lit <= 1 ? rgba(0xF0B467, c.a) : c;
    const bool solid = g.solid();
    g.set_solid(true);
    g.panel(x, cy - h * 0.5f, w, h, with_alpha(c, 0.0f), 1, 4, with_alpha(c, 0.75f * c.a), 1.6f);
    g.panel(x + w + 0.5f, cy - 3, nub, 6, with_alpha(c, 0.75f * c.a), 1, 1.2f);
    const float inner = w - 6;
    float k = float(lit) / 5.0f;
    if (charging)
        k = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(float(time_) * 3.0f));
    if (k > 0)
        g.panel(x + 3, cy - h * 0.5f + 3, std::max(2.5f, inner * k), h - 6, fill, 1, 2);
    g.set_solid(solid);
    return w + nub;
}

void App::draw_island(int style)
{
    if (!settings_)
        return;
    Gfx &g = *g_;
    /* style: 0 the Porpoise look, 1 Porpoise Legacy, 2 Revolution, 3 Star Cube (4.5: the
     * last two at their top bars' height, in their own panels: glass_pill). */
    const bool classic = style == 1, bar = style != 0;
    const float h0 = bar ? kBarH : 64, top = bar ? kBarY : 40, right = bar ? 1866 : 1864;
    const Color soft = classic ? kLavender : ink(0.75f);

    /* What's here. */
    const porpoise::move::Status ms = porpoise::move::status();
    bool move = ms.connected, camera = ms.camera, ball = ms.ball;
    int battery = ms.battery, pads = 0;
    for (int i = 0; i < 4; ++i)
        pads += porpoise::pad::connected(i) ? 1 : 0;
    if (preview_kit())
    {
        move = camera = ball = true;
        battery = 4;
        pads = 1;
    }
    const std::string clock = clock_text();
    const float clock_size = ts(classic ? 25 : 26);
    const float clock_w = g.measure(Font::Bold, clock_size, clock);
    /* The row's pieces, right to left: the time, a rule, the camera, the
     * DualSenses, the Move with its battery. */
    ctl_art(-1, 0, 0, 0, kClear); /* (loads the pictures) */
    const bool art = gl_ctl_ != nullptr; /* (Classic's island too) */
    /* Left to right: the DualSenses, the Move, the camera beside it. */
    float icons_w = 0;
    int groups = 0;
    if (camera)
        icons_w += 28 + 7 + 8, ++groups;
    if (pads)
        icons_w += float(std::min(pads, 4)) * (art ? 36.0f : 32.0f) + float(std::min(pads, 4) - 1) * 8, ++groups;
    if (move)
        icons_w += art ? 22.0f : 18.0f + 8 + 5.0f * 6.0f, ++groups;
    icons_w += float(std::max(0, groups - 1)) * 18;
    const float row_w = 26 + icons_w + (icons_w > 0 ? 18 + 1 + 18 : 0) + clock_w + 26;

    /* News: a controller's, else the covers'. */
    const CtlToast *news = nullptr;
    for (const CtlToast &t : ctl_toasts_)
        if (time_ - t.at < kToastFor)
            news = &t;
    const std::string note = library_note();
    const float wide = 520;
    const int eye_rows = (preview_kit() || ms.camera_state >= 1) ? 1 : 0;
    const int rows_n = (move ? 1 : 0) + std::min(pads, 4) + eye_rows + (move || pads || eye_rows ? 0 : 1);
    /* Its sizes shut and open; dragged, it's between them, on the finger. */
    constexpr float kShortcutsH = 132; /* the shortcuts' row, at its foot */
    const float open_w = std::max(row_w, wide),
                open_h = h0 + 92 + 28 + float(rows_n) * 50 + (note.empty() ? 14.0f : 52.0f) + kShortcutsH;
    const bool news_shown = news || !note.empty();
    const float shut_w = news_shown ? std::max(row_w, wide) : row_w, shut_h = news_shown ? h0 + 64 : h0;
    const float open_k = classic ? (isl_open_ ? 1.0f : 0.0f) : (isl_dragging_ ? isl_drag_ : (isl_open_ ? 1.0f : 0.0f));
    isl_tw_ = shut_w + (open_w - shut_w) * open_k;
    isl_th_ = shut_h + (open_h - shut_h) * open_k;
    if (isl_w_ <= 0 || isl_drawn_at_ < 0 || time_ - isl_drawn_at_ > 0.25)
    {
        isl_w_ = isl_tw_;
        isl_h_ = isl_th_;
    }
    isl_drawn_at_ = time_;
    const float w = isl_w_, h = isl_h_;
    const float x0 = right - w, x1 = right, cy = top + h0 * 0.5f;
    if (classic)
        g.panel(x0, top, w, h, rgba(0x0A1236, 0.70f), 0.9f, std::min(h0 * 0.5f, 28.0f), rgba(0x3D4F9E, 0.9f), 1.6f);
    else
        glass_pill(x0, top, w, h, 32, bar ? 1.25f : 1.0f); /* (the other themes': solid) */
    const Color c = classic ? kWhite : ink(1.0f);

    float x = x1 - 26;
    g.text_mid(Font::Bold, clock_size, x, cy, c, Align::Right, clock);
    x -= clock_w;
    if (icons_w > 0)
    {
        x -= 18;
        g.panel(x - 1, cy - 13, 1, 26, with_alpha(c, 0.2f), 1, 0);
        x -= 19;
    }
    if (camera)
    {
        g.panel(x - 8, cy - 4, 8, 8, kGood, 1, 4);
        x -= 8 + 7;
        camera_icon(x - 14, cy, 28, c);
        x -= 28 + 18;
    }
    if (move && art)
    {
        ctl_art(2, x - 11, cy, 34, c); /* (its battery: in the island, opened) */
        draw_move_ball(x - 11, cy, 34, c.a); /* the ball in its color */
        x -= 22 + 18;
    }
    else if (move)
    {
        if (battery >= 0)
        {
            const int lit = battery >= 6 ? 5 : std::clamp(battery, 0, 5);
            for (int k = 4; k >= 0; --k)
            {
                g.panel(x - 4, cy - 6.5f, 4, 13, k < lit ? (lit <= 1 ? rgba(0xF0B467) : c) : with_alpha(c, 0.28f), 1,
                        1);
                x -= 6;
            }
            x -= 8;
        }
        ctl_icon(0, x - 9, cy, 24, c, rgba(ball_hex()));
        x -= 18 + 18;
    }
    for (int i = 0; i < std::min(pads, 4); ++i)
    {
        if (art)
        {
            ctl_art(0, x - 18, cy, 36, c);
            x -= 36 + 8;
        }
        else
        {
            ctl_icon(1, x - 16, cy, 32, c, rgba(ball_hex()));
            x -= 32 + 8;
        }
    }

    /* Below the row: news, or everything, as the island opens (rows past its
     * edge as it is now wait until it has grown to them). */
    const float body_top = top + h0;
    const bool opened = isl_open_ || (isl_dragging_ && !classic);
    const float open = opened ? clamp01((h - h0 - 12) / std::max(1.0f, open_h - h0 - 12))
                              : clamp01((h - h0 - 12) / std::max(1.0f, isl_th_ - h0 - 12));
    if (h < h0 + 20 || open <= 0.02f)
        return;
    const float a0 = open * open;
    const float bottom = top + h - 6;
    /* What's under the glass's edge as it is now fades in as the edge passes
     * it (and out, closing), so nothing pops in or is cut off. */
    float a = a0;
    auto reveal = [&](float y_top, float y_bottom) {
        const float span = std::max(1.0f, y_bottom - y_top);
        const float k = clamp01((bottom - (y_bottom - 0.45f * span)) / (0.6f * span));
        a = a0 * k * k * (3.0f - 2.0f * k);
        return a > 0.01f;
    };
    if (!opened)
    {
        /* News drops down out of it. */
        const float ny = body_top + 26;
        if (!reveal(ny - 18, ny + 18))
            return;
        if (news)
        {
            ctl_icon(news->icon, x0 + 46, ny, 32, with_alpha(c, a), rgba(ball_hex(), a));
            g.text_mid(Font::ExtraBold, ts(19), x0 + 80, news->sub.empty() ? ny : ny - 11, with_alpha(c, a),
                       Align::Left, fit(g, Font::ExtraBold, ts(19), news->title, w - 110));
            if (!news->sub.empty())
                g.text_mid(Font::Regular, ts(15), x0 + 80, ny + 13, with_alpha(soft, a), Align::Left,
                           fit(g, Font::Regular, ts(15), news->sub, w - 110));
        }
        else
            g.text_mid(Font::Bold, ts(18), x0 + 26, ny, with_alpha(c, a), Align::Left,
                       fit(g, Font::Bold, ts(18), note, w - 52));
        return;
    }
    float y = body_top;
    g.panel(x0 + 26, y + 2, w - 52, 1, with_alpha(c, 0.16f * a0), 1, 0);
    if (reveal(y + 20, y + 80))
    {
        /* The time, big, and the whole date. */
        std::string hm = clock, ampm;
        const std::size_t sp = clock.rfind(' ');
        if (sp != std::string::npos)
        {
            hm = clock.substr(0, sp);
            ampm = clock.substr(sp + 1);
        }
        const float tw = g.text_mid(Font::ExtraBold, ts(44), x0 + 26, y + 50, with_alpha(c, a), Align::Left, hm);
        if (!ampm.empty())
            g.text_mid(Font::Bold, ts(22), x0 + 26 + tw + 6, y + 54, with_alpha(soft, a), Align::Left, ampm);
        g.text_mid(Font::Bold, ts(19), x1 - 26, y + 52, with_alpha(soft, a), Align::Right, long_date());
    }
    y += 92;
    if (reveal(y - 6, y + 14))
        g.text_mid(Font::ExtraBold, ts(13), x0 + 26, y + 4, with_alpha(soft, 0.8f * a), Align::Left, tr("CONNECTED"),
                   1.5f);
    y += 28;
    /* One row each: its picture, its name, and on the right what's worth
     * knowing - a battery where the controller tells its charge, a player's
     * light - never "Connected" (the heading says so). */
    auto row = [&](int icon, const std::string &name, int level, std::uint32_t light, const std::string &value, Color vc) {
        if (reveal(y, y + 46))
        {
            const float ry = y + 22;
            if (icon == 5)
                camera_icon(x0 + 44, ry, 28, with_alpha(c, a));
            else if (art)
                ctl_art(icon == 0 ? 2 : icon == 1 ? 0 : icon, x0 + 44, ry, icon == 0 ? 34 : 38, with_alpha(c, a));
            else
                ctl_icon(icon, x0 + 42, ry, icon == 0 ? 26 : 30, with_alpha(c, a), rgba(ball_hex(), a));
            g.text_mid(Font::ExtraBold, ts(18), x0 + 76, ry, with_alpha(c, a), Align::Left, name);
            float rx = x1 - 26;
            if (level >= 0)
                rx -= battery_icon(rx, ry, level, with_alpha(c, a)) + 12;
            if (light)
            {
                g.panel(rx - 10, ry - 5, 10, 10, rgba(light, a), 1, 5);
                rx -= 22;
            }
            if (!value.empty())
                g.text_mid(Font::Regular, ts(16), rx, ry, with_alpha(vc, a), Align::Right, value);
        }
        y += 50;
    };
    {
        /* The DualSenses first, then the Move, then the camera. */
        const int lights[4] = {settings_->light_1, settings_->light_2, settings_->light_3, settings_->light_4};
        static const std::uint32_t kLights[10] = {0x2F6FFF, 0xFF3A48, 0x10C860, 0xFF4CC0, 0x8C4CFF,
                                                  0xFF8A1A, 0xFFC81A, 0x1AC8FF, 0xFFFFFF, 0x50546A};
        for (int i = 0; i < std::min(pads, 4); ++i)
            row(1, tr("DualSense"), -1, kLights[std::clamp(lights[i], 0, 9)],
                pads > 1 ? trf("Player {n}", {{"n", std::to_string(i + 1)}}) : "", soft);
    }
    if (move)
        row(0, tr("PlayStation Move"), battery, 0, battery >= 6 ? tr("Charging") : "", soft);
    const int eye = preview_kit() ? 4 : ms.camera_state;
    if (eye >= 1)
        row(5, tr("PS3 Eye"), -1, eye == 4 ? 0x34C77B : 0, /* working: a green dot */
            eye == 4   ? std::string()
            : eye == 3 ? tr("Not plugged in")
            : eye == 2 ? tr("Starting")
                       : tr("Needs the ELF loader"),
            soft);
    if (!move && !pads && !camera)
        row(4, tr("No controllers"), -1, 0, "", soft);
    if (!note.empty() && reveal(y + 10, y + 36))
    {
        /* What's downloading, when something is. */
        g.panel(x0 + 26, y + 4, w - 52, 1, with_alpha(c, 0.16f * a), 1, 0);
        g.text_mid(Font::Regular, ts(15), x0 + 26, y + 28, with_alpha(soft, 0.9f * a), Align::Left,
                   fit(g, Font::Regular, ts(15), note, w - 52));
    }
    /* The shortcuts: the finger on the touch pad picks one, a click opens it. */
    y += note.empty() ? 6.0f : 52.0f;
    if (!isl_sc_tried_)
    {
        isl_sc_tried_ = true;
        isl_sc_tex_ = g.texture_file(g.asset_dir() + "/ui/shortcuts.png");
    }
    if (reveal(y, y + kShortcutsH - 14))
    {
        g.panel(x0 + 26, y + 2, w - 52, 1, with_alpha(c, 0.16f * a), 1, 0);
        static const char *const kNames[4] = {"Memory Cards", "Settings", "Online", "Screenshots"};
        const float span = (w - 52) / 4.0f, tile = 64, ty = y + 18;
        for (int i = 0; i < 4; ++i)
        {
            const float cx = x0 + 26 + span * (float(i) + 0.5f);
            const bool on = isl_pick_ == i;
            if (classic)
                g.panel(cx - tile * 0.5f, ty, tile, tile, on ? rgba(0x1F63F0, 0.9f * a) : rgba(0x07102E, 0.5f * a), 1,
                        tile * 0.5f, on ? rgba(0x8BD9FF, a) : rgba(0x3D4F9E, 0.8f * a), on ? 2.0f : 1.4f);
            else if (on && g_skin == 0 && settings_->ui_mode == 2)
            {
                /* (Light: bright glass on light glass doesn't show: the picked one dark) */
                const bool toned = g.toned();
                g.set_tone(false);
                g.panel(cx - tile * 0.5f, ty, tile, tile, rgba(0x1B2130, 0.92f * a), 1, tile * 0.5f);
                g.set_tone(toned);
            }
            else
                glass_pill(cx - tile * 0.5f, ty, tile, tile, tile * 0.5f, a, on);
            const bool light_pick = on && !classic && g_skin == 0 && settings_->ui_mode == 2;
            if (isl_sc_tex_)
            {
                const float uv[4] = {float(i) / 4.0f, 0, float(i + 1) / 4.0f, 1};
                const float is = 34;
                /* (on the picked one's bright glass, dark ink where it's light) */
                const bool dark = on && !classic && !light_pick && (g_skin == 0 || g_skin == 2);
                const bool toned = g.toned();
                if (dark)
                    g.set_tone(false);
                /* (a picture's tint isn't toned for the light looks by itself: ink is) */
                const Color plain = light_pick ? rgba(0xFFFFFF, a) : g.toned() ? g.tone(with_alpha(c, a)) : with_alpha(c, a);
                g.image_part(isl_sc_tex_, cx - is * 0.5f, ty + tile * 0.5f - is * 0.5f, is, is, uv,
                             dark ? with_alpha(kInkDark, a) : plain);
                g.set_tone(toned);
            }
            g.text_mid(Font::Bold, ts(14), cx, ty + tile + 20, with_alpha(on ? c : soft, a), Align::Center,
                       fit(g, Font::Bold, ts(14), tr(kNames[i]), span - 6));
        }
    }
}

void App::island_shortcut(int which)
{
    /* Straight there, the island and the menu shut. */
    isl_open_ = false;
    isl_pick_ = -1;
    gl_menu_ = false;
    sfx(Sound::DetailsFlip);
    switch (which)
    {
    case 0:
        open_menu_tab(int(Tab::MemoryCards));
        break;
    case 1:
        open_menu_tab(int(Tab::Settings));
        rail_ = 0;
        on_rail_ = true;
        break;
    case 2:
    {
        open_menu_tab(int(Tab::Settings));
        for (int i = 0; i < section_count(); ++i)
            if (section_name(i) == "Console and Online")
                rail_ = i;
        const int first = first_row_of(section_name(rail_));
        if (first >= 0)
        {
            settings_row_ = first;
            on_rail_ = false;
        }
        break;
    }
    case 3:
        open_shots("", Screen::Main);
        break;
    default:
        break;
    }
}

/* ---- the whole screen -------------------------------------------------------------------- */

void App::draw_glass(double time)
{
    Gfx &g = *g_;
    const int mode = std::clamp(settings_->ui_mode, 0, 2);
    const bool light = mode == 2;
    {
        /* The room, in the game's colours. */
        Look l = g.look();
        l.light0 = Color{gl_amb_[0], gl_amb_[1], gl_amb_[2], 1};
        l.light1 = Color{gl_amb_[3], gl_amb_[4], gl_amb_[5], 1};
        g.set_look(l);
    }
    /* Play pressed: the library parts for the launch screen (launch_intro). */
    {
        const float k = gl_launching_ ? launch_intro(time) : 0.0f;
        gl_exit_ = k * k * (3.0f - 2.0f * k);
    }
    /* Drawn again after a pause (a game, the preview): glides start where they end. */
    gl_snap_ = gl_drawn_at_ < 0 || time_ - gl_drawn_at_ > 0.25;
    gl_drawn_at_ = time_;
    g.set_layer();
    g.set_tone(false);
    g.background();
    g.set_intro(intro_fade_, intro_dy_);
    g.set_tone(light);
    layer_dx_ = layer_dy_ = 0;
    layer_fade_ = 1;

    const bool full_page = screen_ == Screen::WiiSetup || screen_ == Screen::WiiGuide || screen_ == Screen::Mapping ||
                           screen_ == Screen::Browse || screen_ == Screen::TileArt || screen_ == Screen::Shots;
    /* (A game's settings grow out of its page, which stays behind them: it went
     * back to the library under them, and came back after.) */
    const bool details = screen_ == Screen::Details || screen_ == Screen::States ||
                         (screen_ == Screen::GameSettings && tab_ == Tab::Library) ||
                         (screen_ == Screen::Achievements && ach_back_ == Screen::Details);
    /* How far the glass has come in (0 off to the right, 1 the menu's place),
     * and how far it has grown toward a page: what's behind it fades with
     * where the glass is, not on a clock of its own. */
    const float open_k = clamp01((1960.0f - gl_slab_[0]) / (1960.0f - kMenuX));
    const float menu_k = open_k * (1.0f - gl_page_);
    const float page_k = gl_page_;
    /* Behind the menu, the screen fades back; behind a page, it's gone once
     * the glass is nearly over it (Details stayed as Game Settings grew). */
    const float base = clamp01(1.0f - 0.22f * menu_k) * (1.0f - smooth01(0.45f, 0.95f, page_k));
    const float screen_in = screen_anim_ > 0 ? 0.25f + 0.75f * ease_out(1.0f - screen_anim_) : 1.0f;

    if (!full_page && base > 0.01f)
    {
        if (details)
        {
            /* Square: the other cases fade and part as the chosen one flies to
             * the page (it went to black at once). */
            if (screen_ == Screen::Details && flip_anim_ > 0.01f && tab_ == Tab::Library && !lib_->games().empty())
            {
                const float k = flip_anim_;
                gl_part_ = 1.0f - k * k;
                if (settings_->glass_layout == 1)
                    draw_glass_rows(time, base * k * k);
                else
                    draw_glass_flow(time, base * k * k);
                gl_part_ = 0;
            }
            draw_glass_details(time, base);
            if (screen_ == Screen::States)
                draw_glass_states(time);
        }
        else if (tab_ == Tab::Library || gl_page_ < 0.98f)
        {
            if (lib_->games().empty())
            {
                g.set_layer(0, 0, base);
                g.text_mid(Font::ExtraBold, ts(50), 960, 430, ink(1), Align::Center, tr("No games yet"));
                g.text_mid(Font::Regular, ts(22), 960, 494, ink(0.75f), Align::Center,
                           tr("Copy .iso, .rvz or .ciso files with PS5 Upload into"));
                g.text_mid(Font::Bold, ts(22), 960, 532, ink(1), Align::Center, data_dir_ + "/games");
                g.text_mid(Font::Regular, ts(22), 960, 570, ink(0.75f), Align::Center,
                           tr("or add your own folder in Settings, under Games."));
                g.set_layer();
            }
            else if (gl_back_ > 0.001f && tab_ == Tab::Library && screen_ == Screen::Main)
            {
                /* Back from Details: the others close in round the case as it
                 * flies back to its place (draw_glass_details, backing). */
                const float k = smooth01(0.0f, 1.0f, 1.0f - gl_back_);
                gl_part_ = 1.0f - k;
                if (settings_->glass_layout == 1)
                    draw_glass_rows(time, base * (0.35f + 0.65f * k));
                else
                    draw_glass_flow(time, base * (0.35f + 0.65f * k));
                gl_part_ = 0;
                draw_glass_details(time, base);
            }
            else if (settings_->glass_layout == 1)
                draw_glass_rows(time, base * screen_in);
            else
                draw_glass_flow(time, base * screen_in);
            release_far_art(settings_->glass_layout == 1 ? 20 : 10);
            /* The chosen game's back, ready before Play turns the case over
             * (it showed Porpoise's mark until a game's page had loaded it). */
            if (lib_->shown() > 0 && !gl_launching_)
                back_of(lib_->games()[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))]);
            /* Covers (and spines) fetched ahead, one game at a time, for the
             * ones not in view: L1 and R1 find them here. */
            auto &games = lib_->games();
            const int n = int(games.size());
            int loaded = 0;
            for (const Game &game : games)
                loaded += game.cover ? 1 : 0;
            /* (Not behind the curtain back from a game, nor while what's in view
             * loads: the menus came in hitching, and the curtain waited on it.) */
            const bool settled = intro_fade_ >= 0.999f && !g.loading();
            for (int tries = 0; tries < n && loaded < 80 && !gl_launching_ && settled; ++tries)
            {
                Game &game = games[std::size_t(gl_preload_ % n)];
                const bool cover_done = game.cover || (game.cover_tried && game.cover_wait.empty());
                const bool spine_done = game.spine || (game.spine_tried && game.spine_wait.empty()) || !game.cover;
                if (cover_done && spine_done)
                {
                    gl_preload_ = (gl_preload_ + 1) % n;
                    continue;
                }
                if (!cover_done)
                    cover_of(game);
                else
                    spine_of(game);
                break;
            }
        }
    }
    if (full_page)
    {
        /* The setups and the guide, the folder browser: their own pages, over the room. */
        float dy = 0, fade = 1;
        if (screen_anim_ > 0)
        {
            const float pp = ease_out(1.0f - screen_anim_);
            dy = 22.0f * (1.0f - pp);
            fade = 0.25f + 0.75f * pp;
        }
        g.set_layer(0, dy, fade);
        layer_dy_ = dy;
        layer_fade_ = fade;
        if (screen_ == Screen::Browse)
            draw_browser();
        else if (screen_ == Screen::TileArt)
            draw_tile_art(time);
        else if (screen_ == Screen::Shots)
            draw_shots(time);
        else if (screen_ == Screen::Mapping)
            draw_mapping(time);
        else if (screen_ == Screen::WiiGuide)
            draw_wii_guide(time);
        else if (screen_ == Screen::WiiSetup)
            draw_wii_setup(time);
        g.set_layer();
        layer_dy_ = 0;
        layer_fade_ = 1;
    }
    else
    {
        const float top_a = clamp01(1.0f - page_k * 1.5f);
        g.set_layer(0, -150.0f * gl_exit_, 1.0f - gl_exit_); /* Play: up and away */
        draw_glass_brand(top_a);
        if (tab_ == Tab::Library && !details && !lib_->games().empty() && screen_ == Screen::Main)
            draw_glass_filters(base);
        if (gl_page_ < 0.5f)
            draw_island(0); /* under the menu when it's open */
        g.set_layer();
    }

    /* The menu's glass: the room dimmed under it, the menu, or the page it grew to. */
    if (!full_page && gl_slab_a_ > 0.01f && gl_slab_[0] < 1920)
    {
        if (menu_k > 0.01f)
        {
            const bool toned = g.toned();
            g.set_tone(false);
            g.panel(-20, -20, 1960, 1120, light ? rgba(0xF2F4F7, 0.42f * menu_k) : rgba(0x04070C, 0.42f * menu_k), 1,
                    0);
            g.set_tone(toned);
        }
        const float sx = gl_slab_[0], sy = gl_slab_[1], sw = gl_slab_[2], sh = gl_slab_[3];
        glass_pill(sx, sy, sw, sh, 30, gl_slab_a_ * (light ? 1.3f : 1.1f));
        /* The menu, while the glass is its size (not on the glass on its way
         * to or from a page); the page once it has nearly grown. */
        const float as_menu = gl_menu_seen_ ? (1.0f - smooth01(0.0f, 0.3f, gl_page_)) * gl_slab_a_ : 0.0f;
        const float as_page = smooth01(0.6f, 1.0f, gl_page_);
        if (as_menu > 0.02f)
            draw_glass_menu(as_menu);
        if (!gl_menu_ && as_page > 0.01f)
        {
            /* The page rides in with the glass's left edge as it grows. */
            const float dx = sx - kPageX;
            layer_dx_ = dx;
            if (screen_ == Screen::GameSettings || tab_ == Tab::Settings)
            {
                g.set_layer(dx, 0, 1);
                draw_glass_settings(as_page);
            }
            else if (tab_ == Tab::MemoryCards)
            {
                g.set_layer(dx, 0, as_page);
                layer_fade_ = as_page;
                g.text_mid(Font::ExtraBold, ts(34), 72, 90, kWhite, Align::Left, tr("Memory Cards"));
                draw_memory_cards(time);
            }
            g.set_layer();
            layer_dx_ = 0;
            layer_fade_ = 1;
        }
        /* (No island over a full page: Settings, Memory Cards, a game's settings.) */
    }

    /* The buttons to know, bottom left (Play: down and away). */
    g.set_layer(0, 120.0f * gl_exit_, 1.0f - gl_exit_);
    /* They fade as the menu's glass comes in (on the finger too), not at once. */
    const float hints = 1.0f - smooth01(0.0f, 0.6f, open_k);
    if (!full_page && hints > 0.01f)
    {
        if (details && screen_ == Screen::Details)
        {
            std::vector<std::pair<Glyph, std::string>> p = {{Glyph::Cross, "Select"}, {Glyph::Circle, "Back"}};
            p.push_back({Glyph::Triangle, "Menu"}); /* (the achievements are on the page: down) */
            p.push_back({Glyph::R1, lib_->games()[std::size_t(selected_)].favourite ? "Unfavorite" : "Favorite"});
            if (lib_->shown() > 1)
                p.push_back({kKeyL2R2, "Other Games"});
            draw_glass_prompts(p, base * hints);
        }
        else if (tab_ == Tab::Library && screen_ == Screen::Main && gl_page_ < 0.5f)
        {
            if (lib_->games().empty())
                draw_glass_prompts({{Glyph::Triangle, "Menu"}}, base * hints);
            else
                draw_glass_prompts({{Glyph::Cross, "Play"}, {Glyph::Square, "Details"}, {Glyph::Triangle, "Menu"}},
                                   base * hints);
        }
    }
    g.set_layer();

    g.set_tone(light);
    draw_dialog();
    draw_update_overlay(time);
    g.set_intro(1, 0);
    draw_theme_overlay();
}

/* ---- save states ------------------------------------------------------------------------- */

namespace
{
/* A save state's time: "Oct 9, 12:29 AM" (the date by numbers in other languages). */
std::string slot_when(long long t)
{
    if (t <= 0)
        return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[48];
    if (language() == Language::English)
    {
        const int h12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
        std::snprintf(buf, sizeof buf, "%.3s %d, %d:%02d %s", kMonthNames[std::clamp(tm.tm_mon, 0, 11)], tm.tm_mday,
                      h12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    }
    else
        std::strftime(buf, sizeof buf, "%Y-%m-%d  %H:%M", &tm);
    return buf;
}
} // namespace

void App::draw_glass_states(double time)
{
    /* The game's save states on glass over its page: three slots, a picture
     * of each, and when it was saved. */
    (void)time;
    Gfx &g = *g_;
    if (!states_game_)
        return;
    const int mode = std::clamp(settings_->ui_mode, 0, 2);
    const float k = screen_anim_ > 0 ? ease_out(1.0f - screen_anim_) : 1.0f;
    {
        const bool toned = g.toned();
        g.set_tone(false);
        g.panel(-20, -20, 1960, 1120, mode == 2 ? rgba(0xEEF1F5, 0.45f * k) : rgba(0x04070C, 0.45f * k), 1, 0);
        g.set_tone(toned);
    }
    const float pw = 1560, ph = 640, px = (1920 - pw) * 0.5f, py = 200 + (1.0f - k) * 30.0f;
    glass_pill(px, py, pw, ph, 30, 1.15f * k);
    g.text_mid(Font::ExtraBold, ts(40), px + 52, py + 66, ink(k), Align::Left, tr("Save States"));
    {
        const Game &game = *states_game_;
        const std::string title =
            game.db_title.empty() ? (game.title.empty() ? game.file : game.title) : game.db_title;
        g.text_mid(Font::SemiBold, ts(23), px + 52, py + 110, ink(0.70f * k), Align::Left,
                   fit(g, Font::SemiBold, ts(23), title, pw - 104));
    }
    const float cw = 456, ch = 340, gap = 30, cx0 = px + (pw - (cw * 3 + gap * 2)) * 0.5f, cy = py + 156;
    for (int i = 0; i < porpoise::states::kSlots; ++i)
    {
        const float cx = cx0 + float(i) * (cw + gap);
        const bool on = i == states_sel_;
        if (on)
            g.panel(cx - 8, cy - 8, cw + 16, ch + 16, kClear, 1, 26, ink(0.9f * k), 3.0f, 12);
        glass_pill(cx, cy, cw, ch, 20, (on ? 1.6f : 0.9f) * k);
        const float pad = 14, iw_max = cw - pad * 2, ih_max = ch - 76;
        if (menu_slot_tex_[i])
        {
            const float aspect = float(menu_slot_tex_[i]->width) / float(std::max(1, menu_slot_tex_[i]->height));
            float iw = iw_max, ih = iw / aspect;
            if (ih > ih_max)
            {
                ih = ih_max;
                iw = ih * aspect;
            }
            g.image(menu_slot_tex_[i], cx + (cw - iw) * 0.5f, cy + pad + (ih_max - ih) * 0.5f, iw, ih,
                    rgba(0xFFFFFF, (on ? 1.0f : 0.82f) * k), 12);
        }
        else
            g.text_mid(Font::SemiBold, ts(24), cx + cw * 0.5f, cy + pad + ih_max * 0.5f, ink(0.5f * k), Align::Center,
                       menu_slot_used_[i] ? tr("Saved") : tr("Empty"));
        g.text_mid(Font::ExtraBold, ts(25), cx + 22, cy + ch - 32, ink((on ? 1.0f : 0.85f) * k), Align::Left,
                   trf("Slot {n}", {{"n", std::to_string(i + 1)}}));
        if (menu_slot_used_[i])
            g.text_mid(Font::SemiBold, ts(20), cx + cw - 20, cy + ch - 32, ink(0.70f * k), Align::Right,
                       slot_when(menu_slot_time_[i]));
    }
    g.text_mid(Font::SemiBold, ts(21), 960, py + ph - 46, ink(0.70f * k), Align::Center,
               tr("Save a state from the in-game menu (OPTIONS + touch pad) while you play."));
    if (menu_slot_used_[states_sel_])
        draw_glass_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Play from Here"}, {Glyph::Circle, "Back"},
                            {Glyph::Square, "Delete"}},
                           1.0f);
    else
        draw_glass_prompts({{Glyph::DPad, "Choose"}, {Glyph::Circle, "Back"}}, 1.0f);
}

/* ---- starting a game ---------------------------------------------------------------------- */

void App::draw_glass_launch(double time, float k, float bg, float rise, float words)
{
    /* The room in the game's colours, the cover as its case, big, gliding in
     * from where it was in the library; its name, bold, and a thin bar. */
    Gfx &g = *g_;
    const bool light = settings_->ui_mode == 2;
    auto ease = [](float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); };
    {
        Look l = g.look();
        l.light0 = Color{gl_amb_[0], gl_amb_[1], gl_amb_[2], 1};
        l.light1 = Color{gl_amb_[3], gl_amb_[4], gl_amb_[5], 1};
        g.set_look(l);
    }
    g.set_layer();
    g.set_tone(false);
    g.background(light ? 1.0f : bg); /* (Light: the room as it was; up from black it went grey) */
    g.set_tone(light);
    draw_glass_brand(words);

    Texture *cover = launch_ ? (launch_->cover ? launch_->cover : cover_of(*launch_)) : nullptr;
    const float ch = 480, cw = ch * 136.0f / 191.0f; /* the case, as in the library */
    const float cx = 960, cy = 380;                   /* (its reflection clear of the words) */
    /* The case flips over on its way from where it was in the library (its
     * front's rectangle, launch_from_) to the middle, as the room comes up. */
    float x = cx - cw * 0.5f, y = cy - ch * 0.5f, w = cw, h = ch, yaw = 0;
    float lift = 0;
    if (cover && k < 1.0f && launch_from_ok_)
    {
        const float e = ease(k);
        lift = std::sin(k * kPi);
        x = launch_from_[0] + (x - launch_from_[0]) * e;
        y = launch_from_[1] + (y - launch_from_[1]) * e - 30.0f * lift;
        w = launch_from_[2] + (w - launch_from_[2]) * e;
        h = launch_from_[3] + (h - launch_from_[3]) * e;
        const float t = k * k * (3.0f - 2.0f * k);
        yaw = -2.0f * kPi * (1.0f - t) + (settings_->glass_layout == 1 ? 0.0f : 0.30f) * (1.0f - t); /* one whole turn, landing on its front (from the row's rest) */
    }
    /* A soft shadow under it, and the room's light pooled behind. */
    pool(g, x + w * 0.5f, y + h * 0.62f, w * 2.3f, h * 0.8f,
         Color{gl_amb_[0], gl_amb_[1], gl_amb_[2], (settings_->ui_mode == 1 ? 0.0f : light ? 0.25f : 0.45f) * rise});
    if (cover && launch_)
    {
        /* The case itself, as the library drew it, with its reflection. */
        box_white_wii_ = true;
        box_reflect_ = light ? 0.27f : 0.18f;
        const float bw = w, bh = h;
        draw_box3d(*launch_, x + bw * 0.5f, y + bh * 0.5f, bw, bh, bw * kCaseDepth, yaw, 1.0f, false);
        box_reflect_ = 0;
        box_white_wii_ = false;
    }
    else if (!launch_ || launch_->cover_wait.empty())
    {
        g.set_layer(0, 40.0f * (1.0f - rise), rise);
        draw_mark(cx, cy, 230, with_alpha(kWhite, 0.9f));
        g.set_layer();
    }

    g.set_layer(0, 16.0f * (1.0f - words), words);
    const std::string title = launch_ ? (launch_->db_title.empty() ? launch_->title : launch_->db_title) : "";
    g.text_mid(Font::ExtraBold, ts(50), 960, 850, ink(1),
               Align::Center, fit(g, Font::ExtraBold, ts(50), launch_ ? trf("Starting {game}\xE2\x80\xA6", {{"game", title}})
                                                                      : tr("Starting game\xE2\x80\xA6"),
                                  1600));
    /* A thin bar: measured when known, else a moving segment. */
    const float bx = 660, by = 912, bw = 600, bh = 6;
    {
        const bool toned = g.toned();
        g.set_tone(false);
        g.set_solid(true);
        g.panel(bx, by, bw, bh, light ? rgba(0x2A3140, 0.14f) : rgba(0xFFFFFF, 0.16f), 1, 3);
        const Color fillc = light ? rgba(0x2A3140, 0.85f) : rgba(0xFFFFFF, 0.92f);
        if (launch_progress_ >= 0)
            g.panel(bx, by, std::max(12.0f, bw * std::min(launch_progress_, 1.0f)), bh, fillc, 1, 3);
        else
        {
            const float t = float(std::fmod(time - launch_start_, 1.6) / 1.6);
            const float seg = 160, sx = bx + (bw - seg) * (0.5f - 0.5f * std::cos(t * 2 * kPi));
            g.panel(sx, by, seg, bh, fillc, 1, 3);
        }
        g.set_solid(false);
        g.set_tone(toned);
    }
    g.text_mid(Font::SemiBold, ts(22), 960, 952, ink(0.72f), Align::Center, tr(launch_status_));
    g.set_layer();
}
} // namespace porpoise::ui
