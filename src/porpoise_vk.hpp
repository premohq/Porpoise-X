/* Porpoise - the screen: Vulkan on RADV, presented through VK_KHR_display.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cstdint>
#include <mutex>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "libretro.h"
#include "libretro_vulkan.h"

namespace porpoise::vk
{
struct Rect
{
    float x0, y0, x1, y1; /* pixels on the screen */
};

/* Instance, display surface and physical device. Call once at startup. */
bool open_display();
/* The device: through the core's negotiation interface when it has one
 * (Dolphin creates its own device with the features it needs), otherwise ours.
 * Then the swapchain and the presentation pipeline. */
bool open_device(const retro_hw_render_context_negotiation_interface_vulkan *negotiation);
/* The libretro hardware render interface handed to the core. */
retro_hw_render_interface_vulkan *render_interface();

unsigned screen_width();
unsigned screen_height();
double refresh_hz();
/* The display mode's own rate (may be above 60). */
double display_hz();
/* How long the last present waited for the display to free an image. */
double last_present_wait_ms();
/* The TV's refresh, waited for directly (VideoOut's vblank), for V-Sync.
 * vblank_ready(): the display's VideoOut output has been found (it opens
 * with the first swapchain). wait_vblank(): blocks until the next vblank;
 * false when it could not. */
bool vblank_ready();
/* The display's VideoOut handle, from the driver's sceVideoOutOpen (V-Sync). */
void note_videoout(int handle);
bool wait_vblank();

/* Settings > Video > Refresh Rate: VRR. Porpoise itself puts the output at
 * 119.88 Hz when the driver's VideoOut opens (the driver, told nothing of
 * 120 Hz, would take VRR's long refreshes for 60 Hz and put it back), and puts
 * it back as Porpoise closes. Call before the display opens. */
void set_own_high_rate(bool on);
bool own_high_rate();   /* the output was put at 119.88 Hz by Porpoise */
void restore_output();  /* 59.94 Hz again (Porpoise closing) */

/* Under VRR the TV has no steady refresh to wait on: frame generation then
 * gets one of Porpoise's own, ticks of a clock at hz (0: the TV's vblank again). */
void set_virtual_vblank(double hz);
double virtual_vblank_hz();
/* Counts each change of it: the game's pacing starts over on one (the
 * vblank it waits on changed). */
unsigned virtual_vblank_changes();

/* The core's frame, aspect-fit onto the screen. aspect <= 0 uses w / h. */
/* filter: 0 smooth, 1 sharp, 2 sharpen, 3 CRT, 4 arcade CRT, 5 VHS; strength 0..1. */
void present_core_frame(unsigned width, unsigned height, float aspect, int filter, float strength);
/* Accessibility's colour filter on the game's picture (0 off; shaders/quad.frag). */
void set_game_colour_filter(int mode);
/* Enhancements: saturation and contrast (1 as it is), warmth (0 as it is),
 * bloom 0..1, on the game's picture. */
void set_picture_adjust(float saturation, float contrast, float warmth, float bloom);
/* The game's picture cropped: left, top, right, bottom, each a share of it (0..0.1). */
void set_crop(const float crop[4]);
/* A copy of the game's last picture (width x height, RGBA), for thumbnails. */
bool capture_picture(unsigned width, unsigned height, std::vector<std::uint8_t> &rgba);
/* Where the last game picture was drawn, as x, y, w, h in 1920x1080 design space. */
void picture_rect(float out[4]);
/* A frame with no core image (startup, messages). */
void present_clear(float r, float g, float b);
/* 4.0: the core used the frame slots in the last quarter second (it presents
 * from its own thread while it builds shaders at boot): the launch screen
 * then leaves them to it. */
bool core_presenting();

/* What the UI renderer needs to live on the current device. */
struct Context
{
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    std::uint32_t queue_family;
    VkRenderPass render_pass;
    unsigned slots;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    PFN_vkGetDeviceProcAddr get_device_proc;
    std::mutex *queue_mutex;
};
Context context();
/* Drawn inside every presented frame's render pass, after the core's image. */
void set_overlay(void (*draw)(VkCommandBuffer cmd, unsigned slot, void *user), void *user);
/* Called in each frame's command buffer before its render pass (copies into
 * textures that change every frame). */
void set_prepass(void (*record)(VkCommandBuffer cmd, void *user), void *user);
unsigned current_slot();
/* The display's flips so far and those queued (VideoOut's flip status), for
 * the frame statistics; false where it can't be asked. */
bool flip_status(std::uint64_t &count, int &pending);

/* 3.1: frame generation (beta), ps5-framegen's interpolator on the current
 * device. porpoise::framegen decides when; these draw and present. */
namespace fg
{
/* Makes the interpolator for the screen's size (once per device): false and
 * why when it can't be. */
bool start(std::string &error);
void stop();
bool ready();
void set_debug_view(bool on);
enum class Shown
{
    Nothing,   /* nothing presented (no picture yet, or not started) */
    Real,      /* the game's own frame was presented (no in-between frame) */
    Generated, /* the first in-between frame was presented */
};
/* A new frame of the game: drawn into the interpolator as it is shown, then
 * the optical flow and (unless reset) the first of `generated` frames in
 * between. One present: that in-between frame when it was made and
 * show_generated, otherwise the game's frame. `late`: the game's picture is
 * from a run before this one (the core then waits for this present too). */
Shown frame(unsigned width, unsigned height, float aspect, int filter, float strength, bool reset, unsigned generated,
            bool show_generated, bool late = false);
/* The index-th further in-between frame (1, 2), presented. */
bool present_generated(unsigned index);
/* The game's frame given to frame(), presented. */
void present_real();
/* After the game's frame was presented: it becomes the previous one. */
void advance();
/* The GPU time of the interpolator's passes since asked last. */
bool gpu_time(double &mean_ms, double &max_ms, double &flow_ms, unsigned &count);
std::uint64_t generated_total();
} // namespace fg

/* Tear down the device (and its swapchain) so another can take the display. */
void close_device();

void close();
} // namespace porpoise::vk
