cmake_minimum_required(VERSION 3.24)
project(async_low_latency_logger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ALLL_BUILD_BENCHMARKS "Build the benchmark harness" ON)
option(ALLL_BUILD_TESTS "Build the tests" ON)
option(ALLL_ENABLE_TSAN "Build with -fsanitize=thread" OFF)

add_library(logger_core src/logger/sync_logger.cpp src/logger/async_logger.cpp)
target_include_directories(logger_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

if(ALLL_ENABLE_TSAN)
    target_compile_options(logger_core PUBLIC -fsanitize=thread -g)
    target_link_options(logger_core PUBLIC -fsanitize=thread)
endif()

add_executable(async_low_latency_logger src/main.cpp)
target_include_directories(
    async_low_latency_logger
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(async_low_latency_logger PRIVATE logger_core)

if(ALLL_BUILD_BENCHMARKS)
    add_executable(bench_logger benchmarks/bench_main.cpp)
    target_link_libraries(bench_logger PRIVATE logger_core)
    if(ALLL_ENABLE_TSAN)
        target_compile_options(bench_logger PRIVATE -fsanitize=thread -g)
        target_link_options(bench_logger PRIVATE -fsanitize=thread)
    endif()
endif()

if(ALLL_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()
