/* Porpoise - the launcher's own sound: menu effects and the menu music.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sounds and music by Ruben (@elripalda). The effects are short 48 kHz stereo
 * WAVs held in memory; the music is an Ogg Vorbis loop decoded as it plays
 * (stb_vorbis). Each frame the mix tops the audio port's queue up to ~50 ms,
 * so the menus never wait on sound and sound never lags the menus. */
#include "porpoise_sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "porpoise_audio.hpp"
#include "trace.hpp"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"

namespace porpoise::sound
{
namespace
{
constexpr std::size_t kQueueTarget = 3600; /* 48 kHz frames: 75 ms (a slow frame doesn't run it dry) */
constexpr int kVoices = 8;

struct Clip
{
    std::vector<std::int16_t> frames; /* interleaved stereo */
};

struct Voice
{
    int clip = -1;
    std::size_t at = 0; /* frame */
    float gain = 1, fade = 0; /* fade: taken off the gain each frame (stop) */
};

Clip g_clips[int(Effect::Count)];
Clip g_own[int(Effect::Count)]; /* the WAVs, kept for use_own_effects */
Voice g_voices[kVoices];
/* The two loops: 0 Porpoise's original (the legacy looks), 1 Soundscape (the new
 * look). g_music is the one playing; a change fades it out, then in. */
std::vector<unsigned char> g_music_files[2];
stb_vorbis *g_musics[2] = {nullptr, nullptr};
stb_vorbis *g_music = nullptr;
int g_track = 0, g_track_want = 0;
float g_switch = 1.0f; /* the change's own fade */
bool g_music_on = true, g_effects_on = true;
float g_music_volume = 0.3f, g_effects_volume = 0.8f;
float g_fade = 0.0f, g_fade_to = 1.0f, g_fade_step = 1.0f / 90.0f; /* per frame */
bool g_loaded = false;
/* A Wii disc's banner jingle: one at a time; the music steps back under it. */
std::vector<std::int16_t> g_jingle;
std::size_t g_jingle_at = 0;
float g_duck = 1.0f;

bool read_file(const std::string &path, std::vector<unsigned char> &out)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? std::size_t(n) : 0);
    const bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

/* A short Ogg Vorbis clip, 48 kHz stereo, decoded whole. */
bool load_ogg(const std::string &path, Clip &clip)
{
    std::vector<unsigned char> b;
    if (!read_file(path, b))
        return false;
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_memory(b.data(), int(b.size()), &error, nullptr);
    if (!v)
        return false;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.sample_rate == 48000 && info.channels == 2)
    {
        std::int16_t buf[4096];
        for (;;)
        {
            const int n = stb_vorbis_get_samples_short_interleaved(v, 2, buf, 4096);
            if (n <= 0)
                break;
            clip.frames.insert(clip.frames.end(), buf, buf + n * 2);
        }
    }
    stb_vorbis_close(v);
    return !clip.frames.empty();
}

/* A 16-bit stereo PCM WAV, any header layout. */
bool load_wav(const std::string &path, Clip &clip)
{
    std::vector<unsigned char> b;
    if (!read_file(path, b) || b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4))
        return false;
    std::size_t at = 12;
    int channels = 0, bits = 0;
    while (at + 8 <= b.size())
    {
        const std::uint32_t size = b[at + 4] | b[at + 5] << 8 | b[at + 6] << 16 | std::uint32_t(b[at + 7]) << 24;
        const unsigned char *body = b.data() + at + 8;
        if (!std::memcmp(b.data() + at, "fmt ", 4) && size >= 16)
        {
            channels = body[2] | body[3] << 8;
            bits = body[14] | body[15] << 8;
        }
        else if (!std::memcmp(b.data() + at, "data", 4) && bits == 16 && (channels == 1 || channels == 2))
        {
            const std::size_t n = std::min<std::size_t>(size, b.size() - at - 8) / 2;
            const auto *s = reinterpret_cast<const std::int16_t *>(body);
            if (channels == 2)
                clip.frames.assign(s, s + n - n % 2);
            else
                for (std::size_t i = 0; i < n; ++i)
                {
                    clip.frames.push_back(s[i]);
                    clip.frames.push_back(s[i]);
                }
            return true;
        }
        at += 8 + size + (size & 1);
    }
    return false;
}
} // namespace

