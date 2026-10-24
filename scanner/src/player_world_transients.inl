// Included after install_peer_lifecycle, once all network/action metadata is
// defined. This is deliberately NOT an autonomous reset hook.
//
// REQUIRED CALLER PROOF: world transport closed; all admitted ingress drained;
// old native/game/render producers AND consumers completed (including deferred
// projectile/tracer/controller jobs); old owned actors retired/freed; and a
// fresh primary/owned spawn recipe established at the native lifecycle boundary.
// Ingress==0 alone proves NONE of the native-producer/consumer preconditions.
// Do not hold actor-identity, WorldLock, native world/ProcessMgr or recipe locks
// while calling this function. No game pointer is dereferenced and no native
// game method, allocator, hook installer, logger or socket is called here.
void clear_player_world_transients() {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return;
    constexpr auto release = std::memory_order_release;

    // Ingress has no publisher left. This short lock groups the old state-fed
    // action mailboxes, including pickup/give payloads that have no own lock.
    // Native consumers must ALSO be quiescent; the lock is not their lease.
    AcquireSRWLockExclusive(&g_state_net.peer_state_lock);
    g_state_net.pending_button_presses.store(0, release);
    g_state_net.active_button_press_latch.store(0, release);
    g_state_net.puppet_traversal_force.store(0, release);
    g_state_net.puppet_reload_held.store(false, release);
    g_state_net.puppet_reload_press_force_until_ms.store(0, release);
    g_state_net.puppet_reload_release_force_until_ms.store(0, release);
    g_state_net.puppet_reload_press_received_ms.store(0, release);
    g_state_net.puppet_reload_event_sequence.store(0, release);
    g_state_net.puppet_edge_drop_force_until_ms.store(0, release);
    g_state_net.puppet_edge_drop_event_sequence.store(0, release);
    g_state_net.puppet_evade_start_force_until_ms.store(0, release);
    g_state_net.puppet_evade_start_event_sequence.store(0, release);
    g_state_net.puppet_cover_force.store(0, release);
    g_state_net.pending_puppet_melee_hash.store(0, release);
    g_state_net.pending_puppet_melee_presses.store(0, release);
    g_state_net.pending_authoritative_teleport.store(false, release);

    g_pending_pickup_flag.store(false, release);
    g_pending_pickup_controller_start.store(false, release);
    g_pending_pickup_id.store(0, release);
    g_pending_pickup_art.store(0, release);
    g_pending_pickup_class_id.store(0, release);
    g_pending_pickup_name_id.store(0, release);
    g_pending_pickup_seq.store(0, release); // pending stamp, not a send counter
    std::memset(g_pending_pickup_controller_start_args, 0, sizeof(g_pending_pickup_controller_start_args));
    g_controller_pickup_recent_until_ms.store(0, release);
    g_sender_pickup_object_id.store(0, release);
    g_sender_pickup_stable_id.store(0, release);
    g_sender_pickup_capture_art.store(0, release);
    g_sender_pickup_capture_ms.store(0, release);
    g_relay_pickup_wait_mirror.store(false, release);
    g_relay_pickup_wait_art.store(0, release);
    g_relay_pickup_wait_seq.store(0, release);
    g_relay_pickup_wait_id.store(0, release);
    g_relay_pickup_wait_since_ms.store(0, release);
    g_puppet_pickup_anim_suppress_until_ms.store(0, release);
    g_puppet_pickup_gesture_until_ms.store(0, release);
    g_puppet_pickup_gesture_art.store(0, release);
    g_puppet_pickup_action_started.store(false, release);

    g_pending_weapon_give_flag.store(false, release);
    g_pending_weapon_give_seq.store(0, release);
    g_pending_weapon_give_art.store(0, release);
    std::memset(g_pending_weapon_give_info, 0, sizeof(g_pending_weapon_give_info));
    g_pending_weapon_give_world_flags = 0;
    g_pending_weapon_give_consumed_id = 0;
    g_pending_weapon_give_dropped_art = 0;
    g_pending_world_weapon_lifecycle = {};
    g_mirrored_dropped_weapon_identities = {};
    for (auto& block : g_relayed_weapon_store) block = {};
    g_type97_normalized_block_ready.store(false, release);

    // Preserve monotonic producer cursors/high-water marks. Advancing the
    // consumer to head discards old messages without resetting ring sequences.
    g_fire_event_ring.tail = g_fire_event_ring.head.load(std::memory_order_acquire);
    for (auto& packet : g_fire_event_ring.packets) packet = {};
    g_client_dmg_tail = g_client_dmg_head.load(std::memory_order_acquire);
    ReleaseSRWLockExclusive(&g_state_net.peer_state_lock);

    // Clear only the dormant legacy mailbox, never enable/reset ENR2 or AI.
    AcquireSRWLockExclusive(&g_state_net.peer_enemies_lock);
    g_state_net.has_peer_enemies.store(false, release);
    g_state_net.peer_enemies_received_ms.store(0, release);
    g_state_net.peer_enemies = {};
    ReleaseSRWLockExclusive(&g_state_net.peer_enemies_lock);

    AcquireSRWLockExclusive(&g_pending_weapon_world_lock);
    g_pending_weapon_world_flag.store(false, release);
    g_pending_weapon_world_packet = {};
    ReleaseSRWLockExclusive(&g_pending_weapon_world_lock);
    AcquireSRWLockExclusive(&g_pending_local_fire_packet_lock);
    g_pending_local_fire_packets = {};
    g_pending_local_fire_packet_count = 0;
    ReleaseSRWLockExclusive(&g_pending_local_fire_packet_lock);
    AcquireSRWLockExclusive(&g_peer_fire_direction_lock);
    g_peer_fire_directions = {};
    g_peer_fire_direction_count = 0;
    ReleaseSRWLockExclusive(&g_peer_fire_direction_lock);
    AcquireSRWLockExclusive(&g_applied_fire_direction_observation_lock);
    g_applied_fire_direction_observation = {};
    ReleaseSRWLockExclusive(&g_applied_fire_direction_observation_lock);
    AcquireSRWLockExclusive(&g_tracer_visual_pending_lock);
    g_tracer_visual_pending_context = {};
    ReleaseSRWLockExclusive(&g_tracer_visual_pending_lock);
    AcquireSRWLockExclusive(&g_ground_pickup_hud_candidates_lock);
    g_ground_pickup_hud_candidates = {};
    ReleaseSRWLockExclusive(&g_ground_pickup_hud_candidates_lock);
    AcquireSRWLockExclusive(&g_pending_fhud_owner_cleanup_lock);
    g_pending_fhud_owner_cleanup = {};
    ReleaseSRWLockExclusive(&g_pending_fhud_owner_cleanup_lock);

    g_cached_local_input_buttons.store(0, release);
    g_local_edge_drop_send_until_ms.store(0, release);
    g_local_evade_start_send_until_ms.store(0, release);
    g_local_reload_level.store(false, release);
    g_local_reload_press_send_until_ms.store(0, release);
    g_local_reload_release_send_until_ms.store(0, release);
    g_local_reload_press_capture_ms.store(0, release);
    g_fire_button_hold_until_ms = 0;
    g_suppress_puppet_fire_buttons_until_ms = 0;
    g_peer_aim_recent_until_ms = 0;
    g_puppet_draw_ok_puppet = nullptr; g_puppet_draw_ok_art = 0; g_puppet_draw_ok_until_ms = 0;
    g_puppet_gun_ok_puppet = nullptr; g_puppet_gun_ok_art = 0; g_puppet_gun_ok_until_ms = 0;
    g_give_weapon_fail_until_ms = 0;
    g_puppet_native_fire_allowed_after_ms = 0;
    g_fire_puppet_tracked = nullptr;
    g_fire_puppet_ready_after_ms = 0;
    g_player_fire_controller_offset = -1;
    g_puppet_aim_override_frame = false;
    g_puppet_melee_try_anim_bypass.store(false, release);

    // Throw/controller activity and tracked objects are per world; the selector
    // hash and every installed grenade relay/hook remain asset/process-owned.
    g_peer_grenade_command_was_active.store(false, release);
    g_peer_grenade_scheduler_consumed.store(false, release);
    g_peer_grenade_release_until_ms.store(0, release);
    g_peer_native_grenade_spawn_ms.store(0, release);
    g_spawning_peer_animated_grenade.store(false, release);
    g_peer_animated_grenade_init_seen.store(false, release);
    g_peer_grenade_bind_attempted.store(false, release);
    g_peer_release_probe_grenade.store(nullptr, release);
    g_peer_release_probe_grenade_ms.store(0, release);
    g_peer_release_probe_track_last_ms.store(0, release);
    g_peer_grenade_release_probe_until_ms.store(0, release);
    g_last_grenade.store(nullptr, release);
    g_last_grenade_ms.store(0, release);
    g_last_grenade_event_sent.store(false, release);
    g_applying_peer_grenade.store(false, release);
    g_pending_grenade_event.store(false, release);
    g_pending_grenade_event_seq.store(0, release);
    g_pending_grenade_event_art.store(0, release);
    std::memset(g_pending_grenade_event_payload, 0, sizeof(g_pending_grenade_event_payload));
    std::memset(g_pending_grenade_pos, 0, sizeof(g_pending_grenade_pos));
    std::memset(g_pending_grenade_vel, 0, sizeof(g_pending_grenade_vel));
    std::memset(g_pending_grenade_quat, 0, sizeof(g_pending_grenade_quat));
    g_peer_spawned_grenade.store(nullptr, release);
    g_peer_grenade_descriptor_seeded.store(false, release);
    g_peer_arc_grenade.store(nullptr, release);
    g_peer_arc_until_ms.store(0, release);
    std::memset(g_peer_arc_pos, 0, sizeof(g_peer_arc_pos));
    std::memset(g_peer_arc_vel, 0, sizeof(g_peer_arc_vel));
    std::memset(g_peer_arc_quat, 0, sizeof(g_peer_arc_quat));
    g_peer_arc_quat[3] = 1.0F;
    g_peer_arc_last_ms = 0;
    g_active_grenade_throw_seq.store(0, release); // active throw stamp only

    g_pending_breakable_event.store(false, release);
    g_pending_breakable_seq.store(0, release);
    g_pending_breakable_kind.store(0, release);
    std::memset(g_pending_breakable_blob, 0, sizeof(g_pending_breakable_blob));
    std::memset(g_pending_breakable_world_pos, 0, sizeof(g_pending_breakable_world_pos));
    std::memset(g_breakable_sync_world_pos, 0, sizeof(g_breakable_sync_world_pos));
    g_pending_breakable_scene_hash.store(0, release);
    g_breakable_sync_scene_hash.store(0, release);
    g_deferred_breakable_sync_sb.store(nullptr, release);
    g_deferred_breakable_sync_when.store(0, release);
    g_breakable_resolver_override_id.store(0, release);
    g_breakable_resolver_override_obj.store(nullptr, release);
    g_last_breakable_sync_net_id.store(0, release);
    g_last_breakable_sync_tick.store(0, release);

    // Pure metadata resets only: never call the pointer-walking/native
    // clear_peer_head_look_on_puppet or any look/controller rebuild here.
    g_remote_cloth_player.store(0, release);
    g_remote_cloth_parent_draw.store(0, release);
    g_remote_cloth_seen_ms.store(0, release);
    g_local_fganim_data.store(nullptr, release);
    g_puppet_fganim_data.store(nullptr, release);
    g_local_fganim_quat_valid.store(false, release);
    g_local_fganim_quat_captured_ms.store(0, release);
    g_puppet_aim_session_owner.store(nullptr, release);
    g_puppet_aim_gate_last_eval_ms.store(0, release);
    reset_puppet_auto_aim_admission();
    reset_peer_head_look_smooth_state();
    reset_puppet_fganim_world_smoothing(nullptr);
    AcquireSRWLockExclusive(&g_local_aim_pitch_lock);
    g_local_aim_pitch = {};
    ReleaseSRWLockExclusive(&g_local_aim_pitch_lock);
    AcquireSRWLockExclusive(&g_local_strafe_ik_forward_y_lock);
    g_local_strafe_ik_forward_y = {};
    ReleaseSRWLockExclusive(&g_local_strafe_ik_forward_y_lock);
    AcquireSRWLockExclusive(&g_local_aim_leg_base_pose_lock);
    g_local_aim_leg_base_pose = {};
    ReleaseSRWLockExclusive(&g_local_aim_leg_base_pose_lock);
    AcquireSRWLockExclusive(&g_idle_arm_pose_capture_lock);
    g_idle_arm_pose_capture = {};
    ReleaseSRWLockExclusive(&g_idle_arm_pose_capture_lock);
    AcquireSRWLockExclusive(&g_idle_arm_palette_admission_lock);
    g_idle_arm_palette_source_admission = {};
    g_idle_arm_palette_target_admission = {};
    ReleaseSRWLockExclusive(&g_idle_arm_palette_admission_lock);
    AcquireSRWLockExclusive(&g_idle_arm_pose_dwell_lock);
    g_idle_arm_pose_source_dwell = {};
    g_idle_arm_pose_target_dwell = {};
    ReleaseSRWLockExclusive(&g_idle_arm_pose_dwell_lock);

    // Deliberately untouched: process/peer session IDs; all send_seq and
    // *_send_seq, *_last_applied_seq, log/capture counters and ring heads;
    // explicit departure token/tombstones (publish_local_world owns them);
    // owned actor registries/companion budget (native lifecycle owns them);
    // immutable 48B holster donor/visual scalars and installed native hooks.
    // Function-static caches are epoch-guarded AT THEIR CONSUMER in the main
    // source: slot-mirror memo/ungivable/stability, held-weapon/holster/give,
    // authored inventory/last snapshot, cloth player+draw, local-state cached
    // weapon/pose/velocity, aim ramp and controller/cover edge state. They cannot
    // be cleared globally here. Thread-local native scopes remain native-call
    // scoped, never synthetically reset across a suspended call. Diagnostic-only
    // palette baselines are not authority to restore old Player/Fg memory.
    // The optional legacy InputPacket UDP receiver is also a separate ingress,
    // so its unsynchronized g_remote_command_state is NOT touched here.
}
