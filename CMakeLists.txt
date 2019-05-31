cmake_minimum_required(VERSION 3.7)


project(Bistra)
enable_testing()

set(CMAKE_CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


find_package(LLVM CONFIG)
if(NOT LLVM_FOUND OR LLVM_VERSION VERSION_LESS 7.0)
  message("Could not find LLVM. Configure with -DLLVM_DIR=/usr/local/opt/llvm@8/lib/cmake/llvm")
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs support core irreader analysis executionengine instcombine object orcJIT runtimedyld scalaropts transformutils native ipo)

# Export a JSON file with the compilation commands that external tools can use
# to analyze the source code of the project.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)" FORCE)
endif()

if(NOT ENABLE_TESTING)
  message(STATUS "Tests not explicitly enabled/disabled, default to enable")
  set(ENABLE_TESTING YES CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -fno-omit-frame-pointer -O0")

set(BISTRA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BISTRA_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BISTRA_BINARY_DIR}/bin)

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

include_directories (${BISTRA_BINARY_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

file(GLOB_RECURSE header_files include/bistra/*.h)

add_subdirectory(lib/)
add_subdirectory(tools/)
add_subdirectory(tests/)
add_subdirectory(examples/)

