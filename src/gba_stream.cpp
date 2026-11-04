/* Porpoise - the GBA's screen and buttons in a browser (gba_stream.hpp).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "gba_stream.hpp"

#include "ecam_websocket.h"
#include "trace.hpp"

#include <algorithm>
#include <cstdarg>
#include <cerrno>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>


namespace porpoise::gba_stream
{
namespace
{
constexpr unsigned short kPort = 6771;
constexpr std::size_t kMaxClients = 32; /* four GBAs on /multi are eight sockets, and a page just left may still hold its own */
constexpr int kAhead = 8;          /* pictures sent and not yet acknowledged, at most */
constexpr int kKeyQuietMs = 2000;  /* a page that stops saying what it holds lets go */

#include "gba_stream_page.inc" /* kPage: one GBA, / */
#include "gba_stream_multi.inc" /* kMultiPage: several at once, /multi */

/* Porpoise's logging: what matters (the server up or down, a port that won't open, a browser
 * turned away) in trace.txt as a "gba web:" line; the rest (browsers coming and going, the page's
 * buttons, the five-second counts) only with Settings > Debug Logs (set_verbose). */
std::atomic<bool> g_verbose{false};

__attribute__((format(printf, 1, 2))) void note(const char *format, ...)
{
    char line[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    ps5::debug::mark(line);
}

__attribute__((format(printf, 1, 2))) void detail(const char *format, ...)
{
    if (!g_verbose.load(std::memory_order_relaxed))
        return;
    char line[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    ps5::debug::mark(line);
}

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Client
{
    std::thread thread;
    std::atomic<bool> done{false};
};

struct Server
{
    std::mutex life; /* start / stop */
    std::atomic<bool> stop{false};
    int listener = -1;
    std::thread accept_thread, encode_thread;
    std::list<Client> clients; /* the accept thread's */

    /* Each GameCube port's GBA: the newest picture from the game, the newest packed picture made
     * of it, who watches, and the page's buttons. */
    std::mutex m;
    std::condition_variable raw_ready, packed_ready;
    struct Port
    {
        std::vector<std::uint16_t> raw;
        unsigned raw_w = 0, raw_h = 0, raw_seq = 0;
        std::vector<std::uint8_t> packed;
        unsigned packed_seq = 0;
        std::atomic<int> viewers{0}; /* open /video sockets */
        std::atomic<std::uint16_t> keys{0};
        std::atomic<long long> keys_ms{0};
        std::atomic<std::uint64_t> key_owner{0};
        std::atomic<bool> reset{false};
        std::atomic<bool> disconnect{false};
    };
    Port port[4];
    std::atomic<unsigned> ports{0}; /* the ports with a GBA (set_ports) */
    std::atomic<std::uint64_t> next_id{1};

    /* Every five seconds in trace.txt, while someone watches. */
    std::atomic<unsigned> submitted{0}, encoded{0}, sent{0};
    std::atomic<unsigned long long> bytes{0};
};

Server &S()
{
    static Server *s = new Server; /* never destroyed: static destructors may run with threads left */
    return *s;
}

int viewers()
{
    int n = 0;
    for (const Server::Port &p : S().port)
        n += p.viewers.load(std::memory_order_relaxed);
    return n;
}

bool alive()
{
    return !S().stop.load(std::memory_order_relaxed);
}

/* ---- the pictures, packed ---- */

/* Lossless, as the core's own page had it (GBAWeb.cpp's CompressFrame): each pixel XOR the one
 * above, then PackBits-style 16-bit symbols: a control byte below 128 is that many plus one
 * literals that follow, 128 and up a run of (byte - 125) copies of the one that follows. A
 * message is 'G' 'B' 'A' '2', width and height (u16 LE), the packed length (u32 LE), the data. */
void pack(const std::vector<std::uint16_t> &px, unsigned width, unsigned height, std::vector<std::uint8_t> &out)
{
    const std::size_t n = px.size();
    std::vector<std::uint16_t> d(n);
    for (std::size_t i = 0; i < n; ++i)
        d[i] = i < width ? px[i] : static_cast<std::uint16_t>(px[i] ^ px[i - width]);
    out.assign(12, 0);
    out.reserve(n / 2 + 16);
    const auto put16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    std::size_t i = 0;
    while (i < n)
    {
        std::size_t run = 1;
        while (i + run < n && d[i + run] == d[i] && run < 130)
            ++run;
        if (run >= 3)
        {
            out.push_back(static_cast<std::uint8_t>(0x80 | (run - 3)));
            put16(d[i]);
            i += run;
            continue;
        }
        const std::size_t start = i;
        std::size_t count = 0;
        while (i < n && count < 128)
        {
            std::size_t r = 1;
            while (i + r < n && d[i + r] == d[i] && r < 3)
                ++r;
            if (r >= 3)
                break;
            ++i;
            ++count;
        }
        out.push_back(static_cast<std::uint8_t>(count - 1));
        for (std::size_t k = 0; k < count; ++k)
            put16(d[start + k]);
    }
    const std::uint32_t len = static_cast<std::uint32_t>(out.size() - 12);
    const std::uint8_t head[12] = {'G', 'B', 'A', '2', std::uint8_t(width), std::uint8_t(width >> 8),
                                   std::uint8_t(height), std::uint8_t(height >> 8), std::uint8_t(len),
                                   std::uint8_t(len >> 8), std::uint8_t(len >> 16), std::uint8_t(len >> 24)};
    std::copy(head, head + 12, out.begin());
}

void encode_loop()
{
    Server &s = S();
    std::vector<std::uint16_t> px;
    std::vector<std::uint8_t> out;
    unsigned done_seq[4] = {};
    while (alive())
    {
        int which = -1;
        unsigned w = 0, h = 0, seq = 0;
        {
            std::unique_lock lock(s.m);
            auto pending = [&] {
                for (int p = 0; p < 4; ++p)
                    if (s.port[p].raw_seq != done_seq[p])
                        return p;
                return -1;
            };
            s.raw_ready.wait_for(lock, std::chrono::milliseconds(100),
                                 [&] { return s.stop.load() || pending() >= 0; });
            if (s.stop.load())
                return;
            which = pending();
            if (which < 0)
                continue;
            Server::Port &pt = s.port[which];
            px = pt.raw;
            w = pt.raw_w;
            h = pt.raw_h;
            seq = pt.raw_seq;
        }
        done_seq[which] = seq;
        pack(px, w, h, out);
        {
            std::lock_guard lock(s.m);
            s.port[which].packed.swap(out);
            s.port[which].packed_seq = seq;
        }
        ++s.encoded;
        s.packed_ready.notify_all();
    }
}

/* ---- the sockets ---- */

void reply(int fd, int code, const char *reason, const char *type, const std::string &body)
{
    char head[256];
    const int n = std::snprintf(head, sizeof head,
                                "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                                "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                                code, reason, type, body.size());
    ps5stream::SendBytes(fd, head, std::size_t(n), alive);
    ps5stream::SendBytes(fd, body.data(), body.size(), alive);
}

/* Pictures as they come, up to kAhead before the page's "ack"s; "fps N" (60, 30, 20, 15) spaces
 * them. A slow page gets the newest when there's room, never a queue. */
void video(ps5stream::WebSocket &socket, int port)
{
    Server &s = S();
    Server::Port &pt = s.port[port];
    ++pt.viewers;
    detail("gba web: a browser is watching GBA %d", port + 1);
    int ahead = 0, fps = 60;
    unsigned sent_seq = 0;
    long long last_sent = 0;
    std::vector<std::uint8_t> picture;
    std::string message;
    for (;;)
    {
        const ps5stream::WebSocket::Result r = socket.Receive(message, ahead >= kAhead ? 20 : 1);
        if (r == ps5stream::WebSocket::Result::Closed || r == ps5stream::WebSocket::Result::Error)
            break;
        if (r == ps5stream::WebSocket::Result::Text)
        {
            if (message == "ack")
                ahead = std::max(0, ahead - 1);
            else if (message.rfind("fps ", 0) == 0)
                fps = std::clamp(std::atoi(message.c_str() + 4), 1, 60);
            continue;
        }
        if (ahead >= kAhead)
            continue;
        const long long gap = 1000 / fps - 2; /* a little early rather than a frame late */
        if (now_ms() - last_sent < gap)
            continue;
        {
            std::unique_lock lock(s.m);
            s.packed_ready.wait_for(lock, std::chrono::milliseconds(15),
                                    [&] { return s.stop.load() || (pt.packed_seq != sent_seq && !pt.packed.empty()); });
            if (s.stop.load())
                break;
            if (pt.packed_seq == sent_seq || pt.packed.empty())
                continue;
            picture = pt.packed;
            sent_seq = pt.packed_seq;
        }
        if (!socket.Send(2, picture.data(), picture.size()))
            break;
        last_sent = now_ms();
        ++ahead;
        ++s.sent;
        s.bytes += picture.size();
    }
    --pt.viewers;
    detail("gba web: a browser stopped watching GBA %d", port + 1);
}

/* "k <mask>": the buttons held; sent on each change and every quarter second while any is. */
void input(ps5stream::WebSocket &socket, int port)
{
    Server &s = S();
    Server::Port &pt = s.port[port];
    const std::uint64_t me = s.next_id++;
    std::string message;
    for (;;)
    {
        const ps5stream::WebSocket::Result r = socket.Receive(message, 200);
        if (r == ps5stream::WebSocket::Result::Closed || r == ps5stream::WebSocket::Result::Error)
            break;
        if (r == ps5stream::WebSocket::Result::Text && message == "disconnect")
        {
            pt.disconnect.store(true);
            detail("gba web: Disconnect GBA %d from the page", port + 1);
            continue;
        }
        if (r == ps5stream::WebSocket::Result::Text && message == "reset")
        {
            pt.reset.store(true);
            detail("gba web: Reset GBA %d from the page", port + 1);
            continue;
        }
        if (r != ps5stream::WebSocket::Result::Text || message.rfind("k ", 0) != 0)
            continue;
        const long mask = std::strtol(message.c_str() + 2, nullptr, 10);
        pt.keys.store(static_cast<std::uint16_t>(mask & 0x3FF));
        pt.keys_ms.store(now_ms());
        pt.key_owner.store(me);
    }
    if (pt.key_owner.load() == me)
        pt.keys.store(0);
}

void handle(int fd, Client *client)
{
    ps5stream::ConfigureSocket(fd);
    std::string request;
    const long long until = now_ms() + 5000;
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192 && now_ms() < until && alive())
    {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 50) <= 0)
            continue;
        char buf[1024];
        const auto n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0)
            break;
        request.append(buf, std::size_t(n));
    }
    const std::size_t sp1 = request.find(' '), sp2 = sp1 == std::string::npos ? sp1 : request.find(' ', sp1 + 1);
    std::string path = sp2 == std::string::npos ? "" : request.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::size_t q = path.find('?');
    const std::string query = q == std::string::npos ? "" : path.substr(q + 1);
    path = path.substr(0, q);
    /* ?p=1..4: which port's GBA; otherwise the lowest with one. */
    int port = -1;
    const std::size_t pp = query.find("p=");
    if (pp != std::string::npos && (pp == 0 || query[pp - 1] == '&'))
        port = std::atoi(query.c_str() + pp + 2) - 1;
    const unsigned ports = S().ports.load();
    if (port < 0 || port > 3 || !(ports & (1u << port)))
    {
        port = 0;
        for (int i = 0; i < 4; ++i)
            if (ports & (1u << i))
            {
                port = i;
                break;
            }
    }
    if (request.rfind("GET ", 0) != 0)
        reply(fd, 405, "Method Not Allowed", "text/plain", "GET only\n");
    else if (path == "/" || path == "/index.html")
        reply(fd, 200, "OK", "text/html; charset=utf-8", kPage);
    else if (path == "/multi")
        reply(fd, 200, "OK", "text/html; charset=utf-8", kMultiPage);
    else if (path == "/ports")
    {
        /* The ports with a GBA, as a JSON list of 1..4. */
        std::string list = "[";
        for (int i = 0; i < 4; ++i)
            if (ports & (1u << i))
                list += (list.size() > 1 ? "," : "") + std::to_string(i + 1);
        reply(fd, 200, "OK", "application/json", list + "]");
    }
    else if (path == "/video" || path == "/input")
    {
        ps5stream::WebSocket socket(fd, alive);
        if (!socket.Upgrade(request))
            reply(fd, 400, "Bad Request", "text/plain", "a WebSocket is expected here\n");
        else if (path == "/video")
            video(socket, port);
        else
            input(socket, port);
    }
    else
        reply(fd, 404, "Not Found", "text/plain", "not here\n");
    close(fd);
    client->done = true;
}

