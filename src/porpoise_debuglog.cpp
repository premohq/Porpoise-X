/* Porpoise - the debug folder (porpoise_debuglog.hpp).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_debuglog.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace porpoise::debuglog
{
namespace
{
std::mutex g_lock;
bool g_on = false;
std::string g_root;  /* <data>/debug */
std::string g_trace; /* trace.txt */
std::FILE *g_app = nullptr;
std::string g_game_dir;
std::FILE *g_game = nullptr, *g_move = nullptr, *g_camera = nullptr, *g_hub = nullptr;
long g_trace_from = 0; /* where trace.txt was when the game started */
std::FILE *g_perf = nullptr;
std::vector<PerfSecond> g_seconds;
PerfContext g_ctx;
std::string g_game_title;
std::string g_game_id; /* its disc ID, for the picks' section */
long long g_move_bytes = 0, g_camera_bytes = 0, g_hub_bytes = 0;
constexpr long long kMaxFile = 60LL << 20; /* a file stops growing past 60 MB */
timespec g_t0{};

double seconds()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return double(t.tv_sec - g_t0.tv_sec) + double(t.tv_nsec - g_t0.tv_nsec) * 1e-9;
}

std::string stamp(const char *fmt = "%Y%m%d-%H%M%S")
{
    char b[32] = "0";
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    if (localtime_r(&now, &tm))
        std::strftime(b, sizeof b, fmt, &tm);
    return b;
}

void make(const std::string &dir)
{
    mkdir(dir.c_str(), 0777);
}

/* Keep the newest keep entries of dir starting with prefix (names sort by time). */
void prune(const std::string &dir, const char *prefix, std::size_t keep, bool folders)
{
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str()))
    {
        while (const dirent *e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n != "." && n != ".." && n.compare(0, std::strlen(prefix), prefix) == 0)
                names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (std::size_t i = 0; i + keep < names.size(); ++i)
    {
        const std::string path = dir + "/" + names[i];
        if (folders)
        {
            if (DIR *d = opendir(path.c_str()))
            {
                std::vector<std::string> files;
                while (const dirent *e = readdir(d))
                    if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
                        files.push_back(path + "/" + e->d_name);
                closedir(d);
                for (const std::string &f : files)
                    std::remove(f.c_str());
            }
            rmdir(path.c_str());
        }
        else
            std::remove(path.c_str());
    }
}

/* Copy the end of from (past offset, at most max bytes) into to. */
void copy_tail(const std::string &from, long offset, const std::string &to, long max)
{
    std::FILE *in = std::fopen(from.c_str(), "rb");
    if (!in)
        return;
    std::fseek(in, 0, SEEK_END);
    const long size = std::ftell(in);
    long start = offset >= 0 && offset <= size ? offset : 0;
    if (size - start > max)
        start = size - max;
    std::fseek(in, start, SEEK_SET);
    if (std::FILE *out = std::fopen(to.c_str(), "wb"))
    {
        char buf[16384];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
            std::fwrite(buf, 1, n, out);
        std::fclose(out);
    }
    std::fclose(in);
}

long file_size(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? long(st.st_size) : -1;
}

/* A game's name as a folder name: letters, digits and a few marks. */
std::string folder_name(const std::string &title, const std::string &id)
{
    std::string out;
    for (char c : title)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || c == ' ' || c == '-' ||
            c == '_' || c == '(' || c == ')' || c == '.' || c == ',' || c == '\'' || c == '!' || c == '&')
        {
            if (!(c == ' ' && !out.empty() && out.back() == ' '))
                out += c;
        }
        else if (u >= 0x80)
            continue; /* (accents and the like: left out) */
        else if (!out.empty() && out.back() != ' ')
            out += ' '; /* (a colon, a slash: a space, one) */
        if (out.size() >= 60)
            break;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.empty())
        out = "Game";
    if (!id.empty())
        out += " [" + id + "]";
    return out;
}

