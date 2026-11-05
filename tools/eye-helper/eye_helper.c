/* Porpoise - the PS3 Eye helper (Move test builds only).
 *
 * A PS5 app can't reach the PS3 Eye (libSceUsbd isn't offered to apps and the
 * camera's /dev/ugen node is hidden from them), but a payload can. Porpoise
 * hands this to the jailbreak's ELF loader (127.0.0.1:9021); it streams the
 * camera at 640x480, 30 fps, finds the Move's glowing ball in each frame and
 * sends Porpoise its position over TCP: the helper listens on 127.0.0.1:47710
 * (an app may not listen, but it may connect) and stops when Porpoise closes
 * the connection, after 10 s with no connection, or after 10 minutes.
 *
 * Porpoise tells it the ball's color ('a'..'h': the Move's color in Porpoise's
 * list), and asks it to note the room's other lights ('I', sent while the Move's
 * light is off) or forget them ('X'); 'S' stops it. The ball is looked for in its
 * color only, outside the noted lights, which are kept in
 * /data/porpoise-eye-ignore.txt. The color check and the ignored lights follow
 * REXCRAFT88's Move Studio work (github.com/REXCRAFT88/Porpoise-Dolphin-Emulator-
 * for-PS5, branch feature/move-nav-integration).
 *
 * The camera's start-up tables are PS3EYEDriver's (MIT; after Linux's gspca
 * ov534 driver).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#define PORT 47710
#define MAGIC 0x45594550u /* "PEYE" */

static int g_conn = -1;

typedef struct
{
    uint32_t magic, type; /* 1: ball, 2: a text line, 3: done, 4: a picture (160 x 120 RGB follows) */
    uint32_t frame;
    int16_t found, x, y, radius;
    uint16_t count;
    uint8_t r, g, b, pad;
    char text[200];
} Packet;

/* Every packet is sent whole (sizeof Packet), so Porpoise reads them back in fixed steps. */
static bool g_lost;
static void send_packet(const Packet *p, size_t n)
{
    (void)n;
    const char *b = (const char *)p;
    for (size_t sent = 0; !g_lost && sent < sizeof *p;)
    {
        const ssize_t k = send(g_conn, b + sent, sizeof *p - sent, 0);
        if (k <= 0)
            g_lost = true;
        else
            sent += (size_t)k;
    }
}

/* Raw bytes after a packet (a picture's). */
static void send_raw(const void *d, size_t n)
{
    const char *b = (const char *)d;
    for (size_t sent = 0; !g_lost && sent < n;)
    {
        const ssize_t k = send(g_conn, b + sent, n - sent, 0);
        if (k <= 0)
            g_lost = true;
        else
            sent += (size_t)k;
    }
}

/* Porpoise's setup screens are up: they show what the camera sees, small. */
static bool g_video;

static void say(const char *fmt, ...)
{
    Packet p;
    memset(&p, 0, sizeof p);
    p.magic = MAGIC;
    p.type = 2;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p.text, sizeof p.text, fmt, ap);
    va_end(ap);
    send_packet(&p, sizeof p);
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* The ball's color (Porpoise's list: magenta, blue, red, green, cyan, yellow,
 * orange, violet) as the hue looked for, in degrees. */
static const int k_hue[8] = {300, 230, 0, 120, 185, 50, 22, 265};
static int g_color = 0;
/* The room's other lights: boxes on the half-size picture where nothing is taken
 * for the ball. g_learn: note the lights in the next frame. */
typedef struct
{
    int x0, y0, x1, y1;
} Box;
enum
{
    kMaxIgnore = 16
};
static Box g_ignore[kMaxIgnore];
static int g_nignore = 0;
static bool g_learn = false;
#define IGNORE_FILE "/data/porpoise-eye-ignore.txt"

static void save_ignore(void)
{
    FILE *f = fopen(IGNORE_FILE, "w");
    if (!f)
        return;
    for (int i = 0; i < g_nignore; ++i)
        fprintf(f, "%d %d %d %d\n", g_ignore[i].x0, g_ignore[i].y0, g_ignore[i].x1, g_ignore[i].y1);
    fclose(f);
}

