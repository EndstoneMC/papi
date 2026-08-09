cmake_minimum_required(VERSION 3.29)

add_library(papi_core STATIC
        core/parser/identifier.cpp
        core/registry/expansion_manager.cpp
        core/diagnostics/error_throttle.cpp
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

set(PAPI_BINDING_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/python/module.cpp"
        PARENT_SCOPE
)
