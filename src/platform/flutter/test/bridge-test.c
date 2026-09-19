/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// Host tests for the Flutter bridge: the real bridge.c and mGBA core, driven through the
// public mgf_* API, with video/audio stubbed (test/stubs.c).
#include "mgba_flutter.h"
#include "flutter-common.h"
#include "test/stubs.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GB_ROM MGF_FIXTURES_DIR "/dmg-acid2.gb"
#define GBA_ROM MGF_FIXTURES_DIR "/arm.gba"

// Golden FNV-1a hash of the settled dmg-acid2 frame (RGB only). dmg-acid2 renders a static
// reference face, so any correct frame after boot hashes the same (with the default palette
// and model; this test runs before any test changes settings).
#define DMG_ACID2_FRAME_HASH 0xE7759C44u

static int _failures;
static int _checks;
static char _dataDir[PATH_MAX];

#define CHECK(COND) \
	do { \
		++_checks; \
		if (!(COND)) { \
			++_failures; \
			fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #COND); \
		} \
	} while (0)

#define RUN(TEST) \
	do { \
		int before = _failures; \
		fprintf(stderr, "[ RUN  ] %s\n", #TEST); \
		TEST(); \
		mgf_stop(); \
		fprintf(stderr, "%s %s\n", _failures == before ? "[  OK  ]" : "[ FAIL ]", #TEST); \
	} while (0)

static void _sleepMs(int ms) {
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

// Waits (up to 5 s) until at least `frames` more frames have run.
static bool _waitFrames(uint32_t frames) {
	uint32_t target = mgf_frame_count() + frames;
	int waited;
	for (waited = 0; waited < 5000; waited += 2) {
		if (mgf_frame_count() >= target) {
			return true;
		}
		_sleepMs(2);
	}
	return false;
}

static bool _fileExists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static long _fileSize(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long) st.st_size : -1;
}

static mColor _frame[256 * 224];

// Writes the last frame hashed as a PPM, to inspect a golden-hash mismatch.
static void _dumpFrame(const char* name, unsigned width, unsigned height) {
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s.ppm", _dataDir, name);
	FILE* f = fopen(path, "wb");
	if (!f) {
		return;
	}
	fprintf(f, "P6 %u %u 255\n", width, height);
	size_t i;
	for (i = 0; i < width * height; ++i) {
		fputc(_frame[i] & 0xFF, f);
		fputc((_frame[i] >> 8) & 0xFF, f);
		fputc((_frame[i] >> 16) & 0xFF, f);
	}
	fclose(f);
	fprintf(stderr, "  frame written to %s\n", path);
}

static uint32_t _frameHash(unsigned* width, unsigned* height) {
	mColor* frame = _frame;
	if (!mgfStubLastFrame(frame, sizeof(_frame) / sizeof(*_frame), width, height)) {
		return 0;
	}
	uint32_t hash = 2166136261u;
	size_t i;
	for (i = 0; i < *width * *height; ++i) {
		uint32_t rgb = frame[i] & 0xFFFFFF;
		int b;
		for (b = 0; b < 3; ++b) {
			hash ^= (rgb >> (b * 8)) & 0xFF;
			hash *= 16777619u;
		}
	}
	return hash;
}

static void _bootGb(void) {
	CHECK(mgf_load_rom(GB_ROM) == MGF_PLATFORM_GB);
	CHECK(mgf_start());
	CHECK(_waitFrames(10));
}

static void testVersion(void) {
	const char* version = mgf_version();
	CHECK(version && version[0]);
}

static void testLoadRejectsMissingAndNonRoms(void) {
	CHECK(mgf_load_rom("/nonexistent/game.gba") == MGF_PLATFORM_NONE);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/notes.gba", _dataDir);
	FILE* f = fopen(path, "w");
	fputs("definitely not a ROM", f);
	fclose(f);
	CHECK(mgf_load_rom(path) == MGF_PLATFORM_NONE);
	CHECK(!mgf_start());
	CHECK(!mgf_is_running());
}

static void testGbBootsAndRendersReferenceFrame(void) {
	CHECK(mgf_load_rom(GB_ROM) == MGF_PLATFORM_GB);
	int w, h;
	mgf_get_video_size(&w, &h);
	CHECK(w == 160 && h == 144); // known before start: the UI sizes its texture from this
	CHECK(mgf_start());
	CHECK(mgf_is_running());
	CHECK(_waitFrames(120));
	CHECK(mgfStubFramesPosted() > 0);

	unsigned fw, fh;
	uint32_t hash = _frameHash(&fw, &fh);
	CHECK(fw == 160 && fh == 144);
	if (hash != DMG_ACID2_FRAME_HASH) {
		fprintf(stderr, "  dmg-acid2 frame hash 0x%08X (expected 0x%08X)\n", hash, DMG_ACID2_FRAME_HASH);
		_dumpFrame("dmg-acid2", fw, fh);
	}
	CHECK(hash == DMG_ACID2_FRAME_HASH);
}

static void testGbaBoots(void) {
	CHECK(mgf_load_rom(GBA_ROM) == MGF_PLATFORM_GBA);
	int w, h;
	mgf_get_video_size(&w, &h);
	CHECK(w == 240 && h == 160);
	CHECK(mgf_start());
	CHECK(_waitFrames(30));
}

static void testBatterySaveFileIsCreated(void) {
	_bootGb();
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/saves/dmg-acid2.sav", _dataDir);
	CHECK(_fileExists(path));
}

static void testTapShorterThanAFrameIsLatched(void) {
	_bootGb();
	mgf_pause(1);
	mgfStubResetKeysSeen();
	// Press and release before any frame samples the keys
	mgf_set_touch_keys(1 << MGF_KEY_A);
	mgf_set_touch_keys(0);
	mgf_pause(0);
	CHECK(_waitFrames(3));
	CHECK(mgfStubKeysSeen() & (1 << MGF_KEY_A));
	// ...and it is released afterwards
	CHECK(_waitFrames(2));
	struct mCore* core = mgfTestCore();
	CHECK(core->getKeys(core) == 0);
}

static void testTouchAndPadKeysCombine(void) {
	_bootGb();
	mgf_set_touch_keys(1 << MGF_KEY_A);
	mgfSetPadKeys(1 << MGF_KEY_START);
	CHECK(_waitFrames(3));
	struct mCore* core = mgfTestCore();
	CHECK(core->getKeys(core) == ((1 << MGF_KEY_A) | (1 << MGF_KEY_START)));
	mgf_set_touch_keys(0);
	mgfSetPadKeys(0);
	CHECK(_waitFrames(3));
	CHECK(core->getKeys(core) == 0);
}

static void testPauseStopsFrames(void) {
	_bootGb();
	mgf_pause(1);
	uint32_t frames = mgf_frame_count();
	_sleepMs(100);
	CHECK(mgf_frame_count() == frames);
	mgf_pause(0);
	CHECK(_waitFrames(5));
}

static void testLiveConfigReload(void) {
	_bootGb();
	struct mCore* core = mgfTestCore();
	mgf_set_config("frameskip", "2");
	CHECK(core->opts.frameskip == 2);
	mgf_set_config("mute", "1");
	CHECK(core->opts.mute);
	mgf_set_config("mute", "0");
	CHECK(!core->opts.mute);
	mgf_set_config("volume", "64");
	CHECK(core->opts.volume == 64);
	mgf_set_config("frameskip", "0");
	mgf_set_config("volume", "256");
}

static void testSettingsPersistAcrossLoadsAndUnset(void) {
	mgf_set_config("gb.model", "CGB");
	CHECK(mgf_load_rom(GB_ROM) == MGF_PLATFORM_GB);
	const char* model = mCoreConfigGetValue(&mgfTestCore()->config, "gb.model");
	CHECK(model && strcmp(model, "CGB") == 0);

	mgf_set_config("gb.model", "");
	CHECK(mCoreConfigGetValue(&mgfTestCore()->config, "gb.model") == NULL);
	CHECK(mgf_load_rom(GB_ROM) == MGF_PLATFORM_GB);
	CHECK(mCoreConfigGetValue(&mgfTestCore()->config, "gb.model") == NULL);
}

static void testFastForward(void) {
	_bootGb();
	struct mCoreThread* thread = mgfTestThread();
	struct mCore* core = mgfTestCore();

	mgf_set_fast_forward(1, 3.0f, 1);
	CHECK(thread->impl->sync.fpsTarget > 179.9f && thread->impl->sync.fpsTarget < 180.1f);
	CHECK(thread->impl->sync.audioWait);
	CHECK(core->opts.mute);

	mgf_set_fast_forward(1, 0, 0);
	CHECK(!thread->impl->sync.audioWait);
	CHECK(_waitFrames(30));

	mgf_set_fast_forward(0, 0, 0);
	CHECK(thread->impl->sync.fpsTarget > 59.9f && thread->impl->sync.fpsTarget < 60.1f);
	CHECK(thread->impl->sync.audioWait);
	CHECK(!core->opts.mute);
	CHECK(_waitFrames(5));
}

static void testSaveAndLoadState(void) {
	_bootGb();
	CHECK(_waitFrames(60));
	CHECK(mgf_save_state(3));

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/states/dmg-acid2.ss3", _dataDir);
	CHECK(_fileSize(path) > 0);

	strlcat(path, ".thumb", sizeof(path));
	CHECK(_fileSize(path) == 12 + 160 * 144 * 4);
	FILE* f = fopen(path, "rb");
	uint8_t header[12] = { 0 };
	CHECK(f && fread(header, 1, sizeof(header), f) == sizeof(header));
	if (f) {
		fclose(f);
	}
	CHECK(memcmp(header, "MGFT", 4) == 0);
	CHECK((header[4] | header[5] << 8) == 160);
	CHECK((header[8] | header[9] << 8) == 144);

	CHECK(mgf_load_state(3));
	CHECK(_waitFrames(5));
	CHECK(!mgf_load_state(8)); // empty slot
}

static void testLoadingAnotherRomStopsTheCurrentOne(void) {
	_bootGb();
	CHECK(mgf_load_rom(GBA_ROM) == MGF_PLATFORM_GBA);
	CHECK(!mgf_is_running());
	CHECK(!mgfStubAudioRunning());
	CHECK(mgf_start());
	CHECK(_waitFrames(5));
}

static void testOperationsAfterStopAreSafe(void) {
	_bootGb();
	mgf_stop();
	CHECK(!mgf_is_running());
	CHECK(!mgfStubAudioRunning());
	CHECK(!mgf_save_state(1));
	CHECK(!mgf_load_state(1));
	mgf_pause(1);
	mgf_reset();
	mgf_set_fast_forward(1, 2.0f, 0);
	mgf_set_config("frameskip", "0");
	int w, h;
	mgf_get_video_size(&w, &h);
	CHECK(w == 0 && h == 0);
}

int main(void) {
	char tmpl[] = "/tmp/mgf-test-XXXXXX";
	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		return 2;
	}
	strlcpy(_dataDir, tmpl, sizeof(_dataDir));
	mgf_init(_dataDir);

	RUN(testVersion);
	RUN(testLoadRejectsMissingAndNonRoms);
	RUN(testGbBootsAndRendersReferenceFrame);
	RUN(testGbaBoots);
	RUN(testBatterySaveFileIsCreated);
	RUN(testTapShorterThanAFrameIsLatched);
	RUN(testTouchAndPadKeysCombine);
	RUN(testPauseStopsFrames);
	RUN(testLiveConfigReload);
	RUN(testSettingsPersistAcrossLoadsAndUnset);
	RUN(testFastForward);
	RUN(testSaveAndLoadState);
	RUN(testLoadingAnotherRomStopsTheCurrentOne);
	RUN(testOperationsAfterStopAreSafe);

	fprintf(stderr, "\n%d checks, %d failed\n", _checks, _failures);
	return _failures ? 1 : 0;
}
