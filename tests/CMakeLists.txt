# ==============================================================================
#  NeoVoiDrive tests (T20: unit test harness)
#
#  Console target `unit_tests` runs every juce_core::UnitTest-derived case.
#  No external test framework (ARCHITECTURE §5.3 — JUCE UnitTest only).
#
#  Usage:
#    ctest --test-dir build -C Release
#    build/unit_tests_artefacts/Release/unit_tests.exe [--name=...] [--category=...]
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

find_package(Python3 COMPONENTS Interpreter)

# Console executable with hand-written main() (see unit/UnitTestsMain.cpp).
juce_add_console_app(unit_tests
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_compile_features(unit_tests PRIVATE cxx_std_20)

# T37: the plugin processor (ModDelayLine buffers, engines) is a multi-100 KB
# stack frame; MSVC's 1 MB default stack overflows inside P01's renderBuffer
# (0xc00000fd). 8 MB matches a DAW-host context.
if (MSVC)
    target_link_options(unit_tests PRIVATE "/STACK:8388608")
endif()

target_compile_definitions(unit_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    NVD_VOICE_WAV="${CMAKE_CURRENT_LIST_DIR}/assets/voice.wav"
    # JucePlugin_* macros are normally provided by juce_add_plugin's generated
    # definitions; define them here for the standalone test build (T02 needs
    # the real PluginProcessor, which references JucePlugin_Name & friends).
    JucePlugin_Name=\"NeoVoiDrive\"
    JucePlugin_VersionString=\"${PROJECT_VERSION}\"
    JucePlugin_Manufacturer=\"NeoVoi\"
    JucePlugin_ManufacturerCode=0x4e766f69
    JucePlugin_PluginCode=0x4e766476
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_IsSynth=0)

if (MSVC)
    # /Zc:__cplusplus: MSVC keeps __cplusplus at 199711 unless this is passed;
    # make the macro reflect the actual standard (JUCE itself uses _MSVC_LANG,
    # but our own code may rely on __cplusplus).
    # /utf-8: sources are UTF-8; without this MSVC reads them as the system
    # codepage (936) and warns C4819 on any non-ASCII character.
    # Audit P1-4: SSE2 baseline everywhere (no /arch:AVX2 translation units).
    target_compile_options(unit_tests PRIVATE /Zc:__cplusplus /utf-8)
endif()

# Aggregate every unit test source under unit/.  New test files added by later
# DSP tasks (T04/T05-T12...) are picked up automatically via CONFIGURE_DEPENDS.
file(GLOB_RECURSE UNIT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/unit/*.cpp")

target_sources(unit_tests PRIVATE
    ${UNIT_TEST_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/offline_render/RenderCore.cpp" 
    # The real plugin processor (and its parameter layout) is compiled into the
    # test binary so parameter layout / persistence tests exercise production
    # code paths (T02).
    "${CMAKE_CURRENT_LIST_DIR}/../source/PluginProcessor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/PluginEditor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/params/ParameterLayout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/dsp/FormantEngine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/NeoLookAndFeel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/TopBarComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/CrossoverDisplay.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/BandCardComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/BottomBarComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/util/PresetBank.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/util/PresetIO.cpp")

# tests/ may access all of source/ headers (ARCHITECTURE §1). DSP tests
# include "dsp/..." directly; the plugin shell is compiled in only so T02's
# parameter layout / persistence tests can run against the real processor.
# T68: FormantEngine is self-contained. Golden-test audio lives in
# tests/assets, so the test build has no third-party DSP source dependency.
target_include_directories(unit_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../source"
    "${CMAKE_CURRENT_LIST_DIR}")   # offline_render/RenderCore.h (T21 smoke tests)

target_link_libraries(unit_tests
    PRIVATE
        juce::juce_core
        juce::juce_events   # DeletedAtShutdown (JUCE 9) used by the runner
        juce::juce_audio_processors  # APVTS + parameter types (T02)
        juce::juce_gui_basics        # PluginEditor, compiled in for T02 tests
        juce::juce_dsp               # HarmonicExciter oversampling (T09/S1)
        NeoVoiDriveData             # embedded Google Sans Code font (BinaryData)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Register with CTest (exit code reflects pass/fail, see UnitTestsMain.cpp).
add_test(NAME unit_tests COMMAND unit_tests)
set_tests_properties(unit_tests PROPERTIES TIMEOUT 600)

# ==============================================================================
#  T21: offline renderer (wav in -> full engine -> wav out).
#  Console tool; the render core is shared with unit_tests for smoke tests.
# ==============================================================================
juce_add_console_app(offline_render
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_compile_features(offline_render PRIVATE cxx_std_20)

target_compile_definitions(offline_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name=\"NeoVoiDrive\"
    JucePlugin_VersionString=\"${PROJECT_VERSION}\"
    JucePlugin_Manufacturer=\"NeoVoi\"
    JucePlugin_ManufacturerCode=0x4e766f69
    JucePlugin_PluginCode=0x4e766476
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_IsSynth=0)

if (MSVC)
    target_compile_options(offline_render PRIVATE /Zc:__cplusplus /utf-8)
endif()

target_sources(offline_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/offline_render/Main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/offline_render/RenderCore.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/PluginProcessor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/PluginEditor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/params/ParameterLayout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/dsp/FormantEngine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/NeoLookAndFeel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/TopBarComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/CrossoverDisplay.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/BandCardComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/ui/BottomBarComponent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/util/PresetBank.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../source/util/PresetIO.cpp")

target_include_directories(offline_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../source")

target_link_libraries(offline_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics        # PluginProcessor::createEditor chain
        juce::juce_dsp
        NeoVoiDriveData             # embedded Google Sans Code font (BinaryData)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# End-to-end smoke: python generates a short stereo wav, the tool renders it,
# then the script verifies the output file exists and is non-trivial.
if (Python3_FOUND)
    add_test(NAME offline_smoke
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/offline_render/smoke_test.py"
                $<TARGET_FILE:offline_render>)
    set_tests_properties(offline_smoke PROPERTIES TIMEOUT 600)
endif()
