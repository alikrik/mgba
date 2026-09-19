/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "flutter-common.h"
#include "test/stubs.h"

#include <mgba/core/core.h>
#include <mgba/core/sync.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/threading.h>

#include <stdatomic.h>
#include <unistd.h>

#define MAX_PIXELS (256 * 224)

// Video: keep a copy of the last frame, and the core's keys at the time (the bridge's frame
// callback calls setKeys immediately before posting, so this observes key latching).

static Mutex _frameMutex = PTHREAD_MUTEX_INITIALIZER;
static mColor _frame[MAX_PIXELS];
static unsigned _frameWidth;
static unsigned _frameHeight;
static _Atomic unsigned _framesPosted;
static _Atomic uint32_t _keysSeen;

void mgfVideoSetWindow(struct ANativeWindow* window) {
	UNUSED(window);
}

void mgfVideoPost(const mColor* pixels, size_t stride, unsigned width, unsigned height) {
	struct mCore* core = mgfTestCore();
	if (core) {
		atomic_fetch_or(&_keysSeen, core->getKeys(core));
	}
	MutexLock(&_frameMutex);
	unsigned y;
	for (y = 0; y < height && (y + 1) * width <= MAX_PIXELS; ++y) {
		memcpy(&_frame[y * width], &pixels[y * stride], width * sizeof(mColor));
	}
	_frameWidth = width;
	_frameHeight = height;
	MutexUnlock(&_frameMutex);
	atomic_fetch_add(&_framesPosted, 1);
}

unsigned mgfStubFramesPosted(void) {
	return atomic_load(&_framesPosted);
}

bool mgfStubLastFrame(mColor* out, size_t capacity, unsigned* width, unsigned* height) {
	MutexLock(&_frameMutex);
	bool ok = _frameWidth && _frameWidth * _frameHeight <= capacity;
	if (ok) {
		memcpy(out, _frame, _frameWidth * _frameHeight * sizeof(mColor));
		*width = _frameWidth;
		*height = _frameHeight;
	}
	MutexUnlock(&_frameMutex);
	return ok;
}

uint32_t mgfStubKeysSeen(void) {
	return atomic_load(&_keysSeen);
}

void mgfStubResetKeysSeen(void) {
	atomic_store(&_keysSeen, 0);
}

// Audio: a thread that drains the core's buffer through mCoreSync, like the AAudio callback,
// so audio-paced emulation keeps running (faster than real time, which suits tests).

static Thread _audioThread;
static _Atomic bool _audioRunning;
static _Atomic bool _audioPaused;
static struct mCore* _audioCore;
static struct mCoreSync* _audioSync;

static THREAD_ENTRY _drain(void* context) {
	UNUSED(context);
	int16_t samples[2048];
	while (atomic_load(&_audioRunning)) {
		if (!atomic_load(&_audioPaused)) {
			mCoreSyncLockAudio(_audioSync);
			struct mAudioBuffer* buffer = _audioCore->getAudioBuffer(_audioCore);
			while (mAudioBufferAvailable(buffer)) {
				mAudioBufferRead(buffer, samples, sizeof(samples) / sizeof(*samples) / 2);
			}
			mCoreSyncConsumeAudio(_audioSync);
		}
		usleep(1000);
	}
	THREAD_EXIT(0);
}

bool mgfAudioStart(struct mCore* core, struct mCoreSync* sync) {
	_audioCore = core;
	_audioSync = sync;
	atomic_store(&_audioPaused, false);
	atomic_store(&_audioRunning, true);
	ThreadCreate(&_audioThread, _drain, NULL);
	return true;
}

void mgfAudioPause(bool paused) {
	atomic_store(&_audioPaused, paused);
}

void mgfAudioStop(void) {
	if (!atomic_load(&_audioRunning)) {
		return;
	}
	atomic_store(&_audioRunning, false);
	ThreadJoin(&_audioThread);
}

bool mgfStubAudioRunning(void) {
	return atomic_load(&_audioRunning);
}
