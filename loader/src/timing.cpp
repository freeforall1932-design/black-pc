// The Xbox time-stamp counter. XAPI's QueryPerformanceCounter is a bare `rdtsc` and its
// QueryPerformanceFrequency a constant 733,333,333 Hz (the Xbox CPU clock); the game's own timer helpers
// make the same assumption. On a PC `rdtsc` ticks at the host clock (3-5 GHz), so everything the game
// times in seconds (fades, blur ramps) ran several times too fast. These patches return a 733.33 MHz
// counter derived from the host performance counter instead. The game's calibrated timer (FUN_00180c00,
// which measures raw `rdtsc` against QueryPerformanceCounter) then calibrates itself correctly.
#include "timing.h"

#include "common.h"
#include "image.h"
#include "xbe.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t XBOX_TSC_HZ = 733333333;

// The Xbox never sleeps with a game running, but the PC does: after a sleep (or a debugger break) the host counter
// has moved on by the whole pause, and the game saw one frame that long. So the counter the game reads moves by the
// host time between two readings, but by a second at most; the game reads it many times a frame.
uint64_t xbox_tsc()
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    static SRWLOCK lock = SRWLOCK_INIT;
    static int64_t last_host, game; // host counts
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&lock);
    if (!last_host) game = last_host = now.QuadPart;
    int64_t step = now.QuadPart - last_host;
    if (step > 0) {
        game += step < freq.QuadPart ? step : freq.QuadPart;
        last_host = now.QuadPart;
    }
    uint64_t t = (uint64_t)game;
    ReleaseSRWLockExclusive(&lock);
    if (step > 2 * freq.QuadPart)
        logf("timing: the clock jumped %.1f s (the PC slept?); the game sees 1 s", (double)step / freq.QuadPart);
    uint64_t f = (uint64_t)freq.QuadPart;
    return t / f * XBOX_TSC_HZ + t % f * XBOX_TSC_HZ / f;
}

// XAPI QueryPerformanceCounter (0x232da), stdcall
BOOL __stdcall xapi_QueryPerformanceCounter(LARGE_INTEGER *p)
{
    p->QuadPart = (LONGLONG)xbox_tsc();
    return TRUE;
}

void __cdecl store_tsc() { *(uint64_t *)(uintptr_t)0x002C0468 = xbox_tsc(); }
uint32_t __cdecl tsc_low() { return (uint32_t)xbox_tsc(); }

// The game's helpers keep ecx; LTCG callers may rely on that, so wrap the C functions. The original functions
// also leave the flags alone (`rdtsc` does not touch them, the rest is push/mov/pop), and a C call does not,
// so the wrappers keep those too. LTCG analysed these functions' bodies: what they preserve, callers may
// assume.
__declspec(naked) void store_tsc_thunk() // FUN_00084190: *(uint64 *)0x2c0468 = rdtsc
{
    __asm {
        pushfd
        push ecx
        call store_tsc
        pop ecx
        popfd
        ret
    }
}

__declspec(naked) void tsc_low_thunk() // FUN_000841a0: return (uint32)rdtsc
{
    __asm {
        pushfd
        push ecx
        call tsc_low
        pop ecx
        popfd
        ret
    }
}

struct Patch {
    uint32_t addr;
    uint8_t expect[6];
    size_t expect_len;
    const void *target;
    const char *what;
};

bool patch(const Patch &p)
{
    auto *code = (uint8_t *)(uintptr_t)p.addr;
    if (memcmp(code, p.expect, p.expect_len) != 0) {
        logf("timing: %s at 0x%08x does not match this build; not patched", p.what, p.addr);
        return false;
    }
    code[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)p.target - (p.addr + 5));
    memcpy(code + 1, &rel, 4);
    return true;
}

} // namespace

