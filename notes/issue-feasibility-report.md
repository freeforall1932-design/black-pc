# Open issues on the source repo (Spark198rus/black-pc) — feasibility, recalculated

Source repo: `Spark198rus/black-pc` (this fork, `freeforall1932-design/black-pc`, is branched from it).
"Naive" feasibility = assuming full access to the toolset (REA, Ghidra MCP, universal-modder skills, the
`ai-game-modding-guides`) **and** the preconditions those tools need: the user's legally-owned Xbox `default.xbe`
dump, Ghidra itself, and a Windows + MSVC build environment to compile and playtest. "Recalculated" = what is
actually possible **in this sandbox, this session**, after reading the repo.

## The three things that collapse feasibility here

1. **No game binary.** `game/default.xbe` is not in the repo (by design — `.gitignore`, `CLAUDE.md`) and isn't in
   this sandbox. BLACK's code is proprietary Xbox game data; the user must dump their own disc. Any fix that needs
   reading the actual x86 machine code (gameplay values, HUD draw calls, the save-system crash site) needs that
   file, and it does not exist here.
2. **No Ghidra, no way to get it.** This sandbox's egress allowlist is `github.com`, `codeload.github.com`,
   `api.github.com`, `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org`. GitHub *release assets* — including
   Ghidra's own releases — are served from `objects.githubusercontent.com`, which is not reachable (tested: connection
   fails). So even the empty analysis tool can't be installed here, before the binary problem even comes up.
3. **No build toolchain.** `loader/CMakeLists.txt` hard-fails without MSVC (`message(FATAL_ERROR ...)`) because the
   loader uses MSVC-specific x86 inline asm and linker options. This sandbox is Linux with no `cmake`, no
   `clang-cl`/`cl.exe`, no mingw, no Wine. So nothing here can even be compiled, let alone run against a game to
   verify a fix. Every "done" item in `TODO.md` was verified by the user playing it on their own Windows PC — that
   loop isn't available to me.

What **is** available: the repo's own pre-extracted knowledge (`NOTES.md`, `notes/functions.csv`,
`ghidra_scripts/*.java`) — this is text derived from the binary, not the binary itself, and it's substantial (addresses,
struct offsets, shader passes already named). Where an issue's fix only needs editing loader C++ against already-documented
facts, I can draft that confidently even without the binary. Where it needs *new* RE (an address/offset nobody has
found yet), I cannot find it here — that step is blocked by points 1 and 2 above, full stop.

## Per-issue feasibility

