/* Porpoise UI - the launcher: library, details, launch, memory cards, settings.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ui_app.cpp holds the library, details, memory cards, dialogs and launch;
 * ui_app_settings.cpp holds Settings, a game's own settings and the folder
 * browser. ui_app_common.hpp has what they share. */
#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "porpoise_banner.hpp"
#include "porpoise_borders.hpp"
#include "porpoise_netfs.hpp"
#include "ui_achievements.hpp"
#include "ui_theme.hpp"
#include "ui_cheats.hpp"
#include "porpoise_tile_art.hpp"
#include "ui_gfx.hpp"
#include "ui_library.hpp"
#include "ui_memcard.hpp"
#include "ui_recommend.hpp"
#include "ui_settings.hpp"

namespace porpoise::ui
{
/* The GameCube controller as a Wii game's controller (porpoise::pad::WiiGameCube). */
/* Dolphin's built-in graphics mods (porpoise_gfxmods): the rows' texts. */
inline constexpr const char *kGfxBloomHelp =
    "Dolphin's graphics mod for this game's glow, which can look blocky at high resolutions. Off removes it, "
    "Blurred softens it, Native Resolution draws it as the console did. Applies the next time the game starts.";
inline constexpr const char *kGfxDofHelp =
    "Dolphin's graphics mod for this game's depth of field (the blur on faraway things). Off removes it, Blurred "
    "softens it, Native Resolution draws it as the console did. Applies the next time the game starts.";
inline constexpr const char *kGfxHudHelp =
    "Hides the game's HUD, for clean screenshots. Applies the next time the game starts.";
inline constexpr const char *kGfxGoopHelp =
    "Dolphin's graphics mod for Super Mario Sunshine: the goop is drawn at the game's own resolution, with round "
    "edges instead of blocky ones at high resolutions. Applies the next time the game starts.";
/* Settings > Controls' extras: the rows' texts and choices. */
inline constexpr const char *kQuickSlotHelp =
    "In a game, touch pad + L1 saves to this slot and touch pad + L2 loads it, without opening the menu.";
inline constexpr const char *kTurboHelp =
    "While you hold this button, it presses again and again by itself, about fifteen times a second: for games "
    "that make you mash a button.";
inline constexpr const char *kTriggerFeelHelp =
    "The DualSense's adaptive triggers click near the bottom of L2 and R2, as the GameCube's L and R do at the end "
    "of their press. In GameCube games, and Wii games played with the GameCube controller.";
inline constexpr const char *kLightHelp =
    "The color of this player's controller light bar, so everyone knows which controller is theirs.";
inline const std::vector<std::string> kQuickSlotValues = {"Off", "Slot 1", "Slot 2", "Slot 3"};
inline const std::vector<std::string> kTurboValues = {"Off", "Cross", "Circle", "Square", "Triangle",
                                                      "L1",  "R1",    "L2",     "R2"};
inline const std::vector<std::string> kTriggerFeelValues = {"Off", "Light", "Firm", "Extra Firm"};
inline const std::vector<std::string> kLightValues = {"Blue",   "Red",    "Green", "Pink",  "Purple",
                                                      "Orange", "Yellow", "Cyan",  "White", "Off"};
inline constexpr const char *kGameCubeOnWiiHelp =
    "GameCube Controller: for Wii games that take one, like Super Smash Bros. Brawl and Mario Kart Wii, with your "
    "GameCube buttons.";
/* Physical buttons, as porpoise::pad::Button bits. */
struct Input
{
    std::uint32_t held = 0;
    float stick_x = 0, stick_y = 0; /* -1..1 */
    float right_x = 0, right_y = 0; /* the right stick, -1..1 */
    std::uint32_t wii = 0;          /* the menu Wii Remote's buttons as the DualSense's (wii_menu_bits) */
};

/* The launcher's sounds; the host plays them (App only says when). */
enum class Sound
{
    GameRow,
    MenuScroll,
    MovingTab,
    DetailsFlip,
    LaunchGame,
    BootSonar,     /* the boot's hum and pings, in Soundscape's key (porpoise::sound) */
    BootSonarLegacy, /* ...in the original music's */
};

class Gfx;
/* The boot's sonar screen at rest (main's first frames, before the menus). */
void draw_boot_backdrop(Gfx &g);
/* The device is going: the boot's own pictures with it (ui_app_boot.cpp). */
void forget_boot_textures();

struct OnlineTest; /* the Online hub's server test (ui_app_online.cpp) */
} // namespace porpoise::ui
namespace porpoise::nand
{
struct Progress;
}
namespace porpoise::sysupdate
{
struct Job;
}
namespace porpoise::ui
{

/* An HD texture pack in Load/Textures (Settings > Textures). */
struct TexturePack
{
    std::string folder; /* the game's ID, or its first letters */
    long textures = 0;  /* its pictures (.png, .dds) */
    bool counted = false;
};

/* The menu Wii Remote's buttons (and its Nunchuk's, in the menus) as the
 * DualSense's: D-pad, A Cross, B Circle, + R1, - L1, 1 Triangle, 2 R2; C Options,
 * Z Square, the stick the D-pad. */
std::uint32_t wii_menu_bits(std::uint16_t remote_buttons);
/* The Switch 2's GameCube controller (porpoise_ns2gc.hpp) in the menus, as the
 * DualSense's buttons: A Cross, B Circle, X Square, Y Triangle, START Options,
 * Z R1, ZL L1, L and R (or their triggers pressed) L2 and R2, the D-pad and
 * the control stick the D-pad. 0 when it isn't plugged in. */
std::uint32_t gc_menu_bits();

class App
{
public:
    struct SaveScan; /* Memory Cards read on a worker thread (ui_app.cpp) */
    enum class Action
    {
        None,
        Launch,      /* launch_game() says which */
        SettingsChanged,
        Rescan,      /* game folders changed: search again */
        FetchCovers, /* covers and game info now (Sort & filter) */
        CheckUpdate, /* look for a newer Porpoise now */
        InstallUpdate,
        InstallVersion, /* the one picked in About > Choose a version (picked_version()) */
        Quit,        /* close Porpoise (after an update) */
        Reinitialize, /* settings wiped: Porpoise starts again as at its first start */
        MoveData,     /* Porpoise's folder moves to move_target() */
        UseFolder,    /* the Porpoise folder already at move_target() becomes the one in use */
        Restart,        /* close Porpoise and open it again (a setting that needs it) */
        CoversAgain,    /* the art of take_covers_again()'s games downloaded again */
        StayInSandbox,  /* freeing Porpoise closed it: don't ask again (Stay in the sandbox on) */
        RetryJailbreak, /* freeing Porpoise closed it: ask again, restarting Porpoise */
        LiveSettings,   /* a look or sound setting changed and is saved: use it now */
    };
    const std::string &move_target() const { return move_target_; }

    void set_sound_hook(void (*play)(Sound)) { sound_ = play; }
    /* Plays a Wii disc's banner jingle (48 kHz stereo frames); null stops it. */
    void set_jingle_hook(void (*play)(const std::int16_t *, std::size_t)) { jingle_ = play; }
    void init(Gfx *gfx, Library *library, Settings *settings, const std::string &settings_path,
              const std::string &options_path, const std::string &saves_dir);
    Action update(const Input &in, double dt);
    /* tools/ui-preview: Controllers & Accessories with a device chosen, or its
     * settings section (section = true). */
    void preview_controllers(int select, bool section);
    void preview_real_remote_setup() /* the UI preview: Wii Remote Setup with real Wii Remotes */
    {
        ws_real_ = true;
        ws_game_ = nullptr;
        screen_ = Screen::WiiSetup;
    }
    void draw(double time);

    Game *launch_game() { return launch_; }
    double launch_intro_length() const; /* seconds: the library fading under the launch screen */
    void launch_cover_now(); /* the launching game's cover, loaded now (after the display changed hands) */
    /* The launch screen's entrance (0..1, about half a second): the library
     * still shows under it until it is 1. */
    float launch_intro(double time) const;
    /* The game was left to start it afresh (applied changes that need it). */
    bool take_relaunch()
    {
        const bool r = menu_relaunch_;
        menu_relaunch_ = false;
        return r;
    }
    /* After CoversAgain: the games (IDs) whose art is downloaded again. */
    std::vector<std::string> take_covers_again()
    {
        std::vector<std::string> ids;
        ids.swap(covers_again_);
        return ids;
    }
    /* After SettingsChanged: whether the applied changes need a new search for games. */
    bool take_rescan()
    {
        const bool r = rescan_after_apply_;
        rescan_after_apply_ = false;
        return r;
    }
#ifdef PORPOISE_HOST_PREVIEW
    void preview_setup_step(int step) { ws_step_ = step; } /* tools/ui-preview: show each step */
#ifdef PORPOISE_HOST_PREVIEW
    /* tools/ui-preview: the Controllers pages (PV_CTL=1). */
    void preview_controllers(int step, bool move, int controller, int point, int row)
    {
        if (screen_ != Screen::WiiSetup)
            open_wii_setup(nullptr, false, "");
        ws_move_ = move;
        ws_controller_ = controller;
        ws_step_ = step;
        ws_point_ = point;
        ws_row_ = row;
    }
    /* tools/ui-preview: the Controller Hub (view 0 the tiles, 1 every setting; open a section). */
    void preview_ctl_hub(bool move, int controller, int view, int open, int focus, int sub)
    {
        if (screen_ != Screen::WiiSetup)
            open_wii_setup(nullptr, false, "");
        ws_move_ = move;
        ws_controller_ = controller;
        ws_step_ = kWsController;
        ws_view_ = view;
        ws_open_ = open;
        ws_focus_ = focus;
        ws_front_ = sub;
        ws_sub_ = sub;
        ws_scroll_ = ws_scroll_to_;
        ws_pic_ = -2;
        ws_pic_was_ = -1;
    }
    /* 4.5 Beta 4: the hub on another player's page, holding hold, playing as as. */
    void preview_hub_player(int player, int hold, int as)
    {
        static const char *const holds[4] = {"", "wii_p2_hold", "wii_p3_hold", "wii_p4_hold"};
        static const char *const ases[4] = {"", "wii_p2_as", "wii_p3_as", "wii_p4_as"};
        if (player > 0)
        {
            ws_eff_.set(holds[player], std::to_string(hold));
            ws_eff_.set(ases[player], std::to_string(as));
        }
        ws_player_ = player;
        ws_player_at_ = -10;
        ws_pcol_ = hold;
        ws_prow_ = 0;
        ws_pill_x_ = -1;
    }
    void preview_glass(bool menu, int row, bool island)
    {
        gl_menu_ = menu;
        gl_row_ = row;
        isl_open_ = island;
    }
    void preview_ctl_toasts()
    {
        ctl_toasts_.clear();
        ctl_toast(0, "PlayStation Move connected", "Player 1 \xC2\xB7 Wii Remote");
        ctl_toast(2, "Move battery low", "Player 1 \xC2\xB7 charge it soon");
        ctl_toast(3, "PlayStation Move disconnected", "Player 1 is the DualSense again");
    }
#endif
    void preview_look_changed() { apply_look(); }
    void preview_ingame_hub(bool recal) { open_wii_setup_in_game(recal); } /* tools/ui-preview */
    bool preview_ingame_hub_open() const { return ws_in_game_; }
    void preview_wii_guide() { open_wii_guide(); }
    void preview_island_pick(int i) { isl_pick_ = i; } /* tools/ui-preview */
    void preview_move_pointer(float x, float y, bool on) /* tools/ui-preview: the Move's pointer there */
    {
        preview_mp_x_ = x;
        preview_mp_y_ = y;
        mp_on_ = on;
    }
    float preview_mp_x_ = -1, preview_mp_y_ = -1;
    void preview_dialog(bool danger) /* tools/ui-preview */
    {
        open_dialog(DialogKind::Info, "Reinitialize Porpoise?",
                    "Every setting, and each game's own settings, go back to how Porpoise ships, and it starts again "
                    "as it did the first time. Your games, folders, memory cards, saves and save states stay.",
                    "Reinitialize");
        dialog_.danger = danger;
        dialog_.no = "Cancel";
    } /* tools/ui-preview */ /* tools/ui-preview: a look set from outside */
    void preview_launch_start(double t) { launch_start_ = t; } /* tools/ui-preview: the entrance's clock */
    void preview_game_settings(int i) { open_game_settings(lib_->games()[std::size_t(i)]); } /* tools/ui-preview */
    void preview_tile_art(int part, int row, const porpoise::tileart::Layer &l) /* tools/ui-preview */
    {
        art_.part = part;
        art_.row = row;
        (part ? art_.spec.bg : art_.spec.icon) = l;
        art_.dirty[part] = true;
    }
    void preview_mapping(int kind, int row) /* tools/ui-preview: Customize buttons on a controller's tab */
    {
        map_target_ = settings_;
        map_game_ = nullptr;
        map_in_game_ = false;
        map_return_ = Screen::Main;
        begin_mapping();
        map_kind_ = kind;
        map_row_ = row;
        open_screen(Screen::Mapping);
    }
    void preview_pointer(float x, float y) /* tools/ui-preview: the home screen's pointer there */
    {
        preview_px_ = x;
        preview_py_ = y;
    }
    void preview_home_page(int page) { home_page_ = page, home_scroll_ = float(page), home_synced_ = true; }
    void preview_starcube(int face, bool page);
    void preview_shots(bool view) /* tools/ui-preview: the gallery, or one picture */
    {
        open_shots("", Screen::Main);
        shot_sel_ = 1;
        shot_view_ = view;
    }
    void preview_nand(int done, int total); /* tools/ui-preview: the import's progress */
    void preview_code(const std::string &name, const std::string &digits) /* tools/ui-preview: Add a Code */
    {
        open_code();
        code_.name = name;
        code_.digits = digits;
        code_.kr = 1, code_.kc = 2;
        code_.anim = 1;
    }
    void preview_system(int mode, int row) /* tools/ui-preview: Console's menus and channels lists */
    {
        open_system(mode);
        sys_.row = row;
        sys_.anim = 1;
        if (mode == 3 && sys_.marked.size() > 1)
            sys_.marked[1] = 1;
    }
    void preview_dock(int sel) /* tools/ui-preview */
    {
        dock_focus_ = sel >= 0, dock_sel_ = sel, dock_lift_ = 1;
        dock_open_ = sel >= 0 ? 1.0f : 0.0f;
        dock_glide_ = float(sel < 0 ? 0 : sel);
    }
    /* tools/ui-preview: Settings on a section's name (on its rows when row >= 0). */
    void preview_section(const std::string &name, int row = -1)
    {
        for (int i = 0; i < section_count(); ++i)
            if (section_name(i) == name)
            {
                rail_ = i;
                on_rail_ = row < 0;
                if (row >= 0)
                    settings_row_ = first_row_of(name) + row;
            }
    }
    /* tools/ui-preview: the Online hub on a card (its rows when inside). */
    void preview_hub(int mode, bool inside, int row, int typing = 0, const std::string &text = "",
                     const std::vector<std::pair<std::string, std::string>> &servers = {})
    {
        if (!hub_.open)
            open_hub();
        if (!servers.empty())
            hub_.servers = servers;
        hub_.mode = mode, hub_.inside = inside, hub_.row = row, hub_.typing = typing, hub_.text = text;
        hub_.anim = 1;
        hub_.kb = Keyboard{};
        hub_.kb.numeric = typing == 2;
        hub_.name = "Dawnofthed WFC";
    }
    void preview_hub_close() { hub_ = HubPanel{}; }
#endif
    /* Channels (WADs) the core installs in the Wii before this start, one a
     * line; asking clears them. */
    /* The Wii System Update this start runs first (its region), or ""; asking clears it. */
    std::string take_sys_update()
    {
        std::string s;
        s.swap(pending_update_);
        return s;
    }
    std::string update_list() const { return update_list_path(); }
    /* 3.1: Wii System Update stopped the art downloads for its request and
     * didn't go on to a game (cancelled, failed): they start again. */
    bool take_covers_resume()
    {
        const bool r = covers_resume_;
        covers_resume_ = false;
        return r;
    }
    std::string take_wad_installs()
    {
        std::string s;
        for (const std::string &w : wad_installs_)
            s += w + "\n";
        wad_installs_.clear();
        return s;
    }
    /* The save state to start it from ("" for none); asking clears it. */
    std::string take_launch_state()
    {
        std::string s;
        s.swap(launch_state_);
        return s;
    }
    /* The launch screen, while the emulator starts. progress < 0 = unknown. */
    /* clock: the time the launch screen is drawn with (draw_launch's), so its
     * entrance plays every launch, not only the first (the menus' own clock
     * stands still while a game runs). */
    void begin_launch(Game *g, double clock = -1); /* (-1: the menus' clock, tools/ui-preview) */
    void set_launch_status(const std::string &status, float progress);
    void draw_launch(double time);
    /* Covers live on a device; after a device change they must be reloaded. */
    void forget_textures();
    /* Before the library is searched again: gives the covers' textures back. */
    void release_covers();
    /* After it was searched again. */
    void library_changed();
    /* Art (cover or disc) was downloaded for this disc ID. */
    void cover_arrived(const std::string &id);
    /* A short line under the game count, e.g. "Getting covers 3 of 9". */
    void set_note(const std::string &note) { note_ = note; }
    /* A line by the game count for a few seconds ("Covers and game info are up to date."). */
    void flash_note(const std::string &note)
    {
        flash_note_ = note;
        flash_time_ = time_;
    }
    /* Where a game's own settings are kept (they may not exist). */
    std::string game_settings_path(const Game &g) const;
    void auto_apply_picks(const Game &g); /* 4.6: Porpoise's Picks, the first time (ui_app_settings.cpp) */