namespace {
// The world step the game is patched to (1/60 s, see install_60fps_patches), and the 1/30 s animation-frame
// quantum. BLACK's animation rates, its weapon intervals and its post-process hold times are authored in
// 30 fps frames, so at 60 fps anything that turns frames into seconds must keep using 1/30 (the refire
// rounding below, the reload-blur multiply at 0x17B314). Both need storage, not constexpr: the patches point
// the game's instructions at their addresses.
float g_frame_time = 1.0f / 60.0f;
float g_anim_frame = 1.0f / 30.0f;

// A disp32/imm32 operand at `at` inside the instruction at `insn` (whose first bytes must match).
struct Operand {
    uint32_t insn;
    uint8_t expect[6];
    uint32_t at, old_value;
};
} // namespace

// Weapon refire at 60 fps. FUN_001EEFD0 (weapon state machine, ebp = weapon user) fires when its timer
// [ebp+0xDC] is <= 0 (FUN_001F0210 subtracts the frame time first) and then sets the timer to the weapon's
// refire interval (e.g. 0.04 s). Checked once per frame, the interval is effectively rounded up to whole
// frames: at 30 fps 0.04 s fires every 2nd frame (66 ms, ~15 shots/s), at 60 fps every 3rd (50 ms, ~20/s).
// The original rate is the 30 fps rounding, so the timer gets the interval rounded up to whole 1/30 s steps
// (minus a little, so the last of the two 1/60 s frames fires and not one later).
//
// How many 1/30 s frames that is has to come from the game's own countdown, not from ceil(interval * 30): the
// timer is a float32 and the game subtracts 1/30 s from it once per frame, so near an exact multiple of
// 1/30 s the accumulated rounding decides whether the original fired on frame m or m+1 (at 0.2/0.3/0.4/0.5 s
// it is m+1, at 0.04/0.1/0.25 s it is m). Reproduce that countdown here and hand the 60 fps timer half of it;
// then the 60 fps beat is the 30 fps beat exactly, which is what "the original rate" means (the 30 fps run is
// the game's own code, so it is also what the Xbox did). Checked in notes/refire_interval_test.cpp with a
// model of both timers: 2 ms to 5 s in 1 ms steps, 4999 intervals, all matching. (Before that the ceil()
// formula fired one 1/30 s early on the 40 of them that are exact multiples of 0.1 s - 0.2/0.3/0.4/0.5 s and
// 1.5-5.0 s.)
extern "C" uint32_t __cdecl refire_interval(uint32_t bits)
{
    constexpr float kStep = 1.0f / 30.0f; // the frame time the game counted the timer down with at 30 fps
    constexpr int kLimit = 1000;          // 33 s: past that it is not a fire interval, use the plain rounding
    float t;
    memcpy(&t, &bits, 4);
    if (!(t > 0.0f)) return bits; // 0, negative or NaN: the game stores it as it is
    int frames = 0;
    for (float left = t; left > 0.0f && frames < kLimit; frames++) left -= kStep;
    float iv = frames < kLimit ? (float)frames / 30.0f - 0.0005f
                               : ceilf(t * 30.0f - 1e-3f) / 30.0f - 0.0005f;
    memcpy(&bits, &iv, 4);
    return bits;
}

// The instruction a thunk replaces is a plain store: it touches neither the flags nor an xmm register, so the
// game's code around the site may have both live, and the C call clobbers both (32-bit code has xmm0-xmm7
// only). The project checks this by hand elsewhere (mouselook's 0x1ECF76 is documented as "no xmm register is
// live"); here it costs one push and eight moves per shot to not depend on it. `movups` is the only SSE move
// in the port - if the assembler rejects it, four `movd`/`movaps`-free alternatives are in the audit notes.
#define REFIRE_THUNK(name, reg)                                                                                    \
    __declspec(naked) void name()                                                                                  \
    {                                                                                                              \
        __asm pushfd                                                                                               \
        __asm pushad                                                                                               \
        __asm sub esp, 128                                                                                         \
        __asm movups xmmword ptr [esp], xmm0                                                                       \
        __asm movups xmmword ptr [esp + 16], xmm1                                                                  \
        __asm movups xmmword ptr [esp + 32], xmm2                                                                  \
        __asm movups xmmword ptr [esp + 48], xmm3                                                                  \
        __asm movups xmmword ptr [esp + 64], xmm4                                                                  \
        __asm movups xmmword ptr [esp + 80], xmm5                                                                  \
        __asm movups xmmword ptr [esp + 96], xmm6                                                                  \
        __asm movups xmmword ptr [esp + 112], xmm7                                                                 \
        __asm push reg                                                                                             \
        __asm call refire_interval                                                                                 \
        __asm add esp, 4                                                                                           \
        __asm mov dword ptr [ebp + 0xDC], eax                                                                      \
        __asm movups xmm0, xmmword ptr [esp]                                                                       \
        __asm movups xmm1, xmmword ptr [esp + 16]                                                                  \
        __asm movups xmm2, xmmword ptr [esp + 32]                                                                  \
        __asm movups xmm3, xmmword ptr [esp + 48]                                                                  \
        __asm movups xmm4, xmmword ptr [esp + 64]                                                                  \
        __asm movups xmm5, xmmword ptr [esp + 80]                                                                  \
        __asm movups xmm6, xmmword ptr [esp + 96]                                                                  \
        __asm movups xmm7, xmmword ptr [esp + 112]                                                                 \
        __asm add esp, 128                                                                                         \
        __asm popad                                                                                                \
        __asm popfd                                                                                                \
        __asm ret                                                                                                  \
    }
