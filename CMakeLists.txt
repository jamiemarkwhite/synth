cmake_minimum_required(VERSION 3.22)

# Must come before project(), which otherwise fixes the architecture to the host's.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(uBeat VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(ENGINE_SOURCES
    src/Arpeggiator.cpp
    src/DrumMachine.cpp
    src/Groovebox.cpp
    src/PatternBank.cpp
    src/PatternGenerator.cpp
    src/Sampler.cpp
    src/SynthEngine.cpp
)

juce_add_plugin(uBeat
    COMPANY_NAME "uBeat"
    BUNDLE_ID dev.ubeat.ubeat
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "μBeat uses the microphone to record your own sounds into the sampler."
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ubea
    PLUGIN_CODE Ubt1
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "uBeat"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon-32.png"
    PLUGIN_NAME "μBeat"
)

target_sources(uBeat PRIVATE
    ${ENGINE_SOURCES}
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/StandaloneApp.cpp
    src/ui/NeonLookAndFeel.cpp
    src/ui/Pages.cpp
    src/ui/Widgets.cpp
)

target_compile_definitions(uBeat PUBLIC
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(uBeat
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(uBeatTests PRODUCT_NAME "uBeatTests")

target_sources(uBeatTests PRIVATE
    ${ENGINE_SOURCES}
    tests/TestMain.cpp
    tests/SynthEngineTests.cpp
    tests/ArpeggiatorTests.cpp
    tests/DrumMachineTests.cpp
    tests/SequencerTests.cpp
    tests/SamplerTests.cpp
    tests/SynthTrackTests.cpp
)

target_include_directories(uBeatTests PRIVATE src)

target_compile_definitions(uBeatTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(uBeatTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME uBeatTests COMMAND uBeatTests)

# Development tool: renders the editor's tabs to PNG files.
juce_add_console_app(UiSnapshot PRODUCT_NAME "UiSnapshot")

target_sources(UiSnapshot PRIVATE
    ${ENGINE_SOURCES}
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/NeonLookAndFeel.cpp
    src/ui/Pages.cpp
    src/ui/Widgets.cpp
    tools/UiSnapshot.cpp
)

target_include_directories(UiSnapshot PRIVATE src)

target_compile_definitions(UiSnapshot PRIVATE
    JucePlugin_Name="uBeat"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(UiSnapshot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Development tool: draws the app icon. Run: IconMaker assets
juce_add_console_app(IconMaker PRODUCT_NAME "IconMaker")
target_sources(IconMaker PRIVATE tools/IconMaker.cpp)
target_include_directories(IconMaker PRIVATE src)
target_compile_definitions(IconMaker PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(IconMaker
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
