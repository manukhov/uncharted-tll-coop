cmake_minimum_required(VERSION 3.24)
project(TLLObjectScanner LANGUAGES CXX)

add_executable(tll_object_scanner src/main.cpp)
add_executable(tll_nudge src/nudge.cpp)
add_executable(tll_injector src/injector.cpp)
add_executable(tll_buddy_teleport src/buddy_teleport.cpp)
add_executable(tll_remote_input_client src/remote_input_client.cpp)
add_executable(tll_runtime_code_dump src/runtime_code_dump.cpp)
add_executable(tll_netplayer_probe src/netplayer_probe.cpp)
add_executable(tll_read src/mem_read.cpp)
add_executable(tll_write_dword src/write_dword.cpp)
add_executable(tll_scan_dword src/scan_dword.cpp)
add_executable(tll_slot_dump src/slot_dump.cpp)
add_library(tll_teleport_hook SHARED src/teleport_hook.cpp)
add_library(tll_teleport_hook_v2 SHARED src/teleport_hook.cpp)

foreach(
    target
    tll_object_scanner
    tll_nudge
    tll_injector
    tll_buddy_teleport
    tll_remote_input_client
    tll_runtime_code_dump
    tll_netplayer_probe
    tll_read
    tll_write_dword
    tll_scan_dword
    tll_slot_dump
    tll_teleport_hook
    tll_teleport_hook_v2
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

set_target_properties(
    tll_teleport_hook_v2
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME "tll_teleport_hook_v2"
)

target_link_libraries(tll_teleport_hook PRIVATE ws2_32)
target_link_libraries(tll_teleport_hook_v2 PRIVATE ws2_32)
target_link_libraries(tll_remote_input_client PRIVATE ws2_32)
