cmake_minimum_required(VERSION 3.29)

find_package(GTest REQUIRED)
include(GoogleTest)

# Native test binary. Every suite links the private core directly so registry,
# parser, and lifecycle internals can be exercised without a running server.
add_executable(papi_test
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
