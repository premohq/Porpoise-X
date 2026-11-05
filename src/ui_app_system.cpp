/* Porpoise UI - Settings > Console: the GameCube Menu (the player's own
 * BIOS) and the Wii Menu (the player's own, from their Wii System Data or its
 * WAD), each started with a game in its drive or none; and the player's
 * channels (WADs) put in the Wii Menu, installed by the core before the menu
 * starts. Also Wii System Data's removal: the imported Wii set aside, whole.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_pad.hpp"
#include "ui_app.hpp"

#include <algorithm>
#include <memory>
#include <pthread.h>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

#include "porpoise_bios.hpp"
#include "porpoise_covers.hpp"
#include "porpoise_sysupdate.hpp"
#include "title_threads.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
bool exists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

/* The Wii Menu a disc of this region needs: 'U', 'E', 'J', 'K', or 0. */
char menu_family(const std::string &region)
{
    if (region == "USA")
        return 'U';
    if (region == "Japan")
        return 'J';
    if (region == "Korea")
        return 'K';
    if (region.empty())
        return 0;
    return 'E'; /* Europe, Germany, France, Spain, Italy, Australia */
}

/* A GameCube BIOS's folder for a game's region: an NTSC one serves US and
 * Japanese games, a PAL one the rest. */
bool pal_region(const std::string &region)
{
    return region != "USA" && region != "Japan" && region != "Korea";
}

/* Wii System Update's region names, as the update service takes them. */
const char *const kUpdateRegions[] = {"USA", "EUR", "JPN", "KOR"};

std::string region_name(const std::string &code)
{
    return code == "EUR" ? tr("Europe") : code == "JPN" ? tr("Japan") : code == "KOR" ? tr("Korea") : tr("USA");
}

struct UpdateRun
{
    std::shared_ptr<porpoise::sysupdate::Job> job;
    std::string region, path;
};

void *run_update(void *arg)
{
    std::unique_ptr<UpdateRun> run(static_cast<UpdateRun *>(arg));
#ifndef PORPOISE_HOST_PREVIEW
    porpoise::sysupdate::fetch(run->region, run->path, *run->job);
#else
    run->job->state.store(2);
#endif
    return nullptr;
}

bool is_menu_wad(const Game &g)
{
    return g.format == "WAD" && g.id.empty() && g.title == "Wii Menu";
}

std::string hex4(const std::string &id)
{
    if (id.size() < 4)
        return "";
    char out[9];
    for (int i = 0; i < 4; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(id[std::size_t(i)]);
        if (c == '_')
            return ""; /* not a letter in the title ID: not known from here */
        std::snprintf(out + i * 2, 3, "%02x", c);
    }
    return std::string(out, 8);
}
} // namespace

bool App::wii_menu_installed() const
{
    return exists(saves_dir_ + "/User/Wii/title/00000001/00000002/content/title.tmd");
}

const Game *App::wii_menu_wad() const
{
    for (const Game &g : lib_->games())
        if (is_menu_wad(g))
            return &g;
    return nullptr;
}

char App::wii_menu_region() const
{
    std::FILE *f = std::fopen((saves_dir_ + "/User/Wii/title/00000001/00000002/content/title.tmd").c_str(), "rb");
    if (!f)
        return 0;
    unsigned char v[2] = {};
    const bool ok = std::fseek(f, 0x1DC, SEEK_SET) == 0 && std::fread(v, 1, 2, f) == 2;
    std::fclose(f);
    if (!ok)
        return 0;
    switch (v[1] & 0xF) /* as Dolphin's GetSysMenuRegion */
    {
    case 0: return 'J';
    case 1: return 'U';
    case 2: return 'E';
    case 6: return 'K';
    default: return 0;
    }
}

bool App::channel_installed(const Game &g) const
{
    const std::string lo = hex4(g.id);
    if (lo.empty())
        return false;
    for (const char *hi : {"00010001", "00010002", "00010004", "00010005", "00010008"})
        if (exists(saves_dir_ + "/User/Wii/title/" + hi + "/" + lo + "/content/title.tmd"))
            return true;
    return false;
}

