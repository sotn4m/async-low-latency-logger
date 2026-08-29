cmake_minimum_required(VERSION 3.24)
project(async_low_latency_logger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ALLL_BUILD_BENCHMARKS "Build the benchmark harness" ON)

add_library(logger_core
  src/logger/sync_logger.cpp
)
target_include_directories(logger_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(async_low_latency_logger src/main.cpp)
target_include_directories(async_low_latency_logger PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(async_low_latency_logger PRIVATE logger_core)

if(ALLL_BUILD_BENCHMARKS)
  add_executable(bench_logger benchmarks/bench_main.cpp)
  target_link_libraries(bench_logger PRIVATE logger_core)
endif()