void accept_loop()
{
    Server &s = S();
    long long last_report = now_ms();
    unsigned rep_sub = 0, rep_enc = 0, rep_sent = 0;
    unsigned long long rep_bytes = 0;
    while (alive())
    {
        pollfd p{s.listener, POLLIN, 0};
        if (poll(&p, 1, 100) > 0)
        {
            const int fd = accept(s.listener, nullptr, nullptr);
            if (fd >= 0)
            {
                if (s.clients.size() >= kMaxClients)
                {
                    note("gba web: a browser turned away (%zu connections open)", s.clients.size());
                    close(fd);
                }
                else
                {
                    s.clients.emplace_back();
                    Client *c = &s.clients.back();
                    c->thread = std::thread(handle, fd, c);
                }
            }
        }
        for (auto it = s.clients.begin(); it != s.clients.end();)
        {
            if (it->done.load())
            {
                it->thread.join();
                it = s.clients.erase(it);
            }
            else
                ++it;
        }
        if (now_ms() - last_report >= 5000)
        {
            last_report = now_ms();
            const unsigned sub = s.submitted.load(), enc = s.encoded.load(), snt = s.sent.load();
            const unsigned long long b = s.bytes.load();
            if (snt != rep_sent || sub != rep_sub)
                detail("gba web: last 5 s: %u frames from the GBAs, %u packed, %u sent, %llu KiB (%d browser%s)",
                       sub - rep_sub, enc - rep_enc, snt - rep_sent, (b - rep_bytes) / 1024, viewers(),
                       viewers() == 1 ? "" : "s");
            rep_sub = sub, rep_enc = enc, rep_sent = snt, rep_bytes = b;
        }
    }
}
} // namespace

