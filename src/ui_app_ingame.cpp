/* Porpoise UI - the in-game menu: Options + touch pad pauses the game and
 * slides this in from the left, in the launcher's own glass.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Five tabs, L1 / R1 between them:
 *   Game      resume, save states (three slots, with a picture each), fast
 *             forward, volume, quit
 *   Video     resolution, widescreen, aspect, the screen filter and border...
 *   Graphics  shader compilation and the other Dolphin graphics options
 *   Controls  the button layout, Customize buttons (the mapping screen, over
 *             the game), vibration, and the controller with a line out to
 *             every button, saying which GameCube button it is.
 *   Patches   how the game plays in widescreen this time (its 16:9 code, its
 *             own option, the emulated hack or 4:3), and every cheat and patch
 *             it has, each with its switch: so nothing on screen is a mystery.
 *   Achievements  with RetroAchievements on and a set for the game: every
 *             achievement, unlocked or not (ui_app_achievements.cpp).
 * Changes are saved as the game's own settings and take effect at once
 * (main applies them as take_menu_change() reports them). */
#include <algorithm>
#include <utility>
#include <cctype>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <sys/stat.h>

#include "porpoise_borders.hpp"
#include "porpoise_core.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_states.hpp"
#include "ecam_bridge.h"
#include "ui_app.hpp"
#include "porpoise_gfxmods.hpp"
#include "ui_app_common.hpp"
#include "ui_cheats.hpp"
#include "ui_i18n.hpp"
#include "ui_setups.hpp"
#include "ui_widescreen.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
enum TabId
{
    kTabGame,
    kTabVideo,
    kTabGraphics,
    kTabAudio,
    kTabControls,
    kTabPatches,
    kTabCount,
};
const char *const kTabNames[kTabCount] = {"Game", "Video", "Graphics", "Audio", "Controls", "Patches"};

/* The Audio tab's Sound preset row (Settings::audio_preset), worked out from
 * the game's settings each time its rows are. */
int g_audio_preset = 0;
/* How the game's sound is running (pulled from Dolphin's mixer, or Classic):
 * the rows under the preset are that way's, since a change between the two
 * only takes at the next start. */
bool g_sound_pulled = true;
/* The game in the menu's ID: its graphics mods (porpoise_gfxmods). */
std::string g_menu_game_id;
int g_menu_discs = 0, g_menu_disc = 0; /* App::set_menu_discs */
std::vector<int> g_menu_disc_numbers;                         /* the game's own discs' numbers */
std::vector<std::pair<std::string, int>> g_menu_other_discs; /* 4.0: other games' discs */
int g_menu_disc_pick = 0, g_menu_disc_chosen = -1;
/* The GBAs (GameCube ports with one): which the GBA rows are for, its folder's games (and which
 * is picked), the e-Reader cards. Lists read as the rows are made. */
std::string g_menu_gba_dir; /* <data>/games/gba */
int g_menu_gba = -1, g_menu_gba_rom_pick = 0, g_menu_gba_card_pick = 0;
std::vector<std::string> g_menu_gba_roms, g_menu_gba_cards;
int g_menu_gba_slot = 1, g_menu_gba_import_pick = 0;
std::vector<std::string> g_menu_gba_imports; /* the game's .sav files in exports, newest first */