    /* The in-game menu (Options + touch pad), over the paused game.
     * play: the settings the game runs with; quick changes go there and into
     * the game's own settings file. */
    void open_game_menu(Game *game, Settings *play);
    /* 3.1: the running game's discs (their disc numbers, 0 the first, in the
     * core's order), for Change Disc; 4.0: and other games' discs that can go
     * in (title and disc number, after the game's own). current: which of all
     * of them is in (-1: none known). */
    void set_menu_discs(std::vector<int> own, int current, std::vector<std::pair<std::string, int>> others = {});
    /* The disc Change Disc chose (of the game's own, then the others), or -1. */
    int take_menu_disc();
    /* Returns a porpoise::core::Menu answer (0 stay, 1 resume, 2 library, 3 home). */
    int update_game_menu(const Input &in, double dt);
    void draw_game_menu(double time);
    /* The key of a quick setting just changed in the menu ("" when none). */
    std::string take_menu_change()
    {
        std::string k;
        if (!menu_changes_.empty())
        {
            k = menu_changes_.front();
            menu_changes_.erase(menu_changes_.begin());
            return k;
        }
        k.swap(menu_change_);
        return k;
    }
    /* A save or load the menu asks of the host, which does it on the core's
     * thread and answers with menu_state_done(). */
    struct MenuRequest
    {
        enum Kind
        {
            None,
            Save,
            Load,
        } kind = None;
        int slot = 0;
    };
    MenuRequest take_menu_request();
    /* Take Screenshot, chosen in the menu: once. And a note to show in it. */
    bool take_menu_shot() { return std::exchange(menu_shot_, false); }
    void menu_note(const std::string &note)
    {
        menu_note_ = note;
        menu_note_time_ = time_;
    }
    /* Saving or loading is under way (the menu shows it). */
    bool menu_busy() const { return menu_busy_.kind != MenuRequest::None; }
    void menu_state_done(MenuRequest::Kind kind, int slot, bool ok);
    /* Fast forward chosen in the menu: 1 (off), 2 or 4. Not saved. */
    int menu_fast_forward() const { return menu_ff_ == 2 ? 4 : menu_ff_ == 1 ? 2 : 1; }
    void step_fast_forward() { menu_ff_ = (menu_ff_ + 1) % 3; }
    /* The newest release on GitHub ("v1.2" and its page), for the update notice. */
    /* Every release GitHub lists (newest first), for About > Choose a version. */
    void set_versions(const std::vector<std::string> &tags, const std::vector<int> &builds,
                      const std::vector<bool> &betas, const std::vector<std::size_t> &sizes);
    int picked_version() const { return version_pick_; }
    static std::string version_label(const std::string &tag, int build);
    void set_latest_release(const std::string &tag, const std::string &url, std::size_t zip_size = 0,
                            int build = 0);
    /* The updater, as porpoise::update::Phase numbers: 0 idle, 1 checking,
     * 2 downloading, 3 installing, 4 done, 5 failed, 6 checked. */
    void set_update_progress(int phase, std::size_t done, std::size_t total, const std::string &error);
    bool updating() const { return update_phase_ == 2 || update_phase_ == 3 || update_phase_ == 4 || update_phase_ == 7; }
    /* Back from a game to the library. */
    void return_from_game();
    /* Black over everything at amount (0..1), with Porpoise's mark in the
     * middle: leaving a game, coming back, and the start. */
    void draw_curtain(float amount);
    /* A line under the curtain's mark ("Saving your place..."), "" for none. */
    void set_curtain_note(const std::string &note) { curtain_note_ = note; }
    std::string curtain_note_;
    /* The menus' entrance: everything but the background faded by `fade` and
     * lowered by `dy` design pixels (Porpoise starting, or back from a game). */
    void set_intro(float fade, float dy)
    {
        intro_fade_ = fade;
        intro_dy_ = dy;
    }
    /* Buttons already down count only once they've been let go. */
    void swallow_held() { held_ = prev_ = 0xFFFFFFFFu; }
    /* The boot (ui_app_boot.cpp): Sonar, over the menus as they come up. Once a
     * frame before update: true while it keeps the buttons (any button skips
     * ahead to the landing). Drawn after draw(). Events for main to act on:
     * kBootMusic, the music comes in now; kBootSkipped, the hum stops. */
    void start_boot(bool legacy_music);
    bool boot_on() const { return boot_on_; }
    bool boot_frame(const Input &in, bool ready); /* ready: frames come steadily (it waits for them) */
    void draw_boot();
    void draw_move_pointer(); /* the Move's pointer, over the menus (ui_app_movepointer.cpp) */
    static constexpr int kBootMusic = 1, kBootSkipped = 2;
    int take_boot_events();
    void preview_boot_time(double t) { preview_boot_t_ = t; } /* tools/ui-preview */
    /* The menu language changed (rebuilds Settings' rows). */
    void language_changed() { build_settings(); }
    /* A message with an OK button. */
    void show_message(const std::string &title, const std::string &message)
    {
        open_dialog(DialogKind::Info, title, message, "");
    }
    /* The sandbox message at start, which the player can turn off. */
    void show_sandbox_notice(const std::string &title, const std::string &message);
    /* An alpha or beta build: its notice, once nothing else is showing, the
     * first time this build starts (ui_app.cpp). */
    void show_testing_notice() { testing_notice_ = true; }
    /* Freeing Porpoise from the sandbox closed it last time, so it didn't ask. */
    void offer_jailbreak_retry();
    /* RetroAchievements: the account the panel shows, and signing in and out
     * (porpoise_ra, given by the host). Without them there is no panel. */
    struct RaState
    {
        bool signed_in = false, busy = false;
        std::string user, message;
        int points = 0;
    };
    void set_ra(std::function<RaState()> state, std::function<bool(const std::string &, const std::string &)> login,
                std::function<void()> logout)
    {
        ra_state_ = std::move(state);
        ra_login_ = std::move(login);
        ra_logout_ = std::move(logout);
    }
    /* The account panel (L1 + Square in the library, or Settings > Games). */
    void open_account();
    /* Signed in or out: Settings' row says so. */
    void account_changed() { build_settings(); }
    /* A game's achievements: live (the game running) or as last kept. */
    using AchievementSource = std::function<AchievementSet(const std::string &disc_id, bool live)>;
    void set_achievement_source(AchievementSource source) { ach_source_ = std::move(source); }
#ifdef PORPOISE_HOST_PREVIEW
    void preview_achievements(int focus)
    {
        ach_focus_ = focus;
        if (!lib_->games().empty())
            open_achievements(lib_->games().front());
    }
#endif
#ifdef PORPOISE_HOST_PREVIEW
    void preview_account(bool typing, int field, int kr, int kc)
    {
        open_account();
        acct_.typing = typing;
        acct_.field = field;
        acct_.row = field;
        acct_.kb.kr = kr;
        acct_.kb.kc = kc;
        acct_.anim = 1;
        acct_.user = "Ripalda";
        acct_.pass = "hunter22";
    }
    /* tools/ui-preview: the network share panel, a row in focus, typing or
     * not, with a message (ok: green). */
    void preview_share_busy(bool find); /* tools/ui-preview: a test or search under way (ui_app_netshare.cpp) */
    /* tools/ui-preview: the computers found (scanning: still looking); nfs: the NFS type. */
    void preview_share_network(const std::vector<netfs::Found> &computers, bool scanning, bool nfs, int card);
    void preview_share_open(int index) { open_share(index); }
    void preview_dns(const std::string &text, const std::string &message)
    {
        open_dns();
        dns_.text = text;
        dns_.message = message;
        dns_.kb.kr = 3;
        dns_.kb.kc = 0;
        dns_.anim = 1;
    }
    void preview_dns_close() { dns_ = DnsPanel{}; }
    bool preview_share_waiting(); /* a scan, search or test going on */
    std::string preview_share_state();
    void preview_share_numeric()
    {
        share_.kb = Keyboard{};
        share_.kb.numeric = true;
        share_.kb.kr = 2;
        share_.kb.kc = 1;
    }
    void preview_share(int index, int row, int button, bool typing, const std::string &message, bool ok,
                       const std::vector<std::string> &found = {})
    {
        open_share(index);
        if (index < 0)
        {
            share_.share = netfs::Share{"", "192.168.1.20", "", "", "", "", ""};
            share_.path = "Games";
        }
        share_.row = row;
        share_.button = button;
        share_.typing = typing;
        share_.message = message;
        share_.message_ok = ok;
        share_.found = found;
        share_.find_key = "x";
        share_.scan.reset();
        share_.scanned = true;
        share_.anim = 1;
    }
#endif

private:
    enum class Tab
    {
        Library,
        MemoryCards,
        Settings,
    };
    enum class Screen
    {
        Main,
        Details,
        Sort,
        Browse,       /* choosing a game folder */
        GameSettings, /* one game's own settings */
        Mapping,      /* the buttons: a GameCube input for each DualSense control */
        States,       /* a game's save states, over its Details */
        WiiGuide,     /* how to hold the DualSense as each Wii controller */
        WiiSetup,     /* the Wii Remote setup: controller, hold, centre, the screen's corners */
        Welcome,      /* the first start: choose a theme */
        Achievements, /* a game's RetroAchievements, from its Details (Square) */
        TileArt,      /* a game's home screen tile art (its settings > Home screen) */
        Shots,        /* screenshots: every game's (Settings) or one game's (Details) */
        Controllers,  /* Controllers & Accessories: the DualSense, the DolphinBar, Wii Remotes 1-4 (ui_app_controllers.cpp) */
    };
    struct SettingRow
    {
        std::string section, label, help, key;
        std::vector<std::string> values;
        int *int_value = nullptr;
        bool *bool_value = nullptr;
        std::string *text_value = nullptr; /* a text setting chosen from a list (the border) */
        std::vector<std::string> text_choices; /* text_value's for each of values (else the border's) */
        std::vector<int> order;            /* when set: values[i] is the setting's order[i] */
        int toggle = -1;                   /* a switch: 1 on, 0 off; -1 not a switch */
        int rec = -1;                      /* its recommendation (rec_rows_) */
        std::string tag;                   /* a small label beside the switch: where it comes from */
        int setup = -1;                    /* kRowUseSetup: which setup */
        int min = 0;
        bool header = false;
        int action = 0;      /* look::RowAction */
        int folder = -1;     /* the folder a Remove row is for */
        bool rescan = false; /* changing it searches the library again */
        bool beta = false;   /* a BETA badge after the label */
        bool alpha = false;  /* with beta: the badge says ALPHA (3.1's frame generation) */
        int live = -1;       /* Controllers & Accessories: its value is what that device does now (0 DualSense, 1 DolphinBar, 2-5 Wii Remote 1-4) */
        bool subhead = false; /* 4.5: a heading inside a tab (label: its name), not a row you stop on */
        bool indent = false;  /* drawn a step in: under an opened row (the credits) */
        std::string group;    /* the part of the tab it's in (the section it had before 4.5) */
    };
    enum class DialogKind
    {
        Info,
        ResetAll,
        ResetGame,
        DeleteSave,
        CopySave,
        DeleteState,
        InstallUpdate,
        InstallVersion,
        CopyGame,
        DeleteWiiSave,
        Resume, /* quick resume: Resume (yes) or Start Over (the other button) */
        Reinitialize,
        SandboxNotice, /* OK, or Don't show again (the other button) */
        MoveData,      /* Porpoise's folder to another drive */
        UseFolder,     /* a Porpoise folder already on a drive: use it (move_target_) */
        JailbreakClosed, /* freeing Porpoise closed it last time: Stay in the sandbox, or Try again */
        ApplyChanges,    /* leaving Settings with changes not applied: Apply or Discard (Circle: keep editing) */
        RestartPorpoise, /* applied changes that need Porpoise started again: Restart now, or Later */
        RestartGame,     /* in a game: applied changes that need the game started again */
        CoversAgain,     /* every game's art downloaded again: Download, or Cancel */
        Wizard,          /* the first start's setup, one question a step (wizard_step_) */
        DeleteShot,      /* a screenshot (Screen::Shots) */
        Testing,         /* an alpha or beta build's notice, at its first start (look::kChannel) */
        NandImport,      /* Console > Wii System Data: import the backup found (nand_bin_) */
        NandRemove,      /* Console > Wii System Data: the imported Wii set aside, a fresh one in its place */
        SysUpdate,       /* Console > Wii System Update: for update_region_ */
    };
    struct Dialog
    {
        bool open = false;
        DialogKind kind = DialogKind::Info;
        std::string title, message, yes; /* yes empty: just OK */
        std::string no;                  /* the other button, when it isn't Cancel */
        Texture *picture = nullptr;      /* shown above the message (quick resume) */
        /* A checklist instead of a message (Check my setup): each line with a
         * tick, or a mark that something needs doing. */
        std::vector<std::pair<bool, std::string>> checks;
        bool danger = false;
        int choice = 0; /* 0 = cancel, 1 = yes */
        float anim = 0;
    };