void start()
{
    Server &s = S();
    std::lock_guard life(s.life);
    if (s.accept_thread.joinable())
        return;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || listen(fd, 8) != 0)
    {
        note("gba web: cannot listen on port %u (errno %d)", unsigned(kPort), errno);
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    s.stop = false;
    s.listener = fd;
    s.accept_thread = std::thread(accept_loop);
    s.encode_thread = std::thread(encode_loop);
    note("gba web: serving on port %u", unsigned(kPort));
}

void stop()
{
    Server &s = S();
    std::lock_guard life(s.life);
    if (!s.accept_thread.joinable())
        return;
    s.stop = true;
    s.raw_ready.notify_all();
    s.packed_ready.notify_all();
    s.accept_thread.join();
    s.encode_thread.join();
    for (Client &c : s.clients)
        c.thread.join();
    s.clients.clear();
    close(s.listener);
    s.listener = -1;
    {
        std::lock_guard lock(s.m);
        for (Server::Port &p : s.port)
        {
            p.keys = 0;
            p.raw.clear();
            p.packed.clear();
            p.raw_seq = p.packed_seq = 0;
        }
    }
    note("gba web: stopped");
}

void set_verbose(bool on)
{
    g_verbose.store(on);
}

void set_ports(unsigned ports)
{
    S().ports.store(ports & 15u);
}