std::vector<std::string> files_in(const std::string &dir, std::initializer_list<const char *> endings)
{
    std::vector<std::string> out;
    if (DIR *d = opendir(dir.c_str()))
    {
        while (const dirent *e = readdir(d))
        {
            std::string n = e->d_name, low = n;
            for (char &c : low)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            for (const char *end : endings)
            {
                const std::string x = end;
                if (low.size() > x.size() && low.compare(low.size() - x.size(), x.size(), x) == 0)
                    out.push_back(n);
            }
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string gba_port_dir(int port)
{
    return g_menu_gba_dir + "/port" + std::to_string(port + 1);
}

/* A GBA's exports: <port folder>/exports, and its game's name without the ending ("bios"). */
std::string gba_exports(int port)
{
    return gba_port_dir(port) + "/exports";
}

std::string gba_stem(const Settings &p, int port)
{
    std::string stem = p.gba_rom[port].empty() ? "bios" : p.gba_rom[port];
    if (const auto dot = stem.rfind('.'); dot != std::string::npos && dot > 0)
        stem.resize(dot);
    return stem;
}

std::string gba_state_path(const Settings &p, int port, int slot)
{
    return gba_exports(port) + "/" + gba_stem(p, port) + ".ss" + std::to_string(std::clamp(slot, 1, 9));
}

/* The sheet's own "tabs" past Porpoise's, and Achievements (kTabCount): the GBA's rows, and all the Game tab's */
constexpr int kEcGba = kTabCount + 1, kEcAll = kTabCount + 2;
/* Its Buttons category (Playtendoh's app/buttons_menu.h): in place of Customize Buttons' screen */
constexpr int kEcButtons = kTabCount + 3;
/* Rows of the list on their own, under the categories */
constexpr int kEcFast = kTabCount + 4, kEcDisc = kTabCount + 5, kEcRestart = kTabCount + 6, kEcVolume = kTabCount + 7;

/* The Buttons category's names, Playtendoh's (its porpoise_rows.cpp and buttons_menu.h): the GameCube's
 * buttons in GcInput order, the DualSense's in Control order */
const char *const kPadButtonNames[GcCount] = {"A", "B", "X", "Y", "Z", "L", "R", "Start",
                                              "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
const char *const kPadControlNames[CtlCount] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3",
                                                "Options", "Touchpad click", "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
/* The DualSense control held now (the first in Control order), or -1 */
int held_control(std::uint32_t held)
{
    for (int c = 0; c < CtlCount; ++c)
        if (held & control_bit(c))
            return c;
    return -1;
}

/* What a row does. */
enum class Kind
{
    Resume,
    Save,
    Load,
    FastForward,
    Library,
    Home,
    Restart, /* start the game over (a second press confirms) */
    Customize,
    SaveSetup, /* this game's video and graphics, kept as a setup */
    UseSetup,  /* a setup's settings for this game */
    Int,  /* a setting with a list of values */
    Bool, /* a setting that is on or off */
    Border,
    Info,  /* a fact to read (its help says more) */
    Cheat, /* one of the game's codes: on or off, from the next start */
    Shot,  /* Take Screenshot */
    Disc,  /* 3.1: Change Disc (a game on two discs) */
    GbaReset, /* the GBA starts over (a GameCube port with a GBA), the game goes on */
    GbaPick,  /* which GBA the rows below are for (more than one) */
    GbaLink,  /* Disconnect: the GBA's link cable out, or in again */
    GbaRom,   /* the GBA's game: a file in games/gba/portN (or none), put in with Cross */
    GbaDo,    /* index: 4 / 5 load / save a GBA state, 6 / 7 import / export its save */
    GbaCard,  /* an e-Reader card (games/gba/ereader), scanned with Cross */
    GbaSlot,  /* the GBA state slot (1..9) Load / Save GBA State use */
    GbaImport, /* one of the game's exported saves (its folder's exports), imported with Cross */
    Group, /* 4.5: a heading that opens or closes the rows under it (index: which) */
    PadTest,   /* Under Playtendoh, its Buttons category: what the DualSense sends now */
    PadButton, /* a GameCube button (index: GcInput) and its DualSense control: Cross, then the control */
    PadReset,  /* every button back as they start (held) */
};

struct Row
{
    Kind kind;
    const char *key; /* the setting's key, for Int / Bool / Border */
    const char *label;
    int *iv = nullptr;
    bool *bv = nullptr;
    int min = 0;
    std::vector<std::string> values = {}; /* English; translated when drawn */
    bool gap_before = false;
    int index = -1;        /* Cheat: which of the game's codes */
    std::string text = {}; /* a label as it is (a code's name), when label is null */
    bool child = false;    /* under a Group: drawn a step in */
};

/* 4.5: the Video tab's groups, open or closed (as left, for the run). */
enum
{
    kGroupColor,
    kGroupCrop,
    kGroupCount
};
bool g_group_open[kGroupCount] = {false, false};

const std::vector<std::string> &crop_values()
{
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out = {"Off"};
        for (int i = 1; i <= 20; ++i)
        {
            char b[16];
            std::snprintf(b, sizeof b, i % 2 ? "%d.5%%" : "%d%%", i / 2);
            out.push_back(b);
        }
        return out;
    }();
    return v;
}

/* What the Patches tab shows. */
struct Patches
{
    const std::vector<Cheat> *cheats = nullptr;
};

const std::vector<std::string> kAdjust = {"-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5"};
const std::vector<std::string> kPercent = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};

std::vector<Row> rows_for(int tab, Settings &p, bool wii = false, const Patches *patches = nullptr)
{
    const bool wii_game = wii; /* (wii turns false below for a GameCube controller in a Wii game) */
    std::vector<Row> r;
    switch (tab)
    {
    case kTabGame:
        r.push_back({Kind::Resume, "", "Resume"});
        r.push_back({Kind::Save, "", "Save State\xE2\x80\xA6"});
        r.push_back({Kind::Load, "", "Load State\xE2\x80\xA6"});
        r.push_back({Kind::Shot, "", "Take Screenshot"});
        r.push_back({Kind::FastForward, "", "Fast Forward"});
        if (g_menu_discs > 1 || !g_menu_other_discs.empty())
            r.push_back({Kind::Disc, "", "Change Disc"});
        r.push_back({Kind::Bool, "fps_overlay", "FPS Counter", nullptr, &p.fps_overlay, 0, {"Off", "On"}});
        r.push_back({Kind::Restart, "", "Start Over"});
        if (!wii_game && p.gba_port() >= 0)
        {
            /* The GBA rows: which GBA, then what Dolphin's GBA window can do to it. */
            int count = 0;
            for (int i = 0; i < 4; ++i)
                count += p.port_has_gba(i) ? 1 : 0;
            if (g_menu_gba < 0 || !p.port_has_gba(g_menu_gba))
                g_menu_gba = p.gba_port();
            const int port = g_menu_gba;
            if (count > 1)
                r.push_back({Kind::GbaPick, "", "GBA", nullptr, nullptr, 0, {}, true});
            r.push_back({Kind::GbaReset, "", "Reset GBA", nullptr, nullptr, 0, {}, count <= 1});
            r.push_back({Kind::GbaLink, "", "GBA Link Cable"});
            g_menu_gba_roms = files_in(gba_port_dir(port), {".gba", ".agb", ".bin"});
            g_menu_gba_rom_pick = 0;
            for (std::size_t i = 0; i < g_menu_gba_roms.size(); ++i)
                if (g_menu_gba_roms[i] == p.gba_rom[port])
                    g_menu_gba_rom_pick = int(i) + 1;
            r.push_back({Kind::GbaRom, "", "GBA Game"});
            static const char *const kBeside[] = {"gba_rom1", "gba_rom2", "gba_rom3", "gba_rom4"};
            r.push_back({Kind::Bool, kBeside[port], "GBA Save Beside the Game", nullptr, &p.gba_rom_beside[port], 0,
                         {"Off", "On"}});
            /* States: nine slots for the GBA's game; saves: exported with the date and time, and any
             * of the game's there imported (newest first). */
            r.push_back({Kind::GbaSlot, "", "GBA State Slot"});
            r.push_back({Kind::GbaDo, "", "Load GBA State", nullptr, nullptr, 0, {}, false, 4});
            r.push_back({Kind::GbaDo, "", "Save GBA State", nullptr, nullptr, 0, {}, false, 5});
            r.push_back({Kind::GbaDo, "", "Export GBA Save", nullptr, nullptr, 0, {}, false, 7});
            {
                const std::string stem = gba_stem(p, port);
                g_menu_gba_imports.clear();
                for (const std::string &f : files_in(gba_exports(port), {".sav"}))
                    if (f.size() > stem.size() && f.compare(0, stem.size(), stem) == 0 &&
                        (f[stem.size()] == ' ' || f[stem.size()] == '.')) /* <game>.sav, <game> <date>.sav */
                        g_menu_gba_imports.push_back(f);
                std::reverse(g_menu_gba_imports.begin(), g_menu_gba_imports.end()); /* the dated ones: newest first */
                g_menu_gba_import_pick = std::clamp(g_menu_gba_import_pick, 0,
                                                    std::max(0, int(g_menu_gba_imports.size()) - 1));
                r.push_back({Kind::GbaImport, "", "Import GBA Save"});
            }
            g_menu_gba_cards = files_in(g_menu_gba_dir + "/ereader", {".raw"});
            if (!g_menu_gba_cards.empty())
                r.push_back({Kind::GbaCard, "", "Scan e-Reader Card"});
        }
        r.push_back({Kind::Library, "", "Quit to Library", nullptr, nullptr, 0, {}, true});
        if (!ecam_bridge::request.valid) /* started by PS5-ECAM, Quit to Library goes back to it already */
            r.push_back({Kind::Home, "", "Close Porpoise"});
        break;
    case kTabVideo:
        r.push_back({Kind::Int, "resolution", "Internal Resolution", &p.resolution, nullptr, 1,
                     {"1x (480p)", "2x (720p)", "3x (1080p)", "4x (1440p)", "5x (1800p)", "6x (4K)"}});
#ifndef PORPOISE_DESKTOP
        r.push_back({Kind::Bool, "frame_gen", "Frame Generation (Beta)", nullptr, &p.frame_gen, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "fg_steady30", "Steady 30 FPS", nullptr, &p.fg_steady30, 0, {"Off", "On"}});
#endif
        r.push_back({Kind::Int, "wide", "Widescreen", &p.wide, nullptr, 0, {"Auto", "On", "Off", "Universal (Beta)"}});
        r.push_back({Kind::Int, "aspect", "Aspect Ratio", &p.aspect, nullptr, 0,
                     {"Auto", "Force 16:9", "Force 4:3", "Stretch to Fill"}});
        r.push_back({Kind::Int, "antialiasing", "Anti-Aliasing", &p.antialiasing, nullptr, 0,
                     {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"}});
        r.push_back({Kind::Int, "anisotropy", "Anisotropic Filtering", &p.anisotropy, nullptr, 0,
                     {"1x", "2x", "4x", "8x", "16x"}});
        r.push_back({Kind::Int, "screen_filter", "Screen Filter", &p.screen_filter, nullptr, 0,
                     {"Smooth", "Sharp", "Sharpen (CAS)", "CRT", "Arcade CRT", "VHS", "Soft VHS", "8-Bit", "Pocket",
                      "Scanlines", "Shadow Mask", "LCD", "FSR 1", "16-Bit", "NTSC Composite", "Aperture Grille", "Smooth Edges (FXAA)"}});
        r.push_back({Kind::Int, "filter_strength", "Filter Strength", &p.filter_strength, nullptr, 1,
                     {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}});
        /* Color and the border, and the crop: each its own group, closed
         * until opened (the tab ran long). */
        {
            Row g;
            g.kind = Kind::Group;
            g.key = "";
            g.label = "Color and Border";
            g.index = kGroupColor;
            g.gap_before = true;
            r.push_back(g);
            if (g_group_open[kGroupColor])
            {
                r.push_back({Kind::Int, "bloom", "Extra Bloom", &p.bloom, nullptr, 0, {"Off", "Low", "Medium", "High"}});
                r.push_back({Kind::Int, "color_saturation", "Saturation", &p.color_saturation, nullptr, 0, kAdjust});
                r.push_back({Kind::Int, "color_contrast", "Contrast", &p.color_contrast, nullptr, 0, kAdjust});
                r.push_back({Kind::Int, "color_warmth", "Warmth", &p.color_warmth, nullptr, 0, kAdjust});
                r.push_back({Kind::Border, "border", "Border"});
                for (std::size_t i = r.size() - 5; i < r.size(); ++i)
                    r[i].child = true;
            }
            Row c;
            c.kind = Kind::Group;
            c.key = "";
            c.label = "Crop";
            c.index = kGroupCrop;
            r.push_back(c);
            if (g_group_open[kGroupCrop])
            {
                r.push_back({Kind::Int, "crop_left", "Left", &p.crop_left, nullptr, 0, crop_values()});
                r.push_back({Kind::Int, "crop_right", "Right", &p.crop_right, nullptr, 0, crop_values()});
                r.push_back({Kind::Int, "crop_top", "Top", &p.crop_top, nullptr, 0, crop_values()});
                r.push_back({Kind::Int, "crop_bottom", "Bottom", &p.crop_bottom, nullptr, 0, crop_values()});
                r.push_back({Kind::Bool, "crop_overscan", "Trim the Game's Black Edges", nullptr, &p.crop_overscan, 0,
                             {"Off", "On"}});
                for (std::size_t i = r.size() - 5; i < r.size(); ++i)
                    r[i].child = true;
            }
        }
        r.push_back({Kind::Bool, "fps_overlay", "FPS Counter", nullptr, &p.fps_overlay, 0, {"Off", "On"}});
        if (!wii_game && p.gba_port() >= 0)
        {
            /* The GBA on a GameCube port: its screen over the game. */
            r.push_back({Kind::Int, "gba_show_mode", "Show GBA Screen", &p.gba_show_mode, nullptr, 0,
                         {"While Held", "Toggle", "On"}, true});
            static const char *const kKeys[] = {"gba_window1", "gba_window2", "gba_window3", "gba_window4"};
            static const char *const kSize[] = {"GBA 1 Screen Size", "GBA 2 Screen Size", "GBA 3 Screen Size",
                                                "GBA 4 Screen Size"};
            static const char *const kOpacity[] = {"GBA 1 Opacity", "GBA 2 Opacity", "GBA 3 Opacity", "GBA 4 Opacity"};
            for (int i = 0; i < 4; ++i)
                if (p.port_has_gba(i))
                {
                    r.push_back({Kind::Int, kKeys[i], kSize[i], &p.gba_window_size[i], nullptr, 0,
                                 {"1x", "2x", "3x", "4x"}});
                    r.push_back({Kind::Int, kKeys[i], kOpacity[i], &p.gba_window_opacity[i], nullptr, 0, {"40%", "50%", "60%", "70%", "80%", "90%", "100%"}});
                    static const char *const kFilter[] = {"GBA 1 Screen Filter", "GBA 2 Screen Filter",
                                                          "GBA 3 Screen Filter", "GBA 4 Screen Filter"};
                    static const char *const kBlend[] = {"GBA 1 Interframe Blending", "GBA 2 Interframe Blending",
                                                         "GBA 3 Interframe Blending", "GBA 4 Interframe Blending"};
                    r.push_back({Kind::Int, kKeys[i], kFilter[i], &p.gba_window_filter[i], nullptr, 0,
                                 {"Off", "Soft Pixels"}});
                    r.push_back({Kind::Bool, kKeys[i], kBlend[i], nullptr, &p.gba_window_blend[i], 0, {"Off", "On"}});
                }
            r.push_back({Kind::Bool, "gba_web", "GBA Web Page", nullptr, &p.gba_web, 0, {"Off", "On"}});
        }
        break;
    case kTabGraphics:
        r.push_back({Kind::Int, "shader_mode", "Shader Compilation", &p.shader_mode, nullptr, 0,
                     {"Synchronous", "Ubershaders", "Async Ubershaders", "Async, Skip Drawing"}});
        r.push_back({Kind::Int, "texture_cache", "Texture Cache Accuracy", &p.texture_cache, nullptr, 0,
                     {"Fast", "Middle", "Safe"}});
        r.push_back({Kind::Int, "texture_filter", "Texture Filtering", &p.texture_filter, nullptr, 0,
                     {"Game's Own", "Nearest (Sharp)", "Linear (Smooth)"}});
        r.push_back({Kind::Int, "resampling", "Output Resampling", &p.resampling, nullptr, 0,
                     {"Default", "Bilinear", "B-Spline", "Mitchell-Netravali", "Catmull-Rom", "Sharp Bilinear",
                      "Area Sampling"}});
        r.push_back({Kind::Bool, "pixel_lighting", "Per-Pixel Lighting", nullptr, &p.pixel_lighting, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "disable_fog", "Disable Fog", nullptr, &p.disable_fog, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "skip_dupes", "Skip Duplicate Frames", nullptr, &p.skip_dupes, 0, {"Off", "On"}});
        {
            /* Dolphin's built-in graphics mods, for the games that have them. */
            const porpoise::gfxmods::Offer mods = porpoise::gfxmods::offer(g_menu_game_id);
            if (mods.bloom)
                r.push_back({Kind::Int, "gfx_bloom", "Bloom", &p.gfx_bloom, nullptr, 0,
                             mods.own_bloom ? std::vector<std::string>{"Game's Own", "Off", "Blurred"}
                                            : std::vector<std::string>{"Game's Own", "Off", "Blurred",
                                                                       "Native Resolution"}});
            if (mods.dof)
                r.push_back({Kind::Int, "gfx_dof", "Depth of Field", &p.gfx_dof, nullptr, 0,
                             {"Game's Own", "Off", "Blurred", "Native Resolution"}});
            if (mods.hud)
                r.push_back({Kind::Bool, "gfx_hud", "Hide the HUD", nullptr, &p.gfx_hud, 0, {"Off", "On"}});
            if (!mods.extra_title.empty())
                r.push_back({Kind::Bool, "gfx_extra", "Native Resolution Goop", nullptr, &p.gfx_extra, 0,
                             {"Off", "On"}});
        }
        r.push_back({Kind::SaveSetup, "", "Save as a Setup", nullptr, nullptr, 0, {}, true});
        r.push_back({Kind::UseSetup, "", "Use a Setup"});
        break;
    case kTabAudio:
    {
        r.push_back({Kind::Int, "volume", "Volume", &p.volume, nullptr, 0, kPercent});
        r.push_back({Kind::Bool, "muted", "Mute Game", nullptr, &p.muted, 0, {"Off", "On"}});
        g_audio_preset = p.audio_preset();
        std::vector<std::string> presets = {"Smooth", "Responsive", "Extra Smooth", "Classic (2.1)"};
        if (g_audio_preset == Settings::kAudioCustom)
            presets.push_back("Custom");
        r.push_back({Kind::Int, "audio_preset", "Sound Preset", &g_audio_preset, nullptr, 0, presets, true});
        if (g_sound_pulled)
        {
            r.push_back({Kind::Int, "audio_buffer", "Audio Buffer", &p.audio_buffer, nullptr, 0,
                         {"40 ms", "80 ms", "160 ms"}});
            r.push_back({Kind::Bool, "audio_fill", "Fill Audio Gaps", nullptr, &p.audio_fill, 0, {"Off", "On"}});
        }
        else
        {
            r.push_back({Kind::Int, "audio_buffer", "Audio Buffer", &p.audio_buffer, nullptr, 0,
                         {"Low", "Normal", "Safe"}});
            r.push_back({Kind::Bool, "audio_stretch", "Audio Stretching", nullptr, &p.audio_stretch, 0,
                         {"Off", "On"}});
        }
        r.push_back({Kind::Bool, "dsp_accurate", "Accurate Audio", nullptr, &p.dsp_accurate, 0, {"Off", "On"}, true});
        if (wii)
            r.push_back({Kind::Int, "wiimote_speaker", "Wii Remote Speaker", &p.wiimote_speaker, nullptr, 0,
                         {"Off", "TV", "Controller"}});
        if (!wii_game && p.gba_port() >= 0)
        {
            /* The GBA on a GameCube port: its sound. */
            static const char *const kKeys[] = {"gba_window1", "gba_window2", "gba_window3", "gba_window4"};
            static const char *const kVolume[] = {"GBA 1 Volume", "GBA 2 Volume", "GBA 3 Volume", "GBA 4 Volume"};
            static const char *const kMute[] = {"Mute GBA 1", "Mute GBA 2", "Mute GBA 3", "Mute GBA 4"};
            bool first = true;
            for (int i = 0; i < 4; ++i)
                if (p.port_has_gba(i))
                {
                    r.push_back({Kind::Int, kKeys[i], kVolume[i], &p.gba_window_volume[i], nullptr, 0, {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, first});
                    r.push_back({Kind::Bool, kKeys[i], kMute[i], nullptr, &p.gba_window_muted[i], 0, {"Off", "On"}});
                    static const char *const kSpeaker[] = {"GBA 1 Sound on the Controller", "GBA 2 Sound on the Controller",
                                                           "GBA 3 Sound on the Controller", "GBA 4 Sound on the Controller"};
                    static const char *const kSmooth[] = {"GBA 1 Speaker Resampling", "GBA 2 Speaker Resampling",
                                                          "GBA 3 Speaker Resampling", "GBA 4 Speaker Resampling"};
                    r.push_back({Kind::Bool, kKeys[i], kSpeaker[i], nullptr, &p.gba_window_speaker[i], 0,
                                 {"Off", "On"}});
                    r.push_back({Kind::Int, kKeys[i], kSmooth[i], &p.gba_window_smooth[i], nullptr, 0,
                                 {"Simple", "Smooth"}});
                    first = false;
                }
        }
        break;
    }
    case kTabControls:
        if (wii && p.wii_controller == porpoise::pad::WiiGameCube)
        {
            /* A GameCube controller in a Wii game: the GameCube's buttons, no
             * pointer or motion. */
            r.push_back({Kind::Customize, "wii_setup", "Controller Hub"});
            wii = false; /* then the GameCube's rows */
        }
        if (!wii)
        {
            r.push_back({Kind::Int, "button_layout", "Button Layout", &p.button_layout, nullptr, 0,
                         {"GameCube", "PlayStation", "My Layout 1", "My Layout 2", "My Layout 3", "My Layout 4"}});
            r.push_back({Kind::Customize, "", "Customize Buttons"});
            if (!wii_game)
                r.push_back({Kind::Int, "p1_port", "Player 1's Port", &p.p1_port, nullptr, 0,
                             {"Port 1", "Port 2", "Port 3", "Port 4"}});
            r.push_back({Kind::Int, "invert_main", "Invert the Control Stick", &p.invert_main, nullptr, 0,
                         {"Off", "Up and Down", "Left and Right", "Both"}});
            r.push_back({Kind::Int, "invert_c", "Invert the C-Stick", &p.invert_c, nullptr, 0,
                         {"Off", "Up and Down", "Left and Right", "Both"}});
            r.push_back({Kind::Int, "stick_sensitivity", "Stick Sensitivity", &p.stick_sensitivity, nullptr, 0,
                         {"Normal", "Lower", "Low", "Lowest"}});
            r.push_back({Kind::Int, "stick_deadzone", "Stick Dead Zone", &p.stick_deadzone, nullptr, 0,
                         {"Off", "Small", "Medium", "Large"}});
        }
        if (wii)
        {
            /* The Wii Remote: put right while playing. */
            r.push_back({Kind::Int, "wii_source", "Wii Remotes", &p.wii_source, nullptr, 0,
                         {"DualSense", "Real Wii Remotes (DolphinBar)"}});
            r.push_back({Kind::Bool, "real_wiimote_speaker", "Real Wii Remote Speakers", nullptr,
                         &p.real_wiimote_speaker, 0, {"Off", "On"}});
            r.push_back({Kind::Customize, "wii_recal", "Recalibrate the Pointer"});
            r.push_back({Kind::Customize, "wii_setup", "Controller Hub"}); /* (the Wii controls: all in the hub) */
        }
        r.push_back({Kind::Bool, "rumble", "Vibration", nullptr, &p.rumble, 0, {"Off", "On"}});
        r.push_back({Kind::Int, "turbo", "Turbo Button", &p.turbo, nullptr, 0, kTurboValues});
        r.push_back({Kind::Int, "quick_slot", "Quick Save Buttons", &p.quick_slot, nullptr, 0, kQuickSlotValues});
        if (!wii) /* the GameCube's L and R (also the GameCube controller in a Wii game) */
            r.push_back({Kind::Int, "trigger_feel", "Trigger Click", &p.trigger_feel, nullptr, 0, kTriggerFeelValues});
        break;
    case kTabPatches:
        r.push_back({Kind::Info, "ws_status", "Widescreen"});
        if (!patches || !patches->cheats || patches->cheats->empty())
            r.push_back({Kind::Info, "no_codes", "Cheats and Patches"});
        else
            for (std::size_t i = 0; i < patches->cheats->size(); ++i)
            {
                Row row{Kind::Cheat, "cheat", nullptr};
                row.index = int(i);
                const std::string &name = (*patches->cheats)[i].name;
                row.text = name.size() > 1 && name[0] == '$' ? name.substr(1) : name;
                row.gap_before = i == 0;
                r.push_back(row);
            }
        break;
    default:
        break;
    }
    return r;
}

/* The help line under the rows. */
const char *help_for(const Row &row, const Settings &p)
{
    if (row.key == std::string("resolution") && p.resolution >= 4)
        return "The highest resolutions may slow some games down.";
    if (row.key == std::string("screen_filter"))
        return "Porpoise's own filter on the way to the TV. CRT and Arcade CRT look best at 1080p or above.";
    if (row.key == std::string("wide"))
        return "Takes effect the next time the game starts. On, for a game without a widescreen code, uses the "
               "emulated widescreen hack, which can glitch at the screen edges.";
    if (row.key == std::string("border"))
        return "Fills the bars beside a 4:3 picture (widescreen off). Add your own PNGs to /data/porpoise/borders.";
    if (row.key == std::string("shader_mode"))
        return "Async Ubershaders hide the stutter when a game draws something new. Takes effect after a restart of the game.";
    if (row.key == std::string("texture_cache"))
        return "Safe fixes some games' text and effects; Fast is quickest.";
    if (row.kind == Kind::FastForward)
        return "Runs the game 2x or 4x faster until you turn it off. The game's sound pauses meanwhile.";
    if (row.kind == Kind::Save)
        return "Choose one of three slots to save this moment in.";
    if (row.kind == Kind::Load)
        return "Choose a saved moment to go back to.";
    if (row.kind == Kind::SaveSetup)
        return "Keeps this game's Video and Graphics settings as a setup, to use on other games.";
    if (row.kind == Kind::UseSetup)
        return "Puts a setup's Video and Graphics settings on this game.";
    if (row.kind == Kind::Customize && row.key == std::string("wii_recal"))
        return "Point at the middle and two corners again: after moving, or if the pointer feels off.";
    if (row.kind == Kind::Customize && row.key == std::string("wii_setup"))
        return "The whole setup: the Wii controller, how to hold it, your screen, and (advanced) fine-tuning.";
    if (row.key == std::string("wii_preset"))
        return "A Wii Remote set-up kept under a name (made in the setup's Fine-tune page, Advanced).";
    if (row.key == std::string("audio_preset"))
        return g_sound_pulled ? "Smooth covers the gaps when the game runs slow, as Dolphin does on a PC. Responsive: "
                              "less delay. Extra Smooth: for games that slow down often. Classic: 2.1's sound, from "
                              "the next start."
                            : "Classic is 2.1's sound. Smooth, Responsive and Extra Smooth cover the gaps when the "
                              "game runs slow; they apply the next time the game starts.";
    if (row.key == std::string("turbo"))
        return kTurboHelp;
    if (row.key == std::string("quick_slot"))
        return kQuickSlotHelp;
    if (row.key == std::string("trigger_feel"))
        return kTriggerFeelHelp;
    if (row.key == std::string("gfx_bloom"))
        return kGfxBloomHelp;
    if (row.key == std::string("gfx_dof"))
        return kGfxDofHelp;
    if (row.key == std::string("gfx_hud"))
        return kGfxHudHelp;
    if (row.key == std::string("gfx_extra"))
        return kGfxGoopHelp;
    if (row.key == std::string("audio_buffer"))
        return "How much sound is kept ready. More holds off crackling when a game slows down, for a little delay.";
    if (row.key == std::string("audio_fill"))
        return "When a game runs slow, the sound it just played covers the gap, faded, instead of a crackle. Off "
               "leaves the gap silent.";
    if (row.key == std::string("audio_stretch"))
        return "When a game slows down, its sound slows with it, slightly lower, instead of crackling.";
    if (row.key == std::string("dsp_accurate"))
        return "Dolphin's exact sound chip: fixes wrong sound in a few games, but needs much more of the processor. "
               "Takes effect the next time the game starts.";
    if (row.key == std::string("wiimote_speaker"))
        return "Sounds from the Wii Remote's own speaker: in the TV's sound, or from the controller. Takes effect the "
               "next time the game starts.";
    if (row.kind == Kind::Customize)
        return "Your own layouts: change any button on a picture of the DualSense.";
    if (row.kind == Kind::Group)
        return row.index == kGroupCrop
                   ? "Trim the picture's edges: black bars a game draws, or a picture that doesn't quite fill the "
                     "screen. Cross opens it."
                   : "Extra bloom, color and the border beside a 4:3 picture. Cross opens it.";
    if (row.key && std::strncmp(row.key, "crop_", 5) == 0 && std::strcmp(row.key, "crop_overscan") != 0)
        return "Cuts this much off that edge; the rest fills the screen at its own shape. You see it as you change "
               "it, and it's kept for this game.";
    if (row.key == std::string("crop_overscan"))
        return "Dolphin trims the black border some games draw round their picture (a thin bar at the sides of "
               "some widescreen games). Takes effect after a restart of the game.";
    if (row.key == std::string("fps_overlay"))
        return "Frames per second, in a corner while you play.";
    return ""; /* (no line: a game's own changes need no saying) */
}

/* How this run of the game plays in widescreen (Settings::ws_plan, as
 * ui_widescreen's Plan): the value shown, and what it means. */
const char *ws_value(int plan, bool wii)
{
    if (wii)
        return "The Wii's Setting";
    switch (plan)
    {
    case 1: return "16:9 Code";
    case 4: return "16:9 Code + Hack";
    case 2: return "In the game's options";
    case 3: return "Emulated Hack";
    case 5: return "Universal (Beta)";
    default: return "Off: 4:3";
    }
}
const char *ws_help(int plan, bool wii)
{
    if (wii)
        return "Wii games are 16:9 when Wii Widescreen is on in Porpoise's settings, as on a Wii.";
    switch (plan)
    {
    case 1:
        return "This game runs in true 16:9 with its widescreen code, so nothing pops in at the edges. The code is "
               "listed below.";
    case 4: return "This game's widescreen code needs Dolphin's widescreen hack as well, so both are on.";
    case 2:
        return "This game has a 16:9 option of its own: turn it on in the game's options, and the picture follows.";
    case 3: return "Dolphin's emulated widescreen hack is on. Things at the edges of the screen may pop in and out.";
    case 5:
        return "Porpoise's universal widescreen (beta) widens the game's own 3D view to 16:9 when it finds how, and "
               "uses Dolphin's widescreen hack when it doesn't. The menus and HUD may look stretched.";
    default: return "This game plays in 4:3. Widescreen, in the Video tab, changes that the next time it starts.";
    }
}

/* The schematic (assets/ui/controller-lines.png, tools/make-controller-art.py):
 * its crop of Zacksly's 4096x2160 drawing, and where each line ends there. */
constexpr float kLinesX0 = 780, kLinesY0 = 150, kLinesW = 2540, kLinesH = 1610;
struct LineEnd
{
    float x, y;
    bool left; /* the label goes to the left of the end */
};
enum EndId
{
    kEndL,
    kEndR,
    kEndTouch,
    kEndCreate,
    kEndOptions,
    kEndDPad,
    kEndFace,
    kEndLStick,
    kEndRStick,
    kEndCount,
};
constexpr LineEnd kEnds[kEndCount] = {
    {1023.36f, 229.18f, true},   {3072.64f, 230.91f, false}, {1885.13f, 237.25f, true},
    {969.74f, 544.5f, true},     {3126.26f, 544.5f, false},  {860.62f, 1015.03f, true},
    {3154.78f, 1015.03f, false}, {864.88f, 1430.69f, true},  {3231.12f, 1430.69f, false},
};

std::string slot_date(long long t)
{
    if (t <= 0)
        return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[48];
    std::strftime(buf, sizeof buf, "%Y-%m-%d  %H:%M", &tm);
    return buf;
}
/* A GBA's row, wherever Porpoise's tabs have it. */
bool is_gba_row(const Row &r)
{
    switch (r.kind)
    {
    case Kind::GbaReset:
    case Kind::GbaPick:
    case Kind::GbaLink:
    case Kind::GbaRom:
    case Kind::GbaSlot:
    case Kind::GbaDo:
    case Kind::GbaImport:
    case Kind::GbaCard:
        return true;
    default:
        return r.key && std::strncmp(r.key, "gba_", 4) == 0;
    }
}

/* Under Playtendoh the rows of a category: its Game without what the tiles and Quit do, every GBA row in one
 * category of its own. Standalone, Porpoise's tabs as they are. */
std::vector<Row> sheet_rows(int tab, Settings &p, bool wii, const Patches *patches)
{
    if (!ecam_bridge::request.valid)
        return rows_for(tab, p, wii, patches);
    if (tab == kEcAll)
        return rows_for(kTabGame, p, wii, patches);
    std::vector<Row> out;
    if (tab == kEcVolume)
    {
        for (const Row &r : rows_for(kTabAudio, p, wii, patches))
            if (r.key == std::string("volume"))
                out.push_back(r);
        return out;
    }
    if (tab == kEcButtons)
    {
        out.push_back({Kind::PadTest, "pad_test", "Controller test"});
        for (int gc = 0; gc < GcCount; ++gc)
        {
            Row r{Kind::PadButton, "", kPadButtonNames[gc]};
            r.index = gc;
            out.push_back(r);
        }
        out.push_back({Kind::PadReset, "pad_reset", "Reset to defaults"});
        return out;
    }
    if (tab == kEcGba)
    {
        for (const int from : {int(kTabGame), int(kTabVideo), int(kTabAudio)})
            for (const Row &r : rows_for(from, p, wii, patches))
                if (is_gba_row(r))
                {
                    out.push_back(r);
                    out.back().gap_before = false;
                }
        return out;
    }
    for (const Row &r : rows_for(tab, p, wii, patches))
    {
        /* (Customize Buttons: the Buttons category instead) */
        if (r.kind == Kind::Customize && !*r.key)
            continue;
        if (is_gba_row(r) || (tab == kTabAudio && r.key == std::string("volume")) || (tab == kTabGame && (r.kind == Kind::Resume || r.kind == Kind::Save || r.kind == Kind::Load ||
                                                  r.kind == Kind::Shot || r.kind == Kind::Library)))
            continue;
        out.push_back(r);
    }
    return out;
}

std::vector<int> sheet_categories(Settings &p, bool wii, int tabs)
{
    std::vector<int> cats;
    for (int i = 0; i < kTabCount; ++i)
        if (i != kTabGame)
        {
            cats.push_back(i);
            if (i == kTabGraphics) /* as the Wii U's: the volume on its own, its slider at once */
                cats.push_back(kEcVolume);
            /* the GameCube's buttons (a GameCube controller's in a Wii game too); the Wii Remote's are the hub's */
            if (i == kTabControls && (!wii || p.wii_controller == porpoise::pad::WiiGameCube))
                cats.push_back(kEcButtons);
        }
    Patches none;
    if (!sheet_rows(kEcGba, p, wii, &none).empty())
        cats.push_back(kEcGba);
    if (tabs > kTabCount)
        cats.push_back(kTabCount);
    cats.push_back(kEcFast);
    for (const Row &r : rows_for(kTabGame, p, wii, &none))
        if (r.kind == Kind::Disc)
            cats.push_back(kEcDisc);
    /* (no Start Over row: the Reset tile, as Playtendoh's other sides have it) */
    return cats;
}
} // namespace

/* ---- save-state slots ------------------------------------------------------------------- */

void App::menu_free_slots()
{
    free_badges();
    for (Texture *&t : menu_slot_tex_)
        if (t)
        {
            g_->free_texture(t);
            t = nullptr;
        }
}

void App::load_slots(const Game *game)
{
    menu_free_slots();
    if (!game)
        return;
    const std::string key = Library::key_of(*game);
    for (int i = 0; i < porpoise::states::kSlots; ++i)
    {
        const porpoise::states::Slot s = porpoise::states::slot(key, i);
        menu_slot_used_[i] = s.exists;
        menu_slot_time_[i] = s.time;
        menu_slot_tex_[i] = s.exists ? g_->texture_file(s.picture_path) : nullptr;
    }
}

void App::menu_state_done(MenuRequest::Kind kind, int slot, bool ok)
{
    menu_busy_ = {};
    menu_busy_handed_ = false;
    menu_confirm_ = false;
    if (kind == MenuRequest::Save)
    {
        menu_note_ = ok ? trf("Saved to slot {n}.", {{"n", std::to_string(slot + 1)}})
                        : tr("The game's state couldn't be saved.");
        load_slots(menu_game_);
        menu_slots_mode_ = 0; /* back to the list */
        sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
    }
    else
    {
        menu_note_ = ok ? trf("Slot {n} loaded.", {{"n", std::to_string(slot + 1)}})
                        : tr("That slot couldn't be loaded.");
        sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
        if (ok)
        {
            /* Straight back into the game, at that moment. */
            menu_slots_mode_ = 0;
            menu_closing_ = true;
            menu_answer_ = 1;
            if (settings_->reduced_motion)
                menu_anim_ = 0;
        }
    }
    menu_note_time_ = time_;
}

App::MenuRequest App::take_menu_request()
{
    /* Handed over once the "Saving..." it shows has been drawn, so the
     * player sees it while the game's state is taken; or after a moment
     * anyway, when the menu isn't being drawn (before the game's first
     * picture). */
    if (menu_busy_.kind == MenuRequest::None || menu_busy_handed_ ||
        (menu_busy_frames_ < 1 && time_ - menu_busy_start_ < 0.25))
        return {};
    menu_busy_handed_ = true;
    return menu_busy_;
}

/* ---- opening and input ------------------------------------------------------------------ */

void App::set_menu_discs(std::vector<int> own, int current, std::vector<std::pair<std::string, int>> others)
{
    g_menu_disc_numbers = std::move(own);
    g_menu_discs = int(g_menu_disc_numbers.size());
    g_menu_other_discs = std::move(others);
    g_menu_disc = current;
    /* A game on two discs: the menu opens on its other disc (Cross puts it
     * in, as in 3.1). Otherwise on the disc that's in, so Cross does nothing
     * until another one is picked. */
    const int count = g_menu_discs;
    if (count > 1 && current >= 0 && current < count)
        g_menu_disc_pick = (current + 1) % count;
    else
        g_menu_disc_pick = current >= 0 ? current : 0;
    g_menu_disc_chosen = -1;
}

int App::take_menu_disc()
{
    const int chosen = g_menu_disc_chosen;
    g_menu_disc_chosen = -1;
    return chosen;
}

void App::open_game_menu(Game *game, Settings *play)
{
    menu_game_ = game;
    menu_play_ = play;
    if (play)
        menu_draft_ = menu_base_ = *play; /* changes wait for Apply */
    menu_prompt_ = 0;
    menu_row_ = 0;
    menu_tab_ = kTabGame;
    ec_open_ = false;
    ec_shown_ = -1;
    ec_on_tiles_ = true;
    ec_tile_ = 0;
    ec_cat_ = 0;
    ec_scroll_ = ec_scroll_target_ = 0;
    ec_ring_ok_ = false;
    menu_anim_ = 0;
    menu_closing_ = false;
    menu_answer_ = 0;
    menu_note_.clear();
    menu_restart_armed_ = false;
    map_in_game_ = false;
    pad_listen_ = -1;
    pad_settling_ = false;
    ec_hold_ = -1;
    menu_slots_mode_ = 0;
    menu_confirm_ = false;
    menu_busy_ = {};
    menu_busy_handed_ = false;
    menu_borders_ = porpoise::borders::list();
    if (menu_wide_open_ == -2) /* the first pause of this run */
        menu_wide_open_ = play ? play->wide : -1;
    /* The Patches tab: the game's codes and their switches as it has them. */
    menu_cheats_.clear();
    menu_cheat_on_.clear();
    if (game && play && game->id.size() == 6 && !sys_dir_.empty())
    {
        /* <data>/cheats, where the player's own codes go, is theirs to make: an
         * empty one made here was taken for a broken online patch. */
        menu_cheats_ = cheats_for(sys_dir_, game->id, data_dir_ + "/cheats", game->platform == "Wii");
        for (const Cheat &c : menu_cheats_)
            menu_cheat_on_.push_back(c.default_on ? play->get(cheat_key(c, false)) != "1"
                                                  : play->get(cheat_key(c, true)) == "1");
        /* What's on first, the widescreen code in use at the top; the rest in
         * their lists' order. */
        std::vector<std::size_t> order(menu_cheats_.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        auto rank = [&](std::size_t i) {
            return !menu_cheat_on_[i] ? (menu_cheats_[i].own ? 2 : 3)
                   : widescreen::is_widescreen_code(menu_cheats_[i].name) ? 0
                                                                          : 1;
        };
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
        std::vector<Cheat> cheats;
        std::vector<char> on;
        for (std::size_t i : order)
        {
            cheats.push_back(menu_cheats_[i]);
            on.push_back(menu_cheat_on_[i]);
        }
        /* At most 80, after the sort: what's on (and the player's own) stays. */
        if (cheats.size() > 80)
        {
            cheats.resize(80);
            on.resize(80);
        }
        menu_cheats_.swap(cheats);
        menu_cheat_on_.swap(on);
    }
    load_slots(menu_game_);
    ach_focus_ = 0;
    ach_scroll_ = 0;
    load_achievements(game ? game->id : std::string(), true);
    prev_ = held_ = raw_held_ = raw_prev_ = 0xFFFFFFFFu; /* buttons still down from the shortcut don't count */
    mp_holds_seen_ = false; /* (the Move's Start, held to open it, isn't a hold to close it) */
    mp_steps_ = 0;
    sfx(Sound::DetailsFlip);
}

int App::update_game_menu(const Input &in, double dt)
{
    time_ += dt;
    ui_dt_ = dt > 0 && dt < 0.1 ? dt : 1.0 / 60.0;
    prev_ = held_;
    const std::uint32_t gc = gc_menu_bits(); /* the Switch 2's GameCube controller */
    follow_last_controller(in.held, in.wii, true, gc);
    held_ = in.held | in.wii | gc;
    raw_prev_ = raw_held_;
    raw_held_ = in.held;
    if (in.stick_y < -0.55f) held_ |= BtnUp;
    if (in.stick_y > 0.55f) held_ |= BtnDown;
    if (in.stick_x < -0.55f) held_ |= BtnLeft;
    if (in.stick_x > 0.55f) held_ |= BtnRight;
    const bool calm = settings_->reduced_motion;
    if (menu_closing_)
    {
        menu_anim_ = calm ? 0.0f : std::max(0.0f, menu_anim_ - float(dt) * 6.0f);
        if (menu_anim_ <= 0.0f)
            menu_free_slots();
        return menu_anim_ <= 0.0f ? menu_answer_ : 0;
    }
    menu_anim_ = calm ? 1.0f : std::min(1.0f, menu_anim_ + float(dt) * 5.0f);
    if (!menu_play_)
        return 1;

    /* The Move's pointer: point at a row and pull T; T held and swept up or
     * down goes along the rows; its Start held again closes the menu. */
    mp_frame(held_, !map_in_game_ && !ws_in_game_ && menu_busy_.kind == MenuRequest::None && !menu_slots_mode_ &&
                        !menu_prompt_,
             true, 72.0f);
    bool up = nav(BtnUp, rep_up_, dt), down = nav(BtnDown, rep_down_, dt);
    if (mp_live_)
    {
        const int step = mp_take_step();
        if (step > 0)
            down = true;
        else if (step < 0)
            up = true;
        const int tabs = kTabCount + (menu_has_achievements() ? 1 : 0);
        if (mp_click_ >= kMpTab && mp_click_ < kMpTab + tabs)
        {
            if (menu_tab_ != mp_click_ - kMpTab)
            {
                menu_tab_ = mp_click_ - kMpTab;
                menu_row_ = 0;
                menu_confirm_ = false;
                sfx(Sound::MovingTab);
            }
            return 0;
        }
        if (mp_moved_to_ >= kMpRow && mp_moved_to_ - kMpRow != menu_row_)
        {
            menu_row_ = mp_moved_to_ - kMpRow;
            menu_confirm_ = false;
            menu_restart_armed_ = false;
            sfx(Sound::MenuScroll);
        }
        if (mp_click_ >= kMpRow)
        {
            menu_row_ = mp_click_ - kMpRow;
            held_ |= BtnCross;
        }
    }
    if (mp_hold_)
        held_ |= BtnOptions | BtnTouch; /* as the shortcut that opened it: closes it */
    const bool left = nav(BtnLeft, rep_left_, dt), right = nav(BtnRight, rep_right_, dt);

    /* The mapping screen or the Wii Remote setup, over the game. */
    if (map_in_game_)
    {
        update_mapping(up, down, left, right);
        return 0;
    }
    if (ws_in_game_)
    {
        update_wii_setup(up, down, left, right);
        return 0;
    }
    /* Under Playtendoh, its Buttons category waiting for a press, or for the one taken to be let go: the menu
     * gets a still controller meanwhile, as Playtendoh's (app/buttons_menu.h). */
    if (pad_listen_ >= 0 || pad_settling_)
    {
        pad_capture();
        return 0;
    }
    /* Saving or loading: nothing to do but wait. */
    if (menu_busy_.kind != MenuRequest::None)
        return 0;
    /* Choosing a slot to save to, or to load. */
    if (menu_slots_mode_)
    {
        const bool saving = menu_slots_mode_ == 1;
        if (left || right)
        {
            menu_slot_ = (menu_slot_ + (left ? porpoise::states::kSlots - 1 : 1)) % porpoise::states::kSlots;
            menu_confirm_ = false;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle))
        {
            menu_slots_mode_ = 0;
            menu_confirm_ = false;
            sfx(Sound::MovingTab);
            return 0;
        }
        if (pressed(BtnCross))
        {
            if (!saving && !menu_slot_used_[menu_slot_])
            {
                menu_note_ = tr("That slot is empty.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
            }
            else if (saving && menu_slot_used_[menu_slot_] && !menu_confirm_)
            {
                menu_confirm_ = true; /* a second press replaces it */
                sfx(Sound::MovingTab);
            }
            else
            {
                menu_busy_.kind = saving ? MenuRequest::Save : MenuRequest::Load;
                menu_busy_.slot = menu_slot_;
                menu_busy_frames_ = 0;
                menu_busy_handed_ = false;
                menu_busy_start_ = time_;
                sfx(Sound::DetailsFlip);
            }
        }
        return 0;
    }

    /* L1 / R1: the tabs (Achievements last, when the game has a set). */
    const int tabs = kTabCount + (menu_has_achievements() ? 1 : 0);
    if (menu_tab_ >= tabs && !(ecam_ && menu_tab_ >= kEcGba))
        menu_tab_ = kTabGame;
    /* Under Playtendoh: its sheet's way. Quick tiles, then one list: categories, the open one's rows under it,
     * Up and Down going through them all, and Cross on another category opening that one instead. */
    if (ecam_ && !menu_prompt_)
    {
        /* The categories (the GBA's when this game has one, Achievements when it has a set), then Quit. */
        const bool wii_game = menu_game_ && menu_game_->platform == "Wii";
        const std::vector<int> cat_tabs = sheet_categories(menu_draft_, wii_game, tabs);
        const int cats = int(cat_tabs.size()) + 1;
        if (ec_open_)
        {
            /* In the open category's rows. Its ends lead out to the list. */
            Patches none;
            none.cheats = &menu_cheats_;
            const int count = menu_tab_ != kTabCount ? int(sheet_rows(menu_tab_, menu_draft_, menu_game_ && menu_game_->platform == "Wii", &none).size()) : 0;
            /* (Achievements has its own list: Up and Down are its, and only Circle or Left leave it) */
            const int ach_last = int(ach_.list.size()) - 1;
            if (pressed(BtnCircle) || (menu_tab_ != kTabCount && ((up && menu_row_ == 0) || (down && menu_row_ >= count - 1))) ||
                (menu_tab_ == kTabCount && (up && ach_focus_ <= 0 || down && ach_focus_ >= ach_last)))
            {
                const bool closing = pressed(BtnCircle);
                ec_open_ = false;
                ec_cat_ = down && !closing ? ec_shown_ + 1 : ec_shown_;
                if (closing)
                {
                    ec_shown_ = -1;
                    menu_tab_ = kTabGame;
                }
                menu_confirm_ = false;
                sfx(closing ? Sound::MovingTab : Sound::MenuScroll);
                return 0;
            }
        }
        else
        {
            /* A tile's or Quit's row of the Game category, as if chosen there. */
            const auto choose = [&](Kind kind) {
                menu_tab_ = kEcAll;
                ec_shown_ = -1;
                Patches none;
                none.cheats = &menu_cheats_;
                const std::vector<Row> game = rows_for(kTabGame, menu_draft_, menu_game_ && menu_game_->platform == "Wii", &none);
                for (std::size_t i = 0; i < game.size(); ++i)
                    if (game[i].kind == kind)
                        menu_row_ = int(i);
            };
            bool chosen = false;
            if (up || down)
            {
                menu_restart_armed_ = false;
                Patches none;
                none.cheats = &menu_cheats_;
                const int shown_tab = ec_shown_ >= 0 && ec_shown_ < int(cat_tabs.size()) ? cat_tabs[std::size_t(ec_shown_)] : kTabCount;
                const int shown_rows = ec_shown_ < 0 || ec_shown_ >= int(cat_tabs.size()) ? 0
                                       : shown_tab == kTabCount ? int(ach_.list.size())
                                                                : int(sheet_rows(shown_tab, menu_draft_, wii_game, &none).size());
                ec_hold_ = -1;
                if (ec_on_tiles_)
                {
                    ec_on_tiles_ = false;
                    ec_cat_ = down ? 0 : cats - 1;
                }
                else if (down && ec_cat_ == ec_shown_ && shown_rows > 0)
                {
                    menu_tab_ = shown_tab;
                    menu_row_ = 0;
                    ach_focus_ = 0;
                    ec_open_ = true;
                }
                else if (up && ec_cat_ == ec_shown_ + 1 && ec_shown_ >= 0 && shown_rows > 0)
                {
                    menu_tab_ = shown_tab;
                    menu_row_ = shown_rows - 1;
                    ach_focus_ = shown_rows - 1;
                    ec_open_ = true;
                }
                else if ((up && ec_cat_ == 0) || (down && ec_cat_ == cats - 1))
                    ec_on_tiles_ = true;
                else
                    ec_cat_ += down ? 1 : -1;
                sfx(Sound::MenuScroll);
                return 0;
            }
            if (ec_on_tiles_)
            {
                if (left || right)
                {
                    ec_tile_ = std::clamp(ec_tile_ + (right ? 1 : -1), 0, 3);
                    ec_hold_ = -1;
                    sfx(Sound::MenuScroll);
                }
                if (ec_tile_ == 3)
                {
                    /* Reset, held as Playtendoh's Reset tile is on its other sides: what isn't saved goes with it.
                     * (A screenshot is Touchpad + Square, Screenshot buttons in Playtendoh's Settings.) */
                    if (pressed(BtnCross))
                        ec_hold_ = time_;
                    else if (!(held_ & BtnCross))
                        ec_hold_ = -1;
                    if (ec_hold_ >= 0 && time_ - ec_hold_ >= 0.6)
                    {
                        ec_hold_ = -1;
                        ec_quit_held_ = true;
                        menu_restart_armed_ = true;
                        choose(Kind::Restart);
                        chosen = true;
                    }
                }
                else if (pressed(BtnCross))
                {
                    static constexpr Kind kTiles[3] = {Kind::Resume, Kind::Save, Kind::Load};
                    choose(kTiles[ec_tile_]);
                    chosen = true;
                }
            }
            else if (ec_cat_ == cats - 1)
            {
                /* Held, as Playtendoh's: what isn't saved goes with it */
                if (pressed(BtnCross))
                    ec_hold_ = time_;
                else if (!(held_ & BtnCross))
                    ec_hold_ = -1;
                if (ec_hold_ >= 0 && time_ - ec_hold_ >= 0.6)
                {
                    ec_hold_ = -1;
                    ec_quit_held_ = true;
                    choose(Kind::Library);
                    chosen = true;
                }
            }
            else if (cat_tabs[std::size_t(ec_cat_)] >= kEcFast)
            {
                /* A row of its own: it acts as it would among the others. */
                const int plain = cat_tabs[std::size_t(ec_cat_)];
                if (plain == kEcVolume)
                {
                    if (right || left)
                    {
                        ec_shown_ = -1;
                        menu_tab_ = kEcVolume;
                        menu_row_ = 0;
                        chosen = true;
                    }
                }
                else if (plain == kEcRestart)
                {
                    /* Held, as Quit: what isn't saved goes with it */
                    if (pressed(BtnCross))
                        ec_hold_ = time_;
                    else if (!(held_ & BtnCross))
                        ec_hold_ = -1;
                    if (ec_hold_ >= 0 && time_ - ec_hold_ >= 0.6)
                    {
                        ec_hold_ = -1;
                        ec_quit_held_ = true;
                        menu_restart_armed_ = true;
                        choose(Kind::Restart);
                        chosen = true;
                    }
                }
                else if (pressed(BtnCross) || right || left)
                {
                    choose(plain == kEcFast ? Kind::FastForward : Kind::Disc);
                    chosen = true;
                }
            }
            else if (pressed(BtnCross) || right || left)
            {
                /* Open it, closing the one that was; on it, or Left: closed. */
                const bool opening = ec_shown_ != ec_cat_ && !left;
                ec_shown_ = opening ? ec_cat_ : -1;
                if (opening)
                {
                    menu_tab_ = cat_tabs[std::size_t(ec_cat_)];
                    menu_row_ = 0;
                }
                else
                    menu_tab_ = kTabGame;
                sfx(Sound::DetailsFlip);
                return 0;
            }
            /* Circle closes what's open before it leaves the menu. */
            if (pressed(BtnCircle) && ec_shown_ >= 0)
            {
                ec_shown_ = -1;
                menu_tab_ = kTabGame;
                sfx(Sound::MovingTab);
                return 0;
            }
            /* Otherwise only the ways out: nothing else acts on a row from here. */
            if (!chosen && !pressed(BtnCircle) && !pressed(BtnSquare) &&
                !((held_ & (BtnOptions | BtnTouch)) == (BtnOptions | BtnTouch) && (prev_ & (BtnOptions | BtnTouch)) != (BtnOptions | BtnTouch)))
                return 0;
        }
    }
    else if (!menu_prompt_ && (pressed(BtnL1) || pressed(BtnR1)))
    {
        menu_tab_ = (menu_tab_ + (pressed(BtnL1) ? tabs - 1 : 1)) % tabs;
        menu_row_ = 0;
        menu_confirm_ = false;
        sfx(Sound::MovingTab);
        return 0;
    }
    if (menu_tab_ == kTabCount && !menu_prompt_)
    {
        /* Achievements: the list, fresh every couple of seconds. */
        if (menu_game_ && time_ - ach_loaded_at_ > 2.0)
            load_achievements(menu_game_->id, true);
        if (!menu_has_achievements())
        {
            menu_tab_ = kTabGame; /* the set went (the core lost it) */
            menu_row_ = 0;
            return 0;
        }
        if (achievements_nav(up, down))
            sfx(Sound::MenuScroll);
        const bool shortcut = (held_ & (BtnOptions | BtnTouch)) == (BtnOptions | BtnTouch) &&
                              (prev_ & (BtnOptions | BtnTouch)) != (BtnOptions | BtnTouch);
        if (pressed(BtnCircle) || shortcut)
        {
            sfx(Sound::DetailsFlip);
            menu_closing_ = true;
            menu_answer_ = 1;
            if (calm)
            {
                menu_anim_ = 0;
                menu_free_slots();
                return 1;
            }
        }
        return 0;
    }
    Settings &p = menu_draft_; /* the rows change it; Apply hands it to the game */
    Patches patches;
    patches.cheats = &menu_cheats_;
    g_menu_game_id = menu_game_ ? menu_game_->id : std::string();
    g_menu_gba_dir = data_dir_ + "/games/gba";
    std::vector<Row> rows = sheet_rows(menu_tab_, p, menu_game_ && menu_game_->platform == "Wii", &patches);
    const int count = int(rows.size());
    if (menu_prompt_)
        up = down = false; /* the question is answered first */
    if (up || down)
        menu_confirm_ = false; /* "press again to replace" is for the row it was said on */
    if (up)
    {
        menu_row_ = (menu_row_ + count - 1) % count;
        sfx(Sound::MenuScroll);
    }
    if (down)
    {
        menu_row_ = (menu_row_ + 1) % count;
        sfx(Sound::MenuScroll);
    }
    menu_row_ = std::clamp(menu_row_, 0, count - 1);
    if (up || down)
        menu_restart_armed_ = false;

    auto close = [&](int answer) {
        sfx(answer == 1 ? Sound::DetailsFlip : Sound::MovingTab);
        menu_closing_ = true;
        menu_answer_ = answer;
        if (calm)
        {
            menu_anim_ = 0;
            menu_free_slots();
        }
        return calm ? answer : 0;
    };
    const bool combo = (held_ & (BtnOptions | BtnTouch)) == (BtnOptions | BtnTouch) &&
                       (prev_ & (BtnOptions | BtnTouch)) != (BtnOptions | BtnTouch);
    if (menu_prompt_)
    {
        /* The question over the menu: left / right, Cross, Circle. */
        if ((left && menu_prompt_choice_ != 1) || (right && menu_prompt_choice_ != 0))
        {
            menu_prompt_choice_ = left ? 1 : 0;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle))
        {
            menu_prompt_ = 0; /* keep editing, or Later */
            sfx(Sound::MovingTab);
            return 0;
        }
        if (!pressed(BtnCross))
            return 0;
        const int prompt = menu_prompt_;
        menu_prompt_ = 0;
        if (prompt == 2)
        {
            if (menu_prompt_choice_ != 1)
                return 0; /* Later */
            /* Start the game afresh: back to the library for a moment, and
             * straight in again with the new settings read at its start. */
            menu_relaunch_ = true;
            return close(5);
        }
        const int answer = menu_prompt_answer_;
        if (menu_prompt_choice_ == 1)
        {
            apply_menu_pending();
            if (menu_prompt_ == 2)
                return 0; /* applied, and the game needs starting over: that question first */
        }
        else
            menu_draft_ = *menu_play_; /* Discard */
        return close(answer);
    }
    const bool waiting = !menu_draft_.changed_keys(*menu_play_).empty();
    if (pressed(BtnSquare) && waiting)
    {
        apply_menu_pending();
        return 0;
    }
    /* Triangle: this tab back to the settings every game uses (Oct 25). */
    if (pressed(BtnTriangle) && menu_tab_ != kEcButtons) /* (Buttons: its own reset row) */
    {
        const bool open[kGroupCount] = {g_group_open[0], g_group_open[1]};
        for (bool &o : g_group_open)
            o = true; /* (the rows in closed groups too) */
        Settings &d = menu_draft_;
        const std::vector<Row> all = rows_for(menu_tab_, d, menu_game_ && menu_game_->platform == "Wii", &patches);
        for (int i = 0; i < kGroupCount; ++i)
            g_group_open[i] = open[i];
        std::vector<std::string> keys;
        for (const Row &r : all)
            if (r.key && *r.key && (r.kind == Kind::Int || r.kind == Kind::Bool || r.kind == Kind::Border) &&
                std::strcmp(r.key, "p1_port") != 0)
                keys.push_back(r.key);
        if (menu_tab_ == kTabPatches)
            for (std::size_t i = 0; i < menu_cheats_.size(); ++i)
            {
                const Cheat &c = menu_cheats_[i];
                for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
                    d.forget(k);
                menu_cheat_on_[i] = c.default_on;
            }
        d.copy_keys(*settings_, keys);
        apply_menu_pending();
        if (!menu_prompt_)
        {
            menu_note_ = tr("This tab is back to the settings every game uses.");
            menu_note_time_ = time_;
        }
        return 0;
    }
    /* Leaving: what was changed is applied, no question (Oct 25: "if you
     * press Square, it gets approved"; closing does the same). Only a change
     * that needs the game started over still asks. */
    auto leave = [&](int answer) {
        if (!waiting)
            return close(answer);
        apply_menu_pending();
        if (menu_prompt_ == 2)
            return 0; /* Restart Now / Later first */
        return close(answer);
    };
    if (pressed(BtnCircle) || combo)
        return leave(1);

    const Row &row = rows[std::size_t(menu_row_)];
    const bool cross = pressed(BtnCross) || std::exchange(ec_quit_held_, false);
    const int dir = left ? -1 : (right || cross) ? 1 : 0;
    std::string key;
    switch (row.kind)
    {
    case Kind::Resume:
        if (cross)
            return close(1);
        break;
    case Kind::Library:
        if (cross)
            return leave(2);
        break;
    case Kind::Home:
        if (cross)
            return leave(3);
        break;
    case Kind::Disc:
    {
        /* Left / right: which disc (the game's own, then other games'); Cross
         * puts it in. */
        const int total = g_menu_discs + int(g_menu_other_discs.size());
        if ((left || right) && total > 1)
        {
            g_menu_disc_pick = (g_menu_disc_pick + (left ? total - 1 : 1)) % total;
            sfx(Sound::MenuScroll);
        }
        else if (cross && total > 0)
        {
            if (g_menu_disc_pick == g_menu_disc)
            {
                sfx(Sound::MenuScroll); /* in already */
                break;
            }
            g_menu_disc_chosen = g_menu_disc_pick;
            sfx(Sound::LaunchGame);
            return close(6); /* porpoise::core::kMenuChangeDisc */
        }
        break;
    }
    case Kind::GbaReset:
        if (cross)
        {
            porpoise::core::reset_gba(g_menu_gba);
            menu_note_ = tr("The GBA started over. The game goes on.");
            sfx(Sound::LaunchGame);
        }
        break;
    case Kind::GbaPick:
        if (left || right || cross)
        {
            /* The next GBA (ports with one), round. */
            for (int step = 1; step <= 4; ++step)
            {
                const int next = (g_menu_gba + (left ? 4 - step : step)) % 4;
                if (p.port_has_gba(next))
                {
                    g_menu_gba = next;
                    break;
                }
            }
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::GbaLink:
        if (cross || left || right)
        {
            porpoise::core::toggle_gba_link(g_menu_gba);
            menu_note_ = tr(porpoise::core::gba_connected(g_menu_gba) ? "The GBA's link cable is in again."
                                                                       : "The GBA's link cable is out.");
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::GbaRom:
    {
        const int n = int(g_menu_gba_roms.size()) + 1; /* none, then the folder's games */
        if (left || right)
        {
            g_menu_gba_rom_pick = (g_menu_gba_rom_pick + (left ? n - 1 : 1)) % n;
            sfx(Sound::MenuScroll);
        }
        else if (cross)
        {
            /* Put it in (none: take it out); the GBA starts over with it. Kept for the next start. */
            const int port = g_menu_gba;
            p.gba_rom[port] = g_menu_gba_rom_pick > 0 ? g_menu_gba_roms[std::size_t(g_menu_gba_rom_pick - 1)] : "";
            const std::string path = p.gba_rom[port].empty() ? "" : gba_port_dir(port) + "/" + p.gba_rom[port];
            porpoise::core::gba_request(port, p.gba_rom_beside[port] ? 3 : 2, path);
            menu_note_ = p.gba_rom[port].empty() ? tr("The GBA's game is out: the GBA starts with its BIOS alone.")
                                                 : trf("{game} is in the GBA.", {{"game", p.gba_rom[port]}});
            static const char *const kKeys[] = {"gba_rom1", "gba_rom2", "gba_rom3", "gba_rom4"};
            key = kKeys[port];
            sfx(Sound::LaunchGame);
        }
        break;
    }
    case Kind::GbaSlot:
        if (left || right || cross)
        {
            g_menu_gba_slot = (g_menu_gba_slot - 1 + (left ? 8 : 1)) % 9 + 1;
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::GbaDo:
        if (cross)
        {
            /* A state in the slot's file, or the save exported under the date and time. */
            const int port = g_menu_gba;
            mkdir(gba_exports(port).c_str(), 0777);
            std::string path;
            if (row.index == 7)
            {
                char when[32];
                const std::time_t now = std::time(nullptr);
                std::strftime(when, sizeof when, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
                path = gba_exports(port) + "/" + gba_stem(p, port) + " " + when + ".sav";
            }
            else
                path = gba_state_path(p, port, g_menu_gba_slot);
            struct stat st;
            if (row.index == 4 && stat(path.c_str(), &st) != 0)
            {
                menu_note_ = trf("Slot {n} is empty.", {{"n", std::to_string(g_menu_gba_slot)}});
                sfx(Sound::DetailsFlip);
                break;
            }
            porpoise::core::gba_request(port, row.index, path);
            menu_note_ = row.index == 4   ? trf("The GBA's state is loaded from slot {n}.", {{"n", std::to_string(g_menu_gba_slot)}})
                         : row.index == 5 ? trf("The GBA's state is saved to slot {n}.", {{"n", std::to_string(g_menu_gba_slot)}})
                                          : trf("The GBA's save is exported to {path}.", {{"path", path}});
            sfx(Sound::LaunchGame);
        }
        break;
    case Kind::GbaImport:
    {
        const int n = int(g_menu_gba_imports.size());
        if ((left || right) && n > 0)
        {
            g_menu_gba_import_pick = (g_menu_gba_import_pick + (left ? n - 1 : 1)) % n;
            sfx(Sound::MenuScroll);
        }
        else if (cross)
        {
            if (n == 0)
            {
                menu_note_ = trf("No saves for this game in {path}.", {{"path", gba_exports(g_menu_gba)}});
                sfx(Sound::DetailsFlip);
                break;
            }
            const std::string f = g_menu_gba_imports[std::size_t(std::clamp(g_menu_gba_import_pick, 0, n - 1))];
            porpoise::core::gba_request(g_menu_gba, 6, gba_exports(g_menu_gba) + "/" + f);
            menu_note_ = trf("{file} is the GBA's save now.", {{"file", f}});
            sfx(Sound::LaunchGame);
        }
        break;
    }
    case Kind::GbaCard:
    {
        const int n = int(g_menu_gba_cards.size());
        if ((left || right) && n > 0)
        {
            g_menu_gba_card_pick = (g_menu_gba_card_pick + (left ? n - 1 : 1)) % n;
            sfx(Sound::MenuScroll);
        }
        else if (cross && n > 0)
        {
            const std::string card = g_menu_gba_cards[std::size_t(std::clamp(g_menu_gba_card_pick, 0, n - 1))];
            porpoise::core::gba_request(g_menu_gba, 8, g_menu_gba_dir + "/ereader/" + card);
            menu_note_ = trf("{card} is scanned into the GBA's e-Reader.", {{"card", card}});
            sfx(Sound::LaunchGame);
        }
        break;
    }
    case Kind::Restart:
        if (cross)
        {
            /* Once to ask, again to do it: the game boots afresh. */
            if (menu_restart_armed_)
            {
                menu_restart_armed_ = false;
                return close(4);
            }
            menu_restart_armed_ = true;
            menu_note_ = tr("Press Cross again to start the game over. Unsaved progress is lost.");
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::Shot:
        if (cross)
        {
            menu_shot_ = true; /* the host takes it, between two of the menu's frames */
            sfx(Sound::LaunchGame);
        }
        break;
    case Kind::Save:
    case Kind::Load:
        if (cross)
        {
            const bool any = menu_slot_used_[0] || menu_slot_used_[1] || menu_slot_used_[2];
            if (row.kind == Kind::Load && !any)
            {
                menu_note_ = tr("No save states for this game yet.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
                break;
            }
            menu_slots_mode_ = row.kind == Kind::Save ? 1 : 2;
            menu_confirm_ = false;
            if (row.kind == Kind::Load && !menu_slot_used_[menu_slot_])
            {
                /* Start on the newest slot there is. */
                long long newest = -1;
                for (int i = 0; i < porpoise::states::kSlots; ++i)
                    if (menu_slot_used_[i] && menu_slot_time_[i] > newest)
                    {
                        newest = menu_slot_time_[i];
                        menu_slot_ = i;
                    }
            }
            sfx(Sound::DetailsFlip);
        }
        break;
    case Kind::FastForward:
        if (dir)
        {
            menu_ff_ = (menu_ff_ + (dir < 0 ? 2 : 1)) % 3;
            menu_change_ = "fast_forward";
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::Customize:
        if (cross && std::strcmp(row.key, "wii_recal") == 0)
            open_wii_setup_in_game(true);
        else if (cross && std::strcmp(row.key, "wii_setup") == 0)
            open_wii_setup_in_game(false);
        else if (cross)
            open_mapping_in_game();
        break;
    case Kind::SaveSetup:
    case Kind::UseSetup:
        if (left || right)
        {
            menu_setup_ = (menu_setup_ + (left ? setups::kCount - 1 : 1)) % setups::kCount;
            menu_confirm_ = false;
            sfx(Sound::MenuScroll);
        }
        else if (cross)
        {
            const setups::Setup su = setups::get(menu_setup_);
            const std::string n = std::to_string(menu_setup_ + 1);
            if (row.kind == Kind::SaveSetup)
            {
                if (su.exists && !menu_confirm_)
                {
                    menu_confirm_ = true;
                    menu_note_ = trf("Press Cross again to replace setup {n}.", {{"n", n}});
                    menu_note_time_ = time_;
                    sfx(Sound::MovingTab);
                    break;
                }
                menu_confirm_ = false;
                const bool ok = setups::save(menu_setup_, p, menu_game_ ? menu_game_->title : "");
                menu_note_ = ok ? trf("Saved as setup {n}.", {{"n", n}}) : tr("The setup couldn't be saved.");
                menu_note_time_ = time_;
                sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
            }
            else if (!su.exists)
            {
                menu_note_ = tr("That setup is empty.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
            }
            else if (menu_game_)
            {
                std::vector<std::string> keys = Settings::keys_in(game_settings_path(*menu_game_));
                if (setups::apply(menu_setup_, p, &keys))
                {
                    menu_borders_ = porpoise::borders::list();
                    menu_note_ = trf("Setup {n} is ready: Square applies it.", {{"n", n}});
                    menu_note_time_ = time_;
                    sfx(Sound::LaunchGame);
                }
            }
        }
        break;
    case Kind::Info:
    case Kind::PadTest:
        break;
    case Kind::PadButton:
        if (cross)
        {
            /* Wait for a press, five seconds; the Cross that began it let go first */
            pad_listen_ = row.index;
            pad_armed_ = false;
            pad_until_ = time_ + 5.0;
            menu_note_.clear();
            sfx(Sound::DetailsFlip);
        }
        break;
    case Kind::PadReset:
        /* Held, as Playtendoh's: a ring fills over 600 ms */
        if (pressed(BtnCross))
            ec_hold_ = time_;
        else if (!(held_ & BtnCross))
            ec_hold_ = -1;
        if (ec_hold_ >= 0 && time_ - ec_hold_ >= 0.6)
        {
            ec_hold_ = -1;
            pad_reset();
            sfx(Sound::LaunchGame);
        }
        break;
    case Kind::Group:
        if (cross || left || right)
        {
            bool &open = g_group_open[std::clamp(row.index, 0, kGroupCount - 1)];
            open = left ? false : right ? true : !open;
            sfx(open ? Sound::DetailsFlip : Sound::MenuScroll);
        }
        break;
    case Kind::Cheat:
        if (dir && menu_game_ && row.index >= 0 && row.index < int(menu_cheats_.size()))
        {
            /* As Game settings does it (ui_app_settings): named in
             * [<kind>_Enabled] when on and Dolphin doesn't turn it on by
             * itself, in [<kind>_Disabled] when off and Dolphin would. A
             * cheat (not a patch) needs Dolphin's cheats on for the game. */
            const Cheat &c = menu_cheats_[std::size_t(row.index)];
            const bool on = !menu_cheat_on_[std::size_t(row.index)];
            menu_cheat_on_[std::size_t(row.index)] = on;
            for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
                p.forget(k);
            const std::string k = on && !c.default_on ? cheat_key(c, true) : !on && c.default_on ? cheat_key(c, false) : "";
            if (!k.empty())
                p.set(k, "1");
            if (on && c.kind != "OnFrame")
                p.cheats = true; /* a cheat (not a patch) needs the game's cheats on (Apply saves it) */
            sfx(Sound::MenuScroll); /* waits for Apply; takes effect the next time the game starts */
        }
        break;
    case Kind::Int:
        if (dir)
        {
            const int n = int(row.values.size());
            int v = *row.iv + dir;
            if (cross && !left && !right && v > row.min + n - 1)
                v = row.min; /* Cross goes round */
            v = std::clamp(v, row.min, row.min + n - 1);
            if (v != *row.iv)
            {
                *row.iv = v;
                key = row.key;
            }
        }
        break;
    case Kind::Bool:
        if (dir)
        {
            *row.bv = !*row.bv;
            key = row.key;
        }
        break;
    case Kind::Border:
        if (dir && !menu_borders_.empty())
        {
            int i = 0;
            for (std::size_t b = 0; b < menu_borders_.size(); ++b)
                if (menu_borders_[b].name == p.border)
                    i = int(b);
            const int n = int(menu_borders_.size());
            i = (i + (dir < 0 ? n - 1 : 1)) % n;
            p.border = menu_borders_[std::size_t(i)].name;
            key = row.key;
        }
        break;
    }
    std::vector<std::string> also;
    bool save_key = true;
    if (key == "audio_preset")
    {
        /* A preset sets the sound rows; they are what is saved. */
        p.use_audio_preset(g_audio_preset);
        also = {"audio_pull", "audio_buffer", "audio_fill", "audio_stretch"};
        save_key = false;
    }
    if (key == "wide" && p.widescreen)
    {
        p.widescreen = false; /* 2.0's hack switch gives way to the choice */
        also.push_back("widescreen");
    }
    if (key == "wii_preset")
    {
        /* A preset brings its whole set-up. */
        if (p.wii_preset > 0 && p.use_wii_preset(p.wii_preset - 1))
            also = {"wii_controller", "wii_grip", "wii_pointer", "wii_speed", "wii_screen_x", "wii_screen_y",
                    "wii_smooth", "wii_reach"};
    }
    (void)save_key; /* the keys that differ are what Apply saves */
    if (!key.empty())
        sfx(Sound::MenuScroll); /* waits for Apply (Square, or when the menu closes) */
    /* What you see change (a border, a screen filter) is used and saved at once. */
    if (!key.empty() && applies_at_once(key) && !menu_draft_.changed_keys(*menu_play_).empty())
    {
        std::vector<std::string> only;
        for (const std::string &k : menu_draft_.changed_keys(*menu_play_))
            if (applies_at_once(k))
                only.push_back(k);
        apply_menu_keys(only, true);
    }
    return 0;
}

namespace
{
/* In a game: changes that take effect only when it starts again. */
bool needs_game_restart(const std::string &key)
{
    return key == "wide" || key == "shader_mode" || key == "dsp_accurate" || key == "audio_pull" ||
           key.rfind("gfx_", 0) == 0 || key.rfind("dolphin.", 0) == 0 || key == "cheats" || key == "fast_float" || key == "accurate_fma" ||
           key == "wait_shaders";
}
} // namespace

void App::apply_menu_pending()
{
    if (menu_play_)
        apply_menu_keys(menu_draft_.changed_keys(*menu_play_), false);
}

void App::apply_menu_keys(const std::vector<std::string> &pending, bool quiet)
{
    if (!menu_play_ || pending.empty())
        return;
    menu_play_->copy_keys(menu_draft_, pending);
    if (menu_game_)
    {
        /* Saved as this game's own settings, so they stick next time. */
        std::vector<std::string> keys = Settings::keys_in(game_settings_path(*menu_game_));
        for (const std::string &k : pending)
            if (std::find(keys.begin(), keys.end(), k) == keys.end() && k != "p1_port")
                keys.push_back(k); /* (Player 1's Port from here is for this time only) */
        /* A code switched on needs the game's cheats on, saved: this run may
         * have them on only for its widescreen code. */
        const bool code = std::any_of(pending.begin(), pending.end(),
                                      [](const std::string &k) { return k.rfind("dolphin.", 0) == 0; });
        if (code && menu_play_->cheats && std::find(keys.begin(), keys.end(), "cheats") == keys.end())
            keys.push_back("cheats");
        /* A code's switch back to its default is no key at all. */
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [&](const std::string &k) {
                                      return k.rfind("dolphin.", 0) == 0 && menu_play_->get(k).empty();
                                  }),
                   keys.end());
        mkdir((data_dir_ + "/game-settings").c_str(), 0777);
        menu_play_->save_keys(game_settings_path(*menu_game_), keys);
    }
    /* The host applies each in turn (take_menu_change). */
    std::string restart;
    bool wii = false;
    for (const std::string &k : pending)
    {
        if (k.rfind("wii_", 0) == 0)
            wii = true;
        else if (k.rfind("dolphin.", 0) != 0)
            menu_changes_.push_back(k);
        if (needs_game_restart(k))
            restart = k;
    }
    if (wii)
        menu_changes_.push_back("wii_setup"); /* the whole Wii Remote set-up, pointer included */
    if (quiet)
        return; /* the draft keeps any other change, still waiting */
    menu_draft_ = *menu_play_;
    sfx(Sound::LaunchGame);
    if (!restart.empty())
    {
        menu_prompt_ = 2;
        menu_prompt_choice_ = 1;
    }
    else
    {
        menu_note_ = tr("Applied.");
        menu_note_time_ = time_;
    }
}

void App::set_sound_pulled(bool pulled)
{
    g_sound_pulled = pulled;
}

/* ---- drawing ---------------------------------------------------------------------------- */

/* The controller with a line out to each button, and at each line's end the
 * GameCube button it is. w is the drawing's width; the labels go beside it. */
void App::draw_controller_lines(float x, float y, float w, const Mapping &m)
{
    Gfx &g = *g_;
    if (!lines_art_tried_)
    {
        lines_art_tried_ = true;
        lines_art_ = g.texture_file(g.asset_dir() + "/ui/controller-lines.png", 2048);
    }
    if (!lines_art_)
        return;
    const float k = w / kLinesW, h = kLinesH * k;
    g.image(lines_art_, x, y, w, h, g.tone(rgba(0xBFD8FF, 0.95f)));

    int on_control[CtlCount];
    std::fill(std::begin(on_control), std::end(on_control), -1);
    for (int gc = 0; gc < GcCount; ++gc)
        if (m.control[gc] >= 0 && m.control[gc] < CtlCount)
            on_control[int(m.control[gc])] = gc;

    /* One label line: a DualSense icon and what it is (a chip, or a word). */
    struct Item
    {
        Icon icon;
        int control; /* -1: no chip */
        std::string text;
    };
    const float ih = 30, chip_h = 24, gap = 6;
    auto item_w = [&](const Item &it) {
        float iw = ih + gap;
        if (!it.text.empty())
            iw += g.measure(Font::SemiBold, ts(18), it.text);
        else
            iw += gc_chip_width(it.control >= 0 ? on_control[it.control] : -1, chip_h);
        return iw;
    };
    auto draw_group = [&](EndId end, const std::vector<Item> &items) {
        const LineEnd &e = kEnds[end];
        const float ex = x + (e.x - kLinesX0) * k, ey = y + (e.y - kLinesY0) * k;
        float iy = ey - (float(items.size()) - 1) * ih * 0.5f;
        if (end == kEndTouch)
            iy = ey - 6; /* the touch pad's line ends going up: sit beside its end */
        for (const Item &it : items)
        {
            const float iw = item_w(it);
            float ix = e.left ? ex - 10 - iw : ex + 10;
            g.icon(it.icon, ix + ih * 0.5f, iy, ih + 6, rgba(0xDCE8FF));
            ix += ih + gap;
            if (!it.text.empty())
                g.text_mid(Font::SemiBold, ts(18), ix, iy, kLavender, Align::Left, it.text);
            else
            {
                const int gc = it.control >= 0 ? on_control[it.control] : -1;
                draw_gc_chip(gc, ix + gc_chip_width(gc, chip_h) * 0.5f, iy, chip_h);
            }
            iy += ih;
        }
    };
    draw_group(kEndL, {{Icon::L1, CtlL1, ""}, {Icon::L2, CtlL2, ""}});
    draw_group(kEndR, {{Icon::R1, CtlR1, ""}, {Icon::R2, CtlR2, ""}});
    draw_group(kEndTouch, {{Icon::TouchPad, CtlTouch, ""}});
    draw_group(kEndCreate, {{Icon::Create, -1, ""}});
    draw_group(kEndOptions, {{Icon::Options, CtlOptions, ""}});
    draw_group(kEndDPad, {{Icon::DPadUp, CtlUp, ""}, {Icon::DPadDown, CtlDown, ""}, {Icon::DPadLeft, CtlLeft, ""},
                          {Icon::DPadRight, CtlRight, ""}});
    draw_group(kEndFace, {{Icon::Triangle, CtlTriangle, ""}, {Icon::Square, CtlSquare, ""},
                          {Icon::Circle, CtlCircle, ""}, {Icon::Cross, CtlCross, ""}});
    draw_group(kEndLStick, {{Icon::LStick, -1, tr("Control Stick")}, {Icon::L3, CtlL3, ""}});
    draw_group(kEndRStick, {{Icon::RStick, -1, tr("C-Stick")}, {Icon::R3, CtlR3, ""}});
}

bool App::wii_side_colour() const
{
    return menu_game_ && menu_game_->platform == "Wii";
}

void App::draw_game_menu(double time)
{
    apply_look();
    set_skin();
    {
        /* Over the game: liquid glass would bend the menus' room, which
         * isn't behind it here. */
        Look over_game = g_->look();
        /* The Porpoise look's thin glass has a kind for over a picture (3). */
        over_game.panel_style = glass() ? 3 : 0;
        g_->set_look(over_game);
    }
    if (!menu_play_)
        return;
    Gfx &g = *g_;
    /* The Revolution look: the menu in white over the game. */
    struct Tone
    {
        Gfx &g;
        ~Tone() { g.set_tone(false); }
    } tone{g};
    struct Pointer
    {
        App &app;
        ~Pointer() { app.draw_move_pointer(); } /* over the menu, whichever way it ends */
    } pointer{*this};
    g.set_tone(revolution());
    if (ws_in_game_)
    {
        g.set_layer();
        draw_wii_setup(time);
        return;
    }
    if (map_in_game_)
    {
        g.set_layer();
        draw_mapping(time);
        return;
    }
    /* (Light, too: the words in ink on the light glass - they were white on it.) */
    g.set_tone(revolution() || (glass() && settings_->ui_mode == 2));
    const float t = ease_out(menu_anim_);
    Settings &p = menu_draft_; /* the rows change it; Apply hands it to the game */
    const std::vector<std::string> waiting = menu_draft_.changed_keys(*menu_play_);
    const Color kPending = rgba(0xFFC857);
    g.set_layer();
    if (ecam_)
        g.panel_across(0, 0, 1920, 1080, rgba(0x04060C, (200.0f / 255.0f) * t), rgba(0x04060C, (48.0f / 255.0f) * t));
    else
        g.panel(0, 0, 1920, 1080, rgba(0x03050A, 0.55f * t), 1, 0);

    /* The panel slides in. Playtendoh's sheet (its app/side_menu.h): from the left, where it is, how wide,
     * near-black under a fine edge, in the side's colour. 4.5's every other look: its side menu, on the right,
     * the same place and size, in the theme's own colors (glass_pill, skin_select). */
    const bool gl = !ecam_;
    /* Playtendoh 6.0's sheet: full height, against the screen's left edge */
    const float w = ecam_ ? 700.0f : 720.0f, x = ecam_ ? 0.0f : 1892.0f - w, y = ecam_ ? 0.0f : 28.0f, h = ecam_ ? 1080.0f : 1024.0f;
    const float slide = (ecam_ ? -1.0f : 1.0f) * (w + 80) * (1.0f - t);
    const Color kAccent = rgba(wii_side_colour() ? 0x3FCFF0 : 0x6F6CFF);
    g.set_layer(slide, 0, 0.3f + 0.7f * t);
    if (ecam_)
    {
        /* glass, darker at its foot, the side's colour a glow in its top corner; its edge a hairline in that
         * colour with a soft shadow beyond it (side_menu.h) */
        g.panel(x, y, w, h, rgba(0x0C1120, 238.0f / 255.0f), 0.62f, 0);
        g.blob(x + 180, y + 40, 760, 420, with_alpha(kAccent, 0.12f));
        g.panel_across(x + w, y, 36, h, rgba(0x000000, 0.31f), rgba(0x000000, 0.0f));
        g.line(x + w, y, x + w, y + h, 1.5f, with_alpha(kAccent, 0.35f));
    }
    else if (gl)
        glass_pill(x, y, w, h, 30, 1.15f);
    else
    {
        Glass face;
        face.tint = rgba(0x13308A, 0.88f);
        face.rim = rgba(0x8BD9FF);
        face.radius = kR;
        face.rim_w = 2.2f;
        face.glow = 12;
        face.phase = 0.2f;
        glass_block(g, x + w * 0.5f, y + h * 0.5f, w, h, 22, 0, 0, 36, face);
    }

    /* Playtendoh's header: the cover in its box, the name, the system's and Running's chips. */
    float hy = y + 34;
    if (ecam_ && menu_game_)
    {
        Texture *cover = cover_of(*menu_game_);
        const float box = 124, cx = 64, cy = 72;
        float cw = box * 0.75f, ch = box;
        if (cover && cover->height > 0)
        {
            const float fit_k = std::min(box / float(cover->width), box / float(cover->height));
            cw = float(cover->width) * fit_k;
            ch = float(cover->height) * fit_k;
            g.blob(cx + cw * 0.5f, cy + ch + 6, cw * 1.3f, 30, rgba(0x000000, 0.45f)); /* a soft shadow under it */
            g.image(cover, cx, cy, cw, ch, kWhite, 12);
            g.panel(cx, cy, cw, ch, kClear, 1, 12, rgba(0xFFFFFF, 0.15f), 1);
        }
        else
            g.panel(cx, cy, cw, ch, rgba(0xFFFFFF, 0.047f), 1, 12);
        const float tx = cx + cw + 26, tw = x + w - 40 - tx;
        const Color text = rgba(0xF5F7FB);
        /* the chips first, the name under them (side_menu.h, 6.0) */
        const float chips_y = cy + 4 - 50;
        const std::string sys = menu_game_->platform == "Wii" ? "WII" : "GAMECUBE";
        const float chip = 24 + 22 + g.measure(Font::SemiBold, 18, sys) + 1.5f * float(sys.size());
        g.panel(tx, chips_y + 50, chip, 34, with_alpha(kAccent, 0.16f), 1, 10);
        g.panel(tx + 12, chips_y + 60, 14, 14, kAccent, 1, 4);
        g.text_mid(Font::SemiBold, 18, tx + 34, chips_y + 67, kAccent, Align::Left, sys, 1.5f);
        const std::string running = tr("RUNNING");
        const float chip2 = 24 + g.measure(Font::SemiBold, 18, running) + 1.5f * float(running.size());
        g.panel(tx + chip + 8, chips_y + 50, chip2, 34, rgba(0xFFFFFF, 22.0f / 255.0f), 1, 10);
        g.text_mid(Font::SemiBold, 18, tx + chip + 20, chips_y + 67, rgba(0xF5F7FB, 179.0f / 255.0f), Align::Left, running, 1.5f);
        g.text_mid(Font::SemiBold, 32, tx, cy + 64, text, Align::Left, fit(g, Font::SemiBold, 32, menu_game_->title, tw));
        if (!ecam_details_.empty())
            g.text_mid(Font::Regular, 20, tx, cy + 104, rgba(0xF5F7FB, 117.0f / 255.0f), Align::Left, fit(g, Font::Regular, 20, ecam_details_, tw));
        hy = 232;
    }
    if (!ecam_ && menu_game_)
    {
        Texture *cover = cover_of(*menu_game_);
        float cw = gl ? 64.0f : 84.0f, ch = gl ? 90.0f : 118.0f;
        const float cx = x + 40, cy = hy;
        if (cover && gl && cover->height > 0)
        {
            /* The whole cover, as it is - not cropped into a rounded box - with a
             * soft shadow under it. */
            ch = 104;
            cw = ch * float(cover->width) / float(cover->height);
            const bool toned = g.toned();
            g.set_tone(false);
            g.blob(cx + cw * 0.5f, cy + ch + 4, cw * 1.25f, 22, rgba(0x000000, 0.35f));
            Corner c[4] = {{cx, cy, 1}, {cx + cw, cy, 1}, {cx + cw, cy + ch, 1}, {cx, cy + ch, 1}};
            g.quad3d(cover, c, cw, ch, kWhite, 2.0f, false, false);
            gloss_over(g, c, cw, ch, 2.0f, 0.2f, 0.6f);
            g.set_tone(toned);
        }
        else if (cover)
        {
            float uv[4];
            cover_uv(cover, cw, ch, uv);
            Corner c[4] = {{cx, cy, 1}, {cx + cw, cy, 1}, {cx + cw, cy + ch, 1}, {cx, cy + ch, 1}};
            g.quad3d(cover, c, cw, ch, kWhite, kR * 0.6f, false, false, uv);
            gloss_over(g, c, cw, ch, kR * 0.6f, 0.2f);
        }
        else
            g.panel(cx, cy, cw, ch, kTileFill, 0.6f, kR * 0.6f, kEdge, 1.6f);
        if (gl)
        {
            g.text_mid(Font::ExtraBold, ts(14), cx + cw + 24, cy + ch * 0.5f - 22, rgba(0xFFFFFF, 0.6f), Align::Left,
                       tr("PAUSED"), 1.5f);
            float ly = cy + ch * 0.5f + 10;
            for (const std::string &l : wrap(g, Font::ExtraBold, ts(28), menu_game_->title, w - cw - 100, 2))
            {
                g.text_mid(Font::ExtraBold, ts(28), cx + cw + 24, ly, kWhite, Align::Left, l);
                ly += 34;
            }
            hy = cy + ch + 14;
        }
        else
        {
            g.text_mid(Font::SemiBold, ts(21), cx + cw + 28, cy + 22, kCyan, Align::Left, tr("PAUSED"), 3.0f);
            const auto lines = wrap(g, Font::Bold, ts(32), menu_game_->title, w - cw - 110, 2);
            float ly = cy + 62;
            for (const std::string &l : lines)
            {
                g.text_mid(Font::Bold, ts(32), cx + cw + 28, ly, kWhite, Align::Left, l);
                ly += 40;
            }
            hy = cy + ch + 24;
        }
    }

    /* The tabs, L1 and R1 at their sides. */
    const int tabs = kTabCount + (menu_has_achievements() ? 1 : 0);
    if (!ecam_)
    {
        const float ty = hy + 28, tab_h = 50;
        float size = gl ? ts(19) : tabs > kTabCount ? ts(22) : ts(24);
        const float pad = gl ? 22.0f : tabs > kTabCount ? 28.0f : 36.0f;
        const char *names[kTabCount + 1] = {kTabNames[0], kTabNames[1], kTabNames[2], kTabNames[3],
                                            kTabNames[4], kTabNames[5], "Achievements"};
        float widths[kTabCount + 1], total = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            total = 0;
            for (int i = 0; i < tabs; ++i)
            {
                widths[i] = g.measure(Font::Bold, size, tr(names[i])) + pad;
                total += widths[i];
            }
            /* Longer names (some languages'): smaller, to fit between L1 and R1. */
            const float room = w - (gl ? 56.0f : 150.0f);
            if (total <= room)
                break;
            size *= room / total;
        }
        float tx = x + (w - total) * 0.5f;
        if (!gl) /* (the glass menu's tabs fill its width: L1 / R1 needn't be drawn) */
        {
            g.glyph(Glyph::L1, tx - 44.0f, ty, 40.0f, rgba(0xE8F0FF));
            g.glyph(Glyph::R1, tx + total + 44.0f, ty, 40.0f, rgba(0xE8F0FF));
        }
        for (int i = 0; i < tabs; ++i)
        {
            const bool on = i == menu_tab_;
            if (on && gl)
            {
                /* As the library's filters: the one you're on in bold over a line. */
                const bool solid = g.solid();
                g.set_solid(true);
                g.panel(tx + 10, ty + 18, widths[i] - 20, 3, rgba(0xFFFFFF, 0.9f), 1, 1.5f);
                g.set_solid(solid);
            }
            else if (on)
                g.panel(tx, ty - tab_h * 0.5f, widths[i], tab_h, rgba(0x1F63F0), 0.62f, kR, rgba(0x7FD9FF), 1.6f, 6,
                        0.35f);
            g.text_mid(on ? Font::Bold : Font::SemiBold, size, tx + widths[i] * 0.5f, ty,
                       on ? kWhite : gl ? rgba(0xFFFFFF, 0.65f) : kSoft, Align::Center, tr(names[i]));
            mp_hit(tx + slide, ty - tab_h * 0.5f, widths[i], tab_h, kMpTab + i);
            tx += widths[i];
        }
        hy = ty + tab_h * 0.5f + 16;
    }
    if (!ecam_)
    {
        g.panel(x + 36, hy, w - 72, gl ? 1.0f : 1.5f, gl ? rgba(0xFFFFFF, 0.16f) : rgba(0x6F8FE0, 0.5f), 1, 0);
        hy += 10;
    }

    if (menu_tab_ == kTabCount && tabs > kTabCount && !ecam_)
    {
        /* Achievements: the list here, the one in focus beside the menu. */
        draw_achievement_list(x + 48, hy + 18, w - 96, y + h - 84 - (hy + 18));
        g.set_layer(0, 0, t);
        draw_achievement_card(gl ? 80.0f : 1000.0f, 120, 840, 840);
        g.set_layer(slide, 0, 0.3f + 0.7f * t);
        const float py = y + h - 50, size = ts(22);
        float px = x + 52;
        for (const auto &pr : std::vector<std::pair<Glyph, std::string>>{{Glyph::DPad, tr("Browse")},
                                                                         {Glyph::Circle, tr("Resume")}})
        {
            g.glyph(pr.first, px + 15, py, 30, kWhite);
            px += 42;
            px += g.text_mid(Font::SemiBold, size, px, py, kWhite, Align::Left, pr.second) + 40;
        }
        const std::string tabs_label = ecam_ ? std::string() : tr("Tabs");
        const float tw = g.measure(Font::SemiBold, size, tabs_label), right = x + w - 48;
        if (!gl)
            g.text_mid(Font::SemiBold, size, right, py, kWhite, Align::Right, tabs_label);
        if (g.has_icons() && !gl)
        {
            g.glyph(Glyph::R1, right - tw - 30, py, 30, kWhite);
            g.glyph(Glyph::L1, right - tw - 76, py, 30, kWhite);
        }
        g.set_layer();
        return;
    }

    /* Rows. */
    Patches patches;
    patches.cheats = &menu_cheats_;
    const bool wii = menu_game_ && menu_game_->platform == "Wii";
    g_menu_game_id = menu_game_ ? menu_game_->id : std::string();
    g_menu_gba_dir = data_dir_ + "/games/gba";
    const std::vector<Row> rows = sheet_rows(menu_tab_, p, wii, &patches);
    if (rows.empty() && !ecam_)
    {
        g.set_layer();
        return;
    }
    /* What a row shows beside its label, and whether Left and Right change it. */
    const auto row_value = [&](const Row &row, std::string &value, bool &arrows) {
        switch (row.kind)
        {
        case Kind::PadTest:
        {
            /* what the DualSense sends now, or a dash */
            const int c = held_control(raw_held_ & 0xFFFFu);
            value = c >= 0 ? tr(kPadControlNames[c]) : std::string("-");
            arrows = false;
            break;
        }
        case Kind::PadButton:
        {
            const int c = p.mapping().control[std::clamp(row.index, 0, GcCount - 1)];
            value = pad_listen_ == row.index ? tr("Press a button") : c >= 0 && c < CtlCount ? tr(kPadControlNames[c]) : tr("Not set");
            arrows = false;
            break;
        }
        case Kind::PadReset:
            arrows = false;
            break;
        case Kind::Info:
            arrows = false;
            value = row.key == std::string("ws_status") ? tr(ws_value(p.ws_plan, wii)) : tr("None");
            break;
        case Kind::Cheat:
            value = tr(row.index >= 0 && row.index < int(menu_cheat_on_.size()) && menu_cheat_on_[std::size_t(row.index)]
                           ? "On"
                           : "Off");
            break;
        case Kind::FastForward:
            value = menu_ff_ == 0 ? tr("Off") : menu_ff_ == 1 ? "2x" : "4x";
            break;
        case Kind::GbaPick:
            value = trf("GBA {n}", {{"n", std::to_string(g_menu_gba + 1)}});
            arrows = true;
            break;
        case Kind::GbaLink:
            value = tr(porpoise::core::gba_connected(g_menu_gba) ? "Connected" : "Disconnected");
            break;
        case Kind::GbaRom:
            value = g_menu_gba_rom_pick > 0 && g_menu_gba_rom_pick <= int(g_menu_gba_roms.size())
                        ? g_menu_gba_roms[std::size_t(g_menu_gba_rom_pick - 1)]
                        : tr("None (BIOS)");
            arrows = !g_menu_gba_roms.empty();
            break;
        case Kind::GbaSlot:
        {
            value = trf("Slot {n}", {{"n", std::to_string(g_menu_gba_slot)}});
            struct stat st;
            if (menu_draft_.port_has_gba(g_menu_gba) &&
                stat(gba_state_path(menu_draft_, g_menu_gba, g_menu_gba_slot).c_str(), &st) != 0)
                value += "  \xE2\x80\xA2  " + tr("Empty");
            arrows = true;
            break;
        }
        case Kind::GbaImport:
        {
            /* Its date and time (the game's name is the same for all: left out), and where it is in
             * the list, newest first: "2026-11-01 14:05:33  •  2 of 5". */
            const int n = int(g_menu_gba_imports.size());
            if (n == 0)
            {
                value = tr("None");
                break;
            }
            const int pick = std::clamp(g_menu_gba_import_pick, 0, n - 1);
            std::string name = g_menu_gba_imports[std::size_t(pick)];
            const std::string stem = gba_stem(menu_draft_, g_menu_gba);
            if (name.size() >= stem.size() && name.compare(0, stem.size(), stem) == 0)
                name = name.substr(stem.size());
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".sav") == 0)
                name.resize(name.size() - 4);
            while (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
            /* "2026-11-01 14-05-33" (a file name can't have ':'): the time as a time. */
            if (name.size() == 19 && name[10] == ' ' && name[13] == '-' && name[16] == '-')
                name[13] = name[16] = ':';
            value = (name.empty() ? tr("Undated") : name) + "  \xE2\x80\xA2  " +
                    trf("{i} of {n}", {{"i", std::to_string(pick + 1)}, {"n", std::to_string(n)}});
            arrows = n > 1;
            break;
        }
        case Kind::GbaCard:
            value = g_menu_gba_cards.empty()
                        ? ""
                        : g_menu_gba_cards[std::size_t(std::clamp(g_menu_gba_card_pick, 0, int(g_menu_gba_cards.size()) - 1))];
            arrows = g_menu_gba_cards.size() > 1;
            break;
        case Kind::Disc:
        {
            const int pick = g_menu_disc_pick;
            const auto disc = [](int number) { return trf("Disc {n}", {{"n", std::to_string(number + 1)}}); };
            if (pick < g_menu_discs)
                value = disc(g_menu_disc_numbers[std::size_t(pick)]);
            else if (pick - g_menu_discs < int(g_menu_other_discs.size()))
            {
                const auto &[title, number] = g_menu_other_discs[std::size_t(pick - g_menu_discs)];
                value = title;
                if (number > 0)
                    value += "  \xE2\x80\xA2  " + disc(number);
            }
            if (pick == g_menu_disc) /* (first, so a long title doesn't cut it off) */
                value = tr("In the Drive") + "  \xE2\x80\xA2  " + value;
            arrows = g_menu_discs + int(g_menu_other_discs.size()) > 1;
            break;
        }
        case Kind::SaveSetup:
        case Kind::UseSetup:
        {
            const setups::Setup su = setups::get(menu_setup_);
            value = trf("Setup {n}", {{"n", std::to_string(menu_setup_ + 1)}});
            if (su.exists && !su.from.empty())
                value += "  \xE2\x80\xA2  " + su.from;
            else if (!su.exists)
                value += "  \xE2\x80\xA2  " + tr("Empty");
            break;
        }
        case Kind::Int:
            value = tr(row.values[std::size_t(std::clamp(*row.iv - row.min, 0, int(row.values.size()) - 1))]);
            /* Widescreen: what the setting gives this game, as it runs now. */
            if (row.key == std::string("wide") && *row.iv == menu_wide_open_)
                value += "  \xE2\x80\xA2  " + tr(ws_value(p.ws_plan, wii));
            if (row.key == std::string("button_layout") && *row.iv >= LayoutOwn)
                value = trf("My Layout {n}", {{"n", std::to_string(*row.iv - LayoutOwn + 1)}});
            break;
        case Kind::Bool:
            value = tr(*row.bv ? "On" : "Off");
            break;
        case Kind::Border:
            value = tr("None");
            for (const porpoise::borders::Border &b : menu_borders_)
                if (b.name == p.border)
                    value = b.built_in ? tr(b.label) : b.label;
            break;
        case Kind::Group: /* (drawn by the list: a chevron) */
            arrows = false;
            break;
        default:
            arrows = false;
            break;
        }
    };
    if (ecam_)
    {
    /* side_menu.h's, 6.0 */
    const float kInset = 64, kInner = 576, kTilesTop = 232, kTileH = 132, kGap = 12, kRowH = 56, kPitch = 60;
    const float list_top = kTilesTop + kTileH + 24, list_bottom = y + h - 164, footer = y + h - 100;
    const double dt = std::clamp(time - ec_time_, 0.0, 0.1);
    ec_time_ = time;
    /* menu_canvas.h's Side(): the same values, its 0-255 alphas as they are */
    const Color text_c = rgba(0xF5F7FB), copy = rgba(0xF5F7FB, 179.0f / 255.0f), faint = rgba(0xF5F7FB, 117.0f / 255.0f),
                row_fill = rgba(0xFFFFFF, 14.0f / 255.0f), edge = rgba(0xFFFFFF, 26.0f / 255.0f);
    /* Its Chevron: two thin strokes. point: 0 right, 1 left, 2 down */
    const auto chevron = [&](float cx_, float cy_, Color c, int point) {
        const float r = 6, thick = 2.2f;
        if (point == 2)
        {
            g.line(cx_ - r, cy_ - r / 2, cx_, cy_ + r / 2, thick, c);
            g.line(cx_, cy_ + r / 2, cx_ + r, cy_ - r / 2, thick, c);
            return;
        }
        const float side = point == 1 ? -1.0f : 1.0f;
        g.line(cx_ - side * r / 2, cy_ - r, cx_ + side * r / 2, cy_, thick, c);
        g.line(cx_ + side * r / 2, cy_, cx_ - side * r / 2, cy_ + r, thick, c);
    };

    /* The tiles. */
    {
        static const char *const kNames[4] = {"Resume", "Save State", "Load State", "Reset"};
        const float tw = kInner / 4;
        for (int i = 0; i < 4; ++i)
        {
            const float tx = kInset + float(i) * tw;
            const bool on = !ec_open_ && ec_on_tiles_ && i == ec_tile_;
            /* a round button each, the focused one filled in the side's colour (side_menu.h, 6.0) */
            const float ix = tx + tw * 0.5f, iy = kTilesTop + 38, th_ = 2.4f, radius = 36;
            if (on)
            {
                g.panel(ix - radius - 9, iy - radius - 9, (radius + 9) * 2, (radius + 9) * 2, with_alpha(kAccent, 0.16f), 1, radius + 9);
                g.panel(ix - radius, iy - radius, radius * 2, radius * 2, kAccent, 1, radius);
            }
            else
                g.panel(ix - radius, iy - radius, radius * 2, radius * 2, rgba(0xFFFFFF, 0.063f), 1, radius, rgba(0xFFFFFF, 0.08f), 1);
            /* Its icon, drawn as Playtendoh's menu_canvas.h has them, its name under it. */
            const Color ink = on ? rgba(0x05070D) : text_c;
            if (i == 0)
                g.glyph(Glyph::Arrow, ix, iy, 22, ink, kPi * 0.5f);
            else if (i == 1)
            {
                /* a disk: its body with a cut corner, the label and the shutter */
                const float bx[6] = {-12, 7, 12, 12, -12, -12}, by[6] = {-12, -12, -7, 12, 12, -12};
                for (int k = 0; k < 5; ++k)
                    g.line(ix + bx[k], iy + by[k], ix + bx[k + 1], iy + by[k + 1], th_, ink);
                g.panel(ix - 6 - th_ / 2, iy - 12 - th_ / 2, 11 + th_, 7 + th_, kClear, 1, 0, ink, th_);
                g.panel(ix - 7 - th_ / 2, iy + 2 - th_ / 2, 14 + th_, 10 + th_, kClear, 1, 0, ink, th_);
            }
            else if (i == 2)
            {
                /* an arrow down into a tray */
                g.line(ix, iy - 13, ix, iy + 4, th_, ink);
                g.line(ix - 7, iy - 3, ix, iy + 4, th_, ink);
                g.line(ix + 7, iy - 3, ix, iy + 4, th_, ink);
                g.line(ix - 12, iy + 3, ix - 12, iy + 12, th_, ink);
                g.line(ix - 12, iy + 12, ix + 12, iy + 12, th_, ink);
                g.line(ix + 12, iy + 12, ix + 12, iy + 3, th_, ink);
            }
            else
            {
                /* an arrow round, back to the start: three quarters of a circle, its head at the top */
                constexpr int kArc = 18;
                const float ar = 12;
                for (int k = 0; k < kArc; ++k)
                {
                    const float a0 = -kPi * 0.5f + 1.5f * kPi * float(k) / kArc, a1 = -kPi * 0.5f + 1.5f * kPi * float(k + 1) / kArc;
                    g.line(ix + ar * std::cos(a0), iy + ar * std::sin(a0), ix + ar * std::cos(a1), iy + ar * std::sin(a1), th_, ink);
                }
                g.line(ix, iy - ar, ix - 6, iy - ar - 5, th_, ink);
                g.line(ix, iy - ar, ix - 6, iy - ar + 5, th_, ink);
                /* held: a ring round the tile fills, as a held row's does */
                if (on && ec_hold_ >= 0)
                {
                    const float progress = std::min(1.0f, float((time_ - ec_hold_) / 0.6));
                    constexpr int kSegments = 40;
                    for (int s = 0; s < kSegments && float(s + 1) / kSegments <= progress; ++s)
                    {
                        const float a0 = -kPi * 0.5f + 2 * kPi * float(s) / kSegments, a1 = -kPi * 0.5f + 2 * kPi * float(s + 1) / kSegments;
                        const float rr = radius + 5;
                        g.line(ix + rr * std::cos(a0), iy + rr * std::sin(a0), ix + rr * std::cos(a1), iy + rr * std::sin(a1), 3, kWhite);
                    }
                }
            }
            g.text_mid(Font::Regular, 20, ix, kTilesTop + 96, on ? kWhite : copy, Align::Center, fit(g, Font::Regular, 20, tr(kNames[i]), tw - 8));
        }
    }
    /* The saving and loading slots take the list's place. */
    if (menu_slots_mode_)
    {
        const bool saving = menu_slots_mode_ == 1;
        g.text_mid(Font::SemiBold, 26, kInset + 4, list_top + 16, kWhite, Align::Left, tr(saving ? "Save to which slot?" : "Load which slot?"));
        const float sw = (kInner - kGap * 2) / 3, sh = sw * 0.62f, sy = list_top + 44;
        for (int i = 0; i < porpoise::states::kSlots; ++i)
        {
            const float sx = kInset + float(i) * (sw + kGap);
            const bool on = i == menu_slot_;
            g.panel(sx, sy, sw, sh, row_fill, 1, 14, on ? (menu_confirm_ && saving ? rgba(0xFFB347) : kWhite) : edge, on ? 2.5f : 1);
            if (menu_slot_tex_[i])
            {
                const float a = float(menu_slot_tex_[i]->width) / float(std::max(1, menu_slot_tex_[i]->height));
                float iw = sw - 8, ih = iw / a;
                if (ih > sh - 8)
                {
                    ih = sh - 8;
                    iw = ih * a;
                }
                g.image(menu_slot_tex_[i], sx + (sw - iw) * 0.5f, sy + (sh - ih) * 0.5f, iw, ih, kWhite, 10);
            }
            else
                g.text_mid(Font::Regular, 20, sx + sw * 0.5f, sy + sh * 0.5f, faint, Align::Center, menu_slot_used_[i] ? tr("Saved") : tr("Empty"));
            g.text_mid(Font::SemiBold, 20, sx + 4, sy + sh + 20, on ? kWhite : copy, Align::Left, trf("Slot {n}", {{"n", std::to_string(i + 1)}}));
            if (menu_slot_used_[i])
                g.text_mid(Font::Regular, 16, sx + 4, sy + sh + 44, faint, Align::Left, fit(g, Font::Regular, 16, slot_date(menu_slot_time_[i]), sw - 8));
        }
    }
    else
    {
        /* The list: every category, the open one's rows under it, Quit apart at the end. */
        static const char *const kCats[kTabCount + 1] = {"Game", "Video", "Graphics", "Sound", "Controls", "Patches", "Achievements"};
        const std::vector<int> cat_tabs = sheet_categories(p, wii, tabs);
        const int quit_cat = int(cat_tabs.size());
        /* The rows on their own (Fast Forward and so on), whichever category is open */
        const std::vector<Row> game_rows = rows_for(kTabGame, p, wii, &patches);
        const std::vector<Row> volume_rows = sheet_rows(kEcVolume, p, wii, &patches);
        const auto plain_row = [&](int cat_tab) -> const Row * {
            if (cat_tab == kEcVolume)
                return volume_rows.empty() ? nullptr : &volume_rows[0];
            const Kind kind = cat_tab == kEcFast ? Kind::FastForward : cat_tab == kEcDisc ? Kind::Disc : Kind::Restart;
            for (const Row &r : game_rows)
                if (r.kind == kind)
                    return &r;
            return nullptr;
        };
        /* Achievements: rows like any category's, as Playtendoh's 3DS has them: earned when, or the points */
        const bool ach_shown = menu_tab_ == kTabCount;
        std::vector<Row> ach_rows;
        std::vector<std::string> ach_values;
        int ach_earned = 0;
        for (const Achievement &a : ach_.list)
        {
            ach_earned += a.unlocked ? 1 : 0;
            if (!ach_shown)
                continue;
            Row r{Kind::Info, "", nullptr};
            r.text = a.title;
            ach_rows.push_back(r);
            std::string when;
            if (a.unlocked && a.unlock_time > 0)
            {
                const std::time_t stamp = std::time_t(a.unlock_time);
                char date[32] = {0};
                if (const std::tm *tm = std::localtime(&stamp))
                    std::strftime(date, sizeof date, "%Y-%m-%d", tm);
                when = date;
            }
            ach_values.push_back(!a.unlocked ? plural((long long)a.points, "1 point", "{n} points")
                                 : when.empty() ? tr("Earned")
                                                : trf("Earned {when}", {{"when", when}}));
        }
        const std::vector<Row> &list_rows = ach_shown ? ach_rows : rows;
        const int list_focus = ach_shown ? ach_focus_ : menu_row_;
        struct Line
        {
            int cat, row; /* row -1: the category's own */
            float y;
        };
        std::vector<Line> lines;
        float ly = 0;
        const int focus_cat = ec_cat_;
        int focus_line = -1;
        for (int c = 0; c <= quit_cat; ++c)
        {
            if (c == quit_cat)
                ly += 22;
            if (!ec_open_ && !ec_on_tiles_ && c == focus_cat)
                focus_line = int(lines.size());
            lines.push_back({c, -1, ly});
            ly += kPitch;
            if (c == ec_shown_ && c < quit_cat)
                for (int r = 0; r < int(list_rows.size()); ++r)
                {
                    if (ec_open_ && r == list_focus)
                        focus_line = int(lines.size());
                    lines.push_back({c, r, ly});
                    ly += kPitch;
                }
        }
        /* Hold, Cross and its ring, as a row to confirm has them */
        const auto hold_mark = [&](float right, float cy, Color colour) {
            const float r = 11, rcx = right - r;
            const float progress = ec_hold_ >= 0 ? std::min(1.0f, float((time_ - ec_hold_) / 0.6)) : 0.0f;
            constexpr int kSegments = 32;
            for (int s = 0; s < kSegments; ++s)
            {
                const float a0 = -kPi * 0.5f + 2 * kPi * float(s) / kSegments, a1 = -kPi * 0.5f + 2 * kPi * float(s + 1) / kSegments;
                const bool lit = float(s + 1) / kSegments <= progress;
                g.line(rcx + r * std::cos(a0), cy + r * std::sin(a0), rcx + r * std::cos(a1), cy + r * std::sin(a1), 3, lit ? colour : with_alpha(colour, 0.25f));
            }
            const float bx = right - r * 2 - 12 - 9;
            g.line(bx - 7.2f, cy - 7.2f, bx + 7.2f, cy + 7.2f, 2.2f, copy);
            g.line(bx - 7.2f, cy + 7.2f, bx + 7.2f, cy - 7.2f, 2.2f, copy);
            g.text_mid(Font::Regular, 24, bx - 19, cy + 1, copy, Align::Right, tr("Hold"));
        };
        const float room = list_bottom - list_top;
        if (focus_line >= 0)
        {
            /* Its neighbours half in view too, as Playtendoh's menu scrolls */
            const float fy = lines[std::size_t(focus_line)].y;
            const float top = fy - (focus_line > 0 ? kPitch * 0.5f : 0.0f);
            const float bottom = fy + kRowH + (focus_line + 1 < int(lines.size()) ? kPitch * 0.5f : 0.0f);
            if (top < ec_scroll_target_)
                ec_scroll_target_ = top;
            if (bottom - ec_scroll_target_ > room)
                ec_scroll_target_ = bottom - room;
        }
        else
            ec_scroll_target_ = 0;
        const float total = ly - (kPitch - kRowH);
        ec_scroll_target_ = std::clamp(ec_scroll_target_, 0.0f, std::max(0.0f, total - room));
        ec_scroll_ += (ec_scroll_target_ - ec_scroll_) * (ec_ring_ok_ ? 1.0f - std::exp(-16.0f * float(dt)) : 1.0f);
        g.set_clip(list_top - 8, list_bottom + 8);
        /* The focus: a tint of the side's colour with a bar at its left, gliding to the focused row (side_menu.h,
         * 6.0, in place of the double ring) */
        if (focus_line >= 0)
        {
            const Line &f = lines[std::size_t(focus_line)];
            const float indent = f.row >= 0 ? 28.0f : 0.0f;
            const float target[3] = {kInset - 16 + indent, list_top + f.y - ec_scroll_, kInner + 16 - indent};
            const float k = ec_ring_ok_ ? 1.0f - std::exp(-22.0f * float(dt)) : 1.0f;
            for (int i = 0; i < 3; ++i)
                ec_ring_[i] += (target[i] - ec_ring_[i]) * k;
            ec_ring_ok_ = true;
            const Color tint = f.cat == quit_cat ? rgba(0xFF7272) : kAccent;
            g.panel(ec_ring_[0], ec_ring_[1], ec_ring_[2], kRowH, with_alpha(tint, 0.17f), 1, 14, with_alpha(tint, 0.38f), 1);
            g.panel(ec_ring_[0] + 6, ec_ring_[1] + 14, 4, kRowH - 28, tint, 1, 2);
        }
        else
            ec_ring_ok_ = false;
        /* an open category's rows on a thin guide down their left, in place of a box each */
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i].row >= 0 && (i == 0 || lines[i - 1].row < 0))
            {
                std::size_t last = i;
                while (last + 1 < lines.size() && lines[last + 1].row >= 0)
                    ++last;
                const float top = list_top + lines[i].y - ec_scroll_ + 8, bottom = list_top + lines[last].y - ec_scroll_ + kRowH - 8;
                g.panel(kInset + 4, top, 2, bottom - top, with_alpha(kAccent, 0.45f), 1, 1);
            }
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const Line &l = lines[i];
            const float ry = list_top + l.y - ec_scroll_;
            const bool child = l.row >= 0, on = int(i) == focus_line, quit = l.cat == quit_cat;
            const int cat_tab = child || quit ? 0 : cat_tabs[std::size_t(l.cat)];
            const Row *const row_p = child ? &list_rows[std::size_t(l.row)] : !quit && cat_tab >= kEcFast ? plain_row(cat_tab) : nullptr;
            const float indent = child ? 28.0f : 0.0f, rx = kInset + indent, rw = kInner - indent;
            if (ry + kRowH < list_top - 8 || ry > list_bottom + 8)
                continue;
            if (quit)
                g.panel(kInset, ry - 13, kInner, 1, rgba(0xFFFFFF, 0.08f), 1, 0);
            const float cy = ry + kRowH * 0.5f;
            float right = rx + rw - 14;
            if (!row_p)
            {
                const std::string label = quit ? tr("Quit to the library") : cat_tab == kEcGba ? std::string("GBA")
                                          : cat_tab == kEcButtons ? tr("Buttons")
                                                                  : tr(kCats[cat_tab]);
                g.text_mid(Font::SemiBold, 26, rx + 20, cy, quit ? rgba(0xFF7272) : on ? kWhite : text_c, Align::Left, label);
                if (!quit)
                    chevron(right - 4, cy, on ? text_c : faint, l.cat == ec_shown_ ? 2 : 0);
                /* What it's set to, beside the chevron, as the Wii U's categories say */
                if (!quit)
                {
                    std::string says;
                    if (cat_tab == kEcButtons)
                    {
                        /* as Playtendoh's: how many of the buttons have a control */
                        const Mapping m = p.mapping();
                        int set = 0;
                        for (int i = 0; i < GcCount; ++i)
                            set += m.control[i] >= 0 ? 1 : 0;
                        says = plural((long long)set, "1 button set", "{n} buttons set");
                    }
                    else if (cat_tab == kTabCount)
                        says = trf("{a} of {b}", {{"a", std::to_string(ach_earned)}, {"b", std::to_string(ach_.list.size())}});
                    else if (cat_tab == kTabPatches)
                    {
                        int patches_on = 0;
                        for (const bool cheat_on : menu_cheat_on_)
                            patches_on += cheat_on ? 1 : 0;
                        says = trf("{n} on", {{"n", std::to_string(patches_on)}});
                    }
                    else
                    {
                        const char *const key = cat_tab == kTabVideo ? "resolution" : cat_tab == kTabGraphics ? "shader_mode"
                                                : cat_tab == kTabAudio ? "audio_preset" : cat_tab == kTabControls ? "button_layout" : "";
                        if (*key)
                            for (const Row &candidate : rows_for(cat_tab, p, wii, &patches))
                                if (candidate.key == std::string(key))
                                {
                                    bool unused = true;
                                    row_value(candidate, says, unused);
                                }
                    }
                    if (!says.empty())
                        g.text_mid(Font::Regular, 24, right - 28, cy + 1, on ? text_c : faint, Align::Right, fit(g, Font::Regular, 24, says, rw * 0.5f));
                }
                if (quit && on)
                    hold_mark(right, cy, rgba(0xFF7272));
                continue;
            }
            const Row &row = *row_p;
            const Font label_font = child ? Font::Regular : Font::SemiBold;
            const float label_size = child ? 24.0f : 26.0f;
            std::string value;
            bool arrows = true;
            if (child && ach_shown)
            {
                value = ach_values[std::size_t(l.row)];
                arrows = false;
            }
            else
                row_value(row, value, arrows);
            const std::string label = row.label ? tr(row.label) : row.text;
            float value_left = right;
            if (((!child && row.kind == Kind::Restart) || (child && row.kind == Kind::PadReset)) && on)
            {
                hold_mark(right, cy, kAccent);
                value_left = right - 120;
            }
            else if (!value.empty())
            {
                const bool show_arrows = on && arrows;
                if (show_arrows)
                {
                    chevron(right - 4, cy, kAccent, 0);
                    right -= 22;
                }
                const bool slider = row.iv && row.key == std::string("volume") && row.values.size() > 1;
                const float slider_w = slider ? 132.0f + 18.0f : 0.0f;
                const float label_room = g.measure(label_font, label_size, label) + 36;
                const float room_v = std::max(rw * 0.34f, right - rx - 20 - label_room - slider_w);
                value = fit(g, Font::Regular, 24, value, room_v);
                const float vw = g.measure(Font::Regular, 24, value);
                g.text_mid(Font::Regular, 24, right, cy + 1, on ? text_c : faint, Align::Right, value);
                value_left = right - vw;
                if (slider)
                {
                    /* its track, filled in the side's colour to the knob */
                    const float track_right = value_left - 18, track_left = track_right - 132;
                    const float at = std::clamp(float(*row.iv - row.min) / float(row.values.size() - 1), 0.0f, 1.0f);
                    const float knob = track_left + (track_right - track_left) * at;
                    /* thin, to a small knob (6.0) */
                    const float knob_r = on ? 8.0f : 6.0f;
                    g.panel(track_left, cy - 2, track_right - track_left, 4, rgba(0xFFFFFF, 0.11f), 1, 2);
                    g.panel(track_left, cy - 2, knob - track_left, 4, on ? kAccent : with_alpha(kAccent, 0.7f), 1, 2);
                    g.panel(knob - knob_r, cy - knob_r, knob_r * 2, knob_r * 2, text_c, 1, knob_r);
                    value_left = track_left - 12;
                }
                if (show_arrows)
                    chevron(value_left - 14, cy, kAccent, 1);
                if (show_arrows)
                    value_left -= 22;
            }
            else if (row.kind == Kind::Group)
            {
                chevron(right - 4, cy, on ? copy : faint, g_group_open[std::clamp(row.index, 0, kGroupCount - 1)] ? 2 : 0);
                value_left = right - 22;
            }
            else if (on)
            {
                chevron(right - 4, cy, copy, 0);
                value_left = right - 22;
            }
            g.text_mid(label_font, label_size, rx + 20 + (row.child ? 24.0f : 0.0f), cy + (child ? 1.0f : 0.0f), on ? kWhite : child ? copy : text_c, Align::Left,
                       fit(g, label_font, label_size, label, std::max(80.0f, value_left - rx - 40)));
        }
        g.clear_clip();
        if (ach_shown && ec_open_)
        {
            /* the one in focus, large, beside the sheet */
            g.set_layer(0, 0, t);
            draw_achievement_card(1000, 120, 840, 840);
            g.set_layer(slide, 0, 0.3f + 0.7f * t);
        }
        if (menu_tab_ == kEcButtons && ec_shown_ >= 0)
        {
            /* Buttons open: the DualSense beside the sheet, each control with its GameCube button, the focused
             * one lit (Playtendoh's side_menu.h DrawPad) */
            int focused = -1;
            if (ec_open_ && menu_row_ >= 0 && menu_row_ < int(rows.size()) && rows[std::size_t(menu_row_)].kind == Kind::PadButton)
                focused = p.mapping().control[std::clamp(rows[std::size_t(menu_row_)].index, 0, GcCount - 1)];
            g.set_layer(0, 0, t);
            draw_pad_beside(p, focused, pad_listen_ >= 0, kAccent);
            g.set_layer(slide, 0, 0.3f + 0.7f * t);
        }
        if (total > room)
        {
            const float bar_top = list_top + room * ec_scroll_ / total, bar_h = room * room / total;
            g.panel(x + w - 18, bar_top, 3, bar_h, rgba(0xFFFFFF, 0.25f), 1, 1.5f);
        }
    }
    /* The help, two lines at most, or what's just happened. */
    {
        std::string text;
        if (!menu_note_.empty() && time_ - menu_note_time_ < 4.0)
            text = menu_note_;
        else if (!waiting.empty())
            text = plural((long long)waiting.size(), "1 change waiting: Square applies it.", "{n} changes waiting: Square applies them.");
        else if (ec_open_ && !menu_slots_mode_ && menu_tab_ == kTabCount)
        {
            if (ach_focus_ >= 0 && ach_focus_ < int(ach_.list.size()))
                text = ach_.list[std::size_t(ach_focus_)].description;
        }
        else if (ec_open_ && !menu_slots_mode_ && !rows.empty())
            {
                const Row &focus = rows[std::size_t(std::clamp(menu_row_, 0, int(rows.size()) - 1))];
                if (focus.kind == Kind::PadButton && pad_listen_ == focus.index)
                {
                    const long long left = std::max(0LL, (long long)(pad_until_ - time_)) + 1;
                    text = trf("Press the DualSense button for {button} now.", {{"button", tr(kPadButtonNames[std::clamp(focus.index, 0, GcCount - 1)])}}) +
                           " " + plural(left, "1 second left, or nothing changes.", "{n} seconds left, or nothing changes.");
                }
                else if (focus.kind == Kind::PadTest || focus.kind == Kind::PadButton || focus.kind == Kind::PadReset)
                    text = tr(focus.kind == Kind::PadTest ? "This player's DualSense input, live."
                              : focus.kind == Kind::PadButton
                                  ? "Press Cross, then the DualSense button to use. If another button already uses it, the two swap."
                                  : "Every button back to the PlayStation layout. Your own layouts are kept.");
                else
                text = focus.kind == Kind::Cheat && focus.index >= 0 && focus.index < int(menu_cheats_.size())
                           ? cheat_help(menu_cheats_[std::size_t(focus.index)]) + "  " + tr("Takes effect the next time the game starts.")
                           : focus.key == std::string("ws_status") ? tr(ws_help(p.ws_plan, wii)) : tr(help_for(focus, p));
            }
        else if (!ec_on_tiles_ && !menu_slots_mode_)
        {
            /* a row on its own */
            const std::vector<int> cats_now = sheet_categories(p, wii, tabs);
            if (ec_cat_ == int(cats_now.size()))
                text = tr("Leaves the game for the library. What you have not saved in the game is lost, so Cross is held.");
            else if (cats_now[std::size_t(ec_cat_)] == kEcButtons)
                text = tr("Choose the DualSense button for each button.");
            if (ec_cat_ < int(cats_now.size()) && cats_now[std::size_t(ec_cat_)] >= kEcFast)
            {
                const int plain = cats_now[std::size_t(ec_cat_)];
                const Kind kind = plain == kEcFast ? Kind::FastForward : plain == kEcDisc ? Kind::Disc : Kind::Restart;
                if (plain == kEcVolume)
                {
                    for (const Row &r : sheet_rows(kEcVolume, p, wii, &patches))
                        text = tr(help_for(r, p));
                }
                else if (plain == kEcRestart)
                    text = tr("Starts the game again from the beginning. What you have not saved in the game is lost, so Cross is held.");
                else
                    for (const Row &r : rows_for(kTabGame, p, wii, &patches))
                        if (r.kind == kind)
                            text = tr(help_for(r, p));
            }
        }
        float ly = list_bottom + 26;
        for (const std::string &l : wrap(g, Font::Regular, 20, text, kInner, 2))
        {
            g.text_mid(Font::Regular, 20, kInset, ly, faint, Align::Left, l);
            ly += 26;
        }
    }
    /* The hints. */
    {
        /* menu_canvas.h's Hint: its Button, in lines, and what it does */
        /* under a hairline (6.0) */
        g.panel(kInset, footer, kInner, 1, rgba(0xFFFFFF, 0.08f), 1, 0);
        float px = kInset;
        const float py = footer + 16;
        enum Mark { MarkCross, MarkCircle, MarkChange, MarkSquare };
        std::vector<std::pair<Mark, std::string>> hints = {{MarkCross, tr("Choose")}};
        if (!menu_slots_mode_)
            hints.push_back({MarkChange, tr("Change")});
        hints.push_back({MarkCircle, ec_open_ || menu_slots_mode_ ? tr("Back") : tr("Back to the game")});
        if (!waiting.empty())
            hints.push_back({MarkSquare, tr("Apply")});
        float words = 0;
        for (const auto &hint : hints)
            words += 30 + g.measure(Font::Regular, 22, hint.second) + 32;
        const float size = words <= kInner - 8 ? 22.0f : std::max(22.0f * 0.75f, 22.0f * (kInner - 8) / words);
        for (const auto &hint : hints)
        {
            const float bx = px + 11, by = py + 14, r = 9, thick = 2.2f;
            if (hint.first == MarkCross)
            {
                g.line(bx - r * 0.8f, by - r * 0.8f, bx + r * 0.8f, by + r * 0.8f, thick, copy);
                g.line(bx - r * 0.8f, by + r * 0.8f, bx + r * 0.8f, by - r * 0.8f, thick, copy);
            }
            else if (hint.first == MarkCircle)
                g.panel(bx - r - thick / 2, by - r - thick / 2, (r + thick / 2) * 2, (r + thick / 2) * 2, kClear, 1, r + thick / 2, copy, thick);
            else if (hint.first == MarkSquare)
                g.panel(bx - r * 0.8f - thick / 2, by - r * 0.8f - thick / 2, r * 1.6f + thick, r * 1.6f + thick, kClear, 1, 2, copy, thick);
            else
                for (const float side : {-1.0f, 1.0f})
                {
                    const float tip = bx + side * (r + 3);
                    g.line(tip, by, tip - side * 6, by - 6, thick, copy);
                    g.line(tip, by, tip - side * 6, by + 6, thick, copy);
                }
            g.text_mid(Font::Regular, size, px + 30, py + 13, copy, Align::Left, hint.second);
            px += 30 + g.measure(Font::Regular, size, hint.second) + 32 * size / 22.0f;
        }
    }
    }
    else
    {
    const float row_h = gl ? 50.0f : 56.0f, rx = x + 24, rw = w - 48;
    float ry = hy;
    /* A long tab (Patches; Graphics or Controls with a game's extra rows): a
     * window on its rows, around the focus. */
    int first = 0, last = int(rows.size());
    {
        /* What's below the rows keeps its room: the Game tab's three slots
         * (with the help line and the prompts, ~300), Patches' longer help. */
        const float reserve = menu_tab_ == kTabGame ? (gl ? 270.0f : 330.0f) : menu_tab_ == kTabPatches ? 200.0f : 150.0f;
        const int visible = std::max(3, int((y + h - reserve - hy) / row_h));
        if (last > visible)
        {
            first = std::clamp(menu_row_ - visible / 2, 0, last - visible);
            last = first + visible;
        }
        if (first > 0)
            g.glyph(Glyph::Arrow, x + w * 0.5f, hy - 2, 14, kCyan, 0.0f);
    }
    for (int i = first; i < last; ++i)
    {
        const Row &row = rows[std::size_t(i)];
        if (row.gap_before && i > first)
        {
            g.panel(x + 36, ry + 6, w - 72, gl ? 1.0f : 1.5f, gl ? rgba(0xFFFFFF, 0.16f) : rgba(0x6F8FE0, 0.5f), 1, 0);
            ry += 14;
        }
        const float cy = ry + row_h * 0.5f;
        const bool on = i == menu_row_;
        if (row.kind != Kind::Info)
            mp_hit(rx + slide, ry, rw, row_h, kMpRow + i);
        if (on && gl)
            skin_select(rx, ry + 3, rw, row_h - 6, (row_h - 6) * 0.5f, 1.0f);
        else if (on)
            g.panel(rx, ry + 4, rw, row_h - 8, rgba(0x1D45B8, 0.9f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float indent = row.child ? 26.0f : 0.0f;
        const float label_size = gl ? (row.child ? ts(21) : ts(23)) : ts(27), value_size = gl ? ts(21) : ts(25);
        const std::string label = row.label ? tr(row.label) : row.text;
        bool row_waiting = false;
        if (row.key && *row.key)
            row_waiting = std::find(waiting.begin(), waiting.end(), std::string(row.key)) != waiting.end() ||
                          (std::string(row.key) == "audio_preset" &&
                           std::find_if(waiting.begin(), waiting.end(), [](const std::string &k) {
                               return k.rfind("audio_", 0) == 0;
                           }) != waiting.end());
        if (row.kind == Kind::Cheat && row.index >= 0 && row.index < int(menu_cheats_.size()))
        {
            const Cheat &c = menu_cheats_[std::size_t(row.index)];
            for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
                row_waiting |= std::find(waiting.begin(), waiting.end(), k) != waiting.end();
        }
        if (row_waiting)
            g.panel(rx + 8, cy - 6, 12, 12, kPending, 1, 6); /* changed, not applied yet */
        g.text_mid(on ? Font::Bold : Font::SemiBold, label_size, rx + 28 + indent, cy, on ? kWhite : kSoft, Align::Left,
                   row.label ? label : fit(g, on ? Font::Bold : Font::SemiBold, label_size, label, rw * 0.62f));
        std::string value;
        bool arrows = true;
        row_value(row, value, arrows);
        if (!value.empty())
        {
            const float right = rx + rw - 24;
            value = fit(g, Font::Bold, value_size, value, rw * 0.45f);
            g.text_mid(Font::Bold, value_size, right - (on ? 30 : 0), cy, row_waiting ? kPending : on ? kWhite : kSoft,
                       Align::Right, value);
            if (on && arrows)
            {
                const float vw = g.measure(Font::Bold, value_size, value);
                g.glyph(Glyph::Arrow, right - 30 - vw - 22, cy, 16, kCyan, -kPi * 0.5f);
                g.glyph(Glyph::Arrow, right - 8, cy, 16, kCyan, kPi * 0.5f);
            }
        }
        else if (on)
            g.glyph(Glyph::Arrow, rx + rw - 30, cy, 20, kWhite, kPi * 0.5f);
        ry += row_h;
    }
    if (last < int(rows.size()))
        g.glyph(Glyph::Arrow, x + w * 0.5f, ry + 2, 14, kCyan, kPi);

    const Row &focus = rows[std::size_t(std::clamp(menu_row_, 0, int(rows.size()) - 1))];
    float below = ry + 14;
    if (menu_tab_ == kTabGame)
    {
        /* The three slots, with what each one saw: chosen from while
         * saving or loading, a glance otherwise. */
        const bool picking = menu_slots_mode_ != 0;
        const bool saving = menu_slots_mode_ == 1;
        if (picking)
        {
            g.text_mid(Font::Bold, ts(24), x + 48, below + 8, kWhite, Align::Left,
                       tr(saving ? "Save to which slot?" : "Load which slot?"));
            below += 30;
        }
        /* On glass the slots are 16:9, as the pictures in them are (never squeezed). */
        const float sgap = gl ? 16.0f : 24.0f, sw = gl ? (w - 96 - sgap * 2) / 3.0f : 200.0f;
        const float sh = gl ? sw * 9.0f / 16.0f : 112.0f;
        const float sx0 = x + (w - (sw * 3 + sgap * 2)) * 0.5f;
        for (int i = 0; i < porpoise::states::kSlots; ++i)
        {
            const float sx = sx0 + float(i) * (sw + sgap), sy = below + 4;
            const bool on = picking && i == menu_slot_;
            const bool dim = picking && !saving && !menu_slot_used_[i]; /* nothing to load there */
            const bool replace = on && saving && menu_confirm_;
            const Color edge = replace ? rgba(0xFFB347) : on ? kIcy : rgba(0x5A68A8, dim ? 0.4f : 0.8f);
            g.panel(sx - 4, sy - 4, sw + 8, sh + 8, rgba(0x07102E, dim ? 0.35f : 0.6f), 1, kR * 0.7f, edge,
                    on ? 2.8f : 1.4f, on ? 12 : 0);
            if (menu_slot_tex_[i])
            {
                /* Fit the picture inside the card, at its own shape. */
                const float a = float(menu_slot_tex_[i]->width) / float(std::max(1, menu_slot_tex_[i]->height));
                float iw = sw, ih = sw / a;
                if (ih > sh)
                {
                    ih = sh;
                    iw = sh * a;
                }
                g.image(menu_slot_tex_[i], sx + (sw - iw) * 0.5f, sy + (sh - ih) * 0.5f, iw, ih,
                        on || !picking ? kWhite : rgba(0xFFFFFF, 0.7f), kR * 0.5f);
            }
            else
                g.text_mid(Font::SemiBold, ts(21), sx + sw * 0.5f, sy + sh * 0.5f, with_alpha(kLavender, 0.8f),
                           Align::Center, menu_slot_used_[i] ? tr("Saved") : tr("Empty"));
            const float lw = g.text_mid(Font::Bold, ts(19), sx + 4, sy + sh + 22, on ? kWhite : kSoft, Align::Left,
                                        trf("Slot {n}", {{"n", std::to_string(i + 1)}}));
            if (menu_slot_used_[i])
            {
                /* The date, and the time when there is room. */
                std::string when = slot_date(menu_slot_time_[i]);
                if (g.measure(Font::Regular, ts(17), when) > sw - lw - 16)
                    when = when.substr(0, when.find("  "));
                g.text_mid(Font::Regular, ts(17), sx + sw - 2, sy + sh + 22, kLavender, Align::Right,
                           fit(g, Font::Regular, ts(17), when, sw - lw - 14));
            }
        }
        below += sh + 44;
    }
    if (menu_tab_ == kTabControls && menu_game_ && menu_game_->platform == "Wii" &&
        p.wii_controller != porpoise::pad::WiiGameCube)
    {
        /* Beside the menu: how the DualSense is the Wii controller, as set. */
        g.set_layer();
        /* With Grip on Auto, the hold as it is read right now. */
        draw_hub_buttons(gl ? 40.0f : 960.0f, 40, 920, 1000, t);
        g.set_layer(slide, 0, 0.3f + 0.7f * t);
    }
    else if (menu_tab_ == kTabControls)
    {
        /* The controller and its buttons, as this game has them: smaller
         * when the rows leave less room above the help line. */
        const float room = (y + h - 112 - 10) - (below + 36);
        const float aw = std::min(560.0f, room * kLinesW / kLinesH);
        if (aw > 200)
        {
            draw_controller_lines(x + (w - aw) * 0.5f, below + 36, aw, p.mapping());
            below += 36 + kLinesH * aw / kLinesW;
        }
    }

    /* A note after a save or load, what Cross will do while picking a slot,
     * else the row's help. */
    {
        const double since = time_ - menu_note_time_;
        bool note = !menu_note_.empty() && since < 4.0;
        std::string help;
        if (focus.kind == Kind::Cheat && focus.index >= 0 && focus.index < int(menu_cheats_.size()))
            help = cheat_help(menu_cheats_[std::size_t(focus.index)]) + "  " +
                   tr("Takes effect the next time the game starts.");
        else if (focus.key == std::string("ws_status"))
            help = tr(ws_help(p.ws_plan, wii));
        else if (focus.key == std::string("no_codes"))
            help = trf("Dolphin lists none for this game. Your own codes go in /data/porpoise/cheats/{id}.ini.",
                       {{"id", menu_game_ ? menu_game_->id : std::string("<ID>")}});
        else
            help = tr(help_for(focus, p));
        std::string text = note ? menu_note_ : help;
        bool warn = false;
        if (!note && !waiting.empty() && !menu_slots_mode_)
        {
            text = plural((long long)waiting.size(), "1 change waiting: Square applies it.",
                          "{n} changes waiting: Square applies them.");
            warn = note = true;
        }
        if (menu_slots_mode_ && !note)
        {
            const std::string n = std::to_string(menu_slot_ + 1);
            if (menu_slots_mode_ == 2)
                text = menu_slot_used_[menu_slot_] ? trf("Cross loads slot {n}. The game goes back to that moment.", {{"n", n}})
                                                   : tr("That slot is empty.");
            else if (!menu_slot_used_[menu_slot_])
                text = trf("Cross saves this moment in slot {n}.", {{"n", n}});
            else if (!menu_confirm_)
                text = trf("Slot {n} has a save state. Cross, then Cross again, replaces it.", {{"n", n}});
            else
            {
                text = trf("Press Cross again to replace slot {n}.", {{"n", n}});
                warn = true;
            }
            note = true;
        }
        const int max_lines = menu_tab_ == kTabPatches ? 3 : 2;
        const float ny = std::min(below + 10, y + h - 112 - (max_lines - 2) * 30.0f);
        const auto lines = wrap(g, Font::Regular, ts(22), text, w - 96, max_lines);
        float ly = ny;
        for (const std::string &l : lines)
        {
            g.text_mid(note ? Font::SemiBold : Font::Regular, ts(22), x + 48, ly,
                       warn ? rgba(0xFFC266) : note ? kCyan : kLavender, Align::Left, l);
            ly += 30;
        }
    }

    /* The prompts, smaller, inside the panel. */
    {
        float px = x + 52;
        const float py = y + h - 50, size = ts(22);
        std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::Cross, tr("Select")},
                                                              {Glyph::Circle, tr("Resume")}};
        if (menu_slots_mode_)
            prompts = {{Glyph::DPad, tr("Slot")},
                       {Glyph::Cross, tr(menu_slots_mode_ == 1 ? "Save" : "Load")},
                       {Glyph::Circle, tr("Back")}};
        else
        {
            if (!waiting.empty())
                prompts.push_back({Glyph::Square, tr("Apply")});
            prompts.push_back({Glyph::Triangle, tr("Reset Tab")});
        }
        for (std::size_t i = 0; i < prompts.size(); ++i)
        {
            if (i > 0)
            {
                g.panel(px - 2, py - 16, 1.5f, 32, rgba(0x6F8FE0, 0.6f), 1, 0);
                px += 26;
            }
            g.glyph(prompts[i].first, px + 15, py, 30, kWhite);
            px += 42;
            px += g.text_mid(Font::SemiBold, size, px, py, kWhite, Align::Left, prompts[i].second) + 30;
        }
        /* L1 / R1 tabs, on the right (the Porpoise look has them by the tabs only). */
        if (!gl)
        {
            const std::string tabs = tr("Tabs");
            const float tw = g.measure(Font::SemiBold, size, tabs);
            const float right = x + w - 48;
            g.text_mid(Font::SemiBold, size, right, py, kWhite, Align::Right, tabs);
            if (g.has_icons())
            {
                g.glyph(Glyph::R1, right - tw - 30, py, 30, kWhite);
                g.glyph(Glyph::L1, right - tw - 76, py, 30, kWhite);
            }
        }
    }

    } /* the old look's list, slots, help and prompts */

    /* A question over the menu: Apply or Discard before it closes, or
     * starting the game over for changes that need it. */
    if (menu_prompt_)
    {
        g.panel(x + 8, y + 8, w - 16, h - 16, rgba(0x050A24, 0.72f), 1, kR);
        const float bw = w - 120, bh = 310, bx = x + 60, by = y + h * 0.5f - bh * 0.5f;
        g.panel(bx, by, bw, bh, rgba(0x0F1F63, 0.96f), 0.9f, kR, rgba(0x8BD9FF), 2.0f, 12, 0.2f);
        const bool restart = menu_prompt_ == 2;
        g.text_mid(Font::Bold, ts(32), bx + bw * 0.5f, by + 60, kWhite, Align::Center,
                   tr(restart ? "Restart the game to finish?" : "Apply your changes?"));
        const std::string message =
            restart ? (menu_play_->quick_resume
                           ? tr("Some of the changes take effect when the game starts again. Quick Resume brings you "
                                "back to this moment.")
                           : tr("Some of the changes take effect when the game starts again. Unsaved progress is lost; "
                                "a save state keeps it."))
                    : plural((long long)waiting.size(), "You changed 1 setting. Circle keeps editing.",
                             "You changed {n} settings. Circle keeps editing.");
        float ly = by + 120;
        for (const std::string &l : wrap(g, Font::Regular, ts(24), message, bw - 60, 3))
        {
            g.text_mid(Font::Regular, ts(24), bx + bw * 0.5f, ly, kLavender, Align::Center, l);
            ly += 34;
        }
        const std::string names[2] = {tr(restart ? "Restart Now" : "Apply"), tr(restart ? "Later" : "Discard")};
        const float cw = (bw - 90) * 0.5f, ch = 64, cyb = by + bh - 70;
        for (int b = 0; b < 2; ++b)
        {
            const bool lit = (b == 0) == (menu_prompt_choice_ == 1);
            const float cx = bx + 30 + b * (cw + 30);
            g.panel(cx, cyb - ch * 0.5f, cw, ch, lit ? rgba(0x1F63F0, 0.95f) : rgba(0x07102E, 0.6f), 0.8f, kR,
                    lit ? rgba(0x8BD9FF) : rgba(0x3D5AB0, 0.85f), lit ? 2.0f : 1.4f);
            g.text_mid(Font::Bold, ts(26), cx + cw * 0.5f, cyb, lit ? kWhite : kSoft, Align::Center,
                       fit(g, Font::Bold, ts(26), names[b], cw - 24));
        }
    }

    /* Saving or loading: the panel waits under a veil, with a spinner. */
    if (menu_busy_.kind != MenuRequest::None)
    {
        ++menu_busy_frames_;
        g.panel(x + 8, y + 8, w - 16, h - 16, rgba(0x050A24, 0.72f), 1, kR);
        const float cx = x + w * 0.5f, cy = y + h * 0.5f - 20;
        const float spin = float(time - menu_busy_start_) * 7.0f;
        for (int i = 0; i < 10; ++i)
        {
            const float a = float(i) / 10.0f * 2.0f * kPi;
            const float fade = std::fmod(float(i) / 10.0f - spin / (2.0f * kPi) + 10.0f, 1.0f);
            g.blob(cx + std::cos(a) * 46, cy + std::sin(a) * 46, 22, 22, rgba(0x8BD9FF, 0.25f + 0.75f * fade));
        }
        const std::string n = std::to_string(menu_busy_.slot + 1);
        g.text_mid(Font::Bold, ts(30), cx, cy + 100, kWhite, Align::Center,
                   menu_busy_.kind == MenuRequest::Save ? trf("Saving to slot {n}\xE2\x80\xA6", {{"n", n}})
                                                        : trf("Loading slot {n}\xE2\x80\xA6", {{"n", n}}));
        g.text_mid(Font::Regular, ts(22), cx, cy + 142, kLavender, Align::Center,
                   tr("This can take a few seconds."));
    }
    g.set_layer();
}

/* ---- Playtendoh's Buttons category ------------------------------------------------------- */

void App::pad_capture()
{
    const bool idle = (raw_held_ & 0xFFFFu) == 0;
    if (pad_listen_ < 0)
    {
        if (idle)
            pad_settling_ = false; /* the press taken is let go: the menu reads the controller again */
        return;
    }
    const auto stop = [&](const char *message) {
        pad_listen_ = -1;
        pad_settling_ = !idle;
        menu_note_ = tr(message);
        menu_note_time_ = time_;
    };
    if (time_ > pad_until_)
    {
        stop("No button pressed. Nothing changed.");
        sfx(Sound::MovingTab);
        return;
    }
    if (!pad_armed_)
    {
        pad_armed_ = idle;
        return;
    }
    const int control = held_control(raw_held_ & ~raw_prev_ & 0xFFFFu);
    if (control < 0)
        return;
    const int gc = pad_listen_;
    stop("Done.");
    pad_assign(gc, control);
    sfx(Sound::LaunchGame);
}

/* The DualSense beside the sheet, Zacksly's drawing (assets/ui/dualsense.png), each GameCube button's chip in a
 * column beside it on a line from its control: Playtendoh's side_menu.h DrawPad, place for place. */
void App::draw_pad_beside(const Settings &p, int focused_control, bool listening, Color accent)
{
    Gfx &g = *g_;
    /* each control's centre on the drawing, in its 1400x900 units (ui_app_controls.cpp's kSpots), in Control order */
    static constexpr float kSpot[CtlCount][2] = {{1035, 399}, {1114, 319}, {955, 319}, {1035, 240}, {372, 140}, {1028, 140},
                                                 {366, 112},  {1034, 112}, {525, 468}, {873, 468}, {949, 198}, {700, 233},
                                                 {363, 250},  {363, 388},  {299, 319}, {428, 319}};
    /* the drawing, 640 wide, centred at x 1310 */
    constexpr float kArtX = 990, kArtY = 372, kArtScale = 640.0f / 1400.0f;
    const auto spot_x = [&](int c) { return kArtX + kSpot[c][0] * kArtScale; };
    const auto spot_y = [&](int c) { return kArtY + kSpot[c][1] * kArtScale; };
    const Color title = rgba(0xF5F7FB), faint = rgba(0xF5F7FB, 117.0f / 255.0f);
    g.text_mid(Font::SemiBold, 32, 960, 156, title, Align::Left, tr("Your controller"));
    g.text_mid(Font::Regular, 20, 960, 196, faint, Align::Left,
               fit(g, Font::Regular, 20, tr("Each button shows the console's button it presses."), 900));
    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    if (pad_art_)
        g.image(pad_art_, kArtX, kArtY, 640, 900 * kArtScale, rgba(0xFFFFFF, 0.94f));
    else
    {
        /* no drawing: the controller's outline, so the chips still have a place */
        g.panel(kArtX + 90, kArtY + 40, 460, 340, kClear, 1, 120, with_alpha(accent, 0.55f), 2.2f);
        g.panel(kArtX + 215, kArtY + 45, 210, 115, kClear, 1, 16, rgba(0xFFFFFF, 0.19f), 2.2f);
    }
    /* the focused row's control lit; while it waits for a press, every one */
    const float pulse = 0.75f + 0.25f * std::sin(float(time_) * 5.0f);
    for (int c = 0; c < CtlCount; ++c)
        if (listening || c == focused_control)
        {
            const float r = listening ? 18.0f : 24.0f;
            g.panel(spot_x(c) - r, spot_y(c) - r, r * 2, r * 2, with_alpha(accent, (listening ? 0.30f : 0.55f) * pulse), 1, r,
                    with_alpha(accent, 0.9f), 2.2f);
        }
    /* each button's chip: the left half's in a column left of the drawing, the right half's right of it, the
     * touchpad's above; in the order of their controls from the top, as near level with them as they fit */
    struct Chip
    {
        int gc, control;
        float y;
        int side; /* -1 left, 1 right, 0 above */
    };
    std::vector<Chip> chips;
    const Mapping m = p.mapping();
    for (int gc = 0; gc < GcCount; ++gc)
    {
        const int c = m.control[gc];
        if (c < 0 || c >= CtlCount)
            continue;
        const int side = c == CtlTouch ? 0 : kSpot[c][0] < 700 ? -1 : 1;
        chips.push_back({gc, c, side == 0 ? kArtY - 46 : spot_y(c), side});
    }
    std::stable_sort(chips.begin(), chips.end(), [&](const Chip &a, const Chip &b) { return spot_y(a.control) < spot_y(b.control); });
    constexpr float kGapY = 42, kTop = 300, kBottom = 860;
    for (int side : {-1, 1})
    {
        std::vector<Chip *> column;
        for (Chip &chip : chips)
            if (chip.side == side)
                column.push_back(&chip);
        float next = kTop;
        for (Chip *chip : column)
            next = (chip->y = std::max(chip->y, next)) + kGapY;
        if (!column.empty() && column.back()->y > kBottom)
        {
            float limit = kBottom;
            for (auto it = column.rbegin(); it != column.rend(); ++it, limit -= kGapY)
                (*it)->y = std::min((*it)->y, limit);
        }
    }
    for (const Chip &chip : chips)
    {
        const bool on = chip.control == focused_control;
        const std::string label = tr(kPadButtonNames[chip.gc]);
        const float h = on ? 38.0f : 32.0f, size = on ? 20.0f : 17.0f;
        const float w = std::max(h, g.measure(Font::Bold, size, label) + h * 0.8f);
        const float cx = chip.side < 0 ? kArtX - 30 - w / 2 : chip.side > 0 ? kArtX + 640 + 30 + w / 2 : spot_x(chip.control);
        const float edge_x = chip.side < 0 ? cx + w / 2 : chip.side > 0 ? cx - w / 2 : cx;
        const float edge_y = chip.side == 0 ? chip.y + h / 2 : chip.y;
        const float fx = spot_x(chip.control), fy = spot_y(chip.control);
        g.line(fx, fy, edge_x, edge_y, 1.4f, on ? with_alpha(accent, 0.9f) : rgba(0xFFFFFF, 0.3f));
        g.panel(fx - 4, fy - 4, 8, 8, on ? accent : rgba(0xFFFFFF, 0.6f), 1, 4);
        g.panel(cx - w / 2, chip.y - h / 2, w, h, on ? accent : rgba(0x1A2236, 0.94f), 1, h / 2,
                on ? rgba(0xFFFFFF) : with_alpha(accent, 0.6f), 1.4f);
        g.text_mid(Font::Bold, size, cx, chip.y, on ? rgba(0x06080F) : title, Align::Center, label);
    }
}

/* A GameCube button onto a DualSense control, in the layout the game plays with: the control's old button takes
 * this one's (the two swap). A ready-made layout is copied into My Layout 1 first, which the game then uses. */
void App::pad_assign(int gc, int control)
{
    Settings *const target = menu_play_;
    if (!target || gc < 0 || gc >= GcCount)
        return;
    int own = target->preset_in_use();
    const bool copied = own < 0;
    if (copied)
    {
        own = 0;
        const Mapping m = target->mapping();
        for (int i = 0; i < GcCount; ++i)
            settings_->presets[own][i] = m.control[i];
    }
    map_target_ = target;
    map_game_ = menu_game_;
    map_in_game_ = true;
    map_preset_ = own;
    assign_control(gc, control);
    map_in_game_ = false;
    if (copied)
    {
        menu_note_ = tr("Done. Your buttons are My Layout 1 now.");
        menu_note_time_ = time_;
    }
}

/* Every button back as a first start has them (Settings' button_layout); the player's own layouts are kept. */
void App::pad_reset()
{
    Settings *const target = menu_play_;
    if (!target)
        return;
    const int fallback = LayoutPlayStation; /* (Settings::button_layout's own) */
    map_target_ = target;
    map_game_ = menu_game_;
    map_in_game_ = true;
    const bool changed = target->button_layout != fallback;
    target->button_layout = fallback;
    save_mapping(changed);
    map_in_game_ = false;
    menu_note_ = tr("Done.");
    menu_note_time_ = time_;
}

void App::return_from_game()
{
    menu_free_slots();
    details_ach_for_.clear(); /* the game just played may have unlocked some */
    ach_live_ = false;
    menu_game_ = nullptr;
    menu_play_ = nullptr;
    map_in_game_ = false;
    menu_wide_open_ = -2;
    menu_cheats_.clear();
    menu_cheat_on_.clear();
    menu_ff_ = 0;
    launch_ = nullptr;
    gl_launching_ = false;
    screen_ = Screen::Main;
    tab_ = Tab::Library;
    screen_anim_ = 1.0f;
    held_ = prev_ = 0xFFFFFFFFu; /* the button that quit doesn't press anything here */
    cards_scanned_ = false;
    save_scan_.reset(); /* a scan already running read the old state */
    lift_ = 0.4f;
}
} // namespace porpoise::ui