void readme()
{
    if (std::FILE *f = std::fopen((g_root + "/README.txt").c_str(), "w"))
    {
        std::fputs("Porpoise's debug folder. Nothing here is sent anywhere: zip what you need and send it on the "
                   "Discord.\n"
                   "Turn it off in Settings > About > Debug Logs.\n\n"
                   "app/          a log each time Porpoise starts: the screens you went to, what you chose, what "
                   "happened\n"
                   "crashes/      a crash's report, with the last lines of the app log before it\n"
                   "controllers/  the Controller Hub's motion, a line a frame (hub-<date>-<time>.csv)\n"
                   "games/        a folder per game, and in it one per play (<date>-<time>):\n"
                   "                game.log      what it was started with, and what happened\n"
                   "                performance.txt  what held the game back (the CPU, the graphics...), in words, "
                   "and what to try\n"
                   "                performance.csv  the same a second at a time: frame rate, speed, stutters, each "
                   "thread's time\n"
                   "                motion-*.csv  what the controller felt and what the game was given, a line a "
                   "frame\n"
                   "                move.csv      the PlayStation Move's own readings, every one\n"
                   "                camera.csv    where the PS3 Eye saw the Move's ball\n"
                   "                core.log      Dolphin's log for that play\n"
                   "                trace.txt     Porpoise's trace while it ran\n\n"
                   "Each game keeps its last 10 plays; app logs and crashes their last 20.\n",
                   f);
        std::fclose(f);
    }
}

void line(std::FILE *f, const char *fmt, va_list args)
{
    if (!f)
        return;
    std::fprintf(f, "%s %8.3f  ", stamp("%H:%M:%S").c_str(), seconds());
    std::vfprintf(f, fmt, args);
    std::fputc('\n', f);
    std::fflush(f);
}
/* What held a second back: "" at full speed. (4.6: the PS5's GPU told from Dolphin's video
 * thread, shaders made on the spot, one core.) */
const char *limit_of(const PerfSecond &s)
{
    const float budget = 1000.0f / std::max(10.0f, s.game_hz);
    if (s.fast)
        return "fast forward";
    if (s.speed >= 97.0f)
        return s.stutters > 0 ? (s.shader_ms > 20.0f ? "shaders" : "stutter") : "";
    if (s.shader_ms >= 150.0f)
        return "shaders"; /* a sixth of the second or more spent making shaders */
    if (s.gpu_wait_ms >= 0.5f * budget)
        return "gpu";
    if (s.cpu_ms >= 0.85f * budget && s.cpu_ms >= s.gpu_ms)
        return "cpu";
    if (s.gpu_ms >= 0.85f * budget)
        return "video";
    if (s.run_ms >= 0.85f * budget)
        return "core";
    if (s.present_ms >= 0.3f * budget)
        return "present";
    if (s.audio_wait_ms > 150)
        return "audio";
    return "unclear";
}

std::string clock_of(float t)
{
    char b[16];
    const int m = int(t) / 60, sec = int(t) % 60;
    std::snprintf(b, sizeof b, "%d:%02d", m, sec);
    return b;
}