void App::open_system(int mode)
{
    sys_ = SystemPanel{};
    sys_.mode = mode;
    const auto bioses = porpoise::bios::find_gamecube(data_dir_ + "/bios");
    const bool ntsc = porpoise::bios::for_region(bioses, false) != nullptr;
    const bool pal = porpoise::bios::for_region(bioses, true) != nullptr;
    const char menu = wii_menu_region();
    std::vector<const Game *> list;
    for (const Game &g : lib_->games())
    {
        if (g.format == "WAD" || g.format == "DOL" || g.format == "ELF" || !g.kind.empty())
        {
            if (mode == 3 && g.format == "WAD" && !is_menu_wad(g) && !hex4(g.id).empty())
                list.push_back(&g);
            continue;
        }
        if (mode == 1 && g.platform == "GameCube" && (pal_region(g.region) ? pal : ntsc))
            list.push_back(&g);
        /* A Wii shows "An incorrect disc has been inserted" for another
         * region's disc: only the Wii Menu's own are offered. */
        if (mode == 2 && g.platform == "Wii" && (menu == 0 || menu_family(g.region) == 0 || menu_family(g.region) == menu))
            list.push_back(&g);
    }
    std::sort(list.begin(), list.end(), [](const Game *a, const Game *b) { return a->title < b->title; });
    if (mode != 3)
        sys_.keys.push_back(""); /* No Disc */
    for (const Game *g : list)
    {
        sys_.keys.push_back(Library::key_of(*g));
        if (mode == 3)
        {
            const bool in = channel_installed(*g);
            sys_.installed.push_back(in);
            sys_.marked.push_back(0);
        }
    }
    sys_.gc_region = ntsc ? "USA" : "EUR";
    if (mode == 4)
    {
        sys_.keys.clear();
        for (const char *r : kUpdateRegions)
            sys_.keys.push_back(r);
    }
    sys_.open = true;
    sfx(Sound::DetailsFlip);
}

App::Action App::start_menu(int menu, const Game *disc, const std::string &gc_region)
{
    sysmenu_game_ = disc ? *disc : Game{};
    sysmenu_game_.boot_menu = menu;
    sysmenu_game_.boot_disc = disc != nullptr;
    /* Its own cover and art stay with the library's copy. */
    sysmenu_game_.cover = sysmenu_game_.disc = sysmenu_game_.back = sysmenu_game_.spine = nullptr;
    sysmenu_game_.cover_old = sysmenu_game_.disc_old = sysmenu_game_.back_old = sysmenu_game_.spine_old = nullptr;
    if (!disc)
    {
        sysmenu_game_.platform = menu == 1 ? "GameCube" : "Wii";
        sysmenu_game_.title = menu == 1 ? tr("GameCube Menu") : tr("Wii Menu");
        sysmenu_game_.region = gc_region == "EUR" ? "Europe" : "USA";
        if (menu == 1)
        {
            const auto bioses = porpoise::bios::find_gamecube(data_dir_ + "/bios");
            const porpoise::bios::GameCubeBios *b = porpoise::bios::for_region(bioses, gc_region == "EUR");
            sysmenu_game_.path = b ? b->path : std::string();
        }
        else
            sysmenu_game_.path = wii_menu_installed() ? saves_dir_ + "/User/Wii/title/00000001/00000002/content/title.tmd"
                                 : wii_menu_wad()     ? wii_menu_wad()->path
                                 : !pending_update_.empty() ? update_list_path() /* the update brings the menu */
                                                            : std::string();
        sysmenu_game_.file = sysmenu_game_.path.substr(sysmenu_game_.path.rfind('/') + 1);
    }
    else
        for (Game &g : lib_->games())
            if (Library::key_of(g) == Library::key_of(*disc))
                lib_->mark_played(g);
    /* No Wii Menu in Porpoise's Wii yet, but its WAD in the games: the core
     * installs it first. */
    if (menu == 2 && !wii_menu_installed() && pending_update_.empty())
        if (const Game *w = wii_menu_wad())
            wad_installs_.insert(wad_installs_.begin(), w->path);
    if (sysmenu_game_.path.empty())
    {
        wad_installs_.clear(); /* nothing left for the next start */
        pending_update_.clear();
        return Action::None;
    }
    sys_ = SystemPanel{};
    launch_ = &sysmenu_game_;
    launch_state_.clear();
    sfx(Sound::LaunchGame);
    return Action::Launch;
}

