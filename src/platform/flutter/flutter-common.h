/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGF_COMMON_H
#define MGF_COMMON_H

#include <mgba-util/common.h>

#include <mgba/core/log.h>
#include <mgba-util/image.h>

struct mCore;
struct mCoreSync;
struct ANativeWindow;

mLOG_DECLARE_CATEGORY(FLUTTER);

void mgfVideoSetWindow(struct ANativeWindow* window);
void mgfVideoPost(const mColor* pixels, size_t stride, unsigned width, unsigned height);

bool mgfAudioStart(struct mCore* core, struct mCoreSync* sync);
void mgfAudioPause(bool paused);
void mgfAudioStop(void);

void mgfSetPadKeys(uint32_t keys);

#ifdef MGF_TESTING
// Test-only access to bridge internals (host unit tests, see test/)
struct mCoreThread;
struct mCore* mgfTestCore(void);
struct mCoreThread* mgfTestThread(void);
#endif

#endif
