/* Porpoise - frame pacing: one frame per vblank when the display allows it.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A frame looks smooth when every frame stays on screen for the same number
 * of vblanks. Timing frames with a clock of our own cannot promise that: the
 * clock and the display drift apart, a frame now and then lands a vblank late
 * and the next one early, and movement judders for a moment every few
 * seconds - with the game itself at full speed.
 *
 * The console's swapchain flips at the vblank and an acquire waits for a free
 * image, so when the content rate matches the display (a 59.94 or 60 Hz game
 * on a 60 Hz mode) the display itself can be the clock: present, and the next
 * acquire holds the loop until the vblank. The pacer checks that the
 * swapchain really does hold it (the present waits) for its first couple of
 * seconds; if it doesn't, or the rates don't match (a 50 Hz game, a 120 Hz
 * mode), it falls back to a clock of its own, the way Porpoise always paced.
 * A floor of 96% of a vblank keeps a loop that is not held from racing.
 *
 * Locked to the display, a 59.94 Hz game runs 0.1% fast; the audio
 * resampler's rate control absorbs that without anyone hearing it.
 *
 * 2.0: on the console the swapchain never held the loop (the probe found 0
 * of 120 presents held), so every game and the menus ran on the own clock,
 * with a frame now and then landing a vblank late or early. V-Sync now waits
 * on the display's vblank itself (VideoOut's): a frame finished before its
 * vblank waits for it; one finished after doesn't wait at all, so a game
 * that can't quite hold 60 runs as fast as it can instead of dropping to 30.
 *
 * 2.1.2: the vblanks are timed, not taken from the display mode. A console
 * set to 120 Hz output reports a 60 Hz mode, and its vblanks came twice per
 * frame: a light game (Rocky, Spider-Man 2) finished early, took the next
 * vblank, and ran at 72-75 frames a second, faster than itself. Now a frame
 * waits as many vblanks as make up its own time (two at 120 Hz); a display
 * whose vblanks don't divide the game's rate gets Porpoise's own clock. And
 * no frame, whatever the vblanks say, is shorter than 96% of the game's own. */
#pragma once

namespace porpoise::pacer
{
/* What the pacer measured of the TV, once V-Sync first ran: its refresh rate
 * (59.94 or 119.88; 0 not yet known or under VRR) and whether it is VRR. */
double display_rate_hz();
bool display_vrr();
/* Times the TV's vblanks now if they weren't yet (V-Sync on, the display open),
 * then as display_rate_hz. */
double measure_display();

class Pacer
{
public:
    /* content_hz: how often a new frame is due (a game's rate, capped at 60). */
    void start(double content_hz, const char *who);
    /* After every present: waits as long as this frame needs to. */
    void frame_done();
    /* After a pause or a stall: start counting again from now. */
    void resync();
    bool locked() const { return mode_ == Mode::Locked || mode_ == Mode::Probing || mode_ == Mode::Vblank; }
    bool display_locked() const { return mode_ == Mode::Locked || mode_ == Mode::Vblank; }
    bool vsynced() const { return mode_ == Mode::Vblank; }
    /* Frames that finished after the vblank they were meant for, since the
     * last call (V-Sync only). */
    int take_misses()
    {
        const int n = misses_;
        misses_ = 0;
        return n;
    }
    /* Every miss since start(), for the frame statistics (take_misses is the log's). */
    long long misses_total() const { return misses_total_; }
    /* V-Sync: the display's vblanks per frame of the content (2 at 120 Hz) and
     * their measured rate; 0 when not on the vblank. */
    int vblanks_per_frame() const { return mode_ == Mode::Vblank ? vblanks_per_frame_ : 0; }
    double vblank_hz() const { return mode_ == Mode::Vblank && vblank_ns_ > 0 ? 1e9 / double(vblank_ns_) : 0.0; }

private:
    enum class Mode
    {
        Probing,
        Locked,
        Clock,
        Vblank, /* V-Sync: waits on the TV's own vblank (porpoise::vk::wait_vblank) */
    };
    void enter_vblank();
    void vblank_frame();
    long long last_vblank_ns_ = 0;
    int vblanks_per_frame_ = 1; /* 2 on a 120 Hz output */
    long long frame_floor_ns_ = 0; /* 96% of the content's frame */
    /* The game's own clock, a hair fast (0.2%, within what the sound's rate
     * correction absorbs): held to the display, a frame that would put the
     * game further ahead than half a frame waits a vblank more, so a display
     * a little faster than the game (or one whose vblanks wander) never runs
     * it fast on average - the sound skipped every few seconds when it did. */
    long long cap_ns_ = 0;
    void hold_to_game();
    int quick_vblanks_ = 0;
    int misses_ = 0;
    long long misses_total_ = 0;
    int vblank_retry_ = 0;
    /* Times V-Sync was tried and gave up (a wait that failed or didn't wait,
     * vblanks that don't divide the frame): after a few, the own clock stays
     * until the next start, rather than going back every second and running
     * the game fast while it finds out again. */
    int vblank_failures_ = 0;
    void vblank_failed(long long now, const char *why);
    Mode mode_ = Mode::Clock;
    long long period_ns_ = 16666667;  /* content */
    long long vblank_ns_ = 16666667;  /* display */
    long long last_ns_ = 0, deadline_ns_ = 0;
    int probe_frames_ = 0, probe_held_ = 0;
    long long window_start_ns_ = 0; /* locked: frames over the last window, to catch a loop the display stopped holding */
    int window_frames_ = 0;
    bool compatible_ = false;       /* the display runs at the content's rate */
    long long clock_since_ns_ = 0;  /* when the own clock took over */
    const char *who_ = "";
};

/* V-Sync on (the default) or off (Porpoise's own timer), from the Video
 * settings; takes effect at the next start(). */
void set_vsync(bool on);
long long now_ns();
/* Sleeps most of the way, then spins the last stretch: a sleep can overshoot
 * by up to a millisecond. */
void sleep_until_ns(long long deadline);
} // namespace porpoise::pacer
