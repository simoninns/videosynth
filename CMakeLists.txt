cmake_minimum_required(VERSION 3.20)

project(videosynth VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenEXR REQUIRED)

add_library(videosynth_core
    src/biphase_encoder.cpp
    src/cav_code_generator.cpp
    src/clv_code_generator.cpp
    src/fm_encoder.cpp
    src/chroma_encoder.cpp
    src/progressive_frame_source.cpp
    src/generation_stage.cpp
    src/logger.cpp
    src/output_stage.cpp
    src/pipeline.cpp
    src/progressive_frame_source_probe.cpp
    src/project_validator.cpp
    src/signal_shaping.cpp
    src/vits_definition_provider.cpp
    src/vits_generator.cpp
    src/yaml_project_parser.cpp
)

target_include_directories(videosynth_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
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

target_link_libraries(videosynth
    PRIVATE
        videosynth_core
)

enable_testing()

add_executable(videosynth_tests
    tests/test_active_sample_mapping.cpp
    tests/test_biphase_encoder.cpp
    tests/test_cav_code_generators.cpp
    tests/test_clv_code_generators.cpp
    tests/test_fm_encoder.cpp
    tests/test_chroma_encoder.cpp
    tests/test_compliance_harness.cpp
    tests/test_progressive_frame_source.cpp
    tests/test_fixture_projects.cpp
    tests/test_generation_stage.cpp
    tests/test_output_stage.cpp
    tests/test_pipeline.cpp
    tests/test_signal_shaping.cpp
    tests/test_signal_timing_model.cpp
    tests/test_project_validator.cpp
    tests/test_vits_generator_primitives.cpp
    tests/test_vits_definition_provider.cpp
    tests/test_vits_generator_pal.cpp
    tests/test_vits_generator_ntsc.cpp
    tests/test_line_injection_vits_integration.cpp
    tests/test_yaml_project_parser.cpp
    tests/test_timing_constants.cpp
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

set(videosynth_functional_test_patterns
    "^FrameSourceTest\\."
    "^ProjectFixturesTest\\."
    "^ComplianceHarnessTest\\."
    "^GenerationStageTimingTest\\."
    "^GenerationStageChromaTest\\."
    "^GenerationStageProgressiveTest\\."
    "^LineInjectionVitsIntegrationTest\\."
    "^OutputStageTest\\."
)

set(videosynth_unit_test_patterns
    "^ActiveSampleMappingTest\\."
    "^BiphaseEncoderTest\\."
    "^BiphaseUtilsTest\\."
    "^CavCodeGeneratorTest\\."
    "^ClvCodeGeneratorTest\\."
    "^FmEncoderTest\\."
    "^ChromaEncoderTest\\."
    "^PipelineTest\\."
    "^ProjectValidatorTest\\."
    "^SignalShapingTest\\."
    "^SignalTimingModelTest\\."
    "^TimingConstantsTest\\."
    "^SignalLevelsTest\\."
    "^VitsDefinitionProviderTest\\."
    "^VitsGeneratorTest\\."
    "^VitsGeneratorPalTest\\."
    "^VitsGeneratorNtscTest\\."
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
    "foreach(test_name IN LISTS videosynth_discovered_tests)\n"
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