/* performance.txt: the play's performance in words. */
void write_summary(const std::string &path)
{
    std::FILE *f = std::fopen(path.c_str(), "w");
    if (!f)
        return;
    std::vector<const PerfSecond *> play;
    for (const PerfSecond &s : g_seconds)
        if (!s.fast)
            play.push_back(&s);
    std::fprintf(f, "Performance: %s\n\n", g_game_title.c_str());
    if (play.size() < 5)
    {
        std::fprintf(f, "Too short a play to say (%zu seconds).\n", play.size());
        std::fclose(f);
        return;
    }
    const float hz = play.back()->game_hz, budget = 1000.0f / std::max(10.0f, hz);
    double fps = 0, speed = 0, cpu = 0, gpu = 0, run = 0, wait = 0, shader_ms = 0;
    int full = 0, stutters = 0, shaders = 0, single = 0, shader_stutter_secs = 0;
    float worst = 0, worst_at = 0;
    enum
    {
        kCpu,
        kVideo,
        kCore,
        kPresent,
        kAudio,
        kUnclear,
        kStutter,
        kGpu,
        kShaders,
        kKinds
    };
    int by[kKinds] = {};
    const char *const kinds[kKinds] = {"cpu", "video", "core", "present", "audio", "unclear", "stutter", "gpu", "shaders"};
    for (const PerfSecond *s : play)
    {
        fps += s->fps, speed += s->speed, cpu += s->cpu_ms, gpu += s->gpu_ms, run += s->run_ms;
        wait += s->gpu_wait_ms, shader_ms += s->shader_ms;
        shaders += s->shaders;
        single += s->single_core ? 1 : 0;
        stutters += s->stutters;
        if (s->stutters > 0 && s->shader_ms > 20.0f)
            ++shader_stutter_secs;
        if (s->worst_ms > worst)
            worst = s->worst_ms, worst_at = s->t;
        const char *l = limit_of(*s);
        if (s->speed >= 97.0f)
        {
            ++full;
            if (*l)
                ++by[kStutter]; /* (a stutter at full speed, shaders or not: not "held back") */
            continue;
        }
        for (int k = 0; k < kKinds; ++k)
            if (std::strcmp(l, kinds[k]) == 0)
                ++by[k];
    }
    const bool one_core = single * 2 > int(play.size());
    const double n = double(play.size());
    const int slow = int(play.size()) - full;
    std::fprintf(f, "Played %s. The game runs at %.0f Hz: a frame every %.1f ms.\n", clock_of(float(n)).c_str(), hz, budget);
    std::fprintf(f, "Average: %.1f frames a second, %.0f%% speed. Full speed %.0f%% of the time.\n", fps / n, speed / n,
                 100.0 * full / n);
    if (one_core)
        std::fprintf(f, "Each frame, on average: Dolphin's emulated CPU and its graphics together %.1f ms (one thread), "
                        "the whole run %.1f ms (of the %.1f ms there is).\n",
                     cpu / n, run / n, budget);
    else
        std::fprintf(f, "Each frame, on average: Dolphin's emulated CPU %.1f ms, its video thread %.1f ms, the whole run "
                        "%.1f ms (of the %.1f ms there is).\n",
                     cpu / n, gpu / n, run / n, budget);
    std::fprintf(f, "The PS5's GPU kept the video thread waiting %.1f ms a frame.\n", wait / n);
    std::fprintf(f, "Shaders made while the game waited: %d (%.1f s in all).\n", shaders, shader_ms / 1000.0);
    if (one_core)
        std::fprintf(f, "This game runs on one core: Dolphin's own settings for it say so (Dual Core is off for it "
                        "whatever Porpoise's setting).\n");
    std::fprintf(f, "\n");
    if (slow > 0)
    {
        std::fprintf(f, "Below full speed %s in all. What held it back:\n", clock_of(float(slow)).c_str());
        auto part = [&](int k, const char *words) {
            if (by[k] > 0)
                std::fprintf(f, "  %3.0f%%  %s\n", 100.0 * by[k] / slow, words);
        };
        part(kCpu, one_core ? "the emulated CPU and the graphics on one thread (one core): the PS5's CPU is the limit"
                            : "the emulated CPU (Dolphin's CPU thread needed more than a frame's time): the PS5's CPU is "
                              "the limit");
        part(kGpu, "the PS5's GPU (the video thread waited for it to finish frames): the resolution or the effects");
        part(kVideo, "Dolphin's video thread (the graphics' CPU side, not the GPU)");
        part(kShaders, "shaders being made while the game waited");
        part(2, "the core's frame as a whole, neither thread alone (waiting between them, or loading)");
        part(3, "showing the frame (the display or V-Sync)");
        part(4, "waiting for the sound");
        part(5, "not clear from the timings (often loading, or the game itself running slow)");
        std::fprintf(f, "\n");
    }
    else
        std::fprintf(f, "Full speed the whole time.\n\n");
    std::fprintf(f, "Stutters (a frame over %.0f ms): %d. The worst, %.0f ms, at %s.\n", budget * 1.5f, stutters, worst,
                 clock_of(worst_at).c_str());
    {
        /* When they came: the first minute (shaders built the first time) or later. */
        int early = 0;
        for (const PerfSecond *s : play)
            if (s->t < 60)
                early += s->stutters;
        if (stutters > 0)
            std::fprintf(f, "%d of them in the first minute%s.\n", early,
                         early * 2 > stutters ? " (most likely shaders being built the first time they're seen)" : "");
    }
    /* The slow stretches: seconds under full speed in a row, the longest first,
     * with what held each back most. */
    struct Stretch
    {
        float from, to;
        double fps = 0, speed = 0, cpu = 0, gpu = 0;
        int n = 0, by[kKinds] = {};
    };
    std::vector<Stretch> stretches;
    for (std::size_t i = 0; i < play.size(); ++i)
    {
        const PerfSecond &p = *play[i];
        if (p.speed >= 97.0f)
            continue;
        if (stretches.empty() || p.t - stretches.back().to > 2.5f)
            stretches.push_back({p.t, p.t});
        Stretch &st = stretches.back();
        st.to = p.t;
        st.fps += p.fps, st.speed += p.speed, st.cpu += p.cpu_ms, st.gpu += p.gpu_ms;
        ++st.n;
        const char *l = limit_of(p);
        for (int k = 0; k < kKinds; ++k)
            if (std::strcmp(l, kinds[k]) == 0)
                ++st.by[k];
    }
    std::sort(stretches.begin(), stretches.end(), [](const Stretch &a, const Stretch &b) { return a.n > b.n; });
    if (!stretches.empty())
    {
        std::fprintf(f, "\nThe slow stretches (the longest first):\n");
        for (std::size_t i = 0; i < stretches.size() && i < 6; ++i)
        {
            const Stretch &st = stretches[i];
            int top = kUnclear;
            for (int k = 0; k < kKinds; ++k)
                if (k != kStutter && st.by[k] > st.by[top])
                    top = k;
            const char *const why[kKinds] = {"the emulated CPU", "the video thread", "the core's frame", "showing the frame",
                                             "the sound", "not clear", "stutters", "the PS5's GPU", "shaders"};
            if (one_core)
                std::fprintf(f, "  %s to %s (%d s): %.0f fps, %.0f%% speed, CPU and graphics %.1f ms a frame. Held back "
                                "by: %s.\n",
                             clock_of(st.from).c_str(), clock_of(st.to + 1).c_str(), st.n, st.fps / st.n,
                             st.speed / st.n, st.cpu / st.n, why[top]);
            else
                std::fprintf(f, "  %s to %s (%d s): %.0f fps, %.0f%% speed, CPU %.1f ms, video %.1f ms a frame. Held back "
                                "by: %s.\n",
                             clock_of(st.from).c_str(), clock_of(st.to + 1).c_str(), st.n, st.fps / st.n,
                             st.speed / st.n, st.cpu / st.n, st.gpu / st.n, why[top]);
        }
    }
    /* What to try, from what held it back and what it was set to. */
    std::fprintf(f, "\nWhat to try:\n");
    bool any = false;
    if (by[kCpu] * 3 >= slow && slow > 0)
    {
        any = true;
        std::fprintf(f, "  The CPU side: a lower resolution won't help.\n");
        if (one_core)
            std::fprintf(f, "  - Dolphin keeps this game on one core. Dual core with Sync GPU may run it faster: "
                            "Porpoise's Picks can try it (below), tested first.\n");
        if (!g_ctx.dual_core && !one_core)
            std::fprintf(f, "  - Dual Core is off: turn it on.\n");
        if (!g_ctx.own_cores)
            std::fprintf(f, "  - Emulator on Its Own Cores is off: turn it on.\n");
        if (!g_ctx.fast_float)
            std::fprintf(f, "  - Riptide Boost (beta) is off: it can help, though a few games act up with it.\n");
        std::fprintf(f, "  - The game's own heavy moments (many objects, physics) are the PS5 CPU running Dolphin's "
                        "CPU emulation: send this file.\n");
    }
    if ((by[kGpu] + by[kVideo]) * 3 >= slow && slow > 0)
    {
        any = true;
        std::fprintf(f, "  The graphics side:\n");
        if (g_ctx.resolution > 2)
            std::fprintf(f, "  - Internal Resolution is %dx: one step lower is the biggest saving.\n", g_ctx.resolution);
        if (g_ctx.frame_gen)
            std::fprintf(f, "  - Frame Generation (beta) is on: it costs GPU time.\n");
        std::fprintf(f, "  - Graphics extras (per-pixel lighting, enhancements) cost GPU time too.\n");
    }
    if ((by[kStutter] + by[kShaders]) > 0 && stutters > 10)
    {
        any = true;
        if (shader_stutter_secs * 2 >= by[kStutter] + by[kShaders] && g_ctx.shader_mode >= 0 && g_ctx.shader_mode != 2)
            std::fprintf(f, "  - Most stutters came with shaders being made: Shader Compilation on Async Ubershaders "
                            "hides them.\n");
        if (!g_ctx.wait_shaders)
            std::fprintf(f, "  - Stutters: Build Shaders Before Starting is off; on, a game's known shaders are built "
                            "first.\n");
        else
            std::fprintf(f, "  - Stutters at full speed: new shaders the first time an effect is seen; a second play "
                            "has fewer.\n");
    }
    if (!any)
        std::fprintf(f, "  Nothing stands out.\n");
    /* 4.6: the same as Porpoise's Picks (data/recommended.ini), to test and send in. */
    {
        std::vector<std::string> sure, maybe;
        std::string why;
        auto add_why = [&](const char *w) { why += (why.empty() ? "" : "; ") + std::string(w); };
        if (slow > 0 && (by[kGpu] + by[kVideo]) * 3 >= slow && g_ctx.resolution > 2)
        {
            sure.push_back("resolution = " + std::to_string(g_ctx.resolution - 1));
            add_why("the GPU was the limit at a higher resolution");
        }
        if ((by[kStutter] + by[kShaders]) > 0 && stutters > 10 && shader_stutter_secs * 2 >= by[kStutter] + by[kShaders] &&
            g_ctx.shader_mode >= 0 && g_ctx.shader_mode != 2)
        {
            sure.push_back("shader_mode = 2");
            add_why("shader stutters");
        }
        if (slow > 0 && by[kAudio] * 3 >= slow && g_ctx.audio_buffer >= 0 && g_ctx.audio_buffer < 2)
        {
            sure.push_back("audio_buffer = 2");
            add_why("the sound ran short");
        }
        if (slow > 0 && by[kCpu] * 3 >= slow)
        {
            if (one_core)
            {
                maybe.push_back("dolphin.Core.CPUThread = True");
                maybe.push_back("dolphin.Core.SyncGPU = True");
            }
            if (!g_ctx.fast_float)
                maybe.push_back("fast_float = 1   (Riptide Boost: never applied by itself)");
        }
        std::fprintf(f, "\nPorpoise's Picks from this play (for data/recommended.ini):\n");
        if (sure.empty() && maybe.empty())
            std::fprintf(f, "  None: nothing here needs changing.\n");
        else
        {
            std::fprintf(f, "[%s]\n", g_game_id.empty() ? "<disc ID>" : g_game_id.c_str());
            std::fprintf(f, "note = %s\n", why.empty() ? "Tested on PS5" : why.c_str());
            for (const std::string &l : sure)
                std::fprintf(f, "%s\n", l.c_str());
            for (const std::string &l : maybe)
                std::fprintf(f, "# to test first: %s\n", l.c_str());
        }
    }
    static const char *const outs[4] = {"the PS5's own", "1080p", "1440p", "4K"};
    std::fprintf(f, "\nSet to: internal resolution %dx, output %s, dual core %s, own cores %s, Riptide Boost %s, "
                    "shaders first %s, frame generation %s, V-Sync %s.\n"
                    "Second by second: performance.csv.\n",
                 g_ctx.resolution, outs[std::clamp(g_ctx.output_res, 0, 3)],
                 one_core ? "off for this game (Dolphin's choice)" : g_ctx.dual_core ? "on" : "off", g_ctx.own_cores ? "on" : "off",
                 g_ctx.fast_float ? "on" : "off", g_ctx.wait_shaders ? "on" : "off", g_ctx.frame_gen ? "on" : "off",
                 g_ctx.vsync ? "on" : "off");
    std::fclose(f);
}
} // namespace

