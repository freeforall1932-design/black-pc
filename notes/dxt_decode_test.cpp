// Standalone unit test for the BC1/BC2/BC3 block decoder added to loader/src/d3d/texture.cpp
// (decode_dxt_block), copied here verbatim (minus the Xbox-format enum, replaced by plain ints) so it can be
// compiled and run on Linux with g++ to check the bit-twiddling independently of the Windows/D3D9 code around
// it, which cannot be built in this sandbox (no MSVC/Windows headers here). Delete this file once the real
// loader build has been verified on Windows; it is not part of the port itself.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

enum { XFMT_DXT1 = 1, XFMT_DXT3 = 2, XFMT_DXT5 = 3 };

uint32_t expand5(uint32_t v) { return (v << 3) | (v >> 2); }
uint32_t expand6(uint32_t v) { return (v << 2) | (v >> 4); }

// ---- verbatim copy of decode_dxt_block from loader/src/d3d/texture.cpp ----
void decode_dxt_block(const uint8_t *p, uint32_t fmt, uint32_t out[16])
{
    auto rgb565 = [](uint16_t c) -> uint32_t {
        return 0xFF000000u | expand5((c >> 11) & 31) << 16 | expand6((c >> 5) & 63) << 8 | expand5(c & 31);
    };
    auto lerp_byte = [](int a, int b, int wa, int wb, int wsum) -> uint8_t { return (uint8_t)((a * wa + b * wb) / wsum); };

    const uint8_t *alpha4 = nullptr;
    uint8_t a0 = 255, a1 = 0;
    uint64_t alpha_bits = 0;
    const uint8_t *cb = p;
    if (fmt == XFMT_DXT3) {
        alpha4 = p;
        cb = p + 8;
    } else if (fmt == XFMT_DXT5) {
        a0 = p[0];
        a1 = p[1];
        for (int i = 0; i < 6; i++) alpha_bits |= (uint64_t)p[2 + i] << (8 * i);
        cb = p + 8;
    }

    uint16_t c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8);
    uint32_t idx = cb[4] | (cb[5] << 8) | (cb[6] << 16) | (cb[7] << 24);
    uint32_t cols[4] = {rgb565(c0), rgb565(c1), 0, 0};
    const uint8_t *b0 = (const uint8_t *)&cols[0], *b1 = (const uint8_t *)&cols[1];
    bool punchthrough = fmt == XFMT_DXT1 && c0 <= c1;
    uint8_t *b2 = (uint8_t *)&cols[2], *b3 = (uint8_t *)&cols[3];
    for (int k = 0; k < 3; k++) {
        b2[k] = punchthrough ? lerp_byte(b0[k], b1[k], 1, 1, 2) : lerp_byte(b0[k], b1[k], 2, 1, 3);
        b3[k] = punchthrough ? 0 : lerp_byte(b0[k], b1[k], 1, 2, 3);
    }
    b2[3] = 0xFF;
    b3[3] = punchthrough ? 0 : 0xFF;

    for (int i = 0; i < 16; i++) {
        uint32_t texel = cols[(idx >> (2 * i)) & 3];
        if (alpha4) {
            uint8_t a = ((alpha4[i / 2] >> ((i & 1) * 4)) & 0xF) * 17;
            texel = (texel & 0x00FFFFFF) | ((uint32_t)a << 24);
        } else if (fmt == XFMT_DXT5) {
            uint32_t ai = (uint32_t)((alpha_bits >> (3 * i)) & 7);
            uint8_t a;
            if (a0 > a1) a = ai == 0 ? a0 : ai == 1 ? a1 : lerp_byte(a0, a1, 8 - ai, ai - 1, 7);
            else a = ai == 0 ? a0 : ai == 1 ? a1 : ai == 6 ? 0 : ai == 7 ? 255 : lerp_byte(a0, a1, 6 - ai, ai - 1, 5);
            texel = (texel & 0x00FFFFFF) | ((uint32_t)a << 24);
        }
        out[i] = texel;
    }
}
// ---- end verbatim copy ----

int fails = 0;
void check(const char *name, bool cond) { printf("%-45s %s\n", name, cond ? "ok" : "FAIL"); if (!cond) fails++; }
uint8_t B(uint32_t c) { return c & 0xFF; }
uint8_t G(uint32_t c) { return (c >> 8) & 0xFF; }
uint8_t R(uint32_t c) { return (c >> 16) & 0xFF; }
uint8_t A(uint32_t c) { return (c >> 24) & 0xFF; }

