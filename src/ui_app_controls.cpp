/* Porpoise UI - the button-mapping screen: the DualSense, drawn, with the
 * GameCube button each of its controls is, and a list to change them.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings > Controls > Customize buttons, a game's own settings, and the
 * in-game menu's Controls tab all open it. The player keeps four layouts of
 * their own ("My Layout 1".."4", with the global settings); the top row picks
 * which one to edit. Up and down pick a GameCube button; Cross waits for the
 * DualSense control to give it, and the control's old button moves to where
 * the new one was, so every button stays reachable. Changing a button puts
 * that layout to use for whatever opened the screen (every game, or one).
 * The last two rows start the layout over from a ready-made one.
 *
 * The controller is Zacksly's (PS5 Button Icons and Controls, CC BY 3.0). */
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sys/stat.h>

#include "porpoise_move.hpp"
#include "porpoise_debuglog.hpp"
#include "porpoise_pad.hpp"
#include "ui_app.hpp"
#include "porpoise_states.hpp"
#include <ctime>
#include <unistd.h>
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* Rows: the layout to edit, the twelve GameCube inputs, then "start from"
 * the two ready-made layouts. */
constexpr int kSelectRow = 0;
constexpr int kFirstButton = 1;
constexpr int kFromGameCube = kFirstButton + GcCount;
constexpr int kFromPlayStation = kFromGameCube + 1;
constexpr int kMapRows = kFromPlayStation + 1;
constexpr double kCaptureSeconds = 6.0;

/* The controller art (assets/ui/dualsense.png, from Zacksly's drawing by
 * tools/make-controller-art.py) is 1400x900 in these units whatever its
 * pixel size; these are its controls' centres there, in Control order, and
 * where each one's label sits (an offset from the centre). */
constexpr float kArtW = 1400, kArtH = 900;
struct Spot
{
    float x, y, lx, ly;
};
constexpr Spot kSpots[CtlCount] = {
    {1035, 399, 0, 66},     /* Cross */
    {1114, 319, 74, 0},     /* Circle */
    {955, 319, -74, 0},     /* Square */
    {1035, 240, 62, -50},   /* Triangle */
    {372, 140, -190, 22},   /* L1 */
    {1028, 140, 190, 22},   /* R1 */
    {366, 112, -170, -30},  /* L2 */
    {1034, 112, 170, -30},  /* R2 */
    {525, 468, 0, 100},     /* L3 */
    {873, 468, 0, 100},     /* R3 */
    {949, 198, 46, -72},    /* Options */
    {700, 233, 0, 0},       /* Touch pad */
    {363, 250, 0, 0},       /* Up */
    {363, 388, 0, 0},       /* Down */
    {299, 319, 0, 0},       /* Left */
    {428, 319, 0, 0},       /* Right */
};

/* The GameCube buttons: a short label for the chips, a name for the list, and
 * a colour in the spirit of the controller's own. */
struct GcInfo
{
    const char *chip;
    const char *name;
    std::uint32_t colour;
};
constexpr GcInfo kGc[GcCount] = {
    {"A", "A Button", 0x34C77B},     {"B", "B Button", 0xF2536B},    {"X", "X Button", 0xC9D3E6},
    {"Y", "Y Button", 0xC9D3E6},     {"Z", "Z Button", 0x8E6CFF},    {"L", "L Trigger", 0x9AA8C7},
    {"R", "R Trigger", 0x9AA8C7},    {"START", "Start", 0x9AA8C7},   {"", "D-Pad Up", 0x9AA8C7},
    {"", "D-Pad Down", 0x9AA8C7},    {"", "D-Pad Left", 0x9AA8C7},   {"", "D-Pad Right", 0x9AA8C7},
};
/* The arrow's rotation for the D-pad inputs (Glyph::Arrow points up at 0). */
float arrow_rotation(int gc)
{
    switch (gc)
    {
    case GcDown: return kPi;
    case GcLeft: return -kPi * 0.5f;
    case GcRight: return kPi * 0.5f;
    default: return 0;
    }
}

/* How a DualSense control is written on a keycap ("" for the face buttons,
 * which are drawn as their symbols). */
std::string control_label(int c)
{
    switch (c)
    {
    case CtlL1: return "L1";
    case CtlR1: return "R1";
    case CtlL2: return "L2";
    case CtlR2: return "R2";
    case CtlL3: return "L3";
    case CtlR3: return "R3";
    case CtlOptions: return tr("OPTIONS");
    case CtlTouch: return tr("TOUCH PAD");
    case CtlUp: return tr("D-Pad Up");
    case CtlDown: return tr("D-Pad Down");
    case CtlLeft: return tr("D-Pad Left");
    case CtlRight: return tr("D-Pad Right");
    default: return "";
    }
}

Glyph face_glyph(int c)
{
    switch (c)
    {
    case CtlCircle: return Glyph::Circle;
    case CtlSquare: return Glyph::Square;
    case CtlTriangle: return Glyph::Triangle;
    default: return Glyph::Cross;
    }
}

bool is_face(int c)
{
    return c == CtlCross || c == CtlCircle || c == CtlSquare || c == CtlTriangle;
}

std::string layout_name(int layout)
{
    if (layout >= LayoutOwn)
        return trf("My Layout {n}", {{"n", std::to_string(layout - LayoutOwn + 1)}});
    return tr(layout == LayoutPlayStation ? "PlayStation" : "GameCube");
}

/* A DualSense control's own icon, or its name on a keycap without the atlas. */
Icon control_icon(int c)
{
    switch (c)
    {
    case CtlCross: return Icon::Cross;
    case CtlCircle: return Icon::Circle;
    case CtlSquare: return Icon::Square;
    case CtlTriangle: return Icon::Triangle;
    case CtlL1: return Icon::L1;
    case CtlR1: return Icon::R1;
    case CtlL2: return Icon::L2;
    case CtlR2: return Icon::R2;
    case CtlL3: return Icon::L3;
    case CtlR3: return Icon::R3;
    case CtlOptions: return Icon::Options;
    case CtlTouch: return Icon::TouchPad;
    case CtlUp: return Icon::DPadUp;
    case CtlDown: return Icon::DPadDown;
    case CtlLeft: return Icon::DPadLeft;
    case CtlRight: return Icon::DPadRight;
    default: return Icon::Count;
    }
}

std::string control_name(int c)
{
    if (is_face(c))
        return tr(c == CtlCross ? "Cross" : c == CtlCircle ? "Circle" : c == CtlSquare ? "Square" : "Triangle");
    return control_label(c);
}
} // namespace

/* A GameCube button's coloured chip (an arrow for the D-pad). */
void App::draw_gc_chip(int gc, float cx, float cy, float h, bool on)
{
    Gfx &g = *g_;
    if (gc < 0 || gc >= GcCount)
    {
        g.text_mid(Font::Bold, h * 0.6f, cx, cy, with_alpha(kLavender, 0.7f), Align::Center, "\xE2\x80\x93");
        return;
    }
    const std::string label = kGc[gc].chip;
    const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
    g.panel(cx - w * 0.5f, cy - h * 0.5f, w, h, rgba(kGc[gc].colour), 0.8f, h * 0.5f,
            on ? kWhite : rgba(0xFFFFFF, 0.6f), on ? 2.0f : 1.2f);
    if (label.empty())
        g.glyph(Glyph::Arrow, cx, cy, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
    else
        g.text_mid(Font::Bold, h * 0.55f, cx, cy, rgba(0x0A1236), Align::Center, label);
}

float App::gc_chip_width(int gc, float h)
{
    if (gc < 0 || gc >= GcCount || !kGc[gc].chip[0])
        return h;
    return std::max(h, g_->measure(Font::Bold, h * 0.55f, kGc[gc].chip) + h * 0.7f);
}

/* A DualSense control at the right of a row: its icon (Zacksly's), or a keycap. */
void App::draw_control(int control, float right, float cy, bool on, float size)
{
    Gfx &g = *g_;
    const Icon ic = control_icon(control);
    if (g.has_icons() && ic != Icon::Count)
    {
        g.icon(ic, right - size * 0.5f, cy, size, on ? kWhite : rgba(0xDCE6FF));
        return;
    }
    if (is_face(control))
        g.glyph(face_glyph(control), right - 18, cy, size * 0.66f, kWhite);
    else
        draw_keycap(right, cy, control_label(control), on, size * 0.62f);
}

/* ---- opening and saving ------------------------------------------------------------------- */

void App::open_mapping()
{
    const bool game = screen_ == Screen::GameSettings && game_for_;
    map_target_ = game ? &game_ : settings_;
    map_game_ = game ? game_for_ : nullptr;
    map_in_game_ = false;
    map_return_ = game ? Screen::GameSettings : Screen::Main;
    begin_mapping();
    open_screen(Screen::Mapping);
}

void App::open_mapping_in_game()
{
    map_target_ = menu_play_;
    map_game_ = menu_game_;
    map_in_game_ = true;
    begin_mapping();
}

void App::begin_mapping()
{
    /* Edit the layout in use, or the first one. */
    const int own = map_target_ ? map_target_->preset_in_use() : -1;
    map_preset_ = own >= 0 ? own : 0;
    map_wii_slot_ = map_target_ ? std::clamp(map_target_->wii_buttons_use, 0, 2) : 1;
    map_gba_player_ = 0;
    map_gba_slot_ = map_target_ ? std::clamp(map_target_->gba_buttons_use, 0, 2) : 1;
    map_row_ = own >= 0 ? kFirstButton : kSelectRow;
    map_capture_ = false;
    map_note_.clear();
    sfx(Sound::DetailsFlip);
}

void App::close_mapping()
{
    map_capture_ = false;
    sfx(Sound::DetailsFlip);
    if (map_in_game_)
    {
        map_in_game_ = false;
        return;
    }
    if (map_from_ws_)
    {
        /* Back to the Controllers page it was opened from. */
        map_from_ws_ = false;
        map_kind_ = 0;
        ws_step_ = kWsController;
        ws_row_ = 6; /* the page's buttons (kCrActions) */
        open_screen(Screen::WiiSetup);
        wii_setup_apply_pad();
        return;
    }
    if (map_return_ == Screen::GameSettings)
    {
        build_game_settings();
        open_screen(Screen::GameSettings);
    }
    else
    {
        build_settings();
        open_screen(Screen::Main);
    }
}

/* The layouts live with the global settings; which one is in use belongs to
 * whatever opened the screen. */
void App::save_mapping(bool layout_changed, const char *key)
{
    /* The layouts are the global settings'; every copy of the settings that
     * is on screen or in play gets them, so none puts old ones back. */
    for (Settings *other : {map_target_, &game_, &game_base_, menu_play_, &menu_draft_, &menu_base_})
        if (other && other != settings_)
        {
            std::memcpy(other->presets, settings_->presets, sizeof settings_->presets);
            std::memcpy(other->wii_buttons, settings_->wii_buttons, sizeof settings_->wii_buttons);
            std::memcpy(other->wii_buttons2, settings_->wii_buttons2, sizeof settings_->wii_buttons2);
        }
    if (map_game_ && map_target_ != settings_)
    {
        if (layout_changed)
        {
            const std::string path = game_settings_path(*map_game_);
            std::vector<std::string> keys = Settings::keys_in(path);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
            if (map_target_ == &game_)
            {
                /* Saved: what's in effect for the game now, not a change
                 * waiting for Apply (a Discard would undo it on screen only). */
                if (std::find(game_keys_.begin(), game_keys_.end(), key) == game_keys_.end())
                    game_keys_.push_back(key);
                if (std::find(game_keys_base_.begin(), game_keys_base_.end(), key) == game_keys_base_.end())
                    game_keys_base_.push_back(key);
                game_base_.copy_keys(game_, {key});
            }
            mkdir((data_dir_ + "/game-settings").c_str(), 0777);
            /* From what's in effect: Game Settings' own changes wait for
             * Apply, and aren't written by this. */
            (map_target_ == &game_ ? game_base_ : *map_target_).save_keys(path, keys);
        }
    }
    settings_->save(settings_path_);
    if (map_in_game_)
    {
        /* In effect already: the in-game menu's rows show it as it is. */
        menu_draft_.copy_keys(*map_target_, {key});
        menu_base_.copy_keys(*map_target_, {key});
        menu_change_ = key;
    }
    else
        porpoise::pad::set_wii_buttons(map_target_->wii_buttons_in_use()); /* for the guides' pictures */
}

void App::use_preset(int preset)
{
    const bool changed = map_target_->button_layout != LayoutOwn + preset;
    map_target_->button_layout = LayoutOwn + preset;
    save_mapping(changed);
    map_note_ = map_game_ && map_target_ != settings_
                    ? trf("{layout} is on for this game.", {{"layout", layout_name(LayoutOwn + preset)}})
                    : trf("{layout} is on.", {{"layout", layout_name(LayoutOwn + preset)}});
    map_note_time_ = time_;
}

void App::assign_control(int gc, int control)
{
    int *row = settings_->presets[map_preset_];
    const int old = row[gc];
    for (int i = 0; i < GcCount; ++i)
        if (i != gc && row[i] == control)
            row[i] = old; /* the control's old button takes this one's place */
    row[gc] = control;
    use_preset(map_preset_);
    map_note_ = trf("{button} is now on {control}.", {{"button", tr(kGc[gc].name)}, {"control", control_name(control)}});
    map_note_time_ = time_;
}

void App::start_preset_from(int layout)
{
    const Mapping m = porpoise::pad::preset(layout);
    for (int i = 0; i < GcCount; ++i)
        settings_->presets[map_preset_][i] = m.control[i];
    use_preset(map_preset_);
    map_note_ = trf("{layout} now starts from the {base} layout.",
                    {{"layout", layout_name(LayoutOwn + map_preset_)}, {"base", layout_name(layout)}});
    map_note_time_ = time_;
}

/* ---- input --------------------------------------------------------------------------------- */

App::Action App::update_mapping(bool up, bool down, bool left, bool right)
{
    if (map_kind_ == 5) /* kMapKindGba */
        return update_gba_mapping(up, down, left, right);
    if (map_kind_ > 0)
        return update_wii_mapping(up, down, left, right);
    const bool global = map_target_ == settings_;
    const Action changed = global ? Action::SettingsChanged : Action::None;
    if (!map_capture_ && switch_map_kind())
        return Action::None;
    if (map_capture_)
    {
        /* The press that started the capture doesn't count: wait for every
         * button to be let go, then take the first one pressed. */
        const std::uint32_t mask = 0xFFFFu;
        if (time_ - map_capture_start_ > kCaptureSeconds)
        {
            map_capture_ = false;
            map_note_ = tr("Nothing pressed; the button is unchanged.");
            map_note_time_ = time_;
            return Action::None;
        }
        if (!map_armed_)
        {
            map_armed_ = (raw_held_ & mask) == 0;
            if (map_armed_)
                map_capture_start_ = time_; /* the countdown starts once the hands are free */
            return Action::None;
        }
        const std::uint32_t fresh = (raw_held_ & ~raw_prev_) & mask;
        if (fresh)
            for (int c = 0; c < CtlCount; ++c)
                if (fresh & control_bit(c))
                {
                    assign_control(map_row_ - kFirstButton, c);
                    map_capture_ = false;
                    sfx(Sound::LaunchGame);
                    return changed;
                }
        return Action::None;
    }

    if (up && map_row_ > 0)
    {
        --map_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && map_row_ + 1 < kMapRows)
    {
        ++map_row_;
        sfx(Sound::MenuScroll);
    }
    if (map_row_ == kSelectRow && (left || right))
    {
        map_preset_ = (map_preset_ + (left ? Settings::kPresets - 1 : 1)) % Settings::kPresets;
        sfx(Sound::MovingTab);
    }
    if (pressed(BtnCircle))
    {
        close_mapping();
        return Action::None;
    }
    if (pressed(BtnCross))
    {
        if (map_row_ == kSelectRow)
        {
            use_preset(map_preset_);
            sfx(Sound::LaunchGame);
            return changed;
        }
        if (map_row_ >= kFirstButton && map_row_ < kFromGameCube)
        {
            map_capture_ = true;
            map_armed_ = false;
            map_capture_start_ = time_;
            sfx(Sound::DetailsFlip);
        }
        else
        {
            start_preset_from(map_row_ == kFromGameCube ? LayoutGameCube : LayoutPlayStation);
            sfx(Sound::LaunchGame);
            return changed;
        }
    }
    return Action::None;
}

/* ---- drawing ------------------------------------------------------------------------------- */

void App::draw_keycap(float right, float cy, const std::string &label, bool on, float height)
{
    Gfx &g = *g_;
    const float size = height * 0.52f;
    const float w = std::max(height * 1.5f, g.measure(Font::Bold, size, label) + height * 0.8f);
    g.panel(right - w, cy - height * 0.5f, w, height, rgba(0x07102E, on ? 0.65f : 0.45f), 1, kR * 0.8f,
            on ? rgba(0xE8F2FF) : rgba(0xA9B8E8, 0.85f), 1.8f);
    g.text_mid(Font::Bold, size, right - w * 0.5f, cy, kWhite, Align::Center, label);
}

void App::draw_mapping(double time)
{
    Gfx &g = *g_;
    if (!map_target_)
        return;
    if (map_kind_ == 5) /* kMapKindGba */
    {
        draw_gba_mapping(time);
        return;
    }
    if (map_kind_ > 0)
    {
        draw_wii_mapping(time);
        return;
    }
    const Settings &s = *map_target_;
    /* The screen shows the layout being edited, whichever is in use. */
    Mapping m{};
    for (int i = 0; i < GcCount; ++i)
        m.control[i] = static_cast<std::int8_t>(std::clamp(settings_->presets[map_preset_][i], 0, CtlCount - 1));
    const bool for_game = map_game_ && map_target_ != settings_;
    const bool editing_in_use = s.button_layout == LayoutOwn + map_preset_;
    if (map_in_game_)
        g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.78f), 1, 0);

    /* Left: the controller. */
    const float lx = 90, ly = 136, lw = 830, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0A1236, 0.55f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    g.text_mid(Font::Bold, ts(46), lx + 46, ly + 64, kWhite, Align::Left, tr("Buttons"));
    g.text_mid(Font::Regular, ts(26), lx + 46, ly + 112, kLavender, Align::Left,
               fit(g, Font::Regular, ts(26),
                   for_game ? trf("{game}: {layout}", {{"game", map_game_->title}, {"layout", layout_name(s.button_layout)}})
                            : trf("Every game: {layout}", {{"layout", layout_name(s.button_layout)}}),
                   lw - 92));

    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    const float aw = 760, ah = aw * kArtH / kArtW, ax = lx + (lw - aw) * 0.5f, ay = ly + 160;
    const float k = aw / kArtW;
    auto at = [&](float x, float y) { return std::pair<float, float>{ax + x * k, ay + y * k}; };
    if (pad_art_)
        g.image(pad_art_, ax, ay, aw, ah, g.tone(rgba(0xDDEBFF))); /* line art: dark on the light look */

    /* Which GameCube input is on each control. */
    int on_control[CtlCount];
    std::fill(std::begin(on_control), std::end(on_control), -1);
    for (int gc = 0; gc < GcCount; ++gc)
        if (m.control[gc] >= 0 && m.control[gc] < CtlCount)
            on_control[int(m.control[gc])] = gc;
    const int focus_gc = map_row_ >= kFirstButton && map_row_ < kFromGameCube ? map_row_ - kFirstButton : -1;
    const int focus_ctl = focus_gc >= 0 ? m.control[focus_gc] : -1;
    const float pulse = settings_->reduced_motion ? 1.0f : 0.75f + 0.25f * std::sin(float(time) * 5.0f);

    /* The focused control glows; while waiting for a press, every control does. */
    for (int c = 0; c < CtlCount; ++c)
    {
        const bool lit = map_capture_ ? true : c == focus_ctl;
        if (!lit)
            continue;
        const auto [x, y] = at(kSpots[c].x, kSpots[c].y);
        const float r = map_capture_ ? 30 : 46;
        g.blob(x, y, r * 2, r * 2, rgba(0x5CD3FF, (map_capture_ ? 0.35f : 0.75f) * pulse));
    }
    /* A chip on each control with the GameCube button it is. */
    for (int c = 0; c < CtlCount; ++c)
    {
        const int gc = on_control[c];
        if (gc < 0)
            continue;
        const bool dpad_home = gc >= GcUp && int(m.control[gc]) == CtlUp + (gc - GcUp);
        if (dpad_home && gc != focus_gc)
            continue; /* the D-pad as the D-pad needs no labels */
        const auto [x, y] = at(kSpots[c].x + kSpots[c].lx, kSpots[c].y + kSpots[c].ly);
        const bool on = gc == focus_gc;
        const float h = on ? 40 : 34;
        const std::string label = kGc[gc].chip;
        const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
        g.panel(x - w * 0.5f, y - h * 0.5f, w, h, with_alpha(rgba(kGc[gc].colour), on ? 1.0f : 0.92f), 0.8f, h * 0.5f,
                on ? kWhite : rgba(0xFFFFFF, 0.7f), on ? 2.4f : 1.4f, on ? 10 : 0, 0.3f);
        if (label.empty())
            g.glyph(Glyph::Arrow, x, y, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
        else
            g.text_mid(Font::Bold, h * 0.55f, x, y, rgba(0x0A1236), Align::Center, label);
    }
    {
        /* The sticks, with their icons. */
        const float sy = ly + lh - 56;
        if (g.has_icons())
        {
            const std::string a = tr("Control Stick"), b = tr("C-Stick");
            const float wa = g.measure(Font::Regular, ts(24), a), wb = g.measure(Font::Regular, ts(24), b);
            const float total = 44 + 10 + wa + 50 + 44 + 10 + wb;
            float x = lx + (lw - total) * 0.5f;
            g.icon(Icon::LStick, x + 22, sy, 50, kLavender);
            x += 54;
            x += g.text_mid(Font::Regular, ts(24), x, sy, kLavender, Align::Left, a) + 50;
            g.icon(Icon::RStick, x + 22, sy, 50, kLavender);
            g.text_mid(Font::Regular, ts(24), x + 54, sy, kLavender, Align::Left, b);
        }
        else
            g.text_mid(Font::Regular, ts(24), lx + lw * 0.5f, sy, kLavender, Align::Center,
                       tr("Left stick: control stick   \xE2\x80\xA2   Right stick: C-stick"));
    }

    /* Right: the list. */
    const float px = 950, py = 136, pw = 880, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const float row_h = 42, row_x = px + 22, row_w = pw - 44;
    float y = py + 70; /* under the controller tabs */
    for (int i = 0; i < kMapRows; ++i)
    {
        if (i == kFirstButton || i == kFromGameCube)
        {
            g.panel(row_x + 20, y + 8, row_w - 40, 1.5f, rgba(0x3D4F9E, 0.8f), 1, 0);
            y += 18;
        }
        const bool on = i == map_row_;
        const float h_row = i == kSelectRow ? row_h + 12 : row_h;
        const float cy = y + h_row * 0.5f;
        if (on)
            g.panel(row_x, y + 2, row_w, h_row - 4, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float right = row_x + row_w - 22;
        if (i == kSelectRow)
        {
            /* ◀ My layout N ▶, and whether it's the one in use. */
            g.text_mid(Font::SemiBold, ts(27), row_x + 34, cy, on ? kWhite : kSoft, Align::Left, tr("Layout"));
            const std::string name = layout_name(LayoutOwn + map_preset_);
            const float nw = g.measure(Font::Bold, ts(28), name);
            const float ncx = row_x + row_w * 0.5f + 40;
            g.text_mid(Font::Bold, ts(28), ncx, cy, kWhite, Align::Center, name);
            if (on)
            {
                g.glyph(Glyph::Arrow, ncx - nw * 0.5f - 28, cy, 18, kCyan, -kPi * 0.5f);
                g.glyph(Glyph::Arrow, ncx + nw * 0.5f + 28, cy, 18, kCyan, kPi * 0.5f);
            }
            if (editing_in_use)
            {
                const std::string in_use = tr("In Use");
                const float bw = g.measure(Font::Bold, ts(20), in_use) + 30;
                g.panel(right - bw, cy - 16, bw, 32, rgba(0x2FB574, 0.85f), 0.8f, 16, rgba(0xBDF5D8, 0.9f), 1.4f);
                g.text_mid(Font::Bold, ts(20), right - bw * 0.5f, cy, kWhite, Align::Center, in_use);
            }
        }
        else if (i < kFromGameCube)
        {
            /* The GameCube button's chip, its name, and its control. */
            const int gc = i - kFirstButton;
            const float h = 30, cx = row_x + 40;
            const std::string label = kGc[gc].chip;
            const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
            g.panel(cx - w * 0.5f, cy - h * 0.5f, w, h, rgba(kGc[gc].colour), 0.8f, h * 0.5f, rgba(0xFFFFFF, 0.6f), 1.2f);
            if (label.empty())
                g.glyph(Glyph::Arrow, cx, cy, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
            else
                g.text_mid(Font::Bold, h * 0.55f, cx, cy, rgba(0x0A1236), Align::Center, label);
            g.text_mid(Font::SemiBold, ts(27), row_x + 92, cy, on ? kWhite : kSoft, Align::Left, tr(kGc[gc].name));
            if (on && map_capture_)
                g.text_mid(Font::Bold, ts(25), right, cy, kCyan, Align::Right, tr("Press a button\xE2\x80\xA6"));
            else
                draw_control(m.control[gc], right, cy, on, 54);
        }
        else
        {
            const bool from_gc = i == kFromGameCube;
            g.text_mid(Font::SemiBold, ts(26), row_x + 34, cy, on ? kWhite : kSoft, Align::Left,
                       tr(from_gc ? "Start Over from the GameCube Layout" : "Start Over from the PlayStation Layout"));
            g.text_mid(Font::Regular, ts(21), right, cy, with_alpha(kLavender, 0.9f), Align::Right,
                       tr(from_gc ? "Circle A \xE2\x80\xA2 Cross B" : "Cross A \xE2\x80\xA2 Circle B"));
        }
        y += h_row;
    }
    /* A line after a change, fading out. */
    const double since = time_ - map_note_time_;
    if (!map_note_.empty() && since < 4.0)
    {
        const float a = since < 3.0 ? 1.0f : float(4.0 - since);
        g.text_mid(Font::SemiBold, ts(24), px + pw * 0.5f, py + ph - 32, with_alpha(kCyan, a), Align::Center,
                   fit(g, Font::SemiBold, ts(24), map_note_, pw - 60));
    }

    draw_map_tabs();
    if (map_capture_)
    {
        const int left = int(std::ceil(kCaptureSeconds - (time_ - map_capture_start_)));
        draw_prompts({}, {}, trf("Press the DualSense button for {button} ({n})",
                                 {{"button", tr(kGc[map_row_ - kFirstButton].name)},
                                  {"n", std::to_string(std::max(1, left))}}));
    }
    else if (map_row_ == kSelectRow)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Use This Layout"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
    else if (map_row_ < kFromGameCube)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Change"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
    else
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Start Over"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
}

/* ---- a Wii game's controls ------------------------------------------------------------- */

namespace
{
/* A Wii input's chip text and its name in the list. */
struct WiiInfo
{
    const char *chip;
    const char *name;
    std::uint32_t colour;
};
constexpr std::uint32_t kWiiWhite = 0xEEF3FF, kWiiBlue = 0x7FD9FF, kWiiAmber = 0xFFC85C, kWiiGreen = 0x6BE3A8;
constexpr WiiInfo kWiiInfo[WiInputCount] = {
    {"A", "A Button", kWiiBlue},
    {"B", "B, the trigger", kWiiBlue},
    {"1", "1 Button", kWiiWhite},
    {"2", "2 Button", kWiiWhite},
    {"-", "Minus", kWiiWhite},
    {"+", "Plus", kWiiWhite},
    {"HOME", "HOME Button", kWiiWhite},
    {"", "D-Pad Up", kWiiWhite},
    {"", "D-Pad Down", kWiiWhite},
    {"", "D-Pad Left", kWiiWhite},
    {"", "D-Pad Right", kWiiWhite},
    {"SHAKE", "Shakes the Remote", kWiiAmber},
    {"C", "Nunchuk C", kWiiWhite},
    {"Z", "Nunchuk Z", kWiiWhite},
    {"SHAKE", "Shakes the Nunchuk", kWiiAmber},
    {"a", "a button", kWiiBlue},
    {"b", "b button", kWiiBlue},
    {"x", "x button", kWiiWhite},
    {"y", "y button", kWiiWhite},
    {"ZL", "ZL Button", kWiiWhite},
    {"ZR", "ZR Button", kWiiWhite},
    {"L", "L Trigger", kWiiWhite},
    {"R", "R Trigger", kWiiWhite},
    {"CENTER", "Hold: center and level", kWiiGreen},
};
float wii_arrow(int input)
{
    switch (input)
    {
    case WiDown: return kPi;
    case WiLeft: return -kPi * 0.5f;
    case WiRight: return kPi * 0.5f;
    default: return 0;
    }
}
/* A Wii input's name, chip, colour and arrow (the Wii buttons' list). */
const char *wii_input_name(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].name : "";
}
const char *wii_input_chip(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].chip : "";
}
std::uint32_t wii_input_colour(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].colour : kWiiWhite;
}
float wii_input_arrow(int input)
{
    return wii_arrow(input);
}
const char *stick_name(int role)
{
    switch (role)
    {
    case StickDPad: return "D-Pad";
    case StickNunchuk: return "Nunchuk Stick";
    case StickClassicLeft: return "Left Stick";
    case StickClassicRight: return "Right Stick";
    case StickPointer: return "Aims the pointer";
    case StickTilt: return "Tilts the Remote";
    default: return "";
    }
}
const char *wii_controller_name(int controller)
{
    switch (controller)
    {
    case WiiRemote: return "Remote";
    case WiiSideways: return "Remote Sideways";
    case WiiClassic: return "Classic Controller";
    case WiiTwoControllers: return "Two Controllers";
    case WiiGameCube: return "GameCube Controller";
    default: return "Remote + Nunchuk";
    }
}
} // namespace

namespace
{
const char *pose_words(int pose)
{
    switch (pose)
    {
    case PoseTriggerRight: return "Upright in your right hand, the trigger edge toward the TV";
    case PoseTriggerLeft: return "Upright in your left hand, the trigger edge toward the TV";
    case PoseFacingRight: return "Upright in your right hand, its face toward you";
    case PoseFacingLeft: return "Upright in your left hand, its face toward you";
    default: return "Face up, in both hands";
    }
}
} // namespace

/* One DualSense, drawn as held, with each Wii input on its control. (bx, by)
 * is the top left of its box; art_h the art's long side on screen. Returns
 * the box's width and height. */
std::pair<float, float> App::draw_wii_pad(const WiiLayout &lay, int pose, float bx, float by, float art_h,
                                          float alpha)
{
    Gfx &g = *g_;
    const bool right_up = pose == PoseTriggerRight || pose == PoseFacingRight;
    const bool left_up = pose == PoseTriggerLeft || pose == PoseFacingLeft;
    const bool upright = right_up || left_up;
    const float k = art_h / kArtW;
    const float bw = upright ? kArtH * k : kArtW * k, bh = upright ? kArtW * k : kArtH * k;
    /* Art units -> screen: the right grip up is a quarter turn anticlockwise. */
    auto at = [&](float u, float v) -> std::pair<float, float> {
        if (right_up)
            return {bx + v * k, by + (kArtW - u) * k};
        if (left_up)
            return {bx + (kArtH - v) * k, by + u * k};
        return {bx + u * k, by + v * k};
    };
    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    if (pad_art_)
    {
        const Color tint = with_alpha(rgba(0xDDEBFF), alpha);
        if (!upright)
            g.image(pad_art_, bx, by, bw, bh, g.tone(tint));
        else
        {
            const auto tl = at(0, 0), tr_ = at(kArtW, 0), br = at(kArtW, kArtH), bl = at(0, kArtH);
            const Corner c[4] = {{tl.first, tl.second, 1}, {tr_.first, tr_.second, 1}, {br.first, br.second, 1},
                                 {bl.first, bl.second, 1}};
            g.quad3d(pad_art_, c, kArtW * k, kArtH * k, g.tone(tint), 0, false, false);
        }
    }
    const float chip_h = std::clamp(52 * k / 0.48f, 24.0f, 44.0f);
    /* The shoulder buttons sit on the edge, one behind the other: their tags
     * go outside the outline (art units), joined to the button by a line. */
    auto shoulder = [](int c, float &u, float &v) {
        switch (c)
        {
        case CtlL2: u = 300; v = 18; return true;
        case CtlL1: u = 110; v = 128; return true;
        case CtlR2: u = 1100; v = 18; return true;
        case CtlR1: u = 1290; v = 128; return true;
        default: return false;
        }
    };
    for (int i = 0; i < lay.count; ++i)
    {
        const WiiBinding &b = lay.binds[i];
        if (b.control < 0 || b.control >= CtlCount)
            continue;
        float u, v;
        if (shoulder(b.control, u, v))
        {
            const auto [sx, sy] = at(kSpots[b.control].x, kSpots[b.control].y - 14);
            const auto [tx, ty] = at(u, v);
            const float dx = tx - sx, dy = ty - sy, len = std::sqrt(dx * dx + dy * dy);
            if (len > 1)
            {
                const float nx = -dy / len * 1.3f, ny = dx / len * 1.3f;
                const Corner c[4] = {{sx + nx, sy + ny, 1}, {tx + nx, ty + ny, 1}, {tx - nx, ty - ny, 1},
                                     {sx - nx, sy - ny, 1}};
                g.quad3d(nullptr, c, len, 2.6f, with_alpha(rgba(0x8BD9FF), 0.85f * alpha), 0, false, false);
            }
            g.blob(sx, sy, 18, 18, with_alpha(rgba(0x8BD9FF), 0.9f * alpha));
            const WiiInfo &info = kWiiInfo[b.input];
            const std::string label = info.chip;
            const float cw = label.size() <= 1 ? chip_h : g.measure(Font::Bold, chip_h * 0.46f, label) + chip_h * 0.8f;
            const float pw = cw + chip_h + 14;
            g.panel(tx - pw * 0.5f, ty - chip_h * 0.5f - 5, pw, chip_h + 10, with_alpha(rgba(0x13308A), 0.96f * alpha),
                    0.8f, (chip_h + 10) * 0.5f, with_alpha(rgba(0x8BD9FF), 0.9f * alpha), 1.4f);
            if (g.has_icons())
                g.icon(control_icon(b.control), tx - pw * 0.5f + chip_h * 0.5f + 6, ty, chip_h + 4,
                       with_alpha(rgba(0xDCE8FF), alpha));
            wii_chip(b.input, tx + pw * 0.5f - cw * 0.5f - 6, ty, chip_h, alpha);
            continue;
        }
        const auto [cx, cy] = at(kSpots[b.control].x, kSpots[b.control].y);
        wii_chip(b.input, cx, cy, chip_h, alpha);
    }
    /* The sticks: a word just below each, clear of its L3 / R3 chip. */
    auto stick_tag = [&](int role, float u, float v) {
        if (role == StickNothing)
            return;
        const auto [cx, sy] = at(u, v);
        const float cy = sy + chip_h * 1.2f;
        const std::string t = tr(stick_name(role));
        const float tw = g.measure(Font::SemiBold, ts(20), t) + 24;
        g.panel(cx - tw * 0.5f, cy - 16, tw, 32, with_alpha(rgba(0x13308A), 0.94f * alpha), 0.8f, 16,
                with_alpha(rgba(0x8BD9FF), 0.9f * alpha), 1.4f);
        g.text_mid(Font::SemiBold, ts(20), cx, cy, with_alpha(kWhite, alpha), Align::Center, t);
    };
    stick_tag(lay.left_stick, 525, 468);
    stick_tag(lay.right_stick, 873, 468);
    return {bw, bh};
}

/* A Wii input's chip: its letter, an arrow, or a drawn - / +. */
void App::wii_chip(int input, float cx, float cy, float ch, float alpha)
{
    Gfx &g = *g_;
    const WiiInfo &info = kWiiInfo[input];
    const std::string label = info.chip;
    const float cw = label.size() <= 1 ? ch : g.measure(Font::Bold, ch * 0.46f, label) + ch * 0.8f;
    g.panel(cx - cw * 0.5f, cy - ch * 0.5f, cw, ch, with_alpha(rgba(info.colour), 0.97f * alpha), 0.8f, ch * 0.5f,
            with_alpha(kWhite, 0.85f * alpha), 1.6f, 6, 0.35f);
    const Color ink = with_alpha(rgba(0x0A1236), alpha);
    if (label.empty())
        g.glyph(Glyph::Arrow, cx, cy, ch * 0.5f, ink, wii_arrow(input));
    else if (input == WiMinus || input == WiPlus)
    {
        const float bar = ch * 0.40f, th = std::max(2.0f, ch * 0.10f);
        g.panel(cx - bar * 0.5f, cy - th * 0.5f, bar, th, ink, 1, th * 0.5f);
        if (input == WiPlus)
            g.panel(cx - th * 0.5f, cy - bar * 0.5f, th, bar, ink, 1, th * 0.5f);
    }
    else
        g.text_mid(Font::Bold, label.size() == 1 ? ch * 0.62f : ch * 0.46f, cx, cy, ink, Align::Center, label);
}