    /* The on-screen keyboard (ui_app_keyboard.cpp): the account panel's and
     * the network share panel's. */
    struct Keyboard
    {
        int kr = 1, kc = 0; /* the key in focus */
        bool shift = false, symbols = false;
        bool numeric = false; /* the number pad (an address): digits, a dot, and letters a key away */
    };
    static constexpr int kKeyboardRows = 5;
    /* A frame of typing into text: true when OK (or Options, or Circle) ends it. */
    bool keyboard_update(Keyboard &kb, std::string &text, std::size_t limit, bool spaces, bool up, bool down,
                         bool left, bool right);
    /* The keyboard, its top at y; the panel's glass for its board. */
    void draw_keyboard(const Keyboard &kb, float y, const Glass &face);
    static float keyboard_height();
    void draw_keyboard_prompts(bool numeric = false);
    float keyboard_height_for(const Keyboard &kb) const;

    /* The RetroAchievements account panel and its keyboard. */
    struct AccountPanel
    {
        bool open = false;
        int row = 0;           /* signed out: 0 username, 1 password, 2 sign in */
        std::string user, pass;
        bool typing = false;   /* the keyboard is up */
        int field = 0;         /* what it types into: 0 username, 1 password */
        Keyboard kb;
        bool was_busy = false;
        float anim = 0;
    };
    AccountPanel acct_;

    /* A network share, added or changed (ui_app_netshare.cpp). */
    struct ShareJob; /* a connection test or a search for shared folders, on its own thread */
    static void *run_share_job(void *job);
    static void start_share_job(std::shared_ptr<ShareJob> job);
    void start_share_scan(); /* looks around the network for computers that share files */
    void start_share_find(); /* looks for the computer's shared folders */
    struct SharePanel
    {
        bool open = false;
        int index = -1;       /* the share changed, or -1 for a new one */
        netfs::Share share;   /* host, user, password and protocol; the folder is path */
        netfs::Share saved;   /* the share being changed, as saved (no password) */
        std::string path;     /* the shared folder (or export) and any folder inside it, as typed */
        std::string other_path; /* the path kept for the other type (SMB or NFS), for switching back */
        std::vector<std::string> other_found;
        int row = 0;          /* a ShareRow (ui_app_netshare.cpp) */
        int button = 0;       /* on the buttons: 0 test, 1 save, 2 remove */
        int card = 0;         /* on the network row: the computer in focus */
        bool typing = false;
        Keyboard kb;
        std::vector<std::string> found; /* the computer's shared folders, once looked for */
        std::string find_key;           /* what they were last looked for with (not again for the same) */
        std::shared_ptr<ShareJob> job;  /* a test or a search for shared folders */
        double job_started = 0; /* when it began (time_): given up on after a while */
        std::shared_ptr<ShareJob> scan; /* the look around the network */
        std::vector<netfs::Found> computers; /* what answered */
        bool scanned = false;
        std::string message;
        bool message_ok = false;
        float anim = 0;
        float slide = 0; /* the type switch's highlight: 0 SMB, 1 NFS */
        float cards_x = 0; /* the network row, scrolled */
    };
    SharePanel share_;
    void open_share(int index);
    void close_share();
    Action update_share(bool up, bool down, bool left, bool right);
    void draw_share();
    std::string shares_file() const { return data_dir_ + "/network-shares.txt"; }

    /* The DNS server for Wii games online, typed on the number pad
     * (ui_app_dns.cpp). Settings > System opens it. */
    struct DnsPanel
    {
        bool open = false;
        std::string text;
        Keyboard kb;
        std::string message;
        float anim = 0;
    };
    DnsPanel dns_;
    void open_dns();
    Action update_dns(bool up, bool down, bool left, bool right);
    void draw_dns();

    /* The Online hub (ui_app_online.cpp): Wii games online, every way in. */
    struct HubPanel
    {
        bool open = false;
        float anim = 0, slide = 0;
        int mode = 0;        /* the card: Custom Server, WiiLink WFC, Wiimmfi, WiiConnect24 */
        bool inside = false; /* in the card's page, on its rows */
        int row = 0;
        std::vector<std::pair<std::string, std::string>> servers; /* a name, its DNS server */
        int typing = 0;      /* 1 a server's name, 2 its address */
        int edit = -1;       /* the server being changed; -1 a new one */
        std::string name, text, message;
        Keyboard kb;
        std::shared_ptr<OnlineTest> test;
        std::vector<std::pair<std::string, std::string>> coded; /* games with a code file: title, ID */
    };
    HubPanel hub_;
    /* Console > Wii System Data: the backup found, and an import running. */
    std::string nand_bin_, nand_keys_;
    std::shared_ptr<porpoise::nand::Progress> nand_job_;
    void start_nand_import();
    bool update_nand_job(); /* true while it runs (the menus wait) */
    void draw_nand_job();
    void remove_nand(); /* DialogKind::NandRemove: User/Wii into User/Wii-removed/<date> */

    /* Console > GameCube Menu, Wii Menu and Wii Channels (ui_app_system.cpp):
     * a list to start a menu with a game in its drive (or none), or the
     * player's channels (WADs) to put in the Wii Menu. */
    struct SystemPanel
    {
        bool open = false;
        int mode = 0;          /* 1 the GameCube Menu, 2 the Wii Menu, 3 Wii Channels, 4 the update's region */
        int row = 0;
        float scroll = 0, anim = 0;
        std::vector<std::string> keys;   /* the games' library keys (row 0 is "No Disc" in modes 1 and 2) */
        std::vector<char> marked, installed; /* mode 3 */
        std::string gc_region; /* mode 1 with no disc: the BIOS's (USA or EUR) */
    };
    SystemPanel sys_;
    /* A game's Cheats and Patches > Add a Code (ui_app_code.cpp): a Gecko code
     * typed on a hex pad, into the player's own file for the game. */
    struct CodePanel
    {
        bool open = false;
        bool naming = false; /* the keyboard is up, for its name */
        std::string name, digits, message;
        int kr = 0, kc = 0;  /* the pad's key in focus */
        Keyboard kb;
        float anim = 0;
    };
    CodePanel code_;
    void open_code();
    bool save_code();
    Action update_code(bool up, bool down, bool left, bool right);
    void draw_code();
    Game sysmenu_game_;                  /* what a menu start launches */
    std::vector<std::string> wad_installs_; /* WADs the core installs before the next start */
    bool wii_menu_installed() const;     /* in Porpoise's Wii (an imported NAND, or installed) */
    const Game *wii_menu_wad() const;    /* the Wii Menu's own WAD in the library, if any */
    char wii_menu_region() const;        /* 'U', 'E', 'J', 'K', or 0 when not known */
    bool channel_installed(const Game &g) const;
    void open_system(int mode);
    Action update_system(bool up, bool down);
    void draw_system();
    Action start_menu(int menu, const Game *disc, const std::string &gc_region = "");
    /* Console > Wii System Update: its region (USA, EUR, JPN, KOR), the list
     * of titles being asked for, and the update the next start runs. */
    std::string update_region_, pending_update_;
    std::shared_ptr<porpoise::sysupdate::Job> update_job_;
    bool covers_resume_ = false; /* take_covers_resume */
    void ask_sys_update();      /* the confirmation, for update_region_ */
    void start_sys_update();    /* DialogKind::SysUpdate: the list asked for */
    bool update_sys_update(Action &out); /* true while it's asked for (the menus wait) */
    void draw_sys_update();
    std::string update_list_path() const { return data_dir_ + "/wii-system-update.xml"; }
    void open_hub();
    Action update_hub(bool up, bool down, bool left, bool right);
    void draw_hub();
    std::string hub_servers_path() const;
    void hub_save_servers();
    void hub_set(bool Settings::*field, bool value);
    void hub_use_server(const std::string &address);
    int hub_rows() const;
    std::vector<std::pair<std::string, std::string>> hub_coded_games() const;
    std::function<RaState()> ra_state_;
    /* The host's own checks for the diagnostic test (the jailbreak, the
     * folder, the search, the last game's speed): ok and what was found. */
    std::function<std::vector<std::pair<bool, std::string>>()> diagnostics_;
    void run_diagnostic();
    std::function<bool(const std::string &, const std::string &)> ra_login_;
    std::function<void()> ra_logout_;
    /* L1 changes tab when it's let go, unless Square came while it was held
     * (L1 + Square: the account panel). */
    bool l1_armed_ = false, l1_chord_ = false, l1_tap_ = false;
    Action update_account(bool up, bool down, bool left, bool right);
    void draw_account();