bool load(const std::string &asset_dir)
{
    static const char *const names[int(Effect::Count)] = {"game-row",   "menu-scroll", "moving-tab", "details-flip",
                                                          "launch-game", "boot-sonar", "boot-sonar-legacy"};
    int ok = 0;
    for (int i = 0; i < int(Effect::Count); ++i)
    {
        /* (the boot's sound is an Ogg file: small) */
        ok += i >= int(Effect::BootSonar) ? (load_ogg(asset_dir + "/sounds/" + names[i] + ".ogg", g_clips[i]) ? 1 : 0)
                                          : (load_wav(asset_dir + "/sounds/" + names[i] + ".wav", g_clips[i]) ? 1 : 0);
        g_own[i] = g_clips[i];
    }
    static const char *const tracks[2] = {"/sounds/menu-music.ogg", "/sounds/menu-music-soundscape.ogg"};
    for (int t = 0; t < 2; ++t)
        if (read_file(asset_dir + tracks[t], g_music_files[t]))
        {
            int error = 0;
            stb_vorbis *v = stb_vorbis_open_memory(g_music_files[t].data(), int(g_music_files[t].size()), &error, nullptr);
            if (v)
            {
                const stb_vorbis_info info = stb_vorbis_get_info(v);
                if (info.sample_rate != 48000 || info.channels != 2)
                {
                    stb_vorbis_close(v); /* the mix is 48 kHz stereo; anything else is left out */
                    v = nullptr;
                }
            }
            g_musics[t] = v;
        }
    g_track = g_track_want;
    g_music = g_musics[g_track] ? g_musics[g_track] : g_musics[0];
    ps5::debug::mark_value("sound: effects loaded", ok);
    ps5::debug::mark(g_music ? "sound: menu music ready" : "sound: no menu music");
    g_loaded = true;
    return ok > 0 || g_music;
}

static bool is_boot(int clip)
{
    return clip == int(Effect::BootSonar) || clip == int(Effect::BootSonarLegacy);
}

/* The boot's hum is heard at the sounds' volume, or with the sounds off, the
 * music's (it rises into the music). */
static float boot_gain()
{
    return g_effects_on && g_effects_volume > 0.0f ? g_effects_volume : g_music_on ? g_music_volume : 0.0f;
}

void play(Effect e)
{
    if (!g_loaded || g_clips[int(e)].frames.empty())
    {
        ps5::debug::mark_value("sound: an effect not loaded, number", int(e));
        return;
    }
    if (is_boot(int(e)) ? boot_gain() <= 0.0f : (!g_effects_on || g_effects_volume <= 0.0f))
        return;
    /* The same effect again within 40 ms restarts it instead of stacking. */
    for (Voice &v : g_voices)
        if (v.clip == int(e) && v.at < 1920)
        {
            v.at = 0;
            return;
        }
    Voice *slot = nullptr;
    for (Voice &v : g_voices)
        if (v.clip < 0)
        {
            slot = &v;
            break;
        }
    if (!slot) /* all busy: the one furthest along gives way */
    {
        slot = &g_voices[0];
        for (Voice &v : g_voices)
            if (v.at > slot->at)
                slot = &v;
    }
    slot->clip = int(e);
    slot->at = 0;
    slot->gain = 1;
    slot->fade = 0;
}

void stop(Effect e)
{
    for (Voice &v : g_voices)
        if (v.clip == int(e))
            v.fade = 1.0f / (0.25f * 48000.0f); /* a quarter of a second */
}

void set_effect(Effect e, const std::vector<std::int16_t> &frames)
{
    for (Voice &v : g_voices)
        if (v.clip == int(e))
            v.clip = -1; /* the old one stops */
    g_clips[int(e)].frames = frames;
    if (g_clips[int(e)].frames.size() % 2)
        g_clips[int(e)].frames.pop_back();
}

void use_own_effects()
{
    for (int i = 0; i < int(Effect::Count); ++i)
        set_effect(Effect(i), g_own[i].frames);
}

void set_music(bool on, float volume)
{
    g_music_on = on;
    g_music_volume = std::clamp(volume, 0.0f, 1.0f);
}

void set_music_track(int track)
{
    g_track_want = std::clamp(track, 0, 1);
}

void set_effects(bool on, float volume)
{
    g_effects_on = on;
    g_effects_volume = std::clamp(volume, 0.0f, 1.0f);
}