/* The list beside or below a drawing. Plain buttons go in a grid of
 * "control -> Wii button" pairs; the rest (shake, centring, the D-pad and the
 * sticks) get a line with words. Controls with the same job share an entry. */
float App::draw_wii_list(const WiiLayout &lay, float lx, float ly, float lw, int columns, float alpha)
{
    Gfx &g = *g_;
    struct Entry
    {
        std::vector<Icon> icons;
        int input = -1;
        std::string text;
    };
    std::vector<Entry> plain, worded;
    auto add = [&](Icon icon, int input, const std::string &text) {
        std::vector<Entry> &list = text.empty() ? plain : worded;
        for (Entry &e : list)
            if (e.input == input && e.text == text && e.icons.size() < 3)
            {
                e.icons.push_back(icon);
                return;
            }
        list.push_back({{icon}, input, text});
    };
    bool dpad_listed = false;
    for (int i = 0; i < lay.count; ++i)
    {
        const WiiBinding &b = lay.binds[i];
        const bool arrow_home = b.input >= WiUp && b.input <= WiRight && b.control == CtlUp + (b.input - WiUp);
        if (arrow_home)
        {
            if (!dpad_listed)
                add(Icon::DPad, -1, tr("D-Pad"));
            dpad_listed = true;
            continue;
        }
        const bool simple = b.input <= WiHome || b.input == WiC || b.input == WiZ || (b.input >= WiClA && b.input <= WiClR);
        add(control_icon(b.control), b.input, simple ? std::string() : tr(kWiiInfo[b.input].name));
    }
    if (lay.left_stick != StickNothing)
        add(Icon::LStick, -1, tr(stick_name(lay.left_stick)));
    if (lay.right_stick != StickNothing)
        add(Icon::RStick, -1, tr(stick_name(lay.right_stick)));

    const float icon = 38, ch = 30, row_h = 44;
    auto chip_w = [&](int input) {
        const std::string label = kWiiInfo[input].chip;
        return label.size() <= 1 ? ch : g.measure(Font::Bold, ch * 0.46f, label) + ch * 0.8f;
    };
    auto draw_icons = [&](const Entry &e, float x, float y) {
        for (std::size_t k = 0; k < e.icons.size(); ++k)
        {
            if (k > 0)
            {
                g.text_mid(Font::Regular, ts(20), x - 2, y, with_alpha(kLavender, alpha), Align::Left, "/");
                x += 12;
            }
            if (g.has_icons())
                g.icon(e.icons[k], x + icon * 0.5f, y, icon, with_alpha(rgba(0xDCE8FF), alpha));
            x += icon + 4;
        }
        return x;
    };
    /* The grid: as many pairs to a row as fit. */
    float x = lx, y = ly;
    const float cell = std::max(124.0f, lw / float(std::max(1, columns * 3)));
    for (const Entry &e : plain)
    {
        const float need = float(e.icons.size()) * (icon + 16) + 26 + chip_w(e.input);
        const float take = std::ceil(need / cell) * cell;
        if (x > lx && x + take > lx + lw + 1)
        {
            x = lx;
            y += row_h;
        }
        float cx = draw_icons(e, x, y);
        g.glyph(Glyph::Arrow, cx + 8, y, 12, with_alpha(kLavender, 0.8f * alpha), kPi * 0.5f);
        cx += 22;
        wii_chip(e.input, cx + chip_w(e.input) * 0.5f, y, ch, alpha);
        x += take;
    }
    if (!plain.empty())
        y += row_h;
    /* The worded ones, in columns. */
    const float col_w = lw / float(columns);
    int n = 0;
    for (const Entry &e : worded)
    {
        const int col = n % columns, row = n / columns;
        ++n;
        const float ix = lx + col * col_w, iy = y + row * row_h;
        float tx = draw_icons(e, ix, iy) + 8;
        if (e.input >= 0)
        {
            wii_chip(e.input, tx + chip_w(e.input) * 0.5f, iy, ch, alpha);
            tx += chip_w(e.input) + 12;
        }
        g.text_mid(Font::SemiBold, ts(23), tx, iy, with_alpha(kSoft, alpha), Align::Left,
                   fit(g, Font::SemiBold, ts(23), e.text, ix + col_w - tx - 8));
    }
    return (y - ly) + float((n + columns - 1) / columns) * row_h;
}

void App::draw_wii_controls(float x, float y, float w, float h, const WiiConfig &wii, float alpha, int live_pose,
                            int live_pose_second)
{
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    g.panel(x, y, w, h, rgba(0x0A1236, 0.86f * alpha), 0.85f, kR, with_alpha(rgba(0x3D4F9E), 0.9f * alpha), 1.6f, 0,
            0.10f);
    if (wii.controller == WiiGameCube)
    {
        /* The GameCube's own layout, as in a GameCube game. */
        g.text_mid(Font::Bold, ts(40), x + 40, y + 52, with_alpha(kWhite, alpha), Align::Left,
                   tr(wii_controller_name(wii.controller)));
        const auto lines = wrap(g, Font::Regular, ts(23), tr(kGameCubeOnWiiHelp), w - 80, 3);
        float ly = y + 96;
        for (const std::string &l : lines)
        {
            g.text_mid(Font::Regular, ts(23), x + 40, ly, with_alpha(kLavender, alpha), Align::Left, l);
            ly += 32;
        }
        const float aw = std::min(w - 80.0f, 760.0f);
        const Settings *buttons = ws_in_game_ && menu_play_ ? menu_play_ : settings_;
        draw_controller_lines(x + (w - aw) * 0.5f, ly + 30, aw, buttons->mapping());
        return;
    }
    const bool two = wii.controller == WiiTwoControllers;
    const int pose = live_pose >= 0 ? live_pose : expected_pose(wii, false);
    const std::string auto_word = wii.grip == GripAuto ? tr("Auto: ") : "";
    g.text_mid(Font::Bold, ts(40), x + 40, y + 52, with_alpha(kWhite, alpha), Align::Left,
               tr(wii_controller_name(wii.controller)));

    if (two)
    {
        /* Two DualSenses, mirror images: the Nunchuk on the left, the Remote on the right. */
        const int pose2 = live_pose_second >= 0 ? live_pose_second : expected_pose(wii, true);
        g.text_mid(Font::Regular, ts(23), x + 40, y + 96, with_alpha(kLavender, alpha), Align::Left,
                   fit(g, Font::Regular, ts(23), auto_word + tr("one in each hand, both stood on end"), w - 80));
        const WiiLayout left = wii_layout(wii, true, pose2), right = wii_layout(wii, false, pose);
        const float art_h = std::min(400.0f, h * 0.38f);
        const float pw = kArtH * art_h / kArtW; /* an upright pad's width */
        const float gap = (w - 2 * pw) / 3;
        const float ay = y + 140;
        draw_wii_pad(left, pose2, x + gap, ay, art_h, alpha);
        draw_wii_pad(right, pose, x + 2 * gap + pw, ay, art_h, alpha);
        const float hy = ay + art_h + 30;
        g.text_mid(Font::Bold, ts(24), x + 40, hy, with_alpha(kWhite, alpha), Align::Left, tr("Left hand: the Nunchuk"));
        g.text_mid(Font::Bold, ts(24), x + w * 0.5f + 10, hy, with_alpha(kWhite, alpha), Align::Left,
                   tr("Right hand: the Remote"));
        draw_wii_list(left, x + 40, hy + 50, w * 0.5f - 60, 1, alpha);
        draw_wii_list(right, x + w * 0.5f + 10, hy + 50, w * 0.5f - 50, 1, alpha);
    }
    else
    {
        g.text_mid(Font::Regular, ts(23), x + 40, y + 96, with_alpha(kLavender, alpha), Align::Left,
                   fit(g, Font::Regular, ts(23), auto_word + tr(pose_words(pose)), w - 80));
        const WiiLayout lay = wii_layout(wii, false, pose);
        const bool upright = pose != PoseFlat;
        if (upright)
        {
            const float art_h = std::min(h - 230.0f, 620.0f);
            const bool right_up = pose == PoseTriggerRight || pose == PoseFacingRight;
            const auto [bw, bh] = draw_wii_pad(lay, pose, x + (right_up ? 80 : 30), y + 150, art_h, alpha);
            (void)bh;
            const float lx = x + (right_up ? 80 : 30) + bw + (right_up ? 60 : 140);
            draw_wii_list(lay, lx, y + 160, x + w - 30 - lx, 1, alpha);
        }
        else
        {
            /* (a narrow panel, the Controllers page's: its list takes more rows, so the art is smaller) */
            const float art_w = std::min({w - 120.0f, 680.0f, (h - (w < 800 ? 540.0f : 420.0f)) * kArtW / kArtH});
            const float art_h = art_w; /* the long side */
            const auto [bw, bh] = draw_wii_pad(lay, pose, x + (w - art_w) * 0.5f, y + 130, art_h, alpha);
            (void)bw;
            draw_wii_list(lay, x + 50, y + 130 + bh + 30, w - 100, 2, alpha);
        }
    }

    /* How the motion works. */
    std::string how;
    const bool held = wii.controller != WiiSideways && wii.controller != WiiClassic && wii.controller != WiiGameCube;
    if (wii.pointer == PointerGyro && held)
        how = pose_left_hand(pose) ? tr("Point at the screen to aim. Hold L1 a moment to center the pointer and level the hold.")
                                   : tr("Point at the screen to aim. Hold R1 a moment to center the pointer and level the hold.");
    else if (wii.pointer == PointerTouch && held)
        how = tr("Slide a finger on the touch pad to aim. Flick the controller to shake.");
    else
        how = tr("Tilt and flick the controller: the game feels it.");
    const auto lines = wrap(g, Font::Regular, ts(23), how, w - 80, 2);
    float hy = y + h - 34 - 30 * float(lines.size() - 1);
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(23), x + 40, hy, with_alpha(kLavender, alpha), Align::Left, l);
        hy += 30;
    }
}

/* ---- Settings > Wii Remote > How to hold it ------------------------------------------- */

namespace
{
const char *wii_howto(int controller)
{
    switch (controller)
    {
    case WiiRemote:
        return "Stand the DualSense on end in your right hand with the trigger edge toward the TV: R2 under your "
               "index finger is B, Cross under your thumb is A. Or hold it face up in both hands; with Grip on Auto, "
               "Porpoise reads which. To start, hold it the way you'll play, point at the middle of the "
               "screen and hold R1 for a moment: that hold becomes the Remote held level. Do it again whenever the "
               "pointer wanders.";
    case WiiSideways:
        return "Hold the DualSense face up in both hands, like an NES pad. Tilt it left and right to steer. 1 and 2 "
               "are Square and Cross.";
    case WiiClassic:
        return "Hold it as a normal pad: the buttons are where a Classic Controller has them.";
    case WiiGameCube:
        return kGameCubeOnWiiHelp;
    case WiiTwoControllers:
        return "One DualSense in each hand, both stood on end with their faces toward you. The right one is the "
               "Remote (its grips pointing right): aim with its back. Holding R1 a moment centers the pointer and levels both controllers as you hold them. The left one is the Nunchuk (its "
               "grips pointing left): its stick, C and Z, and its motion. Both feel the game's rumble. The second "
               "controller needs a second signed-in user.";
    default:
        return "Hold the DualSense face up in both hands. The right half is the Remote, the left half the Nunchuk: "
               "the left stick is its stick, L1 is C, L2 is Z. Point the controller at the screen to aim; hold R1 a moment to center.";
    }
}
} // namespace

void App::open_wii_guide()
{
    const bool game = screen_ == Screen::GameSettings && game_for_;
    guide_target_ = game ? &game_ : settings_;
    guide_return_ = game ? Screen::GameSettings : Screen::Main;
    guide_controller_ = std::clamp(guide_target_->wii_controller, 0, int(WiiControllerCount) - 1);
    sfx(Sound::DetailsFlip);
    open_screen(Screen::WiiGuide);
}

App::Action App::update_wii_guide(bool left, bool right)
{
    if (left || right)
    {
        guide_controller_ = (guide_controller_ + (left ? WiiControllerCount - 1 : 1)) % WiiControllerCount;
        sfx(Sound::MovingTab);
    }
    if (pressed(BtnCircle) || pressed(BtnCross))
    {
        sfx(Sound::DetailsFlip);
        if (guide_return_ == Screen::GameSettings)
        {
            build_game_settings();
            open_screen(Screen::GameSettings);
        }
        else
            open_screen(Screen::Main);
    }
    return Action::None;
}

void App::draw_wii_guide(double)
{
    Gfx &g = *g_;
    if (!guide_target_)
        return;
    WiiConfig wii = guide_target_->wii_config(true);
    wii.controller = guide_controller_;
    /* Left: what it is and how to hold it; right: the drawing. */
    const float lx = 90, ly = 136, lw = 750, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    g.text_mid(Font::Bold, ts(46), lx + 44, ly + 64, kWhite, Align::Left, tr("How to Hold It"));
    /* The controller, chosen with left / right. */
    const std::string name = tr(wii_controller_name(guide_controller_));
    const float ny = ly + 140;
    g.panel(lx + 40, ny - 30, lw - 80, 60, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
    g.text_mid(Font::Bold, ts(30), lx + lw * 0.5f, ny, kWhite, Align::Center, name);
    const float nw = g.measure(Font::Bold, ts(30), name);
    g.glyph(Glyph::Arrow, lx + lw * 0.5f - nw * 0.5f - 34, ny, 20, kCyan, -kPi * 0.5f);
    g.glyph(Glyph::Arrow, lx + lw * 0.5f + nw * 0.5f + 34, ny, 20, kCyan, kPi * 0.5f);
    const bool in_use = guide_controller_ == guide_target_->wii_controller;
    g.text_mid(Font::Regular, ts(22), lx + lw * 0.5f, ny + 52, in_use ? kCyan : kLavender, Align::Center,
               in_use ? tr("The Wii controller you have chosen") : tr("Another Wii controller, to look at"));
    const auto lines = wrap(g, Font::Regular, ts(27), tr(wii_howto(guide_controller_)), lw - 90, 16);
    float ty = ny + 120;
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(27), lx + 44, ty, kSoft, Align::Left, l);
        ty += 40;
    }
    {
        const auto beta = wrap(g, Font::Regular, ts(23),
                               tr("Something feels off? Your logs in the debug folder help: send them on the "
                                  "Discord."),
                               lw - 90, 3);
        float by = ly + lh - 40 - 32 * float(beta.size() - 1);
        for (const std::string &l : beta)
        {
            g.text_mid(Font::Regular, ts(23), lx + 44, by, kLavender, Align::Left, l);
            by += 32;
        }
    }
    draw_wii_controls(870, 120, 960, 850, wii, 1.0f);
    draw_prompts({{Glyph::DPad, "Other Wii Controllers"}, {Glyph::Circle, "Back"}}, {}, "");
}

} // namespace porpoise::ui

/* ---- the Wii Remote setup ------------------------------------------------------------------ */

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* What suits a game best, from the first three letters of its disc ID. */
struct Advice
{
    const char *id;
    int controller;
    const char *note;
};
constexpr Advice kAdvice[] = {
    {"RSP", WiiRemote, "Wii Sports: the Remote. For Boxing, two controllers."},
    {"RZT", WiiRemote, "Wii Sports Resort: the Remote (some events want the Nunchuk)."},
    {"RMC", WiiSideways, "Mario Kart Wii: the Remote held sideways; tilt to steer."},
    {"SMN", WiiSideways, "New Super Mario Bros. Wii: the Remote held sideways."},
    {"SUK", WiiSideways, "Kirby's Return to Dream Land: the Remote held sideways."},
    {"R8P", WiiSideways, "Super Paper Mario: the Remote held sideways."},
    {"RMG", WiiRemoteNunchuk, "Super Mario Galaxy: the Remote with the Nunchuk."},
    {"SB4", WiiRemoteNunchuk, "Super Mario Galaxy 2: the Remote with the Nunchuk."},
    {"RZD", WiiRemoteNunchuk, "Twilight Princess: the Remote with the Nunchuk."},
    {"SOU", WiiRemoteNunchuk, "Skyward Sword: the Remote with the Nunchuk."},
    {"RSB", WiiGameCube, "Super Smash Bros. Brawl: the GameCube controller (or the Classic Controller)."},
    {"SX4", WiiClassic, "Xenoblade Chronicles: the Classic Controller."},
};
const Advice *advice_for(const Game *g)
{
    if (!g || g->id.size() < 3)
        return nullptr;
    for (const Advice &a : kAdvice)
        if (g->id.compare(0, 3, a.id) == 0)
            return &a;
    return nullptr;
}
/* The corner targets sit this far toward the corners (the screen's edge is 1). */
constexpr float kCorner = 0.8f;
constexpr float kDeg = 57.2957795f;

bool points(int controller)
{
    return controller != WiiSideways && controller != WiiClassic && controller != WiiGameCube;
}
} // namespace

App::Action App::start_game(Game *g, const std::string &state, bool resume_asked, bool quick)
{
    /* Another console's image (found before this version, or read wrongly
     * once): an error, not a crash in the emulator. */
    if (g && foreign_disc(*g))
    {
        open_dialog(DialogKind::Info, tr("This isn't a GameCube or Wii game"),
                    trf("{file} is a disc image from another console, so Porpoise can't play it.",
                        {{"file", g->file}}),
                    "");
        return Action::None;
    }
    /* Porpoise's Picks first, when the player hasn't made the game their own. */
    if (g)
        auto_apply_picks(*g);
    /* The game's own choice, else the global one. */
    Settings eff = *settings_;
    if (g)
        eff.load(game_settings_path(*g), true);
    /* Quick resume (beta): where the game was left, if it was; ask first. */
    if (g && state.empty() && eff.quick_resume && !resume_asked)
    {
        const std::string resume = porpoise::states::resume_path(Library::key_of(*g));
        struct stat st;
        if (stat(resume.c_str(), &st) == 0 && st.st_size > 0)
        {
            resume_game_ = g;
            open_dialog(DialogKind::Resume, tr("Pick up where you left off?"),
                        relative_time(static_cast<long long>(st.st_mtime), static_cast<long long>(std::time(nullptr))),
                        tr("Resume"));
            dialog_.no = tr("Start Over");
            dialog_.choice = 1; /* Resume first */
            const std::string picture = resume.substr(0, resume.size() - 6) + ".png"; /* resume.state -> resume.png */
            if (access(picture.c_str(), R_OK) == 0)
                dialog_.picture = g_->texture_file(picture);
            return Action::None;
        }
    }
    /* Real Wii Remotes (a DolphinBar) need no DualSense set-up. */
    if (g && g->platform == "Wii" && eff.wii_setup_ask && eff.wii_source == 0 && !quick)
    {
        open_wii_setup(g, true, state);
        return Action::None;
    }
    /* Start Wii Discs in the Wii Menu with the Wii Menu only as its WAD in
     * the games: the core installs it first. */
    if (g && g->platform == "Wii" && g->kind.empty() && eff.wii_menu_boot && !wii_menu_installed())
        if (const Game *w = wii_menu_wad())
            wad_installs_.push_back(w->path);
    launch_ = g;
    launch_state_ = state;
    return Action::Launch;
}

/* The settings the setup shows and changes: the game's (in game), else every
 * game's with the game's own over them. */
Settings &App::ws_settings()
{
    return ws_in_game_ && menu_play_ ? *menu_play_ : ws_eff_;
}

void App::wii_setup_apply_pad()
{
    if (ws_in_game_)
    {
        menu_change_ = "wii_setup"; /* main hands the pad and the core the new set-up */
        return;
    }
    WiiConfig c = ws_eff_.wii_config(false);
    c.controller = ws_controller_;
    c.pointer = PointerGyro;
    c.move = ws_move_ && (ws_controller_ == WiiRemote || ws_controller_ == WiiRemoteNunchuk);
    c.nunchuk_right = c.move && ws_controller_ == WiiRemoteNunchuk && ws_eff_.wii_nunchuk_hand == 1;
    c.navigation = c.move && ws_controller_ == WiiRemoteNunchuk && ws_nav_;
    porpoise::pad::set_wii(c);
}

/* A Wii value kept. The hub's scope: just this game (the game's own settings),
 * or every Wii game (the global settings, and the game's when it has its own
 * value of that key, so the new one shows). everywhere: always every game's. */
void App::ws_store(const char *key, int value, bool everywhere)
{
    porpoise::debuglog::app("hub: %s = %d (%s)", key, value,
                            everywhere || !ws_game_ ? "every game" : ws_scope_ == 0 ? "this game" : "every game");
    const std::string v = std::to_string(value);
    const bool only_game = ws_game_ && ws_scope_ == 0 && !everywhere;
    if (!only_game)
    {
        settings_->set(key, v);
        settings_->save(settings_path_);
    }
    if (ws_in_game_ && menu_play_)
    {
        /* In effect at once; the in-game menu's own copies too, or closing
         * it put the old value back as a change waiting (Oct 25). */
        menu_play_->set(key, v);
        menu_draft_.set(key, v);
        menu_base_.set(key, v);
    }
    ws_eff_.set(key, v);
    ws_dirty_ = true;
    if (ws_game_)
    {
        const std::string path = game_settings_path(*ws_game_);
        std::vector<std::string> keys = Settings::keys_in(path);
        const bool has = std::find(keys.begin(), keys.end(), key) != keys.end();
        if (has || only_game)
        {
            Settings eff = *settings_;
            eff.load(path, true);
            eff.set(key, v);
            if (!has)
                keys.push_back(key);
            mkdir((data_dir_ + "/game-settings").c_str(), 0777);
            eff.save_keys(path, keys);
        }
    }
}

void App::open_wii_setup(Game *g, bool launch, const std::string &state)
{
    ws_game_ = g;
    ws_launch_ = launch;
    ws_state_ = state;
    ws_in_game_ = false;
    ws_recal_only_ = false;
    ws_return_ = screen_ == Screen::States ? Screen::Details : screen_;
    Settings eff = *settings_;
    if (g)
        eff.load(game_settings_path(*g), true);
    ws_eff_ = eff;
    ws_scope_ = g ? 0 : 1; /* from a game: just this game, unless the player says every game */
    ws_controller_ = std::clamp(eff.wii_controller, 0, int(WiiControllerCount) - 1);
    ws_real_ = eff.wii_source == 1;
    ws_move_ = eff.wii_move;
    ws_nav_ = eff.wii_nav;
    ws_ask_ = eff.wii_setup_ask;
    ws_step_ = kWsController;
    ws_row_ = 1; /* the Wii Remote */
    ws_view_ = 0;
    ws_player_ = 0;
    ws_pill_x_ = -1;
    ws_open_ = -1;
    ws_focus_ = 1; /* the main controller */
    ws_sub_ = ws_front_ = 0;
    ws_scroll_ = ws_scroll_to_ = 0;
    ws_dirty_ = false;
    ws_pointing_ = false;
    ws_ptr_a_ = 0;
    ws_pic_ = ws_pic_was_ = -1;
    ws_action_ = 0;
    ws_point_ = 0;
    ws_still_from_ = -1;
    ws_pointing_ = false;
    ws_hold_from_ = -1;
    ws_note_.clear();
    ws_size_mode_ = 0;
    wii_setup_apply_pad();
    sfx(Sound::DetailsFlip);
    porpoise::debuglog::app("hub: opened (%s, controller %d, move %d)", ws_game_ ? ws_game_->title.c_str() : "from Settings",
                            ws_controller_, ws_move_ ? 1 : 0);
    porpoise::debuglog::hub(true);
    porpoise::debuglog::hub_line("# The Controller Hub, a line a frame: t,step,focus,pointing,valid,centred,aim_x,aim_y,"
                                 "rel_yaw,rel_pitch,accel_g,turning; and move,<the Move's readings> lines");
    open_screen(Screen::WiiSetup);
}

void App::open_wii_setup_in_game(bool recalibrate_only)
{
    if (!menu_play_)
        return;
    ws_game_ = menu_game_;
    ws_launch_ = false;
    ws_in_game_ = true;
    ws_recal_only_ = recalibrate_only;
    ws_eff_ = *menu_play_;
    ws_scope_ = 0; /* the game being played: its own */
    ws_controller_ = std::clamp(menu_play_->wii_controller, 0, int(WiiControllerCount) - 1);
    ws_real_ = menu_play_->wii_source == 1;
    ws_move_ = menu_play_->wii_move;
    ws_nav_ = menu_play_->wii_nav;
    ws_view_ = 0;
    ws_player_ = 0;
    ws_pill_x_ = -1;
    ws_open_ = -1;
    ws_focus_ = 1;
    ws_sub_ = ws_front_ = 0;
    ws_scroll_ = ws_scroll_to_ = 0;
    ws_dirty_ = false;
    ws_ptr_a_ = 0;
    ws_pic_ = ws_pic_was_ = -1;
    ws_step_ = recalibrate_only ? (points(ws_controller_) ? kWsReady : kWsHold) : kWsController;
    ws_row_ = 1;
    ws_action_ = 0;
    ws_point_ = 0;
    ws_still_from_ = -1;
    ws_pointing_ = false;
    ws_hold_from_ = -1;
    ws_note_.clear();
    ws_size_mode_ = 0;
    ws_ig_marks_ = 0;
    sfx(Sound::DetailsFlip);
    porpoise::debuglog::app("hub: opened over the game (%s, controller %d, move %d, %s)",
                            ws_game_ ? ws_game_->title.c_str() : "?", ws_controller_, ws_move_ ? 1 : 0,
                            recalibrate_only ? "calibrate" : "all");
    porpoise::debuglog::hub(true);
    porpoise::debuglog::hub_line("# The Controller Hub, a line a frame: t,step,focus,pointing,valid,centred,aim_x,aim_y,"
                                 "rel_yaw,rel_pitch,accel_g,turning; and move,<the Move's readings> lines");
}

void App::close_wii_setup()
{
    ws_hold_from_ = -1;
    porpoise::debuglog::app("hub: closed (controller %d, move %d%s)", ws_controller_, ws_move_ ? 1 : 0,
                            ws_in_game_ ? ", back to the game" : "");
    porpoise::debuglog::hub(false);
    if (ws_in_game_)
    {
        ws_in_game_ = false;
        menu_change_ = "wii_setup";
        return;
    }
    porpoise::pad::set_wii(WiiConfig{});
    if (ws_return_ == Screen::GameSettings)
    {
        build_game_settings();
        open_screen(Screen::GameSettings);
    }
    else
        open_screen(ws_return_ == Screen::WiiSetup ? Screen::Main : ws_return_);
}

