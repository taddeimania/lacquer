cmake_minimum_required(VERSION 3.22)

# macOS 11+. Builds for the machine you're on by default; for a universal
# binary pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64". Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Lacquer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------------
# By default JUCE is downloaded on first configure. To use a local checkout:
#   cmake -B build -DLACQUER_JUCE_DIR=/path/to/JUCE
set(LACQUER_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")

if(LACQUER_JUCE_DIR)
    add_subdirectory(${LACQUER_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Signal path (no JUCE) -----------------------------------------------------
# The stages, the limiter, the loudness meter and LEARN. The tests build
# against this alone.
add_library(LacquerDSP STATIC
    Source/Engine/Loudness.cpp
    Source/Engine/Master.cpp)
target_include_directories(LacquerDSP PUBLIC Source)
set_target_properties(LacquerDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(LacquerDSP PRIVATE /utf-8)
endif()

# --- Plugin -------------------------------------------------------------------
# Copy the built plugin into the system VST3 folder after each build:
#   macOS   ~/Library/Audio/Plug-Ins/VST3
#   Linux   ~/.vst3
#   Windows C:\Program Files\Common Files\VST3  (needs an administrator shell,
#           so it is off by default there)
if(WIN32)
    set(_lacquer_copy_default OFF)
else()
    set(_lacquer_copy_default ON)
endif()
option(LACQUER_COPY_PLUGIN "Install the plugin into the system VST3 folder after building" ${_lacquer_copy_default})

juce_add_plugin(Lacquer
    COMPANY_NAME                "Taddei"
    BUNDLE_ID                   com.taddei.lacquer
    PLUGIN_MANUFACTURER_CODE    Tadd
    PLUGIN_CODE                 Lacq
    PRODUCT_NAME                "Lacquer"
    DESCRIPTION                 "Mastering assistant"
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Mastering Dynamics EQ
    COPY_PLUGIN_AFTER_BUILD     ${LACQUER_COPY_PLUGIN})

set(LACQUER_PLUGIN_SOURCES
    Source/Settings.cpp
    Source/Presets.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp)

target_sources(Lacquer PRIVATE ${LACQUER_PLUGIN_SOURCES})
target_include_directories(Lacquer PRIVATE Source)
if(MSVC)
    target_compile_options(Lacquer PRIVATE /utf-8)
endif()
target_compile_definitions(Lacquer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Lacquer
    PRIVATE
        LacquerDSP
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Engine tests (no JUCE needed) --------------------------------------------
enable_testing()
add_executable(LacquerEngineTests Tests/EngineTests.cpp)
target_link_libraries(LacquerEngineTests PRIVATE LacquerDSP)
add_test(NAME engine COMMAND LacquerEngineTests)

# --- Optional dev tool --------------------------------------------------------
#   LacquerSnapshot out.png [input.wav output.wav]   render the panel headless, and
#   optionally master a WAV file with the default settings
option(LACQUER_BUILD_TOOLS "Build the snapshot tool" OFF)
if(LACQUER_BUILD_TOOLS)
    juce_add_console_app(LacquerSnapshot PRODUCT_NAME "LacquerSnapshot")
    target_sources(LacquerSnapshot PRIVATE Tools/Snapshot.cpp ${LACQUER_PLUGIN_SOURCES})
    target_include_directories(LacquerSnapshot PRIVATE Source)
    if(MSVC)
        target_compile_options(LacquerSnapshot PRIVATE /utf-8)
    endif()
    target_compile_definitions(LacquerSnapshot PRIVATE
        JucePlugin_Name="Lacquer"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LacquerSnapshot PRIVATE
        LacquerDSP
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
