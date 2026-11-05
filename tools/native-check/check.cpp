// Checks HLE_PorpoiseNative's routines against a small interpreter of the
// game's own instructions with the JIT's semantics (each single-precision op
// in double, rounded to single; madds fused in double, then rounded).
#include <sys/mman.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "Core/HLE/HLE_PorpoiseNative.cpp"

using namespace HLE_PorpoiseNative;

static u8* g_mem;

static u32 R32(u32 a) { return (u32(g_mem[a]) << 24) | (u32(g_mem[a + 1]) << 16) | (u32(g_mem[a + 2]) << 8) | g_mem[u32(a + 3)]; }
static void W32(u32 a, u32 v) { g_mem[a] = v >> 24; g_mem[a + 1] = v >> 16; g_mem[a + 2] = v >> 8; g_mem[a + 3] = v; }
static double Rnd(double x) { return double(float(x)); }

struct Ref
{
  u32 gpr[32]{};
  double ps0[32]{}, ps1[32]{};
  u32 cr0 = 0, ctr = 0, lr = 0, so = 0;
};

// Runs code (at base) from pc until a blr or pc == stop.
static u32 Interpret(Ref& r, const u32* code, u32 count, u32 base, u32 pc, u32 stop)
{
  for (long steps = 0; steps < 50'000'000; ++steps)
  {
    if (pc == stop)
      return pc;
    if (pc < base || pc >= base + count * 4)
    {
      std::printf("pc out of code %08x\n", pc);
      std::exit(1);
    }
    const u32 op = code[(pc - base) / 4];
    const u32 opcd = op >> 26, d = (op >> 21) & 31, a = (op >> 16) & 31, b = (op >> 11) & 31,
              c = (op >> 6) & 31;
    const s32 simm = s16(op & 0xFFFF);
    const u32 ea_d = (a ? r.gpr[a] : 0) + simm;
    u32 next = pc + 4;
    auto bc_taken = [&](u32 bo, u32 bi) {
      if (!(bo & 4))
        --r.ctr;
      const bool ctr_ok = (bo & 4) || ((r.ctr != 0) ^ ((bo >> 1) & 1));
      const bool bit = (r.cr0 >> (3 - (bi & 3))) & 1;
      const bool cond_ok = (bo & 16) || (bit == ((bo >> 3) & 1));
      return ctr_ok && cond_ok;
    };
    switch (opcd)
    {
    case 14: r.gpr[d] = (a ? r.gpr[a] : 0) + simm; break;                 // addi
    case 37: W32(ea_d, r.gpr[d]); r.gpr[a] = ea_d; break;                // stwu
    case 36: W32(ea_d, r.gpr[d]); break;                                   // stw
    case 32: r.gpr[d] = R32(ea_d); break;                                  // lwz
    case 34: r.gpr[d] = g_mem[ea_d]; break;                                // lbz
    case 48: { const double v = double(std::bit_cast<float>(R32(ea_d))); r.ps0[d] = r.ps1[d] = v; break; }  // lfs
    case 50: { const u64 v = (u64(R32(ea_d)) << 32) | R32(ea_d + 4); r.ps0[d] = std::bit_cast<double>(v); break; }  // lfd
    case 52: W32(ea_d, std::bit_cast<u32>(float(r.ps0[d]))); break;      // stfs (single values)
    case 18: next = pc + (s32(op << 6) >> 6 & ~3); break;                 // b
    case 16:                                                               // bc
      if (bc_taken(d, a))
        next = pc + (s32(s16(op & 0xFFFC)));
      break;
    case 19:
      if (op == 0x4E800020) return pc;  // blr
      std::printf("op19 %08x\n", op); std::exit(1);
    case 10: { const u32 x = r.gpr[a], y = u32(op & 0xFFFF); r.cr0 = (x < y ? 8 : x > y ? 4 : 2) | r.so; break; }  // cmpli
    case 11: { const s32 x = r.gpr[a], y = simm; r.cr0 = (x < y ? 8 : x > y ? 4 : 2) | r.so; break; }  // cmpi
    case 21: {                                                              // rlwinm
      const u32 sh = b, mb = c, me = (op >> 1) & 31;
      u32 mask = 0;
      for (u32 i = mb;; i = (i + 1) & 31) { mask |= 0x80000000u >> i; if (i == me) break; }
      r.gpr[a] = std::rotl(r.gpr[d], int(sh)) & mask;
      break;
    }
    case 31: {
      const u32 xo = (op >> 1) & 1023;
      if (xo == 444) r.gpr[a] = r.gpr[d] | r.gpr[b];                       // or
      else if (xo == 266) r.gpr[d] = r.gpr[a] + r.gpr[b];                  // add
      else if (xo == 467 && op == (0x7C0903A6 | (d << 21))) r.ctr = r.gpr[d];  // mtctr
      else if (xo == 0) { const s32 x = r.gpr[a], y = r.gpr[b]; r.cr0 = (x < y ? 8 : x > y ? 4 : 2) | r.so; }  // cmpw
      else { std::printf("op31 %08x\n", op); std::exit(1); }
      break;
    }
    case 56: {                                                              // psq_l
      const u32 ea = (a ? r.gpr[a] : 0) + (s32(op << 20) >> 20);
      const bool w = (op >> 15) & 1;
      r.ps0[d] = double(std::bit_cast<float>(R32(ea)));
      r.ps1[d] = w ? 1.0 : double(std::bit_cast<float>(R32(ea + 4)));
      break;
    }
    case 60: {                                                              // psq_st
      const u32 ea = (a ? r.gpr[a] : 0) + (s32(op << 20) >> 20);
      const bool w = (op >> 15) & 1;
      W32(ea, std::bit_cast<u32>(float(r.ps0[d])));
      if (!w) W32(ea + 4, std::bit_cast<u32>(float(r.ps1[d])));
      break;
    }
    case 4: {
      const u32 x5 = (op >> 1) & 31;
      double r0, r1;
      if (x5 == 12) { r0 = Rnd(r.ps0[a] * r.ps0[c]); r1 = Rnd(r.ps1[a] * r.ps0[c]); }                 // ps_muls0
      else if (x5 == 14) { r0 = Rnd(std::fma(r.ps0[a], r.ps0[c], r.ps0[b])); r1 = Rnd(std::fma(r.ps1[a], r.ps0[c], r.ps1[b])); }  // ps_madds0
      else if (x5 == 15) { r0 = Rnd(std::fma(r.ps0[a], r.ps1[c], r.ps0[b])); r1 = Rnd(std::fma(r.ps1[a], r.ps1[c], r.ps1[b])); }  // ps_madds1
      else if (x5 == 21) { r0 = Rnd(r.ps0[a] + r.ps0[b]); r1 = Rnd(r.ps1[a] + r.ps1[b]); }           // ps_add
      else { std::printf("op4 %08x\n", op); std::exit(1); }
      r.ps0[d] = r0; r.ps1[d] = r1;
      break;
    }
    case 59: {
      const u32 x5 = (op >> 1) & 31;
      double v;
      if (x5 == 20) v = Rnd(r.ps0[a] - r.ps0[b]);        // fsubs
      else if (x5 == 25) v = Rnd(r.ps0[a] * r.ps0[c]);   // fmuls
      else { std::printf("op59 %08x\n", op); std::exit(1); }
      r.ps0[d] = r.ps1[d] = v;
      break;
    }
    default: std::printf("op %08x\n", op); std::exit(1);
    }
    pc = next;
  }
  std::printf("runaway\n");
  std::exit(1);
}