namespace
{
/* The calibration's five points: the middle, then the corners (x, y: -1 left / top). */
constexpr int kPtSign[5][2] = {{0, 0}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
constexpr const char *kPtName[5] = {"Middle", "Top left", "Top right", "Bottom right", "Bottom left"};
constexpr double kStillFor = 0.8; /* held still this long, it's taken */
constexpr float kStill = 0.45f;   /* rad/s: still enough */

/* The Controllers page's rows, and its buttons. */
enum
{
    kCrTemplate,
    kCrRemote,
    kCrNunchuk,
    kCrOther,
    kCrSetup,
    kCrAsk,
    kCrActions,
    kCrColor, /* the Move's ball color, and the room's other lights (the right side) */
};
enum
{
    kCaCalibrate,
    kCaSetUp, /* a controller that doesn't point: how to hold it, then level */
    kCaTune,
    kCaButtons,
    kCaTemplate,
    kCaDone,
};
/* Test & Tune's rows. */
enum
{
    kTrFeel,
    kTrSpeed,
    kTrSwing,
    kTrGrip,
    kTrOutput, /* the Move's light and rumble check */
    kTrMeasure,
    kTrTemplate,
    kTrDone,
};
/* Target practice: where the targets come, in turn (the right of the screen). */
constexpr float kTargets[8][2] = {{1250, 430}, {1650, 280}, {1010, 760}, {1580, 740},
                                  {1190, 250}, {1760, 560}, {900, 420},  {1400, 860}};
constexpr float kTargetR = 64;

/* What a Wii controller says about the rows: the Nunchuk (0 off, 1 the same
 * DualSense or the DualSense, 2 a second DualSense; -1 another controller) and
 * the other Wii controllers (0 none, 1 Classic, 2 sideways, 3 GameCube). */
int nunchuk_of(int c)
{
    return c == WiiRemote ? 0 : c == WiiRemoteNunchuk ? 1 : c == WiiTwoControllers ? 2 : -1;
}
/* With the Move: none, the DualSense or the Navigation controller (2). */
int second_of(int c, bool move, bool nav)
{
    if (!move)
        return nunchuk_of(c);
    return c == WiiRemote ? 0 : c == WiiRemoteNunchuk ? (nav ? 2 : 1) : -1;
}
int other_of(int c)
{
    return c == WiiClassic ? 1 : c == WiiSideways ? 2 : c == WiiGameCube ? 3 : 0;
}
/* Feel: Steady, Balanced, Snappy as pointer smoothing. */
int feel_of(int smooth)
{
    return smooth >= 2 ? 0 : smooth == 1 ? 1 : 2;
}
constexpr int kFeelSmooth[3] = {2, 1, 0};
} // namespace

std::vector<int> App::ws_tune_rows() const
{
    std::vector<int> rows;
    if (points(ws_controller_))
    {
        rows = {kTrFeel, kTrSpeed};
        const bool move = ws_move_ && (ws_controller_ == WiiRemote || ws_controller_ == WiiRemoteNunchuk);
        if (move)
        {
            rows.push_back(kTrSwing);
            if (porpoise::move::status().connected)
                rows.push_back(kTrOutput);
        }
        else if (settings_->wii_setup_advanced)
            rows.push_back(kTrGrip);
        rows.push_back(kTrMeasure);
    }
    if (!ws_recal_only_)
        rows.push_back(kTrTemplate);
    rows.push_back(kTrDone);
    return rows;
}

/* The pointer on the setup's pages: where it is, and whether it leads (it moved
 * since the D-pad was last used) and what it's on. */
void App::ws_pointer_step(bool dpad)
{
    const Motion m = porpoise::pad::snapshot(0).motion;
    ws_hover_ = -1;
    if (!m.valid || !m.centred)
    {
        ws_pointing_ = false;
        return;
    }
    const float px = 960 + std::clamp(m.aim_x, -1.05f, 1.05f) * 960;
    const float py = 540 + std::clamp(m.aim_y, -1.05f, 1.05f) * 540;
    ws_px_ = px;
    ws_py_ = py;
    if (dpad)
    {
        /* The D-pad or a stick leads: the pointer steps aside where it is. */
        ws_pointing_ = false;
        ws_nav_time_ = time_;
        ws_rest_x_ = px;
        ws_rest_y_ = py;
    }
    else if (!ws_pointing_)
    {
        /* Back only for a real move: a moment after the D-pad, well away from
         * where it rested, and turning as it goes (a hand settling, or drift,
         * doesn't take it). */
        const float turning = std::sqrt(m.gyro[0] * m.gyro[0] + m.gyro[1] * m.gyro[1] + m.gyro[2] * m.gyro[2]);
        if (time_ - ws_nav_time_ < 0.45)
        {
            ws_rest_x_ = px;
            ws_rest_y_ = py;
        }
        else if (std::hypot(px - ws_rest_x_, py - ws_rest_y_) > 150 && turning > 0.6f)
            ws_pointing_ = true;
    }
    if (ws_pointing_)
        for (std::size_t i = 0; i < ws_hits_.size(); ++i)
        {
            const WsHit &h = ws_hits_[i];
            if (px >= h.x && px < h.x + h.w && py >= h.y && py < h.y + h.h)
                ws_hover_ = int(i);
        }
}

/* A choice on the Controllers page; false when it can't be had (coming soon) or
 * changes nothing. */
bool App::ws_choose(int row, int choice)
{
    const int c = ws_controller_;
    switch (row)
    {
    case kCrTemplate:
    {
        if (choice < 0 || choice >= Settings::kWiiPresets || !settings_->wii_presets[choice].used)
            return false;
        settings_->use_wii_preset(choice);
        settings_->save(settings_path_);
        ws_eff_.use_wii_preset(choice);
        if (ws_in_game_ && menu_play_)
            menu_play_->use_wii_preset(choice);
        ws_controller_ = std::clamp(settings_->wii_controller, 0, int(WiiControllerCount) - 1);
        ws_move_ = settings_->wii_move;
        ws_nav_ = settings_->wii_nav;
        break;
    }
    case kCrRemote:
        /* The main controller: DualSense, PlayStation Move; the Wii Remote and the
         * DualShock 4 are coming soon. */
        if (choice < 0 || choice > 1)
            return false;
        if ((choice == 1) == ws_move_ && !other_of(c))
            return false;
        ws_move_ = choice == 1;
        if (other_of(c) || (ws_move_ && c == WiiTwoControllers))
            ws_controller_ = WiiRemoteNunchuk;
        break;
    case kCrNunchuk:
        /* The second controller: none, the (same or a second) DualSense; with a
         * Move, the DualSense or the Navigation controller (through REXCRAFT88's
         * Move Management). */
        if (choice < 0 || choice > 2)
            return false;
        if (choice == second_of(c, ws_move_, ws_nav_))
            return false;
        if (ws_move_)
        {
            ws_controller_ = choice == 0 ? WiiRemote : WiiRemoteNunchuk;
            ws_nav_ = choice == 2;
            if (ws_nav_)
            {
                ws_note_ = tr("The Navigation controller connects through Move Management by REXCRAFT88, "
                              "which has to be running. Its stick is the Nunchuk's, L1 and L2 are C and Z.");
                ws_note_time_ = time_;
            }
        }
        else
            ws_controller_ = choice == 0 ? WiiRemote : choice == 1 ? WiiRemoteNunchuk : WiiTwoControllers;
        break;
    case kCrOther:
        if (choice < 0 || choice > 3 || choice == other_of(c))
            return false;
        ws_controller_ = choice == 1 ? WiiClassic : choice == 2 ? WiiSideways : choice == 3 ? WiiGameCube
                                                                                           : WiiRemoteNunchuk;
        break;
    case kCrSetup:
    {
        const bool advanced = choice == 1;
        if (choice < 0 || choice > 1 || advanced == settings_->wii_setup_advanced)
            return false;
        settings_->wii_setup_advanced = advanced;
        settings_->save(settings_path_);
        break;
    }
    case kCrColor:
        /* A ball color (it lights at once), or the room's other lights. */
        if (choice >= 0 && choice < porpoise::move::kBallColors)
        {
            if (choice == settings_->move_color)
                return false;
            ws_store("move_color", choice, true);
            settings_->move_color = choice;
            porpoise::move::set_ball_color(choice);
            break;
        }
        if (choice == 8)
        {
            if (porpoise::move::status().camera_state != 4)
            {
                ws_note_ = tr("Plug in the PS3 Eye and turn it on in Settings first.");
                ws_note_time_ = time_;
                return false;
            }
            porpoise::move::ignore_lights();
            ws_note_ = tr("The Move's light goes off a moment: every other light the camera sees now is left out.");
            ws_note_time_ = time_;
            sfx(Sound::MenuScroll);
            return true;
        }
        if (choice == 9)
        {
            porpoise::move::forget_lights();
            ws_note_ = tr("The camera looks everywhere again.");
            ws_note_time_ = time_;
            sfx(Sound::MenuScroll);
            return true;
        }
        return false;
    case kCrAsk:
    {
        if (!ws_game_ || choice < 0 || choice > 1 || (choice == 0) == ws_ask_)
            return false;
        /* This game's own choice (its settings show it too). */
        const std::string path = game_settings_path(*ws_game_);
        Settings eff = *settings_;
        eff.load(path, true);
        eff.wii_setup_ask = choice == 0;
        std::vector<std::string> keys = Settings::keys_in(path);
        if (std::find(keys.begin(), keys.end(), "wii_setup_ask") == keys.end())
            keys.push_back("wii_setup_ask");
        mkdir((data_dir_ + "/game-settings").c_str(), 0777);
        eff.save_keys(path, keys);
        ws_ask_ = eff.wii_setup_ask;
        break;
    }
    default: return false;
    }
    if (!ws_in_game_)
        wii_setup_apply_pad();
    sfx(Sound::MovingTab);
    return true;
}

App::Action App::update_wii_setup(bool up, bool down, bool left, bool right)
{
    if (ws_real_)
    {
        /* Real Wii Remotes: a page to read; back with Circle or Cross, or
         * Triangle for the DualSense's own setup (for when it plays the Wii
         * Remote again). */
        if (pressed(BtnTriangle))
        {
            sfx(Sound::DetailsFlip);
            ws_real_ = false;
            wii_setup_apply_pad();
        }
        else if (pressed(BtnCircle | BtnCross))
        {
            sfx(Sound::DetailsFlip);
            close_wii_setup();
        }
        return Action::None;
    }
    if (ws_in_game_ && !(ws_ig_marks_ & 1))
    {
        ws_ig_marks_ |= 1;
        porpoise::debuglog::app("hub: over the game, first update");
    }
    const Motion m = porpoise::pad::snapshot(0).motion;
    const float turning = std::sqrt(m.gyro[0] * m.gyro[0] + m.gyro[1] * m.gyro[1] + m.gyro[2] * m.gyro[2]);
    const bool pointing = points(ws_controller_);
    const bool advanced = settings_->wii_setup_advanced;
    Settings &t = ws_settings();
    /* The D-pad or a stick: the pointer steps aside until the controller is moved on purpose. */
    const bool stick = std::fabs(stick_x_) > 0.35f || std::fabs(stick_y_) > 0.35f;
    ws_pointer_step(up || down || left || right || stick);
    {
        const float dt = float(std::clamp(time_ - ws_last_t_, 0.0, 0.1));
        ws_last_t_ = time_;
        const float to = ws_pointing_ ? 1.0f : 0.0f;
        ws_ptr_a_ += (to - ws_ptr_a_) * std::min(1.0f, dt * (ws_pointing_ ? 10.0f : 14.0f));
        ws_scroll_ += (ws_scroll_to_ - ws_scroll_) * std::min(1.0f, dt * 12.0f);
        if (std::fabs(ws_scroll_to_ - ws_scroll_) < 0.5f)
            ws_scroll_ = ws_scroll_to_;
    }
    if (porpoise::debuglog::hub_open())
        porpoise::debuglog::hub_line("%.3f,%d,%d,%d,%d,%d,%.4f,%.4f,%.5f,%.5f,%.3f,%.3f", time_, ws_step_, ws_focus_,
                                     ws_pointing_ ? 1 : 0, m.valid ? 1 : 0, m.centred ? 1 : 0, m.aim_x, m.aim_y,
                                     m.rel_yaw, m.rel_pitch,
                                     std::sqrt(m.accel[0] * m.accel[0] + m.accel[1] * m.accel[1] + m.accel[2] * m.accel[2]),
                                     turning);
    if (ws_step_ == kWsController)
        return update_ctl_hub(up, down, left, right);
    const WsHit *hover = ws_hover_ >= 0 && ws_hover_ < int(ws_hits_.size()) ? &ws_hits_[std::size_t(ws_hover_)] : nullptr;
    const bool cross = pressed(BtnCross);
    auto note = [&](const char *text) {
        ws_note_ = tr(text);
        ws_note_time_ = time_;
    };
    auto finish = [&]() -> Action {
        sfx(Sound::LaunchGame);
        const bool launch = ws_launch_ && ws_game_ && !ws_in_game_;
        close_wii_setup();
        if (launch)
        {
            porpoise::pad::set_wii(WiiConfig{});
            launch_ = ws_game_;
            launch_state_ = ws_state_;
            return Action::Launch;
        }
        return ws_in_game_ ? Action::None : Action::SettingsChanged;
    };
    auto start_points = [&]() {
        ws_step_ = kWsReady; /* what's coming first, then Ready */
        ws_point_ = 0;
        ws_still_from_ = -1;
        ws_hold_from_ = -1;
    };
    auto start_tune = [&]() {
        ws_step_ = kWsTry;
        ws_row_ = ws_tune_rows().front();
        ws_target_ = ws_hits_n_ = ws_misses_n_ = 0;
        ws_swing_peak_ = 0;
    };

    /* The Move's light and rumble check, under way: Cross when it lights or buzzes,
     * Circle to stop. */
    if (ws_step_ == kWsTry && porpoise::move::status().check > 0)
    {
        if (cross)
        {
            porpoise::move::output_check_answer();
            note("Got it: the Move keeps that way.");
            sfx(Sound::LaunchGame);
        }
        else if (pressed(BtnCircle))
        {
            porpoise::move::output_check_stop();
            sfx(Sound::DetailsFlip);
        }
        return Action::None;
    }
    if (pressed(BtnCircle))
    {
        sfx(Sound::DetailsFlip);
        ws_hold_from_ = -1;
        ws_still_from_ = -1;
        const bool first = ws_step_ == kWsController ||
                           (ws_recal_only_ && (ws_step_ == kWsPoints || ws_step_ == kWsReady || ws_step_ == kWsCentre ||
                                               ws_step_ == kWsHold));
        if (first)
        {
            close_wii_setup();
            return Action::None;
        }
        if (ws_step_ == kWsTune)
        {
            ws_step_ = ws_tune_from_;
            ws_row_ = ws_step_ == kWsTry ? int(kTrTemplate) : int(kCrActions);
        }
        else if (ws_step_ == kWsTry)
        {
            ws_step_ = ws_recal_only_ ? (pointing ? kWsPoints : kWsCentre) : kWsController;
            ws_row_ = kCrActions;
            ws_point_ = 0;
        }
        else if ((ws_step_ == kWsPoints || ws_step_ == kWsReady || ws_step_ == kWsCentre) && advanced && pointing &&
                 !ws_recal_only_)
            ws_step_ = kWsScreen;
        else
        {
            ws_step_ = kWsController;
            ws_row_ = kCrActions;
        }
        return Action::None;
    }

    switch (ws_step_)
    {
    case kWsHold:
        if (cross)
        {
            ws_row_ = 0;
            ws_hold_from_ = -1;
            if (advanced && pointing && !ws_recal_only_)
                ws_step_ = kWsScreen;
            else if (pointing)
                start_points();
            else
                ws_step_ = kWsCentre;
            sfx(Sound::MenuScroll);
        }
        break;
    case kWsScreen:
    {
        /* Advanced: how to size the pointer - five points, or the screen's size
         * and distance. */
        constexpr int kRows = 4;
        if (up && ws_row_ > 0)
            --ws_row_, sfx(Sound::MenuScroll);
        if (down && ws_row_ + 1 < kRows)
            ++ws_row_, sfx(Sound::MenuScroll);
        const int dir = left ? -1 : right ? 1 : 0;
        if (dir)
        {
            if (ws_row_ == 0)
                ws_size_mode_ = 1 - ws_size_mode_;
            else if (ws_row_ == 1)
                ws_store("wii_size", std::clamp(settings_->wii_size + dir, 10, 150), true);
            else if (ws_row_ == 2)
                ws_store("wii_distance", std::clamp(settings_->wii_distance + dir * 5, 5, 250), true);
            sfx(Sound::MovingTab);
        }
        if (cross)
        {
            if (ws_size_mode_ == 1)
            {
                /* A 16:9 screen: width 0.872 of the diagonal, height 0.490. */
                const float d = float(settings_->wii_distance) * 1.2f; /* tenths of a foot -> inches */
                const float hx = std::atan(float(settings_->wii_size) * 0.8716f * 0.5f / d);
                const float hy = std::atan(float(settings_->wii_size) * 0.4903f * 0.5f / d);
                ws_store("wii_screen_x", int(std::lround(hx * kDeg * 10)), true);
                ws_store("wii_screen_y", int(std::lround(hy * kDeg * 10)), true);
                ws_store("wii_speed", 0, true);
                wii_setup_apply_pad();
                ws_step_ = kWsCentre;
                ws_hold_from_ = -1;
            }
            else
                start_points();
            sfx(Sound::MenuScroll);
        }
        break;
    }
    case kWsCentre:
        /* Hold Cross a moment, still: that hold, pointing at the middle, is level. */
        if ((raw_held_ & BtnCross) && turning < 0.6f)
        {
            if (ws_hold_from_ < 0)
                ws_hold_from_ = time_;
            if (time_ - ws_hold_from_ >= 0.5)
            {
                porpoise::pad::centre_now(0);
                ws_hold_from_ = -1;
                sfx(Sound::LaunchGame);
                start_tune();
            }
        }
        else
            ws_hold_from_ = -1;
        break;
    case kWsReady:
        /* What's coming, full screen: Cross (or the pointer on Ready) begins. */
        if (cross)
        {
            ws_step_ = kWsPoints;
            ws_point_ = 0;
            ws_still_from_ = -1;
            ws_hold_from_ = -1;
            sfx(Sound::MenuScroll);
        }
        break;
    case kWsPoints:
    {
        /* The middle, then the four corners: each taken when the controller is
         * held still on it (or Cross), once it really points that way. */
        if (pressed(BtnSquare))
        {
            ws_point_ = 0;
            ws_still_from_ = -1;
            note("Starting over: the middle first.");
            sfx(Sound::MenuScroll);
            break;
        }
        const int p = std::clamp(ws_point_, 0, 4);
        const bool still = turning < kStill;
        bool aimed = true;
        if (p > 0)
        {
            /* Past a few degrees from the middle, the right way: a pause on the way doesn't count. */
            aimed = m.centred && m.rel_yaw * float(kPtSign[p][0]) > 2.5f / kDeg &&
                    -m.rel_pitch * float(kPtSign[p][1]) > 1.5f / kDeg;
        }
        if (still && aimed)
        {
            if (ws_still_from_ < 0)
                ws_still_from_ = time_;
        }
        else
            ws_still_from_ = -1;
        const bool held = ws_still_from_ >= 0 && time_ - ws_still_from_ >= kStillFor;
        if (cross && !(still && aimed))
            note(p == 0 ? "Hold it still a moment." : "Point at the glowing dot and hold still.");
        if (held || (cross && still && aimed))
        {
            ws_still_from_ = -1;
            if (p == 0)
            {
                porpoise::pad::centre_now(0);
                ws_pts_[0][0] = ws_pts_[0][1] = 0;
            }
            else
            {
                ws_pts_[p][0] = m.rel_yaw;
                ws_pts_[p][1] = m.rel_pitch;
            }
            porpoise::debuglog::app("calibrate: point %d (%s) taken: yaw %.4f pitch %.4f", p + 1, kPtName[p],
                                    double(m.rel_yaw), double(m.rel_pitch));
            if (p < 4)
            {
                ws_point_ = p + 1;
                sfx(Sound::MenuScroll);
                break;
            }
            /* All five: the screen from the corners' spread, its middle from theirs. */
            const float left_yaw = (ws_pts_[1][0] + ws_pts_[4][0]) * 0.5f, right_yaw = (ws_pts_[2][0] + ws_pts_[3][0]) * 0.5f;
            const float top_pitch = (ws_pts_[1][1] + ws_pts_[2][1]) * 0.5f,
                        bottom_pitch = (ws_pts_[3][1] + ws_pts_[4][1]) * 0.5f;
            const float half_x = (right_yaw - left_yaw) / (2 * kCorner), half_y = (top_pitch - bottom_pitch) / (2 * kCorner);
            const bool sane = half_x * kDeg > 3 && half_x * kDeg < 75 && half_y * kDeg > 2 && half_y * kDeg < 60 &&
                              half_y / half_x > 0.25f && half_y / half_x < 1.3f;
            porpoise::debuglog::app("calibrate: screen half %.2f x %.2f degrees, %s", double(half_x * kDeg),
                                    double(half_y * kDeg), sane ? "kept" : "didn't look like a screen");
            if (!sane)
            {
                note("That didn't look like a screen. Let's try again: the middle first.");
                ws_point_ = 0;
                break;
            }
            porpoise::pad::shift_centre(0, (left_yaw + right_yaw) * 0.5f, (top_pitch + bottom_pitch) * 0.5f);
            ws_store("wii_screen_x", int(std::lround(half_x * kDeg * 10)), true);
            ws_store("wii_screen_y", int(std::lround(half_y * kDeg * 10)), true);
            ws_store("wii_speed", 0, true);
            wii_setup_apply_pad();
            sfx(Sound::LaunchGame);
            start_tune();
            return ws_in_game_ ? Action::None : Action::SettingsChanged;
        }
        break;
    }
    case kWsTry:
    {
        /* Test & Tune: the rows on the left, target practice on the right. */
        const std::vector<int> rows = ws_tune_rows();
        auto at = std::find(rows.begin(), rows.end(), ws_row_);
        if (at == rows.end())
            at = rows.begin(), ws_row_ = rows.front();
        const int idx = int(at - rows.begin());
        if (up && idx > 0)
            ws_row_ = rows[std::size_t(idx - 1)], sfx(Sound::MenuScroll);
        if (down && idx + 1 < int(rows.size()))
            ws_row_ = rows[std::size_t(idx + 1)], sfx(Sound::MenuScroll);
        /* The swing meter: how hard the game feels it (g past gravity), its peak held a moment. */
        const float mag = std::sqrt(m.accel[0] * m.accel[0] + m.accel[1] * m.accel[1] + m.accel[2] * m.accel[2]);
        ws_swing_ = std::max(0.0f, mag - 1.0f);
        ws_swing_peak_ = std::max(ws_swing_peak_ * std::pow(0.985f, float(ui_dt_ * 60.0)), ws_swing_);
        auto change = [&](int row, int dir) {
            switch (row)
            {
            case kTrFeel:
            {
                const int f = std::clamp(feel_of(t.wii_smooth) + dir, 0, 2);
                ws_store("wii_smooth", kFeelSmooth[f]);
                break;
            }
            case kTrSpeed: ws_store("wii_reach", std::clamp(t.wii_reach + dir * 10, 60, 160)); break;
            case kTrSwing: ws_store("wii_swing", std::clamp(t.wii_swing + dir * 10, 20, 150)); break;
            case kTrGrip: ws_store("wii_grip", (t.wii_grip + dir + GripCount) % GripCount); break;
            default: return;
            }
            wii_setup_apply_pad();
            sfx(Sound::MovingTab);
        };
        const int dir = left ? -1 : right ? 1 : 0;
        if (dir && !(up || down))
            change(ws_row_, dir);
        auto activate = [&](int row) -> int {
            switch (row)
            {
            case kTrMeasure:
                start_points();
                sfx(Sound::MenuScroll);
                return 0;
            case kTrTemplate:
                ws_tune_from_ = kWsTry;
                ws_step_ = kWsTune;
                ws_row_ = 0;
                ws_preset_slot_ = std::max(0, settings_->wii_preset - 1);
                sfx(Sound::MenuScroll);
                return 0;
            case kTrOutput:
                porpoise::move::output_check_start();
                sfx(Sound::MenuScroll);
                return 0;
            case kTrDone: return 1;
            default: return 0;
            }
        };
        if (pressed(BtnSquare) && pointing)
        {
            start_points();
            sfx(Sound::MenuScroll);
            break;
        }
        if (cross && ws_pointing_)
        {
            if (hover && hover->choice >= 0)
            {
                ws_row_ = hover->row;
                if (hover->row == kTrFeel)
                {
                    ws_store("wii_smooth", kFeelSmooth[std::clamp(hover->choice, 0, 2)]);
                    wii_setup_apply_pad();
                    sfx(Sound::MovingTab);
                }
                else if (hover->row == kTrSpeed || hover->row == kTrSwing || hover->row == kTrGrip)
                    change(hover->row, hover->choice == 0 ? -1 : 1);
                else if (activate(hover->row) == 1)
                    return finish();
            }
            else if (pointing && ws_px_ > 700)
            {
                /* Target practice. */
                const float *tg = kTargets[ws_target_ % 8];
                if (std::hypot(ws_px_ - tg[0], ws_py_ - tg[1]) <= kTargetR + 8)
                {
                    ++ws_hits_n_;
                    ++ws_target_;
                    ws_hit_time_ = time_;
                    sfx(Sound::LaunchGame);
                }
                else
                {
                    ++ws_misses_n_;
                    sfx(Sound::MenuScroll);
                }
            }
        }
        else if (cross && activate(ws_row_) == 1)
            return finish();
        break;
    }
    default: /* kWsTune: Save as Template */
    {
        constexpr int kRows = 4;
        if (up && ws_row_ > 0)
            --ws_row_, sfx(Sound::MenuScroll);
        if (down && ws_row_ + 1 < kRows)
            ++ws_row_, sfx(Sound::MenuScroll);
        const int dir = left ? -1 : right ? 1 : 0;
        const int names = int(Settings::wii_preset_names().size());
        if (dir)
        {
            if (ws_row_ == 0)
                ws_preset_slot_ = (ws_preset_slot_ + dir + Settings::kWiiPresets) % Settings::kWiiPresets;
            else if (ws_row_ == 1)
                ws_preset_name_ = (ws_preset_name_ + dir + names) % names;
            sfx(Sound::MovingTab);
        }
        int row = ws_row_;
        if (cross && ws_pointing_)
            row = hover && hover->choice >= 0 ? hover->row : -1;
        if (cross && row == 2)
        {
            /* Everything as set now, under the name. */
            Settings snapshot = t;
            snapshot.wii_controller = ws_controller_;
            snapshot.wii_move = ws_move_;
            snapshot.wii_nav = ws_nav_;
            settings_->wii_presets[ws_preset_slot_] = {};
            snapshot.save_wii_preset(ws_preset_slot_, ws_preset_name_);
            settings_->wii_presets[ws_preset_slot_] = snapshot.wii_presets[ws_preset_slot_];
            settings_->wii_preset = ws_preset_slot_ + 1;
            settings_->save(settings_path_);
            note("Saved.");
            sfx(Sound::LaunchGame);
        }
        else if (cross && row == 3)
        {
            ws_step_ = ws_tune_from_;
            ws_row_ = ws_step_ == kWsTry ? int(kTrTemplate) : int(kCrActions);
            sfx(Sound::DetailsFlip);
        }
        break;
    }
    }
    return Action::None;
}

void App::draw_wii_setup(double time)
{
    if (ws_real_)
    {
        draw_real_remote_setup(time);
        return;
    }
    Gfx &g = *g_;
    ws_hits_.clear();
    if (ws_in_game_ && !(ws_ig_marks_ & 2))
    {
        ws_ig_marks_ |= 2;
        porpoise::debuglog::app("hub: over the game, first drawn");
    }
    const bool full = ws_step_ == kWsPoints || ws_step_ == kWsReady;
    g.panel(0, 0, 1920, 1080, full ? rgba(0x10173A, 0.97f) : rgba(0x02040C, 0.93f), 1, 0);
    const bool pointing = points(ws_controller_);
    const bool advanced = settings_->wii_setup_advanced;
    const Settings &t = ws_in_game_ && menu_play_ ? *menu_play_ : *settings_;
    const Motion m = porpoise::pad::snapshot(0).motion;
    const float pulse = settings_->reduced_motion ? 1.0f : 0.8f + 0.2f * std::sin(float(time) * 4.0f);
    const bool move = ws_move_ && (ws_controller_ == WiiRemote || ws_controller_ == WiiRemoteNunchuk);

    /* The title, the game, and where we are (the hub draws its own). */
    if (!full && ws_step_ != kWsController)
    {
        const std::string title = ws_step_ == kWsTry    ? tr("Test & Tune")
                                  : ws_step_ == kWsTune ? tr("Save as Template")
                                  : ws_recal_only_      ? tr("Recalibrate the Pointer")
                                                        : tr("Controller Hub");
        g.text_mid(Font::Bold, ts(40), 90, 70, kWhite, Align::Left, title);
        std::string sub = ws_game_ ? ws_game_->title : std::string();
        if (!ws_recal_only_)
            sub += (sub.empty() ? "" : "   \xE2\x80\xA2   ") + (advanced ? tr("Advanced") : tr("Simple"));
        g.text_mid(Font::Regular, ts(24), 90, 112, kLavender, Align::Left, fit(g, Font::Regular, ts(24), sub, 1100));
    }

    auto big_line = [&](const std::string &text, float y, Color c) {
        const auto lines = wrap(g, Font::SemiBold, ts(34), text, 1300, 3);
        for (const std::string &l : lines)
        {
            g.text_mid(Font::SemiBold, ts(34), 960, y, c, Align::Center, l);
            y += 48;
        }
    };
    auto target = [&](float x, float y, float fill) {
        g.blob(x, y, 220, 220, rgba(0x5CD3FF, 0.35f * pulse));
        g.panel(x - 54, y - 54, 108, 108, rgba(0xFFFFFF, 0.0f), 0, 54, rgba(0x8BD9FF, 0.95f), 3.0f);
        g.panel(x - 30, y - 30, 60, 60, rgba(0xFFFFFF, 0.0f), 0, 30, rgba(0xFFFFFF, 0.8f), 2.0f);
        g.panel(x - 9, y - 9, 18, 18, rgba(0xFFFFFF), 1, 9);
        if (fill > 0)
        {
            const float r = 54 * std::min(1.0f, fill);
            g.panel(x - r, y - r, 2 * r, 2 * r, rgba(0x6BE3A8, 0.55f), 0.9f, r);
        }
    };
    auto cursor = [&]() {
        const float a = ws_ptr_a_;
        if (a < 0.01f)
            return;
        g.blob(ws_px_, ws_py_, 90, 90, rgba(0xFFC85C, 0.45f * a));
        g.panel(ws_px_ - 20, ws_py_ - 20, 40, 40, rgba(0xFFFFFF, 0.0f), 0, 20, rgba(0xFFC85C, a), 3.5f);
        g.panel(ws_px_ - 4, ws_py_ - 4, 8, 8, rgba(0xFFC85C, a), 1, 4);
    };
    /* A choice: a big, round target (the pointer can choose it too). */
    auto chip = [&](int row, int choice, float x, float cy, float w, const std::string &label, const std::string &tag,
                    bool chosen, bool enabled, bool focused, float size) {
        const int hit = int(ws_hits_.size());
        ws_hits_.push_back({x, cy - 32, w, 64, row, enabled ? choice : -1});
        const bool hovered = ws_pointing_ && ws_hover_ == hit && enabled;
        const float a = enabled ? 1.0f : 0.45f;
        const Color fill = chosen ? rgba(0x1D45B8, 0.92f) : hovered ? rgba(0x14307E, 0.9f) : rgba(0x0B1747, 0.75f);
        const Color rim = chosen && focused ? kIcy : hovered ? kCyan : chosen ? rgba(0x6F8FF0) : rgba(0x3D4F9E, 0.9f);
        const float bw = chosen && focused ? 2.8f : hovered ? 2.4f : 1.4f;
        g.panel(x, cy - 32, w, 64, with_alpha(fill, a), 0.8f, 32, with_alpha(rim, a), bw, chosen && focused ? 8.0f : 0.0f,
                0.12f);
        const Color ink = with_alpha(chosen || hovered ? kWhite : kSoft, a);
        if (tag.empty())
            g.text_mid(Font::SemiBold, size, x + w * 0.5f, cy, ink, Align::Center, label);
        else
        {
            g.text_mid(Font::SemiBold, size * 0.9f, x + w * 0.5f, cy - 10, ink, Align::Center, label);
            g.text_mid(Font::Bold, ts(15), x + w * 0.5f, cy + 16, rgba(0xFFC85C, 0.95f), Align::Center, tag);
        }
    };
    /* A row of choices from x, as wide as max_w at most (smaller words when it must). */
    auto chips = [&](int row, const std::vector<std::string> &labels, const std::vector<std::string> &tags, int chosen,
                     const std::vector<bool> &enabled, float x, float cy, float max_w, bool focused) {
        float size = ts(24);
        std::vector<float> w(labels.size());
        for (int pass = 0; pass < 2; ++pass)
        {
            float total = 0;
            for (std::size_t i = 0; i < labels.size(); ++i)
            {
                const float lw = g.measure(Font::SemiBold, size * (tags[i].empty() ? 1.0f : 0.9f), labels[i]);
                const float tw = tags[i].empty() ? 0 : g.measure(Font::Bold, ts(15), tags[i]);
                w[i] = std::max(lw, tw) + 52;
                total += w[i] + (i ? 14 : 0);
            }
            if (total <= max_w)
                break;
            size *= max_w / total;
            for (auto &each : w)
                each *= max_w / total;
        }
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            chip(row, int(i), x, cy, w[i], labels[i], tags[i], int(i) == chosen, enabled[i], focused, size);
            x += w[i] + 14;
        }
    };
    /* A controller to choose: a tile with its picture and its name (and "Coming
     * soon" on the ones that aren't here yet). icon: controllers.png's (0
     * DualSense, 1 DualShock 4, 2 Move, 3 Wii Remote, 4 Classic, 5 GameCube),
     * 13 the Remote turned on its side, -1 none (Off). */
    ctl_art(-1, 0, 0, 0, kClear); /* (loads the pictures) */
    auto tiles = [&](int row, const std::vector<std::string> &labels, const std::vector<std::string> &tags,
                     const std::vector<int> &icons, int chosen, const std::vector<bool> &enabled, float x, float top,
                     float max_w, float th, bool focused) {
        const int n = int(labels.size());
        const float gap = 14, tw = (max_w - gap * float(n - 1)) / float(n);
        for (int i = 0; i < n; ++i)
        {
            const float tx = x + float(i) * (tw + gap);
            const int hit = int(ws_hits_.size());
            ws_hits_.push_back({tx, top, tw, th, row, enabled[std::size_t(i)] ? i : -1});
            const bool on = i == chosen, ok = enabled[std::size_t(i)];
            const bool hovered = ws_pointing_ && ws_hover_ == hit && ok;
            const float a = ok ? 1.0f : 0.42f;
            const Color fill = on ? rgba(0x1D45B8, 0.92f) : hovered ? rgba(0x14307E, 0.9f) : rgba(0x0B1747, 0.70f);
            const Color rim = on && focused ? kIcy : hovered ? kCyan : on ? rgba(0x6F8FF0) : rgba(0x3D4F9E, 0.8f);
            g.panel(tx, top, tw, th, with_alpha(fill, a), 0.8f, 22, with_alpha(rim, a), on && focused ? 2.8f : 1.4f,
                    on && focused ? 8.0f : 0.0f, 0.12f);
            const Color ink = with_alpha(on || hovered ? kWhite : kSoft, a);
            const float icon_cy = top + th * 0.40f, icon = std::min(th * 0.66f, tw * 0.6f);
            const int which = icons[std::size_t(i)];
            if (which >= 0 && gl_ctl_)
            {
                if (which == 13)
                {
                    /* The Remote held sideways: its picture turned a quarter. */
                    const float hs = icon * 0.5f;
                    /* (its top to the left: the D-pad under your left thumb) */
                    const Corner q[4] = {{tx + tw * 0.5f - hs, icon_cy + hs, 1}, {tx + tw * 0.5f - hs, icon_cy - hs, 1},
                                         {tx + tw * 0.5f + hs, icon_cy - hs, 1}, {tx + tw * 0.5f + hs, icon_cy + hs, 1}};
                    const float uv[4] = {3.0f / 10.0f, 0.0f, 4.0f / 10.0f, 1.0f};
                    g.quad3d(gl_ctl_, q, icon, icon, g.toned() ? g.tone(ink) : ink, 0.01f, false, false, uv);
                }
                else
                {
                    ctl_art(which, tx + tw * 0.5f, icon_cy, icon, ink);
                    if (which == 2)
                        draw_move_ball(tx + tw * 0.5f, icon_cy, icon, a); /* the Move: its ball in its color */
                }
            }
            else
                g.panel(tx + tw * 0.5f - 18, icon_cy - 2, 36, 4, ink, 1, 2); /* Off: a dash */
            const float ly = top + th * 0.83f;
            const std::string label = fit(g, Font::SemiBold, ts(20), labels[std::size_t(i)], tw - 16);
            if (tags[std::size_t(i)].empty())
                g.text_mid(Font::SemiBold, ts(20), tx + tw * 0.5f, ly, ink, Align::Center, label);
            else
            {
                g.text_mid(Font::SemiBold, ts(19), tx + tw * 0.5f, ly - 9, ink, Align::Center, label);
                g.text_mid(Font::Bold, ts(14), tx + tw * 0.5f, ly + 14, rgba(0xFFC85C, 0.95f), Align::Center,
                           fit(g, Font::Bold, ts(14), tags[std::size_t(i)], tw - 16));
            }
        }
    };
    auto prompts_for = [&](std::vector<std::pair<Glyph, std::string>> p) { draw_prompts(p, {}, ""); };

