cmake_minimum_required(VERSION 3.29)

project(papi VERSION 1.0.0 LANGUAGES CXX)

# Generate the public version header from the CMake project version so that
# getVersion() is header-only and CMakeLists.txt is the single version source.
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/include/endstone_papi/version.h
        @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Endstone requires clang-cl on Windows and Clang/libc++ on Linux. PAPI ships C++
# types across the plugin boundary, so it must use the same toolchain family.
if (WIN32)
    if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
        message(FATAL_ERROR
                "endstone_papi: clang-cl is required on Windows, but CMake detected ${CMAKE_CXX_COMPILER_ID} "
                "(frontend variant: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}). "
                "Reconfigure with CC=clang-cl and CXX=clang-cl.")
    endif ()
    set(CMAKE_LINKER_TYPE LLD)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
elseif (UNIX)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
                "endstone_papi: Clang is required on Linux, but CMake detected ${CMAKE_CXX_COMPILER_ID}. "
                "Reconfigure with CC=clang and CXX=clang++.")
    endif ()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else ()
    message(FATAL_ERROR "endstone_papi: ${CMAKE_SYSTEM_NAME} is not supported")
endif ()

set(PAPI_ENDSTONE_TAG "v0.11.8" CACHE STRING "Endstone tag providing the public API headers")

include(FetchContent)
FetchContent_Declare(
        endstone
        GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
        GIT_TAG ${PAPI_ENDSTONE_TAG}
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(endstone)

# Public consumer contract: headers plus the Endstone API they depend on.
add_library(endstone_papi INTERFACE)
add_library(endstone_papi::endstone_papi ALIAS endstone_papi)
target_include_directories(endstone_papi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(endstone_papi INTERFACE endstone::endstone)
target_compile_features(endstone_papi INTERFACE cxx_std_20)

# Consumed as a subproject (FetchContent/add_subdirectory): expose headers only.
if (NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    return()
endif ()

add_subdirectory(src)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_papi ${PAPI_BINDING_SOURCES})
target_include_directories(_papi PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(_papi PRIVATE papi_core)
set_target_properties(_papi PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # _papi.so is linked with NEEDED libc++.so.1 and libc++abi.so.1 (standard
    # SONAMEs).  tools/repair_wheel.py patches these to Endstone's
    # auditwheel-hashed SONAMEs (libc++-<hash>.so.1.0, libc++abi-<hash>.so.1.0)
    # at wheel build time, so the dynamic linker resolves them from
    # endstone.libs/ via DT_RPATH.  We use DT_RPATH (via --disable-new-dtags)
    # rather than DT_RUNPATH because DT_RPATH propagates to transitive
    # dependencies: libc++'s own NEEDED entries (hashed SONAMEs) are resolved
    # from $ORIGIN/../endstone.libs without needing preloading or import-time
    # symlinks.
    set_target_properties(_papi PROPERTIES
            INSTALL_RPATH "$ORIGIN/../endstone.libs;$ORIGIN"
            LINK_OPTIONS "-Wl,--disable-new-dtags")
endif ()

# Test-only bindings let Python regression tests construct internal types directly.
# Compiled out of release wheels, where BUILD_TESTING is OFF.
if (NOT BUILD_TESTING STREQUAL OFF)
    target_compile_definitions(_papi PRIVATE PAPI_TEST_BINDINGS)
    target_include_directories(_papi PRIVATE ${PROJECT_SOURCE_DIR}/tests/cpp)
endif ()

install(TARGETS _papi DESTINATION "endstone_papi" COMPONENT python)
install(DIRECTORY include/ DESTINATION "endstone_papi/include" COMPONENT python)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/include/ DESTINATION "endstone_papi/include" COMPONENT python)

if (NOT BUILD_TESTING STREQUAL OFF)
    enable_testing()
    add_subdirectory(tests)
endif ()

add_subdirectory(examples/cpp)

# Formats only repository-owned C++ sources; never fetched dependencies.
file(GLOB_RECURSE PAPI_FORMAT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.h.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
find_program(PAPI_CLANG_FORMAT NAMES clang-format)
if (PAPI_CLANG_FORMAT)
    add_custom_target(format
            COMMAND "${PAPI_CLANG_FORMAT}" -i ${PAPI_FORMAT_SOURCES}
            COMMENT "Formatting C++ sources with clang-format"
            VERBATIM)
endif ()