App::Action App::update_system(bool up, bool down)
{
    SystemPanel &p = sys_;
    const int n = int(p.keys.size());
    if (pressed(BtnCircle))
    {
        sys_ = SystemPanel{};
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    if (n > 0 && (up || down))
    {
        p.row = std::clamp(p.row + (down ? 1 : -1), 0, n - 1);
        sfx(Sound::MenuScroll);
    }
    auto game_at = [&](int row) -> const Game * {
        if (row < 0 || row >= n || p.keys[std::size_t(row)].empty())
            return nullptr;
        for (const Game &g : lib_->games())
            if (Library::key_of(g) == p.keys[std::size_t(row)])
                return &g;
        return nullptr;
    };
    if (p.mode == 4)
    {
        if (pressed(BtnCross) && n > 0)
        {
            update_region_ = p.keys[std::size_t(p.row)];
            sys_ = SystemPanel{};
            ask_sys_update();
        }
        return Action::None;
    }
    if (p.mode == 3)
    {
        if (pressed(BtnCross) && n > 0)
        {
            const std::size_t i = std::size_t(p.row);
            if (!p.installed[i])
            {
                p.marked[i] = !p.marked[i];
                sfx(Sound::GameRow);
            }
        }
        const bool any = std::find(p.marked.begin(), p.marked.end(), 1) != p.marked.end();
        if (pressed(BtnSquare) && n > 0)
        {
            /* Square with none chosen: every channel not in yet. */
            for (std::size_t i = 0; i < p.marked.size(); ++i)
                if (!p.installed[i] && (p.marked[i] || !any))
                    if (const Game *g = game_at(int(i)))
                        wad_installs_.push_back(g->path);
            if (wad_installs_.empty())
                return Action::None;
            return start_menu(2, nullptr);
        }
        return Action::None;
    }
    if (pressed(BtnCross) && n > 0)
    {
        const Game *g = game_at(p.row);
        if (p.row > 0 && !g)
            return Action::None;
        return start_menu(p.mode, g, p.gc_region);
    }
    return Action::None;
}

void App::draw_system()
{
    if (!sys_.open)
        return;
    SystemPanel &p = sys_;
    Gfx &g = *g_;
    p.anim = settings_ && settings_->reduced_motion ? 1.0f : std::min(1.0f, p.anim + float(ui_dt_) * 6.0f); /* (open in 1/6 s, at 60 or 120 Hz) */
    const float t = ease_out(p.anim);
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.62f * t), 1, 0);
    g.set_layer(0, 24.0f * (1.0f - t), t);

    const float w = 1180, x = 960 - w * 0.5f, pad = 56, text_w = w - pad * 2;
    const std::string title = p.mode == 1   ? tr("GameCube Menu")
                              : p.mode == 2 ? tr("Wii Menu")
                              : p.mode == 3 ? tr("Wii Channels")
                                            : tr("Wii System Update");
    const std::string about =
        p.mode == 4 ? tr("Your Wii's region: its system software is that region's, and it plays that region's "
                         "discs.")
        : p.mode == 1 ? tr("Starts your own GameCube's menu. Choose a game to put in its drive, or none for the menu "
                         "itself: Memory Cards, the calendar and its settings.")
        : p.mode == 2
            ? tr("Starts your own Wii's menu, with your Miis, channels and settings. Choose a Wii game to put in its "
                 "drive, or none. Only games of your Wii's region are listed, as a Wii only plays those.")
            : tr("Your channels (WADs in your games) put in the Wii Menu. Choose the ones you want with Cross, then "
                 "Square adds them and opens the Wii Menu. Square with none chosen adds every one.");
    const auto lines = wrap(g, Font::Regular, ts(26), about, text_w, 3);
    const int n = int(p.keys.size());
    const int visible = 8;
    const float row_h = 62;
    const float list_h = n == 0 ? 80 : float(std::min(n, visible)) * row_h;
    const float h = 124 + float(lines.size()) * 34 + 24 + list_h + 40;
    const float y = std::max(40.0f, 540 - h * 0.5f - 30);

    Glass face;
    face.tint = rgba(0x16348F, 0.92f);
    face.rim = rgba(0x8BD9FF);
    face.radius = kR;
    face.rim_w = 2.4f;
    face.glow = 14;
    face.phase = 0.4f;
    glass_block(g, 960, y + h * 0.5f, w, h, 18, 0, 0, 42, face);

    float ly = y + 66;
    g.text_mid(Font::Bold, ts(40), x + pad, ly, kWhite, Align::Left, fit(g, Font::Bold, ts(40), title, text_w));
    ly += 62;
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(26), x + pad, ly, kSoft, Align::Left, l);
        ly += 34;
    }
    ly += 24;
    if (n == 0)
    {
        g.text_mid(Font::SemiBold, ts(28), x + pad, ly + 30, kLavender, Align::Left,
                   fit(g, Font::SemiBold, ts(28),
                       p.mode == 3 ? tr("No channels in your games yet: add WAD files to a games folder.")
                                   : tr("No games for it in your library."),
                       text_w));
    }
    else
    {
        const int top = std::clamp(p.row - visible / 2, 0, std::max(0, n - visible));
        p.scroll = smooth(p.scroll, float(top), ui_dt_, 18.0f);
        for (int i = 0; i < n; ++i)
        {
            const float ry = ly + (float(i) - p.scroll) * row_h;
            if (ry < ly - row_h * 0.5f || ry > ly + list_h - row_h * 0.5f)
                continue;
            const bool sel = i == p.row;
            if (sel)
                g.panel(x + pad - 14, ry, text_w + 28, row_h - 8, rgba(0x5CD3FF, 0.22f), 1, kR, kIcy, 2.2f, 8,
                        0.2f);
            const Game *game = nullptr;
            if (p.mode != 4 && !p.keys[std::size_t(i)].empty())
                for (const Game &gm : lib_->games())
                    if (Library::key_of(gm) == p.keys[std::size_t(i)])
                    {
                        game = &gm;
                        break;
                    }
            std::string name = p.mode == 4 ? region_name(p.keys[std::size_t(i)]) : game ? game->title : tr("No Disc");
            std::string right;
            if (p.mode == 3)
                right = p.installed[std::size_t(i)] ? tr("In the Wii Menu")
                        : p.marked[std::size_t(i)]  ? tr("Chosen")
                                                    : "";
            else if (game)
                right = game->region;
            float rw = 0;
            if (!right.empty())
                rw = g.text_mid(Font::SemiBold, ts(24), x + pad + text_w, ry + (row_h - 8) * 0.5f,
                                p.mode == 3 && p.marked[std::size_t(i)] ? kCyan : kLavender, Align::Right, right);
            if (p.mode == 3 && !p.installed[std::size_t(i)])
            {
                /* A box to tick. */
                const float bx = x + pad, by = ry + (row_h - 8) * 0.5f - 14;
                g.panel(bx, by, 28, 28, rgba(0x07102E, 0.8f), 1, 7, kIcy, 2.0f);
                if (p.marked[std::size_t(i)])
                    g.panel(bx + 6, by + 6, 16, 16, kCyan, 1, 4);
            }
            const float nx = x + pad + (p.mode == 3 ? 44 : 0);
            g.text_mid(sel ? Font::Bold : Font::SemiBold, ts(28), nx, ry + (row_h - 8) * 0.5f, kWhite, Align::Left,
                       fit(g, Font::SemiBold, ts(28), name, text_w - rw - (nx - x - pad) - 30));
        }
    }

    g.set_layer();
    if (dialog_.open)
        return;
    drawing_dialog_ = true;
    if (n == 0)
        draw_prompts({}, {{Glyph::Circle, "Back"}}, "");
    else if (p.mode == 3)
        draw_prompts({{Glyph::Cross, "Choose"}, {Glyph::Square, "Add to the Wii Menu"}}, {{Glyph::Circle, "Back"}},
                     "");
    else
        draw_prompts({{Glyph::Cross, p.mode == 4 ? "Choose" : "Start"}}, {{Glyph::Circle, "Back"}}, "");
    drawing_dialog_ = false;
}

