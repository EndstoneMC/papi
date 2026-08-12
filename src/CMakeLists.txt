cmake_minimum_required(VERSION 3.29)

add_library(papi_core STATIC
        core/parser/bracket_replacer.cpp
        core/parser/identifier.cpp
        core/registry/expansion_manager.cpp
        core/service/placeholder_api_impl.cpp
        core/diagnostics/error_throttle.cpp
        platform/endstone/bootstrap.cpp
        platform/endstone/service_publication.cpp
        platform/endstone/server_platform.cpp
)

target_include_directories(papi_core
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(papi_core PUBLIC endstone::endstone)
target_compile_features(papi_core PUBLIC cxx_std_20)
set_target_properties(papi_core PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

set(PAPI_BINDING_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/python/expansion_trampoline.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/python/gil_safe_proxy.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/python/module.cpp"
        PARENT_SCOPE
)
