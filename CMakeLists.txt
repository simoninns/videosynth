cmake_minimum_required(VERSION 3.20)

project(videosynth VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)

add_library(videosynth_core
    src/chroma_encoder.cpp
    src/frame_source.cpp
    src/generation_stage.cpp
    src/logger.cpp
    src/output_stage.cpp
    src/pipeline.cpp
    src/project_validator.cpp
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
    tests/test_chroma_encoder.cpp
    tests/test_frame_source.cpp
    tests/test_fixture_projects.cpp
    tests/test_generation_stage.cpp
    tests/test_output_stage.cpp
    tests/test_pipeline.cpp
    tests/test_signal_timing_model.cpp
    tests/test_project_validator.cpp
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
gtest_discover_tests(videosynth_tests)