int main()
{
    // Test 1: DXT1, solid pure red, 4-colour mode (c0 > c1), all indices 0 -> every texel pure opaque red.
    {
        uint16_t red565 = (31 << 11); // R=31,G=0,B=0
        uint16_t black565 = 0;
        uint8_t block[8] = {(uint8_t)(red565 & 0xFF), (uint8_t)(red565 >> 8), (uint8_t)(black565 & 0xFF), (uint8_t)(black565 >> 8), 0, 0, 0, 0};
        uint32_t out[16];
        decode_dxt_block(block, XFMT_DXT1, out);
        bool ok = true;
        for (int i = 0; i < 16; i++) ok &= R(out[i]) == 255 && G(out[i]) == 0 && B(out[i]) == 0 && A(out[i]) == 255;
        check("DXT1 solid red (4-colour mode, index 0)", ok);
    }
    // Test 2: DXT1 punch-through (c0 <= c1): index 3 must be fully transparent black.
    {
        uint16_t c0 = 100, c1 = 200; // c0 < c1 -> punch-through / 3-colour mode
        uint8_t block[8] = {(uint8_t)(c0 & 0xFF), (uint8_t)(c0 >> 8), (uint8_t)(c1 & 0xFF), (uint8_t)(c1 >> 8),
                             0xFF, 0xFF, 0xFF, 0xFF}; // every texel selects index 3 (0b11 repeated)
        uint32_t out[16];
        decode_dxt_block(block, XFMT_DXT1, out);
        bool ok = true;
        for (int i = 0; i < 16; i++) ok &= out[i] == 0;
        check("DXT1 punch-through index 3 is transparent black", ok);
    }
    // Test 2b: same block, index 0 and 1 must reproduce the two endpoint colours exactly.
    {
        uint16_t c0 = (10 << 11) | (20 << 5) | 5, c1 = (3 << 11) | (40 << 5) | 15; // c0 > c1 in this encoding? check below
        if (c0 <= c1) std::swap(c0, c1); // force 4-colour mode for this check
        uint8_t block[8] = {(uint8_t)(c0 & 0xFF), (uint8_t)(c0 >> 8), (uint8_t)(c1 & 0xFF), (uint8_t)(c1 >> 8), 0x00, 0x00, 0x00, 0x00};
        uint8_t block1[8] = {(uint8_t)(c0 & 0xFF), (uint8_t)(c0 >> 8), (uint8_t)(c1 & 0xFF), (uint8_t)(c1 >> 8), 0x55, 0x55, 0x55, 0x55};
        uint32_t out0[16], out1[16];
        decode_dxt_block(block, XFMT_DXT1, out0);  // all indices 0 -> colour0
        decode_dxt_block(block1, XFMT_DXT1, out1); // all indices 1 (0b01 repeated = 0x55) -> colour1
        uint32_t expect0 = 0xFF000000u | expand5((c0 >> 11) & 31) << 16 | expand6((c0 >> 5) & 63) << 8 | expand5(c0 & 31);
        uint32_t expect1 = 0xFF000000u | expand5((c1 >> 11) & 31) << 16 | expand6((c1 >> 5) & 63) << 8 | expand5(c1 & 31);
        bool ok = out0[0] == expect0 && out1[0] == expect1;
        check("DXT1 4-colour mode: index 0/1 = endpoint colours", ok);
    }
    // Test 3: DXT5 alpha: a0=255, a1=0 (a0>a1, 6-step interpolation), index 0 -> 255, index 1 -> 0.
    {
        uint8_t a0 = 255, a1 = 0;
        uint64_t alpha_bits = 0; // every texel index 0
        uint8_t block[16];
        block[0] = a0; block[1] = a1;
        for (int i = 0; i < 6; i++) block[2 + i] = (uint8_t)(alpha_bits >> (8 * i));
        memset(block + 8, 0, 8); // colour block irrelevant to this check
        uint32_t out[16];
        decode_dxt_block(block, XFMT_DXT5, out);
        check("DXT5 alpha index 0 -> a0 (255)", A(out[0]) == 255);
    }
    // Test 4: DXT3 explicit 4-bit alpha: nibble 0xF -> 255, nibble 0x0 -> 0, exactly (no interpolation).
    {
        uint8_t block[16] = {0};
        block[0] = 0x0F; // texel0 nibble=0xF(->255), texel1 nibble=0x0(->0)
        uint32_t out[16];
        decode_dxt_block(block, XFMT_DXT3, out);
        check("DXT3 explicit alpha nibble 0xF -> 255", A(out[0]) == 255);
        check("DXT3 explicit alpha nibble 0x0 -> 0", A(out[1]) == 0);
    }
    printf(fails == 0 ? "\nALL OK\n" : "\n%d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}
