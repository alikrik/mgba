/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "flutter-common.h"

#include <mgba-util/threading.h>

#include <android/hardware_buffer.h>
#include <android/native_window.h>

#if defined(COLOR_16_BIT)
#error "The Flutter frontend expects 32-bit XBGR8 pixels (R8G8B8X8 in memory)"
#endif

static Mutex _windowMutex = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow* _window = NULL;
static unsigned _windowWidth = 0;
static unsigned _windowHeight = 0;
static bool _lockFailed = false;

void mgfVideoSetWindow(ANativeWindow* window) {
	MutexLock(&_windowMutex);
	if (_window) {
		ANativeWindow_release(_window);
	}
	_window = window;
	_windowWidth = 0;
	_windowHeight = 0;
	_lockFailed = false;
	MutexUnlock(&_windowMutex);
}

void mgfVideoPost(const mColor* pixels, size_t stride, unsigned width, unsigned height) {
	MutexLock(&_windowMutex);
	if (!_window) {
		MutexUnlock(&_windowMutex);
		return;
	}
	if (width != _windowWidth || height != _windowHeight) {
		ANativeWindow_setBuffersGeometry(_window, width, height, AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM);
		_windowWidth = width;
		_windowHeight = height;
	}
	ANativeWindow_Buffer buffer;
	int32_t res = ANativeWindow_lock(_window, &buffer, NULL);
	if (res != 0) {
		if (!_lockFailed) {
			mLOG(FLUTTER, ERROR, "ANativeWindow_lock failed: %i", res);
			_lockFailed = true;
		}
		MutexUnlock(&_windowMutex);
		return;
	}
	unsigned rows = height < (unsigned) buffer.height ? height : (unsigned) buffer.height;
	unsigned cols = width < (unsigned) buffer.width ? width : (unsigned) buffer.width;
	uint8_t* out = buffer.bits;
	unsigned y;
	for (y = 0; y < rows; ++y) {
		memcpy(&out[y * buffer.stride * 4], &pixels[y * stride], cols * sizeof(mColor));
	}
	ANativeWindow_unlockAndPost(_window);
	MutexUnlock(&_windowMutex);
}