    /* RetroAchievements lists (ui_app_achievements.cpp). */
    AchievementSource ach_source_;
    AchievementSet ach_;        /* the set shown */
    std::string ach_for_;       /* its game's disc ID */
    bool ach_live_ = false;     /* from the running game */
    double ach_loaded_at_ = -1;
    int ach_focus_ = 0;
    float ach_scroll_ = 0;
    std::map<std::string, Texture *> ach_tex_; /* badge file -> texture */
    Screen ach_back_ = Screen::Details;
    std::string details_ach_for_; /* the Details game its achievements line is for */
    AchievementSet details_ach_;
    void load_achievements(const std::string &disc_id, bool live);
    Texture *badge_tex(const std::string &url);
    void free_badges();
    bool achievements_nav(bool up, bool down);
    void draw_badge(const std::string &url, bool unlocked, float x, float y, float size);
    void draw_achievement_list(float x, float y, float w, float h);
    void draw_achievement_card(float x, float y, float w, float h);
    void open_achievements(const Game &game);
    Action update_achievements_screen(bool up, bool down);
    void draw_achievements_screen();
    bool menu_has_achievements() const { return ach_live_ && ach_.valid(); }
    const AchievementSet &details_achievements(const Game &game);

    std::uint32_t pressed(std::uint32_t bits) const { return (held_ & ~prev_) & bits; }
    void sfx(Sound s) const
    {
        if (sound_)
            sound_(s);
    }
    bool nav(std::uint32_t bit, float &timer, double dt);
    Texture *cover_of(Game &g);
    Texture *picture(Texture *&tex, Texture *&old, bool &tried, std::string &wait,
                     std::string (Library::*where)(const Game &) const, const Game &g);
    static bool cover_loading(const Game &g) { return !g.cover && !g.cover_wait.empty(); }
    Texture *disc_of(Game &g);
    /* Memory Cards' other views (ui_app_cards.cpp). */
    struct SaveRef
    {
        int kind = 0; /* 0 Slot A, 1 Slot B, 2 the Wii */
        int index = 0;
    };
    struct SaveGroup
    {
        std::string key, title;
        Game *game = nullptr;
        std::vector<SaveRef> saves;
    };
    std::vector<SaveGroup> saves_by_game();
    void draw_card_blocks(double time);
    void draw_saves_by_game(double time);
    void draw_card_cubes(Card &card, int which, float x, float y, double time);
    void draw_card_classic(double time);
    void update_card_classic(bool left, bool right, bool up, bool down);
    static constexpr int kClassicCols = 4, kClassicRows = 3;
    int mc_classic_first_ = 0; /* Retro TV: the first row in view */
    void update_saves_by_game(bool up, bool down);
    int mc_game_sel_ = 0;
    float mc_game_scroll_ = 0;
    /* The library's views (ui_app_views.cpp). */
    void draw_cover_flow(double time);
    void draw_wheel(double time);
    void draw_disc_flow(double time);
    void draw_shelf(double time);
    void draw_box_view(double time);
    void draw_list_view(double time);
    void draw_stack(double time);
    void draw_helix(double time);
    void draw_glass_cube(float cx, float cy, float size, float yaw, float pitch, float roll, Color tint, Color rim,
                         Texture *front, float alpha, bool lit);
    void draw_disc(Game *game, float cx, float cy, float d, float spin, float yaw, float alpha, bool focused);
    /* load_art false: only art already loaded (the spines view's far cases). */
    void draw_box3d(Game &game, float cx, float cy, float w, float h, float depth, float yaw, float alpha,
                    bool load_art = true,
                    bool spine_only = false);
    void draw_spines(double time);
    void draw_spotlight(double time);
    void draw_spotlight_backdrop(); /* under the top bar */
    /* Recently Played: the last games played, in a dock along the bottom of
     * the library (Settings > Interface). Down goes to it. */
    std::vector<int> recent_games();
    void draw_dock(double time);
    bool dock_shown();
    bool dock_focus_ = false;
    int dock_sel_ = 0;
    float dock_lift_ = 0;
    /* Details' whole description (the back of the box), laid out once. */
    std::string whole_text_for_;
    std::vector<std::string> whole_text_lines_;
    float whole_text_size_ = 21, whole_text_line_ = 30;
    float dock_glide_ = 0; /* the chosen game's place, gliding (Floating's magnifier) */
    float dock_open_ = 0;  /* 0 resting, 1 chosen (Down) */
    void draw_dock_floating(const std::vector<int> &recent);
    void draw_dock_discs(const std::vector<int> &recent, double time);
    void draw_dock_bubble(float cx, float top, const std::string &title);
    void release_far_art(int keep);
    std::string game_meta(const Game &g) const;
    bool update_view_nav(bool left, bool right, bool up, bool down, double dt);
    static constexpr int kShelfCols = 6;
    float row_scroll_ = 0;  /* the shelf's first row, sliding */
    float view_yaw_ = 0;    /* the box view's turn (the right stick) */
    double view_idle_ = 0;  /* how long the right stick has been still */
    Texture *spine_of(Game &g);
    Texture *back_of(Game &g);
    void set_tab(int tab, int dir);
    void open_screen(Screen s);

    /* Settings (ui_app_settings.cpp) */
    void build_settings();
    void build_game_settings();
    void add_recommended_rows();
    /* 4.0: the Show All Settings switch first, and with it off only the
     * settings most players need. */
    void consolidate_rows(bool per_game);
    bool row_stop(int i) const;  /* a settings row you can stop on (not a heading) */
    void settle_settings_row(); /* onto one, after a rebuild */
    bool credits_open_ = false; /* About > Credits shown */
    void set_own_count();
    void add_cheat_rows();
    std::vector<Cheat> cheats_;
    std::deque<bool> cheat_on_; /* a deque: the rows point at its elements */
    std::string sys_dir_;
public:
    /* Dolphin's Sys folder, for the cheats it lists. */
    void set_sys_dir(const std::string &dir) { sys_dir_ = dir; }
private:
    void add_setup_rows(bool per_game);
    struct RecRow
    {
        enum Kind
        {
            AllPicks,
            Pick,
            DolphinFix,
        } kind = Pick;
        std::string key, value, off_value;
    };
    std::vector<RecRow> rec_rows_;
    bool rec_on(const RecRow &rec) const;
    void toggle_recommended(int index);
    /* A game's own value for a key, or (nullptr) back to the global one. */
    void set_game_key(const std::string &key, const std::string *value);
    void add_game_rows(Settings &target, bool per_game);
    /* Settings > Textures (ui_app_textures.cpp) and Online (ui_app_online.cpp). */
    void add_texture_rows(bool per_game);
    /* A game's Cheats and Patches as cards (ui_app_cheats_view.cpp). */
    void draw_cheat_cards(const std::vector<int> &section_rows, float px, float py, float pw, float ph);
    float cheat_cards_first_ = 0;
    void add_online_rows(Settings &target, bool per_game);
    std::string texture_root() const;
    void refresh_texture_packs();
    void poll_texture_packs();
    const Game *game_for_pack(const std::string &folder) const;
    std::vector<TexturePack> texture_packs_;
    bool texture_scanning_ = false;
    double texture_scan_time_ = -100;
    void change_setting(int dir);
    /* Settings you see change as you choose them (the look, borders, screen
     * filters, menu sounds): saved at once, with no Apply. */
    static bool applies_at_once(const std::string &key);
    void apply_at_once(const std::vector<std::string> &keys);
    bool live_changed_ = false;
    Action update_settings(bool up, bool down, bool left, bool right);
    Action activate_row(const SettingRow &row);
    void draw_settings();
    void open_game_settings(Game &g);
    void close_game_settings();
    int section_count() const;
    std::string section_name(int index) const;
    int first_row_of(const std::string &section) const;
    void open_browser(std::string path); /* by value: callers pass entries it replaces */
    void draw_browser();
    Action update_browser(bool up, bool down);
    std::string build_label() const;
    /* About > Report a bug: Porpoise's logs into <data>/reports/<time>, and a
     * copy on the first USB drive (usb: its folder there, "" when none).
     * Returns the report's folder, "" when it couldn't be written. */
    std::string save_report(std::string &usb);
public:
    /* Whether Porpoise was left in the app sandbox (porpoise_main). */
    void set_sandboxed(bool sandboxed) { sandboxed_ = sandboxed; }
    /* Whether PS5-ECAM started Porpoise (ecam_bridge.h): no rows for what PS5-ECAM does instead, its
     * updates, home screen tiles, cover downloads, menu sounds and the folder it keeps Porpoise's things in. */
    void set_ecam(bool ecam) { ecam_ = ecam; }
    void set_diagnostics(std::function<std::vector<std::pair<bool, std::string>>()> checks)
    {
        diagnostics_ = std::move(checks);
    }
    /* A search for games is running (Search for games now says so). */
    void set_searching(bool searching)
    {
        if (searching != searching_)
        {
            searching_ = searching;
            build_settings();
        }
    }
    /* Whether the library can be rebuilt now (a search ended): nothing on
     * screen holds one of its games. */
    bool library_free() const
    {
        return (screen_ == Screen::Main || screen_ == Screen::Sort || screen_ == Screen::Browse) && !dialog_.open &&
               !launch_;
    }
    /* The setup check: a title and what Porpoise can see, with what to do. */
    void show_setup_check(bool first_start);
    /* The first start (and after Reinitialize): a theme to begin with, then
     * the welcome and setup check (ui_app_welcome.cpp). */
    void start_welcome(bool upgrade = false); /* upgrade: 4.5's welcome, a look and nothing else */
    /* The game's sound as it is running: pulled from Dolphin's mixer, or Classic. */
    void set_sound_pulled(bool pulled);
    /* A first start found a Porpoise folder on a drive (path, the drive's
     * English name): asks to use it; otherwise the welcome goes on. */
    void offer_found_folder(const std::string &path, const std::string &place, bool handmade = false);
    /* After Reinitialize: the menus as at the first start. */
    void restart_fresh();
private:
    bool sandboxed_ = false;
    bool ecam_ = false;
    bool searching_ = false;
    std::vector<std::string> version_tags_, version_labels_;
    std::vector<std::size_t> version_sizes_;
    std::vector<int> version_builds_;
    int version_pick_ = 0;
    long long change_count() const;

    /* Button mapping (ui_app_controls.cpp) */
    void open_mapping();
    void open_mapping_in_game();
    void begin_mapping();
    void use_preset(int preset);
    void start_preset_from(int layout);
    void draw_control(int control, float right, float cy, bool on, float size);
    void draw_gc_chip(int gc, float cx, float cy, float h, bool on = false);
    float gc_chip_width(int gc, float h);
    void close_mapping();
    Action update_mapping(bool up, bool down, bool left, bool right);
    void draw_mapping(double time);
    /* The same screen for a Wii controller's buttons (map_kind_ 1..4). */
    Action update_wii_mapping(bool up, bool down, bool left, bool right);
    void draw_wii_mapping(double time);
    /* The same screen for the GBA's buttons (map_kind_ 5): one set, every GameCube game. */
    Action update_gba_mapping(bool up, bool down, bool left, bool right);
    void draw_gba_mapping(double time);
    void draw_gba_pad(const porpoise::pad::GbaConfig &gba, float bx, float by, float art_h, float alpha);
    void gba_chip(int input, float cx, float cy, float ch, float alpha);
    void draw_map_tabs();
    bool switch_map_kind(); /* L1 / R1: GameCube, then each Wii controller */
    void assign_control(int gc_input, int control);
    void save_mapping(bool layout_changed, const char *key = "button_layout");
    void use_wii_slot(int slot);
    void draw_keycap(float right, float cy, const std::string &label, bool on, float height = 40);

    /* Dialogs */
    void open_dialog(DialogKind kind, const std::string &title, const std::string &message,
                     const std::string &yes, bool danger = false);
    Action update_dialog(bool left, bool right);
    Action confirm_dialog(DialogKind kind);
    void draw_dialog();

    /* Memory cards */
    void scan_memory_cards();
    void finish_card_scan();
    /* Entering Memory Cards reads both cards and the Wii saves on a worker
     * thread, so the tab slides in without a hitch (ui_app.cpp). */
    std::shared_ptr<SaveScan> save_scan_;
    void start_save_scan();
    void take_save_scan();
    void ensure_saves(bool wii);
    bool saves_loading() const { return save_scan_ != nullptr; }
    void move_memcard(int dx, int dy);
    void free_card_textures();
    Save *focused_save();
    void ask_delete_save();
    void ask_copy_save();
    void export_save_to_usb();
    std::string copy_target(const Save &s) const;