void perf_context(const PerfContext &c)
{
    std::lock_guard<std::mutex> lock(g_lock);
    g_ctx = c;
}

void perf_second(const PerfSecond &s)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on || g_game_dir.empty())
        return;
    if (g_seconds.size() < 6 * 3600)
        g_seconds.push_back(s);
    if (!g_perf)
    {
        g_perf = std::fopen((g_game_dir + "/performance.csv").c_str(), "w");
        if (g_perf)
            std::fputs("# A line a second. speed: the emulation's, percent. cpu: Dolphin's emulated CPU thread a frame "
                       "(ms, average and longest); video: its video thread, busy a frame; run: the core's frame as "
                       "Porpoise waited for it; present and pace: showing the frame and waiting for the next; stutters: "
                       "frames over 1.5 of the game's frame time; limit: what held that second back (blank: full "
                       "speed).\n"
                       "t,fps,game_hz,speed,worst_ms,stutters,cpu_ms,cpu_max_ms,video_ms,run_ms,present_ms,pace_ms,"
                       "misses,audio_wait_ms,fast,limit,gpu_wait_ms,shader_ms,shaders,one_core\n",
                       g_perf);
    }
    if (g_perf)
    {
        std::fprintf(g_perf, "%.0f,%.1f,%.2f,%.1f,%.1f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.1f,%d,%s,%.2f,%.1f,%d,%d\n", s.t,
                     s.fps, s.game_hz, s.speed, s.worst_ms, s.stutters, s.cpu_ms, s.cpu_max_ms, s.gpu_ms, s.run_ms,
                     s.present_ms, s.pace_ms, s.misses, s.audio_wait_ms, s.fast ? 1 : 0, limit_of(s), s.gpu_wait_ms,
                     s.shader_ms, s.shaders, s.single_core ? 1 : 0);
        std::fflush(g_perf);
    }
}

