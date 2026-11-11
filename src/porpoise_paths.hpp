/* Porpoise - where the app and the player's things live.
 *
 * On the PS5 the app is /app0 and the player's things /data/porpoise. On a
 * desktop (PORPOISE_DESKTOP: Windows), Porpoise runs from its own folder
 * (the working directory is set to it at start): the app's files beside the
 * program, the player's in its data folder.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#ifdef PORPOISE_DESKTOP
#define PORPOISE_APP "."
#define PORPOISE_DATA "./data"
#define PORPOISE_LOGS PORPOISE_APP
#else
#define PORPOISE_APP "/app0/retro/porpoise"
#define PORPOISE_DATA "/data/porpoise"
/* Porpoise's own logs (trace.txt, Dolphin's core.log), each start's own with the last one's beside it
 * (.prev): Playtendoh's app folder can't be written, so they are in the data folder, where Playtendoh's
 * reports and Copy Logs to USB look. Where /data can't be reached, the trace goes to the sandbox
 * (kSandboxTrace in trace.cpp). */
#define PORPOISE_LOGS PORPOISE_DATA "/logs"
#endif