    WiiConfig cfg = t.wii_config(false);
    cfg.controller = ws_controller_;
    switch (ws_step_)
    {
    case kWsController:
        draw_ctl_hub(time);
        return;
    case kWsHold:
    {
        const float lx = 90, ly = 160, lw = 750, lh = 780;
        g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
        g.text_mid(Font::Bold, ts(34), lx + 44, ly + 56, kWhite, Align::Left, tr("Pick it up like this"));
        float ty = ly + 120;
        auto para = [&](const std::string &text, Color col) {
            for (const std::string &l : wrap(g, Font::Regular, ts(26), text, lw - 90, 10))
            {
                g.text_mid(Font::Regular, ts(26), lx + 44, ty, col, Align::Left, l);
                ty += 38;
            }
            ty += 14;
        };
        para(tr(pose_words(expected_pose(cfg, false))) + ".", kSoft);
        if (m.valid && pointing)
            para(tr("Porpoise sees: ") + tr(pose_words(m.pose)) + ".", rgba(0x6BE3A8));
        if (ws_controller_ == WiiTwoControllers)
        {
            const bool second = porpoise::pad::connected(1);
            para(second ? tr("Second controller: on.")
                        : tr("Second controller: not found. Sign in a second user and turn their controller on."),
                 second ? rgba(0x6BE3A8) : rgba(0xFFC85C));
        }
        para(tr("Porpoise reads how you hold it, so tilts, swings and the pointer come out the right way."),
             with_alpha(kLavender, 0.85f));
        para(tr("Then press Cross."), kLavender);
        const porpoise::pad::Motion m2 = porpoise::pad::snapshot(1).motion;
        draw_wii_controls(870, 160, 960, 780, cfg, 1.0f, m.valid ? m.pose : -1, m2.valid ? m2.pose : -1);
        prompts_for({{Glyph::Cross, "Next"}, {Glyph::Circle, "Back"}});
        break;
    }
    case kWsScreen:
    {
        const float lx = 90, ly = 160, lw = 750, lh = 780;
        g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
        g.text_mid(Font::Bold, ts(34), lx + 44, ly + 56, kWhite, Align::Left, tr("Your screen"));
        float ty = ly + 120;
        for (const std::string &l :
             wrap(g, Font::Regular, ts(26),
                  tr("The pointer is sized to your screen and how far you sit. Pointing at five points measures both "
                     "at once; or give the size and the distance."),
                  lw - 90, 10))
        {
            g.text_mid(Font::Regular, ts(26), lx + 44, ty, kSoft, Align::Left, l);
            ty += 38;
        }
        auto row = [&](int i, const std::string &label, const std::string &value, float x, float y, float w) {
            const bool on = i == ws_row_;
            if (on)
                g.panel(x, y - 28, w, 56, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
            g.text_mid(Font::SemiBold, ts(27), x + 28, y, on ? kWhite : kSoft, Align::Left, label);
            if (!value.empty())
                g.text_mid(Font::Bold, ts(27), x + w - 28, y, on ? kWhite : kLavender, Align::Right, value);
        };
        float ry = ty + 44;
        const float rx = lx + 30, rw = lw - 60;
        row(0, tr("Size the pointer by"), ws_size_mode_ ? tr("Size and Distance") : tr("Pointing at Five Points"), rx,
            ry, rw);
        ry += 64;
        if (ws_size_mode_)
        {
            row(1, tr("Screen Size"), std::to_string(settings_->wii_size) + "\"", rx, ry, rw);
            ry += 64;
            char d[48];
            std::snprintf(d, sizeof d, "%.1f ft (%.1f m)", settings_->wii_distance / 10.0,
                          settings_->wii_distance * 0.03048);
            row(2, tr("Distance to it"), d, rx, ry, rw);
            ry += 64;
        }
        row(3, ws_size_mode_ ? tr("Next: center") : tr("Next: the five points"), "", rx, ry, rw);
        draw_wii_controls(870, 160, 960, 780, cfg, 1.0f, m.valid ? m.pose : -1);
        prompts_for({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Next"}, {Glyph::Circle, "Back"}});
        break;
    }
    case kWsCentre:
    {
        const float fill = ws_hold_from_ >= 0 ? float((time_ - ws_hold_from_) / 0.5) : 0.0f;
        target(960, 540, fill);
        big_line(tr("Hold it the way you'll play and point at the dot."), 760, kWhite);
        big_line(tr("Then hold Cross, keeping still."), 830, kLavender);
        if (ws_controller_ == WiiTwoControllers)
            big_line(tr("Hold the second controller as you'll play, too."), 900, kLavender);
        prompts_for({{Glyph::Cross, "Hold to Center"}, {Glyph::Circle, ws_recal_only_ ? "Close" : "Back"}});
        break;
    }
    case kWsReady:
    {
        /* Full screen: where the five points will be, what's coming, and Ready. */
        for (int i = 0; i < 5; ++i)
        {
            const float x = 960 + float(kPtSign[i][0]) * kCorner * 960, y = 540 + float(kPtSign[i][1]) * kCorner * 540;
            g.panel(x - 26, y - 26, 52, 52, rgba(0xFFFFFF, 0.0f), 0, 26, rgba(0x5A66A0), 5.0f);
            g.text_mid(Font::Bold, ts(20), x, y + (kPtSign[i][1] < 0 ? 52 : i == 0 ? 52 : -52),
                       with_alpha(kLavender, 0.8f), Align::Center, std::to_string(i + 1));
        }
        g.text_mid(Font::Bold, ts(46), 960, 250, kWhite, Align::Center, tr("Five points, the whole screen"));
        big_line(move ? tr("Sit where you'll play and hold the Move the way you'll play, its light toward the screen.")
                      : tr("Sit where you'll play and hold the controller the way you'll play."),
                 330, kSoft);
        big_line(tr("Point at the middle first, then each corner. Hold still on each one and it's taken by itself."),
                 400, kLavender);
        {
            const std::string ready = tr("Ready");
            const float w = std::max(260.0f, g.measure(Font::Bold, ts(32), ready) + 120), x = 960 - w * 0.5f, cy = 690;
            const int hit = int(ws_hits_.size());
            ws_hits_.push_back({x, cy - 40, w, 80, 0, 0});
            const bool hovered = ws_pointing_ && ws_hover_ == hit;
            g.blob(960, cy, w + 120, 200, rgba(0x5CD3FF, 0.22f * pulse));
            g.panel(x, cy - 40, w, 80, hovered ? rgba(0x2A5BE0) : rgba(0x1D45B8, 0.95f), 0.85f, 40, kIcy, 3.0f, 8.0f,
                    0.15f);
            g.text_mid(Font::Bold, ts(32), 960, cy, kWhite, Align::Center, ready);
        }
        cursor();
        prompts_for({{Glyph::Cross, "Ready"}, {Glyph::Circle, ws_recal_only_ ? "Close" : "Back"}});
        break;
    }
    case kWsPoints:
    {
        /* Full screen: the five points, the one to point at glowing, the done ones ticked. */
        const int p = std::clamp(ws_point_, 0, 4);
        const float fill = ws_still_from_ >= 0 ? float((time_ - ws_still_from_) / kStillFor) : 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float x = 960 + float(kPtSign[i][0]) * kCorner * 960, y = 540 + float(kPtSign[i][1]) * kCorner * 540;
            if (i == p)
                target(x, y, fill);
            else if (i < p)
            {
                g.panel(x - 26, y - 26, 52, 52, rgba(0x2E9E5B), 1, 26);
                draw_check_mark(x, y, 30, true);
            }
            else
                g.panel(x - 26, y - 26, 52, 52, rgba(0xFFFFFF, 0.0f), 0, 26, rgba(0x5A66A0), 5.0f);
        }
        /* The instruction, small, at the top: its ring fills as it's held. */
        {
            const std::string line = tr("Point at the glowing dot and hold still");
            const std::string sub = trf("{where} \xC2\xB7 {n} of 5 \xC2\xB7 it captures by itself",
                                        {{"where", tr(kPtName[p])}, {"n", std::to_string(p + 1)}});
            const float tw = std::max(g.measure(Font::Bold, ts(28), line), g.measure(Font::Regular, ts(20), sub));
            const float w = tw + 130, x = 960 - w * 0.5f, y = 40;
            g.panel(x, y, w, 96, rgba(0xF6F7FC, 0.96f), 1, 48);
            const float rcx = x + 52, rcy = y + 48;
            g.panel(rcx - 24, rcy - 24, 48, 48, rgba(0xFFFFFF, 0.0f), 0, 24, rgba(0xE3E7F3), 6.0f);
            const float r = 24 * std::clamp(fill, 0.0f, 1.0f);
            if (r > 1)
                g.panel(rcx - r, rcy - r, 2 * r, 2 * r, rgba(0x17A6C9), 1, r);
            g.text_mid(Font::Bold, ts(28), x + 96, y + 34, rgba(0x26315E), Align::Left, line);
            g.text_mid(Font::Regular, ts(20), x + 96, y + 66, rgba(0x4F5B8C), Align::Left, sub);
        }
        /* What the PS3 Eye sees, while it's on: the ball should stay in it. */
        if (move && porpoise::move::status().camera_state == 4)
            draw_eye_view(960 - 128, 790, 256, 192, 1.0f);
        prompts_for({{Glyph::Cross, "Take This Point"}, {Glyph::Square, "Start Over"},
                     {Glyph::Circle, ws_recal_only_ ? "Close" : "Back"}});
        break;
    }
    case kWsTry:
    {
        if (!pointing)
        {
            big_line(tr("All set."), 420, kWhite);
            big_line(tr("Tilt and flick the controller: the game feels it."), 490, kLavender);
        }
        /* The rows, on the left. */
        const float lx = 70, ly = 160, lw = 600, lh = 800;
        g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.7f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
        const std::vector<int> rows = ws_tune_rows();
        float y = ly + 56;
        const float cx0 = lx + 36, cw = lw - 72;
        const bool focusing = !ws_pointing_;
        static const char *const kGrips[] = {"Auto", "Both Hands", "Upright, trigger to the TV", "Upright, facing you"};
        auto heading = [&](int row, const std::string &name, const std::string &value, float hy) {
            const bool on = focusing && ws_row_ == row;
            if (on)
                g.panel(lx + 12, hy - 14, 6, 88, kCyan, 1, 3);
            g.text_mid(Font::Bold, ts(25), cx0, hy, on ? kWhite : kSoft, Align::Left, name);
            if (!value.empty())
                g.text_mid(Font::Bold, ts(25), cx0 + cw, hy, on ? kWhite : kLavender, Align::Right, value);
        };
        /* A value with a minus and a plus either side of its bar. */
        auto slider = [&](int row, float sy, float frac) {
            const bool on = focusing && ws_row_ == row;
            chip(row, 0, cx0, sy, 64, "-", "", false, true, on, ts(34));
            chip(row, 1, cx0 + cw - 64, sy, 64, "+", "", false, true, on, ts(34));
            const float bx = cx0 + 84, bw = cw - 168;
            g.panel(bx, sy - 6, bw, 12, rgba(0x0B1747, 0.9f), 1, 6, rgba(0x3D4F9E), 1.2f);
            g.panel(bx, sy - 6, std::max(12.0f, bw * std::clamp(frac, 0.0f, 1.0f)), 12, on ? kCyan : rgba(0x6F8FF0), 1,
                    6);
        };
        for (int row : rows)
        {
            switch (row)
            {
            case kTrFeel:
                heading(row, tr("Feel"), "", y);
                chips(row, {tr("Steady"), tr("Balanced"), tr("Snappy")}, {"", "", ""}, feel_of(t.wii_smooth),
                      {true, true, true}, cx0, y + 48, cw, focusing && ws_row_ == row);
                y += 124;
                break;
            case kTrSpeed:
                heading(row, tr("Pointer speed"), std::to_string(t.wii_reach) + "%", y);
                slider(row, y + 48, float(t.wii_reach - 60) / 100.0f);
                y += 124;
                break;
            case kTrSwing:
                heading(row, tr("Swing strength"), std::to_string(t.wii_swing) + "%", y);
                slider(row, y + 48, float(t.wii_swing - 20) / 130.0f);
                y += 124;
                break;
            case kTrGrip:
                heading(row, tr("Grip"), tr(kGrips[std::clamp(t.wii_grip, 0, 3)]), y);
                slider(row, y + 48, float(std::clamp(t.wii_grip, 0, 3)) / 3.0f);
                y += 124;
                break;
            default:
            {
                const porpoise::move::Status ms = porpoise::move::status();
                const std::string label = row == kTrOutput
                                              ? (ms.check_result > 0
                                                     ? trf("Light & rumble: way {n} works", {{"n", std::to_string(ms.check_result)}})
                                                     : ms.check_result < 0 ? tr("Light & rumble: none worked, send the log")
                                                                           : tr("Light & rumble check"))
                                          : row == kTrMeasure  ? tr("Calibrate again")
                                          : row == kTrTemplate ? tr("Save as template")
                                                               : (ws_launch_ ? tr("Play")
                                                                             : ws_in_game_ ? tr("Back to the Game")
                                                                                           : tr("Done"));
                const bool on = focusing && ws_row_ == row;
                chip(row, 0, cx0, y + 20, cw, label, "", on, true, on, ts(25));
                y += 84;
                break;
            }
            }
        }
        if (pointing)
        {
            /* Target practice. */
            const float *tg = kTargets[ws_target_ % 8];
            const float pop = float(std::clamp((time_ - ws_hit_time_) / 0.25, 0.0, 1.0));
            g.blob(tg[0], tg[1], 230, 230, rgba(0xFF6B8A, 0.25f * pulse));
            g.panel(tg[0] - kTargetR, tg[1] - kTargetR, 2 * kTargetR, 2 * kTargetR, rgba(0xFF6B8A, 0.18f), 1, kTargetR,
                    rgba(0xFF9DB0), 3.0f);
            g.panel(tg[0] - kTargetR * 0.55f, tg[1] - kTargetR * 0.55f, kTargetR * 1.1f, kTargetR * 1.1f,
                    rgba(0xFFFFFF, 0.0f), 0, kTargetR * 0.55f, rgba(0xFFFFFF, 0.85f), 2.5f);
            g.panel(tg[0] - 8, tg[1] - 8, 16, 16, rgba(0xFFFFFF), 1, 8);
            if (pop < 1)
            {
                const float *was = kTargets[(ws_target_ + 7) % 8];
                const float r = kTargetR * (1.0f + pop);
                g.panel(was[0] - r, was[1] - r, 2 * r, 2 * r, rgba(0xFFFFFF, 0.0f), 0, r, rgba(0x6BE3A8, 1.0f - pop), 4);
            }
            g.text_mid(Font::Bold, ts(28), 1270, 170, kWhite, Align::Center,
                       trf("Hits {h}   \xC2\xB7   Misses {m}",
                           {{"h", std::to_string(ws_hits_n_)}, {"m", std::to_string(ws_misses_n_)}}));
            g.text_mid(Font::Regular, ts(22), 1270, 210, kLavender, Align::Center,
                       move ? tr("Point at the target and press the Move button") : tr("Point at the target and press Cross"));
            /* The swing meter, at the bottom. */
            const float mx = 760, mw = 1000, my = 940;
            g.text_mid(Font::Bold, ts(22), mx, my - 36, kSoft, Align::Left, tr("Swing"));
            const float span = 4.0f; /* g past gravity across the bar */
            g.panel(mx, my - 9, mw, 18, rgba(0x0B1747, 0.9f), 1, 9, rgba(0x3D4F9E), 1.2f);
            const float now_w = mw * std::clamp(ws_swing_ / span, 0.0f, 1.0f);
            if (now_w > 2)
                g.panel(mx, my - 9, now_w, 18, rgba(0x6BE3A8), 1, 9);
            const float px = mx + mw * std::clamp(ws_swing_peak_ / span, 0.0f, 1.0f);
            g.panel(px - 3, my - 16, 6, 32, rgba(0xFFC85C), 1, 3);
            const char *const kZones[] = {"Light", "Medium", "Strong"};
            for (int z = 0; z < 3; ++z)
                g.text_mid(Font::Regular, ts(18), mx + mw * (z + 0.5f) / 3.0f, my + 30, with_alpha(kLavender, 0.85f),
                           Align::Center, tr(kZones[z]));
            const char *verdict = ws_swing_peak_ < 0.4f ? "" : ws_swing_peak_ < 1.3f ? "Light" : ws_swing_peak_ < 2.6f ? "Medium" : "Strong";
            if (*verdict)
                g.text_mid(Font::Bold, ts(22), mx + mw, my - 36, rgba(0xFFC85C), Align::Right,
                           trf("Last swing: {what}", {{"what", tr(verdict)}}));
            cursor();
        }
        {
            const porpoise::move::Status ms = porpoise::move::status();
            if (ms.check > 0)
            {
                /* The check, over everything: which way, and what to do. */
                g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.72f), 1, 0);
                const float bw = 1000, bh = 300, bx = 960 - bw * 0.5f, by = 390;
                g.panel(bx, by, bw, bh, rgba(0x0F1F63, 0.95f), 0.8f, kR, kIcy, 2.0f, 8, 0.15f);
                g.text_mid(Font::Bold, ts(36), 960, by + 70, kWhite, Align::Center,
                           trf("Check {n} of {all}", {{"n", std::to_string(ms.check)}, {"all", std::to_string(ms.check_ways)}}));
                g.text_mid(Font::Regular, ts(26), 960, by + 135, kSoft, Align::Center,
                           tr("Watch the ball and feel the Move."));
                g.text_mid(Font::SemiBold, ts(26), 960, by + 180, kWhite, Align::Center,
                           tr("Press Cross the moment it turns green or buzzes."));
                g.text_mid(Font::Regular, ts(22), 960, by + 240, kLavender, Align::Center,
                           tr("Nothing happens? Wait: the next way comes by itself."));
                draw_prompts({{Glyph::Cross, "It Lit Up or Buzzed"}, {Glyph::Circle, "Stop"}}, {}, "");
                break;
            }
        }
        std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::DPad, "Change"}, {Glyph::Cross, "Select"}};
        if (pointing)
            prompts.push_back({Glyph::Square, "Calibrate Again"});
        prompts.push_back({Glyph::Circle, "Back"});
        prompts_for(prompts);
        break;
    }
    default: /* kWsTune: Save as Template */
    {
        const float lx = 90, ly = 160, lw = 900, lh = 640;
        g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
        float ty = ly + 60;
        for (const std::string &l :
             wrap(g, Font::Regular, ts(26),
                  tr("A template keeps the whole setup: the controllers, the pointer's calibration, feel, speed and swing "
                     "strength. Pick it on the Controllers page next time."),
                  lw - 90, 4))
        {
            g.text_mid(Font::Regular, ts(26), lx + 44, ty, kSoft, Align::Left, l);
            ty += 38;
        }
        const auto &names = Settings::wii_preset_names();
        const Settings::WiiPreset &slot = settings_->wii_presets[ws_preset_slot_];
        const float rx = lx + 30, rw = lw - 60;
        float ry = ty + 50;
        const bool focusing = !ws_pointing_;
        auto row = [&](int i, const std::string &label, const std::string &value) {
            const bool on = focusing && i == ws_row_;
            ws_hits_.push_back({rx, ry - 30, rw, 60, i, i});
            const bool hovered = ws_pointing_ && ws_hover_ == int(ws_hits_.size()) - 1;
            if (on || hovered)
                g.panel(rx, ry - 30, rw, 60, rgba(0x1D45B8, on ? 0.88f : 0.5f), 0.7f, 30, on ? kIcy : kCyan, 2.2f, 8,
                        0.18f);
            g.text_mid(Font::SemiBold, ts(27), rx + 30, ry, on || hovered ? kWhite : kSoft, Align::Left, label);
            if (!value.empty())
                g.text_mid(Font::Bold, ts(27), rx + rw - 30, ry, on ? kWhite : kLavender, Align::Right, value);
            ry += 72;
        };
        row(0, tr("Slot"),
            trf("{n}: {name}", {{"n", std::to_string(ws_preset_slot_ + 1)},
                                {"name", slot.used ? tr(names[std::size_t(slot.name)]) : tr("empty")}}));
        row(1, tr("Name"), tr(names[std::size_t(std::clamp(ws_preset_name_, 0, int(names.size()) - 1))]));
        row(2, slot.used ? tr("Save over it") : tr("Save"), "");
        row(3, tr("Back"), "");
        cursor();
        prompts_for({{Glyph::DPad, "Change"}, {Glyph::Cross, "Choose"}, {Glyph::Circle, "Back"}});
        break;
    }
    }
    /* A short note (a point that didn't take, a template saved). */
    if (!ws_note_.empty() && time_ - ws_note_time_ < 3.5)
        big_line(ws_note_, full ? 700 : 960, rgba(0xFFC85C));
}


/* ---- the Controller Hub -------------------------------------------------------------------
 * The main controller (it plays as the Wii Remote) and the second (the Nunchuk)
 * across the top. Under them, three tiles to start from (Hold it, Calibrate,
 * Feel) and All settings: every setting in sections that fold open one at a
 * time, the picture on the left following the open one. The D-pad or a stick
 * moves through it all; the pointer takes over only when the controller is
 * moved on purpose (ws_pointer_step). */

namespace
{
enum HubKind
{
    kHkSeg,     /* a few choices side by side, one chosen */
    kHkSlider,  /* a value along a bar */
    kHkSwatch,  /* the ball's colors */
    kHkButtons, /* things to do */
    kHkTiles,   /* controllers with their pictures */
    kHkText,    /* words only (not focusable) */
};
enum HubSec
{
    kHsHold,
    kHsButtons,
    kHsPointer,
    kHsBall,
    kHsMotion,
    kHsOther,
    kHsSensor,
    kHsSaved,
};
enum HubRowId
{
    kHrGrip = 1000,
    kHrHoldNote,
    kHrHand, /* with the Move: which hand the DualSense (the Nunchuk) is in */
    kHrSecond,
    kHrSetUp,
    kHrSet,
    kHrSetNote,
    kHrSetAct,
    kHrAim,
    kHrEye,
    kHrSpeed,
    kHrSteady,
    kHrPtrAct,
    kHrBall,
    kHrBallAct,
    kHrBallNote,
    kHrSwing,
    kHrFlick,
    kHrMPlus,
    kHrMotionAct,
    kHrOther,
    kHrSensor,
    kHrScope,
    kHrAsk,
    kHrTemplate,
    kHrSaveAct,
};
enum HubFocus
{
    kHfRemote = 1,
    kHfNunchuk = 2,
    kHfTiles = 3,
    kHfAll = 4,
    kHfPlayer = 5, /* the player switcher (pointer only: L1 / R1 on a controller) */
    kHfPHold = 6,  /* players 2 to 4: what they hold */
    kHfPAs = 7,    /* and what their DualSense plays as */
    kHfHead = 100, /* + the section */
};
/* What a button in a row does. */
enum HubAct
{
    kHaCalibrate,
    kHaTune,
    kHaSetUp,
    kHaChange,
    kHaIgnore,
    kHaForget,
    kHaSaveTemplate,
};
/* The pictures on the left (and the tiles'). */
enum HubScene
{
    kHcHold,
    kHcButtons,
    kHcPointer,
    kHcBall,
    kHcMotion,
    kHcOther,
    kHcSensor,
    kHcCal,
};
constexpr float kHubTop = 312, kHubBottom = 972; /* under the controllers, over the prompts */
constexpr float kHubLeftX = 90, kHubLeftW = 880;   /* the picture */
constexpr float kHubColX = 1000, kHubColW = 830;   /* the sections */
constexpr float kHubHeadH = 54, kHubGap = 6, kHubRowH = 58;
const char *const kSteadyNames[3] = {"Steady", "Balanced", "Snappy"};

bool hub_focusable(int kind)
{
    return kind != kHkText;
}
} // namespace

void App::ctl_line(float x0, float y0, float x1, float y1, float width, Color c)
{
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f)
        return;
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    const Corner q[4] = {{x0 + nx, y0 + ny, 1}, {x1 + nx, y1 + ny, 1}, {x1 - nx, y1 - ny, 1}, {x0 - nx, y0 - ny, 1}};
    g_->quad3d(nullptr, q, len, width, c, 0, false, false);
}

/* ---- Ruben's controller diagrams, labeled --------------------------------------------------
 * A label for each control beside the picture, never on it: its line leaves
 * the control's edge straight out, turns up or down, then runs across to the
 * label. Where labels had to move apart, the turns step so no two lines run
 * on top of each other or cross. */

namespace
{
struct DiaSpot
{
    const char *name;
    float x, y, rx, ry; /* half its size across and down */
};
struct Dia
{
    const char *kind;
    float vb[4];
    std::vector<DiaSpot> spots;
};
const Dia kDia[] = {
#include "ui_ctl_diagrams.inc"
};
const DiaSpot *dia_spot(int kind, const std::string &name)
{
    if (kind < 0 || kind >= int(sizeof kDia / sizeof kDia[0]))
        return nullptr;
    for (const DiaSpot &s : kDia[kind].spots)
        if (name == s.name)
            return &s;
    return nullptr;
}
/* A DualSense control's place on the DualSense diagram. */
/* A DualSense control as a label's second line says it. */
std::string ds_word(int control)
{
    static const char *const words[CtlCount] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2",
                                                "Left stick, press", "Right stick, press", "Options", "Touch pad, press",
                                                "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
    return control >= 0 && control < CtlCount ? tr(words[control]) : std::string();
}
const char *ds_anchor(int control)
{
    static const char *const names[CtlCount] = {"cross", "circle", "square", "triangle", "l1",  "r1", "l2",   "r2",
                                                "ls",    "rs",     "options", "touch",  "up", "down", "left", "right"};
    return control >= 0 && control < CtlCount ? names[control] : "";
}
} // namespace

App::DiaBox App::draw_diagram(int kind, const std::vector<DiaLabel> &labels, float x, float y, float w, float h,
                              float alpha, int turn)
{
    Gfx &g = *g_;
    DiaBox box;
    if (kind < 0 || kind >= kDiaKinds || alpha <= 0.01f)
        return box;
    if (!dia_tried_)
    {
        dia_tried_ = true;
        static const char *const files[kDiaKinds] = {"dualsense", "move", "movenav", "remote", "eye"};
        for (int i = 0; i < kDiaKinds; ++i)
            if (i != kDiaNav && i != kDiaRemote) /* (theirs come with those controllers) */
                dia_tex_[i] = g.texture_file(g.asset_dir() + "/ui/diagrams/" + files[i] + ".png", 2048);
    }
    const Dia &d = kDia[kind];
    const float vw = d.vb[2], vh = d.vb[3];
    const bool upright = turn != 0;
    const float colw = 178, rowh = 40, pitch = 50, tagw = 168, lead_lr = 70, lead_tb = 58;

    struct Tag
    {
        const DiaLabel *l;
        int side;
        float ax, ay, rx, ry; /* the control on screen, and half its size across and down */
        float lx = 0, ly = 0;
        int rank = -1; /* how far in its turn steps (-1: a straight line) */
    };
    /* Which sides have labels (auto: the side the control is on). */
    std::vector<Tag> tags;
    for (const DiaLabel &l : labels)
    {
        const DiaSpot *s = dia_spot(kind, l.anchor);
        if (!s)
            continue;
        Tag t{&l, l.side, s->x - d.vb[0], s->y - d.vb[1], s->rx, s->ry};
        if (t.rx <= 0)
            t.rx = t.ry = l.anchor == "dpad" ? (kind == 2 ? 112.0f : 100.0f) : 12.0f;
        tags.push_back(t);
    }
    /* Turned on end: what's left and right is the picture turned. */
    auto turned = [&](float u, float v) -> std::pair<float, float> {
        if (turn == 1)
            return {v, vw - u}; /* a quarter anticlockwise: its right end up */
        if (turn == 2)
            return {vh - v, u};
        return {u, v};
    };
    const float pw = upright ? vh : vw, ph = upright ? vw : vh; /* the picture's size, turned */
    for (Tag &t : tags)
        if (t.side < 0)
        {
            const auto p = turned(t.ax, t.ay);
            t.side = p.first < pw * 0.5f ? 0 : 1;
        }
    bool has[4] = {};
    for (const Tag &t : tags)
        has[t.side] = true;
    const float side_pad = has[0] || has[1] ? colw + lead_lr : 0;
    const float top_pad = has[2] ? rowh + lead_tb : 0, bottom_pad = has[3] ? rowh + lead_tb : 0;
    const float k = std::min((w - 2 * side_pad) / pw, (h - top_pad - bottom_pad - 16) / ph);
    const float aw = pw * k, ah = ph * k;
    const float ax = x + (w - aw) * 0.5f, ay = y + top_pad + (h - top_pad - bottom_pad - ah) * 0.5f;
    box = {ax, ay, aw, ah, k};

    /* The picture. */
    if (Texture *tex = dia_tex_[kind])
    {
        const Color tint = g.tone(with_alpha(rgba(0xDDEBFF), alpha));
        if (!upright)
            g.image(tex, ax, ay, aw, ah, tint);
        else
        {
            auto at = [&](float u, float v) {
                const auto p = turned(u, v);
                return Corner{ax + p.first * k, ay + p.second * k, 1};
            };
            const Corner c[4] = {at(0, 0), at(vw, 0), at(vw, vh), at(0, vh)};
            g.quad3d(tex, c, vw * k, vh * k, tint, 0, false, false);
        }
    }
    for (Tag &t : tags)
    {
        const auto p = turned(t.ax, t.ay);
        t.ax = ax + p.first * k;
        t.ay = ay + p.second * k;
        if (upright)
            std::swap(t.rx, t.ry);
        t.rx *= k;
        t.ry *= k;
    }
    /* The Move's ball lit in its color (Settings: Move Ball Color). */
    if (kind == kDiaMove)
        if (const DiaSpot *orb = dia_spot(kind, "orb"))
        {
            const auto p = turned(orb->x - d.vb[0], orb->y - d.vb[1]);
            const float bx = ax + p.first * k, by = ay + p.second * k, r = std::min(orb->rx, orb->ry) * k * 0.86f;
            in_true_color([&] {
                g.blob(bx, by, r * 5, r * 5, rgba(ball_hex(), 0.42f * alpha));
                g.panel(bx - r, by - r, 2 * r, 2 * r, rgba(ball_hex(), 0.95f * alpha), 0.8f, r,
                        rgba(0xFFFFFF, 0.55f * alpha), 1.5f, 0, 0.3f);
            });
        }
    if (tags.empty())
        return box;

    /* Each side's labels, as near their controls as they can be without overlapping. */
    std::vector<Tag *> side[4];
    for (Tag &t : tags)
        side[t.side].push_back(&t);
    auto spread = [](std::vector<float> &pos, float size, float lo, float hi) {
        const std::size_t n = pos.size();
        for (int pass = 0; pass < 3; ++pass)
        {
            for (std::size_t i = 0; i < n; ++i)
                pos[i] = std::max(pos[i], i == 0 ? lo : pos[i - 1] + size);
            for (std::size_t i = n; i-- > 0;)
                pos[i] = std::min(pos[i], i + 1 == n ? hi - size : pos[i + 1] - size);
        }
        for (std::size_t i = 0; i < n; ++i)
            pos[i] = std::max(pos[i], i == 0 ? lo : pos[i - 1] + size);
    };
    for (int sd = 0; sd < 4; ++sd)
    {
        auto &v = side[sd];
        const bool across = sd >= 2;
        std::stable_sort(v.begin(), v.end(), [&](const Tag *a, const Tag *b) { return across ? a->ax < b->ax : a->ay < b->ay; });
        std::vector<float> pos;
        for (Tag *t : v)
            pos.push_back(across ? t->ax - tagw * 0.5f : t->ay - rowh * 0.5f);
        if (across)
            spread(pos, tagw + 10, x + 4, x + w - 4);
        else
            spread(pos, pitch, y + 2, y + h - 2);
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            if (across)
            {
                v[i]->lx = pos[i];
                v[i]->ly = sd == 2 ? ay - lead_tb - rowh : ay + ah + lead_tb;
            }
            else
            {
                v[i]->ly = pos[i];
                v[i]->lx = sd == 0 ? ax - lead_lr - colw : ax + aw + lead_lr;
            }
        }
        /* The turns: a label moved past its control steps in toward the picture,
         * one step more for each one beyond it the same way (the lines nest). */
        auto off = [&](const Tag *t) {
            return across ? (t->lx + tagw * 0.5f) - t->ax : (t->ly + rowh * 0.5f) - t->ay;
        };
        int rank = 0;
        for (Tag *t : v)
            if (off(t) > 1.5f)
                t->rank = rank++;
        rank = 0;
        for (std::size_t i = v.size(); i-- > 0;)
            if (off(v[i]) < -1.5f)
                v[i]->rank = rank++;
    }

    const Color line = with_alpha(rgba(0x9AD8FF), 0.72f * alpha);
    const float lw = 2.0f, step = 11;
    auto hline = [&](float a, float b, float yy) { g.panel(std::min(a, b) - lw * 0.5f, yy - lw * 0.5f, std::fabs(b - a) + lw, lw, line, 1, lw * 0.5f); };
    auto vline = [&](float xx, float a, float b) { g.panel(xx - lw * 0.5f, std::min(a, b) - lw * 0.5f, lw, std::fabs(b - a) + lw, line, 1, lw * 0.5f); };
    const float chip = 28;
    for (const Tag &t : tags)
    {
        const DiaLabel &l = *t.l;
        float mx, my; /* where the line meets the label */
        if (t.side == 0 || t.side == 1)
        {
            const bool left = t.side == 0;
            my = t.ly + rowh * 0.5f;
            mx = left ? t.lx + colw + 10 : t.lx - 10;
            const float sx = left ? t.ax - t.rx - 4 : t.ax + t.rx + 4;
            if (t.rank < 0)
                hline(sx, mx, t.ay);
            else
            {
                float kx = left ? mx + 16 + float(t.rank) * step : mx - 16 - float(t.rank) * step;
                kx = left ? std::min(kx, sx - 8) : std::max(kx, sx + 8);
                hline(sx, kx, t.ay);
                vline(kx, t.ay, my);
                hline(kx, mx, my);
            }
            g.panel(sx - 3.5f, t.ay - 3.5f, 7, 7, with_alpha(kCyan, alpha), 1, 3.5f);
        }
        else
        {
            const bool top = t.side == 2;
            mx = t.lx + tagw * 0.5f;
            my = top ? t.ly + rowh + 8 : t.ly - 8;
            const float sy = top ? t.ay - t.ry - 4 : t.ay + t.ry + 4;
            if (std::fabs(mx - t.ax) < 1.5f)
                vline(t.ax, sy, my);
            else
            {
                float ky = top ? my + 14 + float(std::max(0, t.rank)) * step : my - 14 - float(std::max(0, t.rank)) * step;
                ky = top ? std::min(ky, sy - 8) : std::max(ky, sy + 8);
                vline(t.ax, sy, ky);
                hline(t.ax, mx, ky);
                vline(mx, ky, my);
            }
            g.panel(t.ax - 3.5f, sy - 3.5f, 7, 7, with_alpha(kCyan, alpha), 1, 3.5f);
        }
        /* The label: the Wii button's chip, what it is, and the control under it. */
        const float cw = l.chip >= 0 ? chip : 0.0f;
        const float room = (t.side >= 2 ? tagw : colw) - (cw > 0 ? cw + 8 : 0);
        const std::string title = fit(g, Font::Bold, ts(18), l.title, room);
        const std::string sub = fit(g, Font::Regular, ts(14), l.sub, t.side >= 2 ? tagw + 20 : room);
        const float cy = t.ly + rowh * 0.5f;
        const Color ink = with_alpha(l.dim ? kSoft : kWhite, alpha * (l.dim ? 0.7f : 1.0f));
        if (t.side == 0)
        {
            const float right = t.lx + colw;
            float tx = right;
            if (cw > 0)
            {
                wii_chip(l.chip, right - cw * 0.5f, cy, chip, alpha);
                tx = right - cw - 8;
            }
            g.text_mid(Font::Bold, ts(18), tx, cy - 8, ink, Align::Right, title);
            g.text_mid(Font::Regular, ts(14), tx, cy + 12, with_alpha(kLavender, alpha), Align::Right, sub);
        }
        else if (t.side == 1)
        {
            float tx = t.lx;
            if (cw > 0)
            {
                wii_chip(l.chip, tx + cw * 0.5f, cy, chip, alpha);
                tx += cw + 8;
            }
            g.text_mid(Font::Bold, ts(18), tx, cy - 8, ink, Align::Left, title);
            g.text_mid(Font::Regular, ts(14), tx, cy + 12, with_alpha(kLavender, alpha), Align::Left, sub);
        }
        else
        {
            const float tw = g.measure(Font::Bold, ts(18), title) + (cw > 0 ? cw + 8 : 0);
            float tx = t.lx + (tagw - tw) * 0.5f;
            if (cw > 0)
            {
                wii_chip(l.chip, tx + cw * 0.5f, cy - 8, chip * 0.85f, alpha);
                tx += cw + 8;
            }
            g.text_mid(Font::Bold, ts(18), tx, cy - 8, ink, Align::Left, title);
            g.text_mid(Font::Regular, ts(14), t.lx + tagw * 0.5f, cy + 13, with_alpha(kLavender, alpha), Align::Center, sub);
        }
    }
    return box;
}

/* The DualSense's labels for a Wii layout: what each control is on the Wii. */
std::vector<App::DiaLabel> App::ds_labels(const WiiLayout &lay, bool upright)
{
    auto title_of = [](int input) -> const char * {
        switch (input)
        {
        case WiA: return "A Button";
        case WiB: return "B Trigger";
        case WiOne: return "1 Button";
        case WiTwo: return "2 Button";
        case WiMinus: return "Minus";
        case WiPlus: return "Plus";
        case WiHome: return "HOME Button";
        case WiShake: return "Shake the Remote";
        case WiC: return "C Button";
        case WiZ: return "Z Button";
        case WiNunchukShake: return "Shake the Nunchuk";
        case WiCentre: return "Center (hold)";
        default: return kWiiInfo[input].name;
        }
    };
    auto short_of = [](int input) -> const char * {
        switch (input)
        {
        case WiShake: case WiNunchukShake: return "shake";
        case WiCentre: return "center";
        case WiHome: return "HOME";
        default: return kWiiInfo[input].chip;
        }
    };
    /* Flat: the shoulders and the D-pad left, the face buttons right, Options and the
     * touch pad above, Square and the right stick below (their lines clear of the
     * buttons beside them). Upright: each label on the side its control is. */
    auto side_of = [&](int c) {
        if (upright)
            return -1;
        switch (c)
        {
        case CtlL1: case CtlL2: case CtlUp: case CtlDown: case CtlLeft: case CtlRight: case CtlL3: return 0;
        case CtlR1: case CtlR2: case CtlTriangle: case CtlCircle: case CtlCross: return 1;
        case CtlOptions: case CtlTouch: return 2;
        default: return 3;
        }
    };
    bool dpad_home = true;
    int dpad_seen = 0;
    for (int i = 0; i < lay.count; ++i)
        if (lay.binds[i].control >= CtlUp && lay.binds[i].control <= CtlRight)
        {
            ++dpad_seen;
            dpad_home &= lay.binds[i].input == WiUp + (lay.binds[i].control - CtlUp);
        }
    dpad_home &= dpad_seen == 4;
    std::vector<DiaLabel> out;
    int l3 = -1, r3 = -1;
    for (int i = 0; i < lay.count; ++i)
    {
        const WiiBinding &b = lay.binds[i];
        if (b.control < 0 || b.control >= CtlCount || b.input < 0 || b.input >= WiInputCount)
            continue;
        if (dpad_home && b.control >= CtlUp && b.control <= CtlRight)
            continue;
        if (b.control == CtlL3)
        {
            l3 = b.input;
            continue;
        }
        if (b.control == CtlR3)
        {
            r3 = b.input;
            continue;
        }
        DiaLabel l;
        l.anchor = ds_anchor(b.control);
        l.side = side_of(b.control);
        const std::string chip = kWiiInfo[b.input].chip;
        l.chip = chip.size() == 1 || b.input == WiMinus || b.input == WiPlus ? b.input : -1;
        l.title = tr(title_of(b.input));
        l.sub = ds_word(b.control);
        out.push_back(l);
    }
    if (dpad_home)
    {
        DiaLabel l;
        l.anchor = "dpad";
        l.side = side_of(CtlLeft);
        l.title = tr("D-Pad");
        l.sub = tr("The same on the Wii");
        out.push_back(l);
    }
    auto stick = [&](int role, int press, int control, const char *name) {
        if (role == StickNothing && press < 0)
            return;
        DiaLabel l;
        l.anchor = ds_anchor(control);
        l.side = side_of(control);
        l.title = role != StickNothing ? tr(stick_name(role)) : tr(title_of(press));
        l.sub = tr(name);
        if (role != StickNothing && press >= 0)
            l.sub += " \xC2\xB7 " + tr("press:") + " " + tr(short_of(press));
        out.push_back(l);
    };
    stick(lay.left_stick, l3, CtlL3, "Left stick");
    stick(lay.right_stick, r3, CtlR3, "Right stick");
    return out;
}

/* The Move's labels: what each of its buttons is on the Wii Remote. */
std::vector<App::DiaLabel> App::move_labels()
{
    std::vector<DiaLabel> out;
    auto add = [&](const char *anchor, int side, int chip, const char *title, const char *sub, bool dim = false) {
        DiaLabel l;
        l.anchor = anchor;
        l.side = side;
        l.chip = chip;
        l.title = tr(title);
        l.sub = tr(sub);
        l.dim = dim;
        out.push_back(l);
    };
    add("orb", 1, -1, "The light", "The PS3 Eye follows it");
    add("square", 0, -1, "Center (hold)", "Square");
    add("triangle", 1, -1, "Not used", "Triangle", true);
    add("movebtn", 0, WiA, "A Button", "Move button");
    add("cross", 0, WiOne, "1 Button", "Cross");
    add("circle", 1, WiTwo, "2 Button", "Circle");
    add("t", 1, WiB, "B Trigger", "T, underneath");
    add("ps", 0, -1, "HOME Button", "PS button");
    add("select", 0, WiMinus, "Minus", "Select, on the side");
    add("start", 1, WiPlus, "Plus", "Start, on the side");
    return out;
}

/* The GameCube controller's buttons on the DualSense (a GameCube game, or the Wii's
 * GameCube controller). */
std::vector<App::DiaLabel> App::gc_labels(const Mapping &m)
{
    std::vector<DiaLabel> out;
    const int sides[CtlCount] = {1, 1, 3, 1, 0, 1, 0, 1, 0, 3, 2, 2, 0, 0, 0, 0};
    bool dpad = true;
    for (int i = 0; i < 4; ++i)
        dpad &= m.control[GcUp + i] == CtlUp + i;
    for (int gc = 0; gc < GcCount; ++gc)
    {
        const int c = m.control[gc];
        if (c < 0 || c >= CtlCount || (dpad && gc >= GcUp && gc <= GcRight))
            continue;
        DiaLabel l;
        l.anchor = ds_anchor(c);
        l.side = sides[c];
        l.title = tr(kGc[gc].name);
        l.sub = ds_word(c);
        out.push_back(l);
    }
    if (dpad)
    {
        DiaLabel l;
        l.anchor = "dpad";
        l.side = 0;
        l.title = tr("D-Pad");
        l.sub = tr("The GameCube's D-pad");
        out.push_back(l);
    }
    DiaLabel ls;
    ls.anchor = "ls", ls.side = 0, ls.title = tr("Control Stick"), ls.sub = tr("Left stick");
    out.push_back(ls);
    DiaLabel rs;
    rs.anchor = "rs", rs.side = 3, rs.title = tr("C-Stick"), rs.sub = tr("Right stick");
    out.push_back(rs);
    return out;
}

/* Every section, its rows as the controllers chosen allow, and a line saying
 * what each is set to. */