REFIRE_THUNK(refire_edx, edx)
REFIRE_THUNK(refire_ecx, ecx)
REFIRE_THUNK(refire_eax, eax)

struct RefireSite {
    uint32_t addr;
    uint8_t expect[6]; // mov [ebp+0DCh], reg
    void (*thunk)();
};
const RefireSite kRefireSites[] = {
    {0x001EFC72, {0x89, 0x95, 0xDC, 0x00, 0x00, 0x00}, refire_edx}, // interval from the fire mode (+0xCC)
    {0x001EFC94, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx}, // interval (+0x20), main fire path
    {0x001EFCA6, {0x89, 0x85, 0xDC, 0x00, 0x00, 0x00}, refire_eax}, // interval (+0x20), first shot
    {0x001EFEF3, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx}, // interval (+0x20), other fire path
};

// The weapon fire rate is gameplay, not cosmetics: 60 fps must not start up if these sites do not match this
// build, or the weapon fires every 3rd 1/60 s frame (50 ms) instead of every 4th (66 ms), 33% too fast. So
// validate before anything is written: install_60fps_patches returning false puts the run back at 30 fps.
bool refire_sites_ok()
{
    for (const RefireSite &s : kRefireSites)
        if (memcmp((const void *)(uintptr_t)s.addr, s.expect, 6)) {
            logf("timing: weapon refire site 0x%08x does not match this build; 60 fps would fire too fast, staying at 30 fps",
                 s.addr);
            return false;
        }
    return true;
}

void apply_refire_patches()
{
    for (const RefireSite &s : kRefireSites) { // call thunk; nop (the 6-byte store it replaces)
        auto *code = (uint8_t *)(uintptr_t)s.addr;
        code[0] = 0xE8;
        int32_t rel = (int32_t)((uintptr_t)s.thunk - (s.addr + 5));
        memcpy(code + 1, &rel, 4);
        code[5] = 0x90;
    }
}

// Reload blur (the post-process controller at *0x2D1990 + 0x830A0, mode 1): its hold time is set at
// 0x17B314 to reload-animation length [anim+0x1C] (in 30 fps animation frames) * frame time (game
// +0x2013C, = 1/60 now) * 0.5 - c, so it halved at 60 fps and the blur cleared early. Use 1/30 there.
// Cosmetic only, so unlike the weapon rate a mismatch does not take 60 fps away: it is logged instead.
const uint8_t kBlurExpect[8] = {0xF3, 0x0F, 0x59, 0x82, 0x3C, 0x01, 0x02, 0x00}; // mulss xmm0, [edx+0x2013C]
bool blur_site_ok()
{
    return !memcmp((const void *)(uintptr_t)0x0017B314, kBlurExpect, 8);
}

void apply_blur_patch()
{
    auto *blur = (uint8_t *)(uintptr_t)0x0017B314;
    blur[3] = 0x05; // mulss xmm0, [disp32]
    uint32_t a = (uint32_t)(uintptr_t)&g_anim_frame;
    memcpy(blur + 4, &a, 4);
}

