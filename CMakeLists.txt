cmake_minimum_required(VERSION 3.24)
project(heliosrt VERSION 0.1.0 LANGUAGES CXX)

option(HELIOS_ENABLE_CUDA "Build CUDA/TensorRT runtime components" OFF)
option(HELIOS_BUILD_TESTS "Build CPU unit and stress tests" ON)
option(HELIOS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(helios_core
    src/runtime/mock_executor.cpp
    src/kv/page_allocator.cpp
    src/scheduler/continuous_scheduler.cpp)
target_include_directories(helios_core PUBLIC include)
target_compile_options(helios_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(HELIOS_ENABLE_SANITIZERS)
  target_compile_options(helios_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(helios_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_executable(helios_sim apps/helios_sim.cpp)
target_link_libraries(helios_sim PRIVATE helios_core)

add_executable(helios_trace_replay apps/trace_replay.cpp)
target_link_libraries(helios_trace_replay PRIVATE helios_core)

if(HELIOS_BUILD_TESTS)
  enable_testing()
  foreach(test_name IN ITEMS page_allocator scheduler runtime)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_link_libraries(test_${test_name} PRIVATE helios_core)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
  add_test(NAME trace_replay
      COMMAND helios_trace_replay ${CMAKE_CURRENT_SOURCE_DIR}/bench/traces/smoke.csv)
endif()

if(HELIOS_ENABLE_CUDA)
  message(FATAL_ERROR "CUDA integration is intentionally gated until Phase 1; see docs/IMPLEMENTATION_PLAN.md")
endif()
