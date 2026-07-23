# Standalone VST3 build of VoiceFX.
#
# Unlike the repository root (which is a component of the proprietary TonPlugIns
# framework), this project builds a self-contained VST3 using only:
#   * the Steinberg VST3 SDK (path in VST3_SDK_DIR),
#   * the vendored Secret Rabbit Code resampler,
#   * the vendored NVIDIA Audio Effects headers,
#   * small compatibility shims in compat/.
cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW) # MSVC runtime selectable via CMAKE_MSVC_RUNTIME_LIBRARY
endif()

# Use the static CRT everywhere (the vendored resampler forces /MT, so everything
# else must match to avoid a runtime-library mismatch at link time).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# Set -DVOICEFX_BUILD_HOSTING_TOOLS=ON to also build the SDK's validator, which is
# handy for testing the plugin. Off by default to keep builds lean.
option(VOICEFX_BUILD_HOSTING_TOOLS "Also build the SDK validator/host tools" OFF)

# Trim the VST3 SDK build down to what a plugin needs.
# VSTGUI is ON: we ship a small custom editor (see resource/voicefx.uidesc and
# vst3_effect_controller.cpp). Turning this on makes the SDK's root CMakeLists
# build the vstgui / vstgui_uidescription / vstgui_support targets we link below.
set(SMTG_ENABLE_VSTGUI_SUPPORT       ON  CACHE BOOL "" FORCE)
set(SMTG_ADD_VSTGUI                  ON  CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ${VOICEFX_BUILD_HOSTING_TOOLS} CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK          OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR           OFF CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT              ON  CACHE BOOL "" FORCE)

project(VoiceFXStandalone VERSION 2.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ----------------------------------------------------------
set(VST3_SDK_DIR "C:/vst3sdk" CACHE PATH "Path to the Steinberg VST3 SDK checkout")
if(NOT EXISTS "${VST3_SDK_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "VST3 SDK not found at '${VST3_SDK_DIR}'. Set -DVST3_SDK_DIR=<path>.")
endif()
add_subdirectory("${VST3_SDK_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)

# Vendored resampler (Secret Rabbit Code).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third-party/secret-rabbit-code"
                 "${CMAKE_BINARY_DIR}/secret-rabbit-code")

# --- Plugin ----------------------------------------------------------------
set(VOICEFX_SOURCES
	src/lib.cpp
	src/nvidia-afx.cpp
	src/nvidia-afx-effect.cpp
	src/resampler.cpp
	src/vst3.cpp
	src/vst3_effect_processor.cpp
	src/vst3_effect_controller.cpp

	# Windows DLL entry point from the VST3 SDK. It defines the global
	# `moduleHandle` and records it in DllMain. Without a custom editor nothing
	# referenced it, so we got away without it; VSTGUI's win32 bundle support
	# now calls getPlatformModuleHandle() to locate the .uidesc inside the
	# bundle, which pulls in this symbol. Also lets InitModule/DeinitModule run.
	"${VST3_SDK_DIR}/public.sdk/source/main/dllmain.cpp"
)

smtg_add_vst3plugin(VoiceFX PACKAGE_NAME "VoiceFX KDver" SOURCES_LIST ${VOICEFX_SOURCES})

target_compile_features(VoiceFX PUBLIC cxx_std_17)

# NOMINMAX: keep <windows.h> from defining min/max macros (they break std::min /
#          max / numeric_limits::max used throughout the sources).
# QUIET:   silence the per-sample D_LOG_LOUD debug logging.
target_compile_definitions(VoiceFX PRIVATE NOMINMAX QUIET)

target_include_directories(VoiceFX PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/compat"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../third-party/nvidia-maxine-afx-sdk/nvafx/include"
)

target_link_libraries(VoiceFX PRIVATE sdk secret-rabbit-code vstgui_support)

# Custom editor UI. The .uidesc is copied into the bundle at
# <VoiceFX KDver.vst3>/Contents/Resources/voicefx.uidesc, where VST3Editor
# looks it up by name at runtime.
#
# We copy it ourselves instead of using smtg_target_add_plugin_resources():
# the SDK's copy command doesn't quote its destination path, so the space in
# the package name ("VoiceFX KDver.vst3") splits the path and the copy fails.
# A VERBATIM post-build command quotes the path correctly. The package path /
# resources subfolder come straight from the SDK's own target properties, so
# this tracks wherever the bundle is written.
get_target_property(_voicefx_pkg_path VoiceFX SMTG_PLUGIN_PACKAGE_PATH)
get_target_property(_voicefx_pkg_res  VoiceFX SMTG_PLUGIN_PACKAGE_RESOURCES)
add_custom_command(TARGET VoiceFX POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${_voicefx_pkg_path}/${_voicefx_pkg_res}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/resource/voicefx.uidesc"
		"${_voicefx_pkg_path}/${_voicefx_pkg_res}/voicefx.uidesc"
	COMMENT "[VoiceFX] Copying voicefx.uidesc into the plugin bundle"
	VERBATIM
)
