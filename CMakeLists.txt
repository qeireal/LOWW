cmake_minimum_required(VERSION 3.24)
project(LOWW VERSION 0.2.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
set(JUCE_SOURCE_DIR "" CACHE PATH "Optional local JUCE checkout")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
    FetchContent_MakeAvailable(JUCE)
endif()
add_subdirectory(third_party/signalsmith-stretch)
get_target_property(stretch_includes signalsmith-stretch INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET signalsmith-stretch PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${stretch_includes}")
get_target_property(linear_includes signalsmith-linear INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET signalsmith-linear PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${linear_includes}")
set(formats VST3 Standalone)
if(APPLE)
    list(APPEND formats AU)
endif()
juce_add_plugin(LOWW
    COMPANY_NAME "LOWW Audio"
    BUNDLE_ID "audio.loww.bass"
    PLUGIN_MANUFACTURER_CODE Trfl
    PLUGIN_CODE Trfb
    AU_MAIN_TYPE kAudioUnitType_Effect
    PRODUCT_NAME "LOWW"
    # Rendered from Source/Logo.h: build/LOWWTests --logo "$PWD/Resources/Icon.png" 1024
    ICON_BIG Resources/Icon.png
    FORMATS ${formats}
    IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LOWW uses audio input to process your instrument."
    VST3_CATEGORIES Fx Distortion)
target_sources(LOWW PRIVATE Source/Engine.cpp Source/Processor.cpp Source/Editor.cpp)
# Open Runde (SIL OFL 1.1, see Resources/Fonts/OFL.txt) is embedded so the UI renders identically in every host.
juce_add_binary_data(LOWWFonts HEADER_NAME LOWWFonts.h NAMESPACE LOWWFonts SOURCES
    Resources/Fonts/OpenRunde-Regular.ttf Resources/Fonts/OpenRunde-Medium.ttf
    Resources/Fonts/OpenRunde-Semibold.ttf Resources/Fonts/OpenRunde-Bold.ttf)
target_compile_definitions(LOWW PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_include_directories(LOWW PUBLIC Source)
target_link_libraries(LOWW PRIVATE juce::juce_audio_utils juce::juce_dsp LOWWFonts
    PUBLIC signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_sources(LOWW_Standalone PRIVATE Source/Standalone.cpp)
target_compile_definitions(LOWW_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
option(LOWW_BUILD_TESTS "Build offline audio tests" ON)
if(LOWW_BUILD_TESTS)
    enable_testing()
    add_executable(LOWWTests)
    target_sources(LOWWTests PRIVATE Tests/AudioTests.cpp)
    target_compile_definitions(LOWWTests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>)
    target_link_libraries(LOWWTests PRIVATE LOWW juce::juce_audio_utils juce::juce_dsp)
    add_test(NAME audio-regression COMMAND LOWWTests)
    add_test(NAME instrument-monitoring COMMAND LOWWTests --monitor)
    add_test(NAME vst3-host COMMAND LOWWTests --host $<TARGET_BUNDLE_DIR:LOWW_VST3>)
    if(APPLE)
        add_test(NAME au-host COMMAND LOWWTests --host $<TARGET_BUNDLE_DIR:LOWW_AU>)
    endif()
endif()
