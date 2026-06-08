cmake_minimum_required(VERSION 3.24)
project(TLLObjectScanner LANGUAGES CXX)

add_executable(tll_object_scanner src/main.cpp)
add_executable(tll_nudge src/nudge.cpp)
add_executable(tll_injector src/injector.cpp)
add_executable(tll_buddy_teleport src/buddy_teleport.cpp)
add_library(tll_teleport_hook SHARED src/teleport_hook.cpp)

foreach(
    target
    tll_object_scanner
    tll_nudge
    tll_injector
    tll_buddy_teleport
    tll_teleport_hook
)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(
        ${target}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
    target_compile_options(
        ${target}
        PRIVATE
            /W4
            /permissive-
            /EHsc
    )
endforeach()

set_target_properties(
    tll_teleport_hook
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME "tll_teleport_hook"
)
