cmake_minimum_required(VERSION 3.22)

project(FabCutie VERSION 0.4.0 LANGUAGES C CXX)

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

# ---------------------------------------------------------------------------
# CLAP: JUCE 8 has no CLAP wrapper of its own, so clap-juce-extensions adds a
# FabCutie_CLAP target next to the JUCE formats. Pinned to a known commit.
# ---------------------------------------------------------------------------
option(FABCUTIE_BUILD_CLAP "Build the CLAP plug-in (fetches clap-juce-extensions)" ON)
set(FABCUTIE_CLAP_JUCE_EXTENSIONS_COMMIT "55525c9858d4b25687be7759a5e0f70eccef218e")
set(FABCUTIE_CLAP_JUCE_EXTENSIONS_PATH "" CACHE PATH "Path to a local clap-juce-extensions checkout (optional)")

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
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE            kAudioUnitType_Effect
    VST3_CATEGORIES         Fx EQ
    COPY_PLUGIN_AFTER_BUILD ${FABCUTIE_COPY_AFTER_BUILD})

target_sources(FabCutie PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/ui/Theme.cpp
    Source/ui/EqModel.cpp
    Source/ui/EqGraph.cpp
    Source/ui/BandPanel.cpp
    Source/ui/SpectrumDisplay.cpp
    Source/ui/LevelMeter.cpp
    Source/ui/AnalyzerBar.cpp
    Source/ui/BandClipboard.cpp
    Source/ui/WorkflowBar.cpp
    Source/ui/OutputBar.cpp
    Source/ui/MidiLearnMenu.cpp
    Source/ui/ValueEntry.cpp
    Source/workflow/ParameterSet.cpp
    Source/workflow/History.cpp
    Source/workflow/Presets.cpp
    Source/ui/InstanceList.cpp
    Source/ui/MatchPanel.cpp)

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

if(FABCUTIE_BUILD_CLAP)
    if(FABCUTIE_CLAP_JUCE_EXTENSIONS_PATH)
        add_subdirectory(${FABCUTIE_CLAP_JUCE_EXTENSIONS_PATH} clap-juce-extensions EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG        ${FABCUTIE_CLAP_JUCE_EXTENSIONS_COMMIT}
            GIT_SUBMODULES_RECURSE ON)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()

    clap_juce_extensions_plugin(TARGET FabCutie
        CLAP_ID "io.github.danmcgrath10.fabcutie"
        CLAP_FEATURES audio-effect equalizer stereo mono surround
        CLAP_MANUAL_URL "https://github.com/danmcgrath10/fabcutie"
        CLAP_SUPPORT_URL "https://github.com/danmcgrath10/fabcutie/issues")
endif()

# ---------------------------------------------------------------------------
# Offline tests (run with ctest). They only need juce_audio_basics (juce_dsp
# for the character stage, juce_audio_processors for the workflow tests), so
# they build without the plugin formats.
# ---------------------------------------------------------------------------
option(FABCUTIE_BUILD_TESTS "Build the DSP tests" ON)

if(FABCUTIE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(FabCutieTests PRODUCT_NAME "FabCutieTests")
    target_sources(FabCutieTests PRIVATE Tests/EqEngineTests.cpp)
    target_include_directories(FabCutieTests PRIVATE Source)
    target_compile_definitions(FabCutieTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutieTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME EqEngineTests COMMAND FabCutieTests)

    juce_add_console_app(FabCutieCharacterTests PRODUCT_NAME "FabCutieCharacterTests")
    target_sources(FabCutieCharacterTests PRIVATE Tests/CharacterTests.cpp)
    target_include_directories(FabCutieCharacterTests PRIVATE Source)
    target_compile_definitions(FabCutieCharacterTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutieCharacterTests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME CharacterTests COMMAND FabCutieCharacterTests)

    juce_add_console_app(FabCutieDynamicTests PRODUCT_NAME "FabCutieDynamicTests")
    target_sources(FabCutieDynamicTests PRIVATE Tests/DynamicEqTests.cpp)
    target_include_directories(FabCutieDynamicTests PRIVATE Source)
    target_compile_definitions(FabCutieDynamicTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutieDynamicTests
        PRIVATE
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME DynamicEqTests COMMAND FabCutieDynamicTests)

    juce_add_console_app(FabCutieSmartToolsTests PRODUCT_NAME "FabCutieSmartToolsTests")
    target_sources(FabCutieSmartToolsTests PRIVATE Tests/SmartToolsTests.cpp)
    target_include_directories(FabCutieSmartToolsTests PRIVATE Source)
    target_compile_definitions(FabCutieSmartToolsTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutieSmartToolsTests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME SmartToolsTests COMMAND FabCutieSmartToolsTests)

    juce_add_console_app(FabCutieWorkflowTests PRODUCT_NAME "FabCutieWorkflowTests")
    target_sources(FabCutieWorkflowTests PRIVATE
        Tests/WorkflowTests.cpp
        Source/Parameters.cpp
        Source/workflow/ParameterSet.cpp
        Source/workflow/History.cpp
        Source/workflow/Presets.cpp)
    target_include_directories(FabCutieWorkflowTests PRIVATE Source)
    target_compile_definitions(FabCutieWorkflowTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutieWorkflowTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME WorkflowTests COMMAND FabCutieWorkflowTests)

    juce_add_console_app(FabCutiePhaseTests PRODUCT_NAME "FabCutiePhaseTests")
    target_sources(FabCutiePhaseTests PRIVATE Tests/PhaseModeTests.cpp)
    target_include_directories(FabCutiePhaseTests PRIVATE Source)
    target_compile_definitions(FabCutiePhaseTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(FabCutiePhaseTests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME PhaseModeTests COMMAND FabCutiePhaseTests)
endif()
