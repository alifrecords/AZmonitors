cmake_minimum_required(VERSION 3.22)

project(AZMonitoring VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: bitta universal binary (Apple Silicon + Intel), Logic/Ableton/Pro Tools uchun
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE: -DJUCE_DIR=/path/to/JUCE berilsa shu ishlatiladi, aks holda yuklab olinadi
# ---------------------------------------------------------------------------
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Formatlar: VST3 (Cubase, Ableton, FL Studio, Studio One, Reaper),
#            AU (Logic Pro, Ableton Mac), AAX (Pro Tools), Standalone
# AAX faqat Avid AAX SDK berilganda yoqiladi:  -DAAX_SDK_PATH=/path/to/aax-sdk
# ---------------------------------------------------------------------------
set(AZ_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AZ_FORMATS AU)
endif()
if(DEFINED AAX_SDK_PATH AND EXISTS "${AAX_SDK_PATH}")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND AZ_FORMATS AAX)
    message(STATUS "AZ Monitoring: AAX (Pro Tools) formati yoqildi")
endif()

option(AZ_COPY_AFTER_BUILD "Build'dan keyin plaginni tizim papkasiga nusxalash" OFF)

juce_add_plugin(AZMonitoring
    COMPANY_NAME                "Azimxodjayev Music"
    COMPANY_WEBSITE             "https://azimxodjayev.music"
    BUNDLE_ID                   "com.azimxodjayevmusic.azmonitoring"
    PRODUCT_NAME                "AZ Monitoring"
    PLUGIN_MANUFACTURER_CODE    Azmx
    PLUGIN_CODE                 AzMn
    FORMATS                     ${AZ_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_None
    COPY_PLUGIN_AFTER_BUILD     ${AZ_COPY_AFTER_BUILD})

target_sources(AZMonitoring PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/Analyzer.cpp
    Source/engine/Diagnosis.cpp
    Source/engine/Profiles.cpp
    Source/engine/ProbeRegistry.cpp
    Source/gui/Views.cpp)

target_include_directories(AZMonitoring PRIVATE Source)

target_compile_definitions(AZMonitoring PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AZMonitoring
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# DSP testlari (EBU Tech 3341 / BS.1770 bo'yicha):  ctest
# ---------------------------------------------------------------------------
option(AZ_BUILD_TESTS "DSP testlarini qurish" ON)
if(AZ_BUILD_TESTS)
    juce_add_console_app(AZDspTests PRODUCT_NAME "AZ DSP Tests")
    target_sources(AZDspTests PRIVATE
        tests/DspTests.cpp
        Source/dsp/Analyzer.cpp
        Source/engine/Diagnosis.cpp
        Source/engine/Profiles.cpp)
    target_include_directories(AZDspTests PRIVATE Source)
    target_compile_definitions(AZDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AZDspTests PRIVATE
        juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME dsp COMMAND AZDspTests)
endif()

# ---------------------------------------------------------------------------
# Ishlab chiquvchi vositasi: interfeys suratlari (sintetik vokal zanjiri bilan)
# ---------------------------------------------------------------------------
option(AZ_BUILD_SNAPSHOT "GUI surat vositasini qurish" OFF)
if(AZ_BUILD_SNAPSHOT)
    juce_add_console_app(AZSnapshot PRODUCT_NAME "AZ Snapshot")
    target_sources(AZSnapshot PRIVATE
        tests/Snapshot.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/dsp/Analyzer.cpp
        Source/engine/Diagnosis.cpp Source/engine/Profiles.cpp Source/engine/ProbeRegistry.cpp Source/gui/Views.cpp)
    target_include_directories(AZSnapshot PRIVATE Source)
    target_compile_definitions(AZSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 AZ_SNAPSHOT=1)
    target_link_libraries(AZSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
