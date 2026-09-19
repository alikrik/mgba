/* Copyright (c) 2026 Daniel Foord
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "flutter-common.h"

#include <android/native_window_jni.h>
#include <jni.h>

JNIEXPORT void JNICALL Java_com_alikrik_fgba_MgbaNative_setSurface(JNIEnv* env, jclass clazz, jobject surface) {
	UNUSED(clazz);
	ANativeWindow* window = NULL;
	if (surface) {
		window = ANativeWindow_fromSurface(env, surface);
	}
	mgfVideoSetWindow(window);
}

JNIEXPORT void JNICALL Java_com_alikrik_fgba_MgbaNative_setPadKeys(JNIEnv* env, jclass clazz, jint keys) {
	UNUSED(env);
	UNUSED(clazz);
	mgfSetPadKeys(keys);
}
