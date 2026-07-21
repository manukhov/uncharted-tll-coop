cmake_minimum_required(VERSION 3.24)
project(TLLCoopTools LANGUAGES CXX)

add_executable(tll_object_scanner src/object_scanner.cpp)
add_executable(tll_player_nudge src/player_position_nudge.cpp)
add_executable(tll_dll_injector src/dll_injector.cpp)
add_executable(tll_buddy_teleport_toggle src/buddy_teleport_toggle.cpp)
add_executable(tll_remote_input_client src/remote_input_client.cpp)
add_executable(tll_runtime_code_dump src/runtime_code_dump.cpp)
add_executable(tll_netplayer_probe src/netplayer_probe.cpp)
add_executable(tll_memory_reader src/memory_reader.cpp)
add_executable(tll_dword_writer src/dword_writer.cpp)
add_executable(tll_dword_scanner src/dword_scanner.cpp)
add_executable(tll_input_slot_dump src/input_slot_dump.cpp)
add_library(tll_coop_runtime SHARED src/coop_runtime.cpp)

foreach(
    target
    tll_object_scanner
    tll_player_nudge
    tll_dll_injector
    tll_buddy_teleport_toggle
    tll_remote_input_client
    tll_runtime_code_dump
    tll_netplayer_probe
    tll_memory_reader
    tll_dword_writer
    tll_dword_scanner
    tll_input_slot_dump
    tll_coop_runtime
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
    tll_coop_runtime
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME "tll_coop_runtime"
)

target_link_libraries(tll_coop_runtime PRIVATE ws2_32)
target_link_libraries(tll_remote_input_client PRIVATE ws2_32)
