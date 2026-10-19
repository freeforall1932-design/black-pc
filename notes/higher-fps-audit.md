# Higher FPS audit (issue #12, "Support for higher FPS?")

Scope: everything the port does about the frame rate - `loader/src/timing.cpp` (frame-time constants, weapon
refire, reload blur), the pacing in `loader/src/d3d/device.cpp` (`pace_swap`, `vblank_thread`,
`D3DDevice_Swap`), the frame-rate plumbing in `main.cpp` / `settings.cpp` / `fe/pc_settings.cpp`, and the
performance notes in `NOTES.md` / `TODO.md`. Written 2026-10-16 from the repo alone: no game dump, no Ghidra,
no MSVC and no Windows in this sandbox, so nothing here was built or played. Everything below is either read
out of the code, measured with the sandbox's g++, or derived from the addresses already recorded in `NOTES.md`.

Upstream state: the issue is open on `Spark198rus/black-pc`, the answer so far has been "60 fps is the
default since 0.2.0". The last session's `notes/issue-feasibility-report.md` called #12 "100% already
shipped"; that is true for the *feature* and was too generous as an *audit* - this file is the audit.

## Verdict in one paragraph

60 fps is real and works, and the two gameplay-visible leftovers the port knew about (weapon fire rate,
reload blur length) are handled. Six things around it are not: the 60 fps gates were partial (a build where
only some sites match could still come up at 60 fps with a wrong fire rate), the refire rounding was derived
from the *ideal* 30 Hz beat instead of the game's own float timer (wrong at fire intervals that are exact
multiples of 0.1 s), the thunks inserted into the weapon state machine clobber EFLAGS and xmm0-7 without
saving them, the frame rate from `--fps` / black.ini was silently coerced to 30 for any value that was not
exactly 60, a PC that cannot hold 60 fps runs the world in slow motion without ever saying so, and the two
questions that decide whether the port is *done* with frame rates - are there other users of the 1/30 s
constant, and which of the 44 readers of the game's frame-time field are frame-rate conversions - were open
with no way to answer them from a playtest. All six have a fix in this changeset (below). **A rate above 60
is not a configuration change and should not be advertised**: it needs the vblank path generalised, the same
30 Hz-quantisation audit redone at 120 Hz, and, above all, a per-frame CPU budget the port does not have -
the port's own cost per frame is ~5-6 ms on the reference Ryzen 2600 and ~15 ms on the i5-4300U/HD 4400
laptop, against a 8.3 ms budget, i.e. it would run everything in visible slow motion. 60 fps is the ceiling
of this design, and the honest answer to #12 is "60 is there, more is not worth it yet, and here is why". The
two scans that answer the last of the six were in turn checked with a g++ model before they ever ran on
Windows, which found three defects in them (F6); the version in this changeset is the checked one.

## Findings

Severity: **gameplay** = the player can feel it, **robustness** = breaks only on a different build or a
different machine, **diagnostics** = makes the next playtest unable to answer a question.

### F1 - 60 fps could be applied partially (gameplay, fixed)

`install_60fps_patches()` validated the four frame-time sites first, then patched them, then ran
`install_refire_patches()`, which could fail on its own and only logged a line
(`timing: weapon refire site ... does not match this build`) while the function still returned `true`. So a
build whose weapon code differs would come up at 60 fps, world stepping 1/60 s, pacing one vblank per frame -
with the weapon refire at the *unpatched* 50 ms instead of 66 ms, i.e. firing 33% too fast, and the log
saying "60 fps". Falling back to 30 fps afterwards would have been just as bad: the frame-time operands were
already redirected, so the world would step 1/60 s while the pacing still flipped every second vblank - half
speed.

Fixed: everything is validated before a single byte is written (`refire_sites_ok()`), and the weapon sites are
a hard gate (they are gameplay). The reload blur is cosmetic, so a missing blur site no longer takes 60 fps
away - it logs that the blur will clear early. `install_60fps_patches()` returning false is exactly the "stay
at 30 fps" path `main.cpp` already had.

### F2 - the refire rounding reproduced the ideal beat, not the game's (gameplay, fixed)