bool want_frame(int port)
{
    return port >= 0 && port < 4 && S().port[port].viewers.load(std::memory_order_relaxed) > 0;
}

void submit(int port, const std::uint16_t *pixels, unsigned width, unsigned height)
{
    Server &s = S();
    if (port < 0 || port > 3 || !pixels || width == 0 || height == 0 || width * height > 240 * 160)
        return;
    {
        std::lock_guard lock(s.m);
        Server::Port &pt = s.port[port];
        pt.raw.assign(pixels, pixels + std::size_t(width) * height);
        pt.raw_w = width;
        pt.raw_h = height;
        if (++pt.raw_seq == 0)
            pt.raw_seq = 1; /* 0 is "nothing yet" */
    }
    ++s.submitted;
    s.raw_ready.notify_one();
}

bool take_disconnect(int port)
{
    return port >= 0 && port < 4 && S().port[port].disconnect.exchange(false);
}

bool take_reset(int port)
{
    return port >= 0 && port < 4 && S().port[port].reset.exchange(false);
}

std::uint16_t keys(int port)
{
    if (port < 0 || port > 3)
        return 0;
    Server::Port &pt = S().port[port];
    if (now_ms() - pt.keys_ms.load(std::memory_order_relaxed) > kKeyQuietMs)
        return 0;
    return pt.keys.load(std::memory_order_relaxed);
}
} // namespace porpoise::gba_stream
