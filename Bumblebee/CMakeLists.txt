cmake_minimum_required(VERSION 3.22)

# ---- macOS: build native Apple Silicon (set to "arm64;x86_64" for a universal binary)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(BUMBLEBEE VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BEE_BUILD_TESTS    "Build the engine unit tests"                 ON)
option(BEE_BUILD_SNAPSHOT "Build a tool that renders the UI to a PNG"   OFF)
option(BEE_COPY_AFTER_BUILD "Install plug-ins to ~/Library after building" ON)

# ---- JUCE: use a local checkout if present (./JUCE or -DJUCE_DIR=...), otherwise download it
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a JUCE checkout")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- The plug-in
# Built as an *instrument with MIDI output*: Cubase does not load third-party VST3 MIDI
# effects into MIDI-insert slots, but it does route MIDI out of a VST3 instrument to
# other tracks. (Ableton/Bitwig/Reaper can use it the same way.)
juce_add_plugin(Bumblebee
    COMPANY_NAME                "BeeWorks"
    BUNDLE_ID                   "com.beeworks.bumblebee"
    PRODUCT_NAME                "Bumblebee"
    PLUGIN_MANUFACTURER_CODE    Beew
    PLUGIN_CODE                 Bb01
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Generator
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD     ${BEE_COPY_AFTER_BUILD})

target_sources(Bumblebee PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Bumblebee PRIVATE Source)

target_compile_definitions(Bumblebee PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    # Linux is only used here for compile checks; avoid optional system headers.
    target_compile_definitions(Bumblebee PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0)
endif()

target_link_libraries(Bumblebee
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Engine unit tests (no JUCE)
if(BEE_BUILD_TESTS)
    add_executable(bee_test tests/bee_test.cpp)
    target_include_directories(bee_test PRIVATE Source)
    enable_testing()
    add_test(NAME bee_test COMMAND bee_test)
endif()

# ---- Optional: render the editor to a PNG (used to preview the UI)
if(BEE_BUILD_SNAPSHOT)
    juce_add_console_app(BeeSnapshot PRODUCT_NAME "BeeSnapshot")
    target_sources(BeeSnapshot PRIVATE tools/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(BeeSnapshot PRIVATE Source)
    target_compile_definitions(BeeSnapshot PRIVATE
        JucePlugin_Name="Bumblebee" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0)
    target_link_libraries(BeeSnapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
