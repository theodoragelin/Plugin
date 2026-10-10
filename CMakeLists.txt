cmake_minimum_required(VERSION 3.22)

# Universal binary + deployment target for macOS builds (ignored elsewhere).
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(Selene VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Selene
    COMPANY_NAME "Agelin Audio"
    BUNDLE_ID "com.agelinaudio.selene"
    PLUGIN_MANUFACTURER_CODE Agln
    PLUGIN_CODE Slne
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Selene"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Selene PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Selene PRIVATE Source)

target_compile_definitions(Selene PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Selene
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test (no JUCE): renders impulse responses and checks stability.
add_executable(selene_render_test tests/render_test.cpp)
target_include_directories(selene_render_test PRIVATE Source)

# Developer tool: renders the editor to PNGs (needs a display; use xvfb-run on Linux).
option(SELENE_BUILD_SNAPSHOT "Build the UI snapshot tool" OFF)
if(SELENE_BUILD_SNAPSHOT)
    juce_add_console_app(selene_snapshot PRODUCT_NAME "SeleneSnapshot")
    target_sources(selene_snapshot PRIVATE tests/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(selene_snapshot PRIVATE Source)
    target_compile_definitions(selene_snapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Selene" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(selene_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
