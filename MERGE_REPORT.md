# Why the fork's 2 commits can't be merged with upstream (and what to do)

**Repository:** `freeforall1932-design/black-pc` — a fork of `Spark198rus/black-pc`
**Date of analysis:** 2026-10-19
**Verdict:** This is **not a code problem and not a permissions problem**. It is **one conflicting hunk in `README.md`**, caused by two people editing the same "Known issues" list at the same time. Every other file the fork changed merges cleanly. The fix is a ~2-minute manual resolution.

**Status: resolved.** `upstream/main` was merged into `arena/4ca71caa-black-pc` as **`4866dac`** on 2026-10-19, keeping both sides (upstream's rewrite + the fork's Mods section and antivirus note). The result differs from `upstream/main` only by the fork's 8 research files. See §8.

---

## 1. Bottom line

| Question | Answer |
|---|---|
| How many commits does the fork have that upstream doesn't? | 2 (`f7ab3ab` + merge commit `8f8439d`) — but only **1 commit of actual content** |
| How many does upstream have that the fork doesn't? | 2 (`3fef279` + merge commit `3b878bd`) — **1 commit of actual content** |
| Do the branches share a base? | Yes: `4c84944` (the fork's parent, 2026-10-14) |
| Are they diverged? | Yes — GitHub's compare API reports `"status": "diverged"`, ahead 2 / behind 2 |
| How many files conflict? | **Exactly 1: `README.md`** |
| How many conflicting hunks? | **Exactly 1** (lines 188–209 of the merged file) |
| Any source-code conflicts? | **None.** All 7 other fork files auto-merge |
| Is the fork broken? | No. The research work is intact and already merged into the fork's `main` via PR #1 |
| What blocks the merge? | Upstream PR #11 rewrote the README (284+/91−, adding a Linux guide and a Portuguese translation). The fork appended an antivirus bullet to the same "Known issues" block that PR #11 reworded. Both sides changed the same lines → Git stops. |

**In one sentence:** the fork's research commits prevent a fast-forward sync, and the only reason a real merge fails is that the fork's README additions and upstream's README rewrite touch the same "Known issues" bullet list — documentation only, no code.

---

## 2. The actual commit topology

```
                    ┌─ f7ab3ab  "Add texture pack support, feasibility report, and doc fixes"   (fork research, 1 real commit)
4c84944 ────────────┤
"Revise README       └─ 8f8439d  Merge pull request #1 from freeforall1932-design/arena/972ccd37-black-pc
 for clarity"                      = fork main / HEAD
(base, 2026-10-14)
                    ┌─ 3fef279  "Revise README for improved setup and instructions"  (upstream, 1 real commit)
4c84944 ────────────┤
                    └─ 3b878bd  Merge pull request #11 from j0nL404/master
                                  = upstream/main
```

Both "2 commit" counts are inflated by merge commits. Verified with Git:

* `8f8439d^{tree} == f7ab3ab^{tree}` → the fork's merge commit adds **nothing** on top of the research commit.
* `3b878bd^{tree} == 3fef279^{tree}` → upstream's merge commit adds **nothing** on top of the README commit.

So each side has exactly **one** content change since the shared base, and they touch **different things except one file**:

| Side | Files changed since `4c84944` | Lines |
|---|---|---|
| Fork (`4c84944..HEAD`) | `README.md`, `TODO.md`, `loader/README.md`, `loader/src/common.h`, `loader/src/d3d/texture.cpp`, `loader/src/main.cpp`, `notes/dxt_decode_test.cpp`, `notes/issue-feasibility-report.md` | +524 / −3 |
| Upstream (`4c84944..upstream/main`) | **`README.md` only** | +284 / −91 |

Unique-file overlap check:

| File | Changed by fork | Changed by upstream |
|---|---|---|
| `README.md` | ✅ (+26) | ✅ (+284/−91) |
| `TODO.md` | ✅ | ❌ |
| `loader/README.md` | ✅ | ❌ |
| `loader/src/common.h` | ✅ | ❌ |
| `loader/src/d3d/texture.cpp` | ✅ | ❌ |
| `loader/src/main.cpp` | ✅ | ❌ |
| `notes/dxt_decode_test.cpp` | ✅ (new) | ❌ |
| `notes/issue-feasibility-report.md` | ✅ (new) | ❌ |

`README.md` is the **only** file both sides touched, and it is the only conflict.

---

## 3. Root cause: the exact conflicting hunk

The fork's README commit added 26 lines: an 18-line **"Mods"** section (texture packs, reload-blur workaround) plus an **8-line antivirus/Defender false-positive bullet** appended to the end of "Known issues".

Upstream's README commit rewrote the whole English README — new sections (`Windows Setup`, `Linux Setup (Distrobox + Wine 9 + DXVK Guide)`, `Controls`, `Settings`, `Legal & Credits`) plus a full **Portuguese translation** — and, as part of that rewrite, **reworded the exact same four "Known issues" bullets** the fork appended to, dropping parentheticals and emphasis:

```diff
-- The game was made for 30 fps; at 60 (the default) something may still run at the wrong speed (PC SETTINGS > DISPLAY >
-  FRAME RATE), but I didn't notice.
-- The game's on-screen prompts show Xbox controller buttons, also when you play with the keyboard.
+- The game was made for 30 fps; at 60 (the default) something may still run at the wrong speed.
+- On-screen prompts show Xbox controller buttons, even when playing with a keyboard.
 ...
-- **Only the NTSC-U build is supported** (`--force` tries another build, which will most likely fail).
+- **Only the NTSC-U build is supported.**
```

The fork left those four bullets untouched and inserted its antivirus bullet **immediately after them**. A three-way merge cannot separate "changed these four lines" (upstream) from "added a bullet right after these four lines" (fork) because the hunks overlap, so Git emits a conflict:

```
188  <<<<<<< HEAD
189
190  - The game was made for 30 fps; ... (PC SETTINGS > DISPLAY > FRAME RATE), but I didn't notice.
191  - The game's on-screen prompts show Xbox controller buttons, also when you play with the keyboard.
192  - Audio: ...
193  - **Only the NTSC-U build is supported** (`--force` tries another build, which will most likely fail).
194  - **Antivirus / Defender false positives.** ...            ← fork-only addition (8 lines)
...
204  =======
205  - The game was made for 30 fps; ... (compacted)
206  - On-screen prompts show Xbox controller buttons, even when playing with a keyboard.
207  - Audio: ... (compacted)
208  - **Only the NTSC-U build is supported.**
209  >>>>>>> upstream/main
```

Everything else in upstream's README rewrite (284 added / 91 removed lines) **auto-merged cleanly**, because it either adds new sections or edits text the fork never touched. The fork's "Mods" section also merged cleanly — it sits above the conflict.

**Reproduction (verified multiple ways):**

| Test | Result |
|---|---|
| `git merge upstream/main` on fork `main` (`8f8439d`) | `CONFLICT (content): Merge conflict in README.md` — 1 unmerged file |
| `git merge upstream/main` on the research commit directly (`f7ab3ab`) | Same single conflict → not an artifact of the merge commit |
| `git cherry-pick 3fef279` onto fork `main` | Same single conflict → confirms it is the README content, not the merge base |
| `gh api .../compare/main...Spark198rus:main` | `ahead_by: 2`, `behind_by: 2`, `status: diverged` |

---

## 4. Timeline: why this happened

All times UTC.

| Time | Event |
|---|---|
| 2026-10-14 13:08 | `4c84944` — last shared commit (upstream README tidy-up) |
| 2026-10-16 14:07 | Upstream contributor `j0nL404` authors `3fef279` (the big README rewrite) |
| 2026-10-16 14:14 | Upstream PR #11 opened |
| 2026-10-16 20:04 | Fork research commit `f7ab3ab` authored and fork PR #1 merged (`8f8439d`) — based on `4c84944`, **before** PR #11 landed |
| 2026-10-17 10:40 | Upstream maintainer merges PR #11 (`3b878bd`) — **14.6 hours after** the fork's merge |

The fork was correct when it was made. It simply could not see upstream's README rewrite because that rewrite was still sitting unmerged in PR #11 at the time. This is a normal "two parallel doc edits" collision, not a bad merge, a broken fork, or an incompatibility between the research and the source.

---

## 5. Why GitHub refuses to do it automatically

Two separate mechanisms combine:

1. **Fast-forward is impossible.** GitHub's "Sync fork" only fast-forwards when the fork's `main` contains no commits that upstream lacks. The fork has its own research commit, so the branches are *diverged* (`ahead 2 / behind 2`). GitHub therefore offers a merge, not a fast-forward.
2. **The merge conflicts.** GitHub runs the same three-way merge Git does and gets the same single `README.md` conflict, so its UI reports *"This branch has conflicts that must be resolved."*

Note that if you were instead trying to open a **PR from the fork's `main` into `Spark198rus/black-pc:main`**, GitHub would show the identical "Can't automatically merge" banner for the identical reason — the fork's README additions and upstream's README rewrite collide. There is no separate code-level blocker.

---

## 6. What is *not* the cause (ruled out)

| Suspect | Finding |
|---|---|
| Binary/code conflicts in the texture-pack work | None. `loader/src/d3d/texture.cpp`, `main.cpp`, `common.h` all merge cleanly; upstream changed no source files since the base |
| New/deleted files colliding | None. The fork's two new files (`notes/dxt_decode_test.cpp`, `notes/issue-feasibility-report.md`) don't exist upstream |
| Branch protection / rulesets | No rulesets (`[]`), and merge-commit, squash and rebase are all enabled on the fork. (The branch-protection read endpoint returned 403 to the CI token, so protection rules can't be fully enumerated, but nothing in the settings blocks a merge.) |
| Repo permission / ownership | The token has `admin: true` on the fork |
| Git LFS / submodules / huge files | Not involved; the conflict is plain text |
| Fork is stale/broken | No — fork `main` is exactly the merged research PR #1; the working tree is clean |
| Upstream removed the fork's work | No — upstream never had the texture-pack change; the fork's 26 README lines and 8 files are intact |

---

## 7. Impact: low, but do not resolve it carelessly

* **Code impact: zero.** Upstream's only change since the base is one README. Merging cannot break the loader or the texture-pack code, and the fork's code is untouched by upstream.
* **Documentation impact: real.** A careless resolution silently loses content:
  * Choosing **"ours"** (fork) would discard upstream's entire rewrite — the Linux/Distrobox/DXVK guide and the *Português* translation (284 added lines).
  * Choosing **"theirs"** (upstream) would silently delete the fork's antivirus/Defender false-positive note (8 lines). The "Mods" section survives either way because it is outside the conflict hunk.

---

## 8. Resolution (applied)

Keep upstream's rewritten README, then re-add the fork-only antivirus bullet at the end of the "Known issues" list. Result: upstream's Linux guide + Portuguese translation **and** the fork's Mods section and AV note.

```bash
cd black-pc
git remote add upstream https://github.com/Spark198rus/black-pc.git   # once
git fetch upstream
git switch main                                                       # fork's main
git merge upstream/main
# edit README.md: in the single "Known issues" hunk, keep the upstream side
# (the four compacted bullets) and append the fork's antivirus bullet underneath.
git add README.md
git commit                                                            # merge commit
git push origin main                                                  # + fast-forward the working branch
```

The final section should read:

```markdown
## Known issues
- The game was made for 30 fps; at 60 (the default) something may still run at the wrong speed.
- On-screen prompts show Xbox controller buttons, even when playing with a keyboard.
- Audio: the reverb is an approximation, some effect sends are not mixed, there is no subwoofer channel, and 3D sound uses distance and panning only.
- **Only the NTSC-U build is supported.**
- **Antivirus / Defender false positives.** Some scanners (Microsoft Defender, Malwarebytes) have flagged release
  builds with generic heuristic labels (`Trojan:Script/Wacatac.H!ml`, `W32.Malware.1E0023F6`
  MachineLearning/Anomalous). These are heuristic detections on an unusual, AI-assisted recomp/decomp binary, not a
  known, reproducible payload; no one has reported an actual infection. If your scanner flags it: check the SHA-256
  of what you downloaded against the release page, build from source yourself instead if you'd rather not trust the
  binary, and consider reporting the false positive to your AV vendor (Microsoft's submission page for Defender,
  Malwarebytes' forum) so it gets whitelisted for everyone. Longer term this needs code-signing and vendor
  submissions on the maintainer's side, which is outside what a source change can fix.
```

Verification after resolving:

```bash
grep -c '^<<<<<<<'  README.md      # 0
grep -c '^## Mods'  README.md      # 1
grep -c 'português' README.md      # 2 (nav link + section)
grep -c 'Antivirus' README.md      # 1
grep -c '^## Linux Setup' README.md # 1
git diff --name-only --diff-filter=U  # empty
```

**Effort:** one hunk, about two minutes. **Risk:** none to the build.

### Alternatives

* **Resolve in the GitHub web editor:** in your fork, open a PR with base `main` and head `Spark198rus:main` ("Compare across forks", shown if GitHub permits that direction for your account). GitHub displays the same single conflict and offers **Resolve conflicts**; edit the same hunk there. If that direction isn't offered, use the local commands above — the result is identical.
* **Do nothing:** the fork remains fully usable and self-consistent. The gap is documentation only (Linux setup + pt-BR translation). The research work is already merged in the fork, so nothing is lost by waiting.

---

## 9. Outlook: sync soon

Upstream has moved beyond docs since this analysis. Open PRs there already target real code:

| PR | Title | Status |
|---|---|---|
| #28 | Support the PAL Europe release alongside NTSC-U | open |
| #26 | Native Linux x86 runtime (POSIX HLE, SDL2, DXVK-Native) | open |
| #22 | Disable reload blur, add tactical lean | open |
| #16 | Bigbuf | open |

The moment any of those touches `loader/src/`, conflicts with the fork's texture-pack changes (`texture.cpp`, `main.cpp`, `common.h`) become real code conflicts instead of a single README hunk. Right now the merge is documentation-only and essentially free; resolving it today keeps the fork's research applicable to upstream.

---

## Appendix A — exact commands used to verify

```bash
# 1. Is the fork actually diverged?
gh api repos/Spark198rus/black-pc/compare/main...freeforall1932-design:main \
  --jq '{status,ahead_by,behind_by,total_commits,merge_base_commit}'
# → {"ahead_by":2,"behind_by":2,"status":"diverged",
#    "merge_base_commit":{"sha":"4c84944..."},"total_commits":2}

# 2. What does each side change?
git diff --name-status 4c84944..HEAD
git diff --name-status 4c84944..upstream/main      # → M README.md (only)

# 3. Attempt the merge
git switch -c test HEAD && git merge --no-commit --no-ff upstream/main
# → Auto-merging README.md
#   CONFLICT (content): Merge conflict in README.md
#   Automatic merge failed; fix conflicts and then commit the result.

git diff --name-only --diff-filter=U               # → README.md
grep -c '^<<<<<<<' README.md                       # → 1
```

## Appendix B — merge commits are not content

```bash
git rev-parse 8f8439d^{tree} f7ab3ab^{tree}   # identical → fork merge adds nothing
git rev-parse 3b878bd^{tree} 3fef279^{tree}   # identical → upstream merge adds nothing
```

Therefore "2 commits on each side" is one real commit each; nothing was double-counted or lost.
