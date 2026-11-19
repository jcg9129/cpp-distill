# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(cpp_distill LANGUAGES CXX)
set(DISTILL_VERSION "0.0.0-dev" CACHE STRING "distill release version")
set(DISTILL_COMMIT "unknown" CACHE STRING "distill source commit")
set(DISTILL_LLVM_REVISION "unknown" CACHE STRING "LLVM source revision")
foreach(IDENTITY DISTILL_VERSION DISTILL_COMMIT DISTILL_LLVM_REVISION)
  if(NOT "${${IDENTITY}}" MATCHES "^[A-Za-z0-9._+-]+$")
    message(FATAL_ERROR "Invalid ${IDENTITY}: use a version, revision or commit identifier")
  endif()
endforeach()
configure_file(cmake/BuildVersion.h.in generated/distill_build_version.h @ONLY)
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
include(cmake/EmbedLibrary.cmake)
add_library(distill_core STATIC
  "${CMAKE_CURRENT_BINARY_DIR}/embedded_names.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/embedded_profiles.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/embedded_library.cpp"
  src/core/library/library.cpp
  src/core/profile/toml.cpp
  src/core/profile/profile.cpp
  src/core/profile/probe.cpp
  src/core/profile/facts.cpp
  src/core/validate/preprocess.cpp
  src/core/validate/configuration.cpp
  src/core/render/render.cpp
  src/core/pipeline/pipeline.cpp
  src/core/report/report.cpp
  src/core/report/prune.cpp
  src/core/resolve/resolve.cpp
  src/core/resolve/requirements.cpp
  src/core/lex/scan.cpp
  src/core/names/adl.cpp
  src/core/prune/prune.cpp
  src/core/prune/protocol.cpp
  src/core/prune/extract.cpp
  src/core/names/names.cpp
  src/core/inline/search.cpp
  src/core/inline/inline.cpp
  src/core/command/options.cpp
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
target_include_directories(distill_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(distill_core PUBLIC Threads::Threads)
if(DISTILL_HAVE_ADDCHDIR_NP AND NOT DISTILL_FORCE_FORK_SPAWN)
  target_compile_definitions(distill_core PRIVATE DISTILL_HAVE_ADDCHDIR_NP=1)
endif()
if(WIN32)
  target_compile_definitions(distill_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
option(DISTILL_PROFILE_ONLY "Build LLVM-free diagnostic-only distill for profile CI" OFF)
if(DISTILL_PROFILE_ONLY)
  add_executable(distill src/cli/judge_main.cpp src/cli/judge.cpp)
  target_link_libraries(distill PRIVATE distill_core)
endif()
set(DISTILL_LLVM_DIR "" CACHE PATH "Pinned SDK lib/cmake/llvm directory")
if(DISTILL_LLVM_DIR AND NOT DISTILL_PROFILE_ONLY)
  # LLVM's package dependency checks (e.g. libffi) can compile C probes.
  enable_language(C)
  set(LLVM_DIR "${DISTILL_LLVM_DIR}" CACHE PATH "" FORCE)
  get_filename_component(DISTILL_CMAKE_DIR "${DISTILL_LLVM_DIR}" DIRECTORY)
  set(Clang_DIR "${DISTILL_CMAKE_DIR}/clang" CACHE PATH "" FORCE)
  find_package(Clang REQUIRED CONFIG NO_DEFAULT_PATH PATHS "${Clang_DIR}")
  if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL "23.1.5")
    message(FATAL_ERROR "The resolver requires LLVM 23.1.5")
  endif()
  foreach(LIB clangFrontend clangParse clangSema clangAST clangLex clangBasic)
    get_target_property(LIB_TYPE ${LIB} TYPE)
    if(NOT LIB_TYPE STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "${LIB} must be static, got ${LIB_TYPE}")
    endif()
  endforeach()
  # Reuse the distribution spike's bounded per-header resource embedding.
  get_filename_component(DISTILL_SDK "${DISTILL_LLVM_DIR}/../../.." ABSOLUTE)
  set(RESOURCE_DIR "${DISTILL_SDK}/lib/clang/${LLVM_VERSION_MAJOR}/include")
  if(NOT EXISTS "${RESOURCE_DIR}/stddef.h")
    message(FATAL_ERROR "Missing Clang resource headers: ${RESOURCE_DIR}")
  endif()
  file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${RESOURCE_DIR}" "${RESOURCE_DIR}/*")
  list(SORT RESOURCE_FILES)
  set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
  set(GENERATED_FILES "${GENERATED_DIR}/table.cpp")
  set(RESOURCE_DEPENDS)
  set(INDEX 0)
  foreach(HEADER IN LISTS RESOURCE_FILES)
    list(APPEND GENERATED_FILES "${GENERATED_DIR}/resource_${INDEX}.cpp")
    list(APPEND RESOURCE_DEPENDS "${RESOURCE_DIR}/${HEADER}")
    math(EXPR INDEX "${INDEX} + 1")
  endforeach()
  add_executable(distill_embed_resources spike/embed-resources.cpp)
  add_custom_command(OUTPUT ${GENERATED_FILES}
    COMMAND distill_embed_resources "${RESOURCE_DIR}" "${GENERATED_DIR}"
    DEPENDS distill_embed_resources ${RESOURCE_DEPENDS} VERBATIM)
  add_library(distill_resources STATIC ${GENERATED_FILES})
  target_include_directories(distill_resources PRIVATE spike)
  add_library(distill_clang STATIC src/clang/evaluate.cpp src/clang/prune_extract.cpp)
  target_include_directories(distill_clang PRIVATE spike)
  target_include_directories(distill_clang SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  target_link_libraries(distill_clang PUBLIC distill_core PRIVATE distill_resources clangFrontend clangParse clangSema clangAST clangLex clangBasic)
  add_executable(distill_prune_worker src/clang/prune_worker.cpp)
  target_link_libraries(distill_prune_worker PRIVATE distill_clang)
  add_executable(distill src/cli/main.cpp src/cli/judge.cpp)
  target_link_libraries(distill PRIVATE distill_clang)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/distill-toolchain.txt" CONTENT
"Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER})
Flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}
Compile options: $<TARGET_PROPERTY:distill,COMPILE_OPTIONS>
Link options: $<TARGET_PROPERTY:distill,LINK_OPTIONS>
MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}
macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}
")
  if(UNIX AND NOT APPLE)
    target_link_options(distill PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(distill_prune_worker PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(distill_clang PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/GR-> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>)
  endif()
endif()
include(CTest)
if(BUILD_TESTING)
  add_library(distill_test_evaluate STATIC tests/support/evaluate.cpp tests/support/profile.cpp)
  target_include_directories(distill_test_evaluate PUBLIC tests/support)
  target_link_libraries(distill_test_evaluate PUBLIC distill_core)
  if(TARGET distill_clang)
    target_link_libraries(distill_test_evaluate PUBLIC distill_clang)
    target_compile_definitions(distill_test_evaluate PRIVATE DISTILL_HAS_CLANG=1)
  endif()
  add_subdirectory(tests/unit)
  add_subdirectory(tests/runner)
endif()