// ---------------------------------------------------------------------------------------------------------
namespace {
// Diagnostics for the next playtest (read-only). The frame-rate leftovers the port has not found yet hide in
// two places: other readers of a float 1/30 s constant, and readers of the game's frame-time field (game
// object +0x2013C - NOTES.md counts 44 sites, of which only the reload blur is known to be a 30 fps
// animation-frame conversion). Both are listed with the containing function from functions.csv (--symbols),
// so the log of one playtest says exactly what still has to be checked: notes/higher-fps-audit.md. The
// instruction patterns are heuristics - a line can be missing, and a value that looks like a reader can be
// data.
// ---------------------------------------------------------------------------------------------------------

constexpr uint32_t kThirtieth = 0x3D088889; // (float)(1.0 / 30.0)
constexpr uint32_t kFrameTimeField = 0x0002013C;

struct Scan {
    const uint8_t *base = nullptr; // the mapped XBE image
    uint32_t image_base = 0, size = 0;
    std::vector<uint32_t> lo, hi; // executable section ranges
    bool ok() const { return size != 0; }
    const uint8_t *p(uint32_t addr) const { return base + (addr - image_base); }
    bool exe(uint32_t addr) const {
        for (size_t i = 0; i < lo.size(); i++)
            if (addr >= lo[i] && addr < hi[i]) return true;
        return false;
    }
};

Scan make_scan()
{
    Scan s;
    const Xbe *xbe = image_xbe();
    if (!xbe || !xbe->size_of_image || xbe->size_of_image > 0x2000000) return s;
    s.base = (const uint8_t *)(uintptr_t)xbe->base;
    s.image_base = xbe->base;
    s.size = xbe->size_of_image;
    for (const XbeSection &sec : xbe->sections) {
        if (!(sec.flags & XBE_SEC_EXECUTABLE) || !sec.vaddr || !sec.vsize) continue;
        s.lo.push_back(sec.vaddr);
        s.hi.push_back(sec.vaddr + sec.vsize);
    }
    return s;
}

// What the instruction whose 4-byte operand starts at `at` does with the value, "" when the bytes before it
// are not a pattern this knows (data, or an instruction form not listed).
const char *sse_name(uint8_t op)
{
    static const struct {
        uint8_t op;
        const char *name;
    } kSse[] = {
        {0x10, "movss"}, {0x11, "movss (store)"}, {0x2A, "cvtsi2ss"}, {0x2E, "ucomiss"}, {0x2F, "comiss"},
        {0x51, "sqrtss"}, {0x58, "addss"},        {0x59, "mulss"},    {0x5A, "cvtss2sd"}, {0x5C, "subss"},
        {0x5D, "minss"},  {0x5E, "divss"},        {0x5F, "maxss"},
    };
    for (const auto &k : kSse)
        if (op == k.op) return k.name;
    return "";
}

const char *use_kind(const Scan &s, uint32_t at)
{
    auto u8 = [&](uint32_t a) { return (uint32_t)*s.p(a); };
    auto have = [&](uint32_t n) { return at - s.image_base >= n; };
    if (have(4) && u8(at - 4) == 0xF3 && u8(at - 3) == 0x0F && u8(at - 1) == 0x05) {
        const char *n = sse_name((uint8_t)u8(at - 2));
        if (*n) return n;
    }
    if (have(5) && u8(at - 5) == 0xF3 && u8(at - 4) == 0x0F && u8(at - 2) == 0x04 && u8(at - 1) == 0x25) {
        const char *n = sse_name((uint8_t)u8(at - 3));
        if (*n) return n; // the same with a SIB byte
    }
    if (have(4) && u8(at - 4) == 0xF2 && u8(at - 3) == 0x0F && u8(at - 1) == 0x05) return "movsd/mulsd/... (double)";
    if (have(2)) {
        uint32_t a = u8(at - 2), b = u8(at - 1);
        if (a == 0xD9 && b == 0x05) return "fld";
        if (a == 0xD8 && (b & 0xC7) == 0x05) return "fadd/fmul/fsub/fdiv";
        if ((a == 0xD9 || a == 0xDD) && (b == 0x1D || b == 0x15)) return "fst/fstp (store)";
        if (a == 0xC7 && b == 0x05) return "mov [this constant], imm32 (a store to the pool)";
    }
    if (have(1)) {
        if (u8(at - 1) == 0x68) return "push imm32";
        if (u8(at - 1) >= 0xB8 && u8(at - 1) <= 0xBF) return "mov reg, imm32";
    }
    // mov [mem], imm32: C7 /0 with a memory ModRM (a stack slot or a field the value is stored into). The
    // instruction length in front of the imm32 is 2 (+disp), or 3 (+disp) when a SIB byte is in the ModRM;
    // the register form (mod 3) is not a store to memory and the ends of the ranges are where other
    // instructions start, so a wrong length cannot match by luck.
    for (uint32_t n : {2u, 3u, 6u, 7u}) {
        if (!have(n + 1) || u8(at - n) != 0xC7) continue;
        uint32_t modrm = u8(at - n + 1), mod = modrm >> 6, rm = modrm & 7;
        if (rm == 4) { // a SIB byte follows
            if (mod == 3 || 3 + (mod == 1 ? 1 : (mod == 2 ? 4 : 0)) != n) continue;
        } else if (mod != 3) {
            if (2 + (mod == 0 ? (rm == 5 ? 4 : 0) : (mod == 1 ? 1 : 4)) != n) continue;
        } else
            continue;
        return "mov [mem], imm32";
    }
    return "";
}

// What the code does with a 4-byte value that is not a pointer: for the +0x2013C frame-time field the
// interesting encoding is a 32-bit displacement off a base pointer (a ModRM with mod=10 - [reg+disp32] or
// [reg+sib+disp32]); the field as a plain immediate is possible but rarer. "" = not a form this knows, which
// for a value found in .text means data or an instruction this does not decode - the caller labels it instead
// of dropping it, so nothing is hidden.
const char *field_ref_kind(const Scan &s, uint32_t at)
{
    auto u8 = [&](uint32_t a) { return (uint32_t)*s.p(a); };
    auto have = [&](uint32_t n) { return at - s.image_base >= n; };
    auto mod10 = [](uint32_t modrm) { return (modrm & 0xC0) == 0x80 && (modrm & 0x07) != 4; };
    auto sib = [](uint32_t modrm) { return (modrm & 0xC7) == 0x84; };

    // F3 0F <op> <ModRM mod=10> <disp32>: movss/mulss/... r, [reg+field]
    if (have(4) && u8(at - 4) == 0xF3 && u8(at - 3) == 0x0F && mod10(u8(at - 1))) {
        const char *n = sse_name((uint8_t)u8(at - 2));
        if (*n) return n;
    }
    // F3 0F <op> <ModRM mod=10 rm=4> <SIB> <disp32>
    if (have(5) && u8(at - 5) == 0xF3 && u8(at - 4) == 0x0F && sib(u8(at - 2))) {
        const char *n = sse_name((uint8_t)u8(at - 3));
        if (*n) return n;
    }
    static const uint8_t kInt[] = {0x01, 0x03, 0x09, 0x0B, 0x2B, 0x39, 0x3B, 0x85, 0x89, 0x8B, 0x8D};
    if (have(2) && mod10(u8(at - 1)))
        for (uint8_t op : kInt)
            if (u8(at - 2) == op) return "int insn, [reg+field]";
    if (have(3) && sib(u8(at - 2)))
        for (uint8_t op : kInt)
            if (u8(at - 3) == op) return "int insn, [reg+sib+field]";
    if (have(1)) {
        static const uint8_t kAlu[] = {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D};
        for (uint8_t op : kAlu) // ALU reg, imm32 (CMP/AND/... - a comparison against the field)
            if (u8(at - 1) == op) return "ALU reg, field (immediate)";
        if (u8(at - 1) == 0x68) return "push field (immediate)";
        if (u8(at - 1) >= 0xB8 && u8(at - 1) <= 0xBF) return "mov reg, field (immediate)";
    }
    return "";
}

// The sites the 60 fps patches redirect (see `ops` in install_60fps_patches).
const uint32_t kFrameRateSites[] = {0x0020C2A4, 0x00193EAE, 0x00212210};

void report_third_second_readers()
{
    Scan s = make_scan();
    if (!s.ok()) return;
    std::vector<uint32_t> consts;
    for (uint32_t off = 0; off + 4 <= s.size; off += 4)
        if (*(const uint32_t *)(s.base + off) == kThirtieth) consts.push_back(s.image_base + off);
    if (consts.empty()) {
        logf("timing: no float 1/30 s value anywhere in the image: the three patched sites are the only 1/30 users");
        return;
    }
    logf("timing: %zu place(s) in the image hold the 1/30 s float - constant pools and inlined values (first at "
         "0x%08x); how the code uses them:",
         consts.size(), consts[0]);
    int uses = 0, to_check = 0, shown = 0;
    for (uint32_t off = 0; off + 4 <= s.size; off++) {
        uint32_t at = s.image_base + off, v = *(const uint32_t *)(s.base + off);
        if (!s.exe(at)) continue;
        bool references = false;
        for (uint32_t c : consts) references |= v == c; // a reader of one of the copies...
        bool inlined = v == kThirtieth;                 // ...or the value itself, inlined into an instruction
        if (!references && !inlined) continue;
        if (inlined && off >= 6) { // the imm32 of a "mov [const], imm32" store: its disp32 line above says it
            uint32_t before = *(const uint32_t *)(s.base + off - 4), op = *(const uint16_t *)(s.base + off - 6);
            bool to_const = false;
            if (op == 0x05C7)
                for (uint32_t c : consts) to_const |= before == c;
            if (to_const) continue;
        }
        const char *kind = use_kind(s, at);
        if (!*kind) continue; // an address table, float data, or a form this does not know
        bool patched = false;
        for (uint32_t a : kFrameRateSites) patched |= at == a;
        uses++;
        if (!patched) to_check++;
        if (shown++ < 24)
            logf("timing:   0x%08x %s%s (%s)%s", at, kind, inlined ? " [inlined value]" : "", sym_name(at).c_str(),
                 patched ? "  [1/30 -> 1/60 at 60 fps]" : "  <- CHECK: is it frame-rate dependent?");
    }
    if (shown > 24) logf("timing:   ... and %d more", shown - 24);
    logf("timing: %d use(s) of a 1/30 s value: %d are the patched frame-rate sites, %d to check", uses,
         uses - to_check, to_check);
}

void report_frame_time_readers()
{
    Scan s = make_scan();
    if (!s.ok()) return;
    std::vector<uint32_t> sites;
    for (uint32_t off = 0; off + 4 <= s.size; off++) {
        uint32_t at = s.image_base + off;
        if (*(const uint32_t *)(s.base + off) == kFrameTimeField && s.exe(at)) sites.push_back(at);
    }
    if (sites.empty()) return;
    int reads = 0, unclassified = 0, shown = 0;
    for (uint32_t at : sites) {
        const char *k = field_ref_kind(s, at);
        if (*k) reads++;
        else unclassified++;
        if (shown++ < 24)
            logf("timing:   0x%08x %s%s (%s)", at, *k ? k : "not a form this decodes - data, or a reader to find by hand",
                 at == 0x0017B314 ? "  [reads 1/30 at 60 fps]" : "", sym_name(at).c_str());
    }
    if (sites.size() > 24) logf("timing:   ... and %zu more", sites.size() - 24);
    logf("timing: %zu use(s) of the frame-time field [object + 0x2013C]: %d classified (heuristic - classified by "
         "the bytes in front of them), %d not; 0x0017B314 (the reload blur) is the only one known to be a 30 fps "
         "animation-frame conversion and it reads 1/30 instead. Check the rest in frame-rate-dependent code "
         "(timers, animation, ramps) against the 30 fps game - notes/higher-fps-audit.md",
         sites.size(), reads, unclassified);
}

} // namespace