static std::mt19937_64 rng(12345);
static float RandF(double scale)
{
  std::uniform_real_distribution<double> u(-1, 1);
  const int kind = rng() % 64;
  if (kind == 0) return 0.0f;
  if (kind == 1) return -0.0f;
  if (kind == 2) return std::bit_cast<float>(u32(rng() & 0x007FFFFF));  // denormal
  if (kind == 3) return float(u(rng) * 1e30);
  if (kind == 4) return float(u(rng) * 1e-30);
  return float(u(rng) * scale);
}

static const u32 kLo = 0x80100000, kHi = 0x80300000;
static const u32 kL1 = 0xE0000000, kL1Size = 0x40000;

static void Fill()
{
  for (u32 r = 0x80110000; r < 0x80180000; r += 0x10000)
    for (u32 a = r; a < r + (r == 0x80110000 ? 0x4000 : 0x1000); a += 4) W32(a, std::bit_cast<u32>(RandF(10)));
  for (u32 a = 0x80200000; a < 0x80200010; a += 4) W32(a, std::bit_cast<u32>(RandF(10)));
  for (u32 r = kL1 + 0x1000; r < kL1 + 0x7000; r += 0x1000)
    for (u32 a = r; a < r + 0x200; a += 4) W32(a, std::bit_cast<u32>(RandF(10)));
}

