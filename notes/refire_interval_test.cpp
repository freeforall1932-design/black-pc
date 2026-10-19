// Standalone check of refire_interval() from loader/src/timing.cpp (copied here verbatim, minus the __cdecl
// and the thunks around it) so it can be compiled and run on Linux with g++ instead of the MSVC-only loader.
// Not part of the port; delete once the loader build has been verified on Windows.
//
// What it checks: the weapon timer (weapon +0xDC) is a float32 that FUN_001F0210 subtracts the frame time
// from once per frame, and the state machine fires when it reaches 0. At 30 fps the original fires after N
// frames where N follows that float countdown (not the ideal ceil(interval * 30)); at 60 fps the patched
// interval must make it fire after exactly 2N frames, so the shot rhythm - and the muzzle flash, the recoil,
// the ammo drain - happens as often per second as it did on the Xbox.
//
// Also prints what the straightforward ceil(interval * 30) formula (the one the port used until 2026-10-16)
// would have done, i.e. which intervals it got wrong.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef _MSC_VER
#define __cdecl
#endif

// ---- verbatim copy of refire_interval from loader/src/timing.cpp ----
uint32_t __cdecl refire_interval(uint32_t bits)
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
// ---- end verbatim copy ----

// The formula this replaced (commit 1ec7488, 2026-10-05).
static uint32_t refire_interval_old(uint32_t bits)
{
    float t;
    memcpy(&t, &bits, 4);
    if (t > 0.0f) t = ceilf(t * 30.0f - 1e-3f) / 30.0f - 0.0005f;
    memcpy(&bits, &t, 4);
    return bits;
}

static uint32_t bits_of(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static float from_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

// The game's timer: starts at `start`, minus the frame time once per frame, fires on the frame that takes it
// to 0 or below. Float32, one rounding step per frame, like the game.
static int frames_to_fire(float start, float frame_time)
{
    float t = start;
    for (int k = 1; k <= 100000; k++) {
        t -= frame_time;
        if (t <= 0.0f) return k;
    }
    return -1;
}

int main()
{
    const float ft30 = 1.0f / 30.0f, ft60 = 1.0f / 60.0f;
    int checked = 0, mismatched = 0, differ_from_old = 0, old_wrong = 0;
    float first_bad = 0.0f, worst = 0.0f, worst_rate = 0.0f;

    for (int step = 2; step <= 5000; step++) { // 0.002 s to 5 s in 1 ms steps
        float d = step / 1000.0f;
        int k30 = frames_to_fire(d, ft30);                                  // the original beat
        int k60 = frames_to_fire(from_bits(refire_interval(bits_of(d))), ft60); // with the patch
        int old60 = frames_to_fire(from_bits(refire_interval_old(bits_of(d))), ft60);
        checked++;
        if (refire_interval(bits_of(d)) != refire_interval_old(bits_of(d))) differ_from_old++;
        if (k60 != 2 * k30) {
            if (!mismatched) first_bad = d;
            mismatched++;
        }
        if (old60 != 2 * k30) {
            old_wrong++;
            if (old60 < 2 * k30) { // fired early: the shot rate went up
                float rate = (float)(2 * k30) / (float)old60;
                if (rate > worst_rate) {
                    worst_rate = rate;
                    worst = d;
                }
            }
        }
    }
    printf("%d intervals (2 ms to 5 s): %d do not match the 30 fps rhythm%s\n", checked, mismatched,
           mismatched ? "" : "  <- all of them match");
    if (mismatched) printf("   first bad: %.4f s\n", first_bad);
    printf("the old ceil() formula missed %d of them; worst %s%.4f s, %.1f%% of the original shots/s\n", old_wrong,
           worst_rate > 1.0f ? "at " : "", worst, worst_rate * 100.0f);
    printf("the new one returns a different interval than the old one for %d intervals (the ones it fixes)\n",
           differ_from_old);

    // The reference cases from NOTES.md ("e.g. 0.04 s"), printed in full.
    const float refs[] = {0.02f, 0.04f, 0.05f, 0.075f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 1.0f, 2.4f};
    for (float d : refs) {
        int k30 = frames_to_fire(d, ft30);
        int k60 = frames_to_fire(from_bits(refire_interval(bits_of(d))), ft60);
        int old60 = frames_to_fire(from_bits(refire_interval_old(bits_of(d))), ft60);
        printf("D=%.3f s: interval %.6f s (old %.6f), 30 fps %3d frames = %7.2f ms, 60 fps %3d frames = %7.2f ms%s%s\n",
               d, from_bits(refire_interval(bits_of(d))), from_bits(refire_interval_old(bits_of(d))), k30,
               k30 * 1000.0 / 30.0, k60, k60 * 1000.0 / 60.0, k60 == 2 * k30 ? "  same" : "  DIFFERENT",
               old60 == 2 * k30 ? "" : " (old formula was wrong here)");
    }

    // The margin: how close the 60 fps timer comes to firing a frame later than intended. The 0.0005 s the
    // patch takes off must stay far above the accumulated float error (a few ulps).
    float min_slack = 1e9f;
    int min_step = 0;
    for (int step = 2; step <= 5000; step++) {
        float d = step / 1000.0f;
        float iv = from_bits(refire_interval(bits_of(d)));
        int k = frames_to_fire(iv, ft60);
        float slack = k * ft60 - iv; // > 0: the timer was at or below 0 on this frame
        if (slack < min_slack) {
            min_slack = slack;
            min_step = step;
        }
    }
    printf("smallest slack before the 60 fps timer fires an extra frame: %.6f s (at D=%.3f s); the patch "
           "subtracts 0.0005 s\n",
           min_slack, min_step / 1000.0f);

    // Non-finite / silly inputs must come back as they are.
    const uint32_t odd[] = {0x00000000u, 0x80000000u, 0xBF800000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u};
    for (uint32_t b : odd)
        if (refire_interval(b) != b) printf("NOT PASSED THROUGH: %08x -> %08x\n", b, refire_interval(b));
    printf("zero/negative/NaN inputs pass through unchanged (inf becomes inf)\n");
    return mismatched != 0;
}
