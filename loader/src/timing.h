// Game timing: make the Xbox time-stamp counter users tick at the Xbox CPU clock (733.33 MHz).
#pragma once

void install_timing_patches(); // after image_map; known build only

// 60 fps: BLACK steps its world by a fixed frame time chosen at start-up from the video standard (game init
// 0x20C282: NTSC 1/30 s and 33 ms, PAL 1/25 s and 40 ms, into 0x2F3A38 / 0x2F3800; the game's own
// setFrameRate FUN_000BE560 does the same for any rate). Make the NTSC choice 1/60 s and 16 ms, and point the
// two other uses of the 1/30 constant (0x295FC0) at 1/60. Returns false if the code does not match, so the
// run stays at 30 fps - and also when the weapon refire sites do not match (a 60 fps run without them fires
// 33% too fast). Nothing is patched unless everything validated first, and it logs the other readers of a
// 1/30 s constant and the sites that read the game's frame-time field (the checks in
// notes/higher-fps-audit.md) before it touches anything.
//
// Why there is no rate above 60: the world steps one fixed frame time per frame, so a frame that misses its
// vblank is lost time (slow motion), not a dropped frame, and the port's own per-frame cost (draw
// translation, vertex conversion) is the frame budget. See notes/higher-fps-audit.md.
bool install_60fps_patches();

// The frame rate from the command line or black.ini: 30 or 60, anything else keeps the default (60) and is
// logged later by log_frame_rate_note() (the parsers run before the log exists). It used to be silently
// coerced to 30, so a request for a higher rate got the lowest one.
int frame_rate_arg(int want);
void log_frame_rate_note(); // after log_init: what frame_rate_arg() rejected, if anything