bool install_60fps_patches()
{
    if (!g_cfg.known_build) return false;
    uint32_t frame = (uint32_t)(uintptr_t)&g_frame_time;
    const Operand ops[] = {
        {0x0020C2A4, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x0020C2A8, 0x00295FC0}, // movss xmm0, [1/30] (NTSC frame time)
        {0x0020C2AC, {0xC7, 0x05, 0x00, 0x38, 0x2F, 0x00}, 0x0020C2B2, 0x21},       // mov [0x2F3800], 33 (ms per frame)
        {0x00193EAE, {0xF3, 0x0F, 0x59, 0x05, 0xC0, 0x5F}, 0x00193EB2, 0x00295FC0}, // mulss xmm0, [1/30]
        {0x00212210, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x00212214, 0x00295FC0}, // movss xmm0, [1/30] -> 0x2F57F4
    };
    // Validate everything before writing anything. A half-applied 60 fps mode is worse than 30 fps: the world
    // stepping 1/60 s while the pacing still flips every 2 vblanks (or a weapon firing too fast) cannot be
    // reported by the log, and main.cpp only falls back when this returns false.
    for (const Operand &o : ops)
        if (memcmp((const void *)(uintptr_t)o.insn, o.expect, 6) || *(uint32_t *)(uintptr_t)o.at != o.old_value) {
            logf("timing: 60 fps patch site 0x%08x does not match this build; staying at 30 fps", o.insn);
            return false;
        }
    if (!refire_sites_ok()) return false;
    bool blur = blur_site_ok();
    report_third_second_readers(); // before the patches: the known sites still point at the 1/30 constant here
    report_frame_time_readers();

    for (const Operand &o : ops) *(uint32_t *)(uintptr_t)o.at = o.old_value == 0x21 ? 16u : frame;
    apply_refire_patches();
    if (blur) {
        apply_blur_patch();
    } else {
        logf("timing: reload blur site 0x0017B314 does not match this build; at 60 fps the reload blur will clear "
             "early (the fire rate and the world speed are unaffected)");
    }
    logf("timing: world steps 1/60 s per frame (60 fps); weapon refire keeps the 30 fps rhythm");
    return true;
}