static void load_ignore(void)
{
    g_nignore = 0;
    FILE *f = fopen(IGNORE_FILE, "r");
    if (!f)
        return;
    Box b;
    while (g_nignore < kMaxIgnore && fscanf(f, "%d %d %d %d", &b.x0, &b.y0, &b.x1, &b.y1) == 4)
        g_ignore[g_nignore++] = b;
    fclose(f);
}

static void say(const char *fmt, ...);

/* True while Porpoise keeps the connection open (and hasn't asked to stop); reads
 * what it asks for on the way. */
static bool porpoise_there(void)
{
    if (g_lost)
        return false;
    char b[16];
    const ssize_t k = recv(g_conn, b, sizeof b, MSG_DONTWAIT);
    if (k == 0)
        return false; /* closed */
    for (ssize_t i = 0; i < k; ++i)
    {
        if (b[i] == 'S')
            return false; /* stop */
        if (b[i] >= 'a' && b[i] <= 'h' && g_color != b[i] - 'a')
        {
            g_color = b[i] - 'a';
            say("helper: looking for color %d (hue %d)", g_color, k_hue[g_color]);
        }
        else if (b[i] == 'I')
            g_learn = true;
        else if (b[i] == 'V')
            g_video = true;
        else if (b[i] == 'v')
            g_video = false;
        else if (b[i] == 'X')
        {
            g_nignore = 0;
            save_ignore();
            say("helper: ignoring 0 lights");
        }
    }
    return true;
}

typedef struct
{
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} DevDesc;

static int (*uInit)(void);
static void (*uExit)(void);
static long (*uList)(void ***);
static void (*uFreeList)(void **, int);
static int (*uDesc)(void *, DevDesc *);
static int (*uOpen)(void *, void **);
static void (*uClose)(void *);
static int (*uCtrl)(void *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *, uint16_t, unsigned int);
static int (*uClaim)(void *, int);
static int (*uRelease)(void *, int);
static int (*uBulk)(void *, unsigned char, unsigned char *, int, int *, unsigned int);
static int (*uClearHalt)(void *, unsigned char);

static int ov_read(void *h, uint16_t reg, uint8_t *val)
{
    return uCtrl(h, 0xC0, 0x01, 0x0000, reg, val, 1, 500);
}

static int ov_write(void *h, uint16_t reg, uint8_t val)
{
    return uCtrl(h, 0x40, 0x01, 0x0000, reg, &val, 1, 500);
}

/* The sensor behind the OV534 bridge, read over its SCCB bus as Linux's ov534 driver does. */
static int sccb_wait(void *h)
{
    for (int i = 0; i < 5; ++i)
    {
        uint8_t s = 0xff;
        if (ov_read(h, 0xf6, &s) < 0)
            return -1;
        if (s == 0x00)
            return 0;
        if (s == 0x04)
            return -2;
        usleep(10000);
    }
    return -3;
}


static const uint8_t k_bridge_init[][2] = {
    {0xe7, 0x3a}, {0xf1, 0x42}, {0x92, 0x01}, {0x93, 0x18}, {0x94, 0x10}, {0x95, 0x10}, {0xE2, 0x00},
    {0xE7, 0x3E}, {0x96, 0x00}, {0x97, 0x20}, {0x97, 0x20}, {0x97, 0x20}, {0x97, 0x0A}, {0x97, 0x3F},
    {0x97, 0x4A}, {0x97, 0x20}, {0x97, 0x15}, {0x97, 0x0B}, {0x8E, 0x40}, {0x1F, 0x81}, {0xC0, 0x50},
    {0xC1, 0x3C}, {0xC2, 0x01}, {0xC3, 0x01}, {0x50, 0x89}, {0x88, 0x08}, {0x8D, 0x00}, {0x8E, 0x00},
    {0x1C, 0x00}, {0x1D, 0x00}, {0x1D, 0x02}, {0x1D, 0x00}, {0x1D, 0x01}, {0x1D, 0x2C}, {0x1D, 0x00},
    {0x1C, 0x0A}, {0x1D, 0x08}, {0x1D, 0x0E}, {0x34, 0x05}, {0xE3, 0x04}, {0x89, 0x00}, {0x76, 0x00},
    {0xE7, 0x2E}, {0x31, 0xF9}, {0x25, 0x42}, {0x21, 0xF0}, {0xE5, 0x04}};
