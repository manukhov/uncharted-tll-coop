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
add_executable(tll_enemy_physics_probe src/enemy_physics_probe.cpp)
add_executable(tll_dword_writer src/dword_writer.cpp)
add_executable(tll_dword_scanner src/dword_scanner.cpp)
add_executable(tll_input_slot_dump src/input_slot_dump.cpp)
add_executable(tll_crash_watch src/crash_watch.cpp)
add_executable(tll_p99_observation_arm src/p99_observation_arm.cpp)
add_executable(tll_p99_wrist_arm src/p99_observation_arm.cpp)
target_compile_definitions(tll_p99_wrist_arm PRIVATE TLL_P99_WRIST_ARM=1)
add_executable(tll_p99_consumed_arm src/p99_observation_arm.cpp)
target_compile_definitions(tll_p99_consumed_arm PRIVATE TLL_P99_CONSUMED_ARM=1)
add_executable(tll_enemy_jointblend_arm src/p99_observation_arm.cpp)
target_compile_definitions(tll_enemy_jointblend_arm PRIVATE TLL_ENEMY_JOINTBLEND_ARM=1)
# Compile the real observer components without fault-injection definitions.
# No DLL/startup, no automatic install, and no link from the campaign runtime.
add_library(tll_enemy_jointblend_core STATIC EXCLUDE_FROM_ALL
    src/enemy_jointblend_hooks.cpp src/enemy_jointblend_install_windows.cpp
    $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
    $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
target_include_directories(tll_enemy_jointblend_core PUBLIC src)
target_compile_features(tll_enemy_jointblend_core PUBLIC cxx_std_20)
target_compile_definitions(tll_enemy_jointblend_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_enemy_jointblend_core PRIVATE /W4 /WX /permissive- /EHsc)
# Read-only external transport, built without private fixture seams. Not linked
# into a game-side installer and never opens/attaches to a process by itself.
add_library(tll_p99_wrist_remote_read STATIC src/nadine_p99_wrist_remote_read.cpp)
target_include_directories(tll_p99_wrist_remote_read PUBLIC src)
target_compile_features(tll_p99_wrist_remote_read PUBLIC cxx_std_20)
target_compile_definitions(tll_p99_wrist_remote_read PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_p99_wrist_remote_read PRIVATE /W4 /permissive- /EHsc)
add_library(tll_p99_debug_windows STATIC src/nadine_p99_debug_windows.cpp)
target_include_directories(tll_p99_debug_windows PUBLIC src)
target_compile_features(tll_p99_debug_windows PUBLIC cxx_std_20)
target_compile_definitions(tll_p99_debug_windows PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_p99_debug_windows PRIVATE /W4 /permissive- /EHsc)
add_library(tll_enemy_jointblend_remote_install STATIC EXCLUDE_FROM_ALL src/enemy_jointblend_remote_install.cpp)
target_include_directories(tll_enemy_jointblend_remote_install PUBLIC src)
target_compile_features(tll_enemy_jointblend_remote_install PUBLIC cxx_std_20)
target_compile_definitions(tll_enemy_jointblend_remote_install PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_enemy_jointblend_remote_install PRIVATE /W4 /WX /permissive- /EHsc)
target_link_libraries(tll_enemy_jointblend_remote_install PUBLIC tll_p99_debug_windows)
add_library(tll_p99_provider_windows STATIC src/nadine_p99_provider_windows.cpp)
target_include_directories(tll_p99_provider_windows PUBLIC src)
target_compile_features(tll_p99_provider_windows PUBLIC cxx_std_20)
target_compile_definitions(tll_p99_provider_windows PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX PSAPI_VERSION=2)
target_compile_options(tll_p99_provider_windows PRIVATE /W4 /permissive- /EHsc)
# Not linked into tll_coop_runtime or tll_p99_wrist_abi. No startup/publication enabled.
add_library(tll_p99_prepared_pair STATIC src/nadine_p99_prepared_pair_windows.cpp)
target_include_directories(tll_p99_prepared_pair PUBLIC src)
target_compile_features(tll_p99_prepared_pair PUBLIC cxx_std_20)
target_compile_definitions(tll_p99_prepared_pair PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(tll_p99_prepared_pair PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(tll_p99_prepared_pair PUBLIC tll_p99_provider_windows tll_p99_wrist_remote_read)
add_library(tll_coop_runtime SHARED src/coop_runtime.cpp
    src/nadine_p99_wrist_invoke.cpp
    src/nadine_p99_wrist_code_windows.cpp
    src/nadine_p99_wrist_prepare_windows.cpp
    $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
    $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
option(TLL_P99_SOURCE_OBSERVATION_EXAM "Enable one writer-disabled source observation in an explicit exam build" OFF)
option(TLL_P99_CONSUMED_CENSUS_EXAM "Enable bounded read-only normal Process/postblend census" OFF)
if(TLL_P99_CONSUMED_CENSUS_EXAM AND TLL_P99_SOURCE_OBSERVATION_EXAM)
    message(FATAL_ERROR "P99 consumed and source observation exams cannot share the FLS package")
endif()
if(TLL_P99_CONSUMED_CENSUS_EXAM)
    target_compile_definitions(tll_coop_runtime PRIVATE TLL_P99_CONSUMED_CENSUS_EXAM=1)
endif()
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
    tll_enemy_physics_probe
    tll_dword_writer
    tll_dword_scanner
    tll_input_slot_dump
    tll_crash_watch
    tll_p99_observation_arm
    tll_p99_wrist_arm
    tll_p99_consumed_arm
    tll_enemy_jointblend_arm
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

# Separate, inert final-image artifact for offline ABI/relocation/unwind audits.
# Excluded from default builds; never replaces or enables the ordinary game DLL. No
# exam/audit macro, initialization, installation or arm entry is exposed here.
add_library(tll_p99_wrist_abi SHARED EXCLUDE_FROM_ALL src/coop_runtime.cpp
    src/nadine_p99_wrist_invoke.cpp src/nadine_p99_wrist_code_windows.cpp
    src/nadine_p99_wrist_prepare_windows.cpp src/nadine_p99_wrist_abi.def
    $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
    $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
target_compile_features(tll_p99_wrist_abi PRIVATE cxx_std_20)
target_compile_definitions(tll_p99_wrist_abi PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN
    NOMINMAX TLL_P99_WRIST_ABI_ARTIFACT=1)
target_compile_options(tll_p99_wrist_abi PRIVATE /W4 /permissive- /EHsc /O2 /Zi)
target_link_options(tll_p99_wrist_abi PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
target_link_libraries(tll_p99_wrist_abi PRIVATE ws2_32 bcrypt)
set_target_properties(tll_p99_wrist_abi PROPERTIES PREFIX "")

# Explicit local entry only, inert DllMain. No game-side startup/ACK/arm and no
# audit seams. Separate filename/export contract from the pinned ABI artifact.
add_library(tll_p99_provider_preparation SHARED EXCLUDE_FROM_ALL
    src/nadine_p99_provider_preparation.cpp src/nadine_p99_provider_preparation.def
    src/nadine_p99_wrist_invoke.cpp src/nadine_p99_wrist_prepare_windows.cpp src/nadine_p99_wrist_code_windows.cpp
    $<TARGET_OBJECTS:tll_p99_native_frame_bridge> $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
target_compile_features(tll_p99_provider_preparation PRIVATE cxx_std_20)
target_compile_definitions(tll_p99_provider_preparation PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX TLL_P99_WRIST_ABI_ARTIFACT=1)
target_compile_options(tll_p99_provider_preparation PRIVATE /W4 /permissive- /EHsc /O2 /Zi)
target_link_options(tll_p99_provider_preparation PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
target_link_libraries(tll_p99_provider_preparation PRIVATE tll_p99_provider_windows bcrypt ws2_32)
set_target_properties(tll_p99_provider_preparation PROPERTIES PREFIX "")

include(CTest)
set(TLL_P99_NATIVE_INVENTORY_DIR "" CACHE PATH "Explicit generated TEST-ONLY native inventory header directory")
if(BUILD_TESTING)
    add_executable(tll_enemy_jointblend_remote_install_tests tests/enemy_jointblend_remote_install_tests.cpp)
    target_link_libraries(tll_enemy_jointblend_remote_install_tests PRIVATE tll_enemy_jointblend_remote_install)
    add_executable(tll_enemy_jointblend_remote_faults_tests tests/enemy_jointblend_remote_install_tests.cpp
        src/enemy_jointblend_remote_install.cpp)
    target_compile_definitions(tll_enemy_jointblend_remote_faults_tests PRIVATE TLL_ENEMY_REMOTE_INSTALL_AUDIT=1)
    target_link_libraries(tll_enemy_jointblend_remote_faults_tests PRIVATE tll_p99_debug_windows)
    add_executable(tll_enemy_jointblend_capture_tests tests/enemy_jointblend_capture_tests.cpp)
    add_executable(tll_enemy_jointblend_collector_tests tests/enemy_jointblend_collector_tests.cpp)
    add_executable(tll_enemy_jointblend_hooks_tests tests/enemy_jointblend_hooks_tests.cpp
        src/enemy_jointblend_hooks.cpp $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    add_executable(tll_enemy_jointblend_install_windows_tests tests/enemy_jointblend_install_windows_tests.cpp
        src/enemy_jointblend_install_windows.cpp)
    target_compile_definitions(tll_enemy_jointblend_install_windows_tests PRIVATE TLL_ENEMY_INSTALL_TESTING=1)
    foreach(enemy_part capture collector hooks install_windows remote_install remote_faults)
        target_include_directories(tll_enemy_jointblend_${enemy_part}_tests PRIVATE src)
        target_compile_features(tll_enemy_jointblend_${enemy_part}_tests PRIVATE cxx_std_20)
        target_compile_definitions(tll_enemy_jointblend_${enemy_part}_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(tll_enemy_jointblend_${enemy_part}_tests PRIVATE /W4 /WX /permissive- /EHsc)
        add_test(NAME enemy_jointblend_${enemy_part} COMMAND tll_enemy_jointblend_${enemy_part}_tests)
        set_tests_properties(enemy_jointblend_${enemy_part} PROPERTIES TIMEOUT 30)
    endforeach()
    add_test(NAME enemy_jointblend_arm COMMAND tll_enemy_jointblend_arm --self-test)
    # Value-only enemy observer contracts. Not linked into the runtime DLL;
    # passing these tests does not install hooks or admit a game-side writer.
    foreach(enemy_contract jointblend_observation jointblend_native_contract)
        add_executable(tll_enemy_${enemy_contract}_tests tests/enemy_${enemy_contract}_tests.cpp)
        target_include_directories(tll_enemy_${enemy_contract}_tests PRIVATE src)
        target_compile_features(tll_enemy_${enemy_contract}_tests PRIVATE cxx_std_20)
        target_compile_options(tll_enemy_${enemy_contract}_tests PRIVATE /W4 /WX /permissive- /EHsc)
        add_test(NAME enemy_${enemy_contract} COMMAND tll_enemy_${enemy_contract}_tests)
        set_tests_properties(enemy_${enemy_contract} PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(tll_enemy_physics_observation_tests tests/enemy_physics_observation_tests.cpp)
    target_include_directories(tll_enemy_physics_observation_tests PRIVATE src)
    target_compile_features(tll_enemy_physics_observation_tests PRIVATE cxx_std_20)
    target_compile_options(tll_enemy_physics_observation_tests PRIVATE /W4 /WX /permissive- /EHsc)
    add_test(NAME enemy_physics_observation COMMAND tll_enemy_physics_observation_tests)
    add_executable(tll_enemy_snapshot_policy_tests tests/enemy_snapshot_policy_tests.cpp)
    target_include_directories(tll_enemy_snapshot_policy_tests PRIVATE src)
    target_compile_features(tll_enemy_snapshot_policy_tests PRIVATE cxx_std_20)
    target_compile_options(tll_enemy_snapshot_policy_tests PRIVATE /W4 /WX /permissive- /EHsc)
    add_test(NAME enemy_snapshot_policy COMMAND tll_enemy_snapshot_policy_tests)
    if(TLL_P99_NATIVE_INVENTORY_DIR)
        if(NOT EXISTS "${TLL_P99_NATIVE_INVENTORY_DIR}/nadine_p99_native_inventory_generated.h")
            message(FATAL_ERROR "Generate/review native inventory first; no automatic profile discovery")
        endif()
        add_executable(tll_p99_bootstrap_native_tests tests/nadine_p99_provider_preparation_tests.cpp)
        target_include_directories(tll_p99_bootstrap_native_tests PRIVATE src "${TLL_P99_NATIVE_INVENTORY_DIR}")
        target_compile_features(tll_p99_bootstrap_native_tests PRIVATE cxx_std_20)
        target_compile_definitions(tll_p99_bootstrap_native_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX PSAPI_VERSION=2
            TLL_P99_BOOTSTRAP_PAD=1 TLL_P99_NATIVE_INVENTORY=1)
        target_compile_options(tll_p99_bootstrap_native_tests PRIVATE /W4 /permissive- /EHsc)
        target_link_libraries(tll_p99_bootstrap_native_tests PRIVATE bcrypt)
        add_dependencies(tll_p99_bootstrap_native_tests tll_p99_provider_preparation)
        foreach(native_case normal race native-path-last native-base-last native-hash-last native-evidence-span native-evidence-iat
                native-prefix-0 native-prefix-1 native-prefix-2 native-prefix-3 native-prefix-4
                native-prefix-5 native-prefix-6 native-prefix-7)
            add_test(NAME nadine_p99_bootstrap_native_${native_case} COMMAND tll_p99_bootstrap_native_tests ${native_case})
            set_tests_properties(nadine_p99_bootstrap_native_${native_case} PROPERTIES TIMEOUT 20)
        endforeach()
    endif()
    foreach(bootstrap_kind small padded)
        add_executable(tll_p99_bootstrap_${bootstrap_kind}_tests tests/nadine_p99_provider_preparation_tests.cpp)
        target_include_directories(tll_p99_bootstrap_${bootstrap_kind}_tests PRIVATE src)
        target_compile_features(tll_p99_bootstrap_${bootstrap_kind}_tests PRIVATE cxx_std_20)
        target_compile_definitions(tll_p99_bootstrap_${bootstrap_kind}_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(tll_p99_bootstrap_${bootstrap_kind}_tests PRIVATE /W4 /permissive- /EHsc)
        target_link_libraries(tll_p99_bootstrap_${bootstrap_kind}_tests PRIVATE bcrypt)
        add_dependencies(tll_p99_bootstrap_${bootstrap_kind}_tests tll_p99_provider_preparation)
    endforeach()
    target_compile_definitions(tll_p99_bootstrap_padded_tests PRIVATE TLL_P99_BOOTSTRAP_PAD=1)
    foreach(bootstrap_case empty null noaccess partial unaligned short version reserved oversize tail path duplicate wrong-self wrong-main wrong-creation wrong-size normal prefix race)
        add_test(NAME nadine_p99_bootstrap_${bootstrap_case} COMMAND tll_p99_bootstrap_small_tests ${bootstrap_case})
        set_tests_properties(nadine_p99_bootstrap_${bootstrap_case} PROPERTIES TIMEOUT 20)
    endforeach()
    foreach(bootstrap_case normal prefix empty race)
        add_test(NAME nadine_p99_bootstrap_padded_${bootstrap_case} COMMAND tll_p99_bootstrap_padded_tests ${bootstrap_case})
        set_tests_properties(nadine_p99_bootstrap_padded_${bootstrap_case} PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(tll_p99_prepared_providers_tests tests/nadine_p99_prepared_providers_tests.cpp)
    target_include_directories(tll_p99_prepared_providers_tests PRIVATE src)
    target_compile_features(tll_p99_prepared_providers_tests PRIVATE cxx_std_20)
    target_compile_options(tll_p99_prepared_providers_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_prepared_providers COMMAND tll_p99_prepared_providers_tests)
    add_executable(tll_p99_prepared_pair_tests tests/nadine_p99_prepared_pair_tests.cpp
        src/nadine_p99_wrist_prepare_windows.cpp src/nadine_p99_wrist_code_windows.cpp)
    target_compile_features(tll_p99_prepared_pair_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_p99_prepared_pair_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_p99_prepared_pair_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_p99_prepared_pair_tests PRIVATE tll_p99_prepared_pair bcrypt)
    add_dependencies(tll_p99_prepared_pair_tests tll_p99_provider_fixture)
    foreach(pair_case normal wrong-prefix provider-before provider-after read-missing read-session
            read-inventory read-after-proof finish-fail proof-fail offer-before offer-after offer-partial)
        add_test(NAME nadine_p99_prepared_pair_${pair_case} COMMAND tll_p99_prepared_pair_tests ${pair_case})
        set_tests_properties(nadine_p99_prepared_pair_${pair_case} PROPERTIES TIMEOUT 20)
    endforeach()
    add_executable(tll_p99_provider_owner_tests tests/nadine_p99_provider_owner_tests.cpp)
    target_include_directories(tll_p99_provider_owner_tests PRIVATE src)
    target_compile_features(tll_p99_provider_owner_tests PRIVATE cxx_std_20)
    target_compile_options(tll_p99_provider_owner_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_provider_owner COMMAND tll_p99_provider_owner_tests)
    add_library(tll_p99_provider_fixture SHARED tests/nadine_p99_provider_fixture.cpp)
    target_compile_features(tll_p99_provider_fixture PRIVATE cxx_std_20)
    add_executable(tll_p99_provider_windows_tests tests/nadine_p99_provider_windows_tests.cpp)
    target_compile_features(tll_p99_provider_windows_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_p99_provider_windows_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX PSAPI_VERSION=2)
    target_compile_options(tll_p99_provider_windows_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_p99_provider_windows_tests PRIVATE tll_p99_provider_windows bcrypt)
    add_dependencies(tll_p99_provider_windows_tests tll_p99_provider_fixture)
    foreach(provider_case normal writable-rw acquire-before-1 acquire-after-1 acquire-before-2 acquire-after-2
            pin-before-1 pin-after-1 pin-before-2 pin-after-2 partial-write seal-before seal-after
            wrong-prefix wrong-path wrong-size wrong-session)
        add_test(NAME nadine_p99_provider_${provider_case} COMMAND tll_p99_provider_windows_tests ${provider_case})
        set_tests_properties(nadine_p99_provider_${provider_case} PROPERTIES TIMEOUT 20)
    endforeach()
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
    add_test(NAME p99_consumed_arm COMMAND tll_p99_consumed_arm --self-test)
    set_tests_properties(p99_consumed_arm PROPERTIES TIMEOUT 20)
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
    add_executable(
        tll_nadine_p99_support_constraint_tests
        tests/nadine_p99_support_constraint_tests.cpp)
    target_include_directories(tll_nadine_p99_support_constraint_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_support_constraint_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            tll_nadine_p99_support_constraint_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(
            tll_nadine_p99_support_constraint_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME nadine_p99_support_constraint
        COMMAND tll_nadine_p99_support_constraint_tests)
    add_executable(tll_nadine_p99_arm_roll_tests tests/nadine_p99_arm_roll_tests.cpp)
    target_include_directories(tll_nadine_p99_arm_roll_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_arm_roll_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(tll_nadine_p99_arm_roll_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(tll_nadine_p99_arm_roll_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME nadine_p99_arm_roll COMMAND tll_nadine_p99_arm_roll_tests)
    add_executable(tll_nadine_p99_arm_reach_tests tests/nadine_p99_arm_reach_tests.cpp)
    target_include_directories(tll_nadine_p99_arm_reach_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_arm_reach_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(tll_nadine_p99_arm_reach_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(tll_nadine_p99_arm_reach_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME nadine_p99_arm_reach COMMAND tll_nadine_p99_arm_reach_tests)
    add_executable(tll_nadine_p99_consumed_commands_tests tests/nadine_p99_consumed_commands_tests.cpp)
    target_include_directories(tll_nadine_p99_consumed_commands_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_consumed_commands_tests PRIVATE cxx_std_20)
    target_compile_options(tll_nadine_p99_consumed_commands_tests PRIVATE /W4 /WX /permissive- /EHsc)
    add_test(NAME nadine_p99_consumed_commands COMMAND tll_nadine_p99_consumed_commands_tests)
    add_executable(tll_nadine_p99_consumed_frame_tests
        tests/nadine_p99_consumed_frame_tests.cpp tests/nadine_p99_consumed_frame_fixture.asm
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>)
    target_include_directories(tll_nadine_p99_consumed_frame_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_consumed_frame_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_consumed_frame_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_consumed_frame_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/EHsc>")
    add_test(NAME nadine_p99_consumed_frame COMMAND tll_nadine_p99_consumed_frame_tests)
    set_tests_properties(nadine_p99_consumed_frame PROPERTIES TIMEOUT 20)
    add_executable(tll_nadine_p99_consumed_owner_tests tests/nadine_p99_consumed_owner_tests.cpp)
    target_include_directories(tll_nadine_p99_consumed_owner_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_consumed_owner_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_consumed_owner_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_consumed_owner_tests PRIVATE /W4 /WX /permissive- /EHsc)
    add_test(NAME nadine_p99_consumed_owner COMMAND tll_nadine_p99_consumed_owner_tests)
    set_tests_properties(nadine_p99_consumed_owner PROPERTIES TIMEOUT 20)
    add_executable(tll_nadine_p99_consumed_runtime_tests
        tests/nadine_p99_consumed_runtime_tests.cpp tests/nadine_p99_consumed_frame_fixture.asm
        $<TARGET_OBJECTS:tll_p99_native_frame_bridge>)
    target_include_directories(tll_nadine_p99_consumed_runtime_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_consumed_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_consumed_runtime_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_consumed_runtime_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/EHsc>")
    add_test(NAME nadine_p99_consumed_runtime COMMAND tll_nadine_p99_consumed_runtime_tests)
    set_tests_properties(nadine_p99_consumed_runtime PROPERTIES TIMEOUT 30)
    add_executable(tll_nadine_p99_consumed_install_tests tests/nadine_p99_consumed_install_tests.cpp)
    target_include_directories(tll_nadine_p99_consumed_install_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_consumed_install_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_consumed_install_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_consumed_install_tests PRIVATE /W4 /WX /permissive- /EHsc)
    add_test(NAME nadine_p99_consumed_install COMMAND tll_nadine_p99_consumed_install_tests)
    set_tests_properties(nadine_p99_consumed_install PROPERTIES TIMEOUT 20)
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
    add_library(tll_nadine_p99_wrist_pin_fixture SHARED tests/nadine_p99_wrist_pin_fixture.cpp)
    add_library(tll_nadine_p99_wrist_prepare_runtime_fixture SHARED tests/nadine_p99_wrist_prepare_runtime_fixture.cpp
        src/nadine_p99_wrist_prepare_windows.cpp src/nadine_p99_wrist_code_windows.cpp
        src/nadine_p99_wrist_invoke.cpp $<TARGET_OBJECTS:tll_p99_native_frame_bridge>
        $<TARGET_OBJECTS:tll_p99_process_frame_bridge>)
    target_include_directories(tll_nadine_p99_wrist_prepare_runtime_fixture PRIVATE src tests)
    target_compile_features(tll_nadine_p99_wrist_prepare_runtime_fixture PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_prepare_runtime_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tll_nadine_p99_wrist_prepare_runtime_fixture PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_nadine_p99_wrist_prepare_runtime_fixture PRIVATE bcrypt ws2_32)
    add_executable(tll_nadine_p99_wrist_prepare_runtime_tests tests/nadine_p99_wrist_prepare_runtime_tests.cpp)
    target_link_libraries(tll_nadine_p99_wrist_prepare_runtime_tests PRIVATE tll_nadine_p99_wrist_prepare_runtime_fixture)
    add_test(NAME nadine_p99_wrist_prepare_runtime COMMAND tll_nadine_p99_wrist_prepare_runtime_tests)
    add_executable(tll_nadine_p99_wrist_prepare_windows_tests tests/nadine_p99_wrist_prepare_windows_tests.cpp
        src/nadine_p99_wrist_prepare_windows.cpp src/nadine_p99_wrist_code_windows.cpp)
    target_include_directories(tll_nadine_p99_wrist_prepare_windows_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_prepare_windows_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_prepare_windows_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX TLL_P99_WRIST_PREPARE_AUDIT=1)
    target_compile_options(tll_nadine_p99_wrist_prepare_windows_tests PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(tll_nadine_p99_wrist_prepare_windows_tests PRIVATE bcrypt tll_p99_wrist_remote_read)
    foreach(prepare_case normal seal-before seal-after)
        add_test(NAME nadine_p99_wrist_prepare_${prepare_case} COMMAND tll_nadine_p99_wrist_prepare_windows_tests
            ${prepare_case} "$<TARGET_FILE:tll_nadine_p99_wrist_pin_fixture>")
        set_tests_properties(nadine_p99_wrist_prepare_${prepare_case} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(tll_nadine_p99_wrist_remote_read_tests tests/nadine_p99_wrist_remote_read_tests.cpp
        src/nadine_p99_wrist_remote_read.cpp)
    target_include_directories(tll_nadine_p99_wrist_remote_read_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_wrist_remote_read_tests PRIVATE cxx_std_20)
    target_compile_definitions(tll_nadine_p99_wrist_remote_read_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX TLL_P99_WRIST_REMOTE_READ_AUDIT=1)
    target_compile_options(tll_nadine_p99_wrist_remote_read_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_wrist_remote_read COMMAND tll_nadine_p99_wrist_remote_read_tests)
    add_executable(tll_nadine_p99_debug_lifecycle_tests tests/nadine_p99_debug_lifecycle_tests.cpp)
    target_include_directories(tll_nadine_p99_debug_lifecycle_tests PRIVATE src)
    target_compile_features(tll_nadine_p99_debug_lifecycle_tests PRIVATE cxx_std_20)
    target_compile_options(tll_nadine_p99_debug_lifecycle_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_debug_lifecycle COMMAND tll_nadine_p99_debug_lifecycle_tests)
    add_executable(tll_nadine_p99_debug_barrier_tests tests/nadine_p99_debug_barrier_tests.cpp src/nadine_p99_debug_windows.cpp)
    target_include_directories(tll_nadine_p99_debug_barrier_tests PRIVATE src)
    target_compile_definitions(tll_nadine_p99_debug_barrier_tests PRIVATE TLL_P99_DEBUG_LIFECYCLE_AUDIT=1)
    target_compile_features(tll_nadine_p99_debug_barrier_tests PRIVATE cxx_std_20)
    target_compile_options(tll_nadine_p99_debug_barrier_tests PRIVATE /W4 /permissive- /EHsc)
    add_test(NAME nadine_p99_debug_barrier COMMAND tll_nadine_p99_debug_barrier_tests)
    set_tests_properties(nadine_p99_debug_barrier PROPERTIES TIMEOUT 30)
    foreach(lifecycle_case normal continue-before continue-after detach-before detach-after)
        add_test(NAME nadine_p99_debug_lifecycle_${lifecycle_case} COMMAND tll_nadine_p99_debug_barrier_tests --lifecycle ${lifecycle_case})
        set_tests_properties(nadine_p99_debug_lifecycle_${lifecycle_case} PROPERTIES TIMEOUT 30)
    endforeach()
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
