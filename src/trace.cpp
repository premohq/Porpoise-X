/*
 * PS5 RetroArch - the startup trace. See src/trace.hpp for why it exists.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "trace.hpp"
#include "porpoise_paths.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5::debug
{
namespace
{
/* Each start's own trace, the last one's kept beside it: Playtendoh starts Porpoise for each game, so
 * trace.txt is that game's, and trace.prev.txt the one before (a crash in it is what a report needs). */
constexpr const char *kTrace = PORPOISE_LOGS "/trace.txt";
constexpr const char *kTracePrev = PORPOISE_LOGS "/trace.prev.txt";
/* Until /data can be reached (a start inside the sandbox, before the jailbreak frees Porpoise): the
 * sandbox's own writable folder, as Playtendoh's Switch side keeps its boot trace. */
constexpr const char *kSandboxTrace = "/download0/porpoise-trace.txt";
constexpr const char *kSandboxTracePrev = "/download0/porpoise-trace.prev.txt";
/* The first lines go to the disk itself, not only the console's cache: a
 * console that freezes as Porpoise starts has to be unplugged, and a file
 * still in the cache is lost with the power (2.1.1's first test left no
 * trace at all). Past them, the cache is enough. */
constexpr int kSyncedLines = 120;
std::atomic<int> g_lines{0};
std::mutex g_lock;
bool g_on_data = false, g_sandbox_turned = false;
/* (a plain array: the crash handler reads it) */
char g_path[64] = "";

/* Where this line goes: the logs folder once it can be made, the last start's trace turned into
 * trace.prev.txt the first time; the sandbox's until then. */
void choose_path()
{
    if (g_on_data)
        return;
#ifndef PORPOISE_DESKTOP
    mkdir(PORPOISE_DATA, 0777);
    mkdir(PORPOISE_LOGS, 0777);
#endif
    struct stat st{};
    if (stat(PORPOISE_LOGS, &st) == 0 && S_ISDIR(st.st_mode))
    {
        std::rename(kTrace, kTracePrev);
        std::snprintf(g_path, sizeof g_path, "%s", kTrace);
        g_on_data = true;
        return;
    }
    if (!g_sandbox_turned)
    {
        std::rename(kSandboxTrace, kSandboxTracePrev);
        g_sandbox_turned = true;
    }
    std::snprintf(g_path, sizeof g_path, "%s", kSandboxTrace);
}

void write(const char *line) noexcept
{
    std::lock_guard<std::mutex> lock(g_lock);
    choose_path();
    std::FILE *file = std::fopen(g_path, "a");
    if (file == nullptr)
        return;
    std::fputs(line, file);
    std::fputc('\n', file);
    std::fflush(file);
    if (g_lines.fetch_add(1, std::memory_order_relaxed) < kSyncedLines)
        fsync(fileno(file));
    std::fclose(file);
}
} // namespace

const char *trace_file() noexcept
{
    if (!g_path[0])
    {
        std::lock_guard<std::mutex> lock(g_lock);
        choose_path();
    }
    return g_path;
}

void mark(const char *step) noexcept
{
    write(step);
}

void mark_value(const char *step, long long value) noexcept
{
    char line[192];
    std::snprintf(line, sizeof(line), "%s = %lld", step, value);
    write(line);
}

void mark_init(const char *step, bool have_video, int width, int height) noexcept
{
    char line[192];
    std::snprintf(line, sizeof(line), "%s (video=%s width=%d height=%d)", step,
                  have_video ? "present" : "NULL", width, height);
    write(line);
}
} // namespace ps5::debug

/* The C door src/input_ps5.cpp writes through; see the note in trace.hpp. */
extern "C" void ps5_input_trace(const char *line) noexcept
{
    ps5::debug::mark(line);
}