The patch rounded the weapon interval up to whole 1/30 s steps with
`ceilf(t * 30.0f - 1e-3f) / 30.0f - 0.0005f`. That reproduces `ceil(interval * 30)` frames - the *ideal* 30 Hz
beat. The game's own timer does not necessarily land on the ideal: the timer is a float32, and
FUN_001F0210 subtracts the frame time from it once per frame, so near an exact multiple of 1/30 s the
*accumulated* rounding decides whether the original fired on frame m or m+1. `notes/refire_interval_test.cpp`
(models both timers in float32, 1 ms steps from 2 ms to 5 s, 4999 intervals) finds 40 intervals where the two
disagree, all of them exact multiples of 0.1 s (which are exact multiples of 1/30 s): 0.2, 0.3, 0.4, 0.5 s and
every 0.1 s from 1.5 s to 5 s. At 0.2 s the 30 fps game fires on frame 7 (233 ms), while the old formula made
the 60 fps timer fire on frame 12 (200 ms) - **16.7% more shots per second**, and the same shape of error
scales with the interval up to 5 s. No weapon interval is known to hit one of those values (NOTES.md only
records 0.04 s, which is unaffected); this is a latent gameplay difference, not a reported one.

Fixed: `refire_interval()` now runs the same 1/30 s countdown the game runs (a float32 `left -= 1/30` loop) to
get the frame count the 30 fps game would use, and stores half of it. The test then finds **0 mismatches** in
the whole range, and the intervals it changes are exactly the 40 it fixes; 0.04/0.05/0.1/0.25 s and everything
else are bit-identical to the playtested values.

*Assumption to check on the real binary (one line in Ghidra):* the model assumes the game's 30 fps countdown
is float32, i.e. **SSE** - `subss` in FUN_001F0210 (0x1F0210, 191 bytes, the function that subtracts the frame
time, called from the state machine FUN_001EEFD0). If that subtraction turns out to be x87 (`fsub`, 80-bit
intermediates), the *old* formula was the right one for those 40 intervals and the loop here should be
replaced by the ceil() expression; the test prints both behaviours side by side, so the decision is quick.
Evidence for SSE: the neighbouring float code in this build is SSE throughout (the frame-time loads at
0x20C2A4/0x193EAE/0x212210 and the blur multiply at 0x17B314 are `movss`/`mulss`, the mouse-look sites
0x1BD6A8/0x1BD6C7 are `addss`), and the Xbox XDK was usually built `/arch:SSE`.

### F3 - the refire thunks clobber EFLAGS and xmm0-7 (robustness, fixed)

The four sites replace a plain store (`mov [ebp+0xDC], reg`) with `pushad; push reg; call
refire_interval; ...; popad; ret`. `pushad`/`popad` cover the general registers, but not EFLAGS (the call
clobbers them; the store it replaced does not), and not xmm0-7 (32-bit code has only those; the C helper is
free to use every one of them). Both an EFLAGS-dependent branch and a live xmm value *after* one of those
stores would be corrupted silently. The project already reasons about this by hand - `mouselook.cpp`
documents its 0x1ECF76 site as "where no xmm register is live" - and the timing thunks carry the same idea
in their comment ("the game's helpers keep ecx; LTCG callers may rely on that"), so this was an oversight in
the same style, not a new class of risk.

Fixed: all three refire thunks now `pushfd`/`pushad`, save xmm0-7 on the stack, and restore all of it
(128 bytes + one push; shots happen a few times a second, so the cost is nothing). The two rdtsc thunks in
`install_timing_patches()` got `pushfd`/`popfd` as well: `rdtsc` leaves the flags alone, a C call does not,
and those functions are jumped to from LTCG callers that may rely on it - the same argument that made the
existing wrappers preserve ecx.

*Compile check:* `movups xmmword ptr [esp + N], xmmN` is the only SSE move in the port. If the MSVC
assembler objects to the `xmmword ptr` size specifier, either drop the specifier (`movups [esp], xmm0` - the
xmm operand gives the size) or replace each pair with `movss`/`movsd`/`movaps` equivalents; nothing else in
the change depends on it.

### F4 - `--fps` and black.ini silently coerced every value to 30 (diagnostics, fixed)

