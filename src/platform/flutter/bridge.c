/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mgba_flutter.h"
#include "flutter-common.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/thread.h>
#include <mgba/core/version.h>

#include <android/log.h>
#include <stdatomic.h>
#include <sys/stat.h>

mLOG_DEFINE_CATEGORY(FLUTTER, "Flutter", "platform.flutter");

struct mgfContext {
	struct mCore* core;
	struct mCoreThread thread;
	mColor* videoBuffer;
	size_t videoStride;
	bool running;
	char dataDir[PATH_MAX];
	char savePath[PATH_MAX];
	char statePath[PATH_MAX];
	_Atomic uint32_t touchKeys;
	_Atomic uint32_t padKeys;
};

static struct mgfContext _ctx;

static struct mLogger _logger;
static struct mLogFilter _logFilter;

static void _log(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
	int priority;
	switch (level) {
	case mLOG_FATAL:
		priority = ANDROID_LOG_FATAL;
		break;
	case mLOG_ERROR:
		priority = ANDROID_LOG_ERROR;
		break;
	case mLOG_WARN:
	case mLOG_GAME_ERROR:
		priority = ANDROID_LOG_WARN;
		break;
	case mLOG_INFO:
		priority = ANDROID_LOG_INFO;
		break;
	default:
		priority = ANDROID_LOG_DEBUG;
		break;
	}
	char message[1024];
	vsnprintf(message, sizeof(message), format, args);
	__android_log_print(priority, "mgba", "%s: %s", mLogCategoryName(category), message);
}

static void _frameCallback(struct mCoreThread* thread) {
	struct mCore* core = thread->core;
	core->setKeys(core, atomic_load(&_ctx.touchKeys) | atomic_load(&_ctx.padKeys));
	unsigned width, height;
	core->currentVideoSize(core, &width, &height);
	mgfVideoPost(_ctx.videoBuffer, _ctx.videoStride, width, height);
}

static void _unload(void) {
	if (!_ctx.core) {
		return;
	}
	_ctx.core->unloadROM(_ctx.core);
	mCoreConfigDeinit(&_ctx.core->config);
	_ctx.core->deinit(_ctx.core);
	_ctx.core = NULL;
	free(_ctx.videoBuffer);
	_ctx.videoBuffer = NULL;
}

const char* mgf_version(void) {
	return projectVersion;
}

int mgf_init(const char* dataDir) {
	if (_ctx.dataDir[0]) {
		return 1;
	}
	strlcpy(_ctx.dataDir, dataDir, sizeof(_ctx.dataDir));
	snprintf(_ctx.savePath, sizeof(_ctx.savePath), "%s/saves", dataDir);
	snprintf(_ctx.statePath, sizeof(_ctx.statePath), "%s/states", dataDir);
	mkdir(_ctx.savePath, 0755);
	mkdir(_ctx.statePath, 0755);
	// mCoreConfigDirectory resolves to $XDG_CONFIG_HOME/mgba on POSIX
	setenv("XDG_CONFIG_HOME", dataDir, 1);

	_logger.log = _log;
	mLogFilterInit(&_logFilter);
	// Lower levels can fire every frame (DMA info, ROM hacks poking the RTC) and flood logcat
	_logFilter.defaultLevels = mLOG_FATAL | mLOG_ERROR;
	_logger.filter = &_logFilter;
	mLogSetDefaultLogger(&_logger);
	return 1;
}

int mgf_load_rom(const char* path) {
	mgf_stop();
	_unload();

	struct mCore* core = mCoreFind(path);
	if (!core) {
		mLOG(FLUTTER, ERROR, "No core found for %s", path);
		return MGF_PLATFORM_NONE;
	}
	if (!core->init(core)) {
		mLOG(FLUTTER, ERROR, "Could not initialize core");
		return MGF_PLATFORM_NONE;
	}
	mCoreInitConfig(core, "fgba");
	struct mCoreOptions opts = {
		.useBios = true,
		.volume = 0x100,
		.audioBuffers = 1024,
		.audioSync = true,
		.videoSync = false,
	};
	mCoreConfigLoadDefaults(&core->config, &opts);
	mCoreConfigSetDefaultValue(&core->config, "savegamePath", _ctx.savePath);
	mCoreConfigSetDefaultValue(&core->config, "savestatePath", _ctx.statePath);
	mCoreLoadConfig(core);

	unsigned width, height;
	core->baseVideoSize(core, &width, &height);
	_ctx.videoStride = width;
	_ctx.videoBuffer = calloc(width * height, sizeof(mColor));
	core->setVideoBuffer(core, _ctx.videoBuffer, _ctx.videoStride);
	_ctx.core = core;

	if (!mCoreLoadFile(core, path)) {
		mLOG(FLUTTER, ERROR, "Could not load ROM %s", path);
		_unload();
		return MGF_PLATFORM_NONE;
	}
	mCoreAutoloadSave(core);
	return core->platform(core);
}

void mgf_get_video_size(int* width, int* height) {
	unsigned w = 0, h = 0;
	if (_ctx.core) {
		_ctx.core->currentVideoSize(_ctx.core, &w, &h);
	}
	*width = w;
	*height = h;
}

int mgf_start(void) {
	if (!_ctx.core || _ctx.running) {
		return 0;
	}
	memset(&_ctx.thread, 0, sizeof(_ctx.thread));
	_ctx.thread.core = _ctx.core;
	_ctx.thread.frameCallback = _frameCallback;
	_ctx.thread.logger.logger = &_logger;
	if (!mCoreThreadStart(&_ctx.thread)) {
		return 0;
	}
	_ctx.running = true;
	mgfAudioStart(_ctx.core, &_ctx.thread.impl->sync);
	return 1;
}

void mgf_pause(int paused) {
	if (!_ctx.running) {
		return;
	}
	if (paused) {
		mCoreThreadPause(&_ctx.thread);
	} else {
		mCoreThreadUnpause(&_ctx.thread);
	}
	mgfAudioPause(paused);
}

void mgf_reset(void) {
	if (_ctx.running) {
		mCoreThreadReset(&_ctx.thread);
	}
}

void mgf_stop(void) {
	if (!_ctx.running) {
		return;
	}
	mCoreThreadEnd(&_ctx.thread);
	// Audio must stop before the join frees the sync object the callback uses
	mgfAudioStop();
	mCoreThreadJoin(&_ctx.thread);
	_ctx.running = false;
	_unload();
}

int mgf_is_running(void) {
	return _ctx.running;
}

void mgf_set_touch_keys(uint32_t keys) {
	atomic_store(&_ctx.touchKeys, keys);
}

void mgfSetPadKeys(uint32_t keys) {
	atomic_store(&_ctx.padKeys, keys);
}
