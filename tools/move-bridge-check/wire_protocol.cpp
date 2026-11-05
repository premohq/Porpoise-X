#include "../../src/porpoise_move_bridge.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace porpoise::move_bridge;

static std::uint64_t clock_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
static void put16(std::uint8_t *p, std::uint16_t v) { p[0]=v; p[1]=v>>8; }
static void put32(std::uint8_t *p, std::uint32_t v) {
    for (unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(v>>(i*8));
}
static void put64(std::uint8_t *p, std::uint64_t v) {
    put32(p,static_cast<std::uint32_t>(v)); put32(p+4,static_cast<std::uint32_t>(v>>32));
}
static void putf(std::uint8_t *p, float f) { std::uint32_t v; std::memcpy(&v,&f,4); put32(p,v); }
static std::vector<std::uint8_t> make_packet(unsigned version, std::uint64_t seq,
                                              std::uint64_t sent, bool connected=true,
                                              bool valid=true) {
    const std::size_t size=version==1?152:184;
    std::vector<std::uint8_t> b(size,0);
    put32(b.data(),0x3142564d); put16(b.data()+4,version); put16(b.data()+6,size);
    put64(b.data()+8,seq); put64(b.data()+16,sent); put32(b.data()+24,valid?1:0);
    std::uint8_t *p=b.data()+32;
    putf(p+12,0); putf(p+16,0); putf(p+20,0); putf(p+24,1);
    putf(p+28,0); putf(p+32,1); putf(p+36,0);
    putf(p+40,0); putf(p+44,0); putf(p+48,0);
    put32(p+76,connected?1:0);
    if(version==2) {
        putf(b.data()+152,160); putf(b.data()+156,120);
        putf(b.data()+160,20); putf(b.data()+164,30);
        put64(b.data()+168,sent); put32(b.data()+176,1);
    }
    return b;
}

class Sender {
public:
    Sender(std::uint16_t port=9976) {
        fd=::socket(AF_INET,SOCK_DGRAM,0); assert(fd!=kInvalidSocket);
        to.sin_family=AF_INET; to.sin_port=htons(port); to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    }
    ~Sender() {
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
    }
    void send(const std::vector<std::uint8_t>& b) {
#if defined(_WIN32)
        const int n=sendto(fd,reinterpret_cast<const char*>(b.data()),static_cast<int>(b.size()),0,
                           reinterpret_cast<const sockaddr*>(&to),sizeof(to));
#else
        const auto n=sendto(fd,b.data(),b.size(),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));
#endif
        assert(n==static_cast<decltype(n)>(b.size()));
    }
private:
    SocketHandle fd;
    sockaddr_in to{};
};

int main() {
#if defined(_WIN32)
    WSADATA wd{}; assert(WSAStartup(MAKEWORD(2,2),&wd)==0);
#endif
    {
        Client p1,p2;assert(p1.open(9980)&&p2.open(9981));Sender s1(9980),s2(9981);
        auto now=clock_us();s1.send(make_packet(2,1,now));s2.send(make_packet(2,99,now));
        p1.poll(now);p2.poll(now);assert(p1.fresh(now)&&p2.fresh(now));
        assert(p1.packet().sequence==1&&p2.packet().sequence==99);
        auto later=clock_us();s1.send(make_packet(2,2,later,false,false));p1.poll(later);p2.poll(later);
        assert(!p1.fresh(later)&&p2.fresh(later));
    }
    Client c; assert(c.open()); Sender s;
    auto t=clock_us();
    auto v1=make_packet(1,10,t); s.send(v1); c.poll(t);
    assert(c.active(t)&&c.fresh(t)&&c.packet().version==1&&c.packet().sequence==10);

    auto heartbeat=make_packet(1,11,clock_us(),false,false); s.send(heartbeat); t=clock_us(); c.poll(t);
    assert(c.active(t)&&!c.fresh(t)&&!c.packet().valid&&c.packet().sequence==11);

    auto v2=make_packet(2,12,clock_us()); s.send(v2); t=clock_us(); c.poll(t);
    assert(c.fresh(t)&&c.optical_fresh(t)&&c.packet().version==2&&c.packet().optical_x==160);

    const auto accepted=c.packet().sequence;
    auto oversized=make_packet(2,12,clock_us()); oversized.push_back(0); s.send(oversized);
    auto nan=make_packet(1,12,clock_us()); put32(nan.data()+32+28,0x7fc00000); s.send(nan);
    auto future=make_packet(1,12,clock_us()+5000000); s.send(future);
    auto stale=make_packet(1,12,clock_us()-500001); s.send(stale);
    auto invalid_connected=make_packet(1,13,clock_us()); put32(invalid_connected.data()+32+76,2); s.send(invalid_connected);
    auto invalid_flags=make_packet(1,13,clock_us()); put32(invalid_flags.data()+24,2); s.send(invalid_flags);
    c.poll(clock_us()); assert(c.packet().sequence==accepted&&c.fresh(clock_us()));

    auto replay=make_packet(2,12,clock_us()); s.send(replay); c.poll(clock_us());
    assert(c.packet().sequence==accepted);

    const auto after_timeout=clock_us()+600001;
    auto disconnected=make_packet(1,1,after_timeout,false,true); s.send(disconnected);
    c.poll(after_timeout);
    assert(c.active(after_timeout)&&!c.fresh(after_timeout)&&c.packet().sequence==1);
    assert(!c.active(after_timeout+500001));

    c.close(); assert(!c.active(clock_us())&&!c.fresh(clock_us()));
#if defined(_WIN32)
    WSACleanup();
#endif
}