void start(const std::string &data_dir, const std::string &trace_path, bool on)
{
    std::lock_guard<std::mutex> lock(g_lock);
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_root = data_dir + "/debug";
    g_trace = trace_path;
    g_on = on;
    if (!on)
        return;
    make(g_root);
    for (const char *sub : {"/app", "/crashes", "/controllers", "/games"})
        make(g_root + sub);
    readme();
    /* The last run: where trace.txt stood when it started, and its app log. A
     * crash written past that point is filed with the log's last lines. */
    long from = -1;
    char last_app[256] = "";
    if (std::FILE *f = std::fopen((g_root + "/.last-run").c_str(), "r"))
    {
        if (std::fscanf(f, "%ld %255s", &from, last_app) < 1)
            from = -1;
        std::fclose(f);
    }
    const long size = file_size(g_trace);
    if (from >= 0 && size > from)
    {
        std::string text;
        if (std::FILE *t = std::fopen(g_trace.c_str(), "rb"))
        {
            std::fseek(t, from, SEEK_SET);
            text.resize(std::size_t(std::min(size - from, 4L << 20)));
            text.resize(std::fread(&text[0], 1, text.size(), t));
            std::fclose(t);
        }
        if (text.find("crash:") != std::string::npos || text.find("terminate") != std::string::npos)
        {
            const std::string path = g_root + "/crashes/crash-" + stamp() + ".txt";
            if (std::FILE *c = std::fopen(path.c_str(), "w"))
            {
                std::fprintf(c, "Porpoise closed unexpectedly last time. The trace from that run:\n\n");
                std::fwrite(text.data(), 1, text.size(), c);
                std::fprintf(c, "\n\nThe end of that run's app log (%s):\n\n", last_app);
                std::fclose(c);
                copy_tail(g_root + "/app/" + last_app, 0, path + ".app", 64 << 10);
                /* (the app log's tail appended) */
                if (std::FILE *a = std::fopen((path + ".app").c_str(), "rb"))
                {
                    if (std::FILE *c2 = std::fopen(path.c_str(), "ab"))
                    {
                        char buf[8192];
                        std::size_t n;
                        while ((n = std::fread(buf, 1, sizeof buf, a)) > 0)
                            std::fwrite(buf, 1, n, c2);
                        std::fclose(c2);
                    }
                    std::fclose(a);
                    std::remove((path + ".app").c_str());
                }
            }
            prune(g_root + "/crashes", "crash-", 20, false);
        }
        /* Every run's trace beside its app log: a freeze (closed from the PS
         * button) leaves no crash, but its last lines show where it stopped. */
        if (last_app[0])
        {
            std::string name = last_app;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0)
                name.resize(name.size() - 4);
            if (std::FILE *o = std::fopen((g_root + "/app/" + name + ".trace.txt").c_str(), "wb"))
            {
                const std::size_t keep = std::min<std::size_t>(text.size(), 1u << 20);
                std::fwrite(text.data() + (text.size() - keep), 1, keep, o);
                std::fclose(o);
            }
        }
    }
    const std::string name = "app-" + stamp() + ".log";
    g_app = std::fopen((g_root + "/app/" + name).c_str(), "w");
    prune(g_root + "/app", "app-", 40, false); /* (logs and their traces: 20 runs) */
    if (std::FILE *f = std::fopen((g_root + "/.last-run").c_str(), "w"))
    {
        std::fprintf(f, "%ld %s\n", size < 0 ? 0L : size, name.c_str());
        std::fclose(f);
    }
}

