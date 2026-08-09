cmake_minimum_required(VERSION 3.29)

# Private native implementation of the PAPI core. Consumers link only against the
# public header target; nothing here is part of the packaged ABI surface.
add_library(papi_core STATIC
        core/version.cpp
)

target_include_directories(papi_core
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(papi_core PUBLIC endstone::endstone)
target_compile_features(papi_core PUBLIC cxx_std_20)
target_compile_definitions(papi_core PRIVATE PAPI_VERSION="${PROJECT_VERSION}")
set_target_properties(papi_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

# Sources compiled into the Python extension module. Kept separate from papi_core
# so the core stays free of Python headers and can be unit tested without CPython.
set(PAPI_BINDING_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/python/module.cpp"
        PARENT_SCOPE
)