    void draw_top_bar();
    float draw_key_pair(float x, float cy, Glyph which, bool measure_only = false);
    void draw_prompts(const std::vector<std::pair<Glyph, std::string>> &left,
                      const std::vector<std::pair<Glyph, std::string>> &right, const std::string &center);
    void draw_library(double time);
    void draw_tile(Game *g, float cx, float cy, float w, float h, float yaw, float alpha, bool focused,
                   bool reflection);
    void draw_details(double time);
    void draw_sort();
    void draw_memory_cards(double time);
    void draw_card(Card &card, int which, float x, float y, double time);
    void draw_brand(float cy);
    float draw_wordmark(float x, float cy, float h, float alpha, int style);
    Texture *wordmark_ = nullptr, *wordmark_mark_ = nullptr;
    bool wordmark_tried_ = false;
    /* The boot: where the look drew its wordmark (x, cy, h) and how, this frame,
     * for it to land on; the welcome's dolphin (cx, cy, width) the same. Hidden
     * (brand_alpha) until the boot's wordmark is there. */
    float brand_box_[3] = {0, 0, 0};
    int brand_style_ = 0;
    bool brand_seen_ = false;
    float welcome_mark_[3] = {0, 0, 0};
    int welcome_mark_style_ = 0;
    bool welcome_mark_seen_ = false;
    float brand_alpha() const;
    void draw_welcome_mark(float cx, float cy, float width, bool ink, float alpha);
    float boot_time() const;
    bool boot_on_ = false, boot_legacy_ = false, boot_sound_ = false, boot_music_ = false;
    double boot_t0_ = 0, preview_boot_t_ = -1;
    int boot_events_ = 0;
    std::uint32_t boot_prev_ = 0;
    static constexpr int kTraceSteps = 12;   /* assets/brand/boot/trace-*.png (tools/make-wordmark.py) */
    static constexpr float kTraceFull = 0.6f; /* ...the last one this far through drawing an outline */
    Texture *boot_glow_ = nullptr, *boot_big_ = nullptr, *boot_trace_[kTraceSteps] = {};
    bool boot_started_ = false;
    /* The Porpoise mark, centred: the textured logo when it is there. */
    bool draw_mark(float cx, float cy, float width, Color tint);
    Texture *save_icon(Save &s);
    Texture *save_banner(Save &s);
    void draw_empty();

    /* The Revolution look's home screen (ui_app_home.cpp): a grid of tiles,
     * twelve to a page, pointed at with the controller's motion. */
    bool revolution() const { return settings_ && settings_->ui_theme == 1; }
    bool starcube() const { return settings_ && settings_->ui_theme == int(ThemeId::StarCube); }
    /* 4.1: the new Porpoise (liquid glass, lit by the game you're on). */
    bool glass() const { return settings_ && settings_->ui_theme == int(ThemeId::Glass); }
    /* The library's view as drawn now: Revolution's Library View keeps its choice;
     * Porpoise is a cover flow or rows (the shelf's moves); Classic a cover flow. */
    int lib_view_now() const
    {
        if (!settings_)
            return 0;
        if (glass())
            return settings_->glass_layout == 1 ? 3 : 0;
        if (revolution())
            return std::clamp(settings_->lib_view, 0, 9);
        return 0;
    }
    int mc_view() const { return starcube() ? 3 : settings_->mc_view; } /* Star Cube's saves are cubes */
    bool home_showing() const /* the grid of tiles (the Revolution look's home, laid out as a grid) */
    {
        return revolution() && settings_->ui_layout == 0 && tab_ == Tab::Library &&
               (screen_ == Screen::Main || screen_ == Screen::Sort);
    }
    static constexpr int kRevTab0 = 16;  /* focus targets: the top bar's tabs */
    static constexpr int kHomeGrow = 19; /* tiles, buttons, arrows, tabs */
    static constexpr int kRdGrow = 22;   /* the opened tile's targets */
    struct HomeItem
    {
        int game = -1;       /* index into the library */
        bool resume = false; /* the first tile: the game played last */
    };
    std::vector<HomeItem> home_items() const;
    void home_pad_sync();
    int home_hit(float x, float y, int items, int pages) const;
    void home_tile_rect(int slot, float scroll_dx, float &x, float &y, float &w, float &h) const;
    void update_home(bool left, bool right, bool up, bool down, bool &play, bool &details, bool &fav, double dt);
    void draw_home(double time);
    /* The Revolution look's shared pieces (ui_app_revolution.cpp). */
    void draw_room();
    void draw_lines_in(float x, float y, float w, float h, float inset);
    void draw_rev_top_bar(bool clock);
    void draw_seg_clock(float cx, float cy, float h);
    std::string short_date() const;
    void draw_hand(float x, float y, float roll, int player, bool pointing);
    /* Opening a tile: it grows to fill the screen, the opened tile comes up
     * out of it (and back down into it on the way out). */
    bool home_rect_of(int game, float rect[4]);
    void start_zoom(int dir);
    void draw_zoom(double time);
    bool zoom_shows_home() const;
    bool pointer_screen() const;
    void rev_pointer_step();
    void rev_pointer_rest();
    void rev_landed();
    int rev_tab_hit(float px, float py) const;
    void draw_rev_pointer();
    std::vector<std::pair<int, std::string>> rev_chips(const Game &game) const;
    void rev_details_rects(const Game &game, std::vector<std::pair<int, std::array<float, 4>>> &out);
    Action update_rev_details(bool left, bool right, bool up, bool down, double dt);
    void draw_rev_picture(Game &game, float x, float y, float w, float h, float fade);
    void draw_rev_details(double time);
    /* A Wii disc's own tile (small) or banner (big), read from the disc
     * (ui_app_banner.cpp); false when there is none (yet). */
    bool has_banner(const Game &game) const;
    bool draw_banner(Game &game, float x, float y, float w, float h, double time, float fade, bool big);
    std::string clock_text() const;
    float ts(float size) const
    {
        if (!settings_)
            return size;
        return size * (settings_->text_size == 3 ? 0.88f : 1.0f + 0.15f * float(settings_->text_size)); /* 3: Smaller */
    }
    /* The theme in use, and the renderer set for it (each frame). */
    const Theme &th() const { return theme(settings_ ? settings_->ui_theme : 0); }
    void apply_look();
    void draw_theme_overlay();
    int theme_seen_ = -1;

    void (*sound_)(Sound) = nullptr;
    void (*jingle_)(const std::int16_t *, std::size_t) = nullptr;
    /* Wii discs' tiles and banners, played a frame at a time by
     * porpoise_banner into streamed textures (ui_app_banner.cpp). */
    struct BannerTex
    {
        Texture *tex = nullptr;
        int w = 0, h = 0;
        std::uint64_t serial = 0;
        double used = 0;
    };
    std::map<std::string, BannerTex> banner_tex_; /* by "<id>/icon" or "<id>/banner" */
    std::vector<std::uint8_t> banner_px_;
    std::set<std::string> no_banner_, banner_checked_;
    std::string jingle_for_;     /* the opened tile whose jingle is due */
    double rd_open_time_ = 0;    /* when the opened tile opened (its banner starts then) */
    std::string banner_lang_;
    void pump_banners();
    void opened_tile(const Game &game); /* the banner starts, its jingle plays */
    BannerTex *banner_texture(const Game &game, bool big);
    void draw_frames(const BannerTex &b, float x, float y, float w, float h, float fade, float radius, bool fill);
    float intro_fade_ = 1, intro_dy_ = 0;
    float rail_glide_ = -1, row_glide_ = -1; /* Settings' highlights, gliding */
    float set_scroll_ = 0, set_scroll_to_ = 0; /* a tab's rows, scrolled (4.5's glass settings) */
    std::string set_scroll_tab_;
    float rail_scroll_ = -1; /* Settings' section list: the first row shown (it scrolls when they don't all fit) */
    double glide_time_ = 0;
    /* The entrance in steps: what starts later (delay, of the whole) comes
     * in after the top bar. */
    void intro_stage(float delay)
    {
        const float f = std::clamp((intro_fade_ - delay) / (1.0f - delay), 0.0f, 1.0f);
        g_->set_intro(f, intro_dy_ > 0 ? 28.0f * (1.0f - f) : 0.0f);
    }
    Gfx *g_ = nullptr;
    Library *lib_ = nullptr;
    Settings *settings_ = nullptr;
    std::string settings_path_, options_path_, saves_dir_, data_dir_;
    std::string details_tex_for_, details_tex_; /* the Details game's texture pack folder, if any */
    std::string details_ws_for_; /* the game Details' Widescreen fact is for */
    int details_ws_ = 0;         /* its ui_widescreen Kind */