std::vector<App::HubSection> App::ctl_sections()
{
    Settings &t = ws_settings();
    const int c = ws_controller_;
    const int other = other_of(c);
    const bool move = ws_move_ && !other;
    const bool pointing = points(c);
    std::vector<HubSection> out;
    auto row = [](int id, int kind, const std::string &label) {
        HubRow r;
        r.id = id;
        r.kind = kind;
        r.label = label;
        return r;
    };
    auto seg = [&](int id, const std::string &label, std::vector<std::string> opts, int cur) {
        HubRow r = row(id, kHkSeg, label);
        r.ok.assign(opts.size(), true);
        r.opts = std::move(opts);
        r.cur = cur;
        return r;
    };
    auto text = [&](int id, const std::string &words) {
        HubRow r = row(id, kHkText, "");
        r.value = words;
        return r;
    };
    auto buttons = [&](int id, std::vector<std::string> opts, std::vector<int> acts, std::vector<bool> ok = {}) {
        HubRow r = row(id, kHkButtons, "");
        r.ok = ok.empty() ? std::vector<bool>(opts.size(), true) : ok;
        r.opts = std::move(opts);
        r.icons = std::move(acts);
        return r;
    };
    auto slider = [&](int id, const std::string &label, float frac, const std::string &value) {
        HubRow r = row(id, kHkSlider, label);
        r.frac = std::clamp(frac, 0.0f, 1.0f);
        r.value = value;
        return r;
    };

    /* How you hold it. */
    {
        HubSection s;
        s.id = kHsHold;
        s.name = tr("How you hold it");
        const int grip = std::clamp(t.wii_grip, 0, 3);
        const std::string grips[4] = {tr("Auto"), tr("Both hands"), tr("Upright, to the TV"), tr("Upright, facing you")};
        if (other == 3)
        {
            s.rows.push_back(text(kHrHoldNote, tr("The DualSense plays as a GameCube controller: its buttons, no pointer "
                                                  "or motion.")));
            s.summary = tr("Like a GameCube controller");
        }
        else if (move && c == WiiRemote)
        {
            s.rows.push_back(text(kHrHoldNote, tr("The Move in your hand like a Wii Remote, its light toward the TV. Hold "
                                                  "Square a moment to center the pointer.")));
            s.summary = tr("The Move, like a Wii Remote");
        }
        else
        {
            s.rows.push_back(seg(kHrGrip, move ? tr("DualSense") : tr("Grip"),
                                 {grips[0], grips[1], grips[2], grips[3]}, grip));
            s.summary = grip == 0 ? tr("Auto: reads it for you") : grips[grip];
            if (move)
            {
                const int hand = std::clamp(t.wii_nunchuk_hand, 0, 1);
                s.rows.push_back(seg(kHrHand, tr("Nunchuk hand"), {tr("Left hand"), tr("Right hand")}, hand));
                s.rows.push_back(text(kHrHoldNote,
                                      hand == 0 ? tr("The Move in your right hand, its light toward the TV; the "
                                                     "DualSense in your left, like a Nunchuk. Hold Square a moment "
                                                     "to center the pointer.")
                                                : tr("The Move in your left hand, its light toward the TV; the "
                                                     "DualSense in your right, like a Nunchuk: its right stick, R1 and "
                                                     "R2, and the face buttons as the D-pad. Hold Square a moment to "
                                                     "center the pointer.")));
            }
            else if (pointing)
                s.rows.push_back(text(kHrHoldNote, tr("Auto reads the controller's angle and which hand it's in, so "
                                                      "tilts and swings come out the right way round.")));
        }
        if (c == WiiTwoControllers)
        {
            const bool second = porpoise::pad::connected(1);
            s.rows.push_back(text(kHrSecond, second ? tr("Second DualSense: on.")
                                                    : tr("Second DualSense: not found. Sign in a second user and turn "
                                                         "their controller on.")));
        }
        if (!pointing && other != 3)
            s.rows.push_back(buttons(kHrSetUp, {tr("Set it up")}, {kHaSetUp}));
        out.push_back(std::move(s));
    }
    /* Buttons. */
    {
        HubSection s;
        s.id = kHsButtons;
        s.name = tr("Buttons");
        if (other != 3)
        {
            const int set = std::clamp(t.wii_buttons_use, 0, 2);
            const std::string sets[3] = {tr("Porpoise's"), tr("Mine 1"), tr("Mine 2")};
            s.rows.push_back(seg(kHrSet, tr("Button set"), {sets[0], sets[1], sets[2]}, set));
            s.rows.push_back(text(kHrSetNote, set == 0 ? tr("Porpoise's set stays as it is, so there's always one that "
                                                            "works. Pick Mine 1 or Mine 2 to make your own.")
                                                       : tr("Your own set: Change buttons to pick what each one does.")));
            s.summary = set == 0 ? tr("Porpoise's set") : sets[set];
        }
        else
            s.summary = tr("The GameCube's");
        if (!ws_in_game_)
            s.rows.push_back(buttons(kHrSetAct, {tr("Change buttons")}, {kHaChange}));
        out.push_back(std::move(s));
    }
    /* Pointer. */
    if (pointing)
    {
        HubSection s;
        s.id = kHsPointer;
        s.name = tr("Pointer");
        const std::string aims[3] = {tr("Gyro"), tr("Touch pad"), tr("Right stick")};
        const int aim = std::clamp(t.wii_pointer, 0, 2);
        if (move)
        {
            const porpoise::move::Status ms = porpoise::move::status();
            s.rows.push_back(text(kHrEye, ms.camera_state != 4 ? tr("The Move aims. The PS3 Eye is off: turn it on in "
                                                                    "Settings to follow the ball.")
                                          : ms.ball         ? tr("The Move aims, and the PS3 Eye sees its ball.")
                                                            : tr("The Move aims. The PS3 Eye is on, looking for the "
                                                                 "ball.")));
        }
        else
            s.rows.push_back(seg(kHrAim, tr("Aim with"), {aims[0], aims[1], aims[2]}, aim));
        s.rows.push_back(slider(kHrSpeed, tr("Speed"), float(t.wii_reach - 60) / 100.0f,
                                std::to_string(t.wii_reach) + "%"));
        const int steady = feel_of(t.wii_smooth);
        s.rows.push_back(seg(kHrSteady, tr("Steadiness"), {tr("Steady"), tr("Balanced"), tr("Snappy")}, steady));
        s.rows.push_back(buttons(kHrPtrAct, {tr("Calibrate"), tr("Test & tune")}, {kHaCalibrate, kHaTune}));
        s.summary = (move ? tr("The Move") : aims[aim]) + " \xC2\xB7 " + std::to_string(t.wii_reach) + "% \xC2\xB7 " +
                    tr(kSteadyNames[steady]);
        out.push_back(std::move(s));
    }
    /* Ball color. */
    if (move)
    {
        HubSection s;
        s.id = kHsBall;
        s.name = tr("Ball color");
        HubRow r = row(kHrBall, kHkSwatch, tr("Color"));
        for (int i = 0; i < porpoise::move::kBallColors; ++i)
            r.opts.push_back(tr(porpoise::move::ball_name(i)));
        r.ok.assign(r.opts.size(), true);
        r.cur = std::clamp(settings_->move_color, 0, porpoise::move::kBallColors - 1);
        s.rows.push_back(r);
        const porpoise::move::Status ms = porpoise::move::status();
        const bool eye = ms.camera_state == 4;
        std::vector<std::string> acts = {ms.learning ? tr("Noting the lights\xE2\x80\xA6") : tr("Ignore other lights")};
        std::vector<int> codes = {kHaIgnore};
        std::vector<bool> ok = {eye};
        if (ms.ignored > 0)
        {
            acts.push_back(trf("Forget them ({n})", {{"n", std::to_string(ms.ignored)}}));
            codes.push_back(kHaForget);
            ok.push_back(true);
        }
        s.rows.push_back(buttons(kHrBallAct, acts, codes, ok));
        s.rows.push_back(text(kHrBallNote, eye ? tr("Lamps or wall lights in the camera's view? Ignore other lights notes "
                                                    "them while the ball is dark for a moment.")
                                               : tr("For the PS3 Eye: turn it on in Settings.")));
        s.summary = r.opts[std::size_t(r.cur)];
        if (ms.ignored > 0)
            s.summary += " \xC2\xB7 " + trf("{n} lights ignored", {{"n", std::to_string(ms.ignored)}});
        out.push_back(std::move(s));
    }
    /* Motion and feel. */
    if (other != 3)
    {
        HubSection s;
        s.id = kHsMotion;
        s.name = tr("Motion and feel");
        if (move)
            s.rows.push_back(slider(kHrSwing, tr("Swing strength"), float(t.wii_swing - 20) / 130.0f,
                                    std::to_string(t.wii_swing) + "%"));
        s.rows.push_back(seg(kHrFlick, tr("Flick to shake"), {tr("On"), tr("Off")}, t.wii_shake ? 0 : 1));
        const int mp = std::clamp(t.wii_motion_plus, 0, 2);
        const std::string mps[3] = {tr("Auto"), tr("On"), tr("Off")};
        s.rows.push_back(seg(kHrMPlus, tr("Wii MotionPlus"), {mps[0], mps[1], mps[2]}, mp));
        if (pointing)
            s.rows.push_back(buttons(kHrMotionAct, {tr("Test & tune")}, {kHaTune}));
        s.summary = (move ? tr("Swing") + " " + std::to_string(t.wii_swing) + "% \xC2\xB7 " : std::string()) +
                    tr("Flick") + " " + (t.wii_shake ? tr("on") : tr("off")) + " \xC2\xB7 MotionPlus " + mps[mp];
        out.push_back(std::move(s));
    }
    /* Other Wii controllers. */
    {
        HubSection s;
        s.id = kHsOther;
        s.name = tr("Other Wii controllers");
        HubRow r = row(kHrOther, kHkTiles, "");
        r.opts = {tr("None"), tr("Classic Controller"), tr("Remote Sideways"), tr("GameCube Controller")};
        r.icons = {-1, 4, 13, 5};
        r.ok.assign(4, true);
        r.cur = other;
        s.rows.push_back(r);
        s.rows.push_back(text(kHrHoldNote, tr("Played on the DualSense, for the games that want them.")));
        s.summary = other ? r.opts[std::size_t(other)] : tr("None");
        out.push_back(std::move(s));
    }
    /* Sensor bar: here, locked, so it's clear where it will be. */
    {
        HubSection s;
        s.id = kHsSensor;
        s.name = tr("Sensor bar");
        s.locked = true;
        s.summary = tr("Coming soon");
        s.rows.push_back(text(kHrSensor, tr("For real Wii Remotes. The PlayStation controllers don't need one: the gyro "
                                            "or the camera does its job.")));
        out.push_back(std::move(s));
    }
    /* Saved setups. */
    {
        HubSection s;
        s.id = kHsSaved;
        s.name = tr("Saved setups");
        if (ws_game_)
            s.rows.push_back(seg(kHrScope, tr("Save for"), {tr("Just this game"), tr("Every Wii game")}, ws_scope_));
        if (ws_game_ && !ws_in_game_)
            s.rows.push_back(seg(kHrAsk, tr("Before this game"), {tr("Show the hub"), tr("Go straight in")},
                                 ws_ask_ ? 0 : 1));
        bool any = false;
        for (const auto &p : settings_->wii_presets)
            any |= p.used;
        if (any)
        {
            const auto &names = Settings::wii_preset_names();
            HubRow r = row(kHrTemplate, kHkSeg, tr("Template"));
            for (int i = 0; i < Settings::kWiiPresets; ++i)
            {
                const Settings::WiiPreset &w = settings_->wii_presets[i];
                r.opts.push_back(w.used ? tr(names[std::size_t(w.name)]) : tr("Empty"));
                r.ok.push_back(w.used);
            }
            r.cur = settings_->wii_preset - 1;
            s.rows.push_back(r);
        }
        s.rows.push_back(buttons(kHrSaveAct, {tr("Save as template")}, {kHaSaveTemplate}));
        s.summary = !ws_game_ ? tr("Every Wii game") : ws_scope_ == 0 ? tr("Just this game") : tr("Every Wii game");
        if (settings_->wii_preset > 0 && settings_->wii_preset <= Settings::kWiiPresets &&
            settings_->wii_presets[settings_->wii_preset - 1].used)
            s.summary += " \xC2\xB7 " +
                         tr(Settings::wii_preset_names()[std::size_t(settings_->wii_presets[settings_->wii_preset - 1].name)]);
        out.push_back(std::move(s));
    }
    return out;
}

/* What the player holds, kept: the controllers (for this game or every one,
 * as the scope says) and the Move (every game: it's what they own). */
void App::ctl_keep()
{
    ws_store("wii_controller", ws_controller_);
    ws_store("wii_move", ws_move_ ? 1 : 0, true);
    ws_store("wii_nav", ws_nav_ ? 1 : 0, true);
    wii_setup_apply_pad();
}

/* A row's value changed: dir along it, or choice (a slider's: thousandths). */
bool App::ctl_change(int row, int dir, int choice)
{
    Settings &t = ws_settings();
    auto pick = [&](int cur, int n) { return choice >= 0 ? std::clamp(choice, 0, n - 1) : std::clamp(cur + dir, 0, n - 1); };
    auto stepped = [&](int cur, int lo, int hi, int step) {
        if (choice >= 0)
            return std::clamp(lo + int(std::lround(float(choice) / 1000.0f * float(hi - lo) / float(step))) * step, lo, hi);
        return std::clamp(cur + dir * step, lo, hi);
    };
    int was = 0, now = 0;
    switch (row)
    {
    case kHrGrip:
        was = std::clamp(t.wii_grip, 0, 3), now = pick(was, 4);
        if (now != was)
            ws_store("wii_grip", now);
        break;
    case kHrHand:
        was = std::clamp(t.wii_nunchuk_hand, 0, 1), now = pick(was, 2);
        if (now != was)
            ws_store("wii_nunchuk_hand", now);
        break;
    case kHrSet:
        was = std::clamp(t.wii_buttons_use, 0, 2), now = pick(was, 3);
        if (now != was)
            ws_store("wii_buttons_use", now);
        break;
    case kHrAim:
        was = std::clamp(t.wii_pointer, 0, 2), now = pick(was, 3);
        if (now != was)
            ws_store("wii_pointer", now);
        break;
    case kHrSpeed:
        was = t.wii_reach, now = stepped(was, 60, 160, 10);
        if (now != was)
            ws_store("wii_reach", now);
        break;
    case kHrSteady:
        was = feel_of(t.wii_smooth), now = pick(was, 3);
        if (now != was)
            ws_store("wii_smooth", kFeelSmooth[now]);
        break;
    case kHrSwing:
        was = t.wii_swing, now = stepped(was, 20, 150, 10);
        if (now != was)
            ws_store("wii_swing", now);
        break;
    case kHrFlick:
        was = t.wii_shake ? 0 : 1, now = pick(was, 2);
        if (now != was)
            ws_store("wii_shake", now == 0 ? 1 : 0);
        break;
    case kHrMPlus:
        was = std::clamp(t.wii_motion_plus, 0, 2), now = pick(was, 3);
        if (now != was)
            ws_store("wii_motion_plus", now);
        break;
    case kHrScope:
        was = ws_scope_, now = pick(was, 2);
        ws_scope_ = now;
        break;
    case kHrAsk:
        was = ws_ask_ ? 0 : 1, now = pick(was, 2);
        if (now != was)
            ws_choose(kCrAsk, now);
        break;
    case kHrTemplate:
    {
        /* Along the saved ones (the empty slots are skipped). */
        was = settings_->wii_preset - 1;
        now = was;
        if (choice >= 0)
            now = choice;
        else
            for (int k = was + dir; k >= 0 && k < Settings::kWiiPresets; k += dir)
                if (settings_->wii_presets[k].used)
                {
                    now = k;
                    break;
                }
        if (now != was && now >= 0 && now < Settings::kWiiPresets && settings_->wii_presets[now].used)
        {
            ws_choose(kCrTemplate, now);
            ws_dirty_ = true;
        }
        else
            now = was;
        break;
    }
    case kHrBall:
        was = settings_->move_color, now = pick(was, porpoise::move::kBallColors);
        if (now != was)
            ws_choose(kCrColor, now);
        return now != was;
    case kHrOther:
        was = other_of(ws_controller_), now = pick(was, 4);
        if (now != was && ws_choose(kCrOther, now))
            ctl_keep();
        return now != was;
    default: return false;
    }
    if (now == was)
        return false;
    wii_setup_apply_pad();
    sfx(Sound::MovingTab);
    return true;
}

/* A row's button pressed. Returns true when the hub gave way to another step. */
bool App::ctl_activate(int row, int which)
{
    (void)row;
    switch (which)
    {
    case kHaCalibrate:
    case kHaSetUp:
        ctl_keep();
        if (which == kHaCalibrate && ws_move_)
            ws_step_ = kWsReady; /* held like a Remote: straight to what's coming */
        else
            ws_step_ = kWsHold;
        ws_point_ = 0;
        ws_still_from_ = -1;
        ws_hold_from_ = -1;
        sfx(Sound::MenuScroll);
        return true;
    case kHaTune:
        ctl_keep();
        ws_step_ = kWsTry;
        ws_row_ = ws_tune_rows().front();
        ws_target_ = ws_hits_n_ = ws_misses_n_ = 0;
        ws_swing_peak_ = 0;
        sfx(Sound::MenuScroll);
        return true;
    case kHaChange:
        ctl_keep();
        open_mapping();
        map_from_ws_ = true;
        map_kind_ = ws_controller_ == WiiGameCube ? 0 : wii_button_set(ws_controller_) + 1;
        map_row_ = map_kind_ > 0 ? 0 : map_row_;
        return true;
    case kHaIgnore: ws_choose(kCrColor, 8); return false;
    case kHaForget: ws_choose(kCrColor, 9); return false;
    case kHaSaveTemplate:
        ctl_keep();
        ws_tune_from_ = kWsController;
        ws_step_ = kWsTune;
        ws_row_ = 0;
        ws_preset_slot_ = std::max(0, settings_->wii_preset - 1);
        sfx(Sound::MenuScroll);
        return true;
    default: return false;
    }
}

App::Action App::update_ctl_hub(bool up, bool down, bool left, bool right)
{
    porpoise::pad::watching_players(); /* (so the players' pages see their Moves) */
    const Motion m = porpoise::pad::snapshot(0).motion;
    /* The swing, for Motion and feel's picture: g past gravity, a sample a frame. */
    {
        const float mag = m.valid ? std::max(0.0f, std::sqrt(m.accel[0] * m.accel[0] + m.accel[1] * m.accel[1] +
                                                             m.accel[2] * m.accel[2]) -
                                                       1.0f)
                                  : 0.0f;
        /* A sample a 60th of a second (the graph's 3 s), whatever the frame rate. */
        ws_trace_acc_ += ui_dt_;
        const bool sample = ws_trace_acc_ >= 1.0 / 60.0 - 1e-6;
        if (sample)
        {
            ws_trace_acc_ = std::fmod(ws_trace_acc_, 1.0 / 60.0);
            ws_trace_[ws_trace_n_ % 180] = mag;
            ++ws_trace_n_;
        }
        ws_swing_peak_ = std::max(ws_swing_peak_ * std::pow(0.97f, float(ui_dt_ * 60.0)), mag);
        if (mag > 0.4f && mag >= ws_swing_peak_ - 0.001f)
        {
            ws_last_swing_ = mag;
            ws_last_swing_at_ = time_;
        }
        if (sample && m.valid && m.centred)
        {
            ws_trail_[ws_trail_n_ % 10][0] = std::clamp(m.aim_x, -1.0f, 1.0f);
            ws_trail_[ws_trail_n_ % 10][1] = std::clamp(m.aim_y, -1.0f, 1.0f);
            ++ws_trail_n_;
        }
    }
    const WsHit *hover = ws_hover_ >= 0 && ws_hover_ < int(ws_hits_.size()) ? &ws_hits_[std::size_t(ws_hover_)] : nullptr;
    const bool cross = pressed(BtnCross);
    const std::vector<HubSection> secs = ctl_sections();
    const int c = ws_controller_;
    const bool pointing = points(c);

    /* Play now (a game waiting), and back. */
    if (ws_launch_ && pressed(BtnTriangle))
    {
        ctl_keep();
        sfx(Sound::LaunchGame);
        const bool launch = ws_game_ && !ws_in_game_;
        close_wii_setup();
        if (launch)
        {
            porpoise::pad::set_wii(WiiConfig{});
            launch_ = ws_game_;
            launch_state_ = ws_state_;
            return Action::Launch;
        }
        return Action::None;
    }
    if (pressed(BtnCircle))
    {
        sfx(Sound::DetailsFlip);
        if (ws_view_ == 1)
        {
            ws_view_ = 0;
            ws_focus_ = kHfAll;
            return Action::None;
        }
        const bool changed = ws_dirty_;
        close_wii_setup();
        return changed && !ws_in_game_ ? Action::SettingsChanged : Action::None;
    }

    /* L1 / R1, or the switcher: another player's controller (4.5 Beta 4).
     * A tap switches, on its release: holding R1 (L1 in the left hand) is
     * still the hub's centring, so a hold never switches. */
    {
        int to = ws_player_;
        const std::uint32_t released = prev_ & ~held_;
        for (const std::uint32_t b : {std::uint32_t(BtnL1), std::uint32_t(BtnR1)})
        {
            if (pressed(b))
            {
                ws_shoulder_ = b;
                ws_shoulder_at_ = time_;
            }
            if ((released & b) && ws_shoulder_ == b)
            {
                if (time_ - ws_shoulder_at_ < 0.28)
                    to = ws_player_ + (b == BtnR1 ? 1 : -1);
                ws_shoulder_ = 0;
            }
        }
        const bool tapped = to != ws_player_;
        const bool on_switcher = ws_pointing_ && hover && hover->row == kHfPlayer;
        if (cross && on_switcher)
            to = hover->choice;
        if (to != ws_player_ && to >= 0 && to < porpoise::pad::kMaxPlayers)
        {
            hub_player(to);
            return Action::None;
        }
        if (tapped)
            sfx(Sound::MenuScroll); /* (the end: nothing further that way) */
        if (cross && on_switcher)
            return Action::None;
    }
    if (ws_player_ > 0)
        return update_hub_player(up, down, left, right, cross, hover);

    /* What the D-pad moves through, top to bottom. */
    std::vector<int> order = {kHfRemote, kHfNunchuk};
    if (ws_view_ == 0)
        order.insert(order.end(), {kHfTiles, kHfAll});
    else
        for (const HubSection &s : secs)
        {
            order.push_back(kHfHead + s.id);
            if (s.id == ws_open_)
                for (const HubRow &r : s.rows)
                    if (hub_focusable(r.kind))
                        order.push_back(r.id);
        }
    auto find_row = [&](int id) -> const HubRow * {
        for (const HubSection &s : secs)
            for (const HubRow &r : s.rows)
                if (r.id == id)
                    return &r;
        return nullptr;
    };
    auto enter = [&](int id) {
        ws_focus_ = id;
        ws_sub_ = 0;
        if (const HubRow *r = find_row(id))
            if (r->kind == kHkSwatch)
                ws_sub_ = std::max(0, r->cur);
    };
    if (std::find(order.begin(), order.end(), ws_focus_) == order.end())
    {
        /* (what it was on went away: the open section's head, else the top) */
        const int head = ws_view_ == 1 && ws_open_ >= 0 ? kHfHead + ws_open_ : kHfRemote;
        enter(std::find(order.begin(), order.end(), head) != order.end() ? head : kHfRemote);
    }
    /* The pointer on something: that's what the D-pad would be on. */
    if (ws_pointing_ && hover && hover->row != 0 && hover->row != kHfPlayer)
    {
        if (hover->row != ws_focus_)
            enter(hover->row);
        if (hover->choice >= 0 && hover->choice < 1000)
        {
            if (hover->row == kHfTiles)
                ws_front_ = hover->choice;
            else if (const HubRow *r = find_row(hover->row))
                if (r->kind == kHkButtons || r->kind == kHkSwatch)
                    ws_sub_ = hover->choice;
        }
    }
    const int idx = int(std::find(order.begin(), order.end(), ws_focus_) - order.begin());
    if (up && idx > 0)
    {
        enter(order[std::size_t(idx - 1)]);
        sfx(Sound::MenuScroll);
    }
    else if (down && idx + 1 < int(order.size()))
    {
        enter(order[std::size_t(idx + 1)]);
        sfx(Sound::MenuScroll);
    }
    auto open_section = [&](int sec) {
        bool exists = false;
        for (const HubSection &s : secs)
            exists |= s.id == sec;
        ws_view_ = 1;
        ws_open_ = exists ? sec : -1;
        enter(exists ? kHfHead + sec : kHfHead + secs.front().id);
        sfx(Sound::DetailsFlip);
    };
    auto coming = [&](bool main, int which) {
        /* One not here yet: what it is, and that it's coming. */
        const char *name = main ? (which == 3 ? "DualShock 4" : which == 2 ? "Wii Remote" : nullptr)
                                : (which == 2 ? "Navigation controller" : nullptr);
        ws_note_ = name ? trf("{what} is available in a future update.", {{"what", tr(name)}})
                        : tr("Available in a future update.");
        ws_note_time_ = time_;
        sfx(Sound::MenuScroll);
    };
    auto choose_controller = [&](int focus, int choice) {
        if (ws_choose(focus == kHfRemote ? kCrRemote : kCrNunchuk, choice))
            ctl_keep();
    };
    auto open_tile = [&](int tile) {
        if (tile == 0)
            open_section(kHsHold);
        else if (tile == 1)
        {
            if (c == WiiGameCube)
            {
                ws_note_ = tr("A GameCube controller has nothing to calibrate.");
                ws_note_time_ = time_;
            }
            else
                ctl_activate(0, pointing ? kHaCalibrate : kHaSetUp);
        }
        else
            open_section(c == WiiGameCube ? kHsButtons : kHsMotion);
    };
    const int dir = left ? -1 : right ? 1 : 0;
    if (dir && !(up || down))
    {
        if (ws_focus_ == kHfRemote || ws_focus_ == kHfNunchuk)
        {
            const bool main = ws_focus_ == kHfRemote;
            const int other = other_of(c);
            const int cur = main ? (other ? (dir > 0 ? -1 : 2) : ws_move_ ? 1 : 0)
                                 : (second_of(c, ws_move_, ws_nav_) < 0 ? (dir > 0 ? -1 : 3)
                                                                         : second_of(c, ws_move_, ws_nav_));
            const int count = main ? 4 : 3;
            bool chose = false;
            for (int k = cur + dir; k >= 0 && k < count; k += dir)
                if (ws_choose(main ? kCrRemote : kCrNunchuk, k))
                {
                    ctl_keep();
                    chose = true;
                    break;
                }
            if (!chose && cur + dir >= 0 && cur + dir < count)
                coming(main, cur + dir); /* only ones not here yet that way */
        }
        else if (ws_focus_ == kHfTiles)
        {
            const int was = ws_front_;
            ws_front_ = std::clamp(ws_front_ + dir, 0, 2);
            if (was != ws_front_)
                sfx(Sound::MovingTab);
        }
        else if (const HubRow *r = find_row(ws_focus_))
        {
            if (r->kind == kHkButtons)
            {
                const int was = ws_sub_;
                ws_sub_ = std::clamp(ws_sub_ + dir, 0, int(r->opts.size()) - 1);
                if (was != ws_sub_)
                    sfx(Sound::MovingTab);
            }
            else if (r->kind == kHkSwatch)
            {
                ws_sub_ = std::clamp(ws_sub_ + dir, 0, int(r->opts.size()) - 1);
                ctl_change(r->id, 0, ws_sub_); /* each color lights at once */
            }
            else
                ctl_change(r->id, dir);
        }
    }
    if (cross)
    {
        int focus = ws_focus_, choice = -1;
        if (ws_pointing_)
        {
            if (!hover)
                return Action::None;
            focus = hover->row;
            choice = hover->choice;
        }
        if (focus == kHfRemote || focus == kHfNunchuk)
        {
            if (choice <= kWsLocked)
                coming(focus == kHfRemote, kWsLocked - choice);
            else if (choice >= 0)
                choose_controller(focus, choice);
            else
                enter(focus == kHfRemote ? kHfNunchuk : ws_view_ == 0 ? kHfTiles : kHfHead + secs.front().id);
        }
        else if (focus == kHfTiles)
            open_tile(choice >= 0 ? choice : ws_front_);
        else if (focus == kHfAll)
        {
            ws_view_ = 1;
            ws_open_ = -1;
            enter(kHfHead + secs.front().id);
            sfx(Sound::DetailsFlip);
        }
        else if (focus >= kHfHead && focus < kHfHead + 100)
        {
            const int sec = focus - kHfHead;
            ws_open_ = ws_open_ == sec ? -1 : sec;
            ws_focus_ = focus;
            sfx(Sound::MovingTab);
        }
        else if (const HubRow *r = find_row(focus))
        {
            if (r->kind == kHkButtons)
            {
                const int b = choice >= 0 ? choice : ws_sub_;
                if (b >= 0 && b < int(r->icons.size()) && r->ok[std::size_t(b)])
                    ctl_activate(r->id, r->icons[std::size_t(b)]);
                else if (b >= 0 && b < int(r->icons.size()) && r->icons[std::size_t(b)] == kHaIgnore)
                    ws_choose(kCrColor, 8); /* (it says why it can't) */
            }
            else if (r->kind == kHkSlider && ws_pointing_ && hover)
            {
                /* Where on the bar the pointer is. */
                const float f = (ws_px_ - (hover->x + 14)) / std::max(1.0f, hover->w - 28);
                ctl_change(r->id, 0, int(std::clamp(f, 0.0f, 1.0f) * 1000));
            }
            else if (choice >= 0)
            {
                if (r->kind == kHkSwatch)
                    ws_sub_ = choice;
                if (r->kind != kHkSeg || r->ok[std::size_t(std::clamp(choice, 0, int(r->ok.size()) - 1))])
                    ctl_change(r->id, 0, choice);
            }
        }
    }
    return Action::None;
}

/* A picture for the hub: what's held, the buttons, the pointer, the ball, the
 * swing, the sensor bar; small for the three tiles. */
/* Beside the in-game menu's Controls tab: the buttons as the hub shows them,
 * for the set-up in play (Oct 25: the old picture there was the DualSense's
 * whatever the hub said). */
void App::draw_hub_buttons(float x, float y, float w, float h, float alpha)
{
    if (!menu_play_ || alpha <= 0.01f)
        return;
    Gfx &g = *g_;
    g.panel(x, y, w, h, rgba(0x0A1236, 0.86f * alpha), 0.85f, kR, with_alpha(rgba(0x3D4F9E), 0.9f * alpha), 1.6f, 0,
            0.10f);
    const bool in = ws_in_game_;
    const int c0 = ws_controller_;
    const bool m0 = ws_move_;
    ws_in_game_ = true; /* (ws_settings(): the game's, as played) */
    ws_controller_ = std::clamp(menu_play_->wii_controller, 0, int(WiiControllerCount) - 1);
    ws_move_ = menu_play_->wii_move;
    const int c = ws_controller_;
    const bool move = ws_move_ && (c == WiiRemote || c == WiiRemoteNunchuk);
    const std::string title = move ? (c == WiiRemoteNunchuk ? (menu_play_->wii_nav ? tr("PlayStation Move + Navigation")
                                                                                  : tr("PlayStation Move + DualSense"))
                                                            : tr("PlayStation Move"))
                                   : tr(wii_controller_name(c));
    g.text_mid(Font::Bold, ts(40), x + 40, y + 52, with_alpha(kWhite, alpha), Align::Left, title);
    g.text_mid(Font::Regular, ts(22), x + 40, y + 94, with_alpha(kLavender, alpha), Align::Left,
               tr("As the Controller Hub has it. Change it there."));
    ctl_scene(kHcButtons, x + 10, y + 120, w - 20, h - 130, alpha, false);
    ws_in_game_ = in;
    ws_controller_ = c0;
    ws_move_ = m0;
}

