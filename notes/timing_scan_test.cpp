// Standalone check of the two diagnostics in loader/src/timing.cpp (report_third_second_readers, which finds
// users of the 1/30 s float, and report_frame_time_readers, which finds users of the object + 0x2013C
// frame-time field), copied here with a fake image instead of the mapped XBE so the instruction patterns can be
// checked on Linux with g++ - they are not exercised until a 60 fps run on Windows, and a wrong pattern would
// silently print a wrong list. Not part of the port; delete once a real 60 fps log has been read.
//
// The three known readers of the 1/30 s constant are seeded with the exact bytes from install_60fps_patches'
// expect[] arrays (movss/mulss xmmN, [0x295FC0]); the rest are the forms such a reader could take (x87, SIB
// addressing, an inlined immediate) plus noise that must NOT be reported (a table of addresses, float data in
// .text, a raw value).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- verbatim copy of Scan and use_kind from loader/src/timing.cpp (XBE access replaced by the fake) ----
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

// ---- end of the copy ----

static const uint32_t kThirtieth = 0x3D088889; // (float)(1.0 / 30.0)
static const uint32_t kThirtiethBits = kThirtieth;

static uint8_t g_image[0x30000];
static const uint32_t kBase = 0x10000;

static void put(uint32_t addr, const void *src, size_t n) { memcpy(g_image + (addr - kBase), src, n); }
static void put_code(uint32_t addr, const uint8_t *bytes, size_t n)
{
    put(addr - (uint32_t)n, bytes, n); // n bytes *before* the operand at addr
}