    Tab tab_ = Tab::Library;
    Screen screen_ = Screen::Main;
    int selected_ = 0;
    float scroll_ = 0;     /* animated selection */
    float lift_ = 0;       /* focus lift, 0..1 */
    int details_row_ = 0;
    /* The box on the details page: it flips in from the library, turns with
     * the right stick, and Triangle shows its back. */
    float flip_anim_ = 0;  /* 1 -> 0 as it arrives */
    float box_yaw_ = 0;    /* animated */
    bool box_back_ = false;
    float right_x_ = 0;
    float layer_dx_ = 0, layer_dy_ = 0, layer_fade_ = 1; /* the screen's motion, this frame */
    bool details_custom_ = false; /* the game has its own settings */
    int sort_row_ = 0;         /* Sort & filter: 0 sort, 1 show, 2 covers and info */
    void keep_selection(const std::string &key); /* after the order changed */
    void look_changed(int was);                 /* Settings > Interface > Look */
    void draw_revolution(double time);
    /* Star Cube (ui_app_starcube.cpp): its home cube and its pages. */
    void sc_tick(double dt);
    void sc_open(int face, bool zoom);
    void sc_go_home();
    void sc_open_details();
    int sc_face_of_tab() const;
    std::vector<int> sc_recent_games() const;
    Action update_starcube(bool left, bool right, bool up, bool down, bool &handled);
    void draw_starcube(double time);
    /* The Porpoise look (ui_app_glass.cpp): cases lit by the game you're on, the
     * filters along the top, the island, the menu that grows into Settings. */
    Action update_glass(bool left, bool right, bool up, bool down, double dt, bool &handled);
    void glass_tick(double dt);
    void draw_glass(double time);
    void draw_glass_flow(double time, float alpha);
    void draw_glass_rows(double time, float alpha);
    void draw_glass_title(Game &game, float cy, float alpha);
    void draw_glass_details(double time, float alpha);
    void draw_glass_filters(float alpha);
    void draw_glass_menu(float alpha);
    void draw_glass_settings(float alpha);
    void draw_glass_states(double time);
    void draw_glass_launch(double time, float k, float bg, float rise, float words);
    bool gl_launching_ = false; /* Play pressed: the library parts for the launch screen */
    float gl_exit_ = 0;         /* how far it has parted, 0..1 */
    std::string glass_row_value(const SettingRow &r, int *index = nullptr) const;
    std::string glass_section_value(const std::string &section) const;
    void draw_glass_brand(float alpha);
    void draw_glass_prompts(const std::vector<std::pair<Glyph, std::string>> &items, float alpha, bool over_dialog = false);
    float draw_platform(const Game &game, float x, float cy, float h, float alpha); /* its icon; returns the width */
    void glass_open_menu();
    void glass_switch_filter(); /* the filter L1 / R1 asked for, now (gl_fpend_) */
    void glass_filters(std::vector<int> &shows) const; /* the filters there are games for */
    struct GlassItem
    {
        int kind = 0; /* 1 Sort By, 2 Get Covers, 3 Memory Cards, 4 a heading, 5 a Settings tab, 6 Show, 7 Full Settings, 8 Favorite */
        int section = -1;
        std::string label, value;
    };
    std::vector<GlassItem> glass_items();
    bool gl_menu_ = false;
    int gl_row_ = 0;
    float gl_menu_first_ = 0;
    float gl_hi_ = -1;                              /* the menu's highlight, gliding */
    float gl_slab_[4] = {1960, 28, 612, 1024};      /* the menu's glass as it is: x y w h */
    float gl_slab_a_ = 0;
    float gl_slab_v_ = 0;      /* its left edge's speed (a spring: it eases in and out) */
    bool gl_menu_seen_ = false; /* the glass has the menu on it (it was opened, and hasn't left the screen since) */
    /* The menu dragged in (or out) along the touch pad: how open it is, 0..1,
     * following the finger; let go, it opens or closes. */
    bool gl_dragging_ = false;
    int gl_drag_start_ = 0;  /* +1 a drag in from the right began, -1 one out (update_glass takes it) */
    bool gl_drag_let_go_ = false;
    bool gl_menu_drag_in_ = true; /* the drag is opening it (else closing) */
    float gl_drag_ = 0, gl_drag_speed_ = 0;
    /* L1 / R1: the filter changes as the cases slide away and back. */
    int gl_fpend_ = -1, gl_fdir_ = 1;
    float gl_fout_ = 0, gl_fin_ = 0;
    int gl_preload_ = 0;
    float gl_back_ = 0; /* Details closing: 1 -> 0 as the case flies back into the row */
    float gl_part_ = 0; /* Details opening: the library's other cases fading apart (draw_glass_flow / rows) */
    double gl_heart_at_ = -10; /* when a favourite was set or cleared (Details' heart) */ /* the next game whose cover is fetched ahead, unseen */
    float gl_amb_[6] = {0.10f, 0.30f, 0.60f, 0.30f, 0.20f, 0.60f}; /* the room's colours, eased */
    float gl_turn_ = 0;                            /* Details: the right stick's turn of the case */
    double gl_turn_idle_ = 0;
    float gl_line_x_ = -1, gl_line_w_ = 0;          /* the filter's underline, gliding */
    float gl_line_tx_ = -1, gl_line_tw_ = 0;        /* where it's going */
    float gl_hi_to_ = -1, gl_first_to_ = 0;         /* the menu's highlight and scroll, where they're going */
    double gl_drawn_at_ = -1;                       /* the last frame drawn: after a pause, things snap */
    bool gl_snap_ = false;
    float gl_page_ = 0;                            /* Settings or Memory Cards in the glass: 0..1 */
    Texture *gl_wii_ = nullptr, *gl_gc_ = nullptr;
    bool gl_icons_tried_ = false;
    /* assets/ui/controllers.png (tools/make-controller-icons.py): the
     * controllers' pictures, then a heart, filled and outlined. */
    Texture *gl_ctl_ = nullptr, *gl_dolphin_ = nullptr;
    bool gl_ctl_tried_ = false;
    void ctl_art(int which, float cx, float cy, float size, Color c);
    float battery_icon(float right, float cy, int level, Color c); /* level 0..5, 6 charging; its width */
    /* The island, top right: what's connected and the time. A swipe down the
     * touch pad opens it, a swipe up closes it; news drops down out of it. */
    void island_tick(double dt);
    void draw_island(int style); /* 0 Porpoise, 1 Classic */
    void camera_icon(float cx, float cy, float size, Color ink);
    /* A piece of the Porpoise look's glass: bright is the chosen button's white. */
    void glass_pill(float x, float y, float w, float h, float r, float alpha, bool bright = false);
    /* 4.5: the side menu, the island and the in-game menu in every theme (ui_app_glass.cpp). */
    void set_skin();
    void menu_haptics(); /* the ticks under the finger (porpoise::pad::menu_tick) */
    int hap_sel_ = -1, hap_row_ = -1, hap_set_ = -1, hap_rail_ = -1, hap_drag_ = -1, hap_isl_ = -1;                                                    /* their colors: the theme's */
    void skin_select(float x, float y, float w, float h, float r, float a); /* a chosen row */
    void open_menu_tab(int tab);
    Action update_side_menu(bool left, bool right, bool up, bool down, bool &handled);
    Action update_menu_rows(bool left, bool right, bool up, bool down);
    void draw_side_menu();
    /* How draw_box3d draws a case for the Porpoise look: a Wii game's in white
     * plastic, a little darker off to the side, its reflection on the floor. */
    float box_bright_ = 1, box_reflect_ = 0;
    bool box_white_wii_ = false;
    bool isl_open_ = false;
    /* The island's shortcuts (Oct 24): Memory Cards, Settings, Online,
     * Screenshots. The finger on the touch pad picks one (where it is across
     * the pad), a click of the pad opens it. -1: none picked. */
    int isl_pick_ = -1;
    Texture *isl_sc_tex_ = nullptr;
    bool isl_sc_tried_ = false;
    void island_shortcut(int which);
    /* The Move's pointer in Porpoise's own menus (Oct 24): the in-game menu,
     * and the Porpoise look's library and a game's page (there, the Move's
     * Start held a second turns it on and off). Point, and pull T to press;
     * hold T and sweep to go along. ui_app_movepointer.cpp */
    struct MpHit
    {
        float x, y, w, h;
        int id;
    };
    enum : int
    {
        kMpGame = 1000,    /* + the game's place in the library */
        kMpDetails = 2000, /* + a game page's button */
        kMpTab = 3000,     /* + an in-game menu's tab */
        kMpRow = 4000,     /* + its row */
    };
    std::vector<MpHit> mp_hits_, mp_next_; /* drawn last frame; being drawn */
    bool mp_on_ = false;                   /* the library's: turned on */
    bool mp_live_ = false;                 /* pointing, this frame */
    float mp_x_ = 960, mp_y_ = 540;
    int mp_hover_ = -1, mp_hover_was_ = -1, mp_press_on_ = -1;
    bool mp_t_ = false, mp_dragged_ = false;
    float mp_t_x_ = 0, mp_t_y_ = 0;
    unsigned mp_holds_ = 0;
    bool mp_holds_seen_ = false;
    /* This frame: T let go over mp_click_ (a hit's id, -1 none); the pointer
     * moved onto mp_moved_to_; steps swept (one given a frame); Start held. */
    int mp_click_ = -1, mp_moved_to_ = -1, mp_steps_ = 0;
    bool mp_hold_ = false;
    void mp_hit(float x, float y, float w, float h, int id);
    void mp_frame(std::uint32_t &held, bool active, bool vertical, float step_px);
    int mp_take_step();
    float isl_w_ = 0, isl_h_ = 0, isl_tw_ = 300, isl_th_ = 64; /* as it is, and as it's going */
    bool isl_touch_ = false, isl_swiped_ = false;
    /* The island dragged open (or shut) along the touch pad: how open, 0..1, on the finger. */
    bool isl_dragging_ = false;
    float isl_drag_ = 0, isl_drag_base_ = 0, isl_drag_speed_ = 0;
    int isl_y0_ = 0, isl_x0_ = 0;
    bool gl_ach_ = false;     /* a game's page: its achievements' list shown */
    float gl_ach_k_ = 0, gl_ach_first_ = 0, gl_ach_first_to_ = 0;
    int gl_ach_row_ = 0;
    double gl_swipe_at_ = -1;
    double isl_t0_ = 0;
    double isl_drawn_at_ = -1; /* drawn again after a pause: it's the size it's going to */
    Action update_welcome(bool up, bool down, bool left, bool right, double dt);
    void draw_welcome(double time);
    void welcome_card(int which, float cx, float cy, float w, float h, float lift, float alpha, bool on, double time);
    int welcome_pick_ = 0; /* a card, or 3: Porpoise Legacy, under them */
    bool welcome_upgrade_ = false;
    float welcome_legacy_ = 0; /* the Legacy line chosen, gliding */
    float welcome_time_ = 0, welcome_glide_ = 0;
    void draw_sc_home(double time, float zoom);
    void draw_sc_drift(double time, float alpha, float rush = 0);
    void draw_sc_header(const std::string &title, double time);
    void draw_sc_page(float x, float y, float w, float h);
    void draw_sc_games(double time);
    void draw_sc_details(double time);
    void draw_sc_calendar(double time);
    bool sc_home_ = true;      /* the cube shows (else one of its pages) */
    int sc_face_ = 0;          /* the edge it's turned to: Games, Calendar, Memory Cards, Settings */
    bool sc_calendar_ = false; /* the Calendar page is open */
    float sc_zoom_ = 0;        /* 0 the cube .. 1 into a page */
    float sc_yaw_ = 0, sc_pitch_ = 0.42f;
    float sc_glow_[4] = {1, 0, 0, 0};
    int sc_first_row_ = 0;     /* the games page's top row */
    float sc_rows_ = 0;
    int sc_recent_ = 0;        /* the calendar's chosen recent game */
    int sc_month_ = 0;         /* months from this one */
    float sc_vyaw_ = 0, sc_vpitch_ = 0; /* the cube's turn, on a spring */
    float sc_pulse_ = 0;       /* a new edge chosen: the labels and caption pop */
    float sc_page_time_ = 9;   /* since a page opened: its pieces arrive in turn */
    std::string library_count() const;          /* "12 games", "5 Wii games" */
    std::string library_note() const;           /* covers being fetched, or a flash */

    /* Settings: the rail of sections, then the rows of one. */
    std::vector<SettingRow> rows_;
    bool on_rail_ = true;
    int rail_ = 0;         /* section under focus on the rail */
    int settings_row_ = 1; /* row under focus once inside a section */
    /* A game's own settings: the global ones with its changes on top. */
    Settings game_;
    std::vector<std::string> game_keys_;
    /* Settings wait for Apply. The rows change a copy (draft_ for everything,
     * game_ for one game, menu_draft_ in a game); base_ / game_base_ /
     * menu_base_ are what was in effect, so the rows that differ are marked. */
    Settings draft_, base_;
    Settings game_base_;
    std::vector<std::string> game_keys_base_;
    Settings menu_draft_, menu_base_;
    std::vector<std::string> menu_changes_; /* applied in-game changes for the host, in turn */
    bool rescan_after_apply_ = false;
    /* The first start's setup, after the look: the sandbox (when Porpoise is
     * in it), finding games, covers, the buttons; then the setup check. */
    int wizard_step_ = -1;
    void wizard_next();
    bool wizard_rescan_ = false;
    std::vector<std::string> covers_again_;
    std::string applied_note_; /* "Settings applied", for a moment in the help line */
    double applied_note_time_ = -100;
    int leave_tab_ = -1, leave_dir_ = 0; /* where Settings was going when Apply or Discard was asked */
    bool leave_game_settings_ = false;
    bool leave_menu_ = false; /* in a game: the menu closes after Apply or Discard */
    std::vector<std::string> pending_keys() const;
    std::vector<std::string> menu_pending_keys() const;
    bool row_pending(const SettingRow &row, const std::vector<std::string> &pending) const;
    Action apply_pending();
    void discard_pending();
    void apply_menu_pending();
    void apply_menu_keys(const std::vector<std::string> &keys, bool quiet);
    /* Asks Apply / Discard before leaving Settings; false when nothing waits. */
    bool ask_before_leaving();
    void finish_leaving();
    Game *game_for_ = nullptr;

    Card card_a_, card_b_;
    /* The Wii saves (ui_app_wiisaves.cpp). */
    std::vector<WiiSave> wii_saves_;
    bool wii_scanned_ = false;
    int wii_sel_ = 0;
    float wii_scroll_ = 0;
    void scan_wii_saves();
    void finish_wii_scan();
    void free_wii_textures();
    Texture *wii_save_banner(WiiSave &s);
    Texture *wii_save_icon(WiiSave &s);
    void update_wii_saves(bool left, bool right, bool up, bool down);
    void draw_wii_saves(double time);
    void forget_banners();
    bool cards_scanned_ = false;
    int mc_card_ = 0;             /* 0 = A, 1 = B */
    int mc_sel_[2] = {0, 0};      /* save under focus on each card */
    float mc_scroll_[2] = {0, 0}; /* first visible row */
    std::string mc_focus_code_;   /* focus this game's save on the next scan */
    float mc_lift_ = 0;

