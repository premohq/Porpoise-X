#pragma once
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "Common/CommonTypes.h"
namespace Common { inline u32 swap32(u32 v) { return __builtin_bswap32(v); } }
namespace Config {
template <class T> struct Info { T v; };
template <class T> T Get(const Info<T>& i) { return i.v; }
inline Info<bool> SESSION_USE_FMA{true};
inline Info<bool> MAIN_ACCURATE_FMADDS{false};
}
namespace Core {
class System;
class CPUThreadGuard {
public:
  explicit CPUThreadGuard(System& s) : m(s) {}
  System& GetSystem() const { return m; }
private:
  System& m;
};
}
namespace PowerPC {
constexpr int BAT_INDEX_SHIFT = 17;
constexpr u32 BAT_PAGE_SIZE = 1u << 17;
struct PairedSingle {
  u64 ps0 = 0, ps1 = 0;
  double PS0AsDouble() const { return std::bit_cast<double>(ps0); }
  double PS1AsDouble() const { return std::bit_cast<double>(ps1); }
  void Fill(double d) { ps0 = ps1 = std::bit_cast<u64>(d); }
};
struct CR { u32 f[8]{}; void SetField(u32 i, u32 v) { f[i] = v; } u32 GetField(u32 i) const { return f[i]; } };
struct PowerPCState {
  u32 gpr[32]{}; PairedSingle ps[32]; CR cr; u32 spr[1024]{}; u32 npc = 0; u32 so = 0;
  u32 GetXER_SO() const { return so; }
};
class MMU {
public:
  u8* base = nullptr;
  template <std::unsigned_integral T> T Read(u32 ea) {
    T v = 0; for (unsigned i = 0; i < sizeof(T); ++i) v = T((v << 8) | base[u32(ea + i)]); return v; }
  template <std::unsigned_integral T> void Write(u32 v, u32 ea) {
    for (unsigned i = 0; i < sizeof(T); ++i) base[u32(ea + i)] = u8(v >> (8 * (sizeof(T) - 1 - i))); }
  bool IsOptimizableRAMAddress(u32, u32) const { return true; }
  static inline u8* code = nullptr;
  static u32 HostRead_Instruction(const Core::CPUThreadGuard&, u32 a) {
    return code ? (u32(code[a]) << 24) | (u32(code[a + 1]) << 16) | (u32(code[a + 2]) << 8) | code[u32(a + 3)] : 0; }
};
}
#define CTR(s) (s).spr[9]
#define LR(s) (s).spr[8]
namespace Memory {
class MemoryManager {
public:
  u8* base = nullptr;
  u8* GetLogicalBase() const { return base; }
  u8* GetL1Cache() const { return base + 0xE0000000u; }
  u32 GetL1CacheSize() const { return 0x40000; }
};
}
namespace Core {
class System {
public:
  PowerPC::PowerPCState ppc; PowerPC::MMU mmu; Memory::MemoryManager mem;
  PowerPC::PowerPCState& GetPPCState() { return ppc; }
  PowerPC::MMU& GetMMU() { return mmu; }
  Memory::MemoryManager& GetMemory() { return mem; }
};
}
namespace HLE { inline void Patch(Core::System&, u32, const char*) {} }
namespace DiscIO {
struct Partition {};
class VolumeDisc {
public:
  Partition GetGamePartition() const { return {}; }
  template <class T> std::optional<T> ReadSwapped(u64, const Partition&) const { return std::nullopt; }
};
inline std::optional<u64> GetBootDOLOffset(const VolumeDisc&, const Partition&) { return std::nullopt; }
}
