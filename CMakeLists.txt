# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(cpp_distill LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(Threads REQUIRED)
add_library(distill_core STATIC
  src/core/command/command.cpp
  src/core/process/process.cpp
  src/core/process/posix.cpp
  src/core/process/windows.cpp
  src/core/probe/probe.cpp)
target_include_directories(distill_core PUBLIC src)
target_link_libraries(distill_core PUBLIC Threads::Threads)
if(WIN32)
  target_compile_definitions(distill_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests/unit)
endif()