    /* In-game menu */
    Game *menu_game_ = nullptr;
    Settings *menu_play_ = nullptr;
    int menu_row_ = 0;
    float menu_anim_ = 0;
    bool menu_closing_ = false;
    int menu_answer_ = 0;
    bool menu_restart_armed_ = false; /* Start over asked once */
    /* A question over the in-game menu: 1 Apply or Discard before it closes
     * (menu_prompt_answer_ is how it was closing), 2 start the game over for
     * applied changes that need it. menu_prompt_choice_: 1 the first button. */
    int menu_prompt_ = 0, menu_prompt_choice_ = 1, menu_prompt_answer_ = 1;
    bool menu_relaunch_ = false;   /* the game left to start afresh */
    bool menu_shot_ = false;       /* take_menu_shot */
    std::string menu_change_;
    int menu_tab_ = 0;   /* Game, Video, Graphics, Audio, Controls, Patches (and Achievements) */
    int menu_slot_ = 0;  /* the save-state slot under focus */
    int menu_ff_ = 0;    /* fast forward: off, 2x, 4x */
    int menu_setup_ = 0; /* the setup under focus in the Graphics tab */
    MenuRequest menu_busy_;           /* the save or load under way */
    bool menu_busy_handed_ = false;   /* given to the host */
    int menu_busy_frames_ = 0;        /* frames drawn showing it */
    double menu_busy_start_ = 0;
    int menu_slots_mode_ = 0;         /* 0 the list, 1 choosing where to save, 2 what to load */
    bool menu_confirm_ = false;       /* the next press replaces a used slot */
    std::string menu_note_;
    double menu_note_time_ = -10;
    Texture *menu_slot_tex_[3] = {nullptr, nullptr, nullptr};
    long long menu_slot_time_[3] = {0, 0, 0};
    bool menu_slot_used_[3] = {false, false, false};
    std::vector<porpoise::borders::Border> menu_borders_;
    /* The Patches tab: the game's codes (ui_cheats) and each one's switch as
     * the game has it; changes take effect the next time it starts. */
    std::vector<Cheat> menu_cheats_;
    std::vector<char> menu_cheat_on_;
    int menu_wide_open_ = -2; /* Widescreen as this run started (-2: not yet): its state shows beside it */
    Texture *lines_art_ = nullptr; /* the controller with lines, for the Controls tab */
    bool lines_art_tried_ = false;
    void load_slots(const Game *game); /* menu_slot_* for this game's save states */
    void menu_free_slots();
    /* Details > Save states (ui_app_states.cpp) */
    void open_states(Game *game);
    Action update_states(bool left, bool right);
    void draw_states();
    void count_states(const Game *game);
    Game *states_game_ = nullptr;
    int states_sel_ = 0;
    int details_states_ = 0; /* how many slots the game in Details has */
    int details_shots_ = 0;  /* and how many screenshots */
    /* Screenshots (ui_app_shots.cpp). */
    struct ShotItem
    {
        std::string path, thumb, key, title, wait;
        long long time = 0;
        Texture *tex = nullptr;
        bool tried = false;
    };
    std::vector<ShotItem> find_shots(const std::string &game_key) const; /* "" every game's; newest first */
    int count_shots(const Game &game) const;
    void open_shots(const std::string &game_key, Screen back_to);
    /* Controllers & Accessories (ui_app_controllers.cpp). */
    void open_controllers(int select);
    Action update_controllers(bool up, bool down, bool left, bool right);
    void draw_controllers(double time);
    std::string controller_row_value(int which, std::string *help) const;
    void refresh_controller_rows();
    /* Which of Controllers & Accessories' devices are plugged in, as the
     * settings section was last built with them (it's built again when that
     * changes): one letter each. */
    std::string controllers_present() const;
    std::string ctl_rows_present_;
    int ctl_sel_ = 1;
    Screen ctl_back_ = Screen::Main;
    std::string ctl_diag_logged_;
    int ctl_spk_slot_ = 0, ctl_spk_tone_ = 0; /* the speaker test: which Remote, which signal */
    Action update_shots(bool left, bool right, bool up, bool down);
    void draw_shots(double time);
    void delete_shot();
    void free_shots();
    std::vector<ShotItem> shots_;
    std::string shots_key_;
    Screen shots_back_ = Screen::Main;
    int shot_sel_ = 0, shot_first_ = 0;
    bool shot_view_ = false;
    Texture *shot_full_ = nullptr;
    int shots_total_ = -1;      /* Settings > Screenshots' count: -1 counted again when asked */
    std::string bios_found_;    /* About's GameCube BIOS line, found when Settings opens */
    bool segaboot_found_ = false; /* the Triforce's SegaBoot in bios/ (About) */
    bool bios_looked_ = false;
    std::string shot_full_for_, shot_full_wait_;
    std::string launch_state_; /* the state to start the launched game from */
    void draw_controller_lines(float x, float y, float w, const porpoise::pad::Mapping &m);
    /* A Wii game's controls: the DualSense as held, each Wii button on its control.
     * live_pose: how the first (second) controller is held right now, or -1. */
    void draw_hub_buttons(float x, float y, float w, float h, float alpha); /* the in-game menu's Controls tab: the hub's Buttons */
    void draw_wii_controls(float x, float y, float w, float h, const porpoise::pad::WiiConfig &wii, float alpha,
                           int live_pose = -1, int live_pose_second = -1);
    std::pair<float, float> draw_wii_pad(const porpoise::pad::WiiLayout &lay, int pose, float bx, float by,
                                         float art_h, float alpha);
    void wii_chip(int input, float cx, float cy, float ch, float alpha);
    float draw_wii_list(const porpoise::pad::WiiLayout &lay, float lx, float ly, float lw, int columns, float alpha);
    /* Settings > Wii Remote > How to hold it. */
    void open_wii_guide();
    void open_wii_setup_in_game(bool recalibrate_only);
    Action update_wii_guide(bool left, bool right);
    void draw_wii_guide(double time);
    /* The Wii Remote setup (before a Wii game, from Settings, or from the
     * pause menu). */
    /* quick: Options (the controller's start) - straight in, past the Wii controller setup. */
    Action start_game(Game *g, const std::string &state, bool resume_asked = false, bool quick = false);
    Game *resume_game_ = nullptr; /* the game the quick-resume question is about */
    void close_dialog();
    void draw_check_mark(float cx, float cy, float size, bool ok);
    void open_wii_setup(Game *g, bool launch, const std::string &state);
    Action update_wii_setup(bool up, bool down, bool left, bool right);
    void draw_wii_setup(double time);
    void draw_real_remote_setup(double time); /* ui_app_controllers.cpp */
    void wii_setup_apply_pad();
    void close_wii_setup();
    Settings &ws_settings();
    /* everywhere: every game's whatever the hub's scope (the room's calibration,
     * the ball's color, what the player holds). */
    void ws_store(const char *key, int value, bool everywhere = false);
    enum WiiStep
    {
        kWsController,
        kWsHold,
        kWsScreen, /* advanced: corners, or size and distance */
        kWsCentre,
        kWsTopLeft,
        kWsBottomRight,
        kWsTry,  /* Test & Tune: target practice, the swing meter, feel, speed and swing strength */
        kWsTune, /* Save as Template: a slot and a name */
        kWsPoints, /* Calibrate the pointer: the middle and the four corners, each taken when held still */
        kWsReady,  /* before the five points: what's coming, and Ready */
    };
    int ws_step_ = kWsController;
    int ws_controller_ = 0;
    bool ws_move_ = false;        /* the PlayStation Move is the Remote (wii_move) */
    bool ws_nav_ = false;         /* with it, the Nunchuk a Navigation controller (wii_nav, alpha) */
    int ws_point_ = 0;            /* kWsPoints: which of the five */
    int ws_color_ = 0;            /* the Move's ball color row: the color in focus, 8 ignore lights, 9 forget them */
    float ws_pts_[5][2] = {};     /* each point's yaw and pitch from the middle */
    double ws_still_from_ = -1;   /* held still on the point since */
    int ws_action_ = 0;           /* the Controllers page's buttons: which one */
    int ws_tune_from_ = kWsTry;   /* where Save as Template goes back to */
    /* Where the pointer can choose something (drawn last frame): a row and a choice. */
    struct WsHit
    {
        float x, y, w, h;
        int row, choice;
    };
    static constexpr int kWsLocked = -10; /* a WsHit's choice: kWsLocked - n, controller n not here yet */
    std::vector<WsHit> ws_hits_;
    bool ws_pointing_ = false;    /* the pointer leads (it moved since the D-pad was used) */
    float ws_px_ = 960, ws_py_ = 540, ws_rest_x_ = 960, ws_rest_y_ = 540;
    int ws_hover_ = -1;           /* the hit the pointer is on */
    int ws_target_ = 0, ws_hits_n_ = 0, ws_misses_n_ = 0;
    double ws_hit_time_ = -10;
    float ws_swing_ = 0, ws_swing_peak_ = 0;
    bool map_from_ws_ = false;    /* Customize buttons was opened from the Controllers page */
    void ws_pointer_step(bool dpad);
    bool ws_choose(int row, int choice);
    std::vector<int> ws_tune_rows() const;
    /* ---- the Controller Hub (ui_app_controls.cpp): the main controller and the
     * second, then three tiles (Hold it, Calibrate, Feel) or every setting in
     * sections that fold open, one at a time, the picture beside them following
     * the open one. */
    struct HubRow
    {
        int id = 0;
        int kind = 0; /* HubKind */
        std::string label;
        std::vector<std::string> opts, tags;
        std::vector<int> icons;
        std::vector<bool> ok;
        int cur = -1;      /* the choice made (a segment, a tile) */
        float frac = 0;    /* a slider's place */
        std::string value; /* a slider's value, or a text row's words */
    };
    struct HubSection
    {
        int id = 0;
        std::string name, summary;
        bool locked = false;
        std::vector<HubRow> rows;
    };
    std::vector<HubSection> ctl_sections();
    Action update_ctl_hub(bool up, bool down, bool left, bool right);
    void draw_ctl_hub(double time);
    bool ctl_change(int row, int dir, int choice = -1);
    bool ctl_activate(int row, int which);
    void ctl_scene(int scene, float x, float y, float w, float h, float alpha, bool small);
    void ctl_line(float x0, float y0, float x1, float y1, float width, Color c);
    /* Ruben's controller diagrams (assets/ui/diagrams), each control labeled beside
     * the picture, its line leaving the control straight out, turning, then
     * running across to the label. */
    enum DiaKind
    {
        kDiaDualSense,
        kDiaMove,
        kDiaNav,
        kDiaRemote,
        kDiaEye,
        kDiaKinds,
    };
    struct DiaLabel
    {
        std::string anchor; /* the control's name in the diagram */
        int side = -1;      /* 0 left, 1 right, 2 above, 3 below; -1 the side it's on */
        int chip = -1;      /* a Wii button's chip (WiiInput), -1 none */
        std::string title, sub;
        bool dim = false;   /* (not used) */
    };
    struct DiaBox
    {
        float x = 0, y = 0, w = 0, h = 0, k = 0; /* where the picture went, and its scale */
    };
    /* turn: 0 as drawn, 1 a quarter anticlockwise (stood on its right end), 2 clockwise. */
    DiaBox draw_diagram(int kind, const std::vector<DiaLabel> &labels, float x, float y, float w, float h, float alpha,
                        int turn = 0);
    std::vector<DiaLabel> ds_labels(const porpoise::pad::WiiLayout &lay, bool upright);
    std::vector<DiaLabel> move_labels();
    std::vector<DiaLabel> gc_labels(const porpoise::pad::Mapping &m);
    Texture *dia_tex_[kDiaKinds] = {};
    bool dia_tried_ = false;
    void ctl_keep();
    /* 4.5 Beta 4: whose controller the hub shows (0..3, L1 / R1 or the
     * switcher at the top). Player 1 has the whole hub; players 2 to 4 a page of
     * their own: what they hold, what it plays as, and whether it's here. */
    int ws_player_ = 0;
    int ws_player_dir_ = 1;     /* the page slides in from this side */
    double ws_player_at_ = -10; /* when it changed */
    std::uint32_t ws_shoulder_ = 0; /* L1 or R1 down, to switch players on a tap's release */
    double ws_shoulder_at_ = -10;
    float ws_pill_x_ = -1;      /* the switcher's highlight, sliding to the player's */
    double ws_pill_t_ = 0;
    int ws_prow_ = 0, ws_pcol_ = 0; /* players 2 to 4's page: the row and tile the D-pad is on */
    void hub_player(int to);
    void draw_hub_switcher(double time);
    void draw_hub_player(double time);
    Action update_hub_player(bool up, bool down, bool left, bool right, bool cross, const WsHit *hover);
    /* A player's light bar color as Porpoise draws it (Settings > Controls), 0xRRGGBB. */
    std::uint32_t player_hex(int player) const;
    int ws_view_ = 0;       /* 0 the three tiles, 1 every setting */
    int ws_open_ = -1;      /* the section open (HubSec), -1 none */
    int ws_focus_ = 0;      /* what the D-pad is on (HubFocus, or a row's id) */
    int ws_sub_ = 0;        /* within it: a button, a swatch, a tile */
    int ws_front_ = 0;      /* the three tiles: which */
    int ws_scope_ = 1;      /* 0 just this game, 1 every Wii game */
    bool ws_dirty_ = false; /* something kept since the hub opened */
    Settings ws_eff_;       /* the values shown: every game's, with this game's own over them */
    float ws_scroll_ = 0, ws_scroll_to_ = 0;
    double ws_nav_time_ = -10, ws_last_t_ = 0;
    float ws_ptr_a_ = 0;  /* the pointer's fade */
    float ws_trace_[180] = {}; /* the swing, a sample a frame (Motion and feel's picture) */
    int ws_trace_n_ = 0;
    double ws_trace_acc_ = 0; /* time toward the swing graph's next sample */
    float ws_trail_[10][2] = {};
    int ws_trail_n_ = 0;
    int ws_pic_ = -1, ws_pic_was_ = -1; /* the picture shown, and before it (they cross-fade) */
    double ws_pic_at_ = -10;
    float ws_last_swing_ = 0;
    double ws_last_swing_at_ = -10;
    void draw_move_pad(float x, float y, float w, float h, float alpha, bool labels = true);
    /* What the PS3 Eye sees (mirrored, as the player sees it), the ball marked. */
    void draw_eye_view(float x, float y, float w, float h, float alpha);
    Texture *eye_tex_ = nullptr;
    unsigned eye_seq_ = 0;
    std::vector<std::uint8_t> eye_px_;
    /* The Move's ball as Porpoise draws it (Settings: Move Ball Color), 0xRRGGBB. */
    std::uint32_t ball_hex() const;
    /* The ball lit in its color on the Move's picture (controllers.png's, drawn
     * side wide at cx, cy). */
    void draw_move_ball(float cx, float cy, float side, float alpha = 1.0f);
    /* Draws with the colors as given, not the look's (its hue, saturation, one
     * color): the ball's color has to read as the ball's. */
    template <class F> void in_true_color(F &&draw)
    {
        Gfx &g = *g_;
        const bool toned = g.toned();
        g.set_tone(false);
        const Look was = g.look();
        Look full = was;
        full.hue = -1;
        full.saturation = 1;
        full.dark = 1;
        full.fill_alpha = 1;
        full.mono = false;
        full.panel_style = 0; /* (glass would tint the fill) */
        g.set_look(full);
        const bool solid = g.solid();
        g.set_solid(true);
        draw();
        g.set_solid(solid);
        g.set_look(was);
        g.set_tone(toned);
    }
    /* Controllers coming and going, the Move's battery: a notice in the look in use,
     * and each player's controller by the clock. */
    struct CtlToast
    {
        int icon = 0; /* 0 Move, 1 DualSense, 2 battery, 3 Move gone */
        std::string title, sub;
        double at = 0;
    };
    std::vector<CtlToast> ctl_toasts_;
    bool ctl_move_was_ = false, ctl_low_told_ = false, ctl_seen_ = false;
    bool ctl_pad_was_[4] = {true, false, false, false};
    int ctl_battery_ = -1;
    void ctl_watch();
    void ctl_toast(int icon, const std::string &title, const std::string &sub);
    void draw_ctl_toasts();
    void ctl_icon(int icon, float cx, float cy, float size, Color ink, Color ball);
    /* The controllers, right-aligned to right_x: style 0 dark, 1 light, 2 Revolution
     * (icons only), 3 Star Cube. Returns the width drawn. */
    float draw_ctl_badges(float right_x, float cy, int style);
    Game *ws_game_ = nullptr;
    bool ws_launch_ = false;
    bool ws_in_game_ = false;    /* over the paused game */
    bool ws_recal_only_ = false; /* the pause menu's "recalibrate": centre, corners, try */
    bool ws_real_ = false;       /* real Wii Remotes (DolphinBar): nothing to set up, how to hold them */
    bool ws_ask_ = true; /* the setup's game: offer the setup before it starts */
    int ws_row_ = 0;
    int ws_size_mode_ = 0;       /* 0 corners, 1 size and distance */
    int ws_ig_marks_ = 0;        /* over a game: its first update and draw logged (1, 2) */
    int ws_preset_slot_ = 0, ws_preset_name_ = 0;
    std::string ws_state_;
    Screen ws_return_ = Screen::Main;
    double ws_hold_from_ = -1;     /* Cross held since, calmly (centring) */
    float ws_tl_[2] = {0, 0};      /* the top-left corner: yaw, pitch */
    std::string ws_note_;
    double ws_note_time_ = -100;
    Settings *guide_target_ = nullptr;
    Screen guide_return_ = Screen::Main;
    int guide_controller_ = 0;