void App::ask_sys_update()
{
    open_dialog(DialogKind::SysUpdate, tr("Update your Wii's system software?"),
                trf("Porpoise downloads the Wii's system software for {region} (the Wii Menu, its IOS and channels) "
                    "from Nintendo's servers, as Dolphin's Online System Update does, and installs it in Porpoise's "
                    "Wii. It takes a few minutes; then the Wii Menu opens.",
                    {{"region", region_name(update_region_)}}),
                tr("Update"));
}

void App::start_sys_update()
{
    if (update_job_ || update_region_.empty())
        return;
    /* The console has one HTTPS session at a time: art being downloaded
     * would hold this request until it's done. */
    porpoise::covers::stop();
    update_job_ = std::make_shared<porpoise::sysupdate::Job>();
    auto *run = new UpdateRun{update_job_, update_region_, update_list_path()};
    pthread_t thread;
    if (create_title_thread(&thread, run_update, run) == 0)
        pthread_detach(thread);
    else
        run_update(run);
}

bool App::update_sys_update(Action &out)
{
    if (!update_job_)
        return false;
    const int state = update_job_->state.load();
    if (state == 0)
    {
        if (pressed(BtnCircle))
        {
            /* Not waiting any longer: the worker finishes on its own, and
             * leaves the list alone. */
            porpoise::sysupdate::cancel(*update_job_);
            update_job_.reset();
            covers_resume_ = true;
            sfx(Sound::DetailsFlip);
        }
        return true;
    }
    const std::shared_ptr<porpoise::sysupdate::Job> done = update_job_;
    update_job_.reset();
    if (state == 1)
    {
        /* The list is in: the core downloads and installs each title as the
         * Wii Menu starts, on the launch screen. */
        pending_update_ = update_region_;
        out = start_menu(2, nullptr);
        return false;
    }
    std::string why;
    {
        std::lock_guard<std::mutex> lock(done->m);
        why = done->error;
    }
    covers_resume_ = true;
    open_dialog(DialogKind::Info, tr("The update didn't start"), tr(why), "");
    return false;
}

