/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGF_TEST_STUBS_H
#define MGF_TEST_STUBS_H

#include <mgba-util/common.h>
#include <mgba-util/image.h>

// Host replacements for video.c / audio.c, with hooks for the tests.

unsigned mgfStubFramesPosted(void);
// Copies the last posted frame (tightly packed); returns false if none was posted yet.
bool mgfStubLastFrame(mColor* out, size_t capacity, unsigned* width, unsigned* height);
// OR of every key mask the core had when a frame was posted, since the last reset.
uint32_t mgfStubKeysSeen(void);
void mgfStubResetKeysSeen(void);
bool mgfStubAudioRunning(void);

#endif