    Dialog dialog_;
    bool drawing_dialog_ = false;
    Texture *logo_ = nullptr;
    bool logo_tried_ = false;
    Texture *pad_art_ = nullptr; /* the controller on the mapping screen */
    bool pad_art_tried_ = false;

    /* Button mapping */
    Screen map_return_ = Screen::Main;
    Settings *map_target_ = nullptr; /* the global settings, or a game's */
    std::string latest_version_, latest_url_; /* "1.2", from GitHub; "" when not newer */
    std::size_t latest_size_ = 0;
    int update_phase_ = 0;
    std::size_t update_done_ = 0, update_total_ = 0;
    std::string update_error_;
    std::string update_note_; /* after a check: up to date, or what went wrong */
    bool update_failed_ = false; /* an install failed: say so once */
    double update_done_time_ = 0; /* when the install finished */
    double update_note_time_ = -100;
    std::string update_row_value() const;
    void draw_update_overlay(double time);
    bool update_available() const { return !latest_version_.empty(); }
    Texture *flags_ = nullptr; /* Settings > Interface > Language */
    bool flags_tried_ = false;
    Texture *qr_ = nullptr; /* Settings > About: the Discord */
    Texture *ripalda_ = nullptr; /* Ruben's mark, beside his name in About */
    bool mark_tried_ = false;
    bool qr_tried_ = false;
    std::vector<std::string> border_names_; /* the Border row's choices */
    int border_choice_ = 0;
    int gba_rom_choice_[4] = {}; /* Settings > GBA's game rows: the file picked in each port's folder */
    /* A game's home screen tile (Game settings > Home screen): its icon (0 the
     * cover, 1 the player's own), background (0 Porpoise's, 1 the cover, 2 the
     * player's own), and whether Porpoise closes after the game. */
    bool fwd_exit_ = true;

    /* The tile art editor (ui_app_tileart.cpp): the icon and the background
     * a game's home screen tile is made with, seen as they will be. */
    struct TileArtEditor
    {
        porpoise::tileart::Spec spec;
        int part = 0; /* 0 the icon, 1 the background */
        int row = 0;  /* 0 the picture, 1 how it fits, 2 what's behind it */
        Texture *tex[2] = {nullptr, nullptr};
        porpoise::tileart::Picture src[2]; /* each part's picture, small for the preview */
        std::string src_from[2];          /* the file it was read from */
        bool dirty[2] = {true, true};
        bool shown[2] = {false, false}; /* the preview has a picture */
        int waiting = -1;               /* the source being downloaded, -1 none */
        std::string note;               /* what the preview can't show, and why */
        bool picking = false;           /* the folder browser is choosing a picture */
    };
    TileArtEditor art_;
    std::string tile_key() const; /* the game's art goes by this: its disc ID */
    void open_tile_art();
    void close_tile_art();
    Action update_tile_art(double dt);
    void draw_tile_art(double time);
    void tile_art_refresh();
    float stick_x_ = 0, stick_y_ = 0; /* the left stick as it is */
    bool browse_images_ = false;      /* the folder browser lists pictures, for the tile art */
    void make_forwarder();
    int audio_preset_ = 0; /* Settings > Audio's Sound preset row (Settings::audio_preset) */
    int music_level_ = 0, sounds_level_ = 0; /* Settings > Interface's Menu Music and Menu Sounds rows */
    int move_choice_ = 0; /* Settings > Games' Move Porpoise's Folder row: the drive picked */
    Game *map_game_ = nullptr;       /* the game whose settings those are, if any */
    bool map_in_game_ = false;       /* opened from the in-game menu */
    int map_preset_ = 0;             /* the player's layout being edited, 0..3 */
    int map_row_ = 0;
    bool map_capture_ = false; /* waiting for a button press */
    bool map_armed_ = false;   /* every button was let go since the capture began */
    double map_capture_start_ = 0;
    std::string map_note_; /* a line under the list after a change */
    double map_note_time_ = -10;
    int map_kind_ = 0;
    int map_wii_slot_ = 1; /* the Wii buttons being edited: 0 Porpoise's, 1 Mine 1, 2 Mine 2 */
    int map_gba_slot_ = 1;   /* the GBA's buttons being edited: the same three */
    int map_gba_player_ = 0; /* whose GBA buttons (0..3: player 1..4) */
    /* Settings > Games > Move Porpoise's folder: the places offered (label,
     * path), the one picked, and the one confirmed. */
    std::vector<std::pair<std::string, std::string>> move_places_;
    int move_pick_ = -1;
    bool testing_notice_ = false;
    bool welcome_after_dialog_ = false; /* offer_found_folder: Start fresh goes on to the welcome */
    std::string move_target_; /* 0 GameCube; 1..4 a Wii controller's buttons (porpoise::pad::wii_button_set + 1) */

    /* Details: L2 / R2 swipe to the previous / next game. */
    float swipe_anim_ = 0; /* 1 -> 0 */
    int swipe_dir_ = 0;
    int swipe_from_ = -1;
    float rep_l2_ = 0, rep_r2_ = 0;
    std::string note_;
    std::string flash_note_;
    /* The home screen (Revolution look). Focus: a tile 0..11 on the page, or
     * one of the buttons (kHome* in ui_app_home.cpp), -1 none. */
    int home_page_ = 0;
    float home_scroll_ = 0;
    int home_focus_ = 0;
    bool home_synced_ = false; /* the focus has been put on the selected game */
    float home_grow_[kHomeGrow] = {};
    float rev_tab_x_[3] = {}, rev_tab_w_[3] = {};
    Texture *hands_[2] = {nullptr, nullptr}; /* open, pointing */
    float zoom_anim_ = 0;   /* 1 -> 0 as a tile opens or closes */
    int zoom_dir_ = 1;      /* +1 opening, -1 closing */
    int zoom_game_ = -1;
    float zoom_from_[4] = {0, 0, 0, 0};
    bool hand_tried_ = false;
    int rd_focus_ = 0;           /* the opened tile: Start, the controls, the chips, the arrows */
    float rd_grow_[kRdGrow] = {};
    bool rd_info_ = false;       /* Triangle: the facts over the banner */
    bool mc_wii_ = false;        /* Memory Cards shows the Wii saves */
    std::string wii_focus_code_; /* focus this game's Wii save on the next scan */
    bool home_pointing_ = false; /* the pointer leads (else the D-pad) */
    float home_px_ = 960, home_py_ = 540, home_roll_ = 0;
    float home_hide_x_ = 0, home_hide_y_ = 0;
    double home_buzz_until_ = -1;
#ifdef PORPOISE_HOST_PREVIEW
    float preview_px_ = -1, preview_py_ = -1;
#endif
    int creator_presses_ = 0; /* About: Cross on the creator's name (developer options) */
    double creator_time_ = -100;
    double flash_time_ = -100;

    /* Motion: the content slides in when the tab changes; screens fade in. */
    float tab_anim_ = 0;
    int tab_dir_ = 0;
    float pill_x_ = -1, pill_w_ = 0;
    float tab_x_[3] = {0, 0, 0}, tab_w_[3] = {0, 0, 0}; /* laid out by draw_top_bar */
    float screen_anim_ = 0;

    /* The folder browser: opens on "/", the whole console; "" is the drives
     * and shortcuts list (Triangle). Folders first, then the games in this
     * folder, so the player sees where their games are. */
    struct BrowseEntry
    {
        enum Kind
        {
            Folder,
            Game,
            Place,
            Picture, /* browse_images_: a .png or .jpg */
        } kind = Folder;
        std::string label, path;
        long long size = 0; /* a game's, in bytes */
        int games = -1;     /* games right inside a folder; -1 not counted */
    };
    std::string browse_path_;
    std::vector<BrowseEntry> browse_entries_;
    int browse_row_ = 0;
    int browse_first_ = 0;
    int browse_games_ = 0;
    bool browse_unreadable_ = false;
    std::string browse_copy_name_; /* the game being copied to the console */
    void start_game_copy(const std::string &from);
    std::vector<BrowseEntry> browse_places() const;

    std::uint32_t held_ = 0, prev_ = 0;
    std::uint32_t raw_held_ = 0, raw_prev_ = 0; /* the buttons alone, without the stick as a D-pad */
    /* Whichever was pressed last, the Wii Remote or the DualSense, has its
     * buttons in the prompts (Gfx::set_wii_buttons). */
    std::uint32_t wii_prev_ = 0, gc_prev_ = 0;
    /* The prompts show the buttons of the controller pressed last: the
     * DualSense's, a Wii Remote's or the Switch 2 GameCube controller's. */
    void follow_last_controller(std::uint32_t pad_held, std::uint32_t wii_held, bool remote_there = true,
                                std::uint32_t gc_held = 0);
    float rep_left_ = 0, rep_right_ = 0, rep_up_ = 0, rep_down_ = 0;
    double nav_held_ = 0; /* how long a direction has been held in the library (speeds it up) */
    float lib_rep_l2_ = 0, lib_rep_r2_ = 0; /* the library's L2 / R2 letter jumps (not Details' timers) */
    double time_ = 0;
    double ui_dt_ = 1.0 / 60.0; /* this frame's step (update's dt): 1/60, or 1/119.88 with the menus at 120 Hz */

    Game *launch_ = nullptr;
    std::string launch_status_;
    float launch_progress_ = -1;
    double launch_start_ = 0;
    /* The cover glide into the launch screen: where the game's cover was in
     * the library's last frame (x y w h), when it was on screen. */
    float launch_from_[4] = {0, 0, 0, 0};
    bool launch_from_ok_ = false;
};
/* Real Wii Remotes connecting and disconnecting (ui_wiimote_notices.cpp):
 * over the menus and over a game. */
void draw_wiimote_notices(Gfx &g);

} // namespace porpoise::ui
