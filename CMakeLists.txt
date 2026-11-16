# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(cpp_distill LANGUAGES C CXX)
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
option(DISTILL_FORCE_FORK_SPAWN "Use fork/exec instead of posix_spawn on POSIX" OFF)
if(NOT WIN32)
  include(CheckSymbolExists)
  set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  check_symbol_exists(posix_spawn_file_actions_addchdir_np "spawn.h" DISTILL_HAVE_ADDCHDIR_NP)
  unset(CMAKE_REQUIRED_DEFINITIONS)
endif()
include(cmake/EmbedNames.cmake)
include(cmake/EmbedProfiles.cmake)
add_library(distill_core STATIC
  "${CMAKE_CURRENT_BINARY_DIR}/embedded_names.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/embedded_profiles.cpp"
  src/core/profile/toml.cpp
  src/core/profile/profile.cpp
  src/core/profile/facts.cpp
  src/core/resolve/resolve.cpp
  src/core/resolve/requirements.cpp
  src/core/lex/scan.cpp
  src/core/names/names.cpp
  src/core/inline/search.cpp
  src/core/inline/inline.cpp
  src/core/command/command.cpp
  src/core/process/process.cpp
  src/core/process/posix.cpp
  src/core/process/windows.cpp
  src/core/probe/probe.cpp)
if(MSVC)
  # Cross-version A1 inventories can contain more than 65K distinct strings.
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/embedded_names.cpp"
    PROPERTIES COMPILE_OPTIONS /bigobj)
endif()
target_include_directories(distill_core PUBLIC src)
target_link_libraries(distill_core PUBLIC Threads::Threads)
if(DISTILL_HAVE_ADDCHDIR_NP AND NOT DISTILL_FORCE_FORK_SPAWN)
  target_compile_definitions(distill_core PRIVATE DISTILL_HAVE_ADDCHDIR_NP=1)
endif()
if(WIN32)
  target_compile_definitions(distill_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
set(DISTILL_LLVM_DIR "" CACHE PATH "Pinned SDK lib/cmake/llvm directory")
if(DISTILL_LLVM_DIR)
  set(LLVM_DIR "${DISTILL_LLVM_DIR}" CACHE PATH "" FORCE)
  get_filename_component(DISTILL_CMAKE_DIR "${DISTILL_LLVM_DIR}" DIRECTORY)
  set(Clang_DIR "${DISTILL_CMAKE_DIR}/clang" CACHE PATH "" FORCE)
  find_package(Clang REQUIRED CONFIG NO_DEFAULT_PATH PATHS "${Clang_DIR}")
  if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL "23.1.5")
    message(FATAL_ERROR "The resolver requires LLVM 23.1.5")
  endif()
  add_library(distill_clang STATIC src/clang/evaluate.cpp)
  target_include_directories(distill_clang SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  target_link_libraries(distill_clang PUBLIC distill_core PRIVATE clangFrontend clangLex clangBasic)
  if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(distill_clang PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/GR-> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>)
  endif()
endif()
include(CTest)
if(BUILD_TESTING)
  add_library(distill_test_evaluate STATIC tests/support/evaluate.cpp)
  target_include_directories(distill_test_evaluate PUBLIC tests/support)
  target_link_libraries(distill_test_evaluate PUBLIC distill_core)
  if(TARGET distill_clang)
    target_link_libraries(distill_test_evaluate PUBLIC distill_clang)
    target_compile_definitions(distill_test_evaluate PRIVATE DISTILL_HAS_CLANG=1)
  endif()
  add_subdirectory(tests/unit)
  add_subdirectory(tests/runner)
endif()