static std::vector<u8> Snap()
{
  std::vector<u8> v(kHi - kLo + kL1Size);
  std::memcpy(v.data(), g_mem + kLo, kHi - kLo);
  std::memcpy(v.data() + (kHi - kLo), g_mem + kL1, kL1Size);
  return v;
}
static void Restore(const std::vector<u8>& v)
{
  std::memcpy(g_mem + kLo, v.data(), kHi - kLo);
  std::memcpy(g_mem + kL1, v.data() + (kHi - kLo), kL1Size);
}

static int Fail(const char* what, int trial) { std::printf("FAIL %s trial %d\n", what, trial); return 1; }

int main()
{
  g_mem = static_cast<u8*>(mmap(nullptr, 0x1'0000'0000ull, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
  Core::System sys;
  sys.mmu.base = g_mem;
  sys.mem.base = g_mem;
  Core::CPUThreadGuard guard(sys);
  int bad = 0;
  long vertices = 0;
  s_avx = true;

  for (int morph = 0; morph < 2; ++morph)
  for (int trial = 0; trial < 3000; ++trial)
  {
    Fill();
    const u32 n = (trial % 50 == 0) ? 0 : (trial % 97 == 0 ? u32(-3) : u32(rng() % 40 + 1));
    const u32 cnt = s32(n) > 0 ? n : 0;
    vertices += cnt;
    u32 mats = 0x80110000, rec = 0x80120000, in = 0x80130000, out = 0x80140000, delta = 0x80150000,
        innrm = 0x80160000, outnrm = 0x80170000;
    const int alias = rng() % 16;
    if (alias == 1) out = in;
    if (alias == 2) outnrm = innrm;
    if (alias == 3) outnrm = out + 4;
    if (alias == 4) out = rec + 8;
    if (alias == 5) { out = mats + 64; }
    for (u32 v = 0; v < cnt; ++v)
    {
      const u32 rr = rec + v * 24;
      for (int j = 0; j < 4; ++j) W32(rr + j * 4, std::bit_cast<u32>(RandF(1)));
      for (int j = 0; j < 4; ++j) g_mem[rr + 0x10 + j] = u8(rng() % (trial % 3 ? 20 : 256));
      g_mem[rr + 0x14] = u8(rng() % 10 == 0 ? rng() % 7 : rng() % 5);
    }
    // Another build's constant offset now and then (as another region's would be).
    const s32 clear_at = trial % 3 ? -0x6158 : -s32(rng() % 0x7FF0) & ~3;
    s_clear_at[morph] = clear_at;
    const u32 sp = 0x802F0000, rtoc = 0x80288000;
    W32(rtoc + clear_at, trial % 5 ? 0 : std::bit_cast<u32>(RandF(3)));
    Ref ref;
    for (int i = 0; i < 32; ++i) { ref.gpr[i] = u32(rng()); ref.ps0[i] = RandF(5); ref.ps1[i] = RandF(5); }
    ref.gpr[1] = sp; ref.gpr[2] = rtoc; ref.gpr[3] = n; ref.gpr[4] = mats; ref.gpr[5] = rec; ref.gpr[6] = in;
    ref.gpr[7] = out; ref.gpr[8] = delta; ref.gpr[9] = innrm; ref.gpr[10] = outnrm;
    ref.lr = 0x80001234; ref.ctr = u32(rng()); ref.so = rng() & 1; ref.cr0 = 0;

    const auto before = Snap();
    // The native routine.
    auto& s = sys.ppc;
    for (int i = 0; i < 32; ++i) { s.gpr[i] = ref.gpr[i]; s.ps[i].ps0 = std::bit_cast<u64>(ref.ps0[i]); s.ps[i].ps1 = std::bit_cast<u64>(ref.ps1[i]); }
    CTR(s) = ref.ctr; LR(s) = ref.lr; s.so = ref.so; s.npc = 0;
    const bool slow = trial % 4 == 3;
    if (slow)
    {
      if (morph) Skin<true>(Slow{sys.mmu}, s, clear_at); else Skin<false>(Slow{sys.mmu}, s, clear_at);
    }
    else
    {
      if (morph) SkinMorphAccumulate(guard); else SkinAccumulate(guard);
    }
    const auto native = Snap();
    Restore(before);
    std::vector<u32> patched(morph ? std::begin(kSkinMorphAccumulate) : std::begin(kSkinAccumulate),
                             morph ? std::end(kSkinMorphAccumulate) : std::end(kSkinAccumulate));
    patched[morph ? 1 : 2] = (patched[morph ? 1 : 2] & 0xFFFF0000u) | (u32(clear_at) & 0xFFFF);
    const u32* code = patched.data();
    const u32 count = u32(patched.size());
    Interpret(ref, code, count, 0x801c0000, 0x801c0000, 0xFFFFFFFF);
    const auto reference = Snap();
    if (native != reference)
    {
      for (std::size_t i = 0; i < native.size(); ++i)
        if (native[i] != reference[i]) { std::printf("mem differs at +%zx (%02x vs %02x)\n", i, native[i], reference[i]); break; }
      bad += Fail(morph ? "morph memory" : "skin memory", trial);
      continue;
    }
    for (int i = 0; i < 32; ++i)
      if (i != 2 && s.gpr[i] != ref.gpr[i]) { std::printf("r%d %08x vs %08x\n", i, s.gpr[i], ref.gpr[i]); bad += Fail("skin gpr", trial); break; }
    if (CTR(s) != ref.ctr) bad += Fail("skin ctr", trial);
    if (s.cr.GetField(0) != ref.cr0) bad += Fail("skin cr0", trial);
    if (s.npc != ref.lr) bad += Fail("skin npc", trial);
  }

  for (int trial = 0; trial < 3000; ++trial)
  {
    Fill();
    const u32 n = u32(rng() % 33 + 1);
    vertices += n;
    // Pointers in the locked cache, as the game has them; sometimes overlapping.
    u32 r4 = kL1 + 0x1000, r5 = kL1 + 0x2000, r6 = kL1 + 0x3000, r7 = kL1 + 0x4000, r8 = kL1 + 0x5000, r9 = kL1 + 0x6000;
    const int alias = rng() % 6;
    if (alias == 1) r5 = r7;
    if (alias == 2) r6 = r8 + 4;
    if (alias == 3) r4 = r9;
    for (u32 v = 0; v < n; ++v) g_mem[r9 + v * 4 + 3] = u8(rng());
    const u32 sp = 0x802F0000, r15 = 0x80200000;
    Ref ref;
    for (int i = 0; i < 32; ++i) { ref.gpr[i] = u32(rng()); ref.ps0[i] = RandF(5); ref.ps1[i] = RandF(5); }
    ref.gpr[1] = sp; ref.gpr[3] = n; ref.gpr[4] = r4; ref.gpr[5] = r5; ref.gpr[6] = r6; ref.gpr[7] = r7;
    ref.gpr[8] = r8; ref.gpr[9] = r9; ref.gpr[15] = r15; ref.gpr[31] = trial % 7 ? 0x43300000 : u32(rng());
    ref.ps0[31] = ref.ps1[31] = trial % 9 ? 4503599627370496.0 : std::bit_cast<double>(u64(0x4330000000000000ull | (rng() & 0xFFF)));
    ref.ps0[28] = ref.ps1[28] = RandF(0.01);
    ref.cr0 = 4 | ref.so;  // cmpwi r3, 0 with r3 > 0
    ref.ctr = u32(rng());
    s_shells_end = 0x8011eb44 + 0x94;
    s_shells_scratch = trial % 3 ? 0x50 : 0x20 + 8 * s32(rng() % 32);
    std::vector<u32> shells(std::begin(kOffsetShells), std::end(kOffsetShells));
    for (int k : {4, 8}) shells[k] = (shells[k] & 0xFFFF0000u) | u32(s_shells_scratch);
    shells[6] = (shells[6] & 0xFFFF0000u) | u32(s_shells_scratch + 4);

    const auto before = Snap();
    auto& s = sys.ppc;
    for (int i = 0; i < 32; ++i) { s.gpr[i] = ref.gpr[i]; s.ps[i].ps0 = std::bit_cast<u64>(ref.ps0[i]); s.ps[i].ps1 = std::bit_cast<u64>(ref.ps1[i]); }
    CTR(s) = ref.ctr; s.cr.SetField(0, ref.cr0); s.npc = 0;
    if (trial % 4 == 3) Shells(Slow{sys.mmu}, s, s_shells_end, s_shells_scratch); else OffsetShells(guard);
    const auto native = Snap();
    Restore(before);
    Interpret(ref, shells.data(), u32(shells.size()), 0x8011eb44, 0x8011eb44, 0x8011eb44 + 0x94);
    const auto reference = Snap();
    if (native != reference) { bad += Fail("shells memory", trial); continue; }
    for (int i = 0; i < 32; ++i)
      if (s.gpr[i] != ref.gpr[i]) { std::printf("r%d\n", i); bad += Fail("shells gpr", trial); break; }
    for (int i : {4, 5, 6, 13, 27, 29, 30})
      if (s.ps[i].ps0 != std::bit_cast<u64>(ref.ps0[i]) || s.ps[i].ps1 != std::bit_cast<u64>(ref.ps1[i]))
      { std::printf("f%d %a vs %a\n", i, s.ps[i].PS0AsDouble(), ref.ps0[i]); bad += Fail("shells fpr", trial); break; }
    if (CTR(s) != ref.ctr) bad += Fail("shells ctr", trial);
    if (s.npc != 0x8011eb44 + 0x94) bad += Fail("shells npc", trial);
  }
  // The matching: a build with other displacements is found, and they are read;
  // one changed instruction elsewhere is not.
  {
    PowerPC::MMU::code = g_mem;
    std::memset(g_mem + 0x80400000, 0, 0x2000);
    const u32 at = 0x80400100, near_miss = 0x80401000;
    for (u32 i = 0; i < std::size(kOffsetShells); ++i)
    {
      W32(at + i * 4, kOffsetShells[i]);
      W32(near_miss + i * 4, kOffsetShells[i]);
    }
    W32(at + 4 * 4, 0x93E10060);
    W32(at + 6 * 4, 0x90610064);
    W32(at + 8 * 4, 0xC9A10060);
    W32(near_miss + 20 * 4, 0xECCD06F3);  // fmuls. instead of fmuls
    const std::vector<Section> sections{{0x80400000, 0x80402000}};
    const auto found = Find(guard, sections, kRoutines[2]);
    if (found.size() != 1 || found[0].address != at || found[0].displacement[0] != 0x60 ||
        found[0].displacement[1] != 0x64 || found[0].displacement[2] != 0x60)
      bad += Fail("matching", 0);
    for (u32 i = 0; i < std::size(kSkinAccumulate); ++i)
      W32(near_miss + i * 4, kSkinAccumulate[i]);
    W32(near_miss + 2 * 4, 0xC1A2A000);  // lfs f13, -0x6000(rtoc)
    const auto skin = Find(guard, sections, kRoutines[0]);
    if (skin.size() != 1 || skin[0].displacement[0] != -0x6000)
      bad += Fail("matching skin", 0);
    PowerPC::MMU::code = nullptr;
  }
  std::printf("%s: %d failures, %ld vertices\n", bad ? "FAIL" : "PASS", bad, vertices);
  return bad != 0;
}
