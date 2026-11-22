cmake_minimum_required(VERSION 3.22)

# ---- macOS: build native Apple Silicon (set to "arm64;x86_64" for a universal binary)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(SNAKE420 VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(S420_BUILD_TESTS    "Build the engine unit tests"                 ON)
option(S420_BUILD_SNAPSHOT "Build a tool that renders the UI to a PNG"   OFF)
option(S420_COPY_AFTER_BUILD "Install plug-ins to ~/Library after building" ON)

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
# Snake + TGB-420 as linked modules. Built as an *instrument with MIDI output*: Cubase does not load third-party VST3 MIDI
# effects into MIDI-insert slots, but it does route MIDI out of a VST3 instrument to
# other tracks. (Ableton/Bitwig/Reaper can use it the same way.)
juce_add_plugin(SNAKE420
    COMPANY_NAME                "S420Port"
    BUNDLE_ID                   "com.s420port.snake420"
    PRODUCT_NAME                "SNAKE-420"
    PLUGIN_MANUFACTURER_CODE    S42p
    PLUGIN_CODE                 S420
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Generator
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD     ${S420_COPY_AFTER_BUILD})

set(S420_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/snake/SnakeModule.cpp
    Source/snake/SnakeUI.cpp
    Source/tgb/TgbModule.cpp
    Source/tgb/TgbUI.cpp)
target_sources(SNAKE420 PRIVATE ${S420_SOURCES})

target_include_directories(SNAKE420 PRIVATE Source Source/snake Source/tgb)

target_compile_definitions(SNAKE420 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    # Linux is only used here for compile checks; avoid optional system headers.
    target_compile_definitions(SNAKE420 PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0)
endif()

target_link_libraries(SNAKE420
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Engine unit tests (no JUCE)
if(S420_BUILD_TESTS)
    enable_testing()
    add_executable(snake_test tests/snake_test.cpp)
    target_include_directories(snake_test PRIVATE Source/snake)
    add_test(NAME snake_test COMMAND snake_test)
    add_executable(tgb_test tests/tgb_test.cpp)
    target_include_directories(tgb_test PRIVATE Source/tgb)
    add_test(NAME tgb_test COMMAND tgb_test)
    add_executable(link_test tests/link_test.cpp)
    target_include_directories(link_test PRIVATE Source/snake Source/tgb)
    add_test(NAME link_test COMMAND link_test)
endif()

# ---- Optional: render the editor to a PNG (used to preview the UI)
if(S420_BUILD_SNAPSHOT)
    juce_add_console_app(S420Snapshot PRODUCT_NAME "S420Snapshot")
    target_sources(S420Snapshot PRIVATE tools/snapshot.cpp ${S420_SOURCES})
    target_include_directories(S420Snapshot PRIVATE Source Source/snake Source/tgb)
    target_compile_definitions(S420Snapshot PRIVATE
        JucePlugin_Name="SNAKE-420" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0)
    target_link_libraries(S420Snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
