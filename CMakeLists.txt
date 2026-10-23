cmake_minimum_required(VERSION 3.22)
project(MonitorMatch VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "macOS minimal versiya")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: -DJUCE_DIR=<yo'l> berilsa o'shandan, aks holda GitHub'dan yuklab olinadi.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MM_FORMATS AU)
endif()

juce_add_plugin(MonitorMatch
    COMPANY_NAME                "Azimxodjayev Music"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.azimxodjayev.monitormatch"
    PLUGIN_MANUFACTURER_CODE    Azmx
    PLUGIN_CODE                 MnMt
    PRODUCT_NAME                "Monitor Match"
    VERSION                     1.0.0
    FORMATS                     ${MM_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(MonitorMatch PRIVATE
    Source/CorrectionEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MonitorMatch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MonitorMatch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
