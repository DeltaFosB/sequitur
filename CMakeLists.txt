cmake_minimum_required(VERSION 3.20)

# Project Definition
project(Sequitur 
    VERSION 0.1.0 
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# 1. Strict Institutional Standards
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Enforce strict ISO C++

# Compiler Flags: Zero Tolerance for Warnings & Max Speed
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3                 # Max Optimization
        -march=native       # Optimize for THIS machine's CPU architecture
        -Wall 
        -Wextra 
        -Werror             # Treat warnings as errors
        -Wpedantic
        -fno-rtti           # Disable Runtime Type Info (Performance)
        -fno-exceptions     # Disable Exceptions (Performance)
    )
endif()

# -----------------------------------------------------------------------------
# 2. Dependencies
# -----------------------------------------------------------------------------
include(FetchContent)

# Find System Thread Support (Required for MetricsWorker background thread)
find_package(Threads REQUIRED)

# Google Test (Unit Testing)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)

# liburing (Linux Async I/O for Gateway)
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED liburing)

# -----------------------------------------------------------------------------
# 3. The Core Library (The Engine)
# -----------------------------------------------------------------------------
# Gather all sources (excluding main.cpp)
file(GLOB_RECURSE SOURCES "src/**/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "src/main.cpp")

add_library(sequitur_core ${SOURCES})

# Explicitly link system threads and liburing to the core engine
target_link_libraries(sequitur_core 
    PUBLIC 
        ${URING_LIBRARIES}
        Threads::Threads
)

# Modern target-based include specification
target_include_directories(sequitur_core PUBLIC include)

# -----------------------------------------------------------------------------
# 4. The Executable (The Application)
# -----------------------------------------------------------------------------
add_executable(sequitur_engine src/main.cpp)
target_link_libraries(sequitur_engine PRIVATE sequitur_core)

# -----------------------------------------------------------------------------
# 5. Testing & Benchmarking
# -----------------------------------------------------------------------------
enable_testing()

# Unit Tests
file(GLOB_RECURSE UNIT_TEST_SOURCES "tests/unit/*.cpp")
add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE sequitur_core gtest_main)

# Integration Tests
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "tests/integration/*.cpp")
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests PRIVATE sequitur_core gtest_main)

# Latency Benchmarks
add_executable(benchmarks tests/benchmarks/bench_pure_latency.cpp)
target_compile_definitions(benchmarks PRIVATE ENABLE_TELEMETRY)
target_link_libraries(benchmarks PRIVATE sequitur_core)
