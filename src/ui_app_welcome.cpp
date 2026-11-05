/* Porpoise UI - the first start: choose a look to begin with (Porpoise,
 * Revolution or Star Cube, or Porpoise Legacy under them), shown live behind
 * the cards; then the welcome and the setup check. 4.5 offers the looks once
 * more to everyone who had Porpoise before. Settings > About > Reinitialize
 * Porpoise brings it back. */

#include "ui_app.hpp"
#include "porpoise_pad.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace porpoise::ui
{

using namespace look;
using namespace porpoise::pad;

namespace
{
/* The looks offered, in order: three cards, then Porpoise Legacy under them. */
constexpr int kWelcomeThemes[4] = {int(ThemeId::Glass), int(ThemeId::Revolution), int(ThemeId::StarCube),
                                   int(ThemeId::Porpoise)};
constexpr int kLegacy = 3;

float ease(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

void App::start_welcome(bool upgrade)
{
    screen_ = Screen::Welcome;
    welcome_upgrade_ = upgrade;
    welcome_time_ = 0;
    welcome_pick_ = 0;
    if (!upgrade)
        for (int i = 0; i < 3; ++i)
            if (settings_->ui_theme == kWelcomeThemes[i])
                welcome_pick_ = i;
    /* The new look, chosen to begin with: shown behind the cards, in Light. */
    if (settings_->ui_theme != kWelcomeThemes[welcome_pick_] || (kWelcomeThemes[welcome_pick_] == int(ThemeId::Glass) &&
                                                                 settings_->ui_mode != 2))
    {
        if (kWelcomeThemes[welcome_pick_] == int(ThemeId::Glass))
            settings_->ui_mode = 2;
        const int was = settings_->ui_theme;
        settings_->ui_theme = kWelcomeThemes[welcome_pick_];
        look_changed(was);
        apply_look();
    }
    welcome_glide_ = float(welcome_pick_);
    welcome_legacy_ = 0;
}

void App::restart_fresh()
{
    tab_ = Tab::Library;
    screen_ = Screen::Main;
    dialog_ = Dialog{};
    selected_ = 0;
    scroll_ = 0;
    on_rail_ = true;
    rail_ = 0;
    mc_wii_ = false;
    sc_home_ = true;
    sc_face_ = 0;
    sc_calendar_ = false;
    sc_zoom_ = 0;
    home_page_ = 0;
    home_scroll_ = 0;
    home_synced_ = false;
    theme_seen_ = settings_->ui_theme;
    apply_look();
    build_settings();
}

App::Action App::update_welcome(bool up, bool down, bool left, bool right, double dt)
{
    const bool calm = settings_->reduced_motion;
    welcome_time_ = std::min(10.0f, welcome_time_ + float(dt));
    const bool legacy = welcome_pick_ == kLegacy;
    if (!legacy)
        welcome_glide_ = calm ? float(welcome_pick_) : smooth(welcome_glide_, float(welcome_pick_), dt, 10.0f);
    welcome_legacy_ = calm ? (legacy ? 1.0f : 0.0f) : smooth(welcome_legacy_, legacy ? 1.0f : 0.0f, dt, 12.0f);
    const int before = welcome_pick_;
    if (legacy)
    {
        /* Up goes back to the card it came from. */
        if (up)
            welcome_pick_ = std::clamp(int(std::lround(welcome_glide_)), 0, 2);
    }
    else
    {
        if (left && welcome_pick_ > 0)
            --welcome_pick_;
        if (right && welcome_pick_ < 2)
            ++welcome_pick_;
        if (down)
            welcome_pick_ = kLegacy;
    }
    if (welcome_pick_ != before)
    {
        /* The look changes behind the cards as you choose (the new look in Light,
         * its default). */
        const int was = settings_->ui_theme;
        settings_->ui_theme = kWelcomeThemes[welcome_pick_];
        if (settings_->ui_theme == int(ThemeId::Glass))
            settings_->ui_mode = 2;
        look_changed(was);
        apply_look();
        sfx(Sound::MenuScroll);
    }
    if (pressed(BtnCross) && welcome_time_ > 0.6f)
    {
        const int was = settings_->ui_theme;
        settings_->ui_theme = kWelcomeThemes[welcome_pick_];
        if (settings_->ui_theme == int(ThemeId::Glass))
            settings_->ui_mode = 2; /* the new look starts in Light */
        if (was != settings_->ui_theme)
            look_changed(was);
        const bool upgrade = welcome_upgrade_;
        welcome_upgrade_ = false;
        settings_->setup_checked = true;
        settings_->welcome_45 = true;
        settings_->save(settings_path_);
        restart_fresh();
        sfx(Sound::LaunchGame);
        if (upgrade)
            return Action::SettingsChanged; /* everything else is as it was */
        /* A few questions, then the welcome and what Porpoise can see. */
        wizard_step_ = -1;
        wizard_rescan_ = false;
        wizard_next();
        return Action::SettingsChanged;
    }
    return Action::None;
}

/* The next question of the first start's setup (each a dialog with two
 * answers; Circle keeps what is set). After the last, the setup check. */
void App::wizard_next()
{
    ++wizard_step_;
    const bool in_sandbox = sandboxed_;
    const int steps = in_sandbox ? 4 : 3;
    int step = wizard_step_;
    if (!in_sandbox && step == 0)
        step = wizard_step_ = 1; /* the sandbox question is only for a sandboxed start */
#ifdef PORPOISE_DESKTOP
    if (step <= 2)
        step = wizard_step_ = 3; /* a computer: no sandbox, its own folders */
#endif
    const int shown = in_sandbox ? step + 1 : step;
    const std::string title = trf("Set up Porpoise \xE2\x80\xA2 {n} of {all}",
                                  {{"n", std::to_string(shown)}, {"all", std::to_string(steps)}});
    switch (step)
    {
    case 0:
        open_dialog(DialogKind::Wizard, title,
                    tr("Porpoise started inside the app sandbox: it can't see /data or USB drives, and it asks your "
                       "jailbreak to free it each time it starts. If your jailbreak closes Porpoise when it does, "
                       "stay in the sandbox: games in /app0/porpoise/games still work."),
                    tr("Free it each start"));
        dialog_.no = tr("Stay in the Sandbox");
        dialog_.choice = settings_->stay_sandboxed ? 0 : 1;
        return;
    case 1:
        open_dialog(DialogKind::Wizard, title,
                    tr("Find your games by itself? Porpoise looks in /data/porpoise/games, /data/games, /data/roms, "
                       "/data/iso and on USB drives. With a big drive full of other things, choose your own folders "
                       "instead (Settings > Games > Add a Game Folder)."),
                    tr("Find Them"));
        dialog_.no = tr("I'll Choose Folders");
        dialog_.choice = settings_->auto_search ? 1 : 0;
        return;
    case 2:
        open_dialog(DialogKind::Wizard, title,
                    tr("Download box art and game info from GameTDB.com while the console is online?"),
                    tr("Download"));
        dialog_.no = tr("Not now");
        dialog_.choice = settings_->download_covers ? 1 : 0;
        return;
    case 3:
        open_dialog(DialogKind::Wizard, title,
                    tr("Which buttons? PlayStation: Cross is A, Circle is B. GameCube: Circle is A, Cross is B, as "
                       "on the GameCube controller. Change it any time in Settings > Controls."),
                    tr("PlayStation"));
        dialog_.no = tr("GameCube");
        dialog_.choice = settings_->button_layout == porpoise::pad::LayoutGameCube ? 0 : 1;
        return;
    default:
        wizard_step_ = -1;
        settings_->save(settings_path_);
        build_settings();
        show_setup_check(true); /* the welcome and what Porpoise can see */
        return;
    }
}

/* One look's card: a little picture of it, its name and what it is. */
void App::welcome_card(int which, float cx, float cy, float w, float h, float lift, float alpha, bool on, double time)
{
    Gfx &g = *g_;
    const float x = cx - w * 0.5f, y = cy - h * 0.5f - lift;
    const float t = settings_->reduced_motion ? 0.0f : float(time);
    /* The card: the look's own panel (glass_pill takes the theme's colors), a
     * ring round the chosen one. */
    glass_pill(x, y, w, h, kR + 6, 1.25f * alpha);
    if (on)
    {
        const bool solid = g.solid();
        g.set_solid(true);
        g.panel(x - 3, y - 3, w + 6, h + 6, kClear, 1, kR + 9, with_alpha(kWhite, 0.9f * alpha), 3.0f, 14);
        g.set_solid(solid);
    }
    /* The picture, in its own colors whatever the look behind it. */
    const Look look_was = g.look();
    const bool toned_was = g.toned();
    {
        Look plain = look_was;
        plain.hue = -1;
        plain.saturation = 1;
        plain.dark = 1;
        plain.fill_alpha = 1;
        plain.panel_style = 0;
        g.set_look(plain);
    }
    g.set_tone(false);
    const bool solid_was = g.solid();
    g.set_solid(true);
    const float px = x + 22, py = y + 22, pw = w - 44, ph = h * 0.50f;
    const int id = kWelcomeThemes[which];
    /* The player's own games, when there are some: the pictures show them. */
    std::vector<Game *> mine;
    if (lib_)
    {
        auto &games = lib_->games();
        const int n = lib_->shown();
        for (int k = 0; k < n && int(mine.size()) < 6; ++k)
        {
            Game &gm = games[std::size_t((std::max(0, selected_) + k) % std::max(1, n))];
            if (cover_of(gm))
                mine.push_back(&gm);
        }
    }
    if (id == int(ThemeId::Glass))
    {
        /* Porpoise: the room lit by the game in front, the cases standing in a
         * row with their reflections, the menu's glass at the side. */
        float tone[6] = {0.85f, 0.28f, 0.22f, 0.25f, 0.35f, 0.85f};
        if (!mine.empty() && mine[0]->cover)
            std::memcpy(tone, mine[0]->cover->tone, sizeof tone);
        g.panel(px, py, pw, ph, rgba(0x0B0E14, alpha), 1, kR * 0.7f);
        g.blob(px + pw * 0.40f, py + ph * 0.86f, pw * 1.0f, ph * 0.6f, Color{tone[0], tone[1], tone[2], 0.45f * alpha});
        g.blob(px + pw * 0.25f, py + ph * 0.2f, pw * 0.7f, ph * 0.6f, Color{tone[3], tone[4], tone[5], 0.22f * alpha});
        const float row_y = py + ph * 0.50f;
        if (mine.size() >= 3)
        {
            box_white_wii_ = true;
            box_reflect_ = 0.18f;
            const int order[5] = {3, 1, 4, 2, 0}; /* far ones first, the one in front last */
            const float xs[5] = {-2, -1, 1, 2, 0};
            for (int k = 0; k < 5; ++k)
            {
                const int gi = order[k] % int(mine.size());
                if (order[k] >= int(mine.size()) && order[k] > 2)
                    continue;
                const float off = xs[k];
                const float bw = off == 0 ? 92.0f : 74.0f, bh = bw * 190.0f / 135.0f;
                const float bx = px + pw * 0.40f + off * 62.0f + (off > 0 ? 18.0f : off < 0 ? -18.0f : 0.0f);
                box_bright_ = off == 0 ? 1.0f : 0.95f;
                draw_box3d(*mine[std::size_t(gi)], bx, row_y, bw, bh, bw * 0.103f, off == 0 ? 0.0f : -off / std::fabs(off) * 0.9f,
                           alpha, false);
            }
            box_bright_ = 1;
            box_reflect_ = 0;
            box_white_wii_ = false;
            g.set_look(look_was);
            g.set_look([&] {
                Look plain = look_was;
                plain.hue = -1;
                plain.saturation = 1;
                plain.dark = 1;
                plain.fill_alpha = 1;
                plain.panel_style = 0;
                return plain;
            }());
            g.set_tone(false);
            g.set_solid(true);
        }
        else
            for (int i = -1; i <= 1; ++i)
            {
                const float tw = i == 0 ? 84.0f : 62.0f, th = tw * 1.4f;
                const float tx = px + pw * 0.40f + float(i) * 96.0f - tw * 0.5f, ty = row_y - th * 0.5f + 4;
                g.panel(tx, ty, tw, th, rgba(i == 0 ? 0xD8463A : 0x3A4A66, 0.9f * alpha), 1, 6,
                        rgba(0xFFFFFF, (i == 0 ? 0.55f : 0.2f) * alpha), i == 0 ? 1.6f : 1.0f, i == 0 ? 10 : 0);
            }
        g.panel(px + pw - 104, py + 14, 90, ph - 28, rgba(0xFFFFFF, 0.10f * alpha), 1, 16, rgba(0xFFFFFF, 0.28f * alpha),
                1.2f);
        for (int i = 0; i < 5; ++i)
            g.panel(px + pw - 92, py + 32 + float(i) * 26, 66, 14, rgba(0xFFFFFF, (i == 1 ? 0.55f : 0.16f) * alpha), 1, 7);
        draw_mark(px + 34, py + 26, 28, with_alpha(rgba(0x5CE0D2), alpha));
    }
    else if (id == int(ThemeId::StarCube))
    {
        g.panel(px, py, pw, ph, rgba(0x020208, alpha), 1, kR * 0.7f, rgba(0x2A2A50, 0.8f * alpha), 1.2f);
        draw_glass_cube(px + pw * 0.5f, py + ph * 0.5f, ph * 0.46f, 0.5f + t * 0.5f, 0.45f + 0.1f * std::sin(t * 0.7f),
                        0.1f, rgba(0x3A2FD0, 0.45f), rgba(0xD0C6FF), mine.empty() ? nullptr : mine[0]->cover, alpha, on);
        for (int i = 0; i < 5; ++i)
        {
            const float fx = px + 30 + std::fmod(float(i) * 97.0f + t * 12.0f, pw - 60);
            const float fy = py + 28 + std::fmod(float(i) * 53.0f, ph - 56);
            draw_glass_cube(fx, fy, 16, t * 0.6f + float(i), t * 0.4f + float(i), 0, rgba(0x2A3FD8, 0.35f),
                            rgba(0x8FA0FF, 0.6f), nullptr, alpha * 0.6f, false);
        }
    }
    else if (id == int(ThemeId::Revolution))
    {
        g.panel(px, py, pw, ph, rgba(0xF2F4F7, alpha), 1, kR * 0.7f);
        const float tw = (pw - 70) / 3, th = (ph - 60) / 2;
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
            {
                const bool lit = r == 0 && c == 1;
                const float tx = px + 20 + float(c) * (tw + 15), ty = py + 20 + float(r) * (th + 20);
                g.panel(tx, ty, tw, th, rgba(0xFFFFFF, alpha), 1, 12, lit ? rgba(0x3DB8EC, alpha) : rgba(0xC6CCD4, alpha),
                        lit ? 3.0f : 1.6f);
                const std::size_t k = std::size_t(r * 3 + c);
                if (k < mine.size() && mine[k]->cover)
                {
                    /* The tile's cover, at its left, as the home shows it. */
                    const float ih = th - 16, iw = ih * 135.0f / 190.0f;
                    g.image(mine[k]->cover, tx + 8, ty + 8, iw, ih, rgba(0xFFFFFF, alpha), 6);
                    g.panel(tx + 16 + iw, ty + th * 0.38f, tw - iw - 30, 7, rgba(0x9AA3AD, 0.6f * alpha), 1, 3.5f);
                    g.panel(tx + 16 + iw, ty + th * 0.58f, (tw - iw - 30) * 0.6f, 6, rgba(0xC6CCD4, 0.7f * alpha), 1, 3);
                }
            }
    }
    g.set_look(look_was);
    g.set_solid(solid_was);
    /* Its name and what it is, in the look's ink. */
    g.set_tone(revolution() || (glass() && settings_->ui_mode == 2));
    const Theme &theme_info = theme(kWelcomeThemes[which]);
    g.text_mid(Font::Bold, ts(40), cx, py + ph + 52, with_alpha(kWhite, alpha), Align::Center, tr(theme_info.name));
    const auto lines = wrap(g, Font::Regular, ts(24), tr(theme_info.about), w - 60, 4);
    float ly = py + ph + 92;
    for (const std::string &line : lines)
    {
        g.text(Font::Regular, ts(24), cx, ly, with_alpha(kWhite, 0.78f * alpha), Align::Center, line);
        ly += ts(24) * 1.35f;
    }
    g.set_tone(toned_was);
}

void App::draw_welcome(double time)
{
    Gfx &g = *g_;
    g.set_layer();
    g.set_tone(false);
    if (revolution())
        draw_room();
    else
        g.background();
    const bool calm = settings_->reduced_motion;
    const float in = calm ? 1.0f : ease(welcome_time_ / 0.6f);
    g.set_intro(intro_fade_, intro_dy_);

    /* The heading, in the theme's ink (Light's too). */
    const bool ink_tone = revolution() || (glass() && settings_->ui_mode == 2);
    g.set_tone(ink_tone);
    g.set_layer(0, 24.0f * (1.0f - in), in);
    /* The wordmark's dolphin (the boot lands it here on a first start). */
    draw_welcome_mark(960, 104, 132, ink_tone, in);
    g.text_mid(Font::ExtraBold, ts(58), 960, 202, kWhite, Align::Center, tr("Welcome to Porpoise 4.5"));
    if (welcome_upgrade_)
        g.text_mid(Font::SemiBold, ts(28), 960, 260, with_alpha(kWhite, 0.82f), Align::Center,
                   tr("Porpoise 4.5 brings a new look. Pick the one you like best."));
    else
        g.text_mid(Font::SemiBold, ts(28), 960, 260, with_alpha(kWhite, 0.82f), Align::Center,
#ifdef PORPOISE_DESKTOP
                   tr("Porpoise plays your GameCube and Wii games on your computer, powered by Dolphin."));
#else
                   tr("Porpoise plays your GameCube and Wii games on PS5, powered by Dolphin."));
#endif
    g.text_mid(Font::Regular, ts(26), 960, 302, with_alpha(kWhite, 0.66f), Align::Center,
               tr("Choose a look. You can change it any time in Settings > Interface."));
    g.set_tone(false);

    /* The three cards; the chosen one lifts, and a light glides under it. */
    const float w = 500, h = 520, gap = 560, cy = 610;
    const float on_cards = 1.0f - welcome_legacy_;
    g.set_layer();
    g.blob(960 + (welcome_glide_ - 1.0f) * gap, cy + h * 0.5f + 22, w * 1.3f, 64, rgba(0x5CD3FF, 0.30f * in * on_cards));
    for (int i = 0; i < 3; ++i)
    {
        const float arrive = calm ? 1.0f : ease((welcome_time_ - 0.15f - 0.12f * float(i)) / 0.5f);
        const bool on = i == welcome_pick_;
        const float lift = on ? 22.0f : 0.0f;
        const float x = 960 + float(i - 1) * gap;
        g.set_layer(0, 60.0f * (1.0f - arrive), arrive * (on ? 1.0f : (welcome_pick_ == kLegacy ? 0.6f : 0.72f)));
        welcome_card(i, x, cy + 30.0f * (1.0f - arrive), on ? w * 1.04f : w, on ? h * 1.04f : h, lift, 1.0f, on, time);
    }

    /* Under them, plainly: the look Porpoise had before. */
    {
        const float arrive = calm ? 1.0f : ease((welcome_time_ - 0.55f) / 0.5f);
        g.set_layer(0, 20.0f * (1.0f - arrive), arrive);
        g.set_tone(ink_tone);
        const std::string line = tr("Or continue using the Porpoise Legacy theme");
        const float lw = g.measure(Font::SemiBold, ts(26), line);
        const float ly = 936, bw = lw + 72, bh = 54;
        if (welcome_legacy_ > 0.01f)
            g.panel(960 - bw * 0.5f, ly - bh * 0.5f, bw, bh, rgba(0xFFFFFF, 0.10f * welcome_legacy_), 1, bh * 0.5f,
                    rgba(0xBDF1FF, 0.8f * welcome_legacy_), 1.8f);
        g.text_mid(Font::SemiBold, ts(26), 960, ly, with_alpha(kWhite, 0.62f + 0.38f * welcome_legacy_), Align::Center,
                   line);
        g.set_tone(false);
    }
    g.set_layer();
    g.set_tone(ink_tone);
    draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Confirm"}}, {}, "");
    g.set_tone(false);
    g.set_intro(1, 0);
}

} // namespace porpoise::ui