void App::ctl_scene(int scene, float x, float y, float w, float h, float alpha, bool small)
{
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    const Settings &t = ws_settings();
    const int c = ws_controller_;
    const int other = other_of(c);
    const bool move = ws_move_ && !other;
    const Motion m = porpoise::pad::snapshot(0).motion;
    WiiConfig cfg = t.wii_config(false);
    cfg.controller = c;
    auto A = [&](Color col) { return with_alpha(col, col.a * alpha); };
    const float cx = x + w * 0.5f;
    auto caption = [&](float cxx, float cyy, const std::string &head, const std::string &sub) {
        g.text_mid(Font::Bold, ts(small ? 14 : 16), cxx, cyy, A(kLavender), Align::Center, head, 1.5f);
        if (!sub.empty())
            g.text_mid(Font::Regular, ts(small ? 16 : 19), cxx, cyy + (small ? 24 : 30), A(kSoft), Align::Center, sub);
    };
    auto status = [&](bool good, const std::string &words) {
        if (small)
            return;
        const float sh = 48, sy = y + h - sh - 22;
        g.panel(x + 28, sy, w - 56, sh, A(good ? rgba(0x2E9E5B, 0.20f) : rgba(0xD08A2A, 0.18f)), 1, 18);
        g.panel(x + 52, sy + sh * 0.5f - 6, 12, 12, A(good ? rgba(0x6BE3A8) : rgba(0xFFC85C)), 1, 6);
        g.text_mid(Font::SemiBold, ts(20), x + 76, sy + sh * 0.5f, A(good ? rgba(0x9FF0C8) : rgba(0xFFD58A)), Align::Left,
                   fit(g, Font::SemiBold, ts(20), words, w - 130));
    };
    /* A picture alone, no labels, fitted in a box (its middle at bx). */
    auto picture = [&](int kind, float bx, float top, float bw, float bh, int turn = 0) {
        draw_diagram(kind, {}, bx - bw * 0.5f, top, bw, bh, alpha, turn);
    };
    auto turn_of = [](int pose) {
        return pose == PoseTriggerRight || pose == PoseFacingRight ? 1 : pose == PoseTriggerLeft || pose == PoseFacingLeft ? 2 : 0;
    };
    /* A line under a map, for the hand it doesn't show. */
    auto foot_line = [&](const std::string &line) {
        float ty = y + h - 64;
        for (const std::string &l : wrap(g, Font::Regular, ts(20), line, w - 80, 2))
        {
            g.text_mid(Font::Regular, ts(20), cx, ty, A(kSoft), Align::Center, l);
            ty += 30;
        }
    };
    auto cz_words = [&](const WiiLayout &lay) {
        std::string cz;
        for (int i = 0; i < lay.count; ++i)
            if (lay.binds[i].input == WiC || lay.binds[i].input == WiZ)
                cz += (cz.empty() ? "" : ", ") + std::string(lay.binds[i].input == WiC ? "C " : "Z ") + tr("on") + " " +
                      control_label(lay.binds[i].control);
        return cz;
    };
    /* What the PS3 Eye sees. */
    auto eye_box = [&](float ex, float ey, float ew, float eh) { draw_eye_view(ex, ey, ew, eh, alpha); };
    auto tv = [&](float tx, float ty, float tw, float th) {
        g.panel(tx, ty, tw, th, A(rgba(0xFFFFFF, 0.03f)), 1, 14, A(rgba(0xFFFFFF, 0.38f)), 3.0f);
    };

    switch (scene)
    {
    case kHcHold:
    case kHcOther:
    {
        if (scene == kHcOther && other)
        {
            /* The controller it plays as: the DualSense's buttons for it. */
            if (other == 3)
                draw_diagram(kDiaDualSense, gc_labels(t.mapping()), x + 10, y + 10, w - 20, h - 20, alpha);
            else
            {
                const int pose = other == 2 ? PoseFlat : expected_pose(cfg, false);
                draw_diagram(kDiaDualSense, ds_labels(wii_layout(cfg, false, pose), false), x + 10, y + 10, w - 20, h - 20,
                             alpha);
            }
            return;
        }
        /* The TV at the top: everything points to it. */
        if (!small)
        {
            g.panel(cx - w * 0.22f, y + 30, w * 0.44f, 9, A(rgba(0xFFFFFF, 0.2f)), 1, 4.5f);
            g.text_mid(Font::Bold, ts(14), cx, y + 58, A(kLavender), Align::Center, tr("YOUR TV"), 1.5f);
        }
        const float top = y + (small ? 6 : 96), bottom = y + h - (small ? 52 : 150);
        const float room = bottom - top;
        const int pose = m.valid && !move ? m.pose : expected_pose(cfg, false);
        if (other)
        {
            /* A Classic Controller, a sideways Remote, a GameCube controller: the
             * DualSense plays as it. */
            const int p2 = other == 2 ? PoseFlat : pose;
            picture(kDiaDualSense, cx, top, w * 0.66f, room, turn_of(p2));
            caption(cx, bottom + 22,
                    tr(other == 1 ? "AS A CLASSIC CONTROLLER" : other == 2 ? "AS A REMOTE, SIDEWAYS" : "AS A GAMECUBE CONTROLLER"),
                    small ? "" : tr("Both hands, face up"));
            status(true, tr("The DualSense plays as it."));
        }
        else if (move)
        {
            const bool with_ds = c == WiiRemoteNunchuk;
            const bool ds_right = with_ds && t.wii_nunchuk_hand == 1; /* (the DualSense in the right hand) */
            const float mx = with_ds ? x + w * (ds_right ? 0.30f : 0.70f) : cx;
            if (with_ds)
            {
                const float dx = x + w * (ds_right ? 0.70f : 0.30f);
                picture(kDiaDualSense, dx, top, w * 0.48f, room);
                caption(dx, bottom + 22, ds_right ? tr("RIGHT HAND") : tr("LEFT HAND"),
                        small ? "" : ds_right ? tr("Right stick, face buttons as the D-pad, R1, R2") : tr("Stick, D-pad, L1, L2"));
            }
            picture(kDiaMove, mx, top, w * 0.3f, room);
            caption(mx, bottom + 22, with_ds ? (ds_right ? tr("LEFT HAND") : tr("RIGHT HAND")) : tr("IN YOUR HAND"),
                    small ? "" : tr("Light toward the TV"));
            const porpoise::move::Status ms = porpoise::move::status();
            status(ms.connected, ms.connected ? tr("The Move is on.") : tr("The Move isn't on: press its PS button."));
        }
        else if (c == WiiTwoControllers)
        {
            picture(kDiaDualSense, x + w * 0.27f, top, w * 0.44f, room, turn_of(expected_pose(cfg, true)));
            picture(kDiaDualSense, x + w * 0.73f, top, w * 0.44f, room, turn_of(expected_pose(cfg, false)));
            caption(x + w * 0.27f, bottom + 22, tr("LEFT HAND"), small ? "" : tr("The Nunchuk"));
            caption(x + w * 0.73f, bottom + 22, tr("RIGHT HAND"), small ? "" : tr("The Wii Remote"));
            const bool second = porpoise::pad::connected(1);
            status(second, second ? tr("Both DualSense controllers are on.") : tr("The second DualSense isn't on yet."));
        }
        else
        {
            /* One DualSense, as it's held (or will be). */
            picture(kDiaDualSense, cx, top, w * 0.7f, room, turn_of(pose));
            caption(cx, bottom + 22, c == WiiRemote ? tr("THE WII REMOTE") : tr("THE WII REMOTE AND THE NUNCHUK"),
                    small ? "" : tr(pose_words(pose)));
            status(m.valid, m.valid ? tr("Porpoise sees it held this way.") : tr("Turn the controller on and pick it up."));
        }
        return;
    }
    case kHcButtons:
    {
        if (other == 3)
        {
            draw_diagram(kDiaDualSense, gc_labels(t.mapping()), x + 10, y + 10, w - 20, h - 20, alpha);
            return;
        }
        if (move)
        {
            draw_diagram(kDiaMove, move_labels(), x + 10, y + 10, w - 20, h - (c == WiiRemoteNunchuk ? 100 : 20), alpha);
            if (c == WiiRemoteNunchuk)
            {
                const std::string cz = cz_words(wii_layout(cfg, false, PoseFlat));
                foot_line((t.wii_nunchuk_hand == 1 ? tr("DualSense in your right hand: its right stick is the Nunchuk's, "
                                                         "the face buttons the D-pad")
                                                    : tr("DualSense in your other hand: the Nunchuk's stick")) +
                          (cz.empty() ? "" : ", " + cz) + ".");
            }
            return;
        }
        /* (drawn face up whatever the hold: How you hold it shows the hold) */
        const int pose = other == 2 ? PoseFlat : expected_pose(cfg, false);
        const WiiLayout lay = wii_layout(cfg, false, pose);
        if (c == WiiTwoControllers)
        {
            /* The right hand's DualSense (the Remote) mapped; the left one's in a line. */
            draw_diagram(kDiaDualSense, ds_labels(lay, false), x + 10, y + 10, w - 20, h - 100, alpha);
            const std::string cz = cz_words(wii_layout(cfg, true, expected_pose(cfg, true)));
            foot_line(tr("The left DualSense is the Nunchuk: its stick") + (cz.empty() ? "" : ", " + cz) + ".");
            return;
        }
        draw_diagram(kDiaDualSense, ds_labels(lay, false), x + 10, y + 10, w - 20, h - 20, alpha);
        return;
    }
    case kHcPointer:
    {
        const float tw = small ? w * 0.86f : w * 0.72f, th = tw * 9.0f / 16.0f;
        const float tx = cx - tw * 0.5f, ty = y + (small ? (h - th) * 0.5f : 96);
        if (move && !small)
            picture(kDiaEye, cx, ty - 82, 120, 92); /* the PS3 Eye, on top of the TV */
        tv(tx, ty, tw, th);
        if (m.valid && m.centred)
        {
            const int n = std::min(ws_trail_n_, 10);
            for (int i = 1; i < n; ++i)
            {
                const float *p = ws_trail_[(ws_trail_n_ - 1 - i) % 10];
                const float px = tx + tw * 0.5f + p[0] * tw * 0.47f, py = ty + th * 0.5f + p[1] * th * 0.47f;
                g.panel(px - 4, py - 4, 8, 8, A(rgba(0x5CD3FF, 0.45f - 0.04f * float(i))), 1, 4);
            }
            const float px = tx + tw * 0.5f + std::clamp(m.aim_x, -1.0f, 1.0f) * tw * 0.47f;
            const float py = ty + th * 0.5f + std::clamp(m.aim_y, -1.0f, 1.0f) * th * 0.47f;
            g.blob(px, py, 70, 70, A(rgba(0x5CD3FF, 0.55f)));
            g.panel(px - 13, py - 13, 26, 26, A(rgba(0xFFFFFF)), 1, 13);
        }
        else
            g.text_mid(Font::SemiBold, ts(small ? 17 : 21), cx, ty + th * 0.5f, A(kLavender), Align::Center,
                       move ? tr("Hold Square a moment to center") : tr("Hold R1 a moment to center"));
        if (small)
            return;
        if (move)
            eye_box(tx + tw - 224, ty + th + 22, 224, 168);
        const float textw = move ? tw - 252 : tw;
        const std::string title = move ? tr("The Move aims") : tr(cfg.pointer == PointerTouch ? "Touch pad"
                                                                 : cfg.pointer == PointerStick ? "Right stick"
                                                                                               : "Gyro");
        const std::string body = move ? tr("The Move's own motion aims. The camera shows where its ball is: keep "
                                           "it in view.")
                                 : cfg.pointer == PointerTouch ? tr("Slide a finger on the touch pad. Good for menus and slow "
                                                                    "games.")
                                 : cfg.pointer == PointerStick ? tr("Push the right stick to move it. No motion needed.")
                                                               : tr("Turn the controller to aim. Hold R1 a moment to center the "
                                                                    "pointer again.");
        g.text_mid(Font::Bold, ts(26), tx, ty + th + 46, A(kWhite), Align::Left, title);
        float by = ty + th + 86;
        for (const std::string &l : wrap(g, Font::Regular, ts(19), body, textw, 3))
        {
            g.text_mid(Font::Regular, ts(19), tx, by, A(kSoft), Align::Left, l);
            by += 28;
        }
        return;
    }
    case kHcBall:
    {
        picture(kDiaMove, x + w * 0.30f, y + 40, w * 0.36f, h - 150);
        caption(x + w * 0.30f, y + h - 74, tr(porpoise::move::ball_name(settings_->move_color)), "");
        eye_box(x + w * 0.56f, y + 120, w * 0.38f, w * 0.38f * 0.75f);
        const porpoise::move::Status ms = porpoise::move::status();
        const float ty = y + 120 + w * 0.38f * 0.75f + 40;
        const std::string words = ms.camera_state != 4 ? tr("Turn on the PS3 Eye in Settings to see what it sees.")
                                  : ms.ball            ? tr("It sees the ball.")
                                                       : tr("Hold the Move where the camera can see it.");
        float ly = ty;
        for (const std::string &l : wrap(g, Font::Regular, ts(19), words, w * 0.38f, 3))
        {
            g.text_mid(Font::Regular, ts(19), x + w * 0.56f, ly, A(kSoft), Align::Left, l);
            ly += 28;
        }
        return;
    }
    case kHcMotion:
    {
        const float gx = x + (small ? 0 : 32), gw = w - (small ? 0 : 64);
        const float gy = y + (small ? 0 : 76), gh = small ? h : 300;
        const float max_g = 4.0f;
        auto yof = [&](float v) { return gy + gh - 14 - std::clamp(v / max_g, 0.0f, 1.0f) * (gh - 28); };
        if (!small)
        {
            g.text_mid(Font::Bold, ts(15), gx, y + 40, A(kLavender), Align::Left,
                       move ? tr("LIVE \xC2\xB7 THE MOVE") : tr("LIVE \xC2\xB7 THE DUALSENSE"), 1.5f);
            char now[32];
            std::snprintf(now, sizeof now, "%.2f g", double(ws_trace_[(ws_trace_n_ + 179) % 180]));
            g.text_mid(Font::SemiBold, ts(18), gx + gw, y + 40, A(kSoft), Align::Right, now);
            g.panel(gx, gy, gw, gh, A(rgba(0xFFFFFF, 0.03f)), 1, 18);
        }
        /* Light, medium and strong: dashed lines across. */
        const float zones[2] = {1.3f, 2.6f};
        for (int z = 0; z < 2; ++z)
        {
            const float ly = yof(zones[z]);
            for (float d = gx + 8; d < gx + gw - 8; d += 16)
                g.panel(d, ly - 1, 8, 2, A(z == 0 ? rgba(0xFFC85C, 0.7f) : rgba(0xFF8A8A, 0.6f)), 1, 1);
            if (!small)
                g.text_mid(Font::Bold, ts(13), gx + 14, ly - 12, A(z == 0 ? rgba(0xFFC85C) : rgba(0xFF9D9D)), Align::Left,
                           z == 0 ? tr("MEDIUM") : tr("STRONG"), 1.2f);
        }
        const int n = std::min(ws_trace_n_, 180);
        float px = 0, py = 0;
        for (int i = 0; i < n; ++i)
        {
            const float v = ws_trace_[(ws_trace_n_ - n + i) % 180];
            const float sx = gx + 8 + (gw - 16) * float(180 - n + i) / 179.0f, sy = yof(v);
            if (i > 0)
                ctl_line(px, py, sx, sy, small ? 3.0f : 3.5f, A(rgba(0x5CD3FF)));
            px = sx, py = sy;
        }
        if (n < 180)
            ctl_line(gx + 8, yof(0), gx + 8 + (gw - 16) * float(180 - n) / 179.0f, yof(0), small ? 3.0f : 3.5f,
                     A(rgba(0x5CD3FF, 0.5f))); /* (before the first samples: still) */
        if (small)
            return;
        /* The numbers under it. */
        const float sy = gy + gh + 24, sw = (gw - 28) / 3.0f, sh = 108;
        const bool recent = time_ - ws_last_swing_at_ < 6;
        const char *verdict = !recent ? "\xE2\x80\x94"
                              : ws_last_swing_ < 1.3f ? "Light"
                              : ws_last_swing_ < 2.6f ? "Medium"
                                                      : "Strong";
        const std::string stats[3][2] = {
            {tr("LAST SWING"), recent ? tr(verdict) : std::string(verdict)},
            {move ? tr("SWING STRENGTH") : tr("FLICK TO SHAKE"), move ? std::to_string(t.wii_swing) + "%" : (t.wii_shake ? tr("On") : tr("Off"))},
            {tr("STEADINESS"), tr(kSteadyNames[feel_of(t.wii_smooth)])},
        };
        for (int i = 0; i < 3; ++i)
        {
            const float bx = gx + float(i) * (sw + 14);
            g.panel(bx, sy, sw, sh, A(rgba(0xFFFFFF, 0.05f)), 1, 18);
            g.text_mid(Font::Bold, ts(14), bx + 20, sy + 30, A(kLavender), Align::Left, stats[i][0], 1.4f);
            g.text_mid(Font::Bold, ts(30), bx + 20, sy + 72, A(kWhite), Align::Left, stats[i][1]);
        }
        float ty = sy + sh + 40;
        for (const std::string &l :
             wrap(g, Font::Regular, ts(19),
                  tr("Swing or flick the controller: the line shows how hard. Games count a swing once it's past the "
                     "first line; Swing strength scales how hard the Move's swings land."),
                  gw, 3))
        {
            g.text_mid(Font::Regular, ts(19), gx, ty, A(kSoft), Align::Left, l);
            ty += 28;
        }
        return;
    }
    case kHcSensor:
    {
        const float tw = w * 0.6f, th = tw * 9.0f / 16.0f, tx = cx - tw * 0.5f, ty = y + 100;
        tv(tx, ty, tw, th);
        const float bw = tw * 0.42f;
        for (float d = cx - bw * 0.5f; d < cx + bw * 0.5f; d += 18)
            g.panel(d, ty + th + 22, 10, 6, A(rgba(0xFFFFFF, 0.45f)), 1, 3);
        /* A lock. */
        const float lx = cx, ly = ty + th * 0.5f - 10;
        g.panel(lx - 14, ly - 4, 28, 24, A(with_alpha(kLavender, 0.9f)), 1, 5);
        g.panel(lx - 9, ly - 20, 18, 22, A(rgba(0xFFFFFF, 0.0f)), 0, 9, A(with_alpha(kLavender, 0.9f)), 3.0f);
        g.text_mid(Font::Bold, ts(22), cx, ly + 52, A(kWhite), Align::Center, tr("Coming with real Wii Remotes"));
        float ty2 = ty + th + 76;
        for (const std::string &l : wrap(g, Font::Regular, ts(19),
                                         tr("The PlayStation controllers don't need a sensor bar: the gyro or the camera "
                                            "does its job. It stays here so you know where it will be."),
                                         tw + 80, 3))
        {
            g.text_mid(Font::Regular, ts(19), cx, ty2, A(kSoft), Align::Center, l);
            ty2 += 28;
        }
        return;
    }
    default: /* kHcCal: the five points on a screen, done or not */
    {
        const float tw = std::min(w * 0.82f, h * 1.5f), th = tw * 9.0f / 16.0f, tx = cx - tw * 0.5f, ty = y + (h - th) * 0.5f;
        tv(tx, ty, tw, th);
        const bool done = t.wii_speed == 0 && t.wii_screen_x > 0;
        const int sign[5][2] = {{0, 0}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 5; ++i)
        {
            const float px = cx + float(sign[i][0]) * tw * 0.38f, py = ty + th * 0.5f + float(sign[i][1]) * th * 0.34f;
            if (done)
            {
                g.panel(px - 15, py - 15, 30, 30, A(rgba(0x2E9E5B)), 1, 15);
                draw_check_mark(px, py, 18, true);
            }
            else
                g.panel(px - 13, py - 13, 26, 26, A(rgba(0xFFFFFF, 0.0f)), 0, 13, A(rgba(0x8BD9FF, 0.8f)), 3.0f);
        }
        return;
    }
    }
}

void App::draw_ctl_hub(double time)
{
    Gfx &g = *g_;
    ws_hits_.clear();
    const Settings &t = ws_settings();
    const int c = ws_controller_;
    const int other = other_of(c);
    const bool move = ws_move_ && !other;
    const bool focusing = ws_ptr_a_ < 0.5f;
    const std::vector<HubSection> secs = ctl_sections();

    /* The title, the game, and who's playing; the four players at the right. */
    g.text_mid(Font::Bold, ts(40), 90, 66, kWhite, Align::Left, tr("Controller Hub"));
    {
        std::string sub = ws_game_ ? ws_game_->title + "   \xE2\x80\xA2   " : std::string();
        sub += trf("Player {n}", {{"n", std::to_string(ws_player_ + 1)}});
        if (ws_player_ > 0)
            sub += "   \xE2\x80\xA2   " + tr("Saved for every Wii game");
        else if (ws_game_)
            sub += "   \xE2\x80\xA2   " + (ws_scope_ == 0 ? tr("Saved for just this game") : tr("Saved for every Wii game"));
        g.text_mid(Font::Regular, ts(22), 90, 108, kLavender, Align::Left, fit(g, Font::Regular, ts(22), sub, 900));
    }
    draw_hub_switcher(time);
    if (ws_player_ > 0)
    {
        draw_hub_player(time);
        return;
    }
    if (const Advice *a = advice_for(ws_game_))
    {
        const bool match = a->controller == c;
        const std::string line = match ? tr("Recommended for this game") : tr("Recommended: ") + tr(wii_controller_name(a->controller));
        g.text_mid(Font::SemiBold, ts(21), 1830, 108, match ? rgba(0x6BE3A8) : rgba(0xFFC85C), Align::Right,
                   fit(g, Font::SemiBold, ts(21), line, 640));
    }

    /* A controller tile: its picture and its name; the ones that aren't here yet
     * greyed (choosing one says it's coming: Oct 24, the tag clipped its edge). */
    ctl_art(-1, 0, 0, 0, kClear); /* (loads the pictures) */
    auto tile = [&](int focus, int choice, float tx, float top, float tw, float th, const std::string &label,
                    const std::string &tag, int icon, bool on, bool ok, bool focused, float fade) {
        const int hit = int(ws_hits_.size());
        ws_hits_.push_back({tx, top, tw, th, focus, ok ? choice : kWsLocked - choice});
        const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit && ok;
        const float a = (ok ? 1.0f : 0.34f) * fade;
        const Color fill = on ? rgba(0x1D45B8, 0.92f) : hovered ? rgba(0x14307E, 0.9f) : rgba(0x0B1747, 0.70f);
        const Color rim = focused || hovered ? kIcy : on ? rgba(0x6F8FF0) : rgba(0x3D4F9E, 0.8f);
        g.panel(tx, top, tw, th, with_alpha(fill, fill.a * a), 0.8f, 22, with_alpha(rim, rim.a * a),
                focused || hovered ? 2.8f : 1.4f, focused ? 8.0f : 0.0f, 0.12f);
        const Color ink = with_alpha(on || hovered || focused ? kWhite : kSoft, a);
        const float icon_cy = top + th * 0.40f, size = std::min(th * 0.62f, tw * 0.6f);
        if (icon >= 0 && gl_ctl_)
        {
            if (icon == 13)
            {
                /* The Remote held sideways: its picture turned a quarter (its top to the left). */
                const float hs = size * 0.5f;
                const Corner q[4] = {{tx + tw * 0.5f - hs, icon_cy + hs, 1}, {tx + tw * 0.5f - hs, icon_cy - hs, 1},
                                     {tx + tw * 0.5f + hs, icon_cy - hs, 1}, {tx + tw * 0.5f + hs, icon_cy + hs, 1}};
                const float uv[4] = {3.0f / 10.0f, 0.0f, 4.0f / 10.0f, 1.0f};
                g.quad3d(gl_ctl_, q, size, size, g.toned() ? g.tone(ink) : ink, 0.01f, false, false, uv);
            }
            else
            {
                ctl_art(icon, tx + tw * 0.5f, icon_cy, size, ink);
                if (icon == 2)
                    draw_move_ball(tx + tw * 0.5f, icon_cy, size, a);
            }
        }
        else
            g.panel(tx + tw * 0.5f - 18, icon_cy - 2, 36, 4, ink, 1, 2); /* None: a dash */
        (void)tag;
        const float ly = top + th * 0.82f;
        g.text_mid(Font::SemiBold, ts(20), tx + tw * 0.5f, ly, ink, Align::Center,
                   fit(g, Font::SemiBold, ts(20), label, tw - 16));
    };

    /* The main controller, then the second one: side by side across the top. */
    {
        const float top = 180, th = 106, gap = 14, group = 48;
        const float tw = (1740 - 5 * gap - group) / 7.0f;
        const float x1 = 90, x2 = 90 + 4 * tw + 3 * gap + group;
        const float dim = other ? 0.55f : 1.0f;
        auto heading = [&](float hx, const std::string &name, const std::string &hint, bool on) {
            const float w = g.text_mid(Font::Bold, ts(22), hx, 158, with_alpha(on ? kWhite : kSoft, dim), Align::Left, name);
            g.text_mid(Font::Regular, ts(18), hx + w + 12, 158, with_alpha(kLavender, 0.9f * dim), Align::Left, hint);
        };
        const bool on_main = focusing && ws_focus_ == kHfRemote, on_second = focusing && ws_focus_ == kHfNunchuk;
        heading(x1, tr("Main controller"), tr("plays as the Wii Remote"), on_main);
        heading(x2, tr("Second controller"), tr("plays as the Nunchuk"), on_second);
        const int main_on = other ? -1 : ws_move_ ? 1 : 0;
        const std::string names[4] = {tr("DualSense"), tr("PlayStation Move"), tr("Wii Remote"), tr("DualShock 4")};
        const int icons[4] = {0, 2, 3, 1};
        for (int i = 0; i < 4; ++i)
            tile(kHfRemote, i, x1 + float(i) * (tw + gap), top, tw, th, names[i], i >= 2 ? tr("Coming soon") : "", icons[i],
                 i == main_on, i < 2, on_main && i == main_on, dim);
        const int second_on = other ? -1 : second_of(c, ws_move_, ws_nav_);
        for (int i = 0; i < 3; ++i)
        {
            const std::string label = ws_move_ ? (i == 0 ? tr("None") : i == 1 ? tr("DualSense") : tr("Navigation"))
                                               : (i == 0 ? tr("None") : i == 1 ? tr("Same DualSense") : tr("Second DualSense"));
            tile(kHfNunchuk, i, x2 + float(i) * (tw + gap), top, tw, th, label, "",
                 i == 0 ? -1 : ws_move_ && i == 2 ? 9 : 0, i == second_on, true, on_second && i == second_on, dim);
        }
        if (other)
            g.text_mid(Font::SemiBold, ts(18), 1830, 158, rgba(0xFFC85C), Align::Right,
                       trf("Playing as: {what}", {{"what", tr(wii_controller_name(c))}}));
        else
        {
            /* What the two rows add up to (Oct 25: one DualSense as the Remote
             * and the Nunchuk is "Same DualSense"). */
            const std::string what = ws_move_ ? (c == WiiRemote ? tr("Remote, the Move")
                                                 : ws_nav_      ? tr("Remote + Nunchuk, Move and Navigation")
                                                                : tr("Remote + Nunchuk, Move and DualSense"))
                                     : c == WiiRemote          ? tr("Remote, one DualSense")
                                     : c == WiiTwoControllers  ? tr("Remote + Nunchuk, two DualSense")
                                                               : tr("Remote + Nunchuk, one DualSense");
            g.text_mid(Font::SemiBold, ts(18), 1830, 158, kLavender, Align::Right, trf("Playing as: {what}", {{"what", what}}));
        }
    }

    const float pulse = 1.0f;
    (void)pulse;
    if (ws_view_ == 0)
    {
        /* The three tiles, and All settings under them. */
        const float top = kHubTop, th = 560, gap = 26;
        const float tw = (1740 - 2 * gap) / 3.0f;
        const bool pointing = points(c);
        for (int i = 0; i < 3; ++i)
        {
            const float tx = 90 + float(i) * (tw + gap);
            const int hit = int(ws_hits_.size());
            ws_hits_.push_back({tx, top, tw, th, kHfTiles, i});
            const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit;
            const bool focused = (focusing && ws_focus_ == kHfTiles && ws_front_ == i) || hovered;
            g.panel(tx, top, tw, th, focused ? rgba(0x14307E, 0.88f) : rgba(0x0B1747, 0.62f), 0.8f, 30,
                    focused ? kIcy : rgba(0x3D4F9E, 0.8f), focused ? 2.8f : 1.4f, focused ? 10.0f : 0.0f, 0.12f);
            g.text_mid(Font::Bold, ts(18), tx + 34, top + 44, kLavender, Align::Left, std::to_string(i + 1), 1.5f);
            const bool gc_buttons = i == 2 && c == WiiGameCube; /* (no motion to feel: its buttons) */
            const std::string title = i == 0 ? tr("Hold it") : i == 1 ? tr("Calibrate") : gc_buttons ? tr("Buttons") : tr("Feel");
            g.text_mid(Font::Bold, ts(38), tx + 34, top + 88, kWhite, Align::Left, title);
            const float px = tx + 30, py = top + 128, pw = tw - 60, ph = th - 128 - 84;
            if (gc_buttons)
                draw_diagram(kDiaDualSense, {}, px, py, pw, ph, 1.0f);
            else if (i == 2)
            {
                /* Feel: the swing as it comes, and how steady the pointer is, big. */
                ctl_scene(kHcMotion, px, py + 20, pw, ph - 110, 1.0f, true);
                g.text_mid(Font::Bold, ts(32), px, py + ph - 40, kWhite, Align::Left, tr(kSteadyNames[feel_of(t.wii_smooth)]));
                g.text_mid(Font::Regular, ts(18), px, py + ph - 4, kLavender, Align::Left, tr("pointer steadiness"));
            }
            else
                ctl_scene(i == 0 ? kHcHold : kHcCal, px, py, pw, ph, 1.0f, true);
            /* What it's at now. */
            std::string foot;
            Color ink = kSoft;
            const Motion m = porpoise::pad::snapshot(0).motion;
            if (i == 0)
            {
                if (move)
                {
                    const bool on = porpoise::move::status().connected;
                    foot = on ? tr("The Move is on") : tr("Turn the Move on");
                    ink = on ? rgba(0x8FF0BD) : rgba(0xFFC85C);
                }
                else
                {
                    foot = m.valid ? tr("Porpoise sees it held right") : tr("Pick up the controller");
                    ink = m.valid ? rgba(0x8FF0BD) : rgba(0xFFC85C);
                }
            }
            else if (i == 1)
            {
                const bool done = t.wii_speed == 0 && t.wii_screen_x > 0;
                foot = c == WiiGameCube ? tr("Nothing to calibrate")
                       : !pointing     ? tr("Level it, then play")
                       : done          ? tr("Five points done")
                                       : tr("Not done yet: about 30 seconds");
                ink = done || !pointing ? rgba(0x8FF0BD) : rgba(0xFFC85C);
            }
            else if (gc_buttons)
            {
                foot = tr("The GameCube controller's layout");
                ink = kSoft;
            }
            else
            {
                foot = move ? tr("Swing strength") + " " + std::to_string(t.wii_swing) + "%"
                            : tr("Flick to shake") + " " + (t.wii_shake ? tr("on") : tr("off"));
                ink = kSoft;
            }
            /* A dot in the line's color: green when it's set, amber when it wants doing. */
            g.panel(tx + 36, top + th - 50, 12, 12, ink, 1, 6);
            g.text_mid(Font::SemiBold, ts(21), tx + 58, top + th - 44, ink, Align::Left,
                       fit(g, Font::SemiBold, ts(21), foot, tw - 100));
        }
        /* All settings. */
        {
            const float y = top + th + 22, h = 64;
            const int hit = int(ws_hits_.size());
            ws_hits_.push_back({90, y, 1740, h, kHfAll, 0});
            const bool focused = (focusing && ws_focus_ == kHfAll) || (ws_ptr_a_ > 0.5f && ws_hover_ == hit);
            g.panel(90, y, 1740, h, focused ? rgba(0x14307E, 0.88f) : rgba(0x0B1747, 0.55f), 0.8f, 32,
                    focused ? kIcy : rgba(0x3D4F9E, 0.7f), focused ? 2.6f : 1.2f, focused ? 8.0f : 0.0f, 0.1f);
            for (int k = 0; k < 3; ++k)
                g.panel(124, y + 20 + float(k) * 10, 26, 4, focused ? kWhite : kSoft, 1, 2);
            const float w = g.text_mid(Font::Bold, ts(24), 172, y + h * 0.5f, kWhite, Align::Left, tr("All settings"));
            std::string list;
            for (const HubSection &s : secs)
                if (!s.locked)
                    list += (list.empty() ? "" : "  \xC2\xB7  ") + s.name;
            g.text_mid(Font::Regular, ts(19), 172 + w + 24, y + h * 0.5f, kLavender, Align::Left,
                       fit(g, Font::Regular, ts(19), list, 1740 - w - 200));
            g.glyph(Glyph::Arrow, 1796, y + h * 0.5f, 16, focused ? kWhite : kSoft, kPi * 0.5f);
        }
    }
    else
    {
        /* The picture: the open section's (it cross-fades as sections change). */
        const int scene = ws_open_ == kHsButtons ? kHcButtons
                          : ws_open_ == kHsPointer ? kHcPointer
                          : ws_open_ == kHsBall    ? kHcBall
                          : ws_open_ == kHsMotion  ? kHcMotion
                          : ws_open_ == kHsOther   ? kHcOther
                          : ws_open_ == kHsSensor  ? kHcSensor
                                                   : kHcHold;
        const bool snap = ws_pic_ == -2;
        if (ws_pic_ == -2)
        {
            ws_pic_ = scene; /* (shown at once) */
            ws_pic_at_ = -10;
        }
        if (scene != ws_pic_)
        {
            ws_pic_was_ = ws_pic_;
            ws_pic_ = scene;
            ws_pic_at_ = time_;
        }
        const float k = settings_->reduced_motion ? 1.0f : float(std::clamp((time_ - ws_pic_at_) / 0.24, 0.0, 1.0));
        const float ease = k * k * (3 - 2 * k);
        g.panel(kHubLeftX, kHubTop, kHubLeftW, kHubBottom - kHubTop, rgba(0x0A1236, 0.5f), 0.8f, 30, rgba(0x3D4F9E, 0.7f),
                1.2f);
        if (ws_pic_was_ >= 0 && ease < 1)
            ctl_scene(ws_pic_was_, kHubLeftX, kHubTop, kHubLeftW, kHubBottom - kHubTop, 1.0f - ease, false);
        ctl_scene(ws_pic_, kHubLeftX, kHubTop, kHubLeftW, kHubBottom - kHubTop, ease, false);

        /* The sections: laid out first (for the scroll), then drawn. */
        struct Item
        {
            int kind; /* 0 a head, 1 a row, 2 a body's top, 3 a body's bottom */
            const HubSection *s;
            const HubRow *r;
            float y, h;
        };
        std::vector<Item> items;
        float y = 0;
        for (const HubSection &s : secs)
        {
            items.push_back({0, &s, nullptr, y, kHubHeadH});
            y += kHubHeadH;
            if (s.id == ws_open_)
            {
                items.push_back({2, &s, nullptr, y, 10});
                y += 10;
                for (const HubRow &r : s.rows)
                {
                    float rh = kHubRowH;
                    if (r.kind == kHkTiles)
                        rh = 124;
                    else if (r.kind == kHkText)
                        rh = 18 + 28 * float(wrap(g, Font::Regular, ts(19), r.value, kHubColW - 100, 4).size());
                    items.push_back({1, &s, &r, y, rh});
                    y += rh;
                }
                items.push_back({3, &s, nullptr, y, 12});
                y += 12;
            }
            y += kHubGap;
        }
        const float area = kHubBottom - kHubTop, total = y - kHubGap;
        /* Keep what's in focus (and its section's head) in view. */
        float want = ws_scroll_to_;
        for (const Item &it : items)
        {
            const int id = it.kind == 0 ? kHfHead + it.s->id : it.kind == 1 ? it.r->id : -1;
            if (id != ws_focus_ || id < 0)
                continue;
            float lo = it.y, hi = it.y + it.h;
            if (it.kind == 1)
                for (const Item &h2 : items)
                    if (h2.kind == 0 && h2.s == it.s)
                        lo = std::min(lo, h2.y);
            if (it.kind == 0 && it.s->id == ws_open_)
                for (const Item &b : items)
                    if (b.s == it.s && b.kind == 3)
                        hi = std::min(it.y + area, b.y + b.h);
            if (hi - want > area)
                want = hi - area;
            if (lo < want)
                want = lo;
        }
        ws_scroll_to_ = std::clamp(want, 0.0f, std::max(0.0f, total - area));
        if (snap)
            ws_scroll_ = ws_scroll_to_;
        if (ws_scroll_ > ws_scroll_to_ + area || ws_scroll_ < ws_scroll_to_ - area)
            ws_scroll_ = ws_scroll_to_;

        const float x = kHubColX, w = kHubColW;
        auto head_icon = [&](int sec, float icx, float icy, Color col) {
            switch (sec)
            {
            case kHsHold:
                ctl_art(ws_move_ && !other ? 2 : 0, icx, icy, 40, col);
                if (ws_move_ && !other)
                    draw_move_ball(icx, icy, 40, col.a);
                break;
            case kHsButtons:
                g.panel(icx - 4, icy - 13, 8, 8, col, 1, 4);
                g.panel(icx - 4, icy + 5, 8, 8, col, 1, 4);
                g.panel(icx - 13, icy - 4, 8, 8, col, 1, 4);
                g.panel(icx + 5, icy - 4, 8, 8, col, 1, 4);
                break;
            case kHsPointer:
                g.panel(icx - 9, icy - 9, 18, 18, with_alpha(col, 0.0f), 0, 9, col, 2.4f);
                g.panel(icx - 1.2f, icy - 15, 2.4f, 6, col, 1, 1);
                g.panel(icx - 1.2f, icy + 9, 2.4f, 6, col, 1, 1);
                g.panel(icx - 15, icy - 1.2f, 6, 2.4f, col, 1, 1);
                g.panel(icx + 9, icy - 1.2f, 6, 2.4f, col, 1, 1);
                break;
            case kHsBall:
                in_true_color([&] {
                    g.blob(icx, icy, 34, 34, rgba(ball_hex(), 0.5f * col.a));
                    g.panel(icx - 10, icy - 10, 20, 20, rgba(ball_hex(), col.a), 1, 10);
                });
                break;
            case kHsMotion:
            {
                const float p[6][2] = {{-14, 0}, {-7, 0}, {-3, -10}, {3, 10}, {7, -4}, {14, 0}};
                for (int i = 1; i < 6; ++i)
                    ctl_line(icx + p[i - 1][0], icy + p[i - 1][1], icx + p[i][0], icy + p[i][1], 2.4f, col);
                break;
            }
            case kHsOther: ctl_art(4, icx, icy, 40, col); break;
            case kHsSensor:
                g.panel(icx - 14, icy - 4, 28, 8, with_alpha(col, 0.0f), 0, 4, col, 2.0f);
                g.panel(icx - 9, icy - 1.5f, 3, 3, col, 1, 1.5f);
                g.panel(icx + 6, icy - 1.5f, 3, 3, col, 1, 1.5f);
                break;
            default:
            {
                /* A bookmark. */
                ctl_line(icx - 7, icy - 11, icx + 7, icy - 11, 2.4f, col);
                ctl_line(icx + 7, icy - 11, icx + 7, icy + 11, 2.4f, col);
                ctl_line(icx + 7, icy + 11, icx, icy + 5, 2.4f, col);
                ctl_line(icx, icy + 5, icx - 7, icy + 11, 2.4f, col);
                ctl_line(icx - 7, icy + 11, icx - 7, icy - 11, 2.4f, col);
                break;
            }
            }
        };
        /* A row of choices, one chosen. */
        auto segs = [&](const HubRow &r, float sx, float cy, float sw, bool row_on, float a) {
            const float sh = 48;
            float size = ts(19);
            std::vector<float> wd(r.opts.size());
            float total_w = 0;
            for (std::size_t i = 0; i < r.opts.size(); ++i)
            {
                wd[i] = g.measure(Font::SemiBold, size, r.opts[i]) + 40;
                total_w += wd[i];
            }
            const float inner = sw - 8;
            if (total_w > inner)
            {
                size *= inner / total_w;
                for (float &e : wd)
                    e *= inner / total_w;
                total_w = inner;
            }
            const float bw = total_w + 8;
            g.panel(sx, cy - sh * 0.5f, bw, sh, rgba(0x0B1747, 0.75f * a), 1, sh * 0.5f, rgba(0x3D4F9E, 0.7f * a), 1.2f);
            float px = sx + 4;
            for (std::size_t i = 0; i < r.opts.size(); ++i)
            {
                const int hit = int(ws_hits_.size());
                ws_hits_.push_back({px, cy - sh * 0.5f, wd[i], sh, r.id, r.ok[i] ? int(i) : -1});
                const bool chosen = int(i) == r.cur;
                const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit && r.ok[i];
                if (chosen)
                    g.panel(px, cy - sh * 0.5f + 4, wd[i], sh - 8, rgba(0x1D45B8, 0.95f * a), 0.85f, (sh - 8) * 0.5f,
                            row_on ? with_alpha(kIcy, a) : rgba(0x6F8FF0, a), row_on ? 2.2f : 1.2f, row_on ? 6.0f : 0.0f);
                else if (hovered)
                    g.panel(px, cy - sh * 0.5f + 4, wd[i], sh - 8, rgba(0x14307E, 0.9f * a), 1, (sh - 8) * 0.5f,
                            with_alpha(kCyan, a), 1.8f);
                const float ia = r.ok[i] ? 1.0f : 0.4f;
                g.text_mid(Font::SemiBold, size, px + wd[i] * 0.5f, cy, with_alpha(chosen || hovered ? kWhite : kSoft, a * ia),
                           Align::Center, r.opts[i]);
                px += wd[i];
            }
        };
        const float top = kHubTop;
        for (const Item &it : items)
        {
            const float iy = top + it.y - ws_scroll_;
            /* Fade at the column's ends; past them, not at all. */
            /* Past the column's ends: fading out within the margins, gone beyond them. */
            const float over = std::max(top - iy, iy + it.h - kHubBottom);
            const float a = over <= 0 ? 1.0f : std::clamp(1.0f - over / 22.0f, 0.0f, 1.0f);
            if (a < 0.02f)
                continue;
            const HubSection &s = *it.s;
            const bool open = s.id == ws_open_;
            if (it.kind == 0)
            {
                const int focus = kHfHead + s.id;
                const int hit = int(ws_hits_.size());
                if (a > 0.9f)
                    ws_hits_.push_back({x, iy, w, it.h, focus, 0});
                const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit;
                const bool on = (focusing && ws_focus_ == focus) || hovered;
                const float bh = open ? it.h + 4 : it.h;
                Color fill = open ? rgba(0x14307E, 0.85f) : rgba(0x0B1747, 0.62f);
                Color rim = on ? kIcy : open ? rgba(0x6F8FF0, 0.8f) : rgba(0x3D4F9E, s.locked ? 0.45f : 0.75f);
                g.panel(x, iy, w, bh, with_alpha(fill, fill.a * a), 0.85f, 20, with_alpha(rim, rim.a * a), on ? 2.6f : 1.3f,
                        on ? 7.0f : 0.0f, 0.1f);
                const Color ink = with_alpha(s.locked ? with_alpha(kSoft, 0.6f) : on || open ? kWhite : kSoft, a);
                head_icon(s.id, x + 38, iy + it.h * 0.5f, ink);
                const float nw = g.text_mid(Font::Bold, ts(23), x + 72, iy + it.h * 0.5f, ink, Align::Left, s.name);
                const float sr = x + w - 54;
                if (s.locked)
                {
                    const float lx = sr - g.measure(Font::Bold, ts(18), s.summary) - 26;
                    g.panel(lx - 7, iy + it.h * 0.5f - 3, 14, 11, with_alpha(rgba(0xFFC85C), 0.9f * a), 1, 2.5f);
                    g.panel(lx - 4.5f, iy + it.h * 0.5f - 10, 9, 10, rgba(0xFFFFFF, 0.0f), 0, 4.5f,
                            with_alpha(rgba(0xFFC85C), 0.9f * a), 2.0f);
                    g.text_mid(Font::Bold, ts(18), sr, iy + it.h * 0.5f, with_alpha(rgba(0xFFC85C), a), Align::Right, s.summary);
                }
                else
                    g.text_mid(Font::Regular, ts(18), sr, iy + it.h * 0.5f, with_alpha(kLavender, a), Align::Right,
                               fit(g, Font::Regular, ts(18), s.summary, sr - (x + 72 + nw + 30)));
                g.glyph(Glyph::Arrow, x + w - 28, iy + it.h * 0.5f, 13, with_alpha(on ? kWhite : kSoft, a), open ? 0.0f : kPi);
                continue;
            }
            if (it.kind == 2 || it.kind == 3)
            {
                /* The open section's body: one panel from under its head to its last row. */
                if (it.kind == 2)
                {
                    float end = it.y;
                    for (const Item &b : items)
                        if (b.s == it.s && b.kind == 3)
                            end = b.y + b.h;
                    const float by = iy - 2, bh = end - it.y + 2;
                    const float vis_a = a;
                    g.panel(x + 10, by, w - 20, bh, rgba(0x0A1236, 0.55f * vis_a), 1, 18, rgba(0x3D4F9E, 0.5f * vis_a), 1.0f);
                }
                continue;
            }
            const HubRow &r = *it.r;
            const bool row_on = focusing && ws_focus_ == r.id;
            const float cy = iy + it.h * 0.5f;
            const float lx = x + 40, cx0 = x + 250, cw = w - 250 - 34;
            if (row_on)
            {
                g.panel(x + 18, iy + 4, w - 36, it.h - 8, rgba(0x1D45B8, 0.22f * a), 1, 14);
                g.panel(x + 18, iy + 10, 4, it.h - 20, with_alpha(kCyan, a), 1, 2);
            }
            switch (r.kind)
            {
            case kHkSeg:
                g.text_mid(Font::SemiBold, ts(20), lx, cy, with_alpha(row_on ? kWhite : kSoft, a), Align::Left,
                           fit(g, Font::SemiBold, ts(20), r.label, cx0 - lx - 16));
                segs(r, cx0, cy, cw, row_on, a);
                break;
            case kHkSlider:
            {
                g.text_mid(Font::SemiBold, ts(20), lx, cy, with_alpha(row_on ? kWhite : kSoft, a), Align::Left, r.label);
                const float bx = cx0 + 12, bw = cw - 120;
                ws_hits_.push_back({bx - 14, cy - 24, bw + 28, 48, r.id, 1000});
                g.panel(bx, cy - 4, bw, 8, rgba(0x0B1747, 0.9f * a), 1, 4, rgba(0x3D4F9E, a), 1.0f);
                const float fx = bx + bw * r.frac;
                g.panel(bx, cy - 4, std::max(8.0f, fx - bx), 8, with_alpha(row_on ? kCyan : rgba(0x6F8FF0), a), 1, 4);
                g.panel(fx - 13, cy - 13, 26, 26, with_alpha(kWhite, a), 1, 13, with_alpha(row_on ? kIcy : rgba(0x6F8FF0), a),
                        2.0f, row_on ? 6.0f : 0.0f);
                g.text_mid(Font::Bold, ts(21), cx0 + cw, cy, with_alpha(row_on ? kWhite : kSoft, a), Align::Right, r.value);
                break;
            }
            case kHkSwatch:
            {
                g.text_mid(Font::SemiBold, ts(20), lx, cy, with_alpha(row_on ? kWhite : kSoft, a), Align::Left, r.label);
                const float sd = 40, sp = 56;
                for (int i = 0; i < int(r.opts.size()); ++i)
                {
                    const float sx = cx0 + 4 + sd * 0.5f + float(i) * sp;
                    const int hit = int(ws_hits_.size());
                    ws_hits_.push_back({sx - sp * 0.5f, cy - 28, sp, 56, r.id, i});
                    const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit;
                    const bool chosen = i == r.cur, focus = row_on && ws_sub_ == i;
                    if (focus || hovered)
                        g.panel(sx - sd * 0.5f - 7, cy - sd * 0.5f - 7, sd + 14, sd + 14, rgba(0xFFFFFF, 0.0f), 0,
                                sd * 0.5f + 7, with_alpha(focus ? kIcy : kCyan, a), 2.6f);
                    const Color col = rgba(porpoise::move::ball_rgb(i), a);
                    in_true_color([&] {
                        if (chosen)
                            g.blob(sx, cy, sd * 1.9f, sd * 1.9f, with_alpha(col, 0.45f * a));
                        g.panel(sx - sd * 0.5f, cy - sd * 0.5f, sd, sd, col, 1, sd * 0.5f,
                                chosen ? rgba(0xFFFFFF, a) : rgba(0xFFFFFF, 0.25f * a), chosen ? 3.0f : 1.4f);
                    });
                }
                const int shown = row_on ? std::clamp(ws_sub_, 0, int(r.opts.size()) - 1) : r.cur;
                if (shown >= 0)
                    g.text_mid(Font::SemiBold, ts(18), cx0 + cw, cy, with_alpha(kLavender, a), Align::Right,
                               r.opts[std::size_t(shown)]);
                break;
            }
            case kHkButtons:
            {
                float bx = lx;
                for (int i = 0; i < int(r.opts.size()); ++i)
                {
                    const float bw = g.measure(Font::SemiBold, ts(20), r.opts[std::size_t(i)]) + 56, bh = 48;
                    const int hit = int(ws_hits_.size());
                    ws_hits_.push_back({bx, cy - bh * 0.5f, bw, bh, r.id, i});
                    const bool ok = r.ok[std::size_t(i)];
                    const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit;
                    const bool focus = (row_on && ws_sub_ == i) || hovered;
                    const float ba = a * (ok ? 1.0f : 0.45f);
                    const bool primary = i == 0 && (r.id == kHrPtrAct || r.id == kHrSetUp);
                    const Color fill = focus ? rgba(0x1D45B8, 0.95f) : primary ? rgba(0x173A9E, 0.9f) : rgba(0x0B1747, 0.75f);
                    g.panel(bx, cy - bh * 0.5f, bw, bh, with_alpha(fill, fill.a * ba), 0.85f, bh * 0.5f,
                            with_alpha(focus ? kIcy : rgba(0x3D4F9E), ba), focus ? 2.4f : 1.2f, focus ? 6.0f : 0.0f, 0.1f);
                    g.text_mid(Font::SemiBold, ts(20), bx + bw * 0.5f, cy, with_alpha(focus ? kWhite : kSoft, ba),
                               Align::Center, r.opts[std::size_t(i)]);
                    bx += bw + 14;
                }
                break;
            }
            case kHkTiles:
            {
                const float th = 104, gap = 12, tw = (w - 80 - gap * float(r.opts.size() - 1)) / float(r.opts.size());
                for (int i = 0; i < int(r.opts.size()); ++i)
                {
                    const int idx = int(ws_hits_.size());
                    tile(r.id, i, lx + float(i) * (tw + gap), iy + (it.h - th) * 0.5f, tw, th, r.opts[std::size_t(i)], "",
                         r.icons[std::size_t(i)], i == r.cur, true, row_on && i == r.cur, a);
                    (void)idx;
                }
                break;
            }
            default: /* kHkText */
            {
                float ty = iy + 22;
                for (const std::string &l : wrap(g, Font::Regular, ts(19), r.value, w - 100, 4))
                {
                    g.text_mid(Font::Regular, ts(19), lx, ty, with_alpha(kLavender, 0.95f * a), Align::Left, l);
                    ty += 28;
                }
                break;
            }
            }
        }
        /* More above or below: a soft edge says so. */
        if (ws_scroll_ > 8)
            g.glyph(Glyph::Arrow, x + w * 0.5f, kHubTop - 12, 12, with_alpha(kSoft, 0.7f), 0);
        if (ws_scroll_ < std::max(0.0f, total - area) - 8)
            g.glyph(Glyph::Arrow, x + w * 0.5f, kHubBottom + 12, 12, with_alpha(kSoft, 0.7f), kPi);
    }

    /* The pointer: there while it leads, fading as the D-pad takes over. */
    if (ws_ptr_a_ > 0.01f)
    {
        const float a = ws_ptr_a_;
        g.blob(ws_px_, ws_py_, 90, 90, rgba(0xFFC85C, 0.45f * a));
        g.panel(ws_px_ - 20, ws_py_ - 20, 40, 40, rgba(0xFFFFFF, 0.0f), 0, 20, rgba(0xFFC85C, a), 3.5f);
        g.panel(ws_px_ - 4, ws_py_ - 4, 8, 8, rgba(0xFFC85C, a), 1, 4);
    }
    std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::DPad, "Choose"}};
    if (ws_view_ == 1 && ws_focus_ >= kHfHead && ws_focus_ < kHfHead + 100)
        prompts.push_back({Glyph::Cross, ws_open_ == ws_focus_ - kHfHead ? "Close" : "Open"});
    else
        prompts.push_back({Glyph::Cross, "Select"});
    if (ws_launch_)
        prompts.push_back({Glyph::Triangle, "Play Now"});
    prompts.push_back({kKeyL1R1, "Player"});
    prompts.push_back({Glyph::Circle, ws_view_ == 1 ? "Back" : ws_in_game_ ? "Close" : "Done"});
    draw_prompts(prompts, {}, "");
    /* A short note (nothing to calibrate, the lights). */
    if (!ws_note_.empty() && time_ - ws_note_time_ < 3.5)
    {
        const float nw = g.measure(Font::SemiBold, ts(22), ws_note_) + 60;
        g.panel(960 - nw * 0.5f, 910, nw, 52, rgba(0x0F1F63, 0.96f), 1, 26, rgba(0xFFC85C, 0.8f), 1.6f);
        g.text_mid(Font::SemiBold, ts(22), 960, 936, rgba(0xFFD58A), Align::Center, ws_note_);
    }
}


