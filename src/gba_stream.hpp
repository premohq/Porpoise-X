/* Porpoise - the GBA's screen and buttons in a browser (a GameCube port set to GBA (Integrated)).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * http://<PS5>:6771/ shows the GBA's picture and takes its buttons back, as PS5-ECAM's remote
 * touch screen does (its port/stream: a WebSocket each for pictures, /video, and input, /input).
 * Unlike it, the pictures are lossless (the GBA's 15-bit colour, packed: gba_stream.cpp), and
 * pushed as they come, up to eight ahead of the
 * browser's "ack"s, so a slow round trip doesn't cap the rate: up to 60 a second, or what the
 * page asks for ("fps 30"). The frames come from the core (dolphin_gba_get_frame) on the game's
 * thread, only while a browser shows them, and packed on the server's own thread. */
#pragma once

#include <cstdint>

namespace porpoise::gba_stream
{
/* Listen on 6771 (again: nothing) / stop and drop every browser. */
void start();
void stop();

/* The page's comings and goings and its five-second counts in trace.txt too (Settings > Debug
 * Logs); what matters is there anyway. */
void set_verbose(bool on);

/* Which GameCube ports have a GBA (bit n = port n + 1): the page offers those. */
void set_ports(unsigned ports);

/* The game's thread, for the GBA in port (0..3): whether a browser wants its pictures now, and
 * its newest (15-bit: red bits 0-4, green 5-9, blue 10-14), copied at once. */
bool want_frame(int port);
void submit(int port, const std::uint16_t *pixels, unsigned width, unsigned height);

/* The buttons held in the browser on that port's page, as mGBA's key mask (A, B, Select, Start,
 * Right, Left, Up, Down, R, L = bits 0..9); let go when the page goes quiet for two seconds or
 * closes. */
std::uint16_t keys(int port);
/* That port's page's Reset GBA was pressed since the last time this was asked. */
bool take_reset(int port);
/* That port's page's Disconnect GBA was pressed (toggles the link cable) since the last asking. */
bool take_disconnect(int port);
} // namespace porpoise::gba_stream