static const uint8_t k_sensor_init[][2] = {
    {0x12, 0x80}, {0x3D, 0x00}, {0x12, 0x01}, {0x11, 0x01}, {0x14, 0x40}, {0x15, 0x00}, {0x63, 0xAA},
    {0x64, 0x87}, {0x66, 0x00}, {0x67, 0x02}, {0x17, 0x26}, {0x18, 0xA0}, {0x19, 0x07}, {0x1A, 0xF0},
    {0x29, 0xA0}, {0x2A, 0x00}, {0x2C, 0xF0}, {0x20, 0x10}, {0x4E, 0x0F}, {0x3E, 0xF3}, {0x0D, 0x41},
    {0x32, 0x00}, {0x13, 0xF0}, {0x22, 0x7F}, {0x23, 0x03}, {0x24, 0x40}, {0x25, 0x30}, {0x26, 0xA1},
    {0x2A, 0x00}, {0x2B, 0x00}, {0x13, 0xF7}, {0x0C, 0xC0}, {0x11, 0x00}, {0x0D, 0x41}, {0x8E, 0x00}};
static const uint8_t k_bridge_vga[][2] = {{0x1c, 0x00}, {0x1d, 0x00}, {0x1d, 0x02}, {0x1d, 0x00}, {0x1d, 0x01},
                                          {0x1d, 0x2C}, {0x1d, 0x00}, {0xc0, 0x50}, {0xc1, 0x3c}};
static const uint8_t k_sensor_vga[][2] = {{0x12, 0x01}, {0x17, 0x26}, {0x18, 0xa0}, {0x19, 0x07},
                                          {0x1a, 0xf0}, {0x29, 0xa0}, {0x2c, 0xf0}, {0x65, 0x20}};

static int sccb_write(void *h, uint8_t reg, uint8_t val)
{
    ov_write(h, 0xf2, reg);
    ov_write(h, 0xf3, val);
    ov_write(h, 0xf5, 0x37);
    return sccb_wait(h);
}

static void eye_led(void *h, int on)
{
    uint8_t d = 0;
    ov_read(h, 0x21, &d);
    ov_write(h, 0x21, d | 0x80);
    ov_read(h, 0x23, &d);
    ov_write(h, 0x23, on ? (d | 0x80) : (d & ~0x80));
    if (!on)
    {
        ov_read(h, 0x21, &d);
        ov_write(h, 0x21, d & ~0x80);
    }
}

enum
{
    kW = 640,
    kH = 480,
    kFrame = kW * kH
};

typedef struct
{
    uint8_t *frame; /* the frame being filled */
    uint8_t *done;  /* the last whole frame */
    int len, collecting, frames, bad;
    uint32_t last_pts;
    int last_fid;
} Assembler;

static void asm_payload(Assembler *a, const uint8_t *p, int n)
{
    if (n < 12 || p[0] != 12 || (p[1] & 0x40) || !(p[1] & 0x04))
    {
        a->collecting = 0;
        ++a->bad;
        return;
    }
    const uint32_t pts = (uint32_t)p[5] << 24 | (uint32_t)p[4] << 16 | (uint32_t)p[3] << 8 | p[2];
    const int fid = p[1] & 1;
    const uint8_t *d = p + 12;
    const int dn = n - 12;
    if (pts != a->last_pts || fid != a->last_fid)
    {
        a->last_pts = pts;
        a->last_fid = fid;
        a->len = 0;
        a->collecting = 1;
    }
    if (!a->collecting)
        return;
    if (a->len + dn > kFrame)
    {
        a->collecting = 0;
        ++a->bad;
        return;
    }
    memcpy(a->frame + a->len, d, dn);
    a->len += dn;
    if (p[1] & 0x02) /* end of frame */
    {
        a->last_pts = 0;
        if (a->len == kFrame)
        {
            memcpy(a->done, a->frame, kFrame);
            ++a->frames;
        }
        else
            ++a->bad;
        a->collecting = 0;
        a->len = 0;
    }
}

/* The glowing ball: on the half-size colour image (one pixel per 2x2 Bayer block),
 * the pixels both bright and strongly coloured, grouped into connected spots;
 * the biggest spot is the ball (a lamp or a window stays out: it isn't one
 * strongly coloured spot). Its size comes from its area. */
typedef struct
{
    int found, x, y, radius, count;
    int r, g, b;
} Ball;

