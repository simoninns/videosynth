cmake_minimum_required(VERSION 3.20)

project(videosynth VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE tooling (clangd, VS Code cpptools).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Version string: PROJECT_VERSION_OVERRIDE (e.g. from a Nix build) wins,
# otherwise the short git commit hash with a "-dirty" suffix when the working
# tree has uncommitted changes.
if(DEFINED PROJECT_VERSION_OVERRIDE)
    set(VIDEOSYNTH_VERSION "${PROJECT_VERSION_OVERRIDE}")
else()
    find_package(Git QUIET)
    set(GIT_COMMIT_HASH "unknown")
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_COMMIT_HASH STREQUAL "")
            set(GIT_COMMIT_HASH "unknown")
        else()
            execute_process(
                COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                RESULT_VARIABLE GIT_DIRTY
                ERROR_QUIET
            )
            if(NOT GIT_DIRTY EQUAL 0)
                set(GIT_COMMIT_HASH "${GIT_COMMIT_HASH}-dirty")
            endif()
        endif()
    endif()
    set(VIDEOSYNTH_VERSION "${GIT_COMMIT_HASH}")
endif()
message(STATUS "VideoSynth version: ${VIDEOSYNTH_VERSION}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/videosynth_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/videosynth_version.h
    @ONLY
)

option(VIDEOSYNTH_BUILD_GUI "Build the Qt6 GUI application" ON)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenEXR REQUIRED)

add_library(videosynth_core
    src/audio_synthesizer.cpp
    src/audio_track_generator.cpp
    src/audio_wav_writer.cpp
    src/biphase_encoder.cpp
    src/biphase_injection_manager.cpp
    src/cav_code_generator.cpp
    src/clv_code_generator.cpp
    src/fm_encoder.cpp
    src/fm_code_generator.cpp
    src/frame_synthesis_pool.cpp
    src/line_placement_engine.cpp
    src/status_code_generator.cpp
    src/chroma_encoder.cpp
    src/progressive_frame_source.cpp
    src/generation_stage.cpp
    src/logger.cpp
    src/noise_injection_stage.cpp
    src/dropout_injection_stage.cpp
    src/output_stage.cpp
    src/path_resolution.cpp
    src/pipeline.cpp
    src/progressive_frame_source_probe.cpp
    src/project_validator.cpp
    src/signal_shaping.cpp
    src/vits_definition_provider.cpp
    src/vits_generator.cpp
    src/yaml_project_emitter.cpp
    src/yaml_project_parser.cpp
    src/osd_renderer.cpp
    src/osd_token_resolver.cpp
)

