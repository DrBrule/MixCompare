cmake_minimum_required(VERSION 3.24)

# Must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(MixCompare VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MC_BUILD_TESTS "Build the unit test runner" ON)
option(MC_COPY_AFTER_BUILD "Install plugins to the user plugin folders after building" ON)

# To use a local JUCE checkout instead of downloading one per build directory:
#   cmake -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE ...
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MixCompare
    PRODUCT_NAME "Mix Compare"
    COMPANY_NAME "GooseHub"
    BUNDLE_ID "com.goosehub.mixcompare"
    PLUGIN_MANUFACTURER_CODE GsHb
    PLUGIN_CODE MxCp
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD ${MC_COPY_AFTER_BUILD})

# Everything except the JUCE plugin client wrappers, shared with the test runner.
set(MC_SOURCES
    src/Parameters.cpp
    src/PluginProcessor.cpp
    src/advice/MixAdvisor.cpp
    src/analysis/LevelMeter.cpp
    src/analysis/LoudnessMeter.cpp
    src/analysis/SourceAnalyser.cpp
    src/analysis/SpectrumAnalyser.cpp
    src/analysis/StereoField.cpp
    src/sources/FilePlayer.cpp
    src/sources/NoiseGenerator.cpp
    src/ui/PluginEditor.cpp
    src/ui/Theme.cpp
    src/ui/UnitViews.cpp)

# The themes and the fonts they use. The font's licence travels with it, as the OFL asks.
juce_add_binary_data(MixCompareData
    NAMESPACE BinaryData
    SOURCES
        resources/themes.json
        resources/fonts/Michroma-Regular.ttf
        resources/fonts/OFL.txt)

set(MC_JUCE_LIBS
    MixCompareData
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp)

set(MC_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_sources(MixCompare PRIVATE ${MC_SOURCES})
target_include_directories(MixCompare PRIVATE src)
target_compile_definitions(MixCompare PUBLIC ${MC_JUCE_DEFINITIONS})
target_link_libraries(MixCompare
    PRIVATE ${MC_JUCE_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MC_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MixCompareTests
        PRODUCT_NAME "MixCompareTests")

    target_sources(MixCompareTests PRIVATE
        ${MC_SOURCES}
        tests/TestMain.cpp
        tests/AdvisorTests.cpp
        tests/AnalysisTests.cpp
        tests/EditorTests.cpp
        tests/ProcessorTests.cpp
        tests/SourceTests.cpp)
    target_include_directories(MixCompareTests PRIVATE src tests)
    target_compile_definitions(MixCompareTests PRIVATE
        ${MC_JUCE_DEFINITIONS}
        JucePlugin_Name="Mix Compare"
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MixCompareTests
        PRIVATE
            ${MC_JUCE_LIBS}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME MixCompareTests COMMAND MixCompareTests)
endif()