/* ---- 4.5 Beta 4: every player's controller ------------------------------------------------ */

namespace
{
/* Players 2 to 4's settings (Settings::wii_p2_hold ...): the key and the value. */
const char *const kHoldKeys[4] = {nullptr, "wii_p2_hold", "wii_p3_hold", "wii_p4_hold"};
const char *const kAsKeys[4] = {nullptr, "wii_p2_as", "wii_p3_as", "wii_p4_as"};
int player_hold(const Settings &t, int p)
{
    const int v[4] = {0, t.wii_p2_hold, t.wii_p3_hold, t.wii_p4_hold};
    return std::clamp(v[std::clamp(p, 0, 3)], 0, int(HoldCount) - 1);
}
int player_as(const Settings &t, int p) /* 0 the same as player 1's, else the controller + 1 */
{
    const int v[4] = {0, t.wii_p2_as, t.wii_p3_as, t.wii_p4_as};
    return std::clamp(v[std::clamp(p, 0, 3)], 0, int(WiiControllerCount));
}
/* What a DualSense can play as, in tile order: the same as player 1, then each
 * Wii controller (WiiController + 1; not two controllers, which is player 1's). */
constexpr int kAsTiles[6] = {0, WiiRemoteNunchuk + 1, WiiRemote + 1, WiiSideways + 1, WiiClassic + 1, WiiGameCube + 1};
constexpr const char *kHoldNames[4] = {"DualSense", "PlayStation Move", "Move + Navigation", "Move + DualSense"};
constexpr const char *kAsNames[6] = {"Same as Player 1", "Remote + Nunchuk", "Remote", "Remote Sideways",
                                     "Classic Controller", "GameCube Controller"};
int as_tile(int as)
{
    for (int i = 0; i < 6; ++i)
        if (kAsTiles[i] == as)
            return i;
    return 0;
}
} // namespace

void App::hub_player(int to)
{
    to = std::clamp(to, 0, porpoise::pad::kMaxPlayers - 1);
    if (to == ws_player_)
        return;
    ws_player_dir_ = to > ws_player_ ? 1 : -1;
    ws_player_ = to;
    ws_player_at_ = time_;
    ws_prow_ = 0;
    ws_pcol_ = player_hold(ws_settings(), to);
    ws_note_.clear();
    sfx(Sound::MovingTab);
}

/* The four players, top right: each one's light bar color, lit when they're
 * here; a highlight that slides to the one shown. */
void App::draw_hub_switcher(double time)
{
    Gfx &g = *g_;
    constexpr float kW = 168, kH = 50, kGap = 10, kY = 66;
    const float x0 = 1830 - 4 * kW - 3 * kGap;
    const float target = x0 + float(ws_player_) * (kW + kGap);
    const double dt = std::clamp(time - ws_pill_t_, 0.0, 0.1);
    ws_pill_t_ = time;
    ws_pill_x_ = ws_pill_x_ < 0 ? target : smooth(ws_pill_x_, target, dt, 22.0f);
    /* The track, then the highlight under the names. */
    g.panel(x0 - 6, kY - kH * 0.5f - 6, 4 * kW + 3 * kGap + 12, kH + 12, rgba(0x0B1747, 0.55f), 0.8f,
            (kH + 12) * 0.5f, rgba(0x3D4F9E, 0.55f), 1.2f);
    g.panel(ws_pill_x_, kY - kH * 0.5f, kW, kH, rgba(0x1D45B8, 0.95f), 0.8f, kH * 0.5f, kIcy, 2.0f, 6.0f, 0.12f);
    for (int p = 0; p < porpoise::pad::kMaxPlayers; ++p)
    {
        const float x = x0 + float(p) * (kW + kGap);
        const int hit = int(ws_hits_.size());
        ws_hits_.push_back({x, kY - kH * 0.5f, kW, kH, kHfPlayer, p});
        const bool on = p == ws_player_;
        const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit && !on;
        if (hovered)
            g.panel(x, kY - kH * 0.5f, kW, kH, rgba(0x14307E, 0.8f), 0.8f, kH * 0.5f, rgba(0x6F8FF0, 0.8f), 1.4f);
        const bool here = p == 0 || porpoise::pad::playing(p) || porpoise::pad::move_link(p) >= 2;
        const Color col = rgba(player_hex(p));
        const float dx = x + 30, r = 7;
        in_true_color([&] {
            if (here)
            {
                g.blob(dx, kY, r * 5, r * 5, with_alpha(col, 0.45f));
                g.panel(dx - r, kY - r, 2 * r, 2 * r, col, 1, r);
            }
            else
                g.panel(dx - r, kY - r, 2 * r, 2 * r, rgba(0x000000, 0.0f), 1, r, with_alpha(col, 0.85f), 2.0f);
        });
        g.text_mid(Font::SemiBold, ts(20), x + 48, kY, on || hovered ? kWhite : kSoft, Align::Left,
                   trf("Player {n}", {{"n", std::to_string(p + 1)}}));
    }
}

/* Players 2 to 4: what they hold, what their DualSense plays as, and whether
 * it's here, with how to connect it. It slides in from the side it came from. */
void App::draw_hub_player(double time)
{
    Gfx &g = *g_;
    const int p = ws_player_;
    const Settings &t = ws_settings();
    const float e = ease_out(float((time - ws_player_at_) / 0.24));
    const float dx = (1.0f - e) * 48.0f * float(ws_player_dir_), a = 0.25f + 0.75f * e;
    const Color pc = rgba(player_hex(p));
    const std::string pname = trf("Player {n}", {{"n", std::to_string(p + 1)}});
    const bool focusing = ws_ptr_a_ < 0.5f;

    /* Two controllers: this DualSense is player 1's Nunchuk. */
    if (p == 1 && ws_controller_ == WiiTwoControllers)
    {
        const float cx = 960 + dx, top = 300, w = 1100, h = 330;
        g.panel(cx - w * 0.5f, top, w, h, with_alpha(rgba(0x0B1747), 0.72f * a), 0.8f, 30,
                with_alpha(rgba(0x3D4F9E), 0.8f * a), 1.4f, 0, 0.12f);
        ctl_art(0, cx - 70, top + 90, 96, with_alpha(kSoft, a));
        ctl_art(0, cx + 70, top + 90, 96, with_alpha(kSoft, a));
        g.text_mid(Font::Bold, ts(34), cx, top + 190, with_alpha(kWhite, a), Align::Center,
                   tr("Player 2's DualSense is Player 1's Nunchuk"));
        g.text_mid(Font::Regular, ts(22), cx, top + 244, with_alpha(kLavender, a), Align::Center,
                   fit(g, Font::Regular, ts(22),
                       tr("Player 1 plays with two DualSense. Pick another second controller for Player 1 to give "
                          "Player 2 their own."),
                       w - 80));
        draw_prompts({{kKeyL1R1, "Player"}, {Glyph::Circle, ws_in_game_ ? "Close" : "Done"}}, {}, "");
        return;
    }

    const int hold = player_hold(t, p);
    const int as = player_as(t, p);
    const bool move = hold != HoldDualSense;
    const WiiConfig shown = porpoise::pad::for_player(ws_eff_.wii_config(false), p);

    /* A tile: one or two controller pictures and a name; dimmed when it can't be
     * picked now. */
    auto tile = [&](int row, int i, float tx, float top, float tw, float th, const std::string &label,
                    std::initializer_list<int> icons, bool on, bool ok) {
        tx += dx;
        const int hit = int(ws_hits_.size());
        ws_hits_.push_back({tx, top, tw, th, row, ok ? i : -1});
        const bool hovered = ws_ptr_a_ > 0.5f && ws_hover_ == hit && ok;
        const bool focused = focusing && ws_prow_ == (row == kHfPHold ? 0 : 1) && ws_pcol_ == i && ok;
        const float ta = (ok ? 1.0f : 0.38f) * a;
        const Color fill = on ? rgba(0x1D45B8, 0.92f) : hovered ? rgba(0x14307E, 0.9f) : rgba(0x0B1747, 0.70f);
        const Color rim = focused || hovered ? kIcy : on ? rgba(0x6F8FF0) : rgba(0x3D4F9E, 0.8f);
        g.panel(tx, top, tw, th, with_alpha(fill, fill.a * ta), 0.8f, 22, with_alpha(rim, rim.a * ta),
                focused || hovered ? 2.8f : 1.4f, focused ? 8.0f : 0.0f, 0.12f);
        const Color ink = with_alpha(on || hovered || focused ? kWhite : kSoft, ta);
        const float icy = top + th * 0.40f, size = std::min(th * 0.58f, tw * 0.5f);
        const int n = int(icons.size());
        int k = 0;
        for (const int icon : icons)
        {
            const float ix = tx + tw * 0.5f + (n == 2 ? (k == 0 ? -size * 0.42f : size * 0.42f) : 0.0f);
            if (icon == 13)
            {
                /* The Remote held sideways: its picture turned a quarter. */
                const float hs = size * 0.5f;
                const Corner q[4] = {{ix - hs, icy + hs, 1}, {ix - hs, icy - hs, 1}, {ix + hs, icy - hs, 1},
                                     {ix + hs, icy + hs, 1}};
                const float uv[4] = {3.0f / 10.0f, 0.0f, 4.0f / 10.0f, 1.0f};
                if (gl_ctl_)
                    g.quad3d(gl_ctl_, q, size, size, g.toned() ? g.tone(ink) : ink, 0.01f, false, false, uv);
            }
            else if (icon == -2)
            {
                /* "Same as Player 1": player 1's color, numbered. */
                const float r = size * 0.30f;
                const Color c1 = with_alpha(rgba(player_hex(0)), ta);
                in_true_color([&] { g.panel(ix - r, icy - r, 2 * r, 2 * r, with_alpha(c1, 0.22f * ta), 1, r, c1, 2.4f); });
                g.text_mid(Font::Bold, ts(26), ix, icy, ink, Align::Center, "P1");
            }
            else
            {
                ctl_art(icon, ix, icy, size, ink);
                if (icon == 2)
                    draw_move_ball(ix, icy, size, ta);
            }
            ++k;
        }
        g.text_mid(Font::SemiBold, ts(20), tx + tw * 0.5f, top + th * 0.82f, ink, Align::Center,
                   fit(g, Font::SemiBold, ts(20), label, tw - 20));
    };
    auto heading = [&](float y, const std::string &name, const std::string &hint, bool on) {
        const float w = g.text_mid(Font::Bold, ts(22), 90 + dx, y, with_alpha(on ? kWhite : kSoft, a), Align::Left, name);
        g.text_mid(Font::Regular, ts(18), 90 + dx + w + 12, y, with_alpha(kLavender, 0.9f * a), Align::Left, hint);
    };

    /* What they hold. */
    {
        constexpr float top = 180, th = 118, gap = 16;
        const float tw = (1740 - 3 * gap) / 4.0f;
        heading(158, tr("What they hold"), trf("{who}'s controller", {{"who", pname}}), focusing && ws_prow_ == 0);
        const int icons[4][2] = {{0, -1}, {2, -1}, {2, 9}, {2, 0}};
        for (int i = 0; i < 4; ++i)
        {
            const float tx = 90 + float(i) * (tw + gap);
            if (icons[i][1] < 0)
                tile(kHfPHold, i, tx, top, tw, th, tr(kHoldNames[i]), {icons[i][0]}, hold == i, true);
            else
                tile(kHfPHold, i, tx, top, tw, th, tr(kHoldNames[i]), {icons[i][0], icons[i][1]}, hold == i, true);
        }
    }
    /* What it plays as: the DualSense's choice; a Move is always the Remote. */
    {
        constexpr float top = 352, th = 118, gap = 16;
        const float tw = (1740 - 5 * gap) / 6.0f;
        heading(330, tr("Plays as"),
                move ? tr("A Move is the Wii Remote, with the Nunchuk when it has a second controller")
                     : trf("in Wii games ({what} now)", {{"what", tr(wii_controller_name(shown.controller))}}),
                focusing && ws_prow_ == 1);
        const int icon[6] = {-2, 3, 3, 13, 4, 5};
        const int cur = move ? (hold == HoldMove ? 2 : 1) : as_tile(as);
        for (int i = 0; i < 6; ++i)
            tile(kHfPAs, i, 90 + float(i) * (tw + gap), top, tw, th, tr(kAsNames[i]), {icon[i]}, i == cur,
                 !move || i == cur);
    }

    /* Whether it's here, and how to connect it: two cards side by side. */
    {
        constexpr float top = 512, h = 404, gap = 26;
        const float w = (1740 - gap) / 2.0f, lx = 90 + dx, rx = 90 + w + gap + dx;
        const int link = porpoise::pad::move_link(p), nav = porpoise::pad::nav_link(p);
        const bool pad_here = porpoise::pad::connected(p);
        bool ready = hold == HoldDualSense ? pad_here : link >= 2;
        if (hold == HoldMoveNav)
            ready = ready && nav >= 2;
        if (hold == HoldMoveDualSense)
            ready = ready && pad_here;
        for (const float x : {lx, rx})
            g.panel(x, top, w, h, with_alpha(rgba(0x0B1747), 0.62f * a), 0.8f, 30, with_alpha(rgba(0x3D4F9E), 0.8f * a),
                    1.4f, 0, 0.12f);
        /* Left: the player, and each controller's state. */
        {
            const float bx = lx + 84, by = top + 86, r = 44;
            in_true_color([&] {
                g.blob(bx, by, r * 3.2f, r * 3.2f, with_alpha(pc, (ready ? 0.42f : 0.18f) * a));
                g.panel(bx - r, by - r, 2 * r, 2 * r, with_alpha(pc, (ready ? 0.95f : 0.30f) * a), 1, r,
                        with_alpha(pc, a), 2.5f);
            });
            g.text_mid(Font::Bold, ts(28), bx, by, with_alpha(kWhite, a), Align::Center, "P" + std::to_string(p + 1));
            g.text_mid(Font::Bold, ts(38), bx + 76, by - 18, with_alpha(kWhite, a), Align::Left, pname);
            g.text_mid(Font::SemiBold, ts(22), bx + 76, by + 24, with_alpha(ready ? rgba(0x8FF0BD) : rgba(0xFFC85C), a),
                       Align::Left, ready ? tr("Ready to play") : tr("Waiting for their controller"));
            struct Line
            {
                int icon;
                std::string name, state;
                bool ok;
            };
            std::vector<Line> lines;
            if (hold == HoldDualSense || hold == HoldMoveDualSense)
                lines.push_back({0, tr("DualSense"), pad_here ? tr("On") : tr("Not signed in"), pad_here});
            if (move)
                lines.push_back({2, tr("PlayStation Move"),
                                 link >= 2 ? tr("Connected through Move Management") : tr("Waiting for Move Management"),
                                 link >= 2});
            if (hold == HoldMoveNav)
                lines.push_back({9, tr("Navigation"), nav >= 2 ? tr("Connected") : tr("Waiting for Move Management"), nav >= 2});
            float ly = top + 192;
            for (const Line &l : lines)
            {
                g.panel(lx + 32, ly - 34, w - 64, 68, with_alpha(rgba(0x14307E), 0.35f * a), 0.8f, 18);
                ctl_art(l.icon, lx + 78, ly, 52, with_alpha(kSoft, a));
                if (l.icon == 2)
                    draw_move_ball(lx + 78, ly, 52, a);
                g.text_mid(Font::SemiBold, ts(22), lx + 120, ly, with_alpha(kWhite, a), Align::Left, l.name);
                const Color sc = l.ok ? rgba(0x8FF0BD) : rgba(0xFFC85C);
                in_true_color([&] { g.panel(lx + w - 64 - 8, ly - 8, 16, 16, with_alpha(sc, a), 1, 8); });
                g.text_mid(Font::Regular, ts(20), lx + w - 64 - 22, ly, with_alpha(kLavender, a), Align::Right,
                           fit(g, Font::Regular, ts(20), l.state, w - 420));
                ly += 82;
            }
            g.text_mid(Font::Regular, ts(18), lx + 40, top + h - 34, with_alpha(kLavender, 0.85f * a), Align::Left,
                       fit(g, Font::Regular, ts(18),
                           move ? tr("Pointer speed, steadiness and swing strength follow Player 1's.")
                                : tr("Pointer and motion settings follow Player 1's."),
                           w - 80));
        }
        /* Right: how to connect it. */
        {
            g.text_mid(Font::Bold, ts(28), rx + 40, top + 56, with_alpha(kWhite, a), Align::Left, tr("How to connect"));
            std::vector<std::string> steps;
            const std::string color = tr(kLightValues[std::size_t(std::clamp(
                p == 1 ? settings_->light_2 : p == 2 ? settings_->light_3 : settings_->light_4, 0,
                int(kLightValues.size()) - 1))]);
            if (hold == HoldDualSense)
            {
                steps.push_back(tr("Sign in another user on the PS5, or add a guest."));
                steps.push_back(trf("Turn their DualSense on. It joins as {who}. Light bar: {color}.",
                                    {{"who", pname}, {"color", color}}));
                steps.push_back(tr("It plays alongside Player 1 in every Wii game."));
            }
            else
            {
                steps.push_back(tr("Start Move Management by REXCRAFT88 with your payload loader."));
                steps.push_back(trf("Open its page on your phone (your PS5's address, port 8095) and give the Move to {who}.",
                                    {{"who", pname}}));
                if (hold == HoldMoveNav)
                    steps.push_back(trf("Give the Navigation controller to {who} too.", {{"who", pname}}));
                else if (hold == HoldMoveDualSense)
                    steps.push_back(trf("Sign in {who}'s user on the PS5. Their DualSense is the Nunchuk.", {{"who", pname}}));
                else
                    steps.push_back(tr("Hold Square a moment to center the pointer."));
            }
            float sy = top + 120;
            for (std::size_t i = 0; i < steps.size(); ++i)
            {
                const float r = 19;
                in_true_color([&] { g.panel(rx + 40, sy - r, 2 * r, 2 * r, with_alpha(pc, 0.22f * a), 1, r, with_alpha(pc, a), 2.0f); });
                g.text_mid(Font::Bold, ts(20), rx + 40 + r, sy, with_alpha(kWhite, a), Align::Center, std::to_string(i + 1));
                const std::vector<std::string> lines = wrap(g, Font::Regular, ts(21), steps[i], w - 150);
                float ty = sy;
                for (const std::string &l : lines)
                {
                    g.text_mid(Font::Regular, ts(21), rx + 104, ty, with_alpha(kSoft, a), Align::Left, l);
                    ty += 32;
                }
                sy = std::max(sy + 72, ty + 30);
            }
            if (move)
                g.text_mid(Font::Regular, ts(18), rx + 40, top + h - 34, with_alpha(kLavender, 0.85f * a), Align::Left,
                           fit(g, Font::Regular, ts(18), "github.com/REXCRAFT88/Move-Management-PS5", w - 80));
        }
    }
    if (ws_ptr_a_ > 0.01f)
    {
        /* The pointer, as on player 1's page. */
        const float pa = ws_ptr_a_;
        g.blob(ws_px_, ws_py_, 90, 90, rgba(0xFFC85C, 0.45f * pa));
        g.panel(ws_px_ - 20, ws_py_ - 20, 40, 40, rgba(0xFFFFFF, 0.0f), 0, 20, rgba(0xFFC85C, pa), 3.5f);
        g.panel(ws_px_ - 4, ws_py_ - 4, 8, 8, rgba(0xFFC85C, pa), 1, 4);
    }
    std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::DPad, "Choose"}, {Glyph::Cross, "Select"}};
    if (ws_launch_)
        prompts.push_back({Glyph::Triangle, "Play Now"});
    prompts.push_back({kKeyL1R1, "Player"});
    prompts.push_back({Glyph::Circle, ws_in_game_ ? "Close" : "Done"});
    draw_prompts(prompts, {}, "");
    if (!ws_note_.empty() && time_ - ws_note_time_ < 3.5)
    {
        const float nw = g.measure(Font::SemiBold, ts(22), ws_note_) + 60;
        g.panel(960 - nw * 0.5f, 910, nw, 52, rgba(0x0F1F63, 0.96f), 1, 26, rgba(0xFFC85C, 0.8f), 1.6f);
        g.text_mid(Font::SemiBold, ts(22), 960, 936, rgba(0xFFD58A), Align::Center, ws_note_);
    }
}

App::Action App::update_hub_player(bool up, bool down, bool left, bool right, bool cross, const WsHit *hover)
{
    const int p = ws_player_;
    if (p == 1 && ws_controller_ == WiiTwoControllers)
        return Action::None; /* (player 1's Nunchuk: nothing to choose here) */
    const Settings &t = ws_settings();
    const int hold = player_hold(t, p);
    const bool move = hold != HoldDualSense;
    auto keep = [&](const char *key, int value) {
        ws_store(key, value, true); /* (players 2 to 4: every game's) */
        wii_setup_apply_pad();
        sfx(Sound::MovingTab);
    };
    auto choose = [&](int row, int i) {
        if (row == 0)
        {
            if (i != hold)
                keep(kHoldKeys[p], i);
        }
        else if (!move)
        {
            const int v = kAsTiles[std::clamp(i, 0, 5)];
            if (v != player_as(t, p))
                keep(kAsKeys[p], v);
        }
    };
    const int rows = move ? 1 : 2; /* (a Move's "plays as" is fixed: nothing to move to there) */
    if (ws_pointing_ && hover && (hover->row == kHfPHold || (hover->row == kHfPAs && !move)) && hover->choice >= 0)
    {
        ws_prow_ = hover->row == kHfPHold ? 0 : 1;
        ws_pcol_ = hover->choice;
    }
    if (ws_prow_ >= rows)
    {
        /* (they just took a Move: the row they were on is fixed now) */
        ws_prow_ = 0;
        ws_pcol_ = hold;
    }
    if (up && ws_prow_ > 0)
    {
        ws_prow_ = 0;
        ws_pcol_ = hold;
        sfx(Sound::MenuScroll);
    }
    else if (down && ws_prow_ + 1 < rows)
    {
        ws_prow_ = 1;
        ws_pcol_ = as_tile(player_as(t, p));
        sfx(Sound::MenuScroll);
    }
    const int dir = left ? -1 : right ? 1 : 0;
    if (dir && !(up || down))
    {
        const int count = ws_prow_ == 0 ? 4 : 6;
        const int to = std::clamp(ws_pcol_ + dir, 0, count - 1);
        if (to != ws_pcol_)
        {
            ws_pcol_ = to;
            choose(ws_prow_, to); /* (as player 1's cards: moving picks) */
        }
    }
    if (cross)
    {
        if (ws_pointing_)
        {
            if (hover && (hover->row == kHfPHold || hover->row == kHfPAs) && hover->choice >= 0)
                choose(hover->row == kHfPHold ? 0 : 1, hover->choice);
            else if (hover && hover->row == kHfPAs)
            {
                ws_note_ = tr("A Move plays as the Wii Remote. Pick DualSense to choose another Wii controller.");
                ws_note_time_ = time_;
            }
        }
        else
            choose(ws_prow_, ws_pcol_);
    }
    return Action::None;
}

} // namespace porpoise::ui

/* ---- Customize buttons: a Wii controller's ------------------------------------------------ */

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* The four Wii controllers the player can set buttons for, in tab order after
 * GameCube (Settings::wii_buttons, porpoise::pad::wii_button_set). */
constexpr int kWiiSetController[4] = {WiiRemoteNunchuk, WiiRemote, WiiSideways, WiiClassic};
constexpr const char *kMapKindNames[6] = {"GameCube", "Remote + Nunchuk", "Remote", "Remote Sideways",
                                          "Classic Controller", "Game Boy Advance"};
constexpr int kMapKinds = 6;
constexpr int kMapKindGba = 5; /* the GBA on a GameCube port (Settings::gba_*) */
/* The tabs' short names, across the top of the list. */
constexpr const char *kMapKindTabs[6] = {"GameCube", "Remote + Nunchuk", "Remote", "Sideways", "Classic", "GBA"};

/* Porpoise's own layout for a set, held face up: the rows of the list. */
WiiLayout base_layout(int set)
{
    WiiConfig config;
    config.active = true;
    config.controller = kWiiSetController[set];
    config.menu = true; /* Porpoise's buttons, not the player's */
    return wii_layout(config, false, PoseFlat);
}
} // namespace

bool App::switch_map_kind()
{
    if (!pressed(BtnL1 | BtnR1))
        return false;
    map_kind_ = (map_kind_ + (pressed(BtnL1) ? kMapKinds - 1 : 1)) % kMapKinds;
    map_row_ = map_kind_ > 0 ? 0 : kSelectRow;
    map_capture_ = false;
    map_note_.clear();
    sfx(Sound::MovingTab);
    return true;
}

