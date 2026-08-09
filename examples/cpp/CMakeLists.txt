cmake_minimum_required(VERSION 3.29)

option(PAPI_BUILD_EXAMPLES "Build the example C++ consumer plugin" OFF)
if (NOT PAPI_BUILD_EXAMPLES)
    return()
endif ()

# Built in-tree against the parent project's header target, so the example proves
# that a third-party plugin compiles against the packaged public contract.
endstone_add_plugin(papi_example plugin.cpp)
target_link_libraries(papi_example PRIVATE endstone_papi::endstone_papi)
target_compile_features(papi_example PRIVATE cxx_std_20)
