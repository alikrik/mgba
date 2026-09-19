/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mgba_flutter.h"
#include "flutter-common.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/directories.h>
#include <mgba/core/serialize.h>
#include <mgba/core/thread.h>
#include <mgba/core/version.h>
#include <mgba-util/vfs.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif
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
	// Keys pressed since the last frame, so taps shorter than a frame are never lost
	_Atomic uint32_t latchedKeys;
	_Atomic uint32_t frameCount;
	bool fastForward;
};

#define MAX_SETTINGS 64

struct mgfSetting {
	char* key;
	char* value;
};

static struct mgfSetting _settings[MAX_SETTINGS];
static size_t _nSettings;

static struct mgfContext _ctx;

static struct mLogger _logger;
static struct mLogFilter _logFilter;

static void _log(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
#ifndef __ANDROID__
	// Host builds (unit tests)
	fprintf(stderr, "%s: ", mLogCategoryName(category));
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	UNUSED(level);
#else
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
#endif
}

static void _frameCallback(struct mCoreThread* thread) {
	struct mCore* core = thread->core;
	uint32_t keys = atomic_load(&_ctx.touchKeys) | atomic_load(&_ctx.padKeys);
	core->setKeys(core, keys | atomic_exchange(&_ctx.latchedKeys, 0));
	atomic_fetch_add(&_ctx.frameCount, 1);
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
	size_t i;
	for (i = 0; i < _nSettings; ++i) {
		mCoreConfigSetValue(&core->config, _settings[i].key, _settings[i].value);
	}
	// Settings are owned by the app (pushed via mgf_set_config), so don't read config.ini,
	// which would also clear the values set above
	mCoreLoadForeignConfig(core, &core->config);

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
	// Reset now so the model (and thus currentVideoSize) is known before mgf_start: an unreset
	// GB core reports the 256x224 SGB frame, which would give the UI the wrong aspect ratio.
	core->reset(core);
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
	_ctx.fastForward = false;
	atomic_store(&_ctx.frameCount, 0);
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
	atomic_fetch_or(&_ctx.latchedKeys, keys);
}

void mgfSetPadKeys(uint32_t keys) {
	atomic_store(&_ctx.padKeys, keys);
	atomic_fetch_or(&_ctx.latchedKeys, keys);
}

static void _writeThumbnail(int slot) {
	struct mCore* core = _ctx.core;
	char suffix[32];
	snprintf(suffix, sizeof(suffix), ".ss%i.thumb", slot);
	struct VFile* vf = mDirectorySetOpenSuffix(&core->dirs, core->dirs.state, suffix, O_CREAT | O_TRUNC | O_WRONLY);
	if (!vf) {
		return;
	}
	unsigned width, height;
	core->currentVideoSize(core, &width, &height);
	uint8_t header[12] = { 'M', 'G', 'F', 'T' };
	STORE_32LE(width, 4, header);
	STORE_32LE(height, 8, header);
	vf->write(vf, header, sizeof(header));
	uint32_t* row = malloc(width * sizeof(*row));
	unsigned x, y;
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			// XBGR8 in memory is R, G, B, X; force X to opaque alpha
			row[x] = _ctx.videoBuffer[y * _ctx.videoStride + x] | 0xFF000000;
		}
		vf->write(vf, row, width * sizeof(*row));
	}
	free(row);
	vf->close(vf);
}

int mgf_save_state(int slot) {
	if (!_ctx.running) {
		return 0;
	}
	mCoreThreadInterrupt(&_ctx.thread);
	bool success = mCoreSaveState(_ctx.core, slot, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	if (success) {
		_writeThumbnail(slot);
	}
	mCoreThreadContinue(&_ctx.thread);
	return success;
}

int mgf_load_state(int slot) {
	if (!_ctx.running) {
		return 0;
	}
	mCoreThreadInterrupt(&_ctx.thread);
	bool success = mCoreLoadState(_ctx.core, slot, SAVESTATE_RTC);
	mCoreThreadContinue(&_ctx.thread);
	return success;
}

static void _storeSetting(const char* key, const char* value) {
	size_t i;
	for (i = 0; i < _nSettings; ++i) {
		if (strcmp(_settings[i].key, key) == 0) {
			break;
		}
	}
	if (i == _nSettings) {
		if (!value || _nSettings == MAX_SETTINGS) {
			return;
		}
		_settings[i].key = strdup(key);
		_settings[i].value = NULL;
		++_nSettings;
	}
	free(_settings[i].value);
	_settings[i].value = value ? strdup(value) : NULL;
}

void mgf_set_config(const char* key, const char* value) {
	if (value && !value[0]) {
		value = NULL;
	}
	_storeSetting(key, value);
	if (!_ctx.core) {
		return;
	}
	if (_ctx.running) {
		mCoreThreadInterrupt(&_ctx.thread);
	}
	mCoreConfigSetValue(&_ctx.core->config, key, value);
	// gb.pal[N] is reloaded as a group under "gb.pal"
	const char* option = strncmp(key, "gb.pal[", 7) == 0 ? "gb.pal" : key;
	_ctx.core->reloadConfigOption(_ctx.core, option, NULL);
	if (_ctx.running) {
		mCoreThreadContinue(&_ctx.thread);
	}
}

void mgf_set_fast_forward(int enabled, float ratio, int mute) {
	if (!_ctx.running) {
		return;
	}
	struct mCore* core = _ctx.core;
	struct mCoreSync* sync = &_ctx.thread.impl->sync;
	mCoreThreadInterrupt(&_ctx.thread);
	_ctx.fastForward = enabled;
	float fpsTarget = core->opts.fpsTarget > 0 ? core->opts.fpsTarget : 60.f;
	if (enabled) {
		core->opts.mute = mute || core->opts.mute;
		if (ratio > 0) {
			// Stay audio-paced, just at a faster clock (mirrors the Qt frontend)
			sync->fpsTarget = fpsTarget * ratio;
			sync->audioWait = true;
		} else {
			sync->fpsTarget = fpsTarget;
			sync->audioWait = false;
		}
		sync->videoFrameWait = false;
	} else {
		core->opts.mute = false;
		mCoreConfigGetBoolValue(&core->config, "mute", &core->opts.mute);
		sync->fpsTarget = fpsTarget;
		sync->audioWait = core->opts.audioSync;
		sync->videoFrameWait = core->opts.videoSync;
	}
	core->reloadConfigOption(core, NULL, NULL);
	mCoreThreadContinue(&_ctx.thread);
}

uint32_t mgf_frame_count(void) {
	return atomic_load(&_ctx.frameCount);
}

#ifdef MGF_TESTING
struct mCore* mgfTestCore(void) {
	return _ctx.core;
}

struct mCoreThread* mgfTestThread(void) {
	return _ctx.running ? &_ctx.thread : NULL;
}
#endif
