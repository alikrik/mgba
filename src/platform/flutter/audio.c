/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "flutter-common.h"

#include <mgba/core/core.h>
#include <mgba/core/sync.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>

#include <aaudio/AAudio.h>

#define BUFFER_SAMPLES 2048

struct mgfAudio {
	AAudioStream* stream;
	struct mCore* core;
	struct mCoreSync* sync;
	struct mAudioBuffer buffer;
	struct mAudioResampler resampler;
	int32_t sampleRate;
};

static struct mgfAudio _audio;

static aaudio_data_callback_result_t _audioCallback(AAudioStream* stream, void* user, void* data, int32_t frames) {
	UNUSED(stream);
	struct mgfAudio* audio = user;
	int16_t* out = data;
	struct mCore* core = audio->core;
	if (!core) {
		memset(out, 0, frames * 2 * sizeof(int16_t));
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	struct mAudioBuffer* buffer = core->getAudioBuffer(core);
	unsigned sampleRate = core->audioSampleRate(core);
	double fauxClock = 1;
	if (audio->sync) {
		if (audio->sync->fpsTarget > 0) {
			fauxClock = mCoreCalculateFramerateRatio(core, audio->sync->fpsTarget);
		}
		mCoreSyncLockAudio(audio->sync);
		audio->sync->audioHighWater = BUFFER_SAMPLES + audio->resampler.highWaterMark + audio->resampler.lowWaterMark + (BUFFER_SAMPLES >> 6);
		audio->sync->audioHighWater *= sampleRate / (fauxClock * audio->sampleRate);
	}
	mAudioResamplerSetSource(&audio->resampler, buffer, sampleRate / fauxClock, true);
	mAudioResamplerProcess(&audio->resampler);
	if (audio->sync) {
		mCoreSyncConsumeAudio(audio->sync);
	}

	size_t read = mAudioBufferRead(&audio->buffer, out, frames);
	if (read < (size_t) frames) {
		memset(&out[read * 2], 0, (frames - read) * 2 * sizeof(int16_t));
	}
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void _errorCallback(AAudioStream* stream, void* user, aaudio_result_t error) {
	UNUSED(stream);
	UNUSED(user);
	// TODO: Reopen the stream on AAUDIO_ERROR_DISCONNECTED (e.g. headphones unplugged).
	mLOG(FLUTTER, ERROR, "AAudio error: %s", AAudio_convertResultToText(error));
}

bool mgfAudioStart(struct mCore* core, struct mCoreSync* sync) {
	AAudioStreamBuilder* builder;
	aaudio_result_t res = AAudio_createStreamBuilder(&builder);
	if (res != AAUDIO_OK) {
		mLOG(FLUTTER, ERROR, "Could not create AAudio builder: %s", AAudio_convertResultToText(res));
		return false;
	}
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setChannelCount(builder, 2);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setDataCallback(builder, _audioCallback, &_audio);
	AAudioStreamBuilder_setErrorCallback(builder, _errorCallback, &_audio);

	res = AAudioStreamBuilder_openStream(builder, &_audio.stream);
	AAudioStreamBuilder_delete(builder);
	if (res != AAUDIO_OK) {
		mLOG(FLUTTER, ERROR, "Could not open AAudio stream: %s", AAudio_convertResultToText(res));
		_audio.stream = NULL;
		return false;
	}

	_audio.sampleRate = AAudioStream_getSampleRate(_audio.stream);
	mAudioBufferInit(&_audio.buffer, BUFFER_SAMPLES, 2);
	mAudioResamplerInit(&_audio.resampler, mINTERPOLATOR_SINC);
	mAudioResamplerSetDestination(&_audio.resampler, &_audio.buffer, _audio.sampleRate);
	_audio.sync = sync;
	_audio.core = core;

	res = AAudioStream_requestStart(_audio.stream);
	if (res != AAUDIO_OK) {
		mLOG(FLUTTER, ERROR, "Could not start AAudio stream: %s", AAudio_convertResultToText(res));
		mgfAudioStop();
		return false;
	}
	mLOG(FLUTTER, INFO, "Audio started at %i Hz, burst %i frames", _audio.sampleRate, AAudioStream_getFramesPerBurst(_audio.stream));
	return true;
}

void mgfAudioPause(bool paused) {
	if (!_audio.stream) {
		return;
	}
	if (paused) {
		AAudioStream_requestPause(_audio.stream);
	} else {
		AAudioStream_requestStart(_audio.stream);
	}
}

void mgfAudioStop(void) {
	if (!_audio.stream) {
		return;
	}
	AAudioStream_requestStop(_audio.stream);
	// Closing waits for any in-flight data callback to return.
	AAudioStream_close(_audio.stream);
	_audio.stream = NULL;
	_audio.core = NULL;
	_audio.sync = NULL;
	mAudioResamplerDeinit(&_audio.resampler);
	mAudioBufferDeinit(&_audio.buffer);
}
