cmake_minimum_required(VERSION 3.12)
project(mgba_flutter C)

# Android frontend bridge for the fgba Flutter app.
# Builds libmgba statically and exposes a small C API (dart:ffi) plus JNI entry points.

get_filename_component(MGBA_ROOT_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MGBA_ROOT "${MGBA_ROOT_DEFAULT}" CACHE PATH "Path to the mGBA source root")

set(LIBMGBA_ONLY ON)
set(M_CORE_GBA ON)
set(M_CORE_GB ON)
set(ENABLE_SCRIPTING OFF CACHE BOOL "" FORCE)
set(USE_LUA OFF CACHE BOOL "" FORCE)
set(USE_FFMPEG OFF CACHE BOOL "" FORCE)
set(USE_EPOXY OFF CACHE BOOL "" FORCE)
set(USE_ELF OFF CACHE BOOL "" FORCE)
set(USE_LZMA OFF CACHE BOOL "" FORCE)
set(USE_LIBZIP OFF CACHE BOOL "" FORCE)
set(USE_MINIZIP OFF CACHE BOOL "" FORCE)
set(BUILD_GL OFF CACHE BOOL "" FORCE)
set(BUILD_GLES2 OFF CACHE BOOL "" FORCE)
set(BUILD_GLES3 OFF CACHE BOOL "" FORCE)
add_subdirectory(${MGBA_ROOT} mgba)

# libmgba keeps its feature defines private (target and directory level, e.g. USE_PTHREADS,
# M_CORE_*); mirror them so struct layouts in the headers match the library ABI.
get_target_property(MGBA_DEFINES mgba COMPILE_DEFINITIONS)
get_directory_property(MGBA_DIR_DEFINES DIRECTORY ${MGBA_ROOT} COMPILE_DEFINITIONS)
list(REMOVE_ITEM MGBA_DEFINES MGBA_DLL)

if(ANDROID)
	add_library(mgba_flutter SHARED
		audio.c
		bridge.c
		jni.c
		video.c)
	target_compile_definitions(mgba_flutter PRIVATE ${MGBA_DEFINES} ${MGBA_DIR_DEFINES})
	target_include_directories(mgba_flutter PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_link_libraries(mgba_flutter PRIVATE mgba aaudio android log)
endif()

# Host unit tests: the real bridge and mGBA core, with video/audio replaced by stubs.
#   cmake -S src/platform/flutter -B build-flutter-test -DMGF_BUILD_TESTS=ON
#   cmake --build build-flutter-test && ctest --test-dir build-flutter-test --output-on-failure
option(MGF_BUILD_TESTS "Build host unit tests for the Flutter bridge" OFF)
if(MGF_BUILD_TESTS AND NOT ANDROID)
	enable_testing()
	add_executable(mgba_flutter_test
		bridge.c
		test/stubs.c
		test/bridge-test.c)
	target_compile_definitions(mgba_flutter_test PRIVATE ${MGBA_DEFINES} ${MGBA_DIR_DEFINES}
		MGF_TESTING MGF_FIXTURES_DIR="${CMAKE_CURRENT_LIST_DIR}/test/fixtures")
	target_include_directories(mgba_flutter_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR})
	target_link_libraries(mgba_flutter_test PRIVATE mgba)
	add_test(NAME mgba_flutter_test COMMAND mgba_flutter_test)
endif()