void set_on(bool on)
{
    std::string data;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (on == g_on)
            return;
        if (!on)
        {
            g_on = false;
            for (std::FILE **f : {&g_app, &g_hub})
                if (*f)
                    std::fclose(*f), *f = nullptr;
            return;
        }
        if (g_root.size() < 6)
            return; /* (not started yet: start() will) */
        data = g_root.substr(0, g_root.size() - 6);
    }
    start(data, g_trace, true);
}

bool on()
{
    return g_on;
}

void app(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on)
        return;
    va_list args;
    va_start(args, fmt);
    line(g_app, fmt, args);
    va_end(args);
    if (g_game)
    {
        va_start(args, fmt);
        line(g_game, fmt, args);
        va_end(args);
    }
}

std::string begin_game(const std::string &title, const std::string &id, const std::string &platform)
{
    std::lock_guard<std::mutex> lock(g_lock);
    for (std::FILE **f : {&g_game, &g_move, &g_camera})
        if (*f)
            std::fclose(*f), *f = nullptr;
    g_game_dir.clear();
    if (!g_on)
        return "";
    const std::string game = g_root + "/games/" + folder_name(title, id);
    make(g_root + "/games");
    make(game);
    prune(game, "20", 9, true); /* the last 10 plays, this one with them */
    g_game_dir = game + "/" + stamp();
    make(g_game_dir);
    g_seconds.clear();
    g_game_title = title;
    g_game_id = id;
    if (g_perf)
        std::fclose(g_perf), g_perf = nullptr;
    if (std::FILE *f = std::fopen((g_root + "/.last-game").c_str(), "w"))
    {
        std::fprintf(f, "%s\n", g_game_dir.c_str());
        std::fclose(f);
    }
    g_game = std::fopen((g_game_dir + "/game.log").c_str(), "w");
    g_trace_from = file_size(g_trace);
    g_move_bytes = g_camera_bytes = 0;
    if (g_game)
        std::fprintf(g_game, "%s (%s, %s), started %s\n\n", title.c_str(), id.c_str(), platform.c_str(),
                     stamp("%Y-%m-%d %H:%M:%S").c_str());
    if (g_app)
    {
        std::fprintf(g_app, "%s %8.3f  game: %s [%s] -> %s\n", stamp("%H:%M:%S").c_str(), seconds(), title.c_str(),
                     id.c_str(), g_game_dir.c_str());
        std::fflush(g_app);
    }
    return g_game_dir;
}

