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

# -----------------------------------------------------------------------------
# 3. The Core Library (The Engine)
# -----------------------------------------------------------------------------
# Gather all core trading engine math and matching logic sources
file(GLOB_RECURSE SOURCES "src/**/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "src/main.cpp")
list(FILTER SOURCES EXCLUDE REGEX "src/utils/MetricsWorker.cpp")

add_library(sequitur_core ${SOURCES})

# Target-based include specification for the engine math headers
target_include_directories(sequitur_core PUBLIC include)

# -----------------------------------------------------------------------------
# 4. The Main Application Executable (The Production Context)
# -----------------------------------------------------------------------------
# Creates the production executable from main.cpp and embeds the MetricsWorker translation unit
add_executable(sequitur_engine 
    src/main.cpp
    src/utils/MetricsWorker.cpp
)

# Link the shared-nothing core matching logic and native system thread support
target_link_libraries(sequitur_engine 
    PRIVATE 
        sequitur_core
        Threads::Threads
)

# Specify include boundaries for the application context
target_include_directories(sequitur_engine PRIVATE include)

# -----------------------------------------------------------------------------
# 5. Testing & Benchmarking (Direct Executables)
# -----------------------------------------------------------------------------
# Unit Tests
file(GLOB_RECURSE UNIT_TEST_SOURCES "tests/unit/*.cpp")
add_executable(unit_tests 
    ${UNIT_TEST_SOURCES}
    src/utils/MetricsWorker.cpp
)
target_link_libraries(unit_tests PRIVATE sequitur_core gtest_main Threads::Threads)

# Integration Tests
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "tests/integration/*.cpp")
add_executable(integration_tests 
    ${INTEGRATION_TEST_SOURCES}
    src/utils/MetricsWorker.cpp
)
target_link_libraries(integration_tests PRIVATE sequitur_core gtest_main Threads::Threads)

# Latency Benchmarks (Exempted Standalone Target)
# Bypasses the core static library boundary to compile matching source files directly
add_executable(benchmarks 
    tests/benchmarks/bench_pure_latency.cpp
    src/core/MatchingEngine.cpp
    src/utils/MetricsWorker.cpp
)

# Target-based include path specification for the standalone benchmark executable
target_include_directories(benchmarks PRIVATE include)

target_compile_definitions(benchmarks PRIVATE ENABLE_TELEMETRY)

# Link threads so the benchmark can test the decoupled pipeline harnesses
target_link_libraries(benchmarks PRIVATE Threads::Threads)
