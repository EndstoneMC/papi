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
        cpp/test_service_publication.cpp
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

add_executable(papi_consumer_compile cpp/consumer_compile.cpp)
target_link_libraries(papi_consumer_compile PRIVATE endstone_papi::endstone_papi)

# T-009: External provider DSO fixture, built from public headers only.
# Simulates a real C++ provider plugin linking against the installed SDK.
add_library(papi_provider_fixture SHARED fixtures/provider/provider_demo.cpp)
target_link_libraries(papi_provider_fixture PRIVATE endstone_papi::endstone_papi)
set_target_properties(papi_provider_fixture PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON)
# Keep the fixture output next to the test binary so the test can find it by path.
set_target_properties(papi_provider_fixture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# T-009: Test that loads the provider fixture DSO and validates the full
# register -> request -> unregister/disable/shutdown -> unload lifecycle.
add_executable(papi_provider_dso_test cpp/test_provider_dso.cpp)
target_include_directories(papi_provider_dso_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
        ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(papi_provider_dso_test PRIVATE
        papi_core
        GTest::gmock_main
        ${CMAKE_DL_LIBS})
target_compile_definitions(papi_provider_dso_test PRIVATE
        PAPI_PROVIDER_DSO_PATH="$<TARGET_FILE:papi_provider_fixture>")
gtest_discover_tests(papi_provider_dso_test
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120)

# PR2-006: A distinct provider module constructs std::make_shared inside the
# provider and passes it through PlaceholderAPI::registerExpansion. Unlike the
# raw-factory fixture above, this deliberately tests the documented C++ ABI.
add_library(papi_provider_shared_ptr_fixture SHARED
        fixtures/provider_shared/provider_shared_demo.cpp)
target_link_libraries(papi_provider_shared_ptr_fixture PRIVATE endstone_papi::endstone_papi)
set_target_properties(papi_provider_shared_ptr_fixture PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_executable(papi_provider_shared_ptr_dso_test cpp/test_provider_shared_ptr_dso.cpp)
add_dependencies(papi_provider_shared_ptr_dso_test papi_provider_shared_ptr_fixture)
target_include_directories(papi_provider_shared_ptr_dso_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
        ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(papi_provider_shared_ptr_dso_test PRIVATE
        papi_core
        GTest::gmock_main
        ${CMAKE_DL_LIBS})
target_compile_definitions(papi_provider_shared_ptr_dso_test PRIVATE
        PAPI_PROVIDER_SHARED_PTR_DSO_PATH="$<TARGET_FILE:papi_provider_shared_ptr_fixture>")
gtest_discover_tests(papi_provider_shared_ptr_dso_test
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120)
