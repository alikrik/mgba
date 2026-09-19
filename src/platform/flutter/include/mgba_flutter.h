/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGBA_FLUTTER_H
#define MGBA_FLUTTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MGF_EXPORT __attribute__((visibility("default")))

#define MGF_PLATFORM_NONE -1
#define MGF_PLATFORM_GBA 0
#define MGF_PLATFORM_GB 1

// Key bits (GBA layout; GB cores use the same low 8 bits)
#define MGF_KEY_A 0
#define MGF_KEY_B 1
#define MGF_KEY_SELECT 2
#define MGF_KEY_START 3
#define MGF_KEY_RIGHT 4
#define MGF_KEY_LEFT 5
#define MGF_KEY_UP 6
#define MGF_KEY_DOWN 7
#define MGF_KEY_R 8
#define MGF_KEY_L 9

MGF_EXPORT const char* mgf_version(void);

// Sets up config, save and savestate directories under dataDir. Call once before loading a ROM.
MGF_EXPORT int mgf_init(const char* dataDir);

// Loads a ROM (stopping any running game first). Returns an MGF_PLATFORM_* value, or MGF_PLATFORM_NONE on failure.
MGF_EXPORT int mgf_load_rom(const char* path);
MGF_EXPORT void mgf_get_video_size(int* width, int* height);

MGF_EXPORT int mgf_start(void);
MGF_EXPORT void mgf_pause(int paused);
MGF_EXPORT void mgf_reset(void);
MGF_EXPORT void mgf_stop(void);
MGF_EXPORT int mgf_is_running(void);

MGF_EXPORT void mgf_set_touch_keys(uint32_t keys);

// Sets an mGBA config option (e.g. "volume", "mute", "frameskip", "gb.model", "gba.bios",
// "useBios", "skipBios", "gb.pal[0]"). A NULL or empty value unsets it. Options are kept for
// the whole session and re-applied on every ROM load; live-reloadable ones take effect at once.
MGF_EXPORT void mgf_set_config(const char* key, const char* value);

// ratio > 0 runs at that multiple of normal speed (audio-paced); ratio <= 0 is unlimited.
MGF_EXPORT void mgf_set_fast_forward(int enabled, float ratio, int mute);

// Frames emulated since the game started (for an FPS counter).
MGF_EXPORT uint32_t mgf_frame_count(void);

// Save states live in <dataDir>/states/<rom>.ss<slot>. Saving also writes <rom>.ss<slot>.thumb:
// "MGFT", uint32 width, uint32 height (little endian), then width*height RGBA8888 pixels.
// Loading a state restores RTC but never overwrites the battery save.
MGF_EXPORT int mgf_save_state(int slot);
MGF_EXPORT int mgf_load_state(int slot);

#ifdef __cplusplus
}
#endif

#endif