/* The controllers, as tabs across the top of the list panel (L1 / R1). */
void App::draw_map_tabs()
{
    Gfx &g = *g_;
    const float px = 950, pw = 880, cy = 136 + 34;
    float size = ts(22), total = 0;
    float widths[kMapKinds];
    for (int pass = 0; pass < 2; ++pass)
    {
        total = 0;
        for (int i = 0; i < kMapKinds; ++i)
        {
            widths[i] = g.measure(Font::SemiBold, size, tr(kMapKindTabs[i])) + 30;
            total += widths[i];
        }
        if (total <= pw - 40)
            break;
        size *= (pw - 40) / total; /* a long language: smaller */
    }
    float x = px + (pw - total) * 0.5f;
    g.panel(x - 4, cy - 22, total + 8, 44, rgba(0x07102E, 0.55f), 0.8f, 22, rgba(0x3D4F9E, 0.8f), 1.2f);
    for (int i = 0; i < kMapKinds; ++i)
    {
        const bool on = i == map_kind_;
        if (on)
            g.panel(x, cy - 18, widths[i], 36, rgba(0x1D45B8, 0.95f), 0.8f, 18, kIcy, 1.8f, 6, 0.15f);
        g.text_mid(Font::SemiBold, size, x + widths[i] * 0.5f, cy, on ? kWhite : kSoft, Align::Center,
                   tr(kMapKindTabs[i]));
        x += widths[i];
    }
}

App::Action App::update_wii_mapping(bool up, bool down, bool left, bool right)
{
    /* Row 0 picks the buttons (Porpoise's, Mine 1, Mine 2); then each
     * binding; then Start Over. Porpoise's own are shown but never change. */
    const int set = map_kind_ - 1;
    const WiiLayout base = base_layout(set);
    const int rows = base.count + 2;
    const bool global = map_target_ == settings_;
    const Action changed = global ? Action::SettingsChanged : Action::None;
    auto (*tables)[CtlCount] = settings_->wii_slot(map_wii_slot_);
    int *perm = tables ? tables[set] : nullptr;
    int bind = map_row_ - 1; /* the binding on this row, if it is one */
    if (map_capture_)
    {
        const std::uint32_t mask = 0xFFFFu;
        if (time_ - map_capture_start_ > kCaptureSeconds)
        {
            map_capture_ = false;
            map_note_ = tr("Nothing pressed; the button is unchanged.");
            map_note_time_ = time_;
            return Action::None;
        }
        if (!map_armed_)
        {
            map_armed_ = (raw_held_ & mask) == 0;
            if (map_armed_)
                map_capture_start_ = time_;
            return Action::None;
        }
        const std::uint32_t fresh = (raw_held_ & ~raw_prev_) & mask;
        if (fresh && perm && bind >= 0 && bind < base.count)
            for (int c = 0; c < CtlCount; ++c)
                if (fresh & control_bit(c))
                {
                    /* The binding moves to the pressed control; whatever was
                     * there takes its old place. */
                    const int from = base.binds[bind].control;
                    int other = 0;
                    for (int e = 0; e < CtlCount; ++e)
                        if (perm[e] == c)
                            other = e;
                    std::swap(perm[from], perm[other]);
                    use_wii_slot(map_wii_slot_);
                    map_note_ = trf("{button} is now on {control}.",
                                    {{"button", tr(wii_input_name(base.binds[bind].input))},
                                     {"control", control_name(c)}});
                    map_note_time_ = time_;
                    map_capture_ = false;
                    sfx(Sound::LaunchGame);
                    return changed;
                }
        return Action::None;
    }
    if (switch_map_kind())
        return Action::None;
    if (up && map_row_ > 0)
    {
        --map_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && map_row_ + 1 < rows)
    {
        ++map_row_;
        sfx(Sound::MenuScroll);
    }
    if (map_row_ == 0 && (left || right))
    {
        map_wii_slot_ = (map_wii_slot_ + (left ? Settings::kWiiButtonSlots - 1 : 1)) % Settings::kWiiButtonSlots;
        sfx(Sound::MovingTab);
        tables = settings_->wii_slot(map_wii_slot_);
        perm = tables ? tables[set] : nullptr;
    }
    bind = map_row_ - 1;
    if (pressed(BtnCircle))
    {
        map_kind_ = 0;
        close_mapping();
        return Action::None;
    }
    if (pressed(BtnCross))
    {
        if (map_row_ == 0)
        {
            use_wii_slot(map_wii_slot_);
            sfx(Sound::LaunchGame);
            return changed;
        }
        if (!perm)
        {
            map_note_ = tr("Porpoise's buttons stay as they are: pick Mine 1 or Mine 2 to make your own.");
            map_note_time_ = time_;
            sfx(Sound::DetailsFlip);
            return Action::None;
        }
        if (bind < base.count)
        {
            map_capture_ = true;
            map_armed_ = false;
            map_capture_start_ = time_;
            sfx(Sound::DetailsFlip);
            return Action::None;
        }
        for (int c = 0; c < CtlCount; ++c)
            perm[c] = c;
        use_wii_slot(map_wii_slot_);
        map_note_ = trf("{controller} is back to Porpoise's layout.", {{"controller", tr(kMapKindNames[map_kind_])}});
        map_note_time_ = time_;
        sfx(Sound::LaunchGame);
        return changed;
    }
    return Action::None;
}

/* The Wii buttons in a slot go to whatever opened the screen (every game, or
 * one), and are saved. */
void App::use_wii_slot(int slot)
{
    /* In a game played with GameCube buttons, the Wii buttons are for every
     * Wii game: this game would never use them. */
    Settings *const target = map_target_;
    const bool in_game = map_in_game_;
    const bool for_all = in_game && !porpoise::pad::wii().active;
    if (for_all)
    {
        map_target_ = settings_;
        map_in_game_ = false;
    }
    const bool changed = map_target_->wii_buttons_use != slot;
    map_target_->wii_buttons_use = slot;
    save_mapping(changed, "wii_buttons_use");
    const std::string name = tr(slot == 0 ? "Porpoise's Set" : slot == 1 ? "Mine 1" : "Mine 2");
    map_note_ = map_game_ && map_target_ != settings_ ? trf("Wii buttons: {name}, for this game.", {{"name", name}})
                                                      : trf("Wii buttons: {name}.", {{"name", name}});
    map_note_time_ = time_;
    map_target_ = target;
    map_in_game_ = in_game;
    if (for_all && target)
    {
        /* The game's copies show it too (In Use), with nothing waiting. */
        target->wii_buttons_use = slot;
        menu_draft_.wii_buttons_use = menu_base_.wii_buttons_use = slot;
    }
}

void App::draw_wii_mapping(double time)
{
    Gfx &g = *g_;
    const int set = map_kind_ - 1;
    const WiiLayout base = base_layout(set);
    static const int kSame[CtlCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    auto (*tables)[CtlCount] = settings_->wii_slot(map_wii_slot_);
    const int *perm = tables ? tables[set] : kSame;
    WiiLayout mine = base;
    for (int i = 0; i < mine.count; ++i)
        mine.binds[i].control = perm[mine.binds[i].control];
    if (map_in_game_)
        g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.78f), 1, 0);

    /* Left: the DualSense as this Wii controller, with the player's buttons. */
    const float lx = 90, ly = 136, lw = 830, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0A1236, 0.55f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    g.text_mid(Font::Bold, ts(46), lx + 46, ly + 64, kWhite, Align::Left, tr(kMapKindNames[map_kind_]));
    g.text_mid(Font::Regular, ts(26), lx + 46, ly + 112, kLavender, Align::Left,
               fit(g, Font::Regular, ts(26), tr("Every Wii game played with this controller"), lw - 92));
    draw_wii_pad(mine, PoseFlat, lx + 35, ly + 170, 760, 1.0f);
    (void)time;

    /* Right: each Wii input and the DualSense control it's on. */
    const float px = 950, py = 136, pw = 880, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const int rows = base.count + 1;
    const float row_x = px + 22, row_w = pw - 44;
    {
        /* Which buttons: Porpoise's, Mine 1, Mine 2 (and which are in use). */
        const bool on = map_row_ == 0;
        const float sy = py + 66, sh = 58;
        if (on)
            g.panel(row_x, sy, row_w, sh, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        else
            g.panel(row_x, sy, row_w, sh, rgba(0x07102E, 0.45f), 0.8f, kR, rgba(0x3D4F9E, 0.8f), 1.2f);
        const int use = map_target_ ? map_target_->wii_buttons_use : 1;
        const std::string name = tr(map_wii_slot_ == 0 ? "Porpoise's Set" : map_wii_slot_ == 1 ? "Mine 1" : "Mine 2");
        g.text_mid(Font::SemiBold, ts(26), row_x + 30, sy + sh * 0.5f, on ? kWhite : kSoft, Align::Left,
                   fit(g, Font::SemiBold, ts(26), tr("Buttons"), row_w * 0.3f));
        const float vx = row_x + row_w * 0.62f;
        g.glyph(Glyph::Arrow, vx - 150, sy + sh * 0.5f, 14, on ? kWhite : kSoft, -kPi * 0.5f);
        g.glyph(Glyph::Arrow, vx + 150, sy + sh * 0.5f, 14, on ? kWhite : kSoft, kPi * 0.5f);
        g.text_mid(Font::Bold, ts(28), vx, sy + sh * 0.5f, kWhite, Align::Center,
                   fit(g, Font::Bold, ts(28), name, 270));
        const bool in_use = use == map_wii_slot_;
        std::string tag = map_wii_slot_ == 0 ? tr("Locked") : "";
        if (in_use)
            tag = tag.empty() ? tr("In Use") : tr("In Use") + " \xE2\x80\xA2 " + tag;
        g.text_mid(Font::SemiBold, ts(21), row_x + row_w - 26, sy + sh * 0.5f, in_use ? rgba(0x7CF0A6) : kLavender,
                   Align::Right, fit(g, Font::SemiBold, ts(21), tag, row_w * 0.22f));
    }
    const float row_h = std::min(44.0f, (ph - 200) / float(rows));
    float y = py + 136;
    for (int i = 0; i < rows; ++i)
    {
        const bool on = i + 1 == map_row_;
        const float cy = y + row_h * 0.5f;
        if (i == base.count)
        {
            g.panel(row_x + 20, y + 4, row_w - 40, 1.5f, rgba(0x3D4F9E, 0.8f), 1, 0);
            y += 12;
        }
        const float ry = i == base.count ? y : y;
        const float rcy = i == base.count ? ry + row_h * 0.5f : cy;
        if (on)
            g.panel(row_x, ry + 2, row_w, row_h - 4, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float right = row_x + row_w - 22;
        if (i < base.count)
        {
            const int input = base.binds[i].input;
            const float h = 28, chx = row_x + 44;
            const std::string chip = wii_input_chip(input);
            const float w = chip.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.5f, chip) + h * 0.7f);
            g.panel(chx - w * 0.5f, rcy - h * 0.5f, w, h, rgba(wii_input_colour(input)), 0.8f, h * 0.5f,
                    rgba(0xFFFFFF, 0.6f), 1.2f);
            if (chip.empty())
                g.glyph(Glyph::Arrow, chx, rcy, h * 0.5f, rgba(0x0A1236), wii_input_arrow(input));
            else
                g.text_mid(Font::Bold, h * 0.5f, chx, rcy, rgba(0x0A1236), Align::Center, chip);
            g.text_mid(Font::SemiBold, ts(25), row_x + 96, rcy, on ? kWhite : kSoft, Align::Left,
                       tr(wii_input_name(input)));
            if (on && map_capture_)
                g.text_mid(Font::Bold, ts(24), right, rcy, kCyan, Align::Right, tr("Press a button\xE2\x80\xA6"));
            else
                draw_control(perm[base.binds[i].control], right, rcy, on, std::min(50.0f, row_h + 6));
        }
        else
            g.text_mid(Font::SemiBold, ts(26), row_x + 34, rcy, on ? kWhite : kSoft, Align::Left,
                       tr("Start Over from Porpoise's Layout"));
        y += row_h;
    }
    const double since = time_ - map_note_time_;
    if (!map_note_.empty() && since < 4.0)
    {
        const float a = since < 3.0 ? 1.0f : float(4.0 - since);
        g.text_mid(Font::SemiBold, ts(24), px + pw * 0.5f, py + ph - 32, with_alpha(kCyan, a), Align::Center,
                   fit(g, Font::SemiBold, ts(24), map_note_, pw - 60));
    }
    draw_map_tabs();
    const int bind = map_row_ - 1;
    if (map_capture_ && bind >= 0 && bind < base.count)
    {
        const int left = int(std::ceil(kCaptureSeconds - (time_ - map_capture_start_)));
        draw_prompts({}, {}, trf("Press the DualSense button for {button} ({n})",
                                 {{"button", tr(wii_input_name(base.binds[bind].input))},
                                  {"n", std::to_string(std::max(1, left))}}));
    }
    else if (map_row_ > 0 && map_wii_slot_ == 0)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Circle, "Back"}}, {{kKeyL1R1, "Controller"}}, "");
    else
        draw_prompts({{Glyph::DPad, "Browse"},
                      {Glyph::Cross, map_row_ == 0 ? "Use" : bind < base.count ? "Change" : "Start Over"},
                      {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
}

/* ---- Customize buttons: the GBA's ----------------------------------------------------------- */

namespace
{
/* Which buttons (Porpoise's, Mine 1, Mine 2), the GBA button, each GBA input, then the stick, the
 * show mode, Start Over. */
constexpr int kGbaRowPlayer = 0; /* whose: player 1..4, each with a whole layout of their own */
constexpr int kGbaRowSlot = 1;
constexpr int kGbaRowModifier = 2;
constexpr int kGbaRowFirstInput = 3;
constexpr int kGbaRowStick = kGbaRowFirstInput + GbaCount;
constexpr int kGbaRowAlways = kGbaRowStick + 1; /* always the GBA: no GBA button to hold */
constexpr int kGbaRowShowMode = kGbaRowAlways + 1;
constexpr int kGbaRowStartOver = kGbaRowShowMode + 1;
constexpr int kGbaRows = kGbaRowStartOver + 1;

const char *gba_slot_name(int slot)
{
    return slot == 0 ? "Porpoise's Set" : slot == 1 ? "Mine 1" : "Mine 2";
}

/* A slot's buttons in a player's layout (1, 2), null for Porpoise's own (0). */
int *gba_slot_of(const Settings::GbaLayoutRef &l, int slot)
{
    return slot == 1 ? l.buttons : slot == 2 ? l.buttons2 : nullptr;
}

/* A slot's buttons as a config (Porpoise's own for slot 0), with the player's GBA button. */
GbaConfig gba_slot_config(const Settings::GbaLayoutRef &l, int slot)
{
    GbaConfig c = porpoise::pad::gba_defaults();
    c.modifier = std::clamp(*l.modifier, 0, CtlCount - 1);
    c.stick_dpad = *l.stick_dpad;
    if (const int *b = gba_slot_of(l, slot))
        for (int i = 0; i < GbaCount; ++i)
            c.control[i] = static_cast<std::int8_t>(std::clamp(b[i], -1, CtlCount - 1));
    return c;
}

/* The GBA inputs in the GBA tab's order (saved by GbaInput, so the order is only how they're
 * shown): the GBA's own buttons, the screen's, the sound's, then Reset and Disconnect together and
 * the quick states. */
int gba_row_input(int row)
{
    static const int kOrder[GbaCount] = {GbaA,           GbaB,          GbaSelect,       GbaStart,         GbaRight,
                                         GbaLeft,        GbaUp,         GbaDown,         GbaR,             GbaL,
                                         GbaShowScreen,  GbaMoveScreen, GbaAdjustOpacity, GbaVolume,       GbaMute,
                                         GbaReset,       GbaDisconnect, GbaQuickSave,    GbaQuickLoad};
    static_assert(GbaCount == 19, "every GbaInput has a place in the GBA tab");
    return row >= 0 && row < GbaCount ? kOrder[row] : 0;
}

const char *gba_input_name(int input)
{
    static const char *const kNames[GbaCount] = {"A",          "B",         "Select",   "Start",
                                                 "D-Pad Right", "D-Pad Left", "D-Pad Up", "D-Pad Down",
                                                 "R",          "L",         "Show GBA Screen", "Move GBA Screen",
                                                 "Adjust GBA Opacity", "Reset GBA",       "Mute GBA",
                                                 "GBA Volume",         "Disconnect GBA",  "Quick Save GBA State",
                                                 "Quick Load GBA State"};
    return input >= 0 && input < GbaCount ? kNames[input] : "";
}

/* A GBA input's chip: its letter or word, an arrow for the D-pad. */
const char *gba_input_chip(int input)
{
    static const char *const kChips[GbaCount] = {"A",   "B", "SEL", "START", "", "",     "",     "",
                                                 "R",   "L", "TV",  "MOVE",  "%", "RESET", "MUTE", "VOL", "LINK",
                                                 "QS",  "QL"};
    return input >= 0 && input < GbaCount ? kChips[input] : "";
}

float gba_arrow(int input)
{
    switch (input)
    {
    case GbaDown: return kPi;
    case GbaLeft: return -kPi * 0.5f;
    case GbaRight: return kPi * 0.5f;
    default: return 0;
    }
}

std::uint32_t gba_input_colour(int input)
{
    if (input == GbaA || input == GbaB)
        return 0xB9A6FF; /* the GBA's lilac face buttons */
    if (input == GbaShowScreen || input == GbaMoveScreen || input == GbaAdjustOpacity)
        return 0x7CF0A6;
    if (input == GbaReset || input == GbaDisconnect)
        return 0xFF8A8A;
    if (input == GbaMute || input == GbaVolume)
        return 0xFFC85C;
    if (input == GbaQuickSave || input == GbaQuickLoad)
        return 0x8BD9FF;
    if (input >= GbaRight && input <= GbaDown)
        return 0x9AA8C7;
    return 0xDCE3F5;
}
} // namespace

/* One chip on the picture: a GBA input, or (input < 0) the GBA button itself. */
void App::gba_chip(int input, float cx, float cy, float ch, float alpha)
{
    Gfx &g = *g_;
    const std::string label = input < 0 ? std::string("GBA") : std::string(gba_input_chip(input));
    const float cw = label.size() <= 1 ? ch : g.measure(Font::Bold, ch * 0.46f, label) + ch * 0.8f;
    const std::uint32_t colour = input < 0 ? 0xFFC85C : gba_input_colour(input);
    g.panel(cx - cw * 0.5f, cy - ch * 0.5f, cw, ch, with_alpha(rgba(colour), 0.97f * alpha), 0.8f, ch * 0.5f,
            with_alpha(kWhite, 0.85f * alpha), 1.6f, 6, 0.35f);
    const Color ink = with_alpha(rgba(0x0A1236), alpha);
    if (label.empty())
        g.glyph(Glyph::Arrow, cx, cy, ch * 0.5f, ink, gba_arrow(input));
    else
        g.text_mid(Font::Bold, label.size() == 1 ? ch * 0.62f : ch * 0.46f, cx, cy, ink, Align::Center, label);
}

/* The DualSense with the GBA's buttons on it, and the GBA button (held for all of them). */
void App::draw_gba_pad(const GbaConfig &gba, float bx, float by, float art_h, float alpha)
{
    Gfx &g = *g_;
    const float k = art_h / kArtW;
    const float bw = kArtW * k, bh = kArtH * k;
    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    if (pad_art_)
        g.image(pad_art_, bx, by, bw, bh, g.tone(with_alpha(rgba(0xDDEBFF), alpha)));
    const float chip_h = std::clamp(52 * k / 0.48f, 24.0f, 44.0f);
    auto shoulder = [](int c, float &u, float &v) {
        switch (c)
        {
        case CtlL2: u = 300; v = 18; return true;
        case CtlL1: u = 110; v = 128; return true;
        case CtlR2: u = 1100; v = 18; return true;
        case CtlR1: u = 1290; v = 128; return true;
        default: return false;
        }
    };
    auto put = [&](int control, int input) {
        if (control < 0 || control >= CtlCount)
            return;
        float u, v;
        if (shoulder(control, u, v))
        {
            const float sx = bx + kSpots[control].x * k, sy = by + (kSpots[control].y - 14) * k;
            const float tx = bx + u * k, ty = by + v * k;
            const float dx = tx - sx, dy = ty - sy, len = std::sqrt(dx * dx + dy * dy);
            if (len > 1)
            {
                const float nx = -dy / len * 1.3f, ny = dx / len * 1.3f;
                const Corner c[4] = {{sx + nx, sy + ny, 1}, {tx + nx, ty + ny, 1}, {tx - nx, ty - ny, 1},
                                     {sx - nx, sy - ny, 1}};
                g.quad3d(nullptr, c, len, 2.6f, with_alpha(rgba(0x8BD9FF), 0.85f * alpha), 0, false, false);
            }
            g.blob(sx, sy, 18, 18, with_alpha(rgba(0x8BD9FF), 0.9f * alpha));
            gba_chip(input, tx, ty, chip_h, alpha);
            return;
        }
        gba_chip(input, bx + kSpots[control].x * k, by + kSpots[control].y * k, chip_h, alpha);
    };
    for (int i = 0; i < GbaCount; ++i)
        if (gba.control[i] != gba.modifier)
            put(gba.control[i], i);
    put(gba.modifier, -1);
    if (gba.stick_dpad)
    {
        /* The left stick: a word just below it. */
        const float cx = bx + 525 * k, cy = by + 468 * k + chip_h * 1.2f;
        const std::string t = tr("GBA D-Pad");
        const float tw = g.measure(Font::SemiBold, ts(20), t) + 24;
        g.panel(cx - tw * 0.5f, cy - 16, tw, 32, with_alpha(rgba(0x13308A), 0.94f * alpha), 0.8f, 16,
                with_alpha(rgba(0x8BD9FF), 0.9f * alpha), 1.4f);
        g.text_mid(Font::SemiBold, ts(20), cx, cy, with_alpha(kWhite, alpha), Align::Center, t);
    }
}

App::Action App::update_gba_mapping(bool up, bool down, bool left, bool right)
{
    /* What opened the screen: every game (Settings) or one game (its own settings). */
    Settings &s = *map_target_;
    const Settings::GbaLayoutRef L = s.gba_layout(map_gba_player_); /* the player being edited's */
    const Action changed = map_target_ == settings_ ? Action::SettingsChanged : Action::None;
    auto saved = [&](const std::string &note) {
        map_note_ = note;
        map_note_time_ = time_;
        if (map_gba_player_ == 0)
            for (const char *key : {"gba_buttons", "gba_buttons2", "gba_buttons_use", "gba_modifier",
                                    "gba_stick_dpad", "gba_always", "gba_show_mode"})
                save_mapping(true, key);
        else
        {
            const std::string p = "gba_p" + std::to_string(map_gba_player_ + 1) + "_";
            for (const char *what : {"buttons", "buttons2", "use", "modifier", "stick", "always"})
                save_mapping(true, (p + what).c_str());
            save_mapping(true, "gba_show_mode");
        }
        return changed;
    };
    if (map_capture_)
    {
        if (time_ - map_capture_start_ > kCaptureSeconds)
        {
            map_capture_ = false;
            map_note_ = tr("Nothing pressed; the button is unchanged.");
            map_note_time_ = time_;
            return Action::None;
        }
        if (!map_armed_)
        {
            map_armed_ = (raw_held_ & 0xFFFFu) == 0;
            if (map_armed_)
                map_capture_start_ = time_;
            return Action::None;
        }
        const std::uint32_t fresh = (raw_held_ & ~raw_prev_) & 0xFFFFu;
        for (int c = 0; c < CtlCount && fresh; ++c)
            if (fresh & control_bit(c))
            {
                map_capture_ = false;
                sfx(Sound::LaunchGame);
                if (map_row_ == kGbaRowModifier)
                {
                    /* A GBA button that was on it takes the GBA button's old control. */
                    for (int *set : {L.buttons, L.buttons2})
                        for (int i = 0; i < GbaCount; ++i)
                            if (set[i] == c)
                                set[i] = *L.modifier;
                    *L.modifier = c;
                    return saved(trf("The GBA button is now {control}.", {{"control", control_name(c)}}));
                }
                const int input = gba_row_input(map_row_ - kGbaRowFirstInput);
                if (c == *L.modifier)
                {
                    map_note_ = tr("That's the GBA button: pick another, or move the GBA button first.");
                    map_note_time_ = time_;
                    return Action::None;
                }
                /* Whatever GBA input was on the pressed control takes this one's old place. */
                int *const set = gba_slot_of(L, map_gba_slot_);
                if (!set)
                    return Action::None;
                for (int i = 0; i < GbaCount; ++i)
                    if (i != input && set[i] == c)
                        set[i] = set[input];
                set[input] = c;
                *L.use = map_gba_slot_; /* what was just made is what plays */
                return saved(trf("{button} is now on {control}.",
                                 {{"button", tr(gba_input_name(input))}, {"control", control_name(c)}}));
            }
        return Action::None;
    }
    if (switch_map_kind())
        return Action::None;
    if (up && map_row_ > 0)
    {
        --map_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && map_row_ + 1 < kGbaRows)
    {
        ++map_row_;
        sfx(Sound::MenuScroll);
    }
    if (pressed(BtnCircle))
    {
        map_kind_ = 0;
        close_mapping();
        return Action::None;
    }
    if (map_row_ == kGbaRowPlayer && (left || right))
    {
        map_gba_player_ = (map_gba_player_ + (left ? 3 : 1)) % 4;
        map_gba_slot_ = std::clamp(*s.gba_layout(map_gba_player_).use, 0, 2);
        sfx(Sound::MovingTab);
        return Action::None;
    }
    if (map_row_ == kGbaRowPlayer)
        return Action::None;
    if (map_row_ == kGbaRowSlot && (left || right))
    {
        map_gba_slot_ = (map_gba_slot_ + (left ? 2 : 1)) % 3;
        sfx(Sound::MovingTab);
        return Action::None;
    }
    if (map_row_ == kGbaRowSlot && pressed(BtnCross))
    {
        *L.use = map_gba_slot_;
        sfx(Sound::LaunchGame);
        return saved(trf("GBA buttons: {name}.", {{"name", tr(gba_slot_name(map_gba_slot_))}}));
    }
    const bool flip = pressed(BtnCross) || left || right;
    if (map_row_ == kGbaRowStick && flip)
    {
        *L.stick_dpad = !*L.stick_dpad;
        sfx(Sound::MovingTab);
        return saved(tr(*L.stick_dpad ? "The left stick is the GBA's D-pad too, with the GBA button held."
                                         : "The left stick does nothing for the GBA."));
    }
    if (map_row_ == kGbaRowAlways && flip)
    {
        *L.always = !*L.always;
        sfx(Sound::MovingTab);
        return saved(tr(*L.always ? "Always the GBA: no GBA button to hold for the GBA this player plays."
                                  : "Hold the GBA button to play the GBA."));
    }
    if (map_row_ == kGbaRowShowMode && flip)
    {
        s.gba_show_mode = (std::clamp(s.gba_show_mode, 0, 2) + (left ? 2 : 1)) % 3;
        sfx(Sound::MovingTab);
        return saved(tr(s.gba_show_mode == 2   ? "The GBA's screen is always on the TV."
                        : s.gba_show_mode == 1 ? "Each press shows or hides the GBA's screen."
                                               : "The GBA's screen shows while its button is held."));
    }
    if (pressed(BtnSquare) && map_row_ >= kGbaRowFirstInput && map_row_ < kGbaRowStick)
    {
        /* Square: the input on no control at all. */
        int *const own = gba_slot_of(L, map_gba_slot_);
        if (!own)
        {
            map_note_ = tr("Porpoise's buttons stay as they are: pick Mine 1 or Mine 2 to make your own.");
            map_note_time_ = time_;
            return Action::None;
        }
        const int input = gba_row_input(map_row_ - kGbaRowFirstInput);
        own[input] = -1;
        *L.use = map_gba_slot_;
        sfx(Sound::DetailsFlip);
        return saved(trf("{button} is on no button now.", {{"button", tr(gba_input_name(input))}}));
    }
    if (!pressed(BtnCross))
        return Action::None;
    int *const set = gba_slot_of(L, map_gba_slot_);
    if (!set && map_row_ != kGbaRowModifier)
    {
        map_note_ = tr("Porpoise's buttons stay as they are: pick Mine 1 or Mine 2 to make your own.");
        map_note_time_ = time_;
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    if (map_row_ == kGbaRowStartOver)
    {
        const GbaConfig d = porpoise::pad::gba_defaults();
        *L.modifier = d.modifier;
        for (int i = 0; i < GbaCount; ++i)
            set[i] = d.control[i];
        *L.stick_dpad = d.stick_dpad;
        sfx(Sound::LaunchGame);
        return saved(tr("The GBA is back to Porpoise's layout."));
    }
    map_capture_ = true;
    map_armed_ = false;
    map_capture_start_ = time_;
    sfx(Sound::DetailsFlip);
    return Action::None;
}

void App::draw_gba_mapping(double)
{
    Gfx &g = *g_;
    const Settings &s = *map_target_;
    const Settings::GbaLayoutRef L = const_cast<Settings &>(s).gba_layout(map_gba_player_);
    const GbaConfig gba = gba_slot_config(L, map_gba_slot_);
    if (map_in_game_)
        g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.78f), 1, 0);

    /* Left: the DualSense as the GBA, with the GBA button held. */
    const float lx = 90, ly = 136, lw = 830, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0A1236, 0.55f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    g.text_mid(Font::Bold, ts(46), lx + 46, ly + 64, kWhite, Align::Left, tr(kMapKindNames[kMapKindGba]));
    g.text_mid(Font::Regular, ts(26), lx + 46, ly + 112, kLavender, Align::Left,
               fit(g, Font::Regular, ts(26),
                   trf("Hold {control} and player 1's controller is the GBA", {{"control", control_name(gba.modifier)}}),
                   lw - 92));
    draw_gba_pad(gba, lx + 35, ly + 170, 760, 1.0f);
    g.text_mid(Font::Regular, ts(22), lx + lw * 0.5f, ly + lh - 40, kLavender, Align::Center,
               fit(g, Font::Regular, ts(22),
                   tr("For every GameCube game with a GBA on a port (Settings > GameCube Ports)."), lw - 60));

    /* Right: the GBA button, each GBA input and the DualSense control it's on, and the extras. */
    const float px = 950, py = 136, pw = 880, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const float row_x = px + 22, row_w = pw - 44;
    const float row_h = std::min(44.0f, (ph - 160) / float(kGbaRows));
    float y = py + 70;
    for (int row = 0; row < kGbaRows; ++row)
    {
        if (row == kGbaRowModifier || row == kGbaRowFirstInput || row == kGbaRowStick)
        {
            g.panel(row_x + 20, y + 4, row_w - 40, 1.5f, rgba(0x3D4F9E, 0.8f), 1, 0);
            y += 12;
        }
        const bool on = row == map_row_;
        const float cy = y + row_h * 0.5f;
        if (on)
            g.panel(row_x, y + 2, row_w, row_h - 4, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float right = row_x + row_w - 22;
        const Color ink = on ? kWhite : kSoft;
        const bool capturing = on && map_capture_;
        if (row == kGbaRowPlayer)
        {
            /* Whose buttons: each player has a whole GBA layout (Settings > GBA says who plays
             * which GBA). */
            g.text_mid(Font::SemiBold, ts(26), row_x + 30, cy, ink, Align::Left,
                       fit(g, Font::SemiBold, ts(26), tr("Player"), row_w * 0.3f));
            const float vx = row_x + row_w * 0.55f;
            g.glyph(Glyph::Arrow, vx - 140, cy, 14, ink, -kPi * 0.5f);
            g.glyph(Glyph::Arrow, vx + 140, cy, 14, ink, kPi * 0.5f);
            g.text_mid(Font::Bold, ts(27), vx, cy, kWhite, Align::Center,
                       trf("Player {n}", {{"n", std::to_string(map_gba_player_ + 1)}}));
        }
        else if (row == kGbaRowSlot)
        {
            /* Which buttons: Porpoise's, Mine 1, Mine 2 (and which are in use). */
            g.text_mid(Font::SemiBold, ts(26), row_x + 30, cy, ink, Align::Left,
                       fit(g, Font::SemiBold, ts(26), tr("Buttons"), row_w * 0.3f));
            const float vx = row_x + row_w * 0.55f;
            g.glyph(Glyph::Arrow, vx - 140, cy, 14, ink, -kPi * 0.5f);
            g.glyph(Glyph::Arrow, vx + 140, cy, 14, ink, kPi * 0.5f);
            g.text_mid(Font::Bold, ts(27), vx, cy, kWhite, Align::Center,
                       fit(g, Font::Bold, ts(27), tr(gba_slot_name(map_gba_slot_)), 250));
            const bool in_use = *L.use == map_gba_slot_;
            std::string tag = map_gba_slot_ == 0 ? tr("Locked") : "";
            if (in_use)
                tag = tag.empty() ? tr("In Use") : tr("In Use") + " \xE2\x80\xA2 " + tag;
            g.text_mid(Font::SemiBold, ts(21), right, cy, in_use ? rgba(0x7CF0A6) : kLavender, Align::Right,
                       fit(g, Font::SemiBold, ts(21), tag, row_w * 0.22f));
        }
        else if (row == kGbaRowModifier || (row >= kGbaRowFirstInput && row < kGbaRowStick))
        {
            const int input = row == kGbaRowModifier ? -1 : gba_row_input(row - kGbaRowFirstInput);
            gba_chip(input, row_x + 44, cy, 28, 1.0f);
            g.text_mid(Font::SemiBold, ts(25), row_x + 96, cy, ink, Align::Left,
                       tr(input < 0 ? "GBA Button (hold)" : gba_input_name(input)));
            const int control = input < 0 ? gba.modifier : gba.control[input];
            if (capturing)
                g.text_mid(Font::Bold, ts(24), right, cy, kCyan, Align::Right, tr("Press a button\xE2\x80\xA6"));
            else if (control < 0)
                g.text_mid(Font::SemiBold, ts(23), right, cy, kLavender, Align::Right, tr("Not Set"));
            else
                draw_control(control, right, cy, on, std::min(50.0f, row_h + 6));
        }
        else if (row == kGbaRowStick || row == kGbaRowAlways || row == kGbaRowShowMode)
        {
            const bool stick = row == kGbaRowStick, always = row == kGbaRowAlways;
            g.text_mid(Font::SemiBold, ts(25), row_x + 34, cy, ink, Align::Left,
                       tr(stick ? "Left Stick as the GBA's D-Pad" : always ? "Always the GBA" : "Show GBA Screen"));
            const std::string value =
                stick    ? tr(*L.stick_dpad ? "On" : "Off")
                : always ? tr(*L.always ? "On" : "Off")
                         : tr(s.gba_show_mode == 2 ? "On" : s.gba_show_mode == 1 ? "Toggle" : "While Held");
            g.text_mid(Font::Bold, ts(25), right, cy, kWhite, Align::Right, value);
        }
        else
            g.text_mid(Font::SemiBold, ts(26), row_x + 34, cy, ink, Align::Left,
                       tr("Start Over from Porpoise's Layout"));
        y += row_h;
    }
    const double since = time_ - map_note_time_;
    if (!map_note_.empty() && since < 4.0)
    {
        const float a = since < 3.0 ? 1.0f : float(4.0 - since);
        g.text_mid(Font::SemiBold, ts(24), px + pw * 0.5f, py + ph - 32, with_alpha(kCyan, a), Align::Center,
                   fit(g, Font::SemiBold, ts(24), map_note_, pw - 60));
    }
    draw_map_tabs();
    if (map_capture_)
    {
        const int left = int(std::ceil(kCaptureSeconds - (time_ - map_capture_start_)));
        const std::string what =
            map_row_ == kGbaRowModifier ? tr("GBA Button (hold)") : tr(gba_input_name(gba_row_input(map_row_ - kGbaRowFirstInput)));
        draw_prompts({}, {}, trf("Press the DualSense button for {button} ({n})",
                                 {{"button", what}, {"n", std::to_string(std::max(1, left))}}));
    }
    else
    {
        const bool input_row = map_row_ >= kGbaRowFirstInput && map_row_ < kGbaRowStick;
        std::vector<std::pair<Glyph, std::string>> left = {
            {Glyph::DPad, "Browse"},
            {Glyph::Cross, map_row_ == kGbaRowPlayer                                    ? "Browse"
                           : map_row_ == kGbaRowSlot                                    ? "Use"
                           : map_row_ == kGbaRowStartOver                             ? "Start Over"
                           : map_row_ == kGbaRowStick || map_row_ == kGbaRowAlways ||
                                     map_row_ == kGbaRowShowMode                        ? "Switch"
                                                                                      : "Change"}};
        if (input_row)
            left.push_back({Glyph::Square, "Clear"});
        left.push_back({Glyph::Circle, "Back"});
        draw_prompts(left, {{kKeyL1R1, "Controller"}}, "");
    }
}
} // namespace porpoise::ui