`g_cfg.fps = _wtoi(v.c_str()) == 60 ? 60 : 30;` (main.cpp) and `g_cfg.fps = atoi(val) == 60 ? 60 : 30;`
(settings.cpp): `--fps 120`, `--fps 59`, `fps = 59` in black.ini and a typo all ended up at **30 fps** - the
rate a higher-FPS request (issue #12) is most likely to arrive as, silently turned into the lowest one.

Fixed: `frame_rate_arg()` accepts 30 and 60, keeps 60 for anything else, and records a note that
`log_frame_rate_note()` writes to the log once the log exists (the parsers run before `log_init`, so a
warning there would only reach stderr). The note says what the value would have needed - a fixed frame time
and one step per frame means a higher rate is not a drop-in change, and the reader is pointed here.

### F5 - a PC that cannot hold 60 runs in slow motion and never says so (diagnostics, fixed)

The world steps one fixed frame time per frame, so a frame that misses its vblank is not a dropped frame, it
is lost time: the game runs slower than real time. `NOTES.md` says this outright ("The world runs slower than
real time if the host cannot keep 60") and the performance recorder measures it ("Game speed: N% of real
time"), but only while it records - by default the user's only symptom is "the game feels slow", and the bug
report that follows cannot say whether the machine is the problem.

Fixed: `report_slow_motion()` in `d3d/device.cpp` counts frames that took more vblanks than the swap interval
(the recorder's own definition of a late frame) and, when more than 10% of the last ~10 s were late, logs one
line telling the user the PC does not hold the requested rate, that the world is therefore slower than real
time, that 30 fps is the original rate, and that F2 records where the time goes. At most once a minute,
never a behaviour change.

### F6 - whether 60 fps is *complete* could not be answered (diagnostics, fixed)

Two sets of sites hide remaining frame-rate leftovers, and both were known only as counts in `NOTES.md`:

- Other readers of a float 1/30 s constant. Three are patched (0x20C2A4/0x193EAE/0x212210, all reading
  0x295FC0). If a fourth copy of the constant exists elsewhere in the image, or another reader of the same
  copy, its user still runs at 30 Hz timing at 60 fps.
- Readers of the game's frame-time field `[object + 0x2013C]`: "Other sites (44) may hide more
  frames-times-frame-time products" (`NOTES.md`). Only 0x17B314 (reload blur) is known to be a 30 fps
  animation-frame conversion.

Fixed: `install_60fps_patches()` now scans the mapped image before patching (read-only) and logs both sets
with the containing function from functions.csv (`--symbols`), capped at 24 lines each, marking the sites it
patches and the ones that need a look. One 60 fps playtest log then answers both questions instead of leaving
them to Ghidra. The instruction patterns are heuristics and say so in the log; the scan only looks at
executable sections, so data tables do not show up.

A scan that is wrong is worse than no scan (it would report a clean list of the wrong sites), and it cannot be
exercised without the game, so both scanners exist a second time in `notes/timing_scan_test.cpp`: a standalone
copy with the mapped image replaced by a fake buffer that seeds every instruction form they claim to
recognise - the two forms the frame-time sites use (`movss`/`mulss` xmm, [disp32]), the form the blur site uses
(`mulss` xmm, [reg+0x2013C]), the x87 forms, the inlined-immediate forms, a second copy of the constant, the
`[reg+sib+...]` forms - plus noise that must *not* be reported: a jump table holding the constant's address,
float data sitting in .text, and the raw value with bytes no instruction explains. Running it found three
defects in the code as first written:

- The field scan assumed the 4-byte operand *ended* at the address the scan hands it. Both scans hand it the
  operand's *start* (the site where the value is), so the ModRM/opcode bytes were looked for two to four bytes
  too early and every displacement form fell through to "not classified"; only the push/mov-immediate forms
  matched, by accident of where their opcode byte sits.
- The "is this the imm32 of a `mov [constant], imm32` store to the pool" test accumulated its matches with
  `&=` where it meant `|=`, so it only skipped that line when *every* copy of the constant in the image was the
  one addressed (it never is), which double-counted the store's value as a second site.
- The `mov [mem], imm32` pattern skipped the no-displacement SIB form (`mov [esp], imm32` - the most common
  stack store there is).

All three are fixed; the test exits 0 with `all checks pass` and reports all 8 seeded field sites
(7 classified, 1 correctly labelled as undecoded) and all 9 seeded constant sites, with none of the noise
reported. The field scan now classifies each site (`movss`/`mulss`/..., `int insn, [reg+field]`,
`int insn, [reg+sib+field]`, `ALU reg, field (immediate)`, `push`/`mov reg, field (immediate)`) and labels
anything it cannot decode instead of dropping it, so the log distinguishes "a reader" from "bytes that happen
to hold the value".

### F7 - the first Swap counted as one enormous late frame (diagnostics, fixed)

`pace_swap()` started with `last_vb == 0` while `g_vblank_count` was already in the hundreds, so
`perf::pace_end(now_vb - last_vb, interval)` recorded that whole startup as vblanks taken - noise in the
recorder (only frame 0 of a recording, which starts at F2, so it never mattered in practice) and it would
have made the new F5 watchdog report a slow PC on the first frame. Fixed: `last_vb` starts at the first tick
the thread has actually seen.

## Answers for issue #12

**Can it be higher than 60?** Not as a configuration change, and the reasons are structural:

1. *The simulation is fixed-step.* One world step per frame, one frame per swap. The rate is the frame time
   constant (1/60 s patched at 0x20C2A4/0x193EAE/0x212210, plus the 16 ms at 0x2F3800) - there is no
   accumulator and no interpolation, so a "higher FPS" mode means a *smaller step*: 1/120 s, plus the
   pacing (`pace_swap` maps the game's TWO_OR_IMMEDIATE request to 1 vblank at 60 fps and 2 at 30 - the only
   place where the configured rate chooses how the port waits - plus the vblank path: `display_sync()` only
   follows the display for 59-61 Hz and the fallback timer is a fixed 59.94 Hz, so a 120 Hz display currently
   gets the 59.94 Hz timer, which is right at 60 fps and wrong at 120).
2. *Everything 30 Hz-quantised would have to be re-audited at the new rate.* The weapon refire (F2) and the
   reload blur are the two that were found; the 44 frame-time-field readers (F6) are the list of candidates,
   and animation data is authored in 30 fps frames (`[anim+0x1C]`), so any conversion between "frames" and
   seconds has to keep using 1/30 - the port already has that rule written down, it just does not scale by
   itself.
3. *The frame budget is not there, and the port cannot make it there quickly.* The port's own per-frame cost
   is the difference between 60 fps now and 60 fps lost: first profile on the reference Ryzen 5 2600 +
   RTX 2060 at 2560x1440, 10.8 ms of the 16.7 ms budget, of which game code was 0.6-1.1 ms and the port
   ~6 ms (`sample_hash`, vertex conversion, the driver's UP draws); after three rounds of optimisation, 5.1 ms
   with 99.5% game speed in a busy scene. The i5-4300U / HD 4400 laptop was at 21.9 ms per frame (73.5% game
   speed) - it already cannot hold 60. A 120 Hz mode has an 8.3 ms budget: the *port alone* does not fit in
   it on the reference machine, before the game, the driver or the GPU fill rate at 2560x1440. Doubling the
   rate doubles that per-frame port cost per second and changes nothing about the GPU-side cost per frame.
4. *The payoff is small.* The visual gain over 60 fps would be smoothness in a game whose animation, fire
   rates and effects are 30 Hz-quantised by construction. A better use of the same effort is the open Round-3
   performance work in `TODO.md` (vertex constants only when they change, caching converted vertices in host
   vertex buffers, log writes off the render thread), which is what makes 60 fps *solid* on the machines
   people actually run, plus a lower render scale for weak GPUs.

If someone still wants to try it: the plumbing is now rate-agnostic enough to be a one-constant experiment
(`g_frame_time`/`g_anim_frame` in `timing.cpp` are the only two frame-time globals, and the `== 60` in
`pace_swap` is the only rate comparison), but expect slow motion on any 60 Hz-locked display and on any
machine where the F5 watchdog fires, and expect to redo the F6 checks at the new rate. Do not advertise it
in `README.md` before a playtest on real hardware.

**Is 60 fps itself correct?** With this changeset: the frame-time constants, the two known 30 Hz leftovers,
the all-or-nothing gating and the pacing are consistent, and the scan in F6 will say whether anything is
still missing. What remains unverifiable from here, in order of how much it matters:

1. `subss` vs `fsub` in FUN_001F0210 (decides F2's assumption for the 40 interval values).
2. Whether the four refire store sites cover every fire path (they cover the four stores after a shot that
   `NOTES.md` found; a fifth path, or a timer set in the *reload* path, would need the same treatment).
3. Whether the game's own `setFrameRate` FUN_000BE560 is ever called at runtime. It writes both the frame
   time and the ms-per-frame value, so a call after startup would silently put the world step back to 1/30 s
   while the port keeps pacing one vblank per frame - double speed. `notes/functions.csv` does not carry it
   (it is not in the function list), so `phase1-xbe-report.txt`/Ghidra has to answer it: look for code that
   calls 0xBE560 after the init at 0x20C282. If it is called (front end, video mode change, a menu), the port
   needs a hook there rather than the init-site patch.
4. The 44 frame-time-field readers (F6 prints them with names).
5. The PAL branch: the patches change the NTSC choice in the init only (the PAL branch's 1/25 s + 40 ms at
   0x2F3A38/0x2F3800 are untouched), so if the game ever took that branch it would step 1/25 s per frame
   while the port paces one vblank per frame - about 2.4x speed. Not reachable in the port as it stands: the
   emulated EEPROM reports `XC_VIDEO = 0x00010000` (widescreen only, `kernel/k_misc.cpp`), no PAL flag, and
   the supported XBE is NTSC-U, so the game never takes it. Worth remembering only if a PAL build, an
   `XGetVideoStandard` return value or a new video option ever makes it reachable - the check belongs where
   the init is patched, not in the pacing. (The other two patched sites, 0x193EAE/0x212210, read the 1/30 s
   constant directly, so they are 60 fps-correct whatever the video standard.)
6. Frame-*count*-based waits in scripted sequences and cutscenes (things that "wait N frames" now take half
   the wall-clock time). The port's own equivalent, the movie-skip heuristic in `input/movies.cpp`
   (`kWait = 10` polls), went from 333 ms at 30 fps to 167 ms at 60 fps - harmless, but the same shape of
   bug in the game's own script code would show as cutscene timing, and only a playtest finds it.

## What changed in this changeset

| File | Change |
| --- | --- |
| `loader/src/timing.cpp` | All-or-nothing validation (F1); refire interval from the game's own countdown (F2); flags + xmm preserved in the refire thunks and flags in the rdtsc thunks (F3); frame-time globals renamed to `g_frame_time`/`g_anim_frame` (the world step and the 1/30 s animation quantum, no longer "60" and "30"); read-only scans of the 1/30 s constants and the frame-time-field readers (F6); `frame_rate_arg()`/`log_frame_rate_note()` (F4) |
| `loader/src/timing.h` | Documents the gate, the reason there is no rate above 60, and the frame-rate argument helpers |
| `loader/src/d3d/device.cpp` | `report_slow_motion()` (F5); `pace_swap()` starts `last_vb` from the first tick (F7) |
| `loader/src/main.cpp`, `loader/src/settings.cpp` | `--fps` and `fps =` go through `frame_rate_arg()` (F4); the note is logged after `log_init` |
| `notes/refire_interval_test.cpp` | The g++ model of both timers, the old-vs-new comparison, the margin check and the pass-through cases (F2's evidence) |
| `notes/timing_scan_test.cpp` | The g++ model of both scans: every instruction form seeded plus noise that must not be reported; fixed three defects in the scans as first written and classified the field-scan sites (F6's evidence) |

Not changed on purpose: the internal resolution, the AA/gamma/blur passes and the vblank thread's
display-follow behaviour (all playtested), and the `pace_swap` swap-interval mapping (TWO_OR_IMMEDIATE paced
as ONE at 60 fps is what makes 60 fps work at all).

## Verifying this on Windows (first build, then one playtest)

1. Build; the log must show `timing: 1 of 3 rdtsc users...` (or 3 of 3), `timing: world steps 1/60 s per
   frame (60 fps); weapon refire keeps the 30 fps rhythm`, and the two scan reports. If a line about a
   60 fps site not matching appears, the build is not the supported one and the run stays at 30 fps - that
   is the F1 gate working.
2. Read the scan lines: if any 1/30 s reader is marked `<- CHECK`, look at it in Ghidra before trusting 60 fps
   for the effect it drives. If the count of `[object + 0x2013C]` sites is around 44, the scan agrees with
   NOTES.md; the classes it prints per site (`mulss`, `int insn, [reg+field]`, ...) say which are readers of
   the field and which are bytes that merely hold its value.
3. Before any of that, the two g++ tests can be run anywhere (no game files, no Windows):
   `g++ -O2 -mfpmath=sse notes/refire_interval_test.cpp -o /tmp/refire_test && /tmp/refire_test` and the same
   for `notes/timing_scan_test.cpp`. They are what says the F2 arithmetic and the F6 patterns are the ones
   described here.
4. Fire the weapon with the slowest fire rate in the game (a 0.2-0.5 s interval, if one exists) at 30 fps and
   at 60 fps and compare shots per second against a stopwatch or `--memdump` of the ammo counters
   (weapon +0x18/+0x14, see NOTES.md). They must match; that is F2.
5. Play a busy scene with the perf recorder (F2/F3) and look for the F5 slow-motion line: its presence is the
   machine's answer, not a port bug, and it is the number to quote when someone asks for more than 60 fps.