enum
{
    kHW = kW / 2,
    kHH = kH / 2
};
static uint8_t g_lit[kHW * kHH];
static int32_t g_queue[kHW * kHH];

/* A color's hue, 0..359 degrees. */
static int hue_of(int r, int g, int b, int mx, int mn)
{
    const int d = mx - mn;
    if (d <= 0)
        return 0;
    int h;
    if (mx == r)
        h = 60 * (g - b) / d;
    else if (mx == g)
        h = 120 + 60 * (b - r) / d;
    else
        h = 240 + 60 * (r - g) / d;
    return (h + 360) % 360;
}

static bool hue_near(int h, int want)
{
    int d = h - want;
    d = d < 0 ? -d : d;
    if (d > 180)
        d = 360 - d;
    return d <= 45;
}

static bool ignored(int x, int y)
{
    for (int i = 0; i < g_nignore; ++i)
        if (x >= g_ignore[i].x0 && x <= g_ignore[i].x1 && y >= g_ignore[i].y0 && y <= g_ignore[i].y1)
            return true;
    return false;
}

/* learn: every bright, strongly colored spot (any color) becomes a place to leave
 * out; otherwise the ball, in its color, outside those places. */
static Ball find_ball(const uint8_t *bayer, bool learn)
{
    Ball best = {0};
    for (int y = 0; y < kHH; ++y)
        for (int x = 0; x < kHW; ++x)
        {
            const int by = y * 2, bx = x * 2;
            const int g1 = bayer[by * kW + bx], r = bayer[by * kW + bx + 1];
            const int b = bayer[(by + 1) * kW + bx], g2 = bayer[(by + 1) * kW + bx + 1];
            const int g = (g1 + g2) / 2;
            const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            bool lit = mx >= 120 && mx - mn >= 60;
            if (lit && !learn)
                lit = hue_near(hue_of(r, g, b, mx, mn), k_hue[g_color]) && !ignored(x, y);
            g_lit[y * kHW + x] = lit;
        }
    int noted = 0;
    if (learn)
        g_nignore = 0;
    for (int start = 0; start < kHW * kHH; ++start)
    {
        if (g_lit[start] != 1)
            continue;
        int head = 0, tail = 0;
        g_queue[tail++] = start;
        g_lit[start] = 2;
        long sx = 0, sy = 0;
        int x0 = kHW, y0 = kHH, x1 = 0, y1 = 0;
        while (head < tail)
        {
            const int i = g_queue[head++];
            const int x = i % kHW, y = i / kHW;
            sx += x;
            sy += y;
            x0 = x < x0 ? x : x0;
            y0 = y < y0 ? y : y0;
            x1 = x > x1 ? x : x1;
            y1 = y > y1 ? y : y1;
            const int next[4] = {x > 0 ? i - 1 : -1, x < kHW - 1 ? i + 1 : -1, y > 0 ? i - kHW : -1,
                                 y < kHH - 1 ? i + kHW : -1};
            for (int k = 0; k < 4; ++k)
                if (next[k] >= 0 && g_lit[next[k]] == 1)
                {
                    g_lit[next[k]] = 2;
                    g_queue[tail++] = next[k];
                }
        }
        if (learn)
        {
            /* A light to leave out, with a little room round it. */
            if (tail >= 3 && g_nignore < kMaxIgnore)
            {
                Box box = {x0 - 4, y0 - 4, x1 + 4, y1 + 4};
                g_ignore[g_nignore++] = box;
                ++noted;
            }
            continue;
        }
        if (tail > best.count && tail >= 6)
        {
            best.found = 1;
            best.count = tail;
            best.x = (int)(sx * 2 / tail) + 1;
            best.y = (int)(sy * 2 / tail) + 1;
            best.radius = (int)(sqrt(tail / 3.14159) * 2 + 0.5);
            /* the colour at its centre */
            const int cx = best.x & ~1, cy = best.y & ~1;
            best.r = bayer[cy * kW + cx + 1];
            best.g = bayer[cy * kW + cx];
            best.b = bayer[(cy + 1) * kW + cx];
        }
    }
    if (learn)
    {
        save_ignore();
        say("helper: ignoring %d lights", noted);
    }
    return best;
}