void App::draw_sys_update()
{
    if (!update_job_)
        return;
    Gfx &g = *g_;
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.7f), 1, 0);
    const float w = 860, h = 220, x = 960 - w * 0.5f, y = 430;
    g.panel(x, y, w, h, rgba(0x13256F, 0.94f), 0.65f, kR, rgba(0x6FAEFF), 2.0f, 10, 0.25f);
    g.text_mid(Font::Bold, ts(34), 960, y + 74, kWhite, Align::Center, tr("Wii System Update"));
    const int dots = int(time_ * 3) % 4;
    g.text_mid(Font::SemiBold, ts(24), 960, y + 140, kSoft, Align::Center,
               tr("Asking for your Wii's system software") + std::string(std::size_t(dots), '.'));
    drawing_dialog_ = true;
    draw_prompts({}, {{Glyph::Circle, "Cancel"}}, "");
    drawing_dialog_ = false;
}

void App::remove_nand()
{
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", std::localtime(&now));
    const std::string dir = saves_dir_ + "/User/Wii-removed";
    ::mkdir(dir.c_str(), 0777);
    const std::string to = dir + "/" + stamp;
    if (std::rename((saves_dir_ + "/User/Wii").c_str(), to.c_str()) != 0)
    {
        open_dialog(DialogKind::Info, tr("Wii System Data wasn't removed"),
                    tr("Porpoise couldn't move its Wii folder aside. Nothing was changed."), "");
        return;
    }
    if (screen_ == Screen::Main && tab_ == Tab::Settings)
    {
        const int row = settings_row_;
        build_settings();
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
    }
    open_dialog(DialogKind::Info, tr("Wii System Data removed"),
                trf("Porpoise has a fresh Wii now. Everything that was in it (settings, Miis, channels and Wii saves) "
                    "is kept in saves/User/Wii-removed/{date}.",
                    {{"date", stamp}}),
                "");
    sfx(Sound::LaunchGame);
}
} // namespace porpoise::ui
