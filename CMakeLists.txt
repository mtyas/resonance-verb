# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(ResonanceVerb VERSION 0.9.0 LANGUAGES C CXX)

# Public project page (used for the LV2 URI and docs).
set(RV_HOMEPAGE "https://github.com/mtyas/resonance-verb" CACHE STRING "Project homepage")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RV_BUILD_PLUGIN "Build the JUCE plugin (downloads JUCE)" ON)
option(RV_BUILD_TESTS  "Build the offline DSP test/benchmark harness" ON)

# ---------------------------------------------------------------- DSP test harness (no JUCE)
if(RV_BUILD_TESTS)
    add_executable(rv_render tests/render_test.cpp)
    target_include_directories(rv_render PRIVATE Source)
    if(NOT MSVC)
        target_compile_options(rv_render PRIVATE -O3)
    endif()
endif()

# ---------------------------------------------------------------- Plugin
if(RV_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)

    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d   # main, 2026-11
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(clap-juce-extensions)

    set(RV_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND RV_FORMATS AU)
    elseif(UNIX)
        list(APPEND RV_FORMATS LV2)
    endif()

    juce_add_plugin(ResonanceVerb
        PRODUCT_NAME              "Resonance Verb"
        COMPANY_NAME              "Matthew Tyas"
        COMPANY_WEBSITE           "${RV_HOMEPAGE}"
        BUNDLE_ID                 com.matthewtyas.resonanceverb
        PLUGIN_MANUFACTURER_CODE  Mtya
        PLUGIN_CODE               Rsv1
        FORMATS                   ${RV_FORMATS}
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          TRUE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        AU_MAIN_TYPE              kAudioUnitType_MusicEffect
        VST3_CATEGORIES           Fx Reverb
        LV2URI                    "${RV_HOMEPAGE}"
        COPY_PLUGIN_AFTER_BUILD   FALSE)

    set(RV_PLUGIN_SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresetManager.cpp
        Source/PresetBrowser.cpp)
    target_sources(ResonanceVerb PRIVATE ${RV_PLUGIN_SOURCES})
    target_include_directories(ResonanceVerb PRIVATE Source)

    target_compile_definitions(ResonanceVerb PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(ResonanceVerb
        PRIVATE juce::juce_audio_utils
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)

    # Headless helpers built from the same sources as the plugin:
    #   rv_plugin_tests          undo/redo, presets, MIDI learn, session state
    #   rv_ui_snapshot out.png   renders the editor to a PNG (docs + visual check)
    foreach(tool rv_plugin_tests rv_ui_snapshot)
        juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
        target_sources(${tool} PRIVATE ${RV_PLUGIN_SOURCES})
        target_include_directories(${tool} PRIVATE Source)
        target_compile_definitions(${tool} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    endforeach()
    target_sources(rv_plugin_tests PRIVATE tests/plugin_tests.cpp)
    target_sources(rv_ui_snapshot PRIVATE tests/ui_snapshot.cpp)

    clap_juce_extensions_plugin(TARGET ResonanceVerb
        CLAP_ID "com.matthewtyas.resonanceverb"
        CLAP_FEATURES audio-effect reverb stereo)
endif()
