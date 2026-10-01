cmake_minimum_required(VERSION 3.22)

project(FabCutie VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary so the plugin loads natively on Apple Silicon and Intel Macs.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if FABCUTIE_JUCE_PATH is set, otherwise fetch it.
# ---------------------------------------------------------------------------
set(FABCUTIE_JUCE_VERSION "8.0.15")
set(FABCUTIE_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(FABCUTIE_JUCE_PATH)
    add_subdirectory(${FABCUTIE_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${FABCUTIE_JUCE_VERSION}
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# Copy the built plugin into the system plug-in folders (e.g. ~/Library/Audio/Plug-Ins/Components).
option(FABCUTIE_COPY_AFTER_BUILD "Install the plugin into the user plug-in folders after building" OFF)

set(FABCUTIE_FORMATS AU VST3 Standalone)
if(NOT APPLE)
    list(REMOVE_ITEM FABCUTIE_FORMATS AU)
endif()

juce_add_plugin(FabCutie
    COMPANY_NAME            "FabCutie"
    COMPANY_WEBSITE         "https://github.com/danmcgrath10/fabcutie"
    BUNDLE_ID               "io.github.danmcgrath10.fabcutie"
    PLUGIN_MANUFACTURER_CODE Fbct
    PLUGIN_CODE             Fceq
    PRODUCT_NAME            "FabCutie"
    DESCRIPTION             "Open-source parametric EQ"
    FORMATS                 ${FABCUTIE_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE            kAudioUnitType_Effect
    VST3_CATEGORIES         Fx EQ
    COPY_PLUGIN_AFTER_BUILD ${FABCUTIE_COPY_AFTER_BUILD})

target_sources(FabCutie PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp)

target_include_directories(FabCutie PRIVATE Source)

target_compile_definitions(FabCutie PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FabCutie
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