target_include_directories(videosynth_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Default location of the bundled asset library used by the "{bundled}" logical
# asset root. For dev builds this is the videosynth-assets submodule; installed
# builds should override it (e.g. -DVIDEOSYNTH_BUNDLED_ASSET_DIR=<datadir>) to
# point at the installed asset directory (see the install rules below). The
# VIDEOSYNTH_ASSET_DIR environment variable overrides this at runtime.
if(NOT DEFINED VIDEOSYNTH_BUNDLED_ASSET_DIR)
    set(VIDEOSYNTH_BUNDLED_ASSET_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/videosynth-assets/assets")
endif()
target_compile_definitions(videosynth_core
    PRIVATE
        VIDEOSYNTH_BUNDLED_ASSET_DIR="${VIDEOSYNTH_BUNDLED_ASSET_DIR}"
)

target_link_libraries(videosynth_core
    PUBLIC
        yaml-cpp
        spdlog::spdlog
        SQLite::SQLite3
        OpenEXR::OpenEXR
)

add_executable(videosynth
    src/main.cpp
)

target_include_directories(videosynth
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(videosynth
    PRIVATE
        videosynth_core
)

if(VIDEOSYNTH_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets Svg QUIET)
    if(NOT Qt6_FOUND)
        message(STATUS "Qt6 not found - skipping GUI build")
        set(VIDEOSYNTH_BUILD_GUI OFF)
    endif()
endif()

if(VIDEOSYNTH_BUILD_GUI)
    add_library(videosynth_gui_lib STATIC
        src/gui/asset_roots.cpp
        src/gui/asset_roots.h
        src/gui/disc_skips_editor.cpp
        src/gui/disc_skips_editor.h
        src/gui/disc_skips_model.cpp
        src/gui/disc_skips_model.h
        src/gui/edit_project_dialog.cpp
        src/gui/edit_project_dialog.h
        src/gui/audio_channel_pairs_editor.cpp
        src/gui/audio_channel_pairs_editor.h
        src/gui/forwarding_logger.cpp
        src/gui/forwarding_logger.h
        src/gui/generation_controller.cpp
        src/gui/generation_controller.h
        src/gui/generation_preferences.cpp
        src/gui/generation_preferences.h
        src/gui/line_injection_presenter.cpp
        src/gui/line_injection_presenter.h
        src/gui/line_injections_editor.cpp
        src/gui/line_injections_editor.h
        src/gui/log_message_model.cpp
        src/gui/log_message_model.h
        src/gui/main_window.cpp
        src/gui/main_window.h
        src/gui/new_project_dialog.cpp
        src/gui/new_project_dialog.h
        src/gui/picture_view_widget.cpp
        src/gui/picture_view_widget.h
        src/gui/preferences_dialog.cpp
        src/gui/preferences_dialog.h
        src/gui/preview_frame_service.cpp
        src/gui/preview_frame_service.h
        src/gui/preview_pane.cpp
        src/gui/preview_pane.h
        src/gui/preview_render.cpp
        src/gui/preview_render.h
        src/gui/preview_window.cpp
        src/gui/preview_window.h
        src/gui/project_document.cpp
        src/gui/project_document.h
        src/gui/project_settings_editor.cpp
        src/gui/project_settings_editor.h
        src/gui/project_settings_presenter.cpp
        src/gui/project_settings_presenter.h
        src/gui/project_templates.cpp
        src/gui/project_templates.h
        src/gui/qt_message_bridge.cpp
        src/gui/qt_message_bridge.h
        src/gui/section_block_presenters.cpp
        src/gui/section_block_presenters.h
        src/gui/section_editor.cpp
        src/gui/section_editor.h
        src/gui/section_list_dock.cpp
        src/gui/section_list_dock.h
        src/gui/section_list_model.cpp
        src/gui/section_list_model.h
        src/gui/source_path_model.cpp
        src/gui/source_path_model.h
        src/gui/source_probe_controller.cpp
        src/gui/source_probe_controller.h
        src/gui/theme_color_tokens.h
        src/gui/theme_controller.cpp
        src/gui/theme_controller.h
        src/gui/theme_manager.cpp
        src/gui/theme_manager.h
        src/gui/validation_controller.cpp
        src/gui/validation_controller.h
        src/gui/validation_issues_model.cpp
        src/gui/validation_issues_model.h
        src/gui/videosynth_gui_resources.qrc
        src/gui/waveform_mapping.h
        src/gui/waveform_scope_widget.cpp
        src/gui/waveform_scope_widget.h
        src/gui/welcome_page.cpp
        src/gui/welcome_page.h
    )

    # Qt code generators are enabled per-target so the core library and CLI
    # build identically with or without Qt present.
    set_target_properties(videosynth_gui_lib PROPERTIES
        AUTOMOC ON
        AUTORCC ON
    )

    target_include_directories(videosynth_gui_lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
        PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(videosynth_gui_lib
        PUBLIC
            videosynth_core
            Qt6::Widgets
            Qt6::Svg
    )

    add_executable(videosynth-gui
        src/gui/main.cpp
    )

    set_target_properties(videosynth-gui PROPERTIES AUTOMOC ON)

    target_include_directories(videosynth-gui
        PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(videosynth-gui
        PRIVATE
            videosynth_gui_lib
    )
endif()

# Install rules. The CLI (and GUI, when built) are installed to the standard
# binary directory; the bundled asset library is installed to the data
# directory when the videosynth-assets submodule is checked out. Packaged
# builds set VIDEOSYNTH_BUNDLED_ASSET_DIR to the installed asset location.
include(GNUInstallDirs)

install(TARGETS videosynth RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(VIDEOSYNTH_BUILD_GUI)
    install(TARGETS videosynth-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/videosynth-assets/assets")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/videosynth-assets/assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/videosynth/assets")
endif()

enable_testing()

add_executable(videosynth_tests
    tests/test_active_sample_mapping.cpp
    tests/test_audio_fixture_projects.cpp
    tests/test_audio_pipeline.cpp
    tests/test_audio_sample_conversion.cpp
    tests/test_audio_synthesizer.cpp
    tests/test_audio_track_generator.cpp
    tests/test_audio_wav_writer.cpp
    tests/test_biphase_encoder.cpp
    tests/test_cav_code_generators.cpp
    tests/test_clv_code_generators.cpp
    tests/test_fm_encoder.cpp
    tests/test_frame_enrichment.cpp
    tests/test_frame_synthesis_pool.cpp
    tests/test_line_placement.cpp
    tests/test_timecode_continuity.cpp
    tests/test_chapter_stopbit.cpp
    tests/test_ntsc_frozen.cpp
    tests/test_biphase_injection_manager.cpp
    tests/test_chroma_encoder.cpp
    tests/test_compliance_harness.cpp
    tests/test_progressive_frame_source.cpp
    tests/test_fixture_projects.cpp
    tests/test_generation_stage.cpp
    tests/test_output_stage.cpp
    tests/test_path_resolution.cpp
    tests/test_pipeline.cpp
    tests/test_signal_shaping.cpp
    tests/test_signal_timing_model.cpp
    tests/test_project_validator.cpp
    tests/test_vits_generator_primitives.cpp
    tests/test_vits_definition_provider.cpp
    tests/test_vits_generator_pal.cpp
    tests/test_vits_generator_ntsc.cpp
    tests/test_line_injection_vits_integration.cpp
    tests/test_yaml_project_emitter.cpp
    tests/test_yaml_project_parser.cpp
    tests/test_timing_constants.cpp
    tests/test_biphase_validation.cpp
    tests/test_cross_injection_validation.cpp
    tests/test_ntsc_virs_validation.cpp
    tests/test_biphase_integration.cpp
    tests/test_fm_transitions.cpp
    tests/test_value_constraints.cpp
    tests/test_chapter_encoding.cpp
    tests/test_biphase_system.cpp
    tests/test_biphase_final.cpp
    tests/test_noise_injection.cpp
    tests/test_deterministic_output.cpp
    tests/test_dropout_injection.cpp
    tests/test_osd_renderer.cpp
    tests/test_osd_token_resolver.cpp
)

target_compile_definitions(videosynth_tests
    PRIVATE
        VIDEOSYNTH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(videosynth_tests
    PRIVATE
        videosynth_core
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(videosynth_tests TEST_LIST videosynth_discovered_tests)

if(VIDEOSYNTH_BUILD_GUI)
    add_executable(videosynth_gui_tests
        tests/gui/gui_test_main.cpp
        tests/gui/test_disc_skips_model.cpp
        tests/gui/test_generation_controller.cpp
        tests/gui/test_generation_parity.cpp
        tests/gui/test_generation_preferences.cpp
        tests/gui/test_line_injection_presenter.cpp
        tests/gui/test_log_message_model.cpp
        tests/gui/test_preview_frame_service.cpp
        tests/gui/test_preview_functional.cpp
        tests/gui/test_preview_render.cpp
        tests/gui/test_project_document.cpp
        tests/gui/test_qt_message_bridge.cpp
        tests/gui/test_project_file_lifecycle.cpp
        tests/gui/test_project_settings_presenter.cpp
        tests/gui/test_project_templates.cpp
        tests/gui/test_section_block_presenters.cpp
        tests/gui/test_section_editing_roundtrip.cpp
        tests/gui/test_section_list_model.cpp
        tests/gui/test_source_path_model.cpp
        tests/gui/test_source_probe_controller.cpp
        tests/gui/test_theme_manager.cpp
        tests/gui/test_theme_settings.cpp
        tests/gui/test_validation_controller.cpp
        tests/gui/test_validation_issues_model.cpp
        tests/gui/test_waveform_mapping.cpp
    )

    target_compile_definitions(videosynth_gui_tests
        PRIVATE
            VIDEOSYNTH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(videosynth_gui_tests
        PRIVATE
            videosynth_gui_lib
            GTest::gtest
    )

    gtest_discover_tests(videosynth_gui_tests
        TEST_LIST videosynth_gui_discovered_tests)
endif()

set(videosynth_functional_test_patterns
    "^AudioFixtureProjectsTest\\."
    "^AudioPipelineTest\\."
    "^AudioTrackGeneratorTest\\."
    "^AudioWavWriterTest\\."
    "^FrameSourceTest\\."
    "^ProjectFixturesTest\\."
    "^ComplianceHarnessTest\\."
    "^DeterministicOutputTest\\."
    "^GenerationParityTest\\."
    "^GenerationPreferencesSettingsTest\\."
    "^GenerationStageTest\\."
    "^GenerationStageTimingTest\\."
    "^GenerationStageChromaTest\\."
    "^GenerationStageProgressiveTest\\."
    "^LineInjectionVitsIntegrationTest\\."
    "^OutputStageTest\\."
    "^PreviewFunctionalTest\\."
    "^ProjectFileLifecycleTest\\."
    "^SectionEditingRoundTripTest\\."
    "^ThemeSettingsTest\\."
    "^YamlProjectEmitterFixturesTest\\."
)

set(videosynth_unit_test_patterns
    "^ActiveSampleMappingTest\\."
    "^AudioSampleConversionTest\\."
    "^AudioSynthesizerTest\\."
    "^AudioWavWriterPathTest\\."
    "^BiphaseEncoderTest\\."
    "^BiphaseUtilsTest\\."
    "^CavCodeGeneratorTest\\."
    "^ClvCodeGeneratorTest\\."
    "^ClvTimecodeForFrameTest\\."
    "^FmEncoderTest\\."
    "^FrameEnrichmentTest\\."
    "^FrameSynthesisPoolTest\\."
    "^OrderedFrameReassemblyTest\\."
    "^LinePlacementTest\\."
    "^TimecodeContinuityTest\\."
    "^ChapterStopBitTest\\."
    "^NtscFrozenValuesTest\\."
    "^BiphaseInjectionManagerTest\\."
    "^BiphaseInjectionManagerPalCavTest\\."
    "^BiphaseInjectionManagerPalClvTest\\."
    "^BiphaseInjectionManagerNtscCavTest\\."
    "^BiphaseInjectionManagerNtscClvTest\\."
    "^ChromaEncoderTest\\."
    "^ForwardingLoggerTest\\."
    "^GenerationControllerTest\\."
    "^GenerationPreferencesTest\\."
    "^LogMessageModelTest\\."
    "^PipelineTest\\."
    "^PreviewFrameServiceTest\\."
    "^PreviewRenderTest\\."
    "^ProjectDocumentTest\\."
    "^ProjectDocumentOpenStateTest\\."
    "^ProjectSettingsPresenterTest\\."
    "^ProjectTemplatesTest\\."
    "^ProjectValidatorTest\\."
    "^QtMessageBridgeTest\\."
    "^SectionListModelTest\\."
    "^SectionBlockPresenterTest\\."
    "^SourcePathModelTest\\."
    "^LineInjectionPresenterTest\\."
    "^DiscSkipsModelTest\\."
    "^SourceProbeControllerTest\\."
    "^ValidationControllerTest\\."
    "^ValidationIssuesModelTest\\."
    "^WaveformMappingTest\\."
    "^BiphaseValidationTest\\."
    "^CrossInjectionValidationTest\\."
    "^NtscVirsValidationTest\\."
    "^BiphaseIntegrationTest\\."
    "^FmTransitionTest\\."
    "^ValueConstraintsTest\\."
    "^ChapterEncodingTest\\."
    "^BiphaseSystemTest\\."
    "^BiphaseFinalTest\\."
    "^NoiseInjectionTest\\."
    "^DropoutInjectionTest\\."
    "^OsdRendererTest\\."
    "^OsdTokenResolverTest\\."
    "^PathResolutionTest\\."
    "^SignalShapingTest\\."
    "^SignalTimingModelTest\\."
    "^TimingConstantsTest\\."
    "^SignalLevelsTest\\."
    "^VitsDefinitionProviderTest\\."
    "^VitsGeneratorTest\\."
    "^VitsGeneratorPalTest\\."
    "^VitsGeneratorNtscTest\\."
    "^ThemeManagerTest\\."
    "^YamlProjectEmitterTest\\."
    "^YamlProjectParserTest\\."
)

set(videosynth_test_label_script "set(videosynth_functional_test_patterns\n")
foreach(pattern IN LISTS videosynth_functional_test_patterns)
    string(APPEND videosynth_test_label_script "  [=[${pattern}]=]\n")
endforeach()
string(APPEND videosynth_test_label_script ")\n\n")

string(APPEND videosynth_test_label_script "set(videosynth_unit_test_patterns\n")
foreach(pattern IN LISTS videosynth_unit_test_patterns)
    string(APPEND videosynth_test_label_script "  [=[${pattern}]=]\n")
endforeach()
string(APPEND videosynth_test_label_script ")\n\n")

string(APPEND videosynth_test_label_script
    "foreach(test_name IN LISTS videosynth_discovered_tests videosynth_gui_discovered_tests)\n"
    "  set(test_classification \"\")\n"
    "\n"
    "  foreach(pattern IN LISTS videosynth_functional_test_patterns)\n"
    "    if(\"\${test_name}\" MATCHES \"\${pattern}\")\n"
    "      set(test_classification \"functional\")\n"
    "      break()\n"
    "    endif()\n"
    "  endforeach()\n"
    "\n"
    "  if(test_classification STREQUAL \"\")\n"
    "    foreach(pattern IN LISTS videosynth_unit_test_patterns)\n"
    "      if(\"\${test_name}\" MATCHES \"\${pattern}\")\n"
    "        set(test_classification \"unit\")\n"
    "        break()\n"
    "      endif()\n"
    "    endforeach()\n"
    "  endif()\n"
    "\n"
    "  if(test_classification STREQUAL \"\")\n"
    "    message(FATAL_ERROR \"Discovered test '\${test_name}' is not classified as unit or functional. Update CMakeLists.txt test label patterns.\")\n"
    "  endif()\n"
    "\n"
    "  set_tests_properties(\"\${test_name}\" PROPERTIES LABELS \"\${test_classification}\")\n"
    "endforeach()\n")

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/videosynth_test_labels.cmake"
    CONTENT "${videosynth_test_label_script}")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/videosynth_test_labels.cmake")

find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_FORMAT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/clang-format-stamps")

    set(FORMAT_STAMPS)
    foreach(source IN LISTS ALL_FORMAT_SOURCES)
        file(RELATIVE_PATH rel_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
        string(REPLACE "/" "_" stamp_name "${rel_source}")
        string(REPLACE "." "_" stamp_name "${stamp_name}")
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/clang-format-stamps/${stamp_name}.stamp")
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror "${source}"
            COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
            DEPENDS "${source}"
            COMMENT "clang-format: ${rel_source}"
            VERBATIM
        )
        list(APPEND FORMAT_STAMPS "${stamp}")
    endforeach()

    add_custom_target(clang_format_check ALL DEPENDS ${FORMAT_STAMPS})
    add_dependencies(videosynth clang_format_check)
    add_dependencies(videosynth_tests clang_format_check)
    if(VIDEOSYNTH_BUILD_GUI)
        add_dependencies(videosynth-gui clang_format_check)
        add_dependencies(videosynth_gui_tests clang_format_check)
    endif()
else()
    message(STATUS "clang-format not found — format checks disabled")
endif()

# clang-tidy runs as part of the build for local development and CI, where the
# dev shell supplies a matching, wrapped clang-tools. The release/package build
# (nix run) must leave this OFF: its sandbox only exposes the stdenv's unwrapped
# clang-tidy, which cannot resolve libc++ headers (e.g. <cstdint>) and fails on
# otherwise-valid sources.
option(VIDEOSYNTH_ENABLE_CLANG_TIDY
    "Run clang-tidy static analysis as part of the build" ON)

if(VIDEOSYNTH_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)

    if(CLANG_TIDY_EXE)
        set_target_properties(videosynth_core videosynth videosynth_tests
            PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
        )
        if(VIDEOSYNTH_BUILD_GUI)
            set_target_properties(videosynth_gui_lib videosynth-gui
                videosynth_gui_tests
                PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
            )
        endif()
    else()
        message(STATUS "clang-tidy not found — static analysis disabled")
    endif()
else()
    message(STATUS "clang-tidy disabled (VIDEOSYNTH_ENABLE_CLANG_TIDY=OFF)")
endif()
