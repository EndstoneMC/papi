cmake_minimum_required(VERSION 3.29)

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(papi_test
        cpp/test_bracket_replacer.cpp
        cpp/test_error_throttle.cpp
        cpp/test_expansion_manager.cpp
        cpp/test_identifier.cpp
        cpp/test_events.cpp
        cpp/test_lifecycle.cpp
        cpp/test_public_api.cpp
        cpp/test_relational.cpp
        cpp/test_service.cpp
        cpp/test_version.cpp
)

target_include_directories(papi_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(papi_test PRIVATE papi_core GTest::gmock_main)

gtest_discover_tests(papi_test
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120
)

file(GLOB PAPI_STATIC_HEADERS RELATIVE "${PROJECT_SOURCE_DIR}/include/endstone_papi"
        "${PROJECT_SOURCE_DIR}/include/endstone_papi/*.h")
file(GLOB PAPI_GENERATED_HEADERS RELATIVE "${PROJECT_BINARY_DIR}/generated/include/endstone_papi"
        "${PROJECT_BINARY_DIR}/generated/include/endstone_papi/*.h")
set(PAPI_PUBLIC_HEADERS ${PAPI_STATIC_HEADERS} ${PAPI_GENERATED_HEADERS})
list(REMOVE_DUPLICATES PAPI_PUBLIC_HEADERS)

set(PAPI_HEADER_TU_SOURCES "")
foreach (header IN LISTS PAPI_PUBLIC_HEADERS)
    string(REGEX REPLACE "\.h$" "" stem "${header}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/headers/include_${stem}.cpp")
    file(CONFIGURE OUTPUT "${generated}" CONTENT
            "#include <endstone_papi/${header}>\n" @ONLY)
    list(APPEND PAPI_HEADER_TU_SOURCES "${generated}")
endforeach ()

add_library(papi_header_self_contained OBJECT ${PAPI_HEADER_TU_SOURCES})
target_link_libraries(papi_header_self_contained PRIVATE endstone_papi::endstone_papi)
target_compile_options(papi_header_self_contained PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated-declarations>)

add_executable(papi_consumer_compile cpp/consumer_compile.cpp)
target_link_libraries(papi_consumer_compile PRIVATE endstone_papi::endstone_papi)
