# ==============================================================================
#  NeoVoiDrive — top-level CMake project (JUCE 9, VST3 + Standalone)
#
#  References:
#    - docs/SPEC.md §7 (platform, formats, module list, metadata)
#    - docs/ARCHITECTURE.md §5 (third-party integration)
#    - https://github.com/juce-framework/JUCE/blob/9.0.0/docs/CMake%20API.md (R10)
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(NeoVoiDrive
    VERSION 0.0.1
    DESCRIPTION "Voice-style vocal texture effect plugin"
    LANGUAGES C CXX)

# ------------------------------------------------------------------------------
#  JUCE 9.0.0. A local checkout under stuff/JUCE (not committed) wins when
#  present; otherwise the pinned upstream tag is fetched. Treat either copy as
#  read-only.
# ------------------------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stuff/JUCE/CMakeLists.txt")
    add_subdirectory(stuff/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.0
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
#  Plugin target.  FORMATS: VST3 + Standalone per SPEC §7 (Q2).
#  Standalone exists for local debugging/offline tests only; CPack installs
#  the VST3 bundle exclusively (2026-11-08).
#  Metadata per SPEC §7 ("暂定" values).
# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
#  Embedded binary data: the Google Sans Code variable font ships INSIDE the
#  plugin (user spec — file-system loading fails in shipped VST3/Standalone
#  layouts). The generated BinaryData.h symbols are used by NeoLookAndFeel.
#  (UI_STYLE_GUIDE §7: embedded Google Sans Code; single global font.)
# ------------------------------------------------------------------------------
juce_add_binary_data(NeoVoiDriveData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/GoogleSansCode-VariableFont_MONO,wght.ttf"
    NAMESPACE
        NeoVoiDrive)

juce_add_plugin(NeoVoiDrive
    VERSION 0.0.1
    PRODUCT_NAME "NeoVoiDrive"
    PLUGIN_NAME "NeoVoiDrive"
    COMPANY_NAME "M.R.V.D."
    BUNDLE_ID "com.neovoi.neovoidrive"
    PLUGIN_MANUFACTURER_CODE Nvoi
    PLUGIN_CODE Nvdv
    VST3_CATEGORIES "Fx|Vocal"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    FORMATS VST3 Standalone)

# Generate JuceHeader.h into the build tree (optional convenience, ARCHITECTURE §5.1).
# Sources below include module headers directly, so this is not required to compile.
juce_generate_juce_header(NeoVoiDrive)

# SPEC §7: C++20.
target_compile_features(NeoVoiDrive PRIVATE cxx_std_20)

# Disable JUCE features we do not use, to avoid pulling in optional dependencies
# (WebView2 / Curl) and legacy VST2 replacement code.  Follows the JUCE 9.0
# official CMake plugin example.
target_compile_definitions(NeoVoiDrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Sources are UTF-8; without /utf-8 MSVC reads them as the system codepage
# (936) and warns C4819 on any non-ASCII comment character.
# ------------------------------------------------------------------------------
#  Optional LTO (release packaging; MSVC /GL + /LTCG). Off by default because
#  it roughly doubles link time; enable with -DNVD_ENABLE_LTO=ON for final
#  release builds (README: optional build switches).
# ------------------------------------------------------------------------------
option(NVD_ENABLE_LTO "Enable interprocedural optimization for release builds" OFF)
if (NVD_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if (MSVC)
    # Audit P1-4: the whole binary (including FormantEngine.cpp) stays at the
    # SSE2 baseline — the README/AGENTS promise pre-Haswell compatibility, and
    # MultibandEngine prepares/warms both formant engines even when Formant is
    # off, so an /arch:AVX2 translation unit would fault on non-AVX2 CPUs at
    # prepareToPlay. /utf-8 applies to the whole target.
    target_compile_options(NeoVoiDrive PRIVATE /utf-8)
endif()

# ------------------------------------------------------------------------------
#  Project sources
# ------------------------------------------------------------------------------
target_sources(NeoVoiDrive
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/params/ParameterLayout.cpp
        source/dsp/FormantEngine.cpp
        source/ui/NeoLookAndFeel.cpp
        source/ui/TopBarComponent.cpp
        source/ui/CrossoverDisplay.cpp
        source/ui/BandCardComponent.cpp
        source/ui/BottomBarComponent.cpp
        source/util/PresetBank.cpp
        source/util/PresetIO.cpp)

# T68: FormantEngine is self-contained (JUCE FFT + cepstral envelope warp).
# There is no third-party DSP include path; golden-test audio lives in
# tests/assets/voice.wav.
target_include_directories(NeoVoiDrive PRIVATE
    source)

# ------------------------------------------------------------------------------
#  JUCE modules (SPEC §7 module list)
# ------------------------------------------------------------------------------
target_link_libraries(NeoVoiDrive
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        NeoVoiDriveData
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# LTO: use the NVD_ENABLE_LTO option above (juce_recommended_lto_flags is
# superseded by it).

# ------------------------------------------------------------------------------
#  Tests (T20: unit test harness; offline renderer comes with T21)
# ------------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ------------------------------------------------------------------------------
#  Release packaging (audit P1-5): `cpack -C Release` produces a ZIP with the
#  VST3 bundle and the third-party licence texts.  The Standalone target is
#  built for local debugging only and is intentionally not installed.
#  Only build artifacts are installed; the optional local stuff/ checkout is
#  not distributed.
# ------------------------------------------------------------------------------
install(DIRECTORY "${CMAKE_BINARY_DIR}/NeoVoiDrive_artefacts/$<CONFIG>/VST3/NeoVoiDrive.vst3"
        DESTINATION "VST3"
        COMPONENT runtime)
install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES/GoogleSansCode-OFL.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/THIRD_PARTY_LICENSES.md"
        DESTINATION "licenses"
        COMPONENT runtime)

set(CPACK_PACKAGE_NAME "NeoVoiDrive")
set(CPACK_PACKAGE_VENDOR "M.R.V.D.")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "NeoVoiDrive-${PROJECT_VERSION}-win64")
# Only the runtime component: JUCE's own install rules (module headers /
# juceaide / CMake config) belong to the default component and must not ship.
set(CPACK_COMPONENTS_ALL runtime)
# Install only the runtime component of this project; JUCE's own install rules
# (module headers / juceaide / CMake config) belong to the default component.
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};NeoVoiDrive;runtime;/")
set(CPACK_GENERATOR "ZIP")
include(CPack)