| # | Issue | Naive (ideal env) | Recalculated (this session) | Why |
|---|---|---|---|---|
| [#12](https://github.com/Spark198rus/black-pc/issues/12) | Support for higher FPS | 90% | **100% — already shipped** | `README.md`/`TODO.md` show 60 fps has been the default since 2026-10-12 (world step 1/60s, weapon refire/blur matched). Nothing to build; just confirm with the reporter and close. |
| [#1](https://github.com/Spark198rus/black-pc/issues/1) | Widescreen fixes (HUD stretch, FMV letterbox) | 65% | **~20%** | `NOTES.md` already documents the general widescreen flag and one UI-rect-shift function (`FUN_00113E70`, menus only), but the *gameplay* HUD (health/ammo bars) and FMV scaling are separate, undocumented draw paths. Finding them needs fresh decompilation of the real binary — blocked. Also worth noting: this may partly be the *original 2006 Xbox game's own* widescreen behavior (common for the era), not a porting bug, which changes the scope from "fix" to "new feature". |
| [#6](https://github.com/Spark198rus/black-pc/issues/6) | Crash on save/autosave | 75% | **~15%** | Unusually good repro data is already in the issue: exact EIP (`0xc49fe`), the faulting opcode (`89 0a` = `mov [edx],ecx`), a full register dump, and the preceding log line (`StartSaveProfile` right after `StartAutosaveSelect`). That's exactly what Ghidra/REA-style tooling is for — decompile `0xc49fe`, see what should have initialized `edx`. But I can't open the binary at all, so I can only hypothesize (most likely something in the HLE's HDD/file emulation returns data the real Xbox save flow doesn't expect, especially since the reporter suspects a Linux/Wine angle). A confident fix needs the real analysis step. |
| [#8](https://github.com/Spark198rus/black-pc/issues/8) | Texture packs/mod support, red blood, less bulletsponge HP, LOD increase | 55% (mixed) | **~15-20% overall, uneven by sub-ask** | Texture-pack override (~45%): the loader already intercepts D3D texture calls (`loader/src/d3d/texture.cpp`), so a "load replacement texture by hash" layer is a plausible source-level addition on top of an *existing* hook point, no new RE strictly required — but still unverifiable without a build. Blood color / enemy HP / LOD (~10-15% each): these are specific data values or shader tints inside the game binary with no existing notes pointing at them; locating them needs fresh RE on the real `default.xbe` — blocked. |
| [#15](https://github.com/Spark198rus/black-pc/issues/15) | ReShade shader fix for blur removal | 40% (as a native toggle) | **~20%** | This is a user-contributed ReShade+ShaderToggler workaround, not a bug report — nothing to "fix" per se. Turning it into a native `--no-reload-blur` option is possible in principle (`NOTES.md` already names the blur ping-pong targets and the DOF composite pass), but telling *reload/hit* blur apart from other passes that reuse the same buffers needs confirmation against the real render loop, which needs a build+playtest I can't do here. Easiest real action: add the ReShade tip to the README/wiki — that I can do right now with no blockers. |
| [#9](https://github.com/Spark198rus/black-pc/issues/9) / [#10](https://github.com/Spark198rus/black-pc/issues/10) | AV false-positive reports (Defender/Malwarebytes) | 30% | **~10%** | This isn't a code bug; AI-assisted recomp/decomp binaries routinely trip ML heuristics (a maintainer and a commenter already say as much). Real remedies are process, not code: code-signing (needs a paid cert I can't obtain here), submitting samples to Microsoft/Malwarebytes for whitelisting (needs accounts/portals outside this sandbox's network allowlist), and possibly adding version-info/manifest metadata to the exe. I can draft the release-process recommendation; I can't execute vendor submissions or signing from here. |
| [#13](https://github.com/Spark198rus/black-pc/issues/13) | Support another XBE build (different checksum, "HDD ready" torrent set) | 25% | **~5%** | Needs a second `default.xbe` (a different, unverified build) to diff against the supported one and find its code-address deltas. That file doesn't exist here, can't be fetched (network allowlist, and it would be a pirated/undumped copy regardless), and the project explicitly scopes itself to one verified SHA-256 by design. |
| [#7](https://github.com/Spark198rus/black-pc/issues/7) | Flatpak version | 20% | **~5%** | The loader is Win32-only by hard requirement (MSVC inline asm, Direct3D 9, Win32 APIs). A Flatpak would have to bottle it with Wine/Proton, which is a packaging/ops project, not something buildable or testable from this Linux sandbox without Wine (not installed, and installing it is its own yak-shave outside scope). |
| [#2](https://github.com/Spark198rus/black-pc/issues/2) | AppImage + 64-bit support | 15% | **~5%** | Same constraint as #7, plus the maintainer already explained 32-bit is inherent to the original 2006 Xbox code (not a choice to revisit), and said AppImage is blocked on an unrelated Linux disk-I/O issue under Wine/Proton. Not something I can move forward without that environment. |
| [#14](https://github.com/Spark198rus/black-pc/issues/14) | Online play / local 2-player | 10% | **~2%** | BLACK shipped single-player only; there is no existing netcode to recover via RE — this would be new engineering (session management, state replication for physics/destructible geometry) layered on top of original single-player game code. Out of scope for "reverse-engineer and patch"; it's a from-scratch feature, independent of any tool access. |

## What I can actually deliver right now, in this session

Without the binary, Ghidra, or a build toolchain, the honest, non-speculative contributions I can make directly in
this repo are the four below. **Scope note:** these are all local to this fork (`freeforall1932-design/black-pc`,
branch `arena/972ccd37-black-pc`) — nothing was posted, commented, or closed on the source repo
(`Spark198rus/black-pc`) or on any of the other repos mentioned earlier (`universal-modder`, `rea-fork`,
`ghidra-mcp`, `ai-game-modding-guides`), per instruction.

- **Done: #12 (higher FPS) tracked as already resolved.** `TODO.md`/`README.md` already show 60 fps shipped as the
  default; no code change was needed. Since I'm not touching the source repo's tracker, this is just recorded
  here rather than closed upstream — the fork maintainer can do that if/when they want to.
- **Done: a native texture-pack mechanism for #8**, added at `loader/src/d3d/texture.cpp` (`--texture-pack <dir>` /
  `--dump-textures <dir>`, default `textures\` next to `black.exe`), documented in `README.md` (Mods),
  `loader/README.md`, and `TODO.md`. This only needed the *existing* D3D texture hook point, not new addresses in
  the game binary, so it was buildable from here — but it has **not been compiled as part of the loader**: there is
  no MSVC/Windows toolchain, Ghidra, or game dump in this sandbox (confirmed: `cmake`, `cl.exe`, `clang-cl`,
  `mingw`, `wine` are all absent here). One piece *was* independently verified: the new BC1/2/3 ("DXT1/3/5") block
  decoder used by `--dump-textures` is plain, portable C++ with no Windows/D3D dependency, so it was pulled out
  into `notes/dxt_decode_test.cpp`, compiled with the sandbox's own g++, and run against hand-built test blocks
  (solid colour, punch-through alpha, DXT3/DXT5 alpha ramps) — all checks pass. The rest of the feature (the WIC
  PNG I/O, the D3D texture creation, the CLI plumbing) is still only reviewed, not built. Treat the whole feature as
  a reviewed draft until it's built and playtested on Windows with a real copy of the game. Blood colour / enemy HP
  / LOD from the rest of #8 still need real addresses from the actual binary and are not attempted.

  **Self-audit findings (2026-10-17), fixed where possible without a compiler:**
  - Found and fixed: `tex_file_name()` used the 2-argument `swprintf_s(buf, fmt, ...)` template-overload form,
    which has no precedent anywhere else in this codebase (every other call site uses `swprintf(buf, count, ...)`
    with an explicit size — `d3d/device.cpp`, `fe/pc_settings.cpp`). Switched to that proven pattern rather than
    trust an MSVC template-overload behavior nothing else here relies on.
  - Checked and resolved (not a bug): `<wincodec.h>` is included with no companion `<objbase.h>`/`<combaseapi.h>`,
    and the project defines `WIN32_LEAN_AND_MEAN` (`CMakeLists.txt`), which normally hides `CoCreateInstance` /
    `CoInitializeEx` from a bare `<windows.h>`. This is a non-issue here because `loader/src/d3d/draw.cpp` already
    uses the exact same APIs (`CoCreateInstance`, `CoInitializeEx`, `IID_PPV_ARGS`, WIC PNG encode) with the exact
    same single `#include <wincodec.h>`, and that file is part of the already-working screenshot feature — so the
    include pattern is proven in this project, not something I introduced speculatively.
  - Checked and resolved (not a bug): `windowscodecs` and `ole32` are already linked for the `blackrt` target
    (`target_link_libraries`, `CMakeLists.txt`), so no build-script change was needed. An earlier draft of this
    patch added `#include <shlwapi.h>` for no reason (unused) and was removed before presenting the feature,
    since `shlwapi.lib` is not linked and would have been a real link error.
  - **Real, unfixed risk — worth flagging before this is built: synchronous disk I/O inside the texture cache's
    lock.** `create_and_upload()` (where the new dump/override hooks live) runs while `host_texture()` holds
    `g_cs`, the same critical section every texture fetch (including cache *hits*) goes through. The original code
    never touched disk there (Xbox textures come straight from already-resident memory); my addition is the first
    thing in that path to do `GetFileAttributesW` and, when a pack file matches, a full WIC PNG decode (or encode,
    for `--dump-textures`) synchronously, still inside that lock. In practice this only runs once per unique
    texture (the cache skips it afterward), so the exposure is concentrated at level/mission loads when many
    textures are created in a burst — but that is exactly when a large "HD texture pack" would have the most
    files to decode, and this project has a tracked history of chasing single-digit-millisecond frame-time
    regressions (`TODO.md`, the 60 fps "Round 1/2/3" notes). A real build should watch for load-time hitches with
    a populated `textures` folder, and if they show up, the fix is to move the file check/decode off this lock
    (e.g. pre-index the pack folder once into an in-memory set so the hot path is a map lookup, and/or decode on a
    background thread while the original Xbox texture is shown until it's ready).
  - **Real, unfixed risk — the dump/override key can collide.** Both features key filenames on `hash`, which is
    `sample_hash()` — the *existing* cache's cheap, sampled (not exhaustive) content hash, built only to answer
    "did this one resource change since last frame", not to uniquely identify a texture against every other
    texture in the game. For large textures it deliberately skips most bytes (`step = n > 8192 ? n / 4096 : 1`).
    Two different textures of the same size/format could in principle hash the same, which would make
    `--dump-textures` silently skip one of them, and — more importantly — could make `--texture-pack` apply one
    texture's replacement PNG to a different, unrelated texture with no error or crash, just a wrong-looking
    texture in game. Low probability, not zero, and only discoverable by testing against the real game with a
    sizeable pack. A follow-up should use a dedicated, stronger hash (e.g. a full 64-bit hash over the whole
    resource) for pack identity instead of reusing the staleness-check heuristic.
  - Minor, cosmetic only: a cube texture's hash is marked "already seen" in `maybe_dump_texture()` before the cube
    check short-circuits it (so a later non-cube texture that happened to collide with that hash would be skipped
    too) — harmless in practice, just imprecise bookkeeping. Override textures are created with one mip level and
    always `D3DFMT_A8R8G8B8` regardless of the original's compression (documented in the code comments as a
    known limitation, not attempted further). A failed `LockRect` while building an override silently returns a
    blank texture rather than falling back to the original — this mirrors the pre-existing behavior of the
    unmodified upload path in the same file, not a new failure mode.
- **Done: the ReShade/ShaderToggler tip for #15** added to `README.md` (Mods) as a documented workaround, with a
  link back to the source issue for the exact files. A *native* blur toggle is still TODO (needs a build to
  confirm which passes are safe to skip).
- **Done: an AV false-positive note for #9/#10** added to `README.md` (Known issues), explaining the heuristic
  nature of the flags and the concrete next steps (checksum verification, building from source, vendor false-
  positive submission, eventual code-signing) — a process writeup, not a code fix, since a code-side cause isn't
  established.
- Everything else that needs new addresses/offsets (#1's HUD/FMV specifics, #6's root cause, #8's blood/HP/LOD,
  #13, #15's exact pass boundaries) still needs to happen on a Windows machine with the real `default.xbe`, Ghidra
  (or REA/ghidra-mcp against it), and a build — i.e., the next session should run there, not in this sandbox.