void game(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on || !g_game)
        return;
    va_list args;
    va_start(args, fmt);
    line(g_game, fmt, args);
    va_end(args);
}

void end_game(const std::string &core_log_path, const char *summary)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_game_dir.empty())
        return;
    if (g_game)
    {
        std::fprintf(g_game, "%s %8.3f  ended: %s\n", stamp("%H:%M:%S").c_str(), seconds(), summary ? summary : "");
        std::fclose(g_game);
        g_game = nullptr;
    }
    for (std::FILE **f : {&g_move, &g_camera, &g_perf})
        if (*f)
            std::fclose(*f), *f = nullptr;
    write_summary(g_game_dir + "/performance.txt");
    g_seconds.clear();
    if (!core_log_path.empty())
        copy_tail(core_log_path, 0, g_game_dir + "/core.log", 8L << 20);
    copy_tail(g_trace, g_trace_from, g_game_dir + "/trace.txt", 4L << 20);
    g_game_dir.clear();
}

void move_sample(std::uint64_t t_us, const float accel[3], const float gyro[3], const float out[3], std::uint32_t buttons)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on)
        return;
    std::FILE *f = nullptr;
    long long *bytes = nullptr;
    if (!g_game_dir.empty())
    {
        if (!g_move)
        {
            g_move = std::fopen((g_game_dir + "/move.csv").c_str(), "w");
            if (g_move)
                std::fputs("# The PlayStation Move's readings, every one. accel in g and gyro in rad/s in the "
                           "DualSense's axes held flat (x right, y up out of the face, z toward the player); out = the "
                           "acceleration the game was given after Swing strength and the swing curve.\n"
                           "t_us,ax,ay,az,gx,gy,gz,out_ax,out_ay,out_az,buttons\n",
                           g_move);
        }
        f = g_move;
        bytes = &g_move_bytes;
    }
    else if (g_hub)
    {
        f = g_hub;
        bytes = &g_hub_bytes;
    }
    if (!f || *bytes > kMaxFile)
        return;
    const int n = std::fprintf(f, "%s%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%08x\n", f == g_hub ? "move," : "",
                               static_cast<unsigned long long>(t_us), accel[0], accel[1], accel[2], gyro[0], gyro[1],
                               gyro[2], out[0], out[1], out[2], buttons);
    if (n > 0)
        *bytes += n;
}