void install_timing_patches()
{
    const Patch patches[] = {
        {0x000232DA, {0x8B, 0x4C, 0x24, 0x04, 0x0F, 0x31}, 6, (const void *)&xapi_QueryPerformanceCounter, "QueryPerformanceCounter"},
        {0x00084190, {0x53, 0x0F, 0x31}, 3, (const void *)&store_tsc_thunk, "timer helper (store rdtsc)"},
        {0x000841A0, {0x83, 0xEC, 0x08, 0x53, 0x0F, 0x31}, 6, (const void *)&tsc_low_thunk, "timer helper (rdtsc low)"},
    };
    int n = 0;
    for (const Patch &p : patches) n += patch(p);
    logf("timing: %d of %zu rdtsc users now tick at 733.33 MHz (host QPC based)", n, sizeof patches / sizeof patches[0]);
    log_frame_rate_note();
}

namespace {
char g_frame_rate_note[200]; // what frame_rate_arg() rejected, for log_frame_rate_note()
} // namespace

// The frame rate asked for on the command line or in black.ini. Only 30 and 60 are rates the game has been
// made to step; anything else (a typo, or a request for a higher one - notes/higher-fps-audit.md explains what
// that would take) keeps the default instead of silently dropping to the original 30, which is what the
// parsers did before: `--fps 120` ran at 30 fps and said nothing.
int frame_rate_arg(int want)
{
    if (want == 30 || want == 60) return want;
    snprintf(g_frame_rate_note, sizeof g_frame_rate_note,
             "frame rate %d is not one of 30 or 60: keeping 60 (the world steps a fixed frame time, one step per "
             "frame; a higher rate is not a drop-in change, see notes/higher-fps-audit.md)",
             want);
    logf("timing: %s", g_frame_rate_note); // stderr until log_init: the parsers run before the log file is open
    return 60;
}

void log_frame_rate_note()
{
    static bool logged;
    if (g_frame_rate_note[0] && !logged) {
        logged = true;
        logf("timing: %s", g_frame_rate_note);
    }
}
