/* Porpoise UI - a game's Cheats and Patches > Add a Code: a Gecko code typed
 * on a hex pad (its digits only; Porpoise spaces them into the code's lines),
 * with a name, into the player's own file for the game
 * (/data/porpoise/cheats/<ID>.ini, Dolphin's format), where it's on unless
 * turned off. As Dolphin's Properties > Gecko Codes > Add New Code.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_pad.hpp"
#include "ui_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
constexpr int kHexCols = 4, kHexRows = 5; /* four rows of digits, then Delete and Save */
constexpr float kHexW = 118, kHexH = 74, kHexGap = 10;
const char kHex[] = "0123456789ABCDEF";

/* The digits as a Gecko code's lines: "XXXXXXXX YYYYYYYY". */
std::vector<std::string> code_lines(const std::string &digits)
{
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < digits.size(); i += 16)
    {
        std::string line = digits.substr(i, 8);
        if (digits.size() > i + 8)
            line += " " + digits.substr(i + 8, 8);
        lines.push_back(line);
    }
    return lines;
}

std::string read_all(const std::string &path)
{
    std::string text;
    if (std::FILE *f = std::fopen(path.c_str(), "rb"))
    {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            text.append(buf, n);
        std::fclose(f);
    }
    return text;
}
} // namespace

void App::open_code()
{
    code_ = CodePanel{};
    code_.open = true;
    code_.name = "";
    sfx(Sound::DetailsFlip);
}

/* Into <cheats>/<ID>.ini's [Gecko] section (made when it isn't there). */
bool App::save_code()
{
    if (!game_for_ || game_for_->id.size() != 6)
        return false;
    const std::string dir = data_dir_ + "/cheats";
    ::mkdir(dir.c_str(), 0777);
    const std::string path = dir + "/" + game_for_->id + ".ini";
    std::string text = read_all(path);
    std::string name = code_.name.empty() ? tr("My Code") : code_.name;
    /* Brackets would be read as the author's name. */
    std::replace(name.begin(), name.end(), '[', '(');
    std::replace(name.begin(), name.end(), ']', ')');
    {
        /* A name the file already has gets a number. */
        const std::string base = name;
        for (int n = 2; text.find("$" + name + "\n") != std::string::npos ||
                        text.find("$" + name + "\r") != std::string::npos;
             ++n)
            name = base + " " + std::to_string(n);
    }
    std::string block = "$" + name + "\n";
    for (const std::string &l : code_lines(code_.digits))
        block += l + "\n";
    if (!text.empty() && text.back() != '\n')
        text += "\n";
    const std::size_t at = text.find("[Gecko]");
    if (at == std::string::npos)
        text += (text.empty() ? "" : "\n") + std::string("[Gecko]\n") + block;
    else
    {
        /* The end of the section: the next "[...]" line, or the file's end. */
        std::size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end + 1;
        std::size_t next = end;
        while (next < text.size())
        {
            if (text[next] == '[')
                break;
            const std::size_t nl = text.find('\n', next);
            next = nl == std::string::npos ? text.size() : nl + 1;
        }
        text.insert(next, block);
    }
    const std::string part = path + ".part";
    std::FILE *f = std::fopen(part.c_str(), "wb");
    const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (!f || std::fclose(f) != 0 || !ok || std::rename(part.c_str(), path.c_str()) != 0)
    {
        std::remove(part.c_str());
        return false;
    }
    return true;
}