void camera(bool found, float x, float y, float radius, int pixels, unsigned frame, double shot_us)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on || g_game_dir.empty() || g_camera_bytes > kMaxFile)
        return;
    if (!g_camera)
    {
        g_camera = std::fopen((g_game_dir + "/camera.csv").c_str(), "w");
        if (g_camera)
            std::fputs("# Where the PS3 Eye saw the Move's ball (its 640 x 480 picture), each frame it sent.\n"
                       "t,found,x,y,radius,pixels,frame,shot_us\n",
                       g_camera);
    }
    if (!g_camera)
        return;
    const int n = std::fprintf(g_camera, "%.3f,%d,%.1f,%.1f,%.1f,%d,%u,%.0f\n", seconds(), found ? 1 : 0, x, y, radius,
                               pixels, frame, shot_us);
    if (n > 0)
        g_camera_bytes += n;
}

void hub(bool open)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_hub)
    {
        std::fclose(g_hub);
        g_hub = nullptr;
    }
    if (!open || !g_on)
        return;
    make(g_root + "/controllers");
    g_hub = std::fopen((g_root + "/controllers/hub-" + stamp() + ".csv").c_str(), "w");
    g_hub_bytes = 0;
    prune(g_root + "/controllers", "hub-", 20, false);
}

void hub_line(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on || !g_hub || g_hub_bytes > kMaxFile)
        return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vfprintf(g_hub, fmt, args);
    va_end(args);
    std::fputc('\n', g_hub);
    if (n > 0)
        g_hub_bytes += n + 1;
}

bool hub_open()
{
    return g_hub != nullptr;
}

namespace
{
std::vector<std::string> newest(const std::string &dir, const char *prefix, std::size_t count)
{
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str()))
    {
        while (const dirent *e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n != "." && n != ".." && n.compare(0, std::strlen(prefix), prefix) == 0)
                names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    if (names.size() > count)
        names.erase(names.begin(), names.end() - long(count));
    return names;
}
} // namespace

std::vector<std::string> gather(const std::string &to)
{
    std::vector<std::string> out;
    std::string game;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_root.empty())
            return out;
        if (g_app)
            std::fflush(g_app);
        if (std::FILE *f = std::fopen((g_root + "/.last-game").c_str(), "r"))
        {
            char b[1024] = "";
            if (std::fgets(b, sizeof b, f))
                game = b;
            std::fclose(f);
            while (!game.empty() && (game.back() == '\n' || game.back() == '\r'))
                game.pop_back();
        }
    }
    make(to + "/debug");
    auto take = [&](const std::string &from, const std::string &name, long max) {
        if (file_size(from) < 0)
            return;
        copy_tail(from, 0, to + "/debug/" + name, max);
        out.push_back("debug/" + name);
    };
    for (const std::string &n : newest(g_root + "/app", "app-", 2))
        take(g_root + "/app/" + n, n, 4L << 20);
    for (const std::string &n : newest(g_root + "/crashes", "crash-", 5))
        take(g_root + "/crashes/" + n, n, 4L << 20);
    for (const std::string &n : newest(g_root + "/controllers", "hub-", 1))
        take(g_root + "/controllers/" + n, n, 16L << 20);
    if (!game.empty())
        for (const char *f : {"game.log", "core.log", "trace.txt", "move.csv", "camera.csv", "WiimoteNew.ini"})
            take(game + "/" + f, std::string("last-game-") + f, 24L << 20);
    if (!game.empty())
        for (const std::string &n : newest(game, "motion-", 1))
            take(game + "/" + n, "last-game-" + n, 24L << 20);
    return out;
}
} // namespace porpoise::debuglog
