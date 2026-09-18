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
add_executable(tll_crash_watch src/crash_watch.cpp)
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
    tll_crash_watch
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
target_link_libraries(tll_crash_watch PRIVATE dbghelp)
# Preserve matching optimized symbols for the next bounded crash capture.
target_compile_options(tll_coop_runtime PRIVATE /Zi)
target_link_options(tll_coop_runtime PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME crash_watch COMMAND tll_crash_watch --self-test
        "${CMAKE_CURRENT_BINARY_DIR}/crash-watch-tests")
    set_tests_properties(crash_watch PROPERTIES TIMEOUT 90)
    add_executable(
        tll_nadine_back_frame_policy_tests
        tests/nadine_back_frame_policy_tests.cpp
    )
    target_include_directories(tll_nadine_back_frame_policy_tests PRIVATE src)
    target_compile_features(tll_nadine_back_frame_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            tll_nadine_back_frame_policy_tests
            PRIVATE /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
            tll_nadine_back_frame_policy_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME nadine_back_frame_policy COMMAND tll_nadine_back_frame_policy_tests)

    add_executable(
        tll_nadine_back_clearance_tests
        tests/nadine_back_clearance_tests.cpp
    )
    target_include_directories(tll_nadine_back_clearance_tests PRIVATE src)
    target_compile_features(tll_nadine_back_clearance_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            tll_nadine_back_clearance_tests
            PRIVATE /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
            tll_nadine_back_clearance_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME nadine_back_clearance COMMAND tll_nadine_back_clearance_tests)

    add_executable(
        tll_nadine_pistol_grip_policy_tests
        tests/nadine_pistol_grip_policy_tests.cpp
    )
    target_include_directories(tll_nadine_pistol_grip_policy_tests PRIVATE src)
    target_compile_features(tll_nadine_pistol_grip_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            tll_nadine_pistol_grip_policy_tests
            PRIVATE /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
            tll_nadine_pistol_grip_policy_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME nadine_pistol_grip_policy COMMAND tll_nadine_pistol_grip_policy_tests)

    add_executable(
        tll_nadine_p99_chloe_order_policy_tests
        tests/nadine_p99_chloe_order_policy_tests.cpp
    )
    target_include_directories(tll_nadine_p99_chloe_order_policy_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_chloe_order_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            tll_nadine_p99_chloe_order_policy_tests
            PRIVATE /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
            tll_nadine_p99_chloe_order_policy_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME nadine_p99_chloe_order_policy
        COMMAND tll_nadine_p99_chloe_order_policy_tests
    )
    add_executable(tll_nadine_p99_group_shadow_tests tests/nadine_p99_group_shadow_tests.cpp)
    target_include_directories(tll_nadine_p99_group_shadow_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_group_shadow_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(tll_nadine_p99_group_shadow_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(tll_nadine_p99_group_shadow_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME nadine_p99_group_shadow COMMAND tll_nadine_p99_group_shadow_tests)
    foreach(policy group_program_manifest group_admission group_scope group_readability group_memory group_entry group_budget group_ingress group_fiber source_read source_materialize)
        add_executable(tll_nadine_p99_${policy}_tests tests/nadine_p99_${policy}_tests.cpp)
        target_include_directories(tll_nadine_p99_${policy}_tests PRIVATE src)
        target_compile_features(tll_nadine_p99_${policy}_tests PRIVATE cxx_std_20)
        if(policy STREQUAL "group_memory" OR policy STREQUAL "group_fiber" OR policy STREQUAL "source_read")
            target_compile_definitions(tll_nadine_p99_${policy}_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        endif()
        if(MSVC)
            target_compile_options(tll_nadine_p99_${policy}_tests PRIVATE /W4 /permissive- /EHsc)
        else()
            target_compile_options(tll_nadine_p99_${policy}_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(policy STREQUAL "group_program_manifest")
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/../reports/actor_packages/nadine-dlc-base.pak")
        elseif(policy STREQUAL "source_read")
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/src/coop_runtime.cpp")
        else()
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests)
        endif()
        set_tests_properties(nadine_p99_${policy} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
    endforeach()
endif()
