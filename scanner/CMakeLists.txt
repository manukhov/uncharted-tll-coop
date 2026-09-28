cmake_minimum_required(VERSION 3.24)
project(TLLCoopTools LANGUAGES CXX)

enable_language(ASM_MASM)
add_library(tll_p99_native_frame_bridge OBJECT src/nadine_p99_group_native_frame.asm)
add_library(tll_p99_process_frame_bridge OBJECT src/nadine_p99_process_native_frame.asm)

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
add_executable(tll_p99_observation_arm src/p99_observation_arm.cpp)
add_executable(tll_p99_wrist_arm src/p99_observation_arm.cpp)
target_compile_definitions(tll_p99_wrist_arm PRIVATE TLL_P99_WRIST_ARM=1)
# Read-only external transport, built without private fixture seams. Not linked
# into a game-side installer and never opens/attaches to a process by itself.
add_library(tll_p99_wrist_remote_read STATIC src/nadine_p99_wrist_remote_read.cpp)
target_include_directories(tll_p99_wrist_remote_read PUBLIC src)
target_compile_features(tll_p99_wrist_remote_read PUBLIC cxx_std_20)
target_compile_definitions(tll_p99_wrist_remote_read PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_p99_wrist_remote_read PRIVATE /W4 /permissive- /EHsc)
add_library(tll_coop_runtime SHARED src/coop_runtime.cpp
    src/nadine_p99_wrist_invoke.cpp
    src/nadine_p99_wrist_code_windows.cpp
    $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
    $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
option(TLL_P99_SOURCE_OBSERVATION_EXAM "Enable one writer-disabled source observation in an explicit exam build" OFF)
if(TLL_P99_SOURCE_OBSERVATION_EXAM)
    target_compile_definitions(tll_coop_runtime PRIVATE TLL_P99_SOURCE_OBSERVATION_EXAM=1)
endif()

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
    tll_p99_observation_arm
    tll_p99_wrist_arm
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

target_link_libraries(tll_coop_runtime PRIVATE ws2_32 bcrypt)
target_link_libraries(tll_remote_input_client PRIVATE ws2_32)
target_link_libraries(tll_crash_watch PRIVATE dbghelp)
# Preserve matching optimized symbols for the next bounded crash capture.
target_compile_options(tll_coop_runtime PRIVATE /Zi)
target_link_options(tll_coop_runtime PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)

include(CTest)
if(BUILD_TESTING)
    # Complete production preparation in an inert, explicitly bound offline DLL.
    # Its private macro cannot change the separately compiled runtime target.
    add_library(tll_p99_preparation_bridge SHARED tests/p99_preparation_bridge.cpp
        src/nadine_p99_wrist_invoke.cpp
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_p99_preparation_bridge PRIVATE src tests)
    target_compile_features(tll_p99_preparation_bridge PRIVATE cxx_std_20)
    target_compile_definitions(tll_p99_preparation_bridge PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_p99_preparation_bridge PRIVATE /W4 /permissive- /EHsc /Zi)
    target_link_options(tll_p99_preparation_bridge PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    target_link_libraries(tll_p99_preparation_bridge PRIVATE ws2_32)
    set_target_properties(tll_p99_preparation_bridge PROPERTIES PREFIX "")
    add_executable(tll_p99_preparation_bridge_tests tests/p99_preparation_bridge_tests.cpp)
    target_include_directories(tll_p99_preparation_bridge_tests PRIVATE tests)
    target_compile_features(tll_p99_preparation_bridge_tests PRIVATE cxx_std_20)
    target_compile_options(tll_p99_preparation_bridge_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_p99_preparation_bridge_tests PRIVATE tll_p99_preparation_bridge)
    add_test(NAME p99_preparation_bridge COMMAND tll_p99_preparation_bridge_tests)
    set_tests_properties(p99_preparation_bridge PROPERTIES TIMEOUT 30)
    # Full production TU data-phase checks on a read-only owned graph. The
    # test alone defines the inert audit/fault seam; no retail code is mapped.
    add_executable(tll_nadine_p99_wrist_read_tests
        tests/nadine_p99_wrist_read_tests.cpp
        src/nadine_p99_wrist_invoke.cpp
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_nadine_p99_wrist_read_tests PRIVATE src tests)
    target_compile_features(tll_nadine_p99_wrist_read_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_read_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_read_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_nadine_p99_wrist_read_tests PRIVATE ws2_32 bcrypt)
    add_test(NAME nadine_p99_wrist_read COMMAND tll_nadine_p99_wrist_read_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../reports/actor_packages/nadine-dlc-base.pak")
    set_tests_properties(nadine_p99_wrist_read PROPERTIES TIMEOUT 30)
    # Isolated numerical audit helper, not a deployable/injected runtime DLL.
    add_library(tll_p99_native_math_bridge SHARED tests/p99_native_math_bridge.cpp)
    target_include_directories(tll_p99_native_math_bridge PRIVATE src)
    target_compile_features(tll_p99_native_math_bridge PRIVATE cxx_std_20)
    target_compile_options(tll_p99_native_math_bridge PRIVATE /W4 /permissive- /EHsc)
    set_target_properties(tll_p99_native_math_bridge PROPERTIES PREFIX "")
    add_executable(tll_p99_native_math_bridge_tests tests/p99_native_math_bridge_tests.cpp)
    target_include_directories(tll_p99_native_math_bridge_tests PRIVATE src)
    target_compile_features(tll_p99_native_math_bridge_tests PRIVATE cxx_std_20)
    target_compile_options(tll_p99_native_math_bridge_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_p99_native_math_bridge_tests PRIVATE tll_p99_native_math_bridge)
    add_test(NAME p99_native_math_bridge COMMAND tll_p99_native_math_bridge_tests)
    set_tests_properties(p99_native_math_bridge PROPERTIES TIMEOUT 30)
    add_test(NAME crash_watch COMMAND tll_crash_watch --self-test
        "${CMAKE_CURRENT_BINARY_DIR}/crash-watch-tests")
    set_tests_properties(crash_watch PROPERTIES TIMEOUT 90)
    add_test(NAME p99_observation_arm COMMAND tll_p99_observation_arm --self-test)
    add_test(NAME p99_wrist_arm COMMAND tll_p99_wrist_arm --self-test)
    set_tests_properties(p99_wrist_arm PROPERTIES TIMEOUT 20)
    set_tests_properties(p99_observation_arm PROPERTIES TIMEOUT 20)
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
    add_executable(tll_dll_injection_completion_tests tests/dll_injection_completion_tests.cpp)
    target_include_directories(tll_dll_injection_completion_tests PRIVATE src)
    target_compile_features(tll_dll_injection_completion_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_dll_injection_completion_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_dll_injection_completion_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME dll_injection_completion COMMAND tll_dll_injection_completion_tests)
    add_executable(tll_local_appearance_observation_tests tests/local_appearance_observation_tests.cpp)
    target_include_directories(tll_local_appearance_observation_tests PRIVATE src)
    target_compile_features(tll_local_appearance_observation_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(tll_local_appearance_observation_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(tll_local_appearance_observation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME local_appearance_observation COMMAND tll_local_appearance_observation_tests)
    # Isolated original-call/SEH/ABI proof for the compiled, uninstalled route.
    add_executable(tll_nadine_p99_wrist_invoke_tests
        tests/nadine_p99_wrist_invoke_tests.cpp
        src/nadine_p99_wrist_invoke.cpp
        tests/nadine_p99_wrist_invoke_fixture.asm)
    target_include_directories(tll_nadine_p99_wrist_invoke_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_invoke_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_invoke_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_invoke_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc>")
    add_test(NAME nadine_p99_wrist_invoke COMMAND tll_nadine_p99_wrist_invoke_tests)
    set_tests_properties(nadine_p99_wrist_invoke PROPERTIES TIMEOUT 20)
    add_executable(tll_nadine_p99_process_native_frame_tests
        tests/nadine_p99_process_native_frame_tests.cpp
        tests/nadine_p99_process_native_frame_fixture.asm
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_nadine_p99_process_native_frame_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_process_native_frame_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_process_native_frame_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_process_native_frame_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc>")
    add_test(NAME nadine_p99_process_native_frame COMMAND tll_nadine_p99_process_native_frame_tests)
    set_tests_properties(nadine_p99_process_native_frame PROPERTIES TIMEOUT 20)
    # Exercise the actual compiled route with explicitly synthetic admission,
    # time and native bodies. Each case owns a fresh one-shot policy/FLS process.
    add_executable(tll_nadine_p99_wrist_route_tests
        tests/nadine_p99_wrist_route_tests.cpp
        tests/nadine_p99_wrist_route_fixture.asm
        tests/nadine_p99_process_native_frame_fixture.asm
        src/nadine_p99_wrist_invoke.cpp
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_nadine_p99_wrist_route_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_route_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_route_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_route_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc>")
    foreach(route_case normal owner-refuse process-refuse read-refuse callback-fault
            cmd16-fault process-fault async-fault budget-before budget-after expiry
            old-async wrapped-async old-process nested-cmd16 other-lease-fault
            disable-ready disable-role poison-scope duplicate-g0 entry-expiry owner-expiry probe-limit)
        add_test(NAME nadine_p99_wrist_route_${route_case}
            COMMAND tll_nadine_p99_wrist_route_tests ${route_case})
        set_tests_properties(nadine_p99_wrist_route_${route_case} PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(tll_nadine_p99_wrist_worker_tests
        tests/nadine_p99_wrist_worker_tests.cpp
        tests/nadine_p99_wrist_route_fixture.asm
        tests/nadine_p99_process_native_frame_fixture.asm
        src/nadine_p99_wrist_invoke.cpp
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_nadine_p99_wrist_worker_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_worker_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_worker_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_worker_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /EHsc>)
    foreach(worker_case capture idle-expiry last-refusal active-timeout arm-timeout export-exists)
        add_test(NAME nadine_p99_wrist_worker_${worker_case}
            COMMAND tll_nadine_p99_wrist_worker_tests ${worker_case} "${CMAKE_CURRENT_BINARY_DIR}/wrist-worker-tests")
        set_tests_properties(nadine_p99_wrist_worker_${worker_case} PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(tll_nadine_p99_wrist_worker_start_tests tests/nadine_p99_wrist_worker_start_tests.cpp)
    target_include_directories(tll_nadine_p99_wrist_worker_start_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_worker_start_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_worker_start_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_worker_start_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_wrist_worker_start COMMAND tll_nadine_p99_wrist_worker_start_tests)
    add_executable(tll_nadine_p99_wrist_code_tests tests/nadine_p99_wrist_code_tests.cpp
        src/nadine_p99_wrist_code_windows.cpp)
    target_include_directories(tll_nadine_p99_wrist_code_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_code_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_code_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX TLL_P99_WRIST_CODE_AUDIT=1)
    target_compile_options(tll_nadine_p99_wrist_code_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_nadine_p99_wrist_code_tests PRIVATE bcrypt)
    add_test(NAME nadine_p99_wrist_code COMMAND tll_nadine_p99_wrist_code_tests
        "C:/UNCHARTED Legacy of Thieves Collection (2016-2017-2022)/Uncharted Legacy of Thieves Collection/tll.exe")
    add_executable(tll_nadine_p99_wrist_remote_read_tests tests/nadine_p99_wrist_remote_read_tests.cpp
        src/nadine_p99_wrist_remote_read.cpp)
    target_include_directories(tll_nadine_p99_wrist_remote_read_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_remote_read_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_remote_read_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX TLL_P99_WRIST_REMOTE_READ_AUDIT=1)
    target_compile_options(tll_nadine_p99_wrist_remote_read_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_wrist_remote_read COMMAND tll_nadine_p99_wrist_remote_read_tests)
    add_executable(tll_nadine_p99_debug_barrier_tests tests/nadine_p99_debug_barrier_tests.cpp)
    target_compile_features(tll_nadine_p99_debug_barrier_tests PRIVATE cxx_std_20)
    target_compile_options(tll_nadine_p99_debug_barrier_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_debug_barrier COMMAND tll_nadine_p99_debug_barrier_tests)
    set_tests_properties(nadine_p99_debug_barrier PROPERTIES TIMEOUT 30)
    foreach(policy group_program_manifest group_admission group_scope group_readability group_memory group_entry group_budget group_ingress group_fiber group_native_frame source_read source_materialize operand_identity retarget_table_admission source_observation preselection_trace wrist_snapshot wrist_install wrist_handshake)
        add_executable(tll_nadine_p99_${policy}_tests tests/nadine_p99_${policy}_tests.cpp)
        target_include_directories(tll_nadine_p99_${policy}_tests PRIVATE src)
        target_compile_features(tll_nadine_p99_${policy}_tests PRIVATE cxx_std_20)
        if(policy STREQUAL "group_memory" OR policy STREQUAL "group_fiber" OR policy STREQUAL "group_native_frame" OR policy STREQUAL "source_read")
            target_compile_definitions(tll_nadine_p99_${policy}_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        endif()
        if(policy STREQUAL "group_native_frame")
            target_sources(tll_nadine_p99_${policy}_tests PRIVATE
                $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
                tests/nadine_p99_group_native_frame_fixture.asm)
        endif()
        if(MSVC)
            target_compile_options(tll_nadine_p99_${policy}_tests PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc>")
        else()
            target_compile_options(tll_nadine_p99_${policy}_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(policy STREQUAL "group_program_manifest")
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/../reports/actor_packages/nadine-dlc-base.pak")
        elseif(policy STREQUAL "source_read" OR policy STREQUAL "retarget_table_admission")
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/src/coop_runtime.cpp")
        else()
            add_test(NAME nadine_p99_${policy} COMMAND tll_nadine_p99_${policy}_tests)
        endif()
        set_tests_properties(nadine_p99_${policy} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
        if(policy STREQUAL "wrist_handshake")
            set_tests_properties(nadine_p99_${policy} PROPERTIES TIMEOUT 20)
        endif()
    endforeach()
endif()