static void *usb_lib(void)
{
    void *u = dlopen("libSceUsbd.sprx", RTLD_LAZY);
    if (!u)
        return NULL;
    *(void **)&uInit = dlsym(u, "sceUsbdInit");
    *(void **)&uExit = dlsym(u, "sceUsbdExit");
    *(void **)&uList = dlsym(u, "sceUsbdGetDeviceList");
    *(void **)&uFreeList = dlsym(u, "sceUsbdFreeDeviceList");
    *(void **)&uDesc = dlsym(u, "sceUsbdGetDeviceDescriptor");
    *(void **)&uOpen = dlsym(u, "sceUsbdOpen");
    *(void **)&uClose = dlsym(u, "sceUsbdClose");
    *(void **)&uCtrl = dlsym(u, "sceUsbdControlTransfer");
    *(void **)&uClaim = dlsym(u, "sceUsbdClaimInterface");
    *(void **)&uRelease = dlsym(u, "sceUsbdReleaseInterface");
    *(void **)&uBulk = dlsym(u, "sceUsbdBulkTransfer");
    *(void **)&uClearHalt = dlsym(u, "sceUsbdClearHalt");
    return uInit && uList && uDesc && uOpen && uCtrl && uBulk ? u : NULL;
}

static void stream(void *h)
{
    for (unsigned i = 0; i < sizeof k_bridge_init / 2; ++i)
        ov_write(h, k_bridge_init[i][0], k_bridge_init[i][1]);
    eye_led(h, 1);
    int sfail = 0;
    for (unsigned i = 0; i < sizeof k_sensor_init / 2; ++i)
    {
        if (sccb_write(h, k_sensor_init[i][0], k_sensor_init[i][1]) < 0)
            ++sfail;
        if (i == 0)
            usleep(10000);
    }
    ov_write(h, 0xe0, 0x09);
    for (unsigned i = 0; i < sizeof k_bridge_vga / 2; ++i)
        ov_write(h, k_bridge_vga[i][0], k_bridge_vga[i][1]);
    for (unsigned i = 0; i < sizeof k_sensor_vga / 2; ++i)
        if (sccb_write(h, k_sensor_vga[i][0], k_sensor_vga[i][1]) < 0)
            ++sfail;
    sccb_write(h, 0x11, 0x04); /* 30 fps */
    sccb_write(h, 0x0d, 0x81);
    ov_write(h, 0xe5, 0x02);
    /* Exposure and gain by hand, low, so only the glowing ball is bright
     * (PS3EYEDriver's setAutogain(false), setExposure, setGain). */
    sccb_write(h, 0x13, 0xf0); /* automatic gain, exposure and white balance off */
    sccb_write(h, 0x64, 0x84);
    sccb_write(h, 0x00, 0x00); /* gain: lowest */
    sccb_write(h, 0x08, 60 >> 7);
    sccb_write(h, 0x10, (uint8_t)(60 << 1)); /* exposure 60 of 255 */
    const int c = uClaim ? uClaim(h, 0) : -1;
    if (uClearHalt)
        uClearHalt(h, 0x81);
    say("helper: camera set up (%d sensor writes failed), claim %d", sfail, c);
    eye_led(h, 1);
    ov_write(h, 0xe0, 0x00); /* stream on */

    static uint8_t xfer[65536];
    static uint8_t frame[kFrame], done[kFrame];
    Assembler a = {frame, done, 0, 0, 0, 0, 0, 0};
    int errors = 0, missed = 0, exposure = 60;
    const double t0 = now();
    while (porpoise_there() && now() - t0 < 600)
    {
        int got = 0;
        const int r = uBulk(h, 0x81, xfer, sizeof xfer, &got, 1000);
        if (r < 0 && got <= 0)
        {
            if (++errors > 20)
            {
                say("helper: video stopped (0x%08x)", r);
                break;
            }
            continue;
        }
        errors = 0;
        const int before = a.frames;
        for (int off = 0; off < got; off += 2048)
            asm_payload(&a, xfer + off, got - off < 2048 ? got - off : 2048);
        if (a.frames != before)
        {
            const bool learn = g_learn;
            g_learn = false;
            const Ball ball = find_ball(done, learn);
            /* Too dark for the ball (a dim light, a far player): a longer exposure. */
            missed = ball.found ? 0 : missed + 1;
            if (missed >= 90 && exposure < 200)
            {
                exposure = exposure < 120 ? 120 : 200;
                sccb_write(h, 0x08, (uint8_t)(exposure >> 7));
                sccb_write(h, 0x10, (uint8_t)(exposure << 1));
                say("helper: no ball for 3 s: exposure %d", exposure);
                missed = 0;
            }
            Packet p;
            memset(&p, 0, sizeof p);
            p.magic = MAGIC;
            p.type = 1;
            p.frame = (uint32_t)a.frames;
            p.found = (int16_t)ball.found;
            p.x = (int16_t)ball.x;
            p.y = (int16_t)ball.y;
            p.radius = (int16_t)ball.radius;
            p.count = (uint16_t)ball.count;
            p.r = (uint8_t)ball.r;
            p.g = (uint8_t)ball.g;
            p.b = (uint8_t)ball.b;
            send_packet(&p, offsetof(Packet, text));
            /* The picture, 160 x 120 colour, ten a second while Porpoise shows it. */
            if (g_video && a.frames % 3 == 0)
            {
                enum
                {
                    kTW = 160,
                    kTH = 120
                };
                static uint8_t thumb[kTW * kTH * 3];
                for (int ty = 0; ty < kTH; ++ty)
                    for (int tx = 0; tx < kTW; ++tx)
                    {
                        const int by = ty * 4, bx = tx * 4;
                        uint8_t *o = thumb + (ty * kTW + tx) * 3;
                        o[0] = done[by * kW + bx + 1];
                        o[1] = (uint8_t)((done[by * kW + bx] + done[(by + 1) * kW + bx + 1]) / 2);
                        o[2] = done[(by + 1) * kW + bx];
                    }
                Packet v;
                memset(&v, 0, sizeof v);
                v.magic = MAGIC;
                v.type = 4;
                v.frame = (uint32_t)a.frames;
                send_packet(&v, sizeof v);
                send_raw(thumb, sizeof thumb);
            }
        }
    }
    ov_write(h, 0xe0, 0x09); /* stream off */
    eye_led(h, 0);
    if (uRelease && c == 0)
        uRelease(h, 0);
    say("helper: stopped after %d frames (%d bad)", a.frames, a.bad);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    const int ls = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in me;
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    me.sin_port = htons(PORT);
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ls < 0 || bind(ls, (struct sockaddr *)&me, sizeof me) != 0 || listen(ls, 1) != 0)
        return 0; /* (another helper is running) */
    /* Porpoise connects right after sending this; give up after 10 s. */
    struct pollfd pf = {ls, POLLIN, 0};
    g_conn = poll(&pf, 1, 10000) == 1 ? accept(ls, NULL, NULL) : -1;
    close(ls);
    if (g_conn < 0)
        return 0;
    /* Each position as soon as it's found: small writes were held back and
     * sent three at a time, a tenth of a second late (Oct 24 logs). */
    setsockopt(g_conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    say("helper: started");
    load_ignore();
    say("helper: ignoring %d lights", g_nignore);
    if (!usb_lib())
    {
        say("helper: the USB library isn't reachable");
        return 0;
    }
    say("helper: sceUsbdInit -> 0x%08x", uInit());
    void **list = NULL;
    const long n = uList(&list);
    bool found = false;
    for (long i = 0; i < n && list && !found; ++i)
    {
        DevDesc d;
        memset(&d, 0, sizeof d);
        if (uDesc(list[i], &d) < 0 || d.idVendor != 0x1415 || d.idProduct != 0x2000)
            continue;
        found = true;
        void *h = NULL;
        const int r = uOpen(list[i], &h);
        if (r < 0 || !h)
        {
            say("helper: the PS3 Eye didn't open (%d)", r);
            break;
        }
        ov_write(h, 0xe7, 0x3a);
        ov_write(h, 0xe0, 0x08);
        usleep(100000);
        ov_write(h, 0xf1, 0x42);
        stream(h);
        uClose(h);
    }
    if (!found)
        say("helper: no PS3 Eye plugged in (%ld USB devices)", n);
    if (list && uFreeList)
        uFreeList(list, 1);
    if (uExit)
        uExit();
    Packet p;
    memset(&p, 0, sizeof p);
    p.magic = MAGIC;
    p.type = 3;
    send_packet(&p, offsetof(Packet, text));
    close(g_conn);
    return 0;
}