int main()
{
    Scan s;
    s.base = g_image;
    s.image_base = kBase;
    s.size = sizeof g_image;
    s.lo = {0x10000}; // .text
    s.hi = {0x20000};
    memset(g_image, 0xCC, sizeof g_image);

    const uint32_t pool = 0x29000; // where the 1/30 s constant lives (outside .text, like 0x295FC0)
    put(pool, &kThirtiethBits, 4);
    const uint32_t pool2 = 0x2A000; // a second copy, to check that both are scanned
    put(pool2, &kThirtiethBits, 4);

    struct Expect {
        uint32_t at;
        const char *kind;
    } expect[] = {
        {0x11008, "movss"},   // F3 0F 10 05 <pool>    - the frame-time init site, as in the code
        {0x11020, "mulss"},   // F3 0F 59 05 <pool>    - the 0x193EAE pattern
        {0x11038, "movss"},   // F3 0F 10 05 <pool2>   - a second copy of the constant
        {0x11050, "movss"},   // F3 0F 10 04 25 <pool> - SIB form
        {0x11068, "fld"},     // D9 05 <pool>          - x87 user
        {0x11078, "push imm32"},                  // 68 <bits>        - the value inlined into an instruction
        {0x11088, "mov reg, imm32"},              // B8 <bits>
        {0x11098, "mov [this constant], imm32 (a store to the pool)"}, // C7 05 <pool> <bits>
        {0x110A8, "mov [mem], imm32"},            // C7 45 F8 <bits>  - into a stack slot / field
    };
    auto emit = [&](uint32_t at, const uint8_t *prefix, size_t pn, uint32_t value) { put_code(at, prefix, pn); put(at, &value, 4); };
    const uint8_t movss[] = {0xF3, 0x0F, 0x10, 0x05};
    const uint8_t mulss[] = {0xF3, 0x0F, 0x59, 0x05};
    const uint8_t sib[] = {0xF3, 0x0F, 0x10, 0x04, 0x25};
    const uint8_t fld[] = {0xD9, 0x05};
    const uint8_t push[] = {0x68};
    const uint8_t movreg[] = {0xB8};
    const uint8_t store[] = {0xC7, 0x05};
    emit(0x11008, movss, 4, pool);
    emit(0x11020, mulss, 4, pool);
    emit(0x11038, movss, 4, pool2);
    emit(0x11050, sib, 5, pool);
    emit(0x11068, fld, 2, pool);
    emit(0x11078, push, 1, kThirtiethBits);
    emit(0x11088, movreg, 1, kThirtiethBits);
    put_code(0x11098, store, 2);
    put(0x11098, &pool, 4);
    put(0x1109C, &kThirtiethBits, 4);
    const uint8_t mov_mem[] = {0xC7, 0x45, 0xF8}; // mov dword ptr [ebp-8], imm32
    emit(0x110A8, mov_mem, 3, kThirtiethBits);

    // Noise that must not be reported: a table of addresses (a jump table holding the constant's address),
    // float data in .text, and the operand of an instruction this does not know.
    for (int i = 0; i < 4; i++) put(0x12000 + 4 * i, &pool, 4);
    for (int i = 0; i < 4; i++) put(0x12100 + 4 * i, &kThirtiethBits, 4);

    // The scan, as in report_third_second_readers().
    std::vector<uint32_t> consts;
    for (uint32_t off = 0; off + 4 <= s.size; off += 4)
        if (*(const uint32_t *)(s.base + off) == kThirtieth) consts.push_back(s.image_base + off);
    printf("places holding the 1/30 s float: %zu (2 pools + the inlined instructions that were\n        seeded: push, mov reg, the store's source and the stack slot = 6)\n", consts.size());

    std::vector<uint32_t> found;
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
        found.push_back(at);
        printf("timing:   0x%08x %s%s\n", at, kind, inlined ? " [inlined value]" : "");
    }

    int bad = 0;
    for (const Expect &e : expect) {
        bool ok = false;
        for (size_t i = 0; i < found.size(); i++) {
            if (found[i] != e.at) continue;
            const char *kind = use_kind(s, e.at);
            ok = !strcmp(kind, e.kind);
            printf("%s 0x%08x expected '%s', got '%s'\n", ok ? "ok  " : "FAIL", e.at, e.kind, kind);
        }
        if (!ok) bad++;
    }
    if (found.size() != sizeof expect / sizeof expect[0]) {
        printf("FAIL: %zu sites found, %zu expected (noise reported?)\n", found.size(), sizeof expect / sizeof expect[0]);
        bad++;
    } else {
        printf("ok   %zu sites found, none of the noise reported\n", found.size());
    }
    // ---- report_frame_time_readers(): the +0x2013C frame-time field ----
    const uint32_t field = 0x0002013C;
    struct FieldExpect {
        uint32_t at;
        const char *kind; // "" = the scan reports it as "not a form this decodes"
    } fexpect[] = {
        {0x11100, "mulss"},                               // F3 0F 59 82 <field>      mulss xmm0, [edx+field]
        {0x11110, "movss"},                               // F3 0F 10 84 8A <field>   movss xmm0, [edx+ecx*4+field]
        {0x11120, "int insn, [reg+field]"},               // 8B 82 <field>            mov eax, [edx+field]
        {0x11130, "int insn, [reg+sib+field]"},           // 89 84 8A <field>         mov [edx+ecx*4+field], eax
        {0x11140, "push field (immediate)"},              // 68 <field>
        {0x11150, "ALU reg, field (immediate)"},          // 3D <field>               cmp eax, field
        {0x11160, "mov reg, field (immediate)"},          // B8 <field>
        {0x11170, ""},                                    // the value in bytes no instruction here explains
    };
    const uint8_t mul_r[] = {0xF3, 0x0F, 0x59, 0x82};
    const uint8_t mov_sib[] = {0xF3, 0x0F, 0x10, 0x84, 0x8A};
    const uint8_t int_r[] = {0x8B, 0x82};
    const uint8_t int_sib[] = {0x89, 0x84, 0x8A};
    const uint8_t push_field[] = {0x68};
    const uint8_t alu_field[] = {0x3D};
    const uint8_t mov_field[] = {0xB8};
    const uint8_t unknown[] = {0xAB, 0xCD};
    emit(0x11100, mul_r, 4, field);
    emit(0x11110, mov_sib, 5, field);
    emit(0x11120, int_r, 2, field);
    emit(0x11130, int_sib, 3, field);
    emit(0x11140, push_field, 1, field);
    emit(0x11150, alu_field, 1, field);
    emit(0x11160, mov_field, 1, field);
    emit(0x11170, unknown, 2, field);

    std::vector<uint32_t> fsites;
    for (uint32_t off = 0; off + 4 <= s.size; off++) {
        uint32_t at = s.image_base + off;
        if (*(const uint32_t *)(s.base + off) == field && s.exe(at)) fsites.push_back(at);
    }
    int fbad = 0, classified = 0, unclassified = 0;
    for (uint32_t at : fsites) {
        const char *k = field_ref_kind(s, at);
        if (*k) classified++;
        else unclassified++;
    }
    printf("field scan: %zu site(s), %d classified, %d not\n", fsites.size(), classified, unclassified);
    for (const FieldExpect &e : fexpect) {
        const char *k = field_ref_kind(s, e.at);
        bool ok = !strcmp(k, e.kind);
        printf("%s 0x%08x expected '%s', got '%s'\n", ok ? "ok  " : "FAIL", e.at, e.kind, k);
        if (!ok) fbad++;
    }
    if (fsites.size() != sizeof fexpect / sizeof fexpect[0]) {
        printf("FAIL: %zu site(s) reported, %zu seeded\n", fsites.size(), sizeof fexpect / sizeof fexpect[0]);
        fbad++;
    } else {
        printf("ok   %zu site(s) reported, none missed and nothing extra\n", fsites.size());
    }
    bad += fbad;

    printf("%s\n", bad ? "FAILED" : "all checks pass");
    return bad != 0;
}
