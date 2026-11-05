/* Porpoise UI preview: renders the launcher screens offscreen (any Vulkan
 * driver, e.g. lavapipe) and writes PNGs, to check the UI without a console.
 *
 *   preview <assets dir> <covers dir> <out dir>
 */
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <unistd.h>

#include <sys/stat.h>

#include "porpoise_borders.hpp"
#include "porpoise_states.hpp"
#include "porpoise_banner.hpp"
#include "ui_recommend.hpp"
#include "ui_setups.hpp"
#include "ui_app.hpp"
#include "ui_widescreen.hpp"
#include "ui_i18n.hpp"
#include "ui_gfx.hpp"
#include "ui_library.hpp"
#include "ui_settings.hpp"
#include "porpoise_netfs.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *);

using namespace porpoise::ui;

#define GI(name) auto name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))
#define GD(name) auto name = reinterpret_cast<PFN_##name>(gdpa(device, #name))

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: preview <assets> <covers> <out>\n");
        return 2;
    }
    const std::string assets = argv[1], covers = argv[2], out = argv[3];
    const std::string saves = argc > 4 ? argv[4] : out;
    const unsigned W = 1920, H = 1080;

    VkInstance instance = VK_NULL_HANDLE;
    auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
        return 1;
    GI(vkEnumeratePhysicalDevices);
    GI(vkCreateDevice);
    GI(vkGetDeviceProcAddr);
    GI(vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    std::uint32_t n = 1;
    vkEnumeratePhysicalDevices(instance, &n, &gpu);
    const float prio = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS)
        return 1;
    PFN_vkGetDeviceProcAddr gdpa = vkGetDeviceProcAddr;
    GD(vkGetDeviceQueue);
    GD(vkCreateImage);
    GD(vkGetImageMemoryRequirements);
    GD(vkAllocateMemory);
    GD(vkBindImageMemory);
    GD(vkCreateImageView);
    GD(vkCreateRenderPass);
    GD(vkCreateFramebuffer);
    GD(vkCreateCommandPool);
    GD(vkAllocateCommandBuffers);
    GD(vkBeginCommandBuffer);
    GD(vkEndCommandBuffer);
    GD(vkCmdBeginRenderPass);
    GD(vkCmdEndRenderPass);
    GD(vkCmdCopyImageToBuffer);
    GD(vkQueueSubmit);
    GD(vkQueueWaitIdle);
    GD(vkCreateBuffer);
    GD(vkGetBufferMemoryRequirements);
    GD(vkBindBufferMemory);
    GD(vkMapMemory);
    GD(vkResetCommandBuffer);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    auto memtype = [&](std::uint32_t bits, VkMemoryPropertyFlags f) {
        for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & f) == f)
                return i;
        return 0u;
    };

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {W, H, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;
    vkCreateImage(device, &ii, nullptr, &image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imem;
    vkAllocateMemory(device, &ai, nullptr, &imem);
    vkBindImageMemory(device, image, imem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    vkCreateImageView(device, &vi, nullptr, &view);

    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    VkRenderPass rp;
    vkCreateRenderPass(device, &rpi, nullptr, &rp);
    VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass = rp;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = W;
    fbi.height = H;
    fbi.layers = 1;
    VkFramebuffer fb;
    vkCreateFramebuffer(device, &fbi, nullptr, &fb);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = VkDeviceSize(W) * H * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback;
    vkCreateBuffer(device, &bi, nullptr, &readback);
    vkGetBufferMemoryRequirements(device, readback, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory rmem;
    vkAllocateMemory(device, &ai, nullptr, &rmem);
    vkBindBufferMemory(device, readback, rmem, 0);
    void *mapped = nullptr;
    vkMapMemory(device, rmem, 0, VK_WHOLE_SIZE, 0, &mapped);

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    vkCreateCommandPool(device, &cpi, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cai, &cmd);

    GfxInit gi;
    gi.instance = instance;
    gi.gpu = gpu;
    gi.device = device;
    gi.queue = queue;
    gi.queue_family = 0;
    gi.render_pass = rp;
    gi.slots = 1;
    gi.get_instance_proc = vkGetInstanceProcAddr;
    gi.get_device_proc = gdpa;
    gi.asset_dir = assets;
    gi.cjk_also = porpoise::ui::language_names();
    if (const char *cache = std::getenv("PREVIEW_CACHE"))
        gi.cache_dir = cache; /* the baked text atlases, as Porpoise keeps them */
    Gfx gfx;
    /* Covers decode on a worker thread in Porpoise; here, in place, so every
     * scene shows them (PREVIEW_ASYNC=1 to see the worker's way). */
    const bool async = std::getenv("PREVIEW_ASYNC") != nullptr;
    gfx.set_async_loads(async);
    if (!gfx.init(gi))
    {
        std::fprintf(stderr, "gfx init failed\n");
        return 1;
    }

    /* A sample library with invented games (covers generated by the preview script). */
    Library lib;
    LibraryPaths lp;
    lp.covers = covers;
    lp.state = out + "/library-state.txt";
    lib.scan(lp);
    const char *titles[][3] = {
        {"PRVW01", "Island Adventure", "GameCube"}, {"PRVW02", "Comet Racers", "GameCube"},
        {"PRVW03", "Haunted Manor", "GameCube"},    {"PRVW04", "Sunny Shores", "GameCube"},
        {"PRVW05", "Sky Sail", "GameCube"},         {"PRVW06", "Mech Hunter", "GameCube"},
        {"PRVW07", "Kart Party", "GameCube"},       {"PRVW08", "Garden Critters", "GameCube"},
        {"NOCOVR", "A Game Without Cover Art", "GameCube"},
        {"PRVW09", "Tiny Tennis", "Wii"},           {"PRVW10", "Bowling Night", "Wii"},
        {"PRVW11", "Puzzle Lab", "GameCube"},       {"PRVW12", "Star Pilots", "Wii"},
        {"PRVW13", "Rhythm Farm", "Wii"},
    };
    for (auto &t : titles)
    {
        Game g;
        g.id = t[0];
        g.title = t[1];
        g.platform = t[2];
        g.file = std::string(t[1]) + ".rvz";
        g.format = "RVZ";
        g.region = "USA";
        g.bytes = 1400ull << 20;
        lib.games().push_back(g);
    }
    lib.sort(lib.sort_order()); /* as a scan leaves it: Sort & filter's count too */
    /* Two Wii games are a disc with a real tile and banner (made by
     * tools/ui-preview from an open-source channel's layout). */
    for (Game &g : lib.games())
    {
        if (g.title == "Bowling Night" || g.title == "Star Pilots")
            g.path = covers + "/../discs/banner-test.rvz";
        if (g.title == "Tiny Tennis")
            g.path = covers + "/../discs/banner-test.iso";
    }
    std::system(("rm -rf '" + out + "/banners'").c_str());
    porpoise::banner::set_cache_dir(out + "/banners");
    lib.games()[3].last_played = (long long)std::time(nullptr) - 30 * 3600;
    for (Game &g : lib.games())
        if (g.id == "PRVW03")
        {
            g.developer = "Lantern Works";
            g.publisher = "Porpoise Preview Co.";
            g.released = "October 31, 2003";
            g.genre = "Adventure, Puzzle";
            g.players = 1;
            g.rating = "ESRB T";
            g.synopsis = "A storm strands you at the gates of an old manor where every room hides a puzzle and every "
                         "portrait seems to follow you. Gather the scattered keys, calm sixty-four restless ghosts and "
                         "find out who still lives on the top floor before the clock strikes midnight.";
        }
    lib.sort(Library::Sort::Title);

    porpoise::Settings settings;
    App ui;
    ui.init(&gfx, &lib, &settings, out + "/settings.ini", out + "/options.ini", saves);
    ui.set_sys_dir(std::string(getenv("HOME")) + "/porpoise-release/build/cores/stage/system/dolphin-emu/Sys");
    /* Two save states for the 4th game, with a cover for their pictures. */
    porpoise::states::set_data_dir(out);
    porpoise::borders::set_dirs(assets, out);
    porpoise::ui::setups::set_dir(out);
    porpoise::ui::setups::save(0, settings, "Kart Party");
    {
        /* A Dolphin fix and a Porpoise pick for the preview's games (PRV...). */
        mkdir((out + "/gs").c_str(), 0777);
        if (std::FILE *f = std::fopen((out + "/gs/PRV.ini").c_str(), "w"))
        {
            std::fputs("# PRVW01 - Preview\n[Video_Hacks]\n# Fixes the entrance videos.\nEFBToTextureEnable = False\n"
                       "[Video_Settings]\nSafeTextureCacheColorSamples = 0\n", f);
            std::fclose(f);
        }
        if (std::FILE *f = std::fopen((out + "/recommended.ini").c_str(), "w"))
        {
            std::fputs("[PRV]\nnote = Smoother on PS5 with these.\nresolution = 2\n"
                       "dolphin.Video_Hacks.EFBToTextureEnable = True\n"
                       "dolphin.Video_Settings.EnableGPUTextureDecoding = True\n", f);
            std::fclose(f);
        }
        porpoise::ui::recommend::set_paths(out + "/recommended.ini", out + "/gs");
    }
    if (lib.games().size() > 3)
    {
        const std::string dir = out + "/states/" + Library::key_of(lib.games()[3]);
        mkdir((out + "/states").c_str(), 0777);
        mkdir(dir.c_str(), 0777);
        for (int slot : {1, 3})
        {
            if (std::FILE *f = std::fopen((dir + "/slot" + std::to_string(slot) + ".state").c_str(), "wb"))
            {
                std::fputs("preview", f);
                std::fclose(f);
            }
            const std::string from = covers + (slot == 1 ? "/PRVW07.png" : "/PRVW03.png");
            if (std::FILE *in = std::fopen(from.c_str(), "rb"))
            {
                if (std::FILE *o = std::fopen((dir + "/slot" + std::to_string(slot) + ".png").c_str(), "wb"))
                {
                    char buf[8192];
                    std::size_t n;
                    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
                        std::fwrite(buf, 1, n, o);
                    std::fclose(o);
                }
                std::fclose(in);
            }
        }
    }

    auto render = [&](const char *name, auto &&draw) {
        gfx.begin(0, float(W), float(H), 12.0f, 0.0f, false);
        draw();
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &b);
        gfx.record_uploads(cmd); /* streamed textures (Wii banners) */
        VkClearValue clear{};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rp;
        rbi.framebuffer = fb;
        rbi.renderArea.extent = {W, H};
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        gfx.record(cmd);
        vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {W, H, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        auto *px = static_cast<unsigned char *>(mapped);
        for (std::size_t i = 0; i < std::size_t(W) * H; ++i)
            px[i * 4 + 3] = 255;
        const std::string path = out + "/" + name + ".png";
        stbi_write_png(path.c_str(), int(W), int(H), 4, px, int(W * 4));
        std::printf("wrote %s\n", path.c_str());
    };

    /* Library, with the 4th game selected (settle the slide first). */
    Input none;
    Input right;
    right.held = 1u << 3;
    for (int i = 0; i < 3; ++i)
    {
        ui.update(right, 0.016);
        ui.update(none, 0.016);
    }
    for (int i = 0; i < 200; ++i)
        ui.update(none, 0.016);
    render("library", [&] { ui.draw(12.0); });
    /* The sandbox help, as porpoise_main shows it: it must fit its dialog. */
    ui.show_message(porpoise::ui::tr("Porpoise can't reach /data"),
                    porpoise::ui::tr("The console started Porpoise inside the app sandbox, so it can't see "
                                     "/data or USB drives, and no jailbreak daemon freed it. A daemon that "
                                     "frees Porpoise must be running before you open it:\n"
                                     "\xE2\x80\xA2 etaHEN: turn on Legacy Command Server in etaHEN's Toolbox "
                                     "settings (etaHEN's built-in app list can't be edited to add Porpoise).\n"
                                     "\xE2\x80\xA2 OnionHEN: add PPSA99764 to exact_title_ids in "
                                     "/data/OnionHEN/config.ini.\n"
                                     "\xE2\x80\xA2 Or run a standalone daemon such as Lapy.\n"
                                     "Then open Porpoise again; if a launch still lands here, try once more. "
                                     "Until then, games go in /app0/porpoise/games."));
    for (int i = 0; i < 30; ++i)
        ui.update(none, 0.016);
    render("sandbox-help", [&] { ui.draw(12.0); });
    {
        Input in;
        in.held = 1u << 4; /* Cross closes it */
        ui.update(in, 0.016);
        ui.update(none, 0.016);
    }

    /* A button press: down for a frame, then up; then let animations settle. */
    auto press = [&](std::uint32_t bit, int times = 1) {
        for (int i = 0; i < times; ++i)
        {
            Input in;
            in.held = bit;
            ui.update(in, 0.016);
            ui.update(none, 0.016);
        }
    };
    auto settle = [&] {
        for (int i = 0; i < 90; ++i)
            ui.update(none, 0.016);
    };
    const std::uint32_t kDown = 1u << 1, kRight = 1u << 3, kCross = 1u << 4, kCircle = 1u << 5,
                        kSquare = 1u << 6, kTriangle = 1u << 7, kR1 = 1u << 9;

    /* PV_ISLAND=1: the island open, a shortcut picked, in each look. */
    if (getenv("PV_ISLAND"))
    {
        struct L
        {
            const char *name;
            int theme, mode;
        } looks[] = {{"glass-dark", int(porpoise::ui::ThemeId::Glass), 0},
                     {"glass-light", int(porpoise::ui::ThemeId::Glass), 2},
                     {"legacy", int(porpoise::ui::ThemeId::Porpoise), 0},
                     {"revolution", int(porpoise::ui::ThemeId::Revolution), 0},
                     {"starcube", int(porpoise::ui::ThemeId::StarCube), 0}};
        for (const L &l : looks)
        {
            settings.ui_theme = l.theme;
            settings.ui_mode = l.mode;
            ui.restart_fresh();
            settle();
            if (l.theme == int(porpoise::ui::ThemeId::StarCube))
            {
                ui.preview_starcube(0, true);
                settle();
            }
            ui.preview_glass(false, 0, true);
            ui.preview_island_pick(1);
            settle();
            ui.preview_island_pick(1);
            render(("island-" + std::string(l.name)).c_str(), [&] { ui.draw(12.0); });
        }
        return 0;
    }

    /* PV_MENUCTL=<theme>: the in-game menu's Controls tab in a Wii game (and
     * with the Move), and the hub over the game. */
    if (const char *mc = getenv("PV_MENUCTL"))
    {
        porpoise::ui::Game &g0 = lib.games()[0];
        g0.platform = "Wii";
        settings.ui_theme = atoi(mc);
        settings.wii_move = getenv("PV_MENUCTL_MOVE") != nullptr;
        settings.wii_controller = getenv("PV_MENUCTL_CTL") ? atoi(getenv("PV_MENUCTL_CTL")) : 0;
        ui.preview_look_changed();
        settle();
        static porpoise::Settings play;
        play = settings;
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const std::string &name) {
            render(name.c_str(), [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        ui.open_game_menu(&g0, &play);
        for (int i = 0; i < 30; ++i)
            ui.update_game_menu(none, 0.016);
        for (int i = 0; i < 4; ++i)
        {
            Input r1;
            r1.held = kR1;
            ui.update_game_menu(r1, 0.016);
            ui.update_game_menu(none, 0.016);
        }
        for (int i = 0; i < 30; ++i)
            ui.update_game_menu(none, 0.016);
        shot("menuctl-controls");
        ui.preview_ingame_hub(false);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        shot("menuctl-hub");
        return 0;
    }

    /* PV_MOVEPTR=1: the Move's pointer over the library, a game's page and the
     * in-game menu (Porpoise look and Legacy), T pulled on what it points at. */
    if (getenv("PV_MOVEPTR"))
    {
        porpoise::ui::Game &g0 = lib.games()[0];
        static porpoise::Settings play;
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        for (int theme : {int(porpoise::ui::ThemeId::Glass), int(porpoise::ui::ThemeId::Porpoise)})
        {
            settings.ui_theme = theme;
            settings.ui_mode = 0;
            ui.restart_fresh();
            settle();
            const std::string tn = theme == int(porpoise::ui::ThemeId::Glass) ? "glass" : "legacy";
            if (theme == int(porpoise::ui::ThemeId::Glass))
            {
                ui.preview_move_pointer(1240, 470, true);
                settle();
                render(("mp-" + tn + "-library").c_str(), [&] { ui.draw(12.0); });
                /* T on the case to the right: it comes to the front; again: its page. */
                Input t;
                t.held = 1u << 11; /* R2 */
                for (int pass = 0; pass < 2; ++pass)
                {
                    render("mp-scratch", [&] { ui.draw(12.0); }); /* (where things are: the pointer's) */
                    ui.update(t, 0.016);
                    ui.update(none, 0.016);
                    ui.preview_move_pointer(960, 470, true);
                    settle();
                }
                render(("mp-" + tn + "-details").c_str(), [&] { ui.draw(12.0); });
                ui.preview_move_pointer(-1, -1, false);
            }
            play = settings;
            ui.open_game_menu(&g0, &play);
            ui.preview_move_pointer(1500, 330, false);
            for (int i = 0; i < 30; ++i)
                ui.update_game_menu(none, 0.016);
            render("mp-scratch", [&] { ui.draw_game_menu(12.0); });
            ui.update_game_menu(none, 0.016);
            render(("mp-" + tn + "-menu").c_str(), [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
            ui.preview_move_pointer(-1, -1, false);
        }
        return 0;
    }

    /* PV_DIALOG=<theme>: a confirm dialog over Settings, and over the library. */
    if (const char *dl = getenv("PV_DIALOG"))
    {
        const std::uint32_t kR1b = 1u << 9;
        settings.ui_theme = atoi(dl);
        settings.ui_mode = getenv("PV_DIALOG_MODE") ? atoi(getenv("PV_DIALOG_MODE")) : 0;
        ui.restart_fresh();
        settle();
        ui.preview_dialog(true);
        settle();
        render("dialog-main", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kR1b, 2);
        settle();
        ui.preview_dialog(true);
        settle();
        render("dialog-settings", [&] { ui.draw(12.0); });
        return 0;
    }

    /* PV_BOOT=1: the boot (Sonar) in each look and over the welcome, a few moments. */
    if (getenv("PV_BOOT"))
    {
        struct Look
        {
            const char *name;
            int theme, mode;
            bool welcome;
        } looks[] = {{"glass-dark", int(porpoise::ui::ThemeId::Glass), 0, false},
                     {"glass-light", int(porpoise::ui::ThemeId::Glass), 2, false},
                     {"revolution", int(porpoise::ui::ThemeId::Revolution), 0, false},
                     {"starcube", int(porpoise::ui::ThemeId::StarCube), 0, false},
                     {"legacy", int(porpoise::ui::ThemeId::Porpoise), 0, false},
                     {"welcome", int(porpoise::ui::ThemeId::Glass), 0, true}};
        const char *only = getenv("PV_BOOT");
        const double times[] = {0.0, 0.5, 0.8, 1.3, 1.9, 2.8, 3.6, 3.75, 4.2, 4.8, 5.3, 5.62, 6.0};
        for (const Look &l : looks)
        {
            if (*only && strcmp(only, "1") && !strstr(l.name, only))
                continue;
            settings.ui_theme = l.theme;
            settings.ui_mode = l.mode;
            ui.restart_fresh();
            if (l.welcome)
                ui.start_welcome();
            settle();
            settle();
            ui.start_boot(l.theme == int(porpoise::ui::ThemeId::Porpoise));
            for (double t : times)
            {
                ui.preview_boot_time(t);
                ui.boot_frame(none, true);
                ui.update(none, 0.016);
                char name[64];
                std::snprintf(name, sizeof name, "boot-%s-%.2f", l.name, t);
                render(name, [&] {
                    ui.draw(12.0);
                    const std::size_t menus = gfx.quads();
                    ui.draw_boot();
                    std::fprintf(stderr, "%s: quads %zu (menus %zu)%s\n", name, gfx.quads(), menus,
                                 gfx.overflowed() ? " OVERFLOW" : "");
                });
            }
        }
        return 0;
    }

    /* PV_HOMES=1: each look's home screen, as it first appears (boot concepts). */
    if (getenv("PV_HOMES"))
    {
        struct Home
        {
            const char *name;
            int theme, mode;
        } homes[] = {{"glass-light", int(porpoise::ui::ThemeId::Glass), 2},
                     {"glass-dark", int(porpoise::ui::ThemeId::Glass), 0},
                     {"glass-oled", int(porpoise::ui::ThemeId::Glass), 1},
                     {"revolution", int(porpoise::ui::ThemeId::Revolution), 0},
                     {"starcube", int(porpoise::ui::ThemeId::StarCube), 0},
                     {"legacy", int(porpoise::ui::ThemeId::Porpoise), 0}};
        for (const Home &h : homes)
        {
            settings.ui_theme = h.theme;
            settings.ui_mode = h.mode;
            ui.restart_fresh();
            settle();
            settle();
            settle();
            render(("home-" + std::string(h.name)).c_str(), [&] { ui.draw(12.0); });
        }
        return 0;
    }

    /* PV_INGAME_HUB=<theme>: the Controller Hub over a game, from the in-game
     * menu, pushed around at random (a freeze hunt). */
    if (const char *ih = getenv("PV_INGAME_HUB"))
    {
        porpoise::ui::Game &g0 = lib.games()[0];
        g0.platform = "Wii";
        settings.ui_theme = atoi(ih);
        settings.wii_move = getenv("PV_INGAME_DS") == nullptr;
        ui.preview_look_changed();
        settle();
        static porpoise::Settings play;
        play = settings;
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const std::string &name) {
            render(name.c_str(), [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        unsigned seed = 12345;
        const std::uint32_t bits[] = {1u << 0, 1u << 1, 1u << 2, 1u << 3, kCross, kCircle, kSquare, kTriangle, 1u << 8, kR1};
        for (int round = 0; round < (getenv("PV_ROUNDS") ? atoi(getenv("PV_ROUNDS")) : 6); ++round)
        {
            ui.open_game_menu(&g0, &play);
            for (int i = 0; i < 30; ++i)
                ui.update_game_menu(none, 0.016);
            ui.preview_ingame_hub(round % 3 == 2);
            for (int i = 0; i < 20; ++i)
                ui.update_game_menu(none, 0.016);
            shot("ih-" + std::to_string(round) + "-open");
            for (int step = 0; step < 400; ++step)
            {
                seed = seed * 1103515245u + 12345u;
                Input in;
                in.held = bits[(seed >> 16) % 10];
                if (in.held == kCircle && ((seed >> 8) & 3))
                    in.held = 1u << 1; /* not out too often */
                ui.update_game_menu(in, 0.016);
                ui.update_game_menu(none, 0.016);
                if (step % 50 == 0)
                {
                    shot("ih-" + std::to_string(round) + "-" + std::to_string(step));
                    std::fprintf(stderr, "round %d step %d hub %d\n", round, step, ui.preview_ingame_hub_open() ? 1 : 0);
                }
                if (!ui.preview_ingame_hub_open())
                    ui.preview_ingame_hub(false);
            }
        }
        std::fprintf(stderr, "in-game hub: done\n");
        return 0;
    }

    /* PV_THEMES=1: 4.5 in the other themes: the island, the side menu, a
     * game's page closing, the in-game menu. */
    if (getenv("PV_THEMES"))
    {
        porpoise::ui::Game &g0 = lib.games()[0];
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        const int themes[3] = {int(porpoise::ui::ThemeId::Porpoise), int(porpoise::ui::ThemeId::Revolution),
                               int(porpoise::ui::ThemeId::StarCube)};
        const char *names[3] = {"legacy", "rev", "sc"};
        for (int t = 0; t < 3; ++t)
        {
            settings.ui_theme = themes[t];
            ui.restart_fresh();
            settle();
            const std::string n = names[t];
            if (t == 2)
            {
                ui.preview_starcube(0, true);
                settle();
                settle();
            }
            ui.preview_glass(false, 0, true);
            settle();
            render(("th-" + n + "-island").c_str(), [&] { ui.draw(12.0); });
            ui.preview_glass(false, 0, false);
            settle();
            press(kTriangle);
            settle();
            render(("th-" + n + "-menu").c_str(), [&] { ui.draw(12.0); });
            press(kCircle);
            settle();
            if (t != 1)
            {
                press(kSquare);
                settle();
                settle();
                press(kCircle);
                for (int i = 0; i < 9; ++i)
                    ui.update(none, 0.016);
                render(("th-" + n + "-back").c_str(), [&] { ui.draw(12.0); });
                settle();
            }
            static porpoise::Settings play;
            play = settings;
            ui.open_game_menu(&g0, &play);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
            render(("th-" + n + "-ingame").c_str(), [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
            settle();
        }
        return 0;
    }

    /* PV_SET=1: 4.5's settings: the side menu's Full Settings, the tabs and
     * their parts, About's credits, a game's settings, and 4.5's welcome. */
    if (getenv("PV_SET"))
    {
        const std::uint32_t kUp = 1u << 0;
        settings.ui_theme = int(porpoise::ui::ThemeId::Glass);
        settings.ui_mode = 2;
        settle();
        render("set-flow-light", [&] { ui.draw(12.0); });
        press(kR1);
        settle();
        render("set-flow-light-gc", [&] { ui.draw(12.0); });
        press(1u << 8); /* L1: back to All */
        settle();
        {
            ui.begin_launch(&lib.games()[2]);
            ui.preview_launch_start(10.0);
            render("set-launch-light", [&] { ui.draw_launch(12.4); });
            Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
            static porpoise::Settings play;
            play = settings;
            ui.open_game_menu(&lib.games()[0], &play);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
            render("set-ingame-light", [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
            ui.return_from_game();
            settle();
        }
        settings.ui_mode = 0;
        settle();
        render("set-flow-dark", [&] { ui.draw(12.0); });
        /* The island opening and closing: its rows fading in and out at its edge. */
        ui.preview_glass(false, 0, true);
        for (int f = 0; f < 3; ++f)
        {
            for (int i = 0; i < 4; ++i)
                ui.update(none, 0.016);
            render(("set-island-open-" + std::to_string(f)).c_str(), [&] { ui.draw(12.0); });
        }
        settle();
        ui.preview_glass(false, 0, false);
        for (int i = 0; i < 5; ++i)
            ui.update(none, 0.016);
        render("set-island-close", [&] { ui.draw(12.0); });
        settle();
        /* Details, then back into the row. */
        press(kSquare);
        settle();
        settle();
        render("set-details", [&] { ui.draw(12.0); });
        press(kCircle);
        for (int f = 0; f < 3; ++f)
        {
            for (int i = 0; i < 6; ++i)
                ui.update(none, 0.016);
            render(("set-back-" + std::to_string(f)).c_str(), [&] { ui.draw(12.0); });
        }
        settle();
        press(kTriangle);
        settle();
        press(kDown, 30);
        settle();
        render("set-menu-bottom", [&] { ui.draw(12.0); });
        press(kCross);
        settle();
        settle();
        render("set-rail", [&] { ui.draw(12.0); });
        press(kCross);
        settle();
        render("set-games", [&] { ui.draw(12.0); });
        press(kDown, 9);
        settle();
        render("set-games-down", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kDown);
        press(kCross);
        settle();
        render("set-video", [&] { ui.draw(12.0); });
        press(kDown, 14);
        settle();
        render("set-video-deep", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kDown, 7);
        press(kCross);
        settle();
        render("set-about", [&] { ui.draw(12.0); });
        press(kDown, 7);
        press(kCross);
        settle();
        render("set-credits", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kUp, 3);
        press(kCross);
        settle();
        render("set-controls", [&] { ui.draw(12.0); });
        ui.preview_game_settings(0);
        settle();
        render("set-game-rail", [&] { ui.draw(12.0); });
        press(kDown, 4);
        press(kCross);
        settle();
        render("set-game-video", [&] { ui.draw(12.0); });
        press(kDown, 10);
        settle();
        render("set-game-video-deep", [&] { ui.draw(12.0); });
        settings.ui_mode = 2;
        ui.start_welcome(true);
        settle();
        settle();
        render("set-welcome-light", [&] { ui.draw(12.0); });
        settings.ui_mode = 0;
        settle();
        render("set-welcome", [&] { ui.draw(12.0); });
        press(kDown);
        settle();
        render("set-welcome-legacy", [&] { ui.draw(12.0); });
        return 0;
    }

    /* PV_GLASS=1: the Porpoise look (4.1): the flow in its three modes, the
     * filters, rows, a game's page, the menu growing into Settings, Memory
     * Cards, the island; then Classic's island; then stop. */
    if (getenv("PV_GLASS"))
    {
        const std::uint32_t kL1 = 1u << 8, kUp = 1u << 0, kLeft = 1u << 2;
        settings.ui_theme = int(porpoise::ui::ThemeId::Glass);
        settings.glass_layout = 0;
        lib.games()[2].favourite = true;
        lib.games()[2].play_seconds = 6 * 3600 + 40 * 60;
        lib.games()[2].last_played = (long long)std::time(nullptr) - 2 * 86400;
        for (int mode : {0, 1, 2})
        {
            settings.ui_mode = mode;
            settle();
            settle();
            render(("gl-flow-" + std::to_string(mode)).c_str(), [&] { ui.draw(12.0); });
        }
        settings.ui_mode = 0;
        press(kR1);
        settle();
        render("gl-filter-gc", [&] { ui.draw(12.0); });
        press(kL1);
        settle();
        settings.glass_layout = 1;
        press(kRight);
        settle();
        render("gl-rows", [&] { ui.draw(12.0); });
        settings.ui_mode = 2;
        settle();
        render("gl-rows-light", [&] { ui.draw(12.0); });
        settings.ui_mode = 0;
        settings.glass_layout = 0;
        settle();
        press(kSquare);
        for (int i = 0; i < 12; ++i)
            ui.update(none, 0.016);
        render("gl-details-in", [&] { ui.draw(12.0); });
        settle();
        settle();
        render("gl-details", [&] { ui.draw(12.0); });
        press(kRight);
        settle();
        render("gl-details-states", [&] { ui.draw(12.0); });
        press(kCross);
        settle();
        render("gl-states", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kCross);
        settle();
        render("gl-states", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kLeft);
        press(kCircle);
        settle();
        press(kTriangle);
        settle();
        render("gl-menu", [&] { ui.draw(12.0); });
        press(kDown, 6);
        settle();
        render("gl-menu-down", [&] { ui.draw(12.0); });
        press(kCross);
        for (int i = 0; i < 8; ++i)
            ui.update(none, 0.016);
        render("gl-grow", [&] { ui.draw(12.0); });
        settle();
        render("gl-settings", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        render("gl-settings-rail", [&] { ui.draw(12.0); });
        press(kCircle);
        for (int i = 0; i < 8; ++i)
            ui.update(none, 0.016);
        render("gl-shrink", [&] { ui.draw(12.0); });
        settle();
        render("gl-menu-back", [&] { ui.draw(12.0); });
        press(kUp, 12);
        press(kDown, 2); /* Memory Cards */
        press(kCross);
        settle();
        settle();
        render("gl-cards", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kCircle);
        settle();
        ui.preview_glass(false, 0, true);
        settle();
        render("gl-island", [&] { ui.draw(12.0); });
        settings.ui_mode = 2;
        settle();
        render("gl-island-light", [&] { ui.draw(12.0); });
        settings.ui_mode = 1;
        ui.preview_glass(false, 0, false);
        ui.preview_ctl_toasts();
        for (int i = 0; i < 30; ++i)
            ui.update(none, 0.016);
        render("gl-news", [&] { ui.draw(12.0); });
        settings.ui_mode = 0;
        settings.ui_theme = 0;
        settle();
        render("classic", [&] { ui.draw(12.0); });
        ui.preview_glass(false, 0, true);
        settle();
        render("classic-island", [&] { ui.draw(12.0); });
        return 0;
    }

    /* PV_GLASS2=1: the Porpoise look's other screens: a game's achievements
     * list, the Apply question, the Online Hub, the launch screen and the
     * in-game menu; then stop. */
    if (getenv("PV_GLASS2"))
    {
        settings.ui_theme = int(porpoise::ui::ThemeId::Glass);
        settings.ui_mode = 0;
        static porpoise::ui::AchievementSet fake;
        fake.game_id = 11;
        fake.title = "Garden Critters";
        mkdir((out + "/achievements").c_str(), 0777);
        mkdir((out + "/achievements/badges").c_str(), 0777);
        const char *titles[] = {"First Steps", "Bug Collector", "Night Owl", "Speed Runner", "Perfectionist",
                                "Secret Garden", "Rainy Day", "Full House", "Long Haul", "Last Bloom"};
        for (int i = 0; i < 10; ++i)
        {
            porpoise::ui::Achievement a;
            a.id = unsigned(100 + i);
            a.points = (i % 4 + 1) * 5;
            a.unlocked = i < 4;
            a.unlock_time = a.unlocked ? (long long)std::time(nullptr) - (i + 1) * 86400 : 0;
            a.title = titles[i];
            a.description = "Something to do in the game to get this one.";
            a.badge_url = "https://media.retroachievements.org/Badge/" + std::to_string(5000 + i) + ".png";
            fake.list.push_back(a);
            const std::string from = covers + "/PRVW0" + std::to_string(1 + i % 8) + ".png";
            std::system(("cp '" + from + "' '" + out + "/achievements/badges/" + porpoise::ui::badge_file(a.badge_url) +
                         "'").c_str());
        }
        fake.total = 10;
        fake.unlocked = 4;
        fake.points_total = 125;
        fake.points = 50;
        ui.set_achievement_source([](const std::string &, bool) { return fake; });
        settle();
        /* Play: the library parts and the case flips into the launch screen. */
        {
            porpoise::ui::Game *sel = nullptr;
            for (auto &gm : lib.games())
                if (gm.cover && !sel)
                    sel = &gm;
            render("gl2-play-0", [&] { ui.draw(12.0); });
            bool begun = false;
            for (double at : {0.15, 0.35, 0.55, 0.75})
            {
                char name[32];
                std::snprintf(name, sizeof name, "gl2-play-%03d", int(at * 1000));
                render(name, [&] {
                    if (!begun)
                    {
                        begun = true;
                        ui.begin_launch(sel ? sel : &lib.games()[0]);
                        ui.preview_launch_start(12.0);
                    }
                    if (ui.launch_intro(12.0 + at) < 1.0f)
                        ui.draw(12.0 + at);
                    ui.draw_launch(12.0 + at);
                });
            }
            ui.return_from_game();
            settle();
        }
        auto frames = [&](int n) {
            /* Drawn each frame too, as on the console (what glides keeps going). */
            for (int i = 0; i < n; ++i)
            {
                gfx.begin(0, float(W), float(H), 12.0f, 0.0f, false);
                ui.draw(12.0);
                ui.update(none, 0.016);
            }
        };
        if (getenv("PV_MOTION"))
        {
            /* Motion, caught partway: the menu coming in, a filter changing, a
             * game's achievements sliding up, Game Settings growing. */
            frames(3);
            {
                Input in;
                in.held = kTriangle;
                ui.update(in, 0.016);
            }
            for (int f : {4, 4, 6})
            {
                frames(f);
                static int k = 0;
                render(("mo-menu-" + std::to_string(++k)).c_str(), [&] { ui.draw(12.0); });
            }
            /* The menu grows into Settings (Video). */
            press(kDown, 6);
            frames(20);
            {
                Input in;
                in.held = kCross;
                ui.update(in, 0.016);
            }
            for (int f : getenv("PV_EVERY") ? std::vector<int>(26, 1) : std::vector<int>{3, 4, 5, 8})
            {
                frames(f);
                static int k = 0;
                render(("mo-grow-" + std::to_string(++k)).c_str(), [&] { ui.draw(12.0); });
            }
            press(kCircle);
            press(kCircle);
            settle();
            press(kCircle);
            settle();
            frames(3);
            {
                Input in;
                in.held = kR1;
                ui.update(in, 0.016);
            }
            for (int f : {4, 5, 5, 8})
            {
                frames(f);
                static int k = 0;
                render(("mo-filter-" + std::to_string(++k)).c_str(), [&] { ui.draw(12.0); });
            }
            settle();
        }
        press(kSquare); /* Details */
        settle();
        render("gl2-details-ach", [&] { ui.draw(12.0); });
        if (getenv("PV_MOTION"))
        {
            frames(3);
            {
                Input in;
                in.held = 1u << 1;
                ui.update(in, 0.016);
            }
            for (int f : {4, 5, 8})
            {
                frames(f);
                static int k = 0;
                render(("mo-ach-" + std::to_string(++k)).c_str(), [&] { ui.draw(12.0); });
            }
            press(kCircle);
            settle();
            press(kRight, 2); /* Game Settings */
            frames(3);
            {
                Input in;
                in.held = kCross;
                ui.update(in, 0.016);
            }
            for (int f : getenv("PV_EVERY") ? std::vector<int>(26, 1) : std::vector<int>{4, 5, 6, 10})
            {
                frames(f);
                static int k = 0;
                render(("mo-gs-" + std::to_string(++k)).c_str(), [&] { ui.draw(12.0); });
            }
            return 0;
        }
        press(kDown);
        settle();
        press(kDown, 2);
        settle();
        render("gl2-ach-list", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kCircle);
        settle();
        /* Settings with a change, then leaving: the Apply question. */
        press(kTriangle);
        settle();
        press(1u << 1, 7); /* down to Enhancements */
        press(kCross);
        settle();
        press(kRight);
        settle();
        press(kCircle);
        press(kCircle);
        settle();
        render("gl2-apply", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        ui.preview_hub(0, false, 0, 0, "", {});
        settle();
        render("gl2-hub", [&] { ui.draw(12.0); });
        ui.begin_launch(&lib.games()[3]);
        ui.preview_launch_start(10.0);
        render("gl2-launch", [&] { ui.draw_launch(12.4); });
        porpoise::ui::Game &g0 = lib.games()[0];
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        porpoise::Settings play = settings;
        ui.open_game_menu(&g0, &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        render("gl2-ingame", [&] {
            gfx.background();
            if (frame)
                gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
            ui.draw_game_menu(12.0);
        });
        return 0;
    }

    /* PV_FUZZ=<theme>: Settings in that look, every row changed one way and back,
     * then Cross on each (a crash hunt), rendering every 10th row. */
    if (const char *fz = getenv("PV_FUZZ"))
    {
        const std::uint32_t kUp = 1u << 0, kLeft = 1u << 2, kR1b = 1u << 9;
        settings.ui_theme = atoi(fz);
        ui.preview_look_changed();
        settle();
        press(kR1b, 2);
        settle();
        render("fuzz-settings", [&] { ui.draw(12.0); });
        const bool cross = getenv("PV_FUZZ_CROSS") != nullptr;
        const int section = getenv("PV_FUZZ_SECTION") ? atoi(getenv("PV_FUZZ_SECTION")) : 3;
        press(kDown, section);
        press(kRight); /* into the section */
        settle();
        for (int i = 0; i < 60; ++i)
        {
            if (i > 0)
                press(kDown);
            if (!cross)
            {
                press(kRight);
                press(kLeft);
            }
            if (cross)
            {
                press(kCross);
                for (int k = 0; k < 10; ++k)
                    ui.update(none, 0.016);
                render(("fuzz-" + std::to_string(i)).c_str(), [&] { ui.draw(12.0); });
                press(kCircle);
                for (int k = 0; k < 10; ++k)
                    ui.update(none, 0.016);
            }
            else
                render(("fuzz-" + std::to_string(i)).c_str(), [&] { ui.draw(12.0); });
            std::fprintf(stderr, "fuzz row %d\n", i);
        }
        (void)kUp;
        return 0;
    }

    /* PV_GUIDE=<theme>: How to Hold It in that look, every controller. */
    if (const char *gd = getenv("PV_GUIDE"))
    {
        settings.ui_theme = atoi(gd);
        ui.preview_look_changed();
        settle();
        ui.preview_wii_guide();
        for (int c = 0; c < 7; ++c)
        {
            for (int k = 0; k < 20; ++k)
                ui.update(none, 0.016);
            render(("guide-" + std::to_string(c)).c_str(), [&] { ui.draw(12.0); });
            press(kRight);
        }
        return 0;
    }

    /* PV_BRAND=1: the top left in each look and mode (the wordmark), then stop. */
    if (getenv("PV_BRAND"))
    {
        struct Look
        {
            const char *name;
            int theme, mode;
        } looks[] = {{"brand-new-light", int(porpoise::ui::ThemeId::Glass), 2},
                     {"brand-new-dark", int(porpoise::ui::ThemeId::Glass), 0},
                     {"brand-new-oled", int(porpoise::ui::ThemeId::Glass), 1},
                     {"brand-classic", 0, 0},
                     {"brand-revolution", int(porpoise::ui::ThemeId::Revolution), 0},
                     {"brand-starcube", int(porpoise::ui::ThemeId::StarCube), 0}};
        for (const Look &l : looks)
        {
            settings.ui_theme = l.theme;
            settings.ui_mode = l.mode;
            ui.preview_look_changed();
            settle();
            render(l.name, [&] { ui.draw(12.0); });
        }
        return 0;
    }

    /* PV_CTL=1: the Controllers pages, the notices and the badges, then stop. */
    if (getenv("PV_CTL"))
    {
        if (getenv("PV_CTL_GLASS"))
            settings.ui_theme = int(porpoise::ui::ThemeId::Glass);
        settings.wii_move = true;
        settings.wii_presets[0].used = true;
        settings.wii_presets[0].name = 0;
        struct Shot
        {
            const char *name;
            int step;
            bool move;
            int controller, point, row;
        } shots[] = {
            {"ctl-main-move", 0, true, 0, 0, 1},       {"ctl-main-move-off", 0, true, 1, 0, 2},
            {"ctl-main-dualsense", 0, false, 0, 0, 6}, {"ctl-main-classic", 0, false, 3, 0, 3},
            {"ctl-points-1", 8, true, 0, 0, 0},       {"ctl-points-3", 8, true, 0, 2, 0},
            {"ctl-tune-move", 6, true, 0, 0, 0},      {"ctl-tune-dualsense", 6, false, 0, 0, 1},
            {"ctl-template", 7, true, 0, 0, 0},
            {"ctl-main-move-color", 0, true, 0, 0, 7}, {"ctl-ready", 9, true, 0, 0, 0},
        };
        settings.move_color = 1; /* a blue ball */
        if (const char *only = getenv("PV_HUB"))
        {
            if (const char *mode = getenv("PV_HUB_MODE"))
                settings.ui_mode = atoi(mode); /* 0 dark, 1 OLED, 2 light */
            if (const char *theme = getenv("PV_HUB_THEME"))
                settings.ui_theme = atoi(theme);
            /* The Controller Hub: name, move, controller, view, open, focus, sub. */
            struct Hub
            {
                const char *name;
                bool move;
                int controller, view, open, focus, sub;
            } hubs[] = {
                {"hub-front-move", true, 0, 0, -1, 3, 0},      {"hub-front-ds", false, 0, 0, -1, 1, 0},
                {"hub-all-move", true, 0, 1, -1, 100, 0},      {"hub-hold-move", true, 0, 1, 0, 100, 0},
                {"hub-buttons-move", true, 0, 1, 1, 101, 0},  {"hub-buttons-ds", false, 0, 1, 1, 1004, 0},
                {"hub-buttons-ds-remote", false, 1, 1, 1, 101, 0},
                {"hub-pointer-move", true, 0, 1, 2, 1008, 0}, {"hub-pointer-ds", false, 0, 1, 2, 1007, 0},
                {"hub-ball", true, 0, 1, 3, 1012, 1},          {"hub-motion-move", true, 0, 1, 4, 1013, 0},
                {"hub-motion-ds", false, 0, 1, 4, 1015, 0},    {"hub-other", false, 3, 1, 5, 1019, 0},
                {"hub-sensor", true, 0, 1, 6, 106, 0},         {"hub-saved", true, 0, 1, 7, 1024, 0},
                {"hub-hold-two", false, 4, 1, 0, 1000, 0},     {"hub-front-gc", false, 5, 0, -1, 3, 1},
            };
            if (!strcmp(only, "players"))
            {
                /* 4.5 Beta 4: the switcher, and players 2 to 4's pages. */
                struct P
                {
                    const char *name;
                    int controller, player, hold, as;
                    bool move;
                } ps[] = {{"hub-p1", 0, 0, 0, 0, true},      {"hub-p2-ds", 0, 1, 0, 0, true},
                          {"hub-p2-ds-classic", 0, 1, 0, 4, false}, {"hub-p3-move", 0, 2, 1, 0, true},
                          {"hub-p3-nav", 0, 2, 2, 0, true},  {"hub-p4-moveds", 0, 3, 3, 0, true},
                          {"hub-p2-two", 4, 1, 0, 0, false}};
                for (const P &q : ps)
                {
                    ui.preview_ctl_hub(q.move, q.controller, 0, -1, 1, 0);
                    ui.preview_hub_player(q.player, q.hold, q.as);
                    for (int i = 0; i < 30; ++i)
                        ui.update(none, 0.016);
                    render(q.name, [&] { ui.draw(12.0); });
                }
                return 0;
            }
            if (!strcmp(only, "walk"))
            {
                /* The D-pad through it: the tiles, All settings, a section, a value, back. */
                const std::uint32_t kUp = 1u << 0, kLeft = 1u << 2;
                ui.preview_ctl_hub(false, 0, 0, -1, 1, 0);
                settle();
                press(kDown, 2);
                press(kRight, 1);
                settle();
                render("hub-walk-1-tile", [&] { ui.draw(12.0); });
                press(kDown);
                press(kCross);
                settle();
                render("hub-walk-2-all", [&] { ui.draw(12.0); });
                press(kDown, 2); /* to Pointer's head */
                press(kCross);
                press(kDown, 2); /* Aim with, Speed */
                press(kRight, 2);
                settle();
                render("hub-walk-3-speed", [&] { ui.draw(12.0); });
                press(kDown, 6);
                settle();
                render("hub-walk-4-down", [&] { ui.draw(12.0); });
                press(kUp, 12);
                press(kLeft);
                settle();
                render("hub-walk-5-main", [&] { ui.draw(12.0); });
                press(kCircle);
                settle();
                render("hub-walk-6-back", [&] { ui.draw(12.0); });
                press(kCircle);
                settle();
                render("hub-walk-7-closed", [&] { ui.draw(12.0); });
                return 0;
            }
            for (const Hub &h : hubs)
            {
                if (*only && !strstr(h.name, only))
                    continue;
                ui.preview_ctl_hub(h.move, h.controller, h.view, h.open, h.focus, h.sub);
                for (int i = 0; i < 40; ++i)
                    ui.update(none, 0.016);
                ui.preview_ctl_hub(h.move, h.controller, h.view, h.open, h.focus, h.sub);
                for (int i = 0; i < 20; ++i)
                    ui.update(none, 0.016);
                render(h.name, [&] { ui.draw(12.0); });
            }
            return 0;
        }
        for (const Shot &s : shots)
        {
            ui.preview_controllers(s.step, s.move, s.controller, s.point, s.row);
            for (int i = 0; i < 20; ++i)
                ui.update(none, 0.016);
            ui.preview_controllers(s.step, s.move, s.controller, s.point, s.row);
            render(s.name, [&] { ui.draw(12.0); });
        }
        press(kCircle, 6);
        settle();
        for (int theme : {0, 1, int(porpoise::ui::ThemeId::StarCube)})
        {
            settings.ui_theme = theme;
            ui.preview_ctl_toasts();
            for (int i = 0; i < 30; ++i)
                ui.update(none, 0.016);
            const std::string name = "ctl-toasts-" + std::to_string(theme);
            render(name.c_str(), [&] { ui.draw(12.0); });
        }
        return 0;
    }

    /* PREVIEW_27=1: 2.7's Apply: Settings with changes waiting, the question
     * on leaving, the restart question; the in-game menu the same way; the
     * cover's glide into the launch screen; then stop. */
    if (std::getenv("PREVIEW_27"))
    {
        if (const char *lang = std::getenv("PREVIEW_PLANG"))
            porpoise::ui::apply_language(std::atoi(lang), "");
        /* The glide: the library, then the entrance at a few moments. */
        settle();
        render("v27-glide-0", [&] { ui.draw(12.0); });
        porpoise::ui::Game *sel = nullptr;
        for (auto &gm : lib.games())
            if (gm.cover && !sel)
                sel = &gm;
        bool begun = false;
        for (double at : {0.08, 0.2, 0.32, 0.45, 0.7})
        {
            char name[32];
            std::snprintf(name, sizeof name, "v27-glide-%03d", int(at * 1000));
            render(name, [&] {
                if (!begun)
                {
                    begun = true;
                    ui.begin_launch(sel ? sel : &lib.games()[0]);
                    ui.preview_launch_start(12.0);
                }
                if (ui.launch_intro(12.0 + at) < 1.0f)
                    ui.draw(12.0 + at);
                ui.draw_launch(12.0 + at);
            });
        }
        ui.return_from_game();
        settle();
        /* Settings: two changes waiting. */
        press(kR1, 2);
        settle();
        press(kRight); /* into Games */
        press(kRight); /* Find games automatically: off */
        press(kDown);
        press(kRight); /* Download covers: off */
        settle();
        render("v27-settings-pending", [&] { ui.draw(12.0); });
        press(kCircle); /* to the sections */
        press(kCircle); /* leaving: Apply or Discard? */
        settle();
        render("v27-settings-ask", [&] { ui.draw(12.0); });
        press(kCircle); /* keep editing */
        settle();
        press(kSquare); /* Apply */
        settle();
        render("v27-settings-applied", [&] { ui.draw(12.0); });
        press(kDown, 1); /* Video */
        press(kRight);
        press(kRight); /* Output resolution */
        press(kSquare);
        settle();
        render("v27-settings-restart", [&] { ui.draw(12.0); });
        press(kCircle);
        /* In a game: a change waiting, then the question on Resume. */
        porpoise::ui::Game &g0 = lib.games()[0];
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const char *name) {
            render(name, [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        auto tap = [&](std::uint32_t b, int times = 1) {
            for (int t = 0; t < times; ++t)
            {
                Input in;
                in.held = b;
                ui.update_game_menu(in, 0.016);
                for (int i = 0; i < 12; ++i)
                    ui.update_game_menu(none, 0.016);
            }
        };
        porpoise::Settings play = settings;
        ui.open_game_menu(&g0, &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        tap(kR1, 2); /* Graphics */
        tap(kRight); /* Shader compilation */
        shot("v27-ingame-pending");
        tap(kCircle);
        shot("v27-ingame-ask");
        tap(kCross); /* Apply: the shaders take effect at the next start */
        shot("v27-ingame-restart");
        /* A game's settings: its home screen tile. */
        ui.return_from_game();
        settle();
        ui.preview_game_settings(3);
        settle();
        press(kDown, 2); /* Home screen (after Recommended) */
        press(kRight);
        settle();
        render("v27-home-screen", [&] { ui.draw(12.0); });
        press(kDown, 3);
        settle();
        render("v27-home-screen-add", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_TILE=1: the tile art editor: the cover whole over its blur, the
     * background's pattern, a screenshot filling it, the picture picker. */
    if (std::getenv("PREVIEW_TILE"))
    {
        namespace art = porpoise::tileart;
        art::set_asset_dir(argv[1]);
        if (const char *lang = std::getenv("PREVIEW_PLANG"))
            porpoise::ui::apply_language(std::atoi(lang), "");
        settle();
        ui.preview_game_settings(3);
        settle();
        press(kDown, 2); /* Home screen */
        press(kRight);
        settle();
        press(kCross); /* Tile art: Edit */
        settle();
        render("tile-1-icon", [&] { ui.draw(12.0); });
        art::Spec d = art::defaults();
        art::Layer l = d.bg;
        l.behind = art::Pattern;
        l.x = 0.5f;
        ui.preview_tile_art(1, 2, l);
        settle();
        render("tile-2-bg-pattern", [&] { ui.draw(12.0); });
        const char *snap = std::getenv("PREVIEW_SNAP");
        l = d.bg;
        l.source = art::File;
        l.file = snap ? snap : "";
        l.fit = art::Fill;
        art::reset_position(l, true);
        ui.preview_tile_art(1, 1, l);
        art::Layer i = d.icon;
        i.fit = art::Fill;
        i.source = art::File;
        i.file = l.file;
        i.zoom = 1.5f;
        ui.preview_tile_art(0, 1, i);
        settle();
        render("tile-3-fill", [&] { ui.draw(12.0); });
        ui.preview_tile_art(1, 0, l);
        press(kCross); /* the picture picker */
        settle();
        render("tile-4-picker", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_ACH=1: a game's achievements on Details, its page, and the
     * in-game menu's Achievements tab; then stop. */
    if (std::getenv("PREVIEW_ACH"))
    {
        static porpoise::ui::AchievementSet fake;
        fake.game_id = 11;
        fake.title = "Garden Critters";
        fake.badge_url = "https://media.retroachievements.org/Images/000001.png";
        const char *titles[] = {"First Steps", "Bug Collector", "Night Owl", "Speed Runner", "Perfectionist",
                                "Secret Garden", "Rainy Day", "Full House", "Long Haul", "Last Bloom"};
        const char *descs[] = {"Finish the first garden.", "Catch 50 critters in one day.",
                               "Play through a whole night without resting.", "Clear the meadow in under 3 minutes.",
                               "Get a perfect score on every garden.", "Find the hidden gate behind the old oak.",
                               "Catch a critter in the rain.", "Fill every jar on the shelf.",
                               "Play for 10 hours.", "See the last flower bloom."};
        mkdir((out + "/achievements").c_str(), 0777);
        mkdir((out + "/achievements/badges").c_str(), 0777);
        for (int i = 0; i < 10; ++i)
        {
            porpoise::ui::Achievement a;
            a.id = unsigned(100 + i);
            a.points = (i % 4 + 1) * 5;
            a.unlocked = i < 4;
            a.unlock_time = a.unlocked ? 1759700000LL - i * 86400 : 0;
            a.type = i == 5 ? 1 : 0;
            a.rarity = 42.5f - float(i) * 3.7f;
            a.progress = i == 6 ? "3/10" : "";
            a.title = titles[i];
            a.description = descs[i];
            a.badge_url = "https://media.retroachievements.org/Badge/" + std::to_string(5000 + i) + ".png";
            fake.list.push_back(a);
            const std::string from = covers + "/PRVW0" + std::to_string(1 + i % 8) + ".png";
            const std::string to = out + "/achievements/badges/" + porpoise::ui::badge_file(a.badge_url);
            std::system(("cp '" + from + "' '" + to + "'").c_str());
        }
        std::system(("cp '" + covers + "/PRVW02.png' '" + out + "/achievements/badges/000001.png'").c_str());
        fake.total = 10;
        fake.unlocked = 4;
        fake.points_total = 125;
        fake.points = 50;
        ui.set_achievement_source([](const std::string &, bool) { return fake; });
        settle();
        press(kSquare); /* Details */
        settle();
        render("ach-details", [&] { ui.draw(12.0); });
        press(kSquare); /* the achievements page */
        settle();
        render("ach-page", [&] { ui.draw(12.0); });
        press(kDown, 5);
        settle();
        render("ach-page-locked", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kCircle);
        settle();
        porpoise::Settings play = settings;
        ui.open_game_menu(&lib.games()[0], &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        Input l1;
        l1.held = 1u << 8; /* L1: round to the last tab */
        ui.update_game_menu(l1, 0.016);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        render("ach-ingame", [&] {
            gfx.background();
            if (frame)
                gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
            ui.draw_game_menu(12.0);
        });
        return 0;
    }
    /* PREVIEW_PATCHES=1: the in-game menu's Patches tab for a game with codes
     * (Super Mario Sunshine's ID, Dolphin's own lists and the widescreen
     * collection), scrolled, and the Video tab's Widescreen row; then stop. */
    if (std::getenv("PREVIEW_PATCHES"))
    {
        const char *root = std::getenv("PREVIEW_ROOT");
        const std::string base = root ? root : "/root/porpoise-release";
        ui.set_sys_dir(base + "/.deps/dolphin-src/Data/Sys");
        porpoise::ui::widescreen::set_dir(base + "/assets/widescreen");
        if (const char *lang = std::getenv("PREVIEW_PLANG"))
            porpoise::ui::apply_language(std::atoi(lang), "");
        porpoise::ui::Game &g0 = lib.games()[0];
        g0.id = std::getenv("PREVIEW_GAMEID") ? std::getenv("PREVIEW_GAMEID") : "GMSE01";
        g0.platform = "GameCube";
        porpoise::Settings play = settings;
        play.ws_plan = 1;
        ui.open_game_menu(&g0, &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const char *name) {
            render(name, [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        auto tap = [&](std::uint32_t b, int times = 1) {
            for (int t = 0; t < times; ++t)
            {
                Input in;
                in.held = b;
                ui.update_game_menu(in, 0.016);
                for (int i = 0; i < 12; ++i)
                    ui.update_game_menu(none, 0.016);
            }
        };
        tap(kR1); /* Video */
        tap(kDown);
        shot("patches-video");
        tap(kR1, 2); /* Audio */
        shot("patches-audio");
        tap(kDown, 2);
        tap(kRight); /* Responsive */
        shot("patches-audio-preset");
        tap(kR1, 2); /* Patches */
        shot("patches-tab");
        tap(kDown, 2);
        shot("patches-code");
        tap(kDown, 9);
        shot("patches-scrolled");
        return 0;
    }
    /* PREVIEW_25=1: 2.5's rows: a game's graphics mods (Super Mario Sunshine),
     * the Controls tab's extras, a Wii game with the GameCube controller, and
     * Settings > Controls; then stop. */
    if (std::getenv("PREVIEW_25"))
    {
        if (const char *lang = std::getenv("PREVIEW_PLANG"))
            porpoise::ui::apply_language(std::atoi(lang), "");
        porpoise::ui::Game &g0 = lib.games()[0];
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const char *name) {
            render(name, [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        auto tap = [&](std::uint32_t b, int times = 1) {
            for (int t = 0; t < times; ++t)
            {
                Input in;
                in.held = b;
                ui.update_game_menu(in, 0.016);
                for (int i = 0; i < 12; ++i)
                    ui.update_game_menu(none, 0.016);
            }
        };
        g0.id = "GMSE01";
        g0.platform = "GameCube";
        porpoise::Settings play = settings;
        play.ws_plan = 1;
        ui.open_game_menu(&g0, &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        tap(kR1, 2); /* Graphics */
        tap(kDown, 12);
        shot("v25-graphics-sunshine");
        tap(kR1, 2); /* Controls */
        tap(kDown, 4);
        shot("v25-controls-gamecube");
        g0.id = "RSBE01";
        g0.platform = "Wii";
        porpoise::Settings wii_play = settings;
        wii_play.wii_controller = porpoise::pad::WiiGameCube;
        ui.open_game_menu(&g0, &wii_play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        tap(kR1, 4); /* Controls */
        shot("v25-controls-brawl-gamecube");
        g0.id = "SX3P01";
        porpoise::Settings tower = settings;
        ui.open_game_menu(&g0, &tower);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        tap(kR1, 2); /* Graphics */
        tap(kDown, 12);
        shot("v25-graphics-pandoras-tower");
        return 0;
    }
    /* PREVIEW_ACCOUNT=1: the RetroAchievements panel signed out, typing, busy
     * with an error, and signed in; then stop. */
    if (std::getenv("PREVIEW_ACCOUNT"))
    {
        static porpoise::ui::App::RaState ra;
        ui.set_ra([] { return ra; }, [](const std::string &, const std::string &) { return true; }, [] {});
        settle();
        ui.preview_account(false, 0, 1, 0);
        ui.preview_account(false, 2, 1, 0);
        settle();
        render("account-out", [&] { ui.draw(12.0); });
        ui.preview_account(true, 1, 2, 3);
        settle();
        render("account-typing", [&] { ui.draw(12.0); });
        ra.message = "Wrong username or password.";
        ui.preview_account(false, 2, 1, 0);
        settle();
        render("account-error", [&] { ui.draw(12.0); });
        ra.message.clear();
        ra.signed_in = true;
        ra.user = "Ripalda";
        ra.points = 1250;
        ui.preview_account(false, 0, 1, 0);
        settle();
        render("account-in", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_SHARE=1: the network share panel - a new share tested, typing,
     * a problem, shared folders found, a saved one being changed - and
     * Settings > Games with a share in it; then stop. */
    /* PREVIEW_SHARE_LIVE=1 (with NETFS_TEST_LOOPBACK=1 and the local Samba and
     * NFS servers): the panel's whole way through, on the network - the scan,
     * a computer picked, its shared folders, a test, NFS, its exports, a test,
     * a save; then stop. */
    if (std::getenv("PREVIEW_SHARE_LIVE"))
    {
        const std::uint32_t kUp = 1u << 0, kLeft = 1u << 2;
        porpoise::netfs::set_shares({});
        settle();
        auto wait = [&](const char *what) {
            for (int i = 0; i < 1500 && ui.preview_share_waiting(); ++i)
            {
                ui.update(none, 0.016);
                usleep(10000);
            }
            settle();
            std::fprintf(stderr, "[%s] %s\n", what, ui.preview_share_state().c_str());
        };
        ui.preview_share_open(-1);
        wait("scanned");
        render("live-1-scanned", [&] { ui.draw(12.0); });
        press(kCross); /* the first computer */
        settle();
        wait("picked");
        render("live-2-found", [&] { ui.draw(12.0); });
        press(kRight); /* the next shared folder, if any */
        press(kLeft);
        press(kDown, 3); /* to the buttons */
        press(kLeft);    /* Test */
        press(kCross);
        wait("tested smb");
        render("live-3-tested", [&] { ui.draw(12.0); });
        press(kUp, 5); /* Share Type */
        std::fprintf(stderr, "[at] %s\n", ui.preview_share_state().c_str());
        press(kRight); /* NFS */
        settle();
        wait("nfs found");
        render("live-4-nfs", [&] { ui.draw(12.0); });
        press(kDown, 3); /* Test */
        press(kCross);
        wait("tested nfs");
        render("live-5-nfs-tested", [&] { ui.draw(12.0); });
        press(kUp, 3); /* SMB again: what it had */
        press(kLeft);
        settle();
        wait("back to smb");
        press(kRight);
        settle();
        wait("nfs again");
        press(kDown, 3);
        press(kRight); /* Save */
        press(kCross);
        settle();
        std::fprintf(stderr, "[saved] %s\n", ui.preview_share_state().c_str());
        for (const auto &s : porpoise::netfs::shares())
            std::fprintf(stderr, "  share %s %s share=%s folder=%s proto=%s\n", s.name.c_str(), s.host.c_str(),
                         s.share.c_str(), s.folder.c_str(), s.protocol.c_str());
        return 0;
    }
    /* PREVIEW_B5=1: 3.0 Alpha 5's settings - the scrolling sections, Online,
     * Textures - and the Online hub; then stop. */
    if (std::getenv("PREVIEW_B5"))
    {
        settle();
        press(kR1, 2);
        settle();
        for (const char *sec : {"Games", "Storage", "Textures", "Online", "Console", "Accessibility", "About"})
        {
            ui.preview_section(sec);
            settle();
            render((std::string("b5-rail-") + sec).c_str(), [&] { ui.draw(12.0); });
        }
        ui.preview_section("Textures", 1);
        settle();
        render("b5-textures-rows", [&] { ui.draw(12.0); });
        ui.preview_section("Online", 0);
        settle();
        render("b5-online-rows", [&] { ui.draw(12.0); });
        const std::vector<std::pair<std::string, std::string>> servers = {
            {"Dawnofthed WFC", "172.8.199.18"}, {"Home Server", "192.168.1.40"}};
        ui.preview_hub(0, false, 0, 0, "", servers);
        settle();
        render("b5-hub-custom", [&] { ui.draw(12.0); });
        ui.preview_hub(0, true, 1);
        settle();
        render("b5-hub-custom-server", [&] { ui.draw(12.0); });
        ui.preview_hub(0, true, 5);
        settle();
        render("b5-hub-custom-https", [&] { ui.draw(12.0); });
        ui.preview_hub(1, false, 0);
        settle();
        render("b5-hub-wiilink", [&] { ui.draw(12.0); });
        ui.preview_hub(2, false, 0);
        settle();
        render("b5-hub-wiimmfi", [&] { ui.draw(12.0); });
        ui.preview_hub(3, true, 0);
        settle();
        render("b5-hub-channels", [&] { ui.draw(12.0); });
        ui.preview_hub(0, true, 3, 1, "Dawnofthed");
        settle();
        render("b5-hub-typing-name", [&] { ui.draw(12.0); });
        ui.preview_hub(0, true, 3, 2, "172.8.199.18");
        settle();
        render("b5-hub-typing-dns", [&] { ui.draw(12.0); });
        ui.preview_hub_close();
        /* A game's Cheats and Patches as cards (own codes from out/cheats). */
        for (int i = 0; i < int(lib.games().size()); ++i)
            if (lib.games()[std::size_t(i)].id == "PRVW03")
            {
                ui.preview_game_settings(i);
                settle();
                ui.preview_section("Cheats and Patches", 2);
                settle();
                render("b5-cheats", [&] { ui.draw(12.0); });
                ui.preview_section("Textures", 0);
                settle();
                render("b5-game-textures", [&] { ui.draw(12.0); });
                ui.preview_section("Online", 0);
                settle();
                render("b5-game-online", [&] { ui.draw(12.0); });
                press(kCircle, 2);
                settle();
                break;
            }
        if (const char *langs = std::getenv("PREVIEW_B5_LANGS"))
            for (const char *at = langs; *at;)
            {
                const int lang = std::atoi(at);
                while (*at && *at != ',')
                    ++at;
                if (*at == ',')
                    ++at;
                settings.ui_language = lang;
                porpoise::ui::apply_language(lang);
                ui.language_changed();
                settle();
                while (gfx.cjk_busy())
                    settle();
                ui.preview_section("Online");
                settle();
                render(("b5-online-" + std::to_string(lang)).c_str(), [&] { ui.draw(12.0); });
                ui.preview_hub(0, true, 1, 0, "", servers);
                settle();
                render(("b5-hub-" + std::to_string(lang)).c_str(), [&] { ui.draw(12.0); });
                ui.preview_hub_close();
            }
        return 0;
    }
    if (std::getenv("PREVIEW_SHARE"))
    {
        porpoise::netfs::set_shares({{"Games", "192.168.1.20", "Games", "", "ruben", "secret"},
                                     {"games", "192.168.1.30", "/volume1/games", "gc", "", "", "nfs"}});
        const std::vector<porpoise::netfs::Found> net = {{"192.168.1.20", "RIPALDA", true, false},
                                                         {"192.168.1.30", "", true, true},
                                                         {"192.168.1.44", "DISKSTATION", true, true}};
        settle();
        /* Rows: 0 network, 1 type, 2 computer, 3 folder, 4 user, 5 password, 6 buttons. */
        ui.preview_share(-1, 0, 0, false, "", false);
        ui.preview_share_network({}, true, false, 0);
        settle();
        render("share-scanning", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 0, 0, false, "", false);
        ui.preview_share_network({}, false, false, 0);
        settle();
        render("share-none", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 0, 0, false, "", false);
        ui.preview_share_network(net, false, false, 0);
        settle();
        render("share-network", [&] { ui.draw(12.0); });
        /* PREVIEW_SHARE_LANGS=1: the panel and the test build's notice in long
         * and CJK languages too. */
        if (std::getenv("PREVIEW_SHARE_LANGS"))
        {
            for (int lang : {9, 12, 6, 15, 2})
            {
                settings.ui_language = lang;
                porpoise::ui::apply_language(lang);
                ui.language_changed();
                settle();
                while (gfx.cjk_busy())
                    settle();
                ui.preview_share(-1, 3, 0, false, "", false);
                ui.preview_share_network(net, false, true, 0);
                ui.preview_share_busy(true);
                settle();
                render(("share-lang-" + std::to_string(lang)).c_str(), [&] { ui.draw(12.0); });
                ui.preview_share(-1, 6, 0, false,
                                 porpoise::ui::tr("The computer didn't let Porpoise in. Allow this PS5's address in "
                                                  "the export's settings. Some NAS boxes also need connections from "
                                                  "non-privileged ports allowed (\"insecure\")."),
                                 false);
                ui.preview_share_network(net, false, false, 0);
                settle();
                render(("share-lang-smb-" + std::to_string(lang)).c_str(), [&] { ui.draw(12.0); });
                press(kCircle);
                ui.show_testing_notice();
                settle();
                render(("notice-lang-" + std::to_string(lang)).c_str(), [&] { ui.draw(12.0); });
                press(kCross);
                settle();
            }
            return 0;
        }
        /* PREVIEW_SHARE_THEMES=1: the panel in other themes too. */
        if (std::getenv("PREVIEW_SHARE_THEMES"))
        {
            for (int theme : {1, 2, 6, 12, 16, 18, 14})
            {
                settings.ui_theme = theme;
                settle();
                while (gfx.theme_fonts_busy())
                {
                    settle();
                    usleep(10000);
                }
                ui.preview_share(-1, 1, 0, false, "Found 3 shared folders. Left and right pick one.", true,
                                 {"Games", "Media"});
                ui.preview_share_network(net, false, false, 1);
                settle();
                render(("share-theme-" + std::to_string(theme)).c_str(), [&] { ui.draw(12.0); });
            }
            settings.ui_theme = 0;
            settle();
            return 0;
        }
        ui.preview_share(-1, 1, 0, false, "", false);
        ui.preview_share_network(net, false, true, 0);
        settle();
        render("share-nfs", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 2, 0, true, "", false);
        ui.preview_share_numeric();
        settle();
        render("share-numpad", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 6, 0, false, "Connected. Porpoise can read this folder.", true);
        settle();
        render("share-tested", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 3, 0, true, "", false);
        settle();
        render("share-typing", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 6, 0, false, "", false);
        ui.preview_share_busy(false);
        settle();
        render("share-busy", [&] { ui.draw(12.1); });
        ui.preview_share(-1, 6, 0, false,
                         "Couldn't reach 192.168.1.20. Check the address, that the computer is on, and that it "
                         "shares files on the network.",
                         false);
        settle();
        render("share-problem", [&] { ui.draw(12.0); });
        ui.preview_share(-1, 3, 0, false, "Found 3 shared folders. Left and right pick one.", true,
                         {"Games", "Media", "Public"});
        settle();
        render("share-found", [&] { ui.draw(12.0); });
        ui.preview_share(0, 6, 2, false, "", false);
        settle();
        render("share-edit", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        /* Settings > Games, down to the share's row. */
        press(kR1, 2);
        press(1u << 0, 12);
        const int section = std::getenv("PREVIEW_SECTION") ? std::atoi(std::getenv("PREVIEW_SECTION")) : 6;
        const int row = std::getenv("PREVIEW_ROW") ? std::atoi(std::getenv("PREVIEW_ROW")) : 12;
        press(kDown, section);
        press(kRight);
        press(kDown, row);
        settle();
        render("share-settings", [&] { ui.draw(12.0); });
        /* The DNS server for online play. */
        ui.preview_dns("172.8.199.18", "");
        settle();
        render("dns-panel", [&] { ui.draw(12.0); });
        ui.preview_dns("172.8.199", "That isn't an address. Type one like 192.168.1.2, or delete it all for Automatic.");
        render("dns-problem", [&] { ui.draw(12.0); });
        ui.preview_dns_close();
        /* The test build's notice, over the library. */
        press(kCircle, 3);
        ui.show_testing_notice();
        settle();
        render("testing-notice", [&] { ui.draw(12.0); });
        for (int theme : {1, 12})
        {
            settings.ui_theme = theme;
            settle();
            while (gfx.theme_fonts_busy())
            {
                settle();
                usleep(10000);
            }
            render(("testing-notice-" + std::to_string(theme)).c_str(), [&] { ui.draw(12.0); });
        }
        settings.ui_theme = 0;
        return 0;
    }
    /* PREVIEW_WIIMAP=1: Customize buttons on each controller's tab; then stop. */
    if (std::getenv("PREVIEW_WIIMAP"))
    {
        settle();
        for (int kind = 0; kind < 5; ++kind)
        {
            ui.preview_mapping(kind, kind == 0 ? 1 : 2);
            settle();
            render(("wiimap-" + std::to_string(kind)).c_str(), [&] { ui.draw(12.0); });
        }
        settings.wii_buttons[0][0] = 2, settings.wii_buttons[0][2] = 0; /* Cross and Square swapped */
        ui.preview_mapping(1, 0);
        settle();
        render("wiimap-custom", [&] { ui.draw(12.0); });
        settings.wii_buttons_use = 0; /* Porpoise's, locked */
        ui.preview_mapping(3, 3);
        settle();
        render("wiimap-locked", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_NAND=1: Settings > Console with Wii System Data, and the import's
     * progress; then stop. */
    if (std::getenv("PREVIEW_NAND"))
    {
        settle();
        press(kR1, 2);
        settle();
        ui.preview_section("Console", 6);
        settle();
        render("nand-console", [&] { ui.draw(12.0); });
        ui.preview_nand(1840, 4211);
        settle();
        render("nand-progress", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_CODE=1: a game's Cheats and Patches with Add a Code, and the pad; then stop. */
    if (std::getenv("PREVIEW_CODE"))
    {
        settle();
        int gi = 0;
        for (int i = 0; i < int(lib.games().size()); ++i)
            if (lib.games()[std::size_t(i)].id.size() == 6)
            {
                gi = i;
                break;
            }
        ui.preview_game_settings(gi);
        settle();
        ui.preview_section("Cheats and Patches", 1);
        settle();
        render("code-cheats", [&] { ui.draw(12.0); });
        ui.preview_code("WiiLink WFC", "C200000000000000488C11C8FF00000000000000C20000000000000045460B3D12345678ABCD");
        settle();
        render("code-pad", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_SYSTEM=1: Settings > Console's menus and channels, and their lists; then stop. */
    if (std::getenv("PREVIEW_SYSTEM"))
    {
        settle();
        press(kR1, 2);
        settle();
        ui.preview_section("Console", 5);
        settle();
        render("system-console", [&] { ui.draw(12.0); });
        ui.preview_section("Console", 10);
        settle();
        render("system-console2", [&] { ui.draw(12.0); });
        for (int mode = 1; mode <= 4; ++mode)
        {
            ui.preview_system(mode, 1);
            settle();
            render(("system-" + std::to_string(mode)).c_str(), [&] { ui.draw(12.0); });
        }
        return 0;
    }
    /* PREVIEW_DOCK=1: Recently Played in each look, resting and chosen; then stop. */
    if (std::getenv("PREVIEW_DOCK"))
    {
        const long long now = (long long)std::time(nullptr);
        for (int i = 0; i < 6 && i < int(lib.games().size()); ++i)
            lib.games()[std::size_t(i)].last_played = now - (i + 1) * 3600LL * 7;
        for (int style = 1; style <= 3; ++style)
        {
            settings.recent_style = style;
            ui.preview_dock(-1);
            settle();
            render(("dock-" + std::to_string(style) + "-rest").c_str(), [&] { ui.draw(12.0); });
            ui.preview_dock(2);
            settle();
            render(("dock-" + std::to_string(style) + "-chosen").c_str(), [&] { ui.draw(12.0); });
        }
        return 0;
    }
    /* PREVIEW_ABOUT=1: Settings > About on the Discord row and on the
     * creator's row; then stop. */
    if (std::getenv("PREVIEW_ABOUT"))
    {
        settle();
        press(kR1, 2);
        settle();
        press(kDown, 9); /* About, the last section */
        press(kRight);
        press(kDown, 3); /* Discord */
        settle();
        render("about-discord", [&] { ui.draw(12.0); });
        press(kDown, 3); /* Created by */
        settle();
        render("about-creator", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_WELCOME=1: the first start's theme picker on each look, the
     * welcome after it and Settings > Interface's last rows; then stop. */
    if (std::getenv("PREVIEW_WELCOME"))
    {
        settings.ui_theme = 0;
        ui.language_changed();
        settle();
        ui.start_welcome();
        settle();
        render("welcome-0", [&] { ui.draw(12.0); });
        press(kRight);
        settle();
        while (gfx.theme_fonts_busy())
        {
            settle();
            usleep(10000);
        }
        settle();
        render("welcome-1", [&] { ui.draw(12.0); });
        press(kRight);
        settle();
        render("welcome-2", [&] { ui.draw(12.0); });
        press(1u << 2); /* left */
        press(1u << 2);
        settle();
        press(kCross);
        settle();
        render("welcome-done", [&] { ui.draw(12.0); });
        return 0;
    }
    /* PREVIEW_STARCUBE=palette: Star Cube's home turned to each edge, then
     * each page and a game's page; then stop. */
    if (const char *sc = std::getenv("PREVIEW_STARCUBE"))
    {
        settings.ui_theme = 14;
        settings.ui_palette = std::atoi(sc);
        if (const char *e = std::getenv("PREVIEW_SCGAMES"))
            settings.sc_games = std::atoi(e);
        if (const char *e = std::getenv("PREVIEW_SCLANG"))
        {
            settings.ui_language = std::atoi(e);
            porpoise::ui::apply_language(settings.ui_language);
        }
        ui.language_changed();
        settle();
        while (gfx.theme_fonts_busy() || gfx.cjk_busy())
        {
            settle();
            usleep(10000);
        }
        const char *names[4] = {"games", "calendar", "cards", "settings"};
        for (int face = 0; face < 4; ++face)
        {
            ui.preview_starcube(face, false);
            settle();
            render((std::string("sc-home-") + names[face]).c_str(), [&] { ui.draw(12.0); });
        }
        for (int face = 0; face < 4; ++face)
        {
            ui.preview_starcube(face, true);
            for (int wait = 0; wait < 40; ++wait)
            {
                settle();
                usleep(10000);
            }
            if (face == 3)
            {
                press(1u << 0, 12);
                press(kDown, 7); /* Interface */
                press(kRight);
                settle();
            }
            render((std::string("sc-page-") + names[face]).c_str(), [&] { ui.draw(12.0); });
        }
        ui.preview_starcube(0, true);
        settle();
        press(kRight);
        press(kCross);
        settle();
        render("sc-page-game", [&] { ui.draw(12.0); });
        /* The in-game menu in a few themes: it takes the theme's colours and font. */
        for (int theme : {14, 12, 11, 5})
        {
            settings.ui_theme = theme;
            settle();
            while (gfx.theme_fonts_busy())
            {
                settle();
                usleep(10000);
            }
            porpoise::Settings play = settings;
            ui.open_game_menu(&lib.games()[3], &play);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
            Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
            render(("sc-ingame-" + std::to_string(theme)).c_str(), [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
            Input c;
            c.held = kCircle;
            ui.update_game_menu(c, 0.016);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
        }
        return 0;
    }
    /* PREVIEW_SHOTS=1: the screenshot gallery and the viewer, with the covers
     * standing in for screenshots; and the library with Recently Played
     * focused. Then stop. */
    if (std::getenv("PREVIEW_SHOTS"))
    {
        const std::string dir = out + "/screenshots/PRVW01";
        mkdir((out + "/screenshots").c_str(), 0777);
        mkdir(dir.c_str(), 0777);
        mkdir((dir + "/.thumbs").c_str(), 0777);
        int n = 0;
        for (const char *name : {"PRVW02", "PRVW03", "PRVW04", "PRVW05", "PRVW06", "PRVW07", "PRVW08"})
        {
            const std::string from = covers + "/" + name + ".png", to = dir + "/2026-10-0" + std::to_string(++n) + "_12-00-00.png";
            std::FILE *in = std::fopen(from.c_str(), "rb");
            std::FILE *o = in ? std::fopen(to.c_str(), "wb") : nullptr;
            char buf[65536];
            std::size_t got;
            while (in && o && (got = std::fread(buf, 1, sizeof buf, in)) > 0)
                std::fwrite(buf, 1, got, o);
            if (in)
                std::fclose(in);
            if (o)
                std::fclose(o);
        }
        for (int view = 0; view < 2; ++view)
        {
            ui.preview_shots(view == 1);
            for (int i = 0; i < 30; ++i)
                settle();
            render(view ? "shots-view" : "shots", [&] { ui.draw(12.0); });
        }
        return 0;
    }
    /* PREVIEW_LOOKS=theme:colours:view[:memcards],...: the library (and, with
     * PREVIEW_PAGES=1, Settings > Interface and Accessibility and Memory
     * Cards) in each, then stop. */
    if (const char *looks = std::getenv("PREVIEW_LOOKS"); looks && *looks)
    {
        const bool pages = std::getenv("PREVIEW_PAGES") != nullptr;
        for (const char *at = looks; *at;)
        {
            int v[4] = {0, 0, 0, 0}, n = 0;
            while (*at && *at != ',')
            {
                if (*at >= '0' && *at <= '9' && n < 4)
                    v[n] = v[n] * 10 + (*at - '0');
                else if (*at == ':')
                    ++n;
                ++at;
            }
            if (*at == ',')
                ++at;
            settings.ui_theme = v[0];
            settings.ui_palette = v[1];
            settings.lib_view = v[2];
            settings.mc_view = v[3];
            if (const char *e = std::getenv("PREVIEW_FONT"))
                settings.ui_font = std::atoi(e);
            if (const char *e = std::getenv("PREVIEW_HC"))
                settings.high_contrast = std::atoi(e) != 0;
            if (const char *e = std::getenv("PREVIEW_FILTER"))
                settings.colour_filter = std::atoi(e);
            if (const char *e = std::getenv("PREVIEW_TEXT"))
                settings.text_size = std::atoi(e);
            ui.language_changed();
            settle();
            while (gfx.theme_fonts_busy() || gfx.cjk_busy()) /* made on workers */
            {
                settle();
                usleep(10000);
            }
            if (const char *e = std::getenv("PREVIEW_TURN")) /* the right stick held for this many frames */
                for (int f = 0, frames = std::atoi(e); f < frames; ++f)
                {
                    Input turn;
                    turn.right_x = 1.0f;
                    ui.update(turn, 0.016);
                }
            char tag[48];
            std::snprintf(tag, sizeof tag, "look-%d-%d-%d-%d", v[0], v[1], v[2], v[3]);
            render((std::string(tag) + "-library").c_str(), [&] { ui.draw(12.0); });
            if (!pages)
                continue;
            press(kR1, 2);
            press(1u << 0, 12);
            press(kDown, 7); /* Interface */
            press(kRight);
            settle();
            render((std::string(tag) + "-interface").c_str(), [&] { ui.draw(12.0); });
            press(kCircle);
            press(kDown); /* Accessibility */
            press(kRight);
            settle();
            render((std::string(tag) + "-access").c_str(), [&] { ui.draw(12.0); });
            press(kCircle);
            press(kR1, 2); /* to Memory Cards */
            for (int wait = 0; wait < 60; ++wait) /* the cards are read on a worker */
            {
                settle();
                usleep(20000);
            }
            render((std::string(tag) + "-cards").c_str(), [&] { ui.draw(12.0); });
            press(kR1, 2); /* back to the library */
            settle();
        }
        gfx.shutdown();
        return 0;
    }

    /* PREVIEW_LANGS=13,16: only these languages' library, a long message (line
     * breaking) and Settings > Interface, then stop. */
    if (const char *langs = std::getenv("PREVIEW_LANGS"); langs && *langs)
    {
        static const char *const tags[] = {"",    "en-",    "es-",   "fr-", "pt-", "it-",
                                           "ja-", "es419-", "ptbr-", "de-", "nl-", "pl-",
                                           "ru-", "zhs-",   "zht-",  "ko-", "tr-"};
        for (const char *at = langs; *at;)
        {
            const int lang = std::atoi(at);
            while (*at && *at != ',')
                ++at;
            if (*at == ',')
                ++at;
            if (lang < 1 || lang > 16)
                continue;
            const std::string tag = tags[lang];
            settings.ui_language = lang;
            porpoise::ui::apply_language(lang);
            ui.language_changed();
            settle();
            while (gfx.cjk_busy()) /* the language's CJK atlas, made on a worker */
                settle();
            if (const char *intro = std::getenv("PREVIEW_INTRO")) /* the menus' entrance, part way */
            {
                const float a = float(std::atof(intro));
                ui.set_intro(a, 28.0f * (1.0f - a));
                render((tag + "entrance").c_str(), [&] { ui.draw(12.0); });
                ui.set_intro(1, 0);
            }
            render((tag + "library").c_str(), [&] { ui.draw(12.0); });
            ui.show_message(
                porpoise::ui::tr("Porpoise can't reach /data"),
                porpoise::ui::tr(
                    "The console started Porpoise inside the app sandbox, so it can't see "
                    "/data or USB drives, and no jailbreak daemon freed it. A daemon that "
                    "frees Porpoise must be running before you open it:\n"
                    "\xE2\x80\xA2 etaHEN: turn on Legacy Command Server in etaHEN's Toolbox "
                    "settings (etaHEN's built-in app list can't be edited to add Porpoise).\n"
                    "\xE2\x80\xA2 OnionHEN: add PPSA99764 to exact_title_ids in "
                    "/data/OnionHEN/config.ini.\n"
                    "\xE2\x80\xA2 Or run a standalone daemon such as Lapy.\n"
                    "Then open Porpoise again; if a launch still lands here, try once more. "
                    "Until then, games go in /app0/porpoise/games."));
            settle();
            render((tag + "message").c_str(), [&] { ui.draw(12.0); });
            press(kCross);
            press(kR1, 2);
            press(1u << 0, 9);
            press(kDown, 7); /* Interface */
            press(kRight);
            settle();
            render((tag + "settings-interface").c_str(), [&] { ui.draw(12.0); });
            press(kCircle);
            press(kR1);
            settle();
        }
        gfx.shutdown();
        return 0;
    }

    if (async)
    {
        render("async-first", [&] { ui.draw(12.0); });
        for (int i = 0; i < 40; ++i)
        {
            usleep(15000);
            render("async-later", [&] { ui.draw(12.0); });
            ui.update(none, 0.016);
        }
    }
    press(kSquare);
    for (int i = 0; i < 9; ++i)
        ui.update(none, 0.016);
    render("details-flip", [&] { ui.draw(12.0); });
    settle();
    render("details", [&] { ui.draw(12.0); });
    press(kTriangle);
    settle();
    render("details-back", [&] { ui.draw(12.0); });
    {
        Input turn;
        turn.right_x = 0.6f;
        for (int i = 0; i < 60; ++i)
            ui.update(turn, 0.016);
        render("details-turn", [&] { ui.draw(12.0); });
    }
    press(kTriangle);
    settle();
    {
        /* R2: the next game, swiping in; L2 back again. */
        const std::uint32_t kL2 = 1u << 10, kR2 = 1u << 11;
        Input in;
        in.held = kR2;
        ui.update(in, 0.016);
        ui.update(none, 0.016);
        for (int i = 0; i < 7; ++i)
            ui.update(none, 0.016);
        render("details-swipe", [&] { ui.draw(12.0); });
        settle();
        render("details-next", [&] { ui.draw(12.0); });
        press(kL2);
        settle();
    }
    press(kDown);
    press(kCross); /* Save states */
    settle();
    render("details-states", [&] { ui.draw(12.0); });
    press(kCircle);
    settle();
    press(kDown);
    press(kCross); /* Game settings */
    press(kRight);
    settle();
    render("game-settings", [&] { ui.draw(12.0); });
    press(kCircle);   /* to the rail */
    press(kDown);     /* Recommended */
    press(kRight);
    settle();
    render("game-settings-recommended", [&] { ui.draw(12.0); });
    press(kDown);     /* the first pick */
    press(kCross);    /* off */
    settle();
    render("game-settings-recommended-toggled", [&] { ui.draw(12.0); });
    press(kCircle);
    press(1u << 0);   /* back up to This game */
    press(kCircle);
    press(kDown);  /* rail: Video */
    press(kRight);
    press(kRight); /* a change for this game */
    settle();
    render("game-settings-video", [&] { ui.draw(12.0); });
    press(kCircle);
    press(kCircle); /* back to details */
    {
        /* A game Dolphin lists codes for: the Cheats and patches section. */
        Game *g = nullptr;
        for (Game &x : lib.games())
            if (x.title == "Garden Critters")
                g = &x;
        if (g)
        {
            const std::string was = g->id;
            g->id = "GMSE01";
            press(kSquare); /* its details */
            settle();
            press(kDown, 2);
            settle();
            render("cheats-step0", [&] { ui.draw(12.0); });
            press(kCross); /* Game settings, again */
            settle();
            render("cheats-step1", [&] { ui.draw(12.0); });
            press(kDown, 12);
            press(kRight);
            settle();
            render("game-settings-cheats", [&] { ui.draw(12.0); });
            press(kDown, 2);
            press(kCross);
            settle();
            render("game-settings-cheats-toggled", [&] { ui.draw(12.0); });
            press(kCross);
            press(kCircle);
            press(kCircle);
            settle();
            g->id = was;
        }
    }
    press(kCircle); /* back to the library */
    settle();

    press(kTriangle);
    settle();
    render("sort", [&] { ui.draw(12.0); });
    press(kDown); /* Show */
    press(kRight); /* GameCube */
    settle();
    render("sort-gamecube", [&] { ui.draw(12.0); });
    press(kCircle);
    settle();
    render("library-gamecube", [&] { ui.draw(12.0); });
    press(kTriangle);
    press(kDown);
    press(kRight); /* Wii */
    press(kCircle);
    settle();
    render("library-wii", [&] { ui.draw(12.0); });
    press(kTriangle);
    press(kDown);
    press(kRight); /* All */
    press(kDown); /* Get covers and info now */
    settle();
    render("sort-covers", [&] { ui.draw(12.0); });
    press(kCircle);
    settle();

    /* The Revolution look: the home screen, pointed at and with the D-pad. */
    {
        settings.ui_theme = 1;
        settle();
        /* Wii banners play on worker threads: draw a while, in real time. */
        auto warm = [&](double seconds) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            while (std::chrono::steady_clock::now() < until)
            {
                gfx.begin(0, float(W), float(H), 12.0f, 0.0f, false);
                ui.update(none, 0.016);
                ui.draw(12.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        };
        warm(2.5);
        ui.preview_pointer(1010, 360);
        settle();
        render("home-pointer", [&] { ui.draw(12.0); });
        ui.preview_pointer(1300, 1010); /* over nothing: the open hand */
        settle();
        render("home-pointer-open", [&] { ui.draw(12.0); });
        ui.preview_pointer(-1, -1);
        press(kDown);
        settle();
        render("home-dpad", [&] { ui.draw(12.0); });
        ui.preview_pointer(156, 1000);
        settle();
        render("home-settings-button", [&] { ui.draw(12.0); });
        ui.preview_pointer(-1, -1);
        ui.preview_home_page(1);
        settle();
        render("home-page-2", [&] { ui.draw(12.0); });
        press(kTriangle);
        settle();
        render("home-sort", [&] { ui.draw(12.0); });
        press(kCircle);
        ui.preview_home_page(0);
        press(1u << 0, 3);
        press(1u << 2, 3);
        press(kRight, 1); /* a Wii game */
        settle();
        press(kCross); /* its tile opens: it grows to fill the screen */
        for (int i = 0; i < 6; ++i)
            ui.update(none, 0.016);
        render("rev-zoom-1", [&] { ui.draw(12.0); });
        for (int i = 0; i < 8; ++i)
            ui.update(none, 0.016);
        render("rev-zoom-2", [&] { ui.draw(12.0); });
        for (int i = 0; i < 8; ++i)
            ui.update(none, 0.016);
        render("rev-zoom-3", [&] { ui.draw(12.0); });
        warm(3.0);
        render("rev-details-wii", [&] { ui.draw(12.0); });
        press(1u << 2); /* Wii controls, to the left of Start */
        settle();
        render("rev-details-controls", [&] { ui.draw(12.0); });
        press(kCross); /* the setup for this game */
        settle();
        render("wii-setup-game", [&] { ui.draw(12.0); });
        press(kDown, 2); /* Before this game */
        settle();
        render("wii-setup-game-ask", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kDown); /* the chips */
        settle();
        render("rev-details-chips", [&] { ui.draw(12.0); });
        ui.preview_pointer(1858, 466);
        settle();
        render("rev-details-pointer", [&] { ui.draw(12.0); });
        ui.preview_pointer(-1, -1);
        press(kCircle);
        settle();
        press(1u << 0, 3);
        press(1u << 2, 3);
        press(kRight, 3); /* a GameCube game */
        press(kCross);
        settle();
        render("rev-details-gc", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        press(kR1); /* Memory Cards, in white */
        settle();
        render("rev-memory-cards", [&] { ui.draw(12.0); });
        press(1u << 10); /* L2: the Wii saves */
        settle();
        press(kRight);
        settle();
        render("rev-wii-saves", [&] { ui.draw(12.0); });
        settings.ui_theme = 0;
        settle();
        render("wii-saves", [&] { ui.draw(12.0); });
        settings.ui_theme = 1;
        press(1u << 10);
        settle();
        press(kR1); /* Settings */
        settle();
        press(kDown, 3);
        press(kRight);
        settle();
        render("rev-settings", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kR1); /* back to the tiles */
        settle();
        {
            porpoise::Settings play = settings;
            play.ui_theme = 1;
            ui.open_game_menu(&lib.games()[3], &play);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
            Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
            render("rev-ingame-menu", [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
            Input c;
            c.held = kCircle;
            ui.update_game_menu(c, 0.016);
            for (int i = 0; i < 40; ++i)
                ui.update_game_menu(none, 0.016);
        }
        settings.ui_layout = 1;
        settle();
        render("rev-coverflow", [&] { ui.draw(12.0); });
        settings.ui_layout = 0;
        settle();
        ui.preview_pointer(1100, 470);
        settle();
        render("home-pointer-tile", [&] { ui.draw(12.0); });
        ui.preview_pointer(-1, -1);
        settings.ui_theme = 0;
        settle();
        /* Check my setup, as at the first start. */
        ui.show_setup_check(true);
        settle();
        render("setup-check", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        /* Quick resume: a saved spot for every game, then Play asks first. */
        settings.quick_resume = true;
        for (const Game &gm : lib.games())
        {
            const std::string resume = porpoise::states::resume_path(Library::key_of(gm));
            const std::string dir = resume.substr(0, resume.rfind('/'));
            mkdir((out + "/states").c_str(), 0777);
            mkdir(dir.c_str(), 0777);
            if (std::FILE *f = std::fopen(resume.c_str(), "wb"))
            {
                std::fputs("state", f);
                std::fclose(f);
            }
            std::FILE *in = std::fopen((covers + "/PRVW07.png").c_str(), "rb");
            std::FILE *png = std::fopen((resume.substr(0, resume.size() - 6) + ".png").c_str(), "wb");
            if (in && png)
            {
                char buf[4096];
                std::size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
                    std::fwrite(buf, 1, n, png);
            }
            if (in)
                std::fclose(in);
            if (png)
                std::fclose(png);
        }
        press(kCross);
        settle();
        render("resume-ask", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        settings.quick_resume = false;
        /* Developer options (Cross three times on the creator's name, in About). */
        settings.developer = true;
        ui.language_changed();
        press(kR1, 2);
        press(1u << 0, 12);
        press(kDown, 7);
        press(kRight);
        settle();
        render("settings-developer", [&] { ui.draw(12.0); });
        press(kCircle);
        /* Every section, top and bottom, for a look at the new rows. */
        for (int sec = 0; sec < 12; ++sec)
        {
            press(1u << 0, 12);
            press(kDown, sec);
            press(kRight);
            settle();
            const std::string top = "sec-" + std::to_string(sec);
            render(top.c_str(), [&] { ui.draw(12.0); });
            press(kDown, 30);
            settle();
            const std::string end = top + "-end";
            render(end.c_str(), [&] { ui.draw(12.0); });
            press(1u << 0, sec == 9 ? 24 : 9);
            if (sec == 9)
            {
                ui.set_versions({"v2.0-beta.1", "v1.5.1", "v1.5", "v1.1"}, {0, 0, 0, 0}, {true, false, false, false},
                                {44u << 20, 42u << 20, 42u << 20, 40u << 20});
                press(kDown, 7);
            }
            settle();
            const std::string mid = top + "-mid";
            render(mid.c_str(), [&] { ui.draw(12.0); });
            press(kCircle);
        }
        press(kCircle);
        settings.developer = false;
        ui.language_changed();
        settle();
    }
    if (const char *only = std::getenv("PREVIEW_ONLY"); only && *only) /* just the scenes above */
    {
        gfx.shutdown();
        return 0;
    }

    press(kR1);
    settle();
    render("memory-cards", [&] { ui.draw(12.0); }); /* scans the cards */
    press(kRight);
    settle();
    render("memory-cards", [&] { ui.draw(12.0); });
    press(kRight, 4);
    settle();
    render("memory-cards-b", [&] { ui.draw(12.0); });
    press(kTriangle);
    settle();
    render("memcard-delete", [&] { ui.draw(12.0); });
    press(kCircle);

    press(kR1);
    settle();
    render("settings", [&] { ui.draw(12.0); });
    {
        /* mid-slide, for the motion */
        Input in;
        in.held = 1u << 8; /* L1 */
        ui.update(in, 0.016);
        ui.update(none, 0.016);
        for (int i = 0; i < 8; ++i)
            ui.update(none, 0.016);
        render("tab-slide", [&] { ui.draw(12.0); });
        settle();
        press(kR1);
        settle();
    }
    press(kRight);
    press(kDown, 3); /* Add a game folder */
    settle();
    render("settings-games", [&] { ui.draw(12.0); });
    press(kCross); /* the browser: the whole console, on /data */
    settle();
    render("browser-root", [&] { ui.draw(12.0); });
    press(kCross); /* /data */
    settle();
    render("browser", [&] { ui.draw(12.0); });
    press(kDown); /* games */
    press(kCross);
    settle();
    render("browser-games", [&] { ui.draw(12.0); });
    press(kCross); /* GameCube */
    press(kDown);
    settle();
    render("browser-files", [&] { ui.draw(12.0); });
    press(kCross); /* copy this one? */
    settle();
    render("browser-copy", [&] { ui.draw(12.0); });
    press(kCircle);
    press(kTriangle); /* drives and shortcuts */
    settle();
    render("browser-drives", [&] { ui.draw(12.0); });
    press(kTriangle); /* back to the whole console */
    press(kCircle);   /* out */
    settle();
    press(kCircle); /* to the rail */
    press(kDown);   /* Video */
    settle();
    render("settings-video", [&] { ui.draw(12.0); });
    press(kDown, 5); /* Interface */
    press(kRight);
    press(kDown, 3); /* Reset all settings */
    press(kCross);
    settle();
    render("dialog", [&] { ui.draw(12.0); });
    press(kCircle);
    press(kCircle);
    press(kDown); /* About */
    press(kRight);
    press(kDown, 2);
    settle();
    render("settings-about", [&] { ui.draw(12.0); });
    /* A newer release: the About row offers it, then the update runs. */
    ui.set_latest_release("v1.2", "https://github.com/elripalda/Porpoise/releases/tag/v1.2", 41u << 20);
    press(1u << 0, 1); /* up to the update row */
    settle();
    render("settings-about-update", [&] { ui.draw(12.0); });
    press(kCross);
    settle();
    render("update-confirm", [&] { ui.draw(12.0); });
    press(kRight);
    press(kCross); /* Install */
    ui.set_update_progress(2, 18u << 20, 41u << 20, "");
    settle();
    render("update-downloading", [&] { ui.draw(12.0); });
    ui.set_update_progress(3, 1630, 2709, "");
    render("update-installing", [&] { ui.draw(12.0); });
    ui.set_update_progress(5, 0, 0, "The download doesn't match its SHA-256. Nothing was changed.");
    settle();
    render("update-failed", [&] { ui.draw(12.0); });
    press(kCross);
    ui.set_update_progress(0, 0, 0, "");
    ui.set_update_progress(4, 2709, 2709, "");
    render("update-done", [&] { ui.draw(12.0); });
    ui.set_update_progress(0, 0, 0, "");
    ui.set_latest_release("v1.1", "", 0);
    settle();
    press(kCircle);
    press(kCircle); /* back to the library */
    settle();

    /* Controls, and the button-mapping screen. */
    {
        const std::uint32_t kUp = 1u << 0;
        press(kR1, 2);    /* Settings, from the library */
        press(kUp, 8);    /* the top of the rail */
        press(kDown, 4);  /* Controls */
        press(kRight);
        settle();
        render("settings-controls", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kDown); /* Wii Remote */
        press(kRight);
        settle();
        render("settings-wii-remote", [&] { ui.draw(12.0); });
        press(kCross); /* Wii Remote setup */
        settle();
        press(kSquare); /* Advanced */
        for (int i = 0; i < 8; ++i)
        {
            ui.preview_setup_step(i);
            const std::string shot_name = "wii-setup-" + std::to_string(i);
            render(shot_name.c_str(), [&] { ui.draw(12.0); });
        }
        ui.preview_setup_step(0);
        press(kCircle);
        settle();
        press(kDown, 2); /* How to hold it */
        press(kCross);
        settle();
        for (int i = 0; i < 5; ++i)
        {
            const std::string shot_name = "wii-guide-" + std::to_string(i);
            render(shot_name.c_str(), [&] { ui.draw(12.0); });
            press(kRight);
            settle();
        }
        press(kCircle);
        settle();
        press(kCircle);
        press(1u << 0); /* back to Controls */
        press(kRight);
        settle();
        press(kDown);     /* Customize buttons */
        press(kCross);
        settle();
        render("mapping", [&] { ui.draw(12.0); });
        press(kDown, 2);  /* B */
        press(kCross);    /* waiting for a button */
        for (int i = 0; i < 20; ++i)
            ui.update(none, 0.016);
        render("mapping-capture", [&] { ui.draw(12.0); });
        press(kCircle);   /* B on Circle */
        settle();
        render("mapping-custom", [&] { ui.draw(12.0); });
        press(kDown, 12); /* Start over from the PlayStation layout */
        press(kCross);
        settle();
        render("mapping-playstation", [&] { ui.draw(12.0); });
        press(kR1); /* the Wii Remote + Nunchuk's buttons */
        settle();
        render("mapping-wii", [&] { ui.draw(12.0); });
        press(kDown, 2);  /* the 1 button */
        press(kCross);
        for (int i = 0; i < 20; ++i)
            ui.update(none, 0.016);
        press(kSquare);   /* 1 on Square */
        settle();
        render("mapping-wii-custom", [&] { ui.draw(12.0); });
        press(kR1, 3);    /* the Classic Controller */
        settle();
        render("mapping-classic", [&] { ui.draw(12.0); });
        press(kCircle);
        settle();
        render("settings-controls-after", [&] { ui.draw(12.0); });
        press(kCircle);
        press(kCircle); /* back to the library */
        settle();
    }

    /* The in-game menu, over a stand-in for the game's picture. */
    {
        porpoise::Settings play = settings;
        ui.open_game_menu(&lib.games()[3], &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        Input dn;
        dn.held = 1u << 1;
        ui.update_game_menu(dn, 0.016);
        ui.update_game_menu(none, 0.016);
        for (int i = 0; i < 20; ++i)
            ui.update_game_menu(none, 0.016);
        Texture *frame = gfx.texture_file(covers + "/PRVW07.png");
        auto shot = [&](const char *name) {
            for (int i = 0; i < 20; ++i)
                ui.update_game_menu(none, 0.016);
            render(name, [&] {
                gfx.background();
                if (frame)
                    gfx.image(frame, 0, 0, 1920, 1080, rgba(0xFFFFFF));
                ui.draw_game_menu(12.0);
            });
        };
        auto menu_press = [&](std::uint32_t bit, int times = 1) {
            for (int i = 0; i < times; ++i)
            {
                Input in;
                in.held = bit;
                ui.update_game_menu(in, 0.016);
                ui.update_game_menu(none, 0.016);
            }
        };
        shot("ingame-menu");
        menu_press(1u << 4);    /* Save state...: the slot picker */
        shot("ingame-save-pick");
        menu_press(1u << 4);    /* slot 1 has a state: asks first */
        shot("ingame-save-replace");
        menu_press(1u << 5);    /* back */
        menu_press(1u << 1);    /* Load state... */
        menu_press(1u << 4);
        shot("ingame-menu-load");
        menu_press(1u << 4);    /* load it: busy until the host answers */
        shot("ingame-loading");
        ui.take_menu_request();
        ui.menu_state_done(porpoise::ui::App::MenuRequest::Load, 0, false);
        menu_press(1u << 5);
        menu_press(1u << 0, 2); /* back to Resume */
        menu_press(1u << 9); /* R1: Video */
        menu_press(1u << 1, 5); /* Screen filter */
        menu_press(1u << 3, 3); /* CRT */
        shot("ingame-video");
        menu_press(1u << 9); /* Graphics */
        menu_press(1u << 1, 9); /* Use a setup */
        shot("ingame-graphics");
        menu_press(1u << 0, 9);
        menu_press(1u << 9); /* Controls */
        shot("ingame-controls");
        {
            /* The same tab for a Wii game: the Wii Remote's rows. */
            const std::string was = lib.games()[3].platform;
            lib.games()[3].platform = "Wii";
            for (int i = 0; i < 4; ++i)
                menu_press(1u << 1);
            shot("ingame-controls-wii");
            /* Row 3 Wii controller, row 6 Grip: the drawing for each way of holding it. */
            menu_press(1u << 0);    /* Wii controller */
            menu_press(1u << 3);    /* Remote */
            menu_press(1u << 1, 3); /* Grip */
            shot("wii-remote-both-hands");
            menu_press(1u << 3);
            shot("wii-remote-upright-right");
            menu_press(1u << 3);
            shot("wii-remote-upright-left");
            menu_press(1u << 3);    /* back to both hands */
            menu_press(1u << 0, 3);
            menu_press(1u << 3, 3); /* Two controllers */
            shot("wii-two-controllers");
            menu_press(1u << 3, 1); /* Remote + Nunchuk */
            for (int i = 0; i < 10; ++i)
                menu_press(1u << 1);
            shot("ingame-controls-wii-2");
            for (int i = 0; i < 14; ++i)
                menu_press(1u << 0);
            lib.games()[3].platform = was;
        }
        menu_press(1u << 1);
        menu_press(1u << 4); /* Customize buttons, over the game */
        shot("ingame-mapping");
        menu_press(1u << 5);
    }

    /* Spanish. */
    porpoise::ui::apply_language(2);
    ui.language_changed();
    press(kCircle, 3);
    settle();
    render("es-library", [&] { ui.draw(12.0); });
    press(kSquare);
    settle();
    render("es-details", [&] { ui.draw(12.0); });
    press(kCircle);
    press(kR1);
    settle();
    render("es-memory-cards", [&] { ui.draw(12.0); });
    press(kR1);
    press(kDown); /* Video */
    settle();
    render("es-settings", [&] { ui.draw(12.0); });
    {
        porpoise::Settings play = settings;
        ui.open_game_menu(&lib.games()[3], &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        render("es-ingame-menu", [&] {
            gfx.background();
            ui.draw_game_menu(12.0);
        });
    }
    /* The other languages: Japanese, Italian, German, Russian, Polish, Latin
     * American Spanish, Brazilian Portuguese, Dutch. */
    for (int lang : {6, 5, 9, 12, 11, 7, 8, 10})
    {
        static const char *const tags[] = {"",    "en-", "es-", "fr-",  "pt-",  "it-", "ja-", "es419-", "ptbr-",
                                           "de-", "nl-", "pl-", "ru-", "zhs-", "zht-", "ko-", "tr-"};
        const std::string tag = tags[lang];
        settings.ui_language = lang;
        porpoise::ui::apply_language(lang);
        ui.language_changed();
        press(kCircle, 3);
        settle();
        while (gfx.cjk_busy())
            settle();
        render((tag + "library").c_str(), [&] { ui.draw(12.0); });
        press(kSquare);
        settle();
        render((tag + "details").c_str(), [&] { ui.draw(12.0); });
        press(kCircle);
        press(kR1, 2);
        press(1u << 0, 9);
        press(kDown, 6); /* Interface */
        press(kRight);
        settle();
        render((tag + "settings-interface").c_str(), [&] { ui.draw(12.0); });
        press(kCircle);
        press(kR1);
        settle();
        porpoise::Settings play = settings;
        ui.open_game_menu(&lib.games()[3], &play);
        for (int i = 0; i < 40; ++i)
            ui.update_game_menu(none, 0.016);
        render((tag + "ingame-menu").c_str(), [&] {
            gfx.background();
            ui.draw_game_menu(12.0);
        });
    }
    settings.ui_language = 0;
    porpoise::ui::apply_language(1);
    ui.language_changed();

    ui.begin_launch(&lib.games()[3]);
    ui.set_launch_status("Preparing graphics", -1);
    render("launch", [&] { ui.draw_launch(12.4); });
    ui.begin_launch(&lib.games()[8]);
    ui.set_launch_status("Reading disc", -1);
    render("launch-nocover", [&] { ui.draw_launch(12.4); });

    Library empty;
    LibraryPaths ep;
    ep.state = out + "/empty-state.txt";
    empty.scan(ep);
    App ui2;
    ui2.init(&gfx, &empty, &settings, out + "/settings.ini", out + "/options.ini", out);
    render("empty", [&] { ui2.draw(12.0); });
    gfx.shutdown();
    return 0;
}