void play_jingle(const std::int16_t *frames, std::size_t count)
{
    if (!frames || count == 0)
    {
        g_jingle.clear();
        g_jingle_at = 0;
        return;
    }
    g_jingle.assign(frames, frames + count * 2);
    g_jingle_at = 0;
}

void fade_music(float to, float seconds)
{
    g_fade_to = std::clamp(to, 0.0f, 1.0f);
    g_fade_step = seconds > 0.01f ? 1.0f / (seconds * 60.0f) : 1.0f;
}

void pump()
{
    if (!g_loaded)
        return;
    /* The fade moves once a frame. */
    if (g_fade < g_fade_to)
        g_fade = std::min(g_fade_to, g_fade + g_fade_step);
    else if (g_fade > g_fade_to)
        g_fade = std::max(g_fade_to, g_fade - g_fade_step);

    const std::size_t queued = porpoise::audio::queued();
    if (queued >= kQueueTarget)
        return;
    const std::size_t n = std::min<std::size_t>(kQueueTarget - queued, 7200);
    static std::vector<float> mix;
    static std::vector<std::int16_t> pcm, music;
    mix.assign(n * 2, 0.0f);

    /* Music, a gentle bed. The gain curve is squared so the low end of the
     * volume setting stays usable. */
    const bool jingle = g_jingle_at < g_jingle.size() / 2;
    g_duck = jingle ? std::max(0.2f, g_duck - 0.05f) : std::min(1.0f, g_duck + 0.02f);
    /* Another loop asked for (the look changed): this one fades out, the other
     * comes in from its start. */
    if (g_track_want != g_track)
    {
        g_switch = std::max(0.0f, g_switch - 1.0f / 24.0f);
        if (g_switch <= 0.0f)
        {
            g_track = g_track_want;
            if (g_musics[g_track])
            {
                g_music = g_musics[g_track];
                stb_vorbis_seek_start(g_music);
            }
        }
    }
    else
        g_switch = std::min(1.0f, g_switch + 1.0f / 45.0f);
    const float music_gain =
        g_music_on ? g_music_volume * g_music_volume * g_fade * g_duck * g_switch * g_switch : 0.0f;
    if (g_music && music_gain > 0.0005f)
    {
        music.resize(n * 2);
        std::size_t got = 0;
        int guard = 0;
        while (got < n && guard++ < 4)
        {
            const int r = stb_vorbis_get_samples_short_interleaved(g_music, 2, music.data() + got * 2, int((n - got) * 2));
            if (r <= 0)
                stb_vorbis_seek_start(g_music); /* the loop's end meets its start */
            else
                got += std::size_t(r);
        }
        for (std::size_t i = 0; i < got * 2; ++i)
            mix[i] += music[i] * music_gain;
    }

    /* Effects. */
    const float fx_gain = g_effects_on ? g_effects_volume : 0.0f;
    for (Voice &v : g_voices)
    {
        if (v.clip < 0)
            continue;
        const auto &f = g_clips[v.clip].frames;
        const std::size_t total = f.size() / 2;
        const float gain = is_boot(v.clip) ? boot_gain() : fx_gain;
        std::size_t i = 0;
        for (; i < n && v.at < total; ++i, ++v.at)
        {
            if (v.fade > 0 && (v.gain -= v.fade) <= 0)
            {
                v.at = total;
                break;
            }
            mix[i * 2] += f[v.at * 2] * gain * v.gain;
            mix[i * 2 + 1] += f[v.at * 2 + 1] * gain * v.gain;
        }
        if (v.at >= total)
            v.clip = -1;
    }

    if (jingle && g_effects_on)
    {
        const float gain = std::max(0.35f, g_effects_volume) * 0.9f;
        for (std::size_t i = 0; i < n && g_jingle_at < g_jingle.size() / 2; ++i, ++g_jingle_at)
        {
            mix[i * 2] += g_jingle[g_jingle_at * 2] * gain;
            mix[i * 2 + 1] += g_jingle[g_jingle_at * 2 + 1] * gain;
        }
    }

    pcm.resize(n * 2);
    for (std::size_t i = 0; i < n * 2; ++i)
        pcm[i] = static_cast<std::int16_t>(std::clamp(mix[i], -32768.0f, 32767.0f));
    porpoise::audio::push_direct(pcm.data(), n);
}
} // namespace porpoise::sound
