# Flutter / Android bridge

`libmgba_flutter.so` is the native layer used by **fGBA**, the Flutter frontend for Android. The fGBA repo vendors this fork as a submodule at `third_party/mgba` and builds this directory through Gradle's `externalNativeBuild`.

| File | Responsibility |
|---|---|
| `include/mgba_flutter.h` | Public C API (`mgf_*`), consumed from Dart via `dart:ffi` / ffigen |
| `bridge.c` | Core lifecycle, `mCoreThread`, keys, settings, fast-forward, save states, logging |
| `video.c` | `ANativeWindow` ownership; blits each frame into the Flutter texture's surface |
| `audio.c` | AAudio output stream; resamples and paces emulation, like the SDL frontend |
| `jni.c` | JNI entry points for the Kotlin side (`Java_com_alikrik_fgba_MgbaNative_*`) |
| `CMakeLists.txt` | Builds libmgba statically (`LIBMGBA_ONLY`) plus the bridge, and copies mGBA's private feature defines onto it so struct layouts match |

Architecture, threading and data-flow documentation live in the fGBA repo: `docs/ARCHITECTURE.md` and `docs/DOMAIN.md`.

To build it on its own, without the app:

```sh
cmake -S src/platform/flutter -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26
cmake --build build-android
```

## Tests

Host unit tests drive the real `bridge.c` and mGBA core through the `mgf_*` API, with video and audio replaced by stubs (`test/stubs.c`). The audio stub drains samples through `mCoreSync` like AAudio does. They use two MIT-licensed test ROMs in `test/fixtures/` (dmg-acid2, jsmolka's gba-tests), and cover:
- loading;
- the golden dmg-acid2 frame;
- key latching;
- settings;
- fast-forward;
- save states;
- lifecycle.

```sh
cmake -S src/platform/flutter -B build-flutter-test -DMGF_BUILD_TESTS=ON
cmake --build build-flutter-test && ctest --test-dir build-flutter-test --output-on-failure

# with line coverage of bridge.c, failing below MIN (default 85%)
src/platform/flutter/test/coverage.sh
```

After changing this directory, push the `flutter-android` branch, then `git pull` inside fGBA's `third_party/mgba` and commit the submodule bump there. If the header changed, regenerate the Dart bindings as well (`dart run ffigen`).