App::Action App::update_code(bool up, bool down, bool left, bool right)
{
    CodePanel &p = code_;
    if (p.naming)
    {
        if (keyboard_update(p.kb, p.name, 40, true, up, down, left, right))
        {
            p.naming = false;
            sfx(Sound::DetailsFlip);
        }
        return Action::None;
    }
    if (pressed(BtnCircle))
    {
        code_ = CodePanel{};
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    if (up)
        p.kr = (p.kr + kHexRows - 1) % kHexRows;
    if (down)
        p.kr = (p.kr + 1) % kHexRows;
    if (left)
        p.kc = (p.kc + kHexCols - 1) % kHexCols;
    if (right)
        p.kc = (p.kc + 1) % kHexCols;
    if (up || down || left || right)
        sfx(Sound::MenuScroll);
    auto erase = [&] {
        if (!p.digits.empty())
            p.digits.pop_back();
        p.message.clear();
        sfx(Sound::MenuScroll);
    };
    auto save = [&] {
        if (p.digits.empty() || p.digits.size() % 16 != 0)
        {
            p.message = p.digits.empty()
                            ? tr("Type the code's digits first.")
                            : trf("Each line of a Gecko code is 16 digits: {n} more to finish this one.",
                                  {{"n", std::to_string(16 - p.digits.size() % 16)}});
            sfx(Sound::MovingTab);
            return;
        }
        if (!save_code())
        {
            p.message = tr("The code couldn't be saved: is the drive full?");
            sfx(Sound::MovingTab);
            return;
        }
        const std::string name = p.name.empty() ? tr("My Code") : p.name;
        code_ = CodePanel{};
        sfx(Sound::LaunchGame);
        /* The new card, on, with the game's other codes. */
        const int row = settings_row_;
        build_game_settings();
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
        applied_note_ = trf("{name} added. It's on; it applies when the game starts.", {{"name", name}});
        applied_note_time_ = time_;
    };
    if (pressed(BtnTriangle))
    {
        p.naming = true;
        p.kb = Keyboard{};
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    if (pressed(BtnSquare))
        erase();
    if (pressed(BtnOptions))
        save();
    if (pressed(BtnCross))
    {
        if (p.kr == kHexRows - 1)
        {
            if (p.kc < 2)
                erase();
            else
                save();
        }
        else if (p.digits.size() < 16 * 256)
        {
            p.digits += kHex[p.kr * kHexCols + p.kc];
            p.message.clear();
            sfx(Sound::GameRow);
        }
    }
    return Action::None;
}

void App::draw_code()
{
    if (!code_.open)
        return;
    CodePanel &p = code_;
    Gfx &g = *g_;
    p.anim = settings_ && settings_->reduced_motion ? 1.0f : std::min(1.0f, p.anim + float(ui_dt_) * 6.0f); /* (open in 1/6 s, at 60 or 120 Hz) */
    const float t = ease_out(p.anim);
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.62f * t), 1, 0);
    g.set_layer(0, 24.0f * (1.0f - t), t);

    Glass face;
    face.tint = rgba(0x16348F, 0.92f);
    face.rim = rgba(0x8BD9FF);
    face.radius = kR;
    face.rim_w = 2.4f;
    face.glow = 14;
    face.phase = 0.4f;

    /* The code on the left, the pad on the right. */
    const float y = 60, h = 860, x = 110, w = 1700;
    glass_block(g, x + w * 0.5f, y + h * 0.5f, w, h, 18, 0, 0, 42, face);
    const float pad = 56, left_w = 900;
    float ly = y + 70;
    g.text_mid(Font::Bold, ts(40), x + pad, ly, kWhite, Align::Left, tr("Add a Code"));
    ly += 52;
    g.text_mid(Font::Regular, ts(24), x + pad, ly, kSoft, Align::Left,
               fit(g, Font::Regular, ts(24),
                   trf("A Gecko code for {game}: type its digits, and Porpoise spaces them into lines.",
                       {{"game", game_for_ ? game_for_->title : std::string()}}),
                   w - pad * 2));
    ly += 56;
    /* Its name. */
    g.text_mid(Font::SemiBold, ts(24), x + pad, ly, kLavender, Align::Left, tr("Name"));
    ly += 22;
    g.panel(x + pad, ly, left_w, 60, rgba(0x07102E, 0.85f), 1, kR, p.naming ? kIcy : rgba(0x3D5AB0, 0.8f),
            p.naming ? 2.2f : 1.4f);
    if (!p.name.empty())
        g.text_mid(Font::SemiBold, ts(28), x + pad + 22, ly + 30, kWhite, Align::Left,
                   fit(g, Font::SemiBold, ts(28), p.name, left_w - 44));
    else
        g.text_mid(Font::Regular, ts(26), x + pad + 22, ly + 30, rgba(0x8A96C8), Align::Left, tr("My Code"));
    ly += 92;
    /* Its lines, the last ones in view. */
    g.text_mid(Font::SemiBold, ts(24), x + pad, ly, kLavender, Align::Left,
               trf("Code ({n} digits)", {{"n", std::to_string(p.digits.size())}}));
    ly += 22;
    const float box_h = 470;
    g.panel(x + pad, ly, left_w, box_h, rgba(0x07102E, 0.85f), 1, kR, rgba(0x3D5AB0, 0.8f), 1.4f);
    const std::vector<std::string> lines = code_lines(p.digits);
    const int shown = 11;
    const int first = std::max(0, int(lines.size()) - shown);
    float cy = ly + 34;
    for (int i = first; i < int(lines.size()); ++i)
    {
        g.text_mid(Font::SemiBold, ts(30), x + pad + 26, cy, kWhite, Align::Left, lines[std::size_t(i)]);
        cy += 40;
    }
    if (!p.naming && std::fmod(time_, 1.0) < 0.6)
    {
        /* Where the next digit goes. */
        const std::string last = lines.empty() || p.digits.size() % 16 == 0 ? std::string() : lines.back();
        const float line_y = lines.empty() || p.digits.size() % 16 == 0 ? cy : cy - 40;
        const float cx = x + pad + 26 + g.measure(Font::SemiBold, ts(30), last);
        g.panel(cx + 2, line_y - 18, 3, 36, kIcy, 1, 0);
    }
    if (!p.message.empty())
        g.text_mid(Font::SemiBold, ts(24), x + pad, ly + box_h + 30, kDanger, Align::Left,
                   fit(g, Font::SemiBold, ts(24), p.message, left_w));

    /* The pad. */
    const float px = x + pad + left_w + 70, py = y + 300;
    for (int r = 0; r < kHexRows; ++r)
    {
        const float ry = py + float(r) * (kHexH + kHexGap);
        auto key = [&](float kx, float kw, bool on, const std::string &label, float size) {
            g.panel(kx, ry, kw, kHexH, on ? rgba(0x1F63F0, 0.95f) : rgba(0x07102E, 0.6f), 1, kR,
                    on ? kIcy : rgba(0x3D5AB0, 0.7f), on ? 2.2f : 1.2f, on ? 8 : 0, on ? 0.25f : 0.0f);
            g.text_mid(Font::Bold, ts(size), kx + kw * 0.5f, ry + kHexH * 0.5f, kWhite, Align::Center,
                       fit(g, Font::Bold, ts(size), label, kw - 14));
        };
        const bool focus = !p.naming && p.kr == r;
        if (r == kHexRows - 1)
        {
            const float wide = 2 * kHexW + kHexGap;
            key(px, wide, focus && p.kc < 2, tr("Delete"), 26);
            key(px + wide + kHexGap, wide, focus && p.kc >= 2, tr("Save"), 26);
            continue;
        }
        for (int c = 0; c < kHexCols; ++c)
            key(px + float(c) * (kHexW + kHexGap), kHexW, focus && p.kc == c,
                std::string(1, kHex[r * kHexCols + c]), 38);
    }

    if (p.naming)
        draw_keyboard(p.kb, y + h - keyboard_height_for(p.kb) - 30, face);
    g.set_layer();
    if (dialog_.open)
        return;
    drawing_dialog_ = true;
    if (p.naming)
        draw_keyboard_prompts();
    else
        draw_prompts({{Glyph::Cross, "Type"}, {Glyph::Square, "Delete"}, {Glyph::Triangle, "Name"}},
                     {{Glyph::Circle, "Cancel"}, {Glyph::Options, "Save"}}, "");
    drawing_dialog_ = false;
}
} // namespace porpoise::ui
