import ida_bytes
import ida_funcs
import ida_loader
import _ida_loader
import ida_name
import ida_pro
import idc


FUNCTIONS = {
    # --- 2026-07-13: native pickup gesture controller ---
    0x140F49300: (
        "PlayerPickupController_Construct",
        "Constructs the mounted Player+0x1EB8 pickup controller, including the "
        "16-entry queue at +0x130 (stride 0x30), queue count +0x430, selected "
        "dra-pickup-* gesture hash +0x434, timing and joint-state fields.",
    ),
    0x140F49CA0: (
        "PlayerPickupController_StartOrQueue",
        "Vtable +0x38. Called by the interact-with-me/Pickup script branch at "
        "0x140DD0A3C. Queues one pickup record and selects the native "
        "dra-pickup-* gesture. Explicitly supports a null object argument; arg3 "
        "is a 32-byte POD copied into the queue record.",
    ),
    0x140F4B150: (
        "PlayerPickupController_TryStartGesture",
        "Resolves and starts the queued dra-pickup-* gesture and waits for the "
        "signal-pickup marker through the controller's native animation state.",
    ),
    0x140F4BF70: (
        "PlayerPickupController_Update",
        "Per-frame pickup-controller update reached through its vtable +0x28; "
        "advances queued pickup records and starts/completes the gesture.",
    ),
    0x140F4CD20: (
        "PlayerPickupController_PostAnimBlending",
        "Confirmed by player-pickup.cpp:897 assertion. Applies pickup gesture "
        "post-blend/joint state while the native pickup action is active.",
    ),
    0x140F4D0E0: (
        "PlayerPickupController_CompleteAndPublish",
        "Completes queued pickup records and publishes the player-pickup actor "
        "state through DispatchOrResolveActorStateDescriptorIntoRuntimeSlot.",
    ),
    # --- 2026-07-12: actor thread context and context-free interact publish ---
    0x1416A2E80: (
        "ThreadContext_GetCurrentProcess",
        "Returns TLS ContextProcess at Fls block +0x98 when the +0x90 tag is set, "
        "otherwise returns the global fallback process context.",
    ),
    0x1416A2ED0: (
        "ThreadContext_SetCurrentProcess",
        "Writes the ContextProcess CRC to the current Fls block +0x90 and the "
        "supplied process object to +0x98. Stock event publishers save, set, "
        "dispatch virtual +0xD0, then restore through this helper.",
    ),
    0x1410BCDF0: (
        "Player_PublishZiplineRequestedInteract",
        "Independent producer of player-requested-interact for a retained zipline "
        "target. Builds the same two-id descriptor as the generic Player interact "
        "publisher without calling Player_InteractDispatcher.",
    ),
    # --- 2026-07-12: Player aim-additive animation pipeline ---
    0x140CAFA20: (
        "PlayerAimController_UpdateBlendState",
        "PlayerAimController vtable slot +0x30. Called from the common Player "
        "controller phase at 0x140C9F420; advances ADS blend at this+0x8EC and "
        "resets/publishes Aim0..Aim7 animation channels.",
    ),
    0x140CADF40: (
        "PlayerAimController_IsAimAdditiveAllowedForCurrentState",
        "Selects the active Player animation-state descriptor and movement mode, "
        "then returns the authored permission for directional aim additives. "
        "ComputeDirectionalAimWeights sets controller+0x8A0 only when this is true.",
    ),
    0x140CAE480: (
        "PlayerAimController_GetAuthoredAimBlendTarget",
        "Returns the authored target value consumed by UpdateBlendState for "
        "controller+0x8EC, based on the active base animation state.",
    ),
    0x140CAF2F0: (
        "PlayerAimController_ResetAndPrimeAimAdditives",
        "PlayerAimController vtable slot +0x68. Resets published directional "
        "weights and primes controller+0x8A0 when the current Player state "
        "allows aim additives; called from independent Player state entries.",
    ),
    0x140CAECB0: (
        "PlayerAimController_ComputeAndPublishAimAdditives",
        "PlayerAimController vtable slot +0x50. Receives aim/body bases and "
        "directional limits, calls 0x140CB32B0, and emits the looking-down-sights "
        "event. Reached by the ground GunMoveShield path through slot +0x58.",
    ),
    0x140CAF110: (
        "PlayerAimController_ApplyWorldAimBasis",
        "PlayerAimController vtable slot +0x58. Converts world basis vectors, "
        "then dispatches slot +0x50 to compute the directional aim-additive weights.",
    ),
    0x140CAF040: (
        "PlayerAimController_ApplyBodyBasisAim",
        "PlayerAimController vtable slot +0x60. Builds the remaining body basis "
        "through slot +0xE8, then dispatches slot +0x58.",
    ),
    0x140CAFD90: (
        "PlayerAimController_GetAimOriginPoint",
        "PlayerAimController vtable slot +0x80. Chooses targHead or targPelvis "
        "from weapon state, resolves that Player skeleton joint, and returns its "
        "world point. Slot +0x58 uses it as the origin for target-origin direction.",
    ),
    0x140CB41F0: (
        "PlayerAimController_PublishDirectionalAimWeights",
        "Publishes aim-add-left/right/down/up to the Player animation graph. "
        "The four weights are scaled by ADS blend this+0x8EC; Aim0..Aim7 CRCs "
        "are initialized by the PlayerAimController constructor 0x140CAE670.",
    ),
    0x140CB32B0: (
        "PlayerAimController_ComputeDirectionalAimWeights",
        "Normalizes targetPoint-originPoint, measures signed horizontal/vertical "
        "angles against the Player body basis, clamps them by the supplied limits, "
        "then calls PlayerAimController_PublishDirectionalAimWeights.",
    ),
    0x140CB1720: (
        "PlayerAimController_GetTargetPointFromReticle",
        "PlayerAimController vtable slot +0xF8. Source assertion supplies the exact "
        "method name. Builds cameraOrigin + cameraForward * ray distance from the "
        "owner Player+0x4010 screen-route pose seed, with targetable collision adjustment.",
    ),
    0x140E84350: (
        "GunMoveShield_CopyAimTargetPoint",
        "Player::GunMoveShield vtable 0x142A27658 slot +0xF0. Copies the 16-byte "
        "target point unchanged before the ground ADS path passes it to the aim controller.",
    ),
    0x14014D9F0: (
        "Quaternion_RotatePositiveZ",
        "Returns q*(0,0,1)*inverse(q): x=2(xz+wy), y=2(yz-wx), "
        "z=1-2(x*x+y*y). GunMoveShield uses it to derive the body forward vector.",
    ),
    0x1410E3670: (
        "Player_CheckAimActionEligibility",
        "Shared Player gameplay-state gate used immediately before native aim "
        "dispatch by PlayerGunMoveShield_UpdateAndDispatch and Player::ZeroG. "
        "Checks camera/look suppression and weapon/action controller state.",
    ),
    # --- 2026-07-12: scoped aim route-camera owner ---
    0x140CB0200: (
        "PlayerAimController_KickTargetPointJob",
        "PlayerAimController vtable 0x142A0B1F8 slot +0xE0. Source assert names "
        "KickTargetPointJob; the sniper branch resolves Player+0x4010 through "
        "ResolveScreenRouteEntityByIndex and SelectPreferredCameraChildFromRouteEntity. "
        "The selected child +0x1A8 is a camera descriptor, not a vtable; virtual "
        "slot +0x230 is only the unzoomed-sniper status query.",
    ),
    # --- 2026-07-11: scoped puppet DrawControl visibility ---
    0x141754510: (
        "IDrawControl_HideMesh",
        "Validates meshIndex, writes mesh[meshIndex].visible byte at +0x243 "
        "(entry+579) to 0, then refreshes DrawControl state.",
    ),
    0x1417545F0: (
        "IDrawControl_DisableMeshFromMainView",
        "For every bound child of the selected mesh, sets render-instance flag "
        "0x40 at instance+0x18.",
    ),
    0x141754A30: (
        "IDrawControl_ShowMesh",
        "Inverse of IDrawControl_HideMesh: writes mesh entry +579 to 0xFF and "
        "refreshes DrawControl state.",
    ),
    0x141754B10: (
        "IDrawControl_EnableMeshInMainView",
        "Inverse of DisableMeshFromMainView: clears render-instance flag 0x40 "
        "for every bound child of the selected mesh.",
    ),
    # --- 2026-07-11: native grenade network-event receive path ---
    0x140BE5C80: (
        "NetEventEmitGrenade_DefaultCtor",
        "Initializes the 0xC0-byte receive event and its vtable. Used before "
        "deserializing the POD payload.",
    ),
    0x140BE5D30: (
        "NetEventEmitGrenade_BuildFromProjectile",
        "Sender constructor. Copies owner identity, transform, weapon/type, and "
        "the grenade descriptor block from a live ProjectileGrenade.",
    ),
    0x140BE6140: (
        "NetEventEmitGrenade_Deserialize",
        "Vtable +0x8. Reads the complete wire payload into event fields +0x18..+0xB5.",
    ),
    0x140BE6260: (
        "NetEventEmitGrenade_Serialize",
        "Vtable +0x10. Symmetric writer for the POD event payload.",
    ),
    0x140BE63A0: (
        "NetEventEmitGrenade_Execute",
        "Vtable +0x18 receiver apply. Builds ProcessSpawnInfo, invokes the native "
        "descriptor materializer, and binds the spawned grenade network id.",
    ),
    0x140BE7000: (
        "SendNetEventEmitGrenade",
        "Source-identified sender wrapper in net-event-weapon.cpp. Builds a "
        "NetEventEmitGrenade from a live ProjectileGrenade and queues it.",
    ),
    0x141544CD0: (
        "ProjectileGrenade_ResolveOwner",
        "Resolves the grenade owner from its screen index or retained owner pair.",
    ),
    # --- 2026-07-10: Player FgAnimData async lifecycle ---
    0x141781390: (
        "AnimManager_CreateAsyncUpdateJobFunctor",
        "Creates the async animation-manager job for an FgAnimData object. The "
        "job callback is AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00.",
    ),
    0x141BB7DD0: (
        "NdGameObject_DisableAnimationAsyncPhases",
        "Source-identified nd-game-object animation disable path. Clears FgAnimData "
        "phase bits 0x08/0x10/0x20 (mask 0x38).",
    ),
    0x141BB8220: (
        "NdGameObject_EnableAnimationAsyncPhases",
        "Source-identified nd-game-object animation enable path. Sets FgAnimData "
        "phase bits 0x08/0x10/0x20 (mask 0x38).",
    ),
    0x141BB88A0: (
        "NdGameObject_EnsureAnimationEnabled",
        "Ensures the owning FgAnimData is enabled when its state field at +4 is zero.",
    ),
    # --- 2026-07-10: pickup-gesture recipe (interact command activation) ---
    0x14183A650: (
        "InputScreenBlock_ActivateCommand",
        "Activates a command id in the per-screen dynamic command list at "
        "block+0x590 (block = g_LocalPlayerInputManager + 3504 + 3056*screenIdx). "
        "Sibling of InputScreenBlock_IsCommandActive (0x14183A3D0). KEY to the "
        "co-op pickup gesture: activating the weapon-pickup interact command here "
        "directly bypasses the +0x518 button routing that never reaches the puppet.",
    ),
    0x141C9A360: (
        "InteractControl_GetAvailabilityForPlayer",
        "Called by Player_InteractDispatcher before action submission. Calls "
        "interactControl vtable+0x8 with the interactable object and a retained "
        "Player handle {player+0xA8, player+0xBC}; return 1 permits the later "
        "vtable+0x28 RequestInteract call. It does not start an action itself.",
    ),
    0x140C805A0: (
        "Player_BuildInteractDispatchState",
        "Builds the 0x15C-byte state consumed by Player_InteractDispatcher. "
        "Resolves the indexed Player, copies the supplied transform, snapshots "
        "the per-screen *interact-command-list*, and queries +0x590 command state.",
    ),
    0x140C88220: (
        "Player_TickInteractDispatcherForScreen",
        "Resolves g_PlayerHandleForHudPublish[*state], then calls "
        "Player_InteractDispatcher on the matching per-screen dispatcher. The "
        "input must already be populated by Player_BuildInteractDispatchState.",
    ),
    0x140DCDEA0: (
        "Player_ScriptWeaponEventHandler",
        "Giant script weapon-event handler (0x8b2d). The ground-pickup give-weapon "
        "branch (hash 0xFB1091A3) returns to 0x140DD4208 - this is the path a real "
        "ground pickup takes, NOT WeaponPickupInteractControl::RequestInteract "
        "(runtime-proven 2026-07-10: RequestInteract hook installed but never fires).",
    ),
    # --- 2026-07-07: weapon give / slot placement (pickup display root) ---
    0x1410A4D40: (
        "PlayerWeaponController_GiveWeapon",
        "vtable +0x158. WeaponInfo fields it reads: +0x80 art id, +0x84 ammo "
        "(-1=random), +0x96 flags word, +0x9C..0xB3 net drop-transform copied to "
        "weapon+0x866 in net mode. If art already owned (weapon+0x860 match) the "
        "path is idempotent (updates ammo/flags); otherwise it calls the create "
        "slot at vtable+0x130. Pickup/scripted give reaches it via sub_140DCDEA0 "
        "(ret_rva 0xDD4208).",
    ),
    0x1410A8920: (
        "PlayerWeaponController_CreateAndAddWeapon",
        "vtable +0x130. Creates the weapon object and inserts it into a manager "
        "SLOT. Resolves the WeaponArtDef by art (GetCachedWeaponArtDefsHashRegistry "
        "+ LookupRegistryRecordPointerById); returns 0 (no create) if the def is "
        "absent. The destination slot/class comes from the def record (*v16 class "
        "hash, compared vs rightHand/ProcessShield/rocket-launcher/... branches). "
        "ROOT of the pickup-display fountain: a relayed rifle (M-30 0x3019CBEE) "
        "lands in manager slot 1 (secondary), colliding with the pistol - needs "
        "a remove/swap, not an additive give.",
    ),
    0x1410A8670: (
        "PlayerWeaponController_KillWeaponsInClasses_1_17",
        "vtable +0x168. ProcessKillTree(GetWeaponByClass(ctrl,1)) then class 17 - "
        "the REMOVE primitive. GetWeaponByClass = ctrl vtable +0x188; ProcessKillTree "
        "destroys the weapon object. Building block for a pickup SWAP (kill the "
        "puppet's conflicting-class weapon, then GiveWeapon the relayed art).",
    ),
    0x1410A8750: (
        "PlayerWeaponController_KillAllWeapons",
        "vtable +0x170. ProcessKillTree over every weapon class (remove-all).",
    ),
    0x1413B49B0: (
        "Hud_EnqueuePickupNotification",
        "Appends {art, ammo, ...} to the indexed Hud object's notification list "
        "at *(hud+0x320)+0x1890 (32-byte stride, count at +0x1930, cap 5). "
        "PlayerWeaponController::GiveWeapon selects Hud by owner Player+0x4010; "
        "this is HUD state, not the character pickup-action state machine.",
    ),
    0x1413B2340: (
        "Hud_ConstructForScreen",
        "Constructs one 0x368-byte Hud object for the supplied screen index and "
        "binds its 0x320 state pointer to the matching 0x19A0-byte static block.",
    ),
    0x1413B2730: (
        "Hud_Init",
        "Initializes the indexed Hud object, command observers, weapon tables, "
        "and sets Hud+0x21 ready. Called for both screen 0 and screen 1.",
    ),
    0x1413B2950: (
        "Hud_UpdateForScreen",
        "Updates one Hud object, publishes reticle offsets, sets the current-Hud "
        "globals, and runs Hud_UpdateChildren.",
    ),
    0x14184DE90: (
        "Hud_UpdateChildren",
        "Updates registered Hud child controllers when Hud+0x21 and Hud+0x14 are "
        "active, then calls the Hud vtable +0x80 finalizer.",
    ),
    0x1413538B0: (
        "Hud_UpdateScreen0Frame",
        "Stock frame wrapper: prerequisite, Hud_UpdateForScreen(screen 0), then "
        "the HUD frame finalizer. It does not update the screen-1 Hud object.",
    ),
    0x140DCDEA0: (
        "Player_ScriptWeaponEventHandler",
        "Player script-event handler (switch on event name-hash). Hash 0xFB1091A3 "
        "branch = give-weapon: builds WeaponInfo and calls GiveWeapon (vt+0x158). "
        "The real ground-pickup give path (ret addr 0x140DD4208).",
    ),
    # --- 2026-07-06: weapon pickup sync design (see initial-findings) ---
    0x14157AA70: (
        "WeaponPickupInteractControl_OnInteract",
        "Named by its own assert (weapon-pickup.cpp). vtable +0x10 of "
        "WeaponPickupInteractControl (0x142AA6E90). Executes the local weapon "
        "pickup: give-weapon, pickup-ammo-count, art registry lookups. SENDER "
        "hook point for the co-op pickup relay.",
    ),
    0x14157B690: (
        "WeaponPickupInteractControl_GetHudAmmoCount",
        "Named by its own assert (weapon-pickup.cpp).",
    ),
    0x1408C8A40: (
        "WeaponPickupInteractControl_RequestInteract",
        "WeaponPickupInteractControl vtable +0x28. Identity is confirmed by its "
        "PickupInteractControl::RequestInteract assert in process-pickup.cpp. "
        "Publishes interact-with-me to the pickup at action start.",
    ),
    0x140C87D70: (
        "Player_PublishRequestedInteract",
        "Called immediately after PickupInteractControl::RequestInteract by the "
        "player interact dispatcher. Publishes player-requested-interact with the "
        "interactable class/name ids; target actor comes from current thread-local "
        "player state.",
    ),
    0x140C88130: (
        "PlayerInteractCommandTable_IsTriggered",
        "Checks the dispatcher command table's pressed byte or hold duration for "
        "one command id. This is the final input gate before pickup RequestInteract.",
    ),
    0x140BCDFC0: (
        "NetEventSyncWeaponPickups_Ctor",
        "Writes vftable 0x1429FCF90, type id 0x0101006A at +8, count u32 at +24; "
        "entries at +28 = {u32 gameobject runtime id, float ammo} pairs.",
    ),
    0x140BCE0B0: (
        "NetEventSyncWeaponPickups_Serialize",
        "Bit-stream write: count 7 bits, then id:32 + float:32 per entry.",
    ),
    0x140BCE440: (
        "NetEventSyncWeaponPickups_Apply",
        "Consumes the entry list: resolves each id via the game-object handle "
        "registry (ResolveGameObjectHandleById) and drives the native "
        "net-pick-up-other-client flow (removes ground pickup, processes remote "
        "client pickup). RECEIVER entry for the co-op pickup relay: stack-construct "
        "the event and call this directly.",
    ),
    0x140A989C0: (
        "ResolveGameObjectHandleById",
        "Generic game-object handle lookup: returns {ptr,id} handle validated by "
        "*(obj+0xBC)==id (same pattern as the retained-player slot table).",
    ),
    0x141579150: (
        "WeaponPickup_Factory",
        "Registered for class name WeaponPickup (registration sub_1400C5560); "
        "object size 1376, vftable 0x142AA66C0.",
    ),
    0x140C85D20: (
        "Player_InteractDispatcher",
        "Generic player interact scan/dispatch (player-pressed-interact-nearby, "
        "player-interact-hold-*). Checks commands against the PER-SCREEN dynamic "
        "command list (input block +0x590) via InputScreenBlock_IsCommandActive - "
        "NOT the +0x518 bitmask; relayed puppet buttons never trigger interacts.",
    ),
    0x140C805A0: (
        "Player_BuildInteractDispatchState",
        "Builds 0x15C-byte interact command/pose block from screenIndex + player "
        "transform; snapshots g_PlayerHandleForHudPublish[screen] and per-screen "
        "+0x590 command active flags from the player's joypad index (+0x39B8).",
    ),
    0x140EC75D0: (
        "PlayerReviveCharacter_UpdateAndDispatch",
        "Player::ReviveCharacter vtable +0x20: revive + weapon-pickup commands, then "
        "BuildInteractDispatchState -> Player_InteractDispatcher for owner screen.",
    ),
    0x140CD3C00: (
        "PlayerBoatDriving_UpdateAndDispatch",
        "Player::BoatDriving vtable +0x20: script/interact/boat-stand paths; ends in "
        "BuildInteractDispatchState -> InteractDispatcher (camera route hardcoded 0).",
    ),
    0x140CD4750: (
        "PlayerBoatDrivingController_Create",
        "Allocates the 40-byte Player::BoatDriving action controller and stores "
        "its owning Player at controller+0x10.",
    ),
    0x140EC8160: (
        "PlayerReviveCharacterController_Create",
        "Allocates the 384-byte Player::ReviveCharacter action controller, "
        "initializes move interfaces, and stores owner at controller+0x10.",
    ),
    0x140C88220: (
        "Player_InteractDispatchFromBuiltState",
        "Thin wrapper: validate g_PlayerHandleForHudPublish[screen] then call "
        "Player_InteractDispatcher on the per-screen dispatcher blob.",
    ),
    0x140C837F0: (
        "InteractDispatcher_RebuildNearbyList",
        "Clears dispatcher+0 count, gathers nearby interactables (sub_141C94340 "
        "Player-class pass + sphere-cast path), appends up to 48 x 32-byte entries "
        "at dispatcher+8 via InteractDispatcher_AppendNearbyEntry.",
    ),
    0x140C87FB0: (
        "InteractDispatcher_TickNearbyList",
        "Per-frame nearby-list maintainer: prune stale entries, rebuild when "
        "dispatcher+0x3570 flag set, refresh per-entry Havok probes. Called from "
        "InteractDispatcher_TickAllScreenBlobs on each per-screen blob.",
    ),
    0x140C817F0: (
        "InteractDispatcher_AppendNearbyEntry",
        "Writes one 32-byte nearby entry {handle,id}@+0, interact index@+16, "
        "probe-hit byte@+24; links input object at dispatcher+0x610+240*index. "
        "Skips KickProbes when InputObject joypad != active local joypad.",
    ),
    0x140C858F0: (
        "InteractDispatcher_PruneNearbyList",
        "Compacts dispatcher nearby list: drops entries whose object fails the "
        "dword_146920B54 visibility/active bit test.",
    ),
    0x140C828B0: (
        "InteractDispatcher_UpdateNearbyEntryProbe",
        "Per-entry sphere-cast probe refresh inside Player_InteractDispatcher; "
        "gated by InputObject_MatchesActiveLocalJoypad on the linked input slot.",
    ),
    0x140C82310: (
        "NearbyObject_KickProbes",
        "Builds CheckNearbyGameObjectsParams and kicks Havok sphere casts "
        "(nearby-object-searcher.cpp NearbyObject::KickProbes). Gated by "
        "InputObject_MatchesActiveLocalJoypad.",
    ),
    0x1401611F0: (
        "InteractDispatcher_TickAllScreenBlobs",
        "Global frame tick: InteractDispatcher_TickNearbyList on qword_143EC4EE0 "
        "(screen 0) and, in net co-op, qword_143EC84A0 (screen 1).",
    ),
    0x141C94340: (
        "InteractDispatcher_GatherNearbyPlayerObjects",
        "Spatial gather of Player-class interactables near the query object; "
        "returns count and fills an output pointer array (max 48).",
    ),
    0x14042D120: (
        "NpcActive_AnimPhaseOwnerUpdate",
        "Active@Npc vtable+0x20 (RTTI vtable 0x142934888). Runs the NPC "
        "anim-phase owner path; this is not a Player::Active callback.",
    ),
    0x1404223F0: (
        "Player_TickDecisionNodeFromControlHost",
        "Syncs player transform from control-host at player+0x1FB0; when host "
        "entity+0x6E bit0x4 set, first-latch (*(player+0x1808))->vtable+0(player) "
        "(player+0x1D52 bit0x4 latch).",
    ),
    0x140CD3760: (
        "Player_DecisionNodePublishGate",
        "Gate before PublishRuntimeState: validates player+0x1FC8 vtable+168, "
        "calls vtable+96 on that object, player vtable+408, then "
        "PublishRuntimeStateFromDecisionNodeLikeObject.",
    ),
    0x140C7FF40: (
        "Player_ResolveWeaponPickupInteractAnim",
        "Resolves weapon-pickup-swap / weapon-pickup-when-no-weapon anim ids and "
        "the interact command id (out a6) from the pickup art def.",
    ),
    0x14183A3D0: (
        "InputScreenBlock_IsCommandActive",
        "Scans the dynamic command-id list at block+0x590 (entries {ptr,count}); "
        "per-screen block = g_LocalPlayerInputManager + 3504 + 3056*screen.",
    ),
    0x1410A3EA0: (
        "PlayerWeaponController_StartHolsterTransition",
        "Called by ApplyWeaponSwitch (vt+0x48) slot==-1 branch on the current "
        "weapon: the native holster-everything transition.",
    ),
    0x141DE8EC0: (
        "NavCharacter_ApplyAnimationRootTransform",
        "NavCharacter/AiCharacter vtable +0x400 post-animation path. Npc override "
        "0x14042FB40 reaches it; when virtual +0x5E8 permits the direct branch it derives "
        "and writes local root position +0x130 and rotation +0x140, then calls "
        "virtual +0x208/+0x1E8. Player vtable +0x400 instead resolves to 0x1410E9210.",
    ),
    0x1403D13E0: (
        "GameCharacter_PostAnimApplyRootTransform",
        "GameCharacter vtable +0x400 wrapper. Rejects a below-world transform, "
        "updates the optional +0x1CA0 owner, then tail-calls the NavCharacter "
        "animation-root transform path.",
    ),
    0x14042FB40: (
        "Npc_PostAnimApplyRootTransform",
        "Npc and NadineBuddy vtable +0x400 override. Runs the shared NavCharacter "
        "root-transform apply and additional NPC post-animation state; this is not "
        "the Player post-animation method.",
    ),
    0x1410E9210: (
        "Player_PostAnimUpdate",
        "Player vtable +0x400 post-animation method. RTTI and the embedded "
        "Player::PostAnimUpdate_Async source assertion distinguish it from the "
        "Npc/NavCharacter root-application path.",
    ),
    0x1410EB6A0: (
        "Player_PostAnimBlending_Async",
        "Player vtable +0x408. Exact Player::PostAnimBlending_Async source "
        "assertion. Runs CharacterLegIkController and other post-blend IK "
        "controllers after the animation manager's phase-0x10 dispatch.",
    ),
    0x141A92260: (
        "AnimManager_PerformAnimBlend",
        "Exact PerformAnimBlend source assertion in anim-mgr-jobs.cpp. Builds "
        "the current 48-byte-per-joint matrix palette and swaps FgAnimData "
        "+0x230 into previous buffer +0x238; fallback allocates and publishes "
        "identity matrices with the same layout.",
    ),
    0x141AB97C0: (
        "ProcessAnimCmdListSegment",
        "Exact ProcessAnimCmdListSegment source assertion in anim-commands.cpp:822. "
        "Per-layer animation-command evaluator reached from PerformAnimBlend and the "
        "normal joint-blend producers; consumes AnimCmdContext and drives palette "
        "output including aim-add-* parameter lookup.",
    ),
    0x141B5CE90: (
        "AudioManager_RenderJobEntry",
        "Ndjob callback scheduled by AudioManager_RunRenderJob. When owner+0x81 "
        "is clear, calls AudioManager_Render then AudioManager_ReportNcaStats.",
    ),
    0x141B5D240: (
        "AudioManager_RunRenderJob",
        "Exact AudioManager::RunRenderJob source assertions in gamelib/audio/arm.cpp. "
        "Schedules AudioManager_RenderJobEntry through the Ndjob work batch.",
    ),
    0x141B61530: (
        "AudioManager_Render",
        "Exact void AudioManager::Render(void) source strings in gamelib/audio/arm.cpp. "
        "The owner+0x14B8, stride-0x6C0 table belongs to audio ARM entries, not joints.",
    ),
    0x141B65C50: (
        "AudioManager_ReportNcaStats",
        "Audio-only diagnostics including the exact NCA System, Samplers, Submixing, "
        "Effects, Premastering, and Mastering report strings.",
    ),
    0x141B757F0: (
        "AudioManager_RenderArmEntry",
        "Audio ARM entry helper called at 0x141B63CE6 from AudioManager_Render. Its "
        "owner-relative fields and 0x6C0 entry layout are unrelated to skeleton palettes.",
    ),
    0x141BC2EF0: (
        "Player_ShouldSkipDirectRootTransformApply",
        "Player vtable +0x5E8 (+1512 dec). Reached virtually from the inherited "
        "NpcActive_AnimPhaseOwnerUpdate callback: "
        "return 0 -> net-anim short branch; return 1 -> control-host / decision-node "
        "continuation. Consults gameplay manager and Player+0x348 m_pNetController "
        "via NetController_ShouldSkipAnimPhaseDirectPath.",
    ),
    0x1419136E0: (
        "NetController_ShouldSkipAnimPhaseDirectPath",
        "Player+0x348 net-controller helper. Resolves child via net-controller "
        "vtable+8(net, player); returns 1 (skip anim-phase direct path) when child "
        "vtable+24 is true or child is null.",
    ),
    0x14126B830: (
        "PlayerActiveDecisionNode_Tick",
        "Decision-node vtable+0 (vtable data ~0x142E47BC0). Owner at node+8 -> "
        "Player. Calls Player_BuildInteractDispatchState + "
        "Player_InteractDispatchFromBuiltState using player+0x4010 screen index; "
        "tail is PublishRuntimeStateFromDecisionNodeLikeObject (Player::Active::Update).",
    ),
    0x140E84440: (
        "PlayerGunMoveShield_UpdateAndDispatch",
        "Player::GunMoveShield vtable 0x142A27658 slot +0x148. Owner Player is "
        "stored at action+0x8. Reads the owner's joypad index at Player+0x39B8, "
        "handles aim/fire/grenade and calls Player_InteractDispatchFromBuiltState "
        "before PublishRuntimeStateFromDecisionNodeLikeObject.",
    ),
    0x140422BB0: (
        "Player_PreAnimHubAndAnchorSync",
        "Sniper/aim-position sync only (size 0x304). Does NOT call "
        "Npc_SyncControlHostEntityRootOnNetSkip. Real 422760 path: "
        "Npc_EarlyUpdateHubAndRetainedRefs -> Npc_SyncControlHostHubAndAnchors.",
    ),
    0x140421150: (
        "Npc_SyncControlHostHubAndAnchors",
        "Called from Npc_EarlyUpdateHubAndRetainedRefs. Requires actor+0x1A0. "
        "Alive: calls Npc_SyncControlHostEntityRootOnNetSkip only when actor+5545&2. "
        "Not-alive: chest/neck aim anchors + always 422760 at epilog.",
    ),
    0x14042FCF0: (
        "Npc_EarlyUpdateHubAndRetainedRefs",
        "Npc/Buddy/Human vtable+0x410 implementation. Calls the NPC hub/anchor "
        "path; Player vtable+0x410 is 0x1410EC230.",
    ),
    0x140422760: (
        "Npc_SyncControlHostEntityRootOnNetSkip",
        "When actor+0x1FF0 exists, its vtable+96 is false, and "
        "Player_ShouldSkipDirectRootTransformApply is true: clears "
        "control-host entity+0x6E bit0x4 (&=0xFA) at *(actor+0x1FB0+16). "
        "Else writes root transform via 0x142143F60/0x142144030 and sets bit0.",
    ),
    0x1410EC230: (
        "Player_Update",
        "Player vtable+0x410 implementation (vtable 0x142A4BFE8). Distinct from "
        "Npc/Buddy/Human +0x410 at 0x14042FCF0.",
    ),
    0x140C990A0: (
        "Player_UpdateMainViewVisibilityForAttachmentsAndWeapons",
        "Called from Player::Active::Update after screen camera-state refresh. "
        "Consumes current/previous visibility bytes Player+0x1618/+0x1619, "
        "publishes DisableMainView or EnableMainView to retained body attachments, "
        "and dispatches the matching PlayerWeaponController visibility slot.",
    ),
    0x1410A01F0: (
        "PlayerWeaponController_DisableWeaponsInMainView",
        "PlayerWeaponController vtable+0x370. Iterates weapon slots and publishes "
        "DisableMainView plus DisableCastShadows to eligible weapon actors.",
    ),
    0x1410A08A0: (
        "PlayerWeaponController_UpdateWeaponMainViewVisibility",
        "PlayerWeaponController vtable+0x378. Applies authored per-weapon main-view "
        "visibility, including EnableMainView/EnableCastShadows inverse events.",
    ),
    0x141ED28E0: (
        "NdAttachableObject_PostJointUpdate_Async",
        "NdAttachableObject vtable+0x410. Updates attachment transforms and "
        "propagates owner DrawControl camera/category state after joint update.",
    ),
    0x141ED62D0: (
        "NdAttachableObject_SyncParentDrawVisibilityAndCameraFlags",
        "Called only by NdAttachableObject_PostJointUpdate_Async. Resolves the "
        "attachment parent through +0x4E0/+0x4E8, synchronizes the child "
        "DrawControl inherited visibility through 0x141754DA0, then propagates "
        "the parent's retained-camera and main-view flag state.",
    ),
    0x141754DA0: (
        "DrawControl_SyncParentVisibilityAndRecomputeInstances",
        "Writes child DrawControl+0x1D from parent +0x1C/+0x1D (or 0xFF for "
        "no parent), combines it with child +0x1C and mesh +0x243/+0x245, and "
        "publishes the resulting active byte to each render instance +0x1A0.",
    ),
    0x141A92A00: (
        "AnimManager_AsyncObjectUpdateJointBlendJob",
        "Source-identified anim-mgr-jobs.cpp AsyncObjectUpdate_JointBlend_Job. "
        "When job flags contain 0x20, resolves retained owner at +0x1A8/+0x1B0 "
        "and dispatches owner vtable+0x410 then +0x418.",
    ),
    0x141A8F9D0: (
        "AnimManager_DispatchRetainedOwnerPhase",
        "Resolves the animation job's retained owner and dispatches vtable phases "
        "+0x400/+0x408/+0x410/+0x418 according to phase bits 8/0x10/0x20.",
    ),
    0x1403E3E30: (
        "NpcManager_UpdateSpotActors",
        "Iterates NpcManager's spot-actor list and calls Npc_UpdatePathSearch; "
        "does not dispatch Player vtable+0x410.",
    ),
    0x1403E84F0: (
        "NpcManager_RegisterSpotActor",
        "Sole append writer for the 16-entry spot-actor handle list at "
        "dword_143D3C0D0+0x10; source string npc-manager.cpp.",
    ),
    0x14043B830: (
        "Npc_UpdatePathSearch",
        "Source-identified Npc::UpdatePathSearch(void), called from the NPC "
        "spot-list tick.",
    ),
    0x1403E59D0: (
        "GameplayWorld_RefreshControlHostEntity6EFlags",
        "Global world tick: joypad-filtered buffer refresh, per-actor loop with "
        "ControlHostHub_RefreshEntity6EProbeFlags on actor+0x1FB0 (gated; Player "
        "vtable+4256 always 0), then always 144610 on worldMgr+0x5A0 hub.",
    ),
    0x142144610: (
        "ControlHostHub_RefreshEntity6EProbeFlags",
        "Walks 112-byte entries at hub+16; when entry+0x6E bit0 set and entry+108 "
        ">=0, sets bit0x4 and conditionally bit0x8 from resolver vtable+104/+120.",
    ),
    0x1401B7500: (
        "Entity6E_MergeFlagByte",
        "Atomic helper: entity+0x6E &= ~1; entity+0x6E |= arg flag byte.",
    ),
    0x140CC08F0: (
        "PlayerBase_SetupNpcAvoidanceCheck",
        "Source string identifies PlayerBase::SetupNpcAvoidanceCheck. Configures "
        "the Player+0x1A20 avoidance helper from point/vector/scalar arguments; "
        "it is not the physical locomotion output.",
    ),
    0x141BB8320: (
        "Character_UpdateDerivedWorldTransformFromLocalRoot",
        "Common Player/NadineBuddy vtable slot +0x2B8. Derives world transform "
        "at +0x240 and root/capsule output at +0x280 from local root +0x130, "
        "local rotation +0x140, and optional parent transform +0x110/+0x120; "
        "also mirrors local root to +0x250.",
    ),
    0x141837CD0: (
        "Joypad_IsCommandActive",
        "Resolves up to two binding descriptors for a named command through "
        "Joypad_ResolveCommandBindings, then accepts the command when either "
        "descriptor passes Joypad_EvaluateCommandDescriptor.",
    ),
    0x141838050: (
        "Joypad_EvaluateCommandDescriptor",
        "Evaluates one authored command descriptor: analog threshold, digital "
        "button state, hold timing, dependencies and temporary command disables.",
    ),
    0x1418384D0: (
        "Joypad_GetCommandValue",
        "Resolves up to two command descriptors, evaluates per-Joypad gamepad "
        "bindings and process-global keyboard bindings separately, and returns "
        "the resulting analog/Boolean command value.",
    ),
    0x141838740: (
        "Joypad_EvaluateKeyboardButtonBinding",
        "Evaluates keyboard/button bindings against four held/pressed/released "
        "64-bit mask groups and per-button timestamps in LocalPlayerInputManager.",
    ),
    0x141838C90: (
        "Joypad_EvaluateGamepadButtonBinding",
        "Evaluates gamepad bindings against Joypad pressed/released/current masks "
        "at +0xD0/+0xD8/+0xE0 and per-button timestamps beginning at +0x10C.",
    ),
    0x14183AE30: (
        "Joypad_ResolveCommandBindings",
        "Binary-searches the authored command table by hash and copies up to two "
        "binding descriptors for Joypad_IsCommandActive.",
    ),
    0x141E397C0: (
        "PlayerInputCommandState_DeriveMoveVectors",
        "Derives two movement magnitudes at state+0x7C and two 16-byte world "
        "movement vectors at state+0x90 from raw pairs at state+0x28, using the "
        "resolved runtime Player transform. Called only by "
        "PlayerInputCommandState_Update.",
    ),
    0x141596D60: (
        "Input_UpdateWindowFocusFlag",
        "Writes g_InputWindowFocused according to whether the game's HWND is "
        "GetForegroundWindow(). This global gates per-player input conversion.",
    ),
    0x141598080: (
        "Input_WindowProcFocusEvents",
        "Window-message handler that sets g_InputWindowFocused for WM_SETFOCUS "
        "and clears it for WM_KILLFOCUS.",
    ),
    0x140C9F8B0: (
        "Player_UpdateInputCommandState",
        "Player vtable slot 16 (+0x80). Sole caller of "
        "PlayerInputCommandState_Update 0x141E39FB0, followed by the broader "
        "Player input/action update.",
    ),
    0x141E39FB0: (
        "PlayerInputCommandState_Update",
        "Updates the embedded Player+0x39A0 input-command state through its two "
        "virtual providers, then derives move direction/magnitude. Sole caller is "
        "Player vtable slot 16 at 0x140C9F8B0.",
    ),
    0x141E39C80: (
        "PlayerInputCommandState_GetMoveVector",
        "Copies a 16-byte move vector selected by index; index 0 is state+0x90 "
        "(Player+0x3A30) and is consumed by PlayerMoveStick_MainUpdate.",
    ),
    0x141E39D10: (
        "PlayerInputCommandState_GetMoveMagnitude",
        "Returns move magnitude selected by index; index 0 is state+0x7C "
        "(Player+0x3A1C) and is consumed by PlayerMoveStick_MainUpdate.",
    ),
    0x14183A1C0: (
        "Joypad_AddTemporaryDisabledCommands",
        "Adds command hashes to the Joypad's 22-entry temporary disable table "
        "with an expiry timestamp. PlayerMoveStick uses it for command 'move'; "
        "this is not an analog-axis reader.",
    ),
    0x14024D7E0: (
        "Player_GetInputCommandState",
        "Returns the embedded Player input-command state at Player+0x39A0. "
        "Its +0x18 field is the Joypad index used independently from the "
        "Player+0x4010 screen index.",
    ),
    0x1402B1890: (
        "PlayerInputCommandState_GetJoypadIndex",
        "Returns dword PlayerInputCommandState+0x18 (Player+0x39B8). "
        "PlayerMoveStick uses it to select FrameState+0xDB0+0xBF0*index.",
    ),
    0x141E39D50: (
        "PlayerInputCommandState_IsCommandActive",
        "Selects the per-player Joypad through state+0x18, then tail-calls "
        "0x141837CD0 with the command hash and transition selector.",
    ),
    0x140F03500: (
        "PlayerMoveStick_MainUpdate",
        "Main StickMoveInterface update (vtable 0x142A30740 +0x60). Reads "
        "Player+0x4010 for screen routing and Player+0x39B8 independently "
        "for Joypad selection; drives move/jump/cover/traversal decisions.",
    ),
    0x140D12B30: (
        "CoverMoveUpdate_UpdatePostAnim",
        "Exact source signature: void CoverMoveUpdate::UpdatePostAnim(Player&), "
        "player-cover-actions.cpp. Reads Player+0x4010 screen index and passes "
        "it to ResolveScreenRoutePoseSeedOrDefault 0x141C077B0 while deriving "
        "cover-movement orientation. Strong static anchor for the per-screen "
        "camera-basis movement path.",
    ),
    0x140D79C00: (
        "PlayerDeathEntry_BE8",
        "Player vtable +0xBE8. Native death-entry variant; clears/updates the "
        "HealthSystem and publishes `player-died`. Remote replicas must not "
        "author this transition while their peer still reports HP > 0.",
    ),
    0x140D79950: (
        "PlayerDeathEntry_BF0",
        "Player vtable +0xBF0. Native player-died publisher variant.",
    ),
    0x140D78EC0: (
        "PlayerDeathEntry_BF8",
        "Player vtable +0xBF8. Native death-entry variant; clears/updates the "
        "HealthSystem and publishes `player-died`.",
    ),
    0x140D792D0: (
        "PlayerDeathEntry_C00",
        "Player vtable +0xC00. Native player-died publisher variant.",
    ),
    0x14042B400: (
        "CharacterDeathCleanupAndPublishFriendDied",
        "Shared death cleanup. If character+0x57C bit 1 is set, publishes "
        "`player-friend-died` to retained player 0. This is a consequence of "
        "an authored Player death, not the original death trigger.",
    ),
    0x141839FB0: (
        "AddOrUpdateJoypadDisabledCommands",
        "Adds or updates a disabled-command record in one per-player joypad "
        "manager. The manager is selected as g_LocalPlayerInputManager +0xDB0 "
        "+ 0xBF0*joypad_index. Two Player jobs concurrently mutated joypad-2, "
        "drove free_count at +0x538 to -1/-2, and crashed at 0x14183A090. The "
        "fixed-array mutation assumes single-Player serialization. Serializing "
        "calls preserved command behavior and survived a fresh campaign load with "
        "slot 1 for over two minutes; free_count=6 and active_count=1 remained sane.",
    ),
    0x140541EF0: (
        "ScriptNpcDieHandler",
        "Campaign npc-die command handler. Performs a native forced death: "
        "checks Pirate death state, applies maximum health damage through "
        "Pirate vtable +0xD00, resolves AiHitController through npc+0x15C8 "
        "and virtual +0x78, builds a clean 0x70-byte HitDescription, then calls "
        "AiHitController vtable +0xB8. This is the safe replicated-death model.",
    ),
    0x14055DF80: (
        "ScriptNpcKillHandler",
        "Script npc-kill handler. Routes to Process::KillTree rather than a "
        "gameplay death transition; unsuitable for replicated combat death.",
    ),
    0x1416A5C20: (
        "ProcessKillTree",
        "ndlib Process::KillTree. Recursively tears down a process hierarchy "
        "and bypasses normal health, hit, ragdoll, and gameplay death handling. "
        "Do not use this as a networked NPC/player death primitive.",
    ),
    0x1402F8B40: (
        "PirateIsDeadViaHealthSystem",
        "Pirate virtual +0x860. Resolves the HealthSystem at npc+0x4A8 and "
        "returns its virtual +0x30 dead test; used by ScriptNpcDieHandler.",
    ),
    0x141DEE1D0: (
        "PirateApplyDirectHealthDamage",
        "Pirate virtual +0xD00. Forwards damage to HealthSystem virtual +0x20 "
        "and returns current health through virtual +0xB8. Used by npc-die "
        "before the explicit AiHitController death transition.",
    ),
    0x140500C20: (
        "AiHitControllerDie",
        "AiHitController virtual +0xB8. Runs the authored NPC death transition "
        "from a HitDescription. ScriptNpcDieHandler calls it with a zeroed "
        "0x70-byte description, direction (0,0,1,0), +0x48=-1 and "
        "+0x64=maxHealth.",
    ),
    0x141FCB210: (
        "HealthSystemApplyDamage",
        "HealthSystem virtual +0x20. Applies integer health damage and updates "
        "the system's current-health/death-related state. Called by "
        "PirateApplyDirectHealthDamage.",
    ),
    0x141FCB510: (
        "HealthSystemIsDead",
        "HealthSystem virtual +0x30. Returns whether virtual +0xB8 current "
        "health is <= 0.",
    ),
    0x141FCAFA0: (
        "HealthSystemGetCurrentHealth",
        "HealthSystem virtual +0xB8. Returns current health, including the "
        "system's bounded regeneration adjustment.",
    ),
    0x141FCA9C0: (
        "HealthSystemGetMaxHealth",
        "HealthSystem virtual +0xC0. Returns the sum of the base and bonus "
        "maximum-health fields at +0x44 and +0x48.",
    ),
    0x141DEF6C0: (
        "ValidateNavigationFrame",
        "Validates a prepared navigation frame under the native NavMesh global "
        "lock. The NPC MoveTo script path requires this check before submitting "
        "a goal; invalid frames must not reach NadineNpcMoveTo_Type02.",
    ),
    0x14027A270: (
        "PublishAttackEventToTarget",
        "Native high-level attack publisher. Accepts a polymorphic AttackInfo "
        "object and a target character, builds the authored Attack event, "
        "dispatches it through the target event path, and publishes the "
        "Attack-broadcast state. This is the preferred damage relay entry.",
    ),
    0x14040E7C0: (
        "NpcHandleAttackEvent",
        "Npc Attack-event handler. Extracts AttackInfo from event+0x28, then "
        "dispatches character virtual slot +0x1170 and performs the surrounding "
        "hitmarker, last-hit, knockdown, kill, and notification logic.",
    ),
    0x140409F70: (
        "CharacterApplyAttackInfo",
        "Shared character virtual at vtable +0x1170 (slot 558). Applies a valid "
        "AttackInfo to a character, calls NpcArmor::OnHealthDamage, updates "
        "HealthSystem, and participates in knockdown/death decisions. Do not "
        "call directly with a partial copied structure.",
    ),
    0x1403D7DF0: (
        "NpcArmorOnHealthDamage",
        "NpcArmor::OnHealthDamage(Npc*, AttackInfo*, float, float). Processes "
        "armor/body-part damage and detach definitions after AttackInfo has "
        "already been built by the upper attack path.",
    ),
    0x1402791C0: (
        "DamageInfoConstructor",
        "Initializes the complete 0x7A0-byte DamageInfo: total damage, point "
        "count, and 24 zeroed points with stride 0x50.",
    ),
    0x1414F1420: (
        "CopyDamageInfo",
        "Copies a complete 0x7A0-byte DamageInfo including total damage, point "
        "count, and all 24 damage points.",
    ),
    0x1402942A0: (
        "GameArmorComputeDamage",
        "Ordinary GameArmor damage calculation. Distributes AttackInfo+0x20 "
        "over AttackInfo+0x24 points and preserves damage for points that do "
        "not map to an armor piece.",
    ),
    0x140294610: (
        "GameArmorComputeExplosionDamage",
        "GameArmor explosion-damage calculation using the derived explosion "
        "fields and armor-piece distances.",
    ),
    0x140294C30: (
        "GameArmorFindPieceForDamagePoint",
        "Maps a damage point's hit-object handle at +0x30/+0x34 to an armor "
        "piece. Returns -1 for zero, stale, non-collision, or unmapped handles.",
    ),
    0x141F38A90: (
        "GameArmorApplyDamageResults",
        "Applies prepared armor-damage result entries to armor pieces.",
    ),
    0x14027A160: (
        "AttackInfoConstructor",
        "Constructs the common 0x860-byte AttackInfo base and initializes damage "
        "points, source handle, type, flags, scalars, and auxiliary fields.",
    ),
    0x140279590: (
        "AttackInfoCopyConstructor",
        "Copies a complete common AttackInfo object, including the large damage "
        "point block and fields through +0x858. Used by derived clone helpers.",
    ),
    0x140279540: (
        "CloneAttackInfo",
        "Virtual clone for the common AttackInfo type. Allocates 0x860 bytes and "
        "calls AttackInfoCopyConstructor.",
    ),
    0x140279FB0: (
        "DeleteAttackInfo",
        "AttackInfo deleting destructor. Frees the complete 0x860-byte object "
        "when flag bit 0 is set.",
    ),
    0x1403FB340: (
        "CloneProjectileAttackInfo",
        "Virtual clone for ProjectileAttackInfo. Allocates 0x8C0 bytes, copies "
        "the AttackInfo base, then copies projectile fields through +0x8B4.",
    ),
    0x1403FB4E0: (
        "DeleteProjectileAttackInfo",
        "ProjectileAttackInfo deleting destructor used through virtual slot +0x0.",
    ),
    0x1408F89E0: (
        "CloneCloseCombatAttackInfo",
        "Virtual clone for CloseCombatAttackInfo. Allocates 0x880 bytes, copies "
        "the AttackInfo base, then copies close-combat fields through +0x878.",
    ),
    0x1408F8B20: (
        "DeleteCloseCombatAttackInfo",
        "CloseCombatAttackInfo deleting destructor used through virtual slot +0x0.",
    ),
    0x140048440: (
        "RegisterScriptCommand_SpawnPlayer",
        "Registers the retained script command entry for spawn-player. Stores a "
        "generic command callback and command metadata; the larger script-side "
        "implementation is recovered separately around 0x140F73910.",
    ),
    0x14007D040: (
        "RegisterNetSpawnCoopBuddyCommand",
        "Registers the retained network command net-spawn-coop-buddy and binds "
        "it to the large runtime handler at 0x140B56090.",
    ),
    0x1403F6890: (
        "NadineNpcMoveTo_Type02",
        "Confirmed native NPC MoveTo writer. Initializes type 0x02 locomotion "
        "command and fills nav frame, facing, move parameters, flags, and "
        "aux fields.",
    ),
    0x141DF2D60: (
        "BuildNavigationFrame",
        "Confirmed navigation-goal builder used by Nadine movement paths.",
    ),
    0x1403F5D30: (
        "RequestDisableAi",
        "Confirmed Nadine AI disable path. Tested as RequestDisableAi(npc, false, true).",
    ),
    0x1403F5C80: (
        "RequestEnableAi",
        "Confirmed Nadine AI restore path. Tested as RequestEnableAi(npc, false).",
    ),
    0x1403F5B60: (
        "InitPendingNpcActionCommand",
        "Resets and initializes the pending NPC action block at npc+0x2470. "
        "Writers in the 0x140545000..0x14056B000 family then fill command kind "
        "at +0x24F4, timing/flags around +0x24F8/+0x2500/+0x2560, and optional "
        "snapshot payloads. This is a prepared action/behavior layer, not a raw "
        "direct-input seam.",
    ),
    0x1403F61C0: (
        "WriteLocoCommand_Type13",
        "Writer for locomotion command type 0x13. Sets +0x2500, mutates +0x2560, "
        "and rebuilds nav frame via 0x14014E240 + BuildNavigationFrame.",
    ),
    0x1403F68C0: (
        "WriteLocoCommand_Type02",
        "Writer for locomotion command type 0x02. Fills nav frame, facing, move "
        "parameters, +0x2514, +0x2560, +0x2561.",
    ),
    0x1403F69B0: (
        "WriteLocoCommand_Type04",
        "Writer for locomotion command type 0x04. Stores +0x2508, move payload, "
        "and flags consumed by dedicated interpreter branch.",
    ),
    0x1403F6C20: (
        "WriteLocoCommand_Type07",
        "Writer for locomotion command type 0x07. Stores scalar parameters at "
        "+0x2558/+0x255C and clears npc+0x1D57 bit 0x2.",
    ),
    0x14041F700: (
        "CopyLocoCommandBlock",
        "Copies locomotion command block from source (&npc+0x2470) into another "
        "internal buffer. Preserves flag bytes at offsets +0xF0/+0xF1.",
    ),
    0x14041E0A8: (
        "CheckLocoCommandTimeGate",
        "Reads npc+0x2500 and npc+0x24F8 before command copy/dispatch path.",
    ),
    0x1403E0400: (
        "ValidateAcquireLocoContext",
        "Validation/acquisition helper used before locomotion command processing.",
    ),
    0x1403DF370: (
        "ProcessLocoStateBranch",
        "Large locomotion-related state handler reached by downstream command paths.",
    ),
    0x1406B2360: (
        "IsInterestingLocoCommandType",
        "Classifier on npc+0x24F4 using bitmask 0x914 over command types 0..0x0B.",
    ),
    0x1406B2C90: (
        "BuildLocoDirectionFromCommand",
        "Interpreter helper for command types 0x02/0x04/0x08/0x0B. Type 0x04 uses "
        "dedicated branch; others either use stored nav frame or fallback orientation path.",
    ),
    0x1406B591C: (
        "DispatchSpecialLocoCommandTypes",
        "Special dispatcher for command types 0x09 and 0x11 before generic "
        "locomotion processing continues.",
    ),
    0x1406AD290: (
        "ResetPendingLocoDispatch",
        "Clears pending dispatch-related state around npc+0x27D8 and npc+0x26A4.",
    ),
    0x140176990: (
        "GetNpcFlag_1D57_Bit1",
        "Getter for npc+0x1D57 bit 0x2.",
    ),
    0x140177D4F: (
        "CheckNpcFlag_1D57_Bit1_Path",
        "Downstream state path that tests npc+0x1D57 bit 0x2.",
    ),
    0x140420441: (
        "CheckNpcFlag_1D57_Bit2_Path",
        "Downstream state path that tests npc+0x1D57 bit 0x4.",
    ),
    0x140B56090: (
        "HandleNetSpawnCoopBuddy",
        "Large retained co-op buddy/network spawn handler. Resolves level or "
        "context objects, consumes roster-like state, and can resolve a live "
        "entity through 0x141D22420.",
    ),
    0x140B580C0: (
        "SelectRetainedCoopPlayerLookHash",
        "Selects the retained co-op player-look hash from the network/co-op mode "
        "field at manager+0x558. Confirmed stock modes choose coop-drake, "
        "coop-chloe, or coop-sullivan.",
    ),
    0x140F73910: (
        "ScriptSpawnPlayer",
        "Large retained spawn-player implementation. Builds or copies a spawn "
        "descriptor, then dispatches into 0x1410F1910 or 0x1410F3350.",
    ),
    0x1400AA8C0: (
        "RegisterPlayerCtorFactoryIntoGlobalPlayerSlotBF00",
        "Startup registration helper that publishes 0x1410C6F00 as the real "
        "Player alloc/ctor pointer through 0x1417C4650 into the global slot "
        "at 0x143EEBF00.",
    ),
    0x1400AA940: (
        "RegisterSiblingPlayerHelperFactoryIntoGlobalSlotBF90",
        "Startup registration helper adjacent to the Player ctor registration. "
        "Seeds the sibling global slot family at 0x143EEBF90/0x143EEBFA0 with "
        "0x1410C6F60 and fixed metadata size 0x8.",
    ),
    0x1401696B0: (
        "CheckEntityAgainstGlobalPlayerSlotBF00Bitset",
        "Small runtime gate that tests a live object's bitset at rcx+0x30 "
        "against selector/id data stored in the globally registered Player "
        "slot record at 0x143EEBF00.",
    ),
    0x140F73C90: (
        "CopySpawnDescriptor_0x184",
        "Memcpy-like helper for the retained spawn-player descriptor block. "
        "Copies two 0x80-byte chunks plus tail fields through offset 0x184.",
    ),
    0x1410F14C0: (
        "CommitResolvedPlayerSpawn",
        "Central retained player-spawn commit path. Accepts resolved look/"
        "transform/context data, seeds a real ProcessSpawnInfo-class stack "
        "packet with vtable 0x14291CF98, initializes it through 0x1417C32A0, "
        "builds a second local packet layer plus fallback via 0x1417651D0, "
        "now clearly tags that path with `Player` and `*player-health-settings*`, "
        "materializes a runtime object through 0x1417C3790, then harvests the "
        "returned object's +0xA8/+0xBC retained-live pair and stores it in the "
        "global retained player-slot table at 0x143EEBE80.",
    ),
    0x1417C32A0: (
        "InitializeSelectorBackedProcessSpawnPacket",
        "Compact packet initializer used by retained/player runtime builders. "
        "Seeds packet fields at +0x10/+0x18/+0x20/+0x28/+0x30/+0x38/+0x48, "
        "stores the incoming selector/hash id in +0x8, resolves a selector/"
        "capability entry into +0x50, and derives a mask/limit into +0x40. "
        "The base field at +0x20 is copied directly from arg4/r9, which is "
        "important because the real Player::Init path later reads "
        "incoming_packet+0x20 before any draw-control or late mounted-owner "
        "stages run. "
        "This is the shared front-end base later consumed either directly by "
        "0x1417C3790 or through the extended wrapper family at "
        "0x1417C35A0 / vtable 0x1428D5D60.",
    ),
    0x1417C35A0: (
        "InitializeExtendedSelectorBackedRuntimeStatePacket_1428D5D60",
        "In-place initializer for the extended selector-backed packet family "
        "with vtable 0x1428D5D60. Calls 0x1417C32A0 for the shared base "
        "fields, then seeds wrapper-local state at +0x58/+0x60/+0x68/+0x70/"
        "+0x78/+0x80/+0x88..+0x98. The base +0x20 source link survives this "
        "wrap and is exactly what the early Player::Init path later passes "
        "into 0x1416860A0. This wrapper also preserves the upper packet shape "
        "at +0x40/+0x48/+0x58 used later by 0x1418C4840, which makes it a "
        "stronger external_state candidate than the materialized object "
        "returned by 0x1417C3790.",
    ),
    0x1417C3790: (
        "ResolveOrMaterializeRuntimeStateObjectFromSelectorPacket",
        "Consumes the compact packet built by 0x1417C32A0. Requires packet "
        "+0x50, checks selector/capability gates through 0x1417C4800, builds "
        "a returned runtime object family, copies packet payload blocks from "
        "+0x20/+0x40 into that object, stores the packet owner/source at "
        "object+0xA0, links the intrusive node back to the object through "
        "node+0x8, stores that node pointer at object+0xA8, and recomputes "
        "object+0xBC from descriptor-local state via 0x14161FC60. So the "
        "canonical +0xA8/+0xBC runtime identity later consumed by Player-side "
        "camera/state code is generic materializer output, not a direct "
        "Player ctor field. The returned object still looks more like a "
        "materialized runtime companion to the packet than the final "
        "Player-side external_state itself; the stronger direct shape match "
        "for 0x1418C4840 remains the extended wrapper family at 0x1417C35A0.",
    ),
    0x1415FC800: (
        "InsertOrRelinkRuntimeStateNodeBySpanIntoIndexedContainer",
        "Intrusive-node/container helper used directly inside "
        "0x1417C3790. Claims or re-links a node, stores span/count at "
        "node+0x20, links it through owner+0x70/+0x80/+0x98, and updates "
        "owner+0xA8. This is object-local bookkeeping for the materialized "
        "runtime family, not the upper Player apply seam.",
    ),
    0x1415FC9F0: (
        "UnlinkRuntimeStateNodeFromIndexedContainer",
        "Inverse intrusive-node/container helper paired with 0x1415FC800. "
        "Detaches a node from owner+0x70/+0x80/+0x98, decrements owner+0xA8, "
        "and clears node fields. Also used directly inside 0x1417C3790, "
        "which pins both helpers as internal bookkeeping for the returned "
        "materialized object family.",
    ),
    0x1416A3330: (
        "ConstructBaseRuntimeObjectWithNullHandleAndFreshObjectId",
        "Lowest shared runtime-object constructor beneath the hidden "
        "descriptor/object family. Installs the base vtable, clears the "
        "canonical identity handle at +0xA8, seeds +0xBC from 0x1416A38A0, "
        "and leaves the object ready for a later materializer to attach a "
        "real owner/node link.",
    ),
    0x1416A38A0: (
        "AllocateFreshRuntimeObjectId",
        "Atomic shared id allocator used by the base runtime-object ctor at "
        "0x1416A3330 to seed the initial object id stored at +0xBC before any "
        "later materialization step rewrites that runtime identity.",
    ),
    0x1410F0D10: (
        "GetRetainedPlayerEntityBySlot",
        "Reads a 16-byte entry from g_RetainedPlayerSlotTable[slot], validates "
        "the embedded runtime id against entity+0xBC, and returns the live "
        "entity pointer when the slot is still valid.",
    ),
    0x1410F0D60: (
        "CopyRetainedPlayerSlotEntry",
        "Copies a raw 16-byte entry from g_RetainedPlayerSlotTable[slot] into "
        "caller-provided storage.",
    ),
    0x1410F0D80: (
        "FocusRetainedPlayerEntityBySlot",
        "Resolves a retained player entity by slot and jumps into a downstream "
        "focus/activation path at 0x14029ACB0.",
    ),
    0x1410F0DD0: (
        "FindRetainedPlayerSlotIndex",
        "Searches the 6-entry g_RetainedPlayerSlotTable for a matching "
        "(handle,id) pair and returns the slot index.",
    ),
    0x1410F0E00: (
        "CopyRetainedPlayerSlot0Entry",
        "Copies g_RetainedPlayerSlotTable[0] into caller-provided storage.",
    ),
    0x140298D70: (
        "ResolveLocalPlayerVFunc_A8",
        "Resolver sub-object (*(0x143FE1070)) vtable +0xA8. Split/coop gate byte "
        "0x143E07473: if inactive (ordinary campaign) tail-calls "
        "ResolveRetainedPlayerHandleBySlot 0x1410F0F60(index). If split/coop "
        "active, scans net-player slot array 0x143DD7488 (NetGameManager+0x3C8, "
        "stride 0x43D0, dormant in campaign). So in campaign local-player "
        "resolution funnels into the 0x143EEBE80 slot table reader.",
    ),
    0x1410F0F60: (
        "ResolveRetainedPlayerHandleBySlot",
        "Resolves a retained slot entry into the live player handle/object. "
        "Campaign branch (split gate 0x143E07473 inactive) reads 16-byte pair "
        "from g_RetainedPlayerSlotTable 0x143EEBE80 + index*0x10 {ptr+0,id+8}, "
        "validates ptr->(+8)->+0xBC==id, returns ptr (marks +0x72) else 0. Slot "
        "1 empty in campaign => local-player-1==0. Writer of these pairs is "
        "CommitResolvedPlayerSpawn 0x1410F14C0.",
    ),
    0x1410F3DD0: (
        "RefreshPrimaryRetainedPlayerCamera",
        "Touches retained player slot 0, restores entity focus, then queries "
        "the primary screen-route entity and updates camera state through "
        "0x141C078D0.",
    ),
    0x1410F3E40: (
        "CheckRetainedPlayerSlotActive",
        "Validates a retained slot entry, resolves the live entity, and calls "
        "entity virtual slot +0x860 to test or update active state.",
    ),
    0x1410F3A80: (
        "WritePrimaryRetainedPlayerSlot0",
        "Writes a resolved (handle,id) pair into g_RetainedPlayerSlotTable[0] "
        "and mirrors the same pair into caller-provided output storage.",
    ),
    0x1410F3B20: (
        "PromoteRetainedPlayerSlot",
        "Builds or resolves a retained player record for the requested slot, "
        "stores the resulting (handle,id) pair into g_RetainedPlayerSlotTable[slot], "
        "and marks the resolved entity with byte +0x368C. The current disasm "
        "also shows that it manually builds the same base ProcessSpawnInfo "
        "packet plus extended wrapper family (`0x1417C32A0` then local vtable "
        "0x1428D5D60) before passing that state into the upper retained "
        "manager path, which makes it one of the strongest concrete promotion-"
        "adjacent retained entrypoints found so far.",
    ),
    0x1410F3F50: (
        "IsPrimaryRetainedPlayerAvailable",
        "Returns success when retained slot 0 is active/available, gated by "
        "global debug/runtime flags.",
    ),
    0x1410F3F80: (
        "HasPrimaryRetainedPlayerEntity",
        "Checks whether retained slot 0 resolves to a live entity, falling back "
        "to CheckRetainedPlayerSlotActive(0) when needed.",
    ),
    0x1410F3FC0: (
        "IsRetainedPlayerSlotUsable",
        "Validates an arbitrary retained slot index through "
        "CheckRetainedPlayerSlotActive(slot), gated by the same global runtime "
        "flags as the primary-slot helpers.",
    ),
    0x1410F8E90: (
        "RefreshAllRetainedPlayerEntities",
        "Iterates over the six entries of g_RetainedPlayerSlotTable and calls "
        "0x1410F8D80 for each live slot entity.",
    ),
    0x141C07420: (
        "SwitchRetainedCameraModeStateBlocks",
        "Retained-camera mode-switch bridge. Writes CameraManager+0x128 and "
        "updates two global camera-state blocks selected from the shared "
        "slot-state array using CameraManager+0x124 plus relative indices "
        "+2 and +4.",
    ),
    0x141C078D0: (
        "UpdateScreenRouteEntityCameraStateFromActorTransform",
        "Heavy screen-route entity update path. Walks the route's child-control "
        "list at +0x100, selects a preferred child/fallback descriptor, "
        "refreshes route-local follow state through +0x2F0, and commits the "
        "final camera descriptor through 0x141C06F20.",
    ),
    0x141C074E0: (
        "AttachScreenRouteEntityToResolvedActorPair",
        "Enables route-entity attachment mode, refreshes the attached actor "
        "pair at +0x2A8/+0x2B0 via 0x141C0DFD0, and marks the active screen "
        "lane in the shared camera state block.",
    ),
    0x141C07640: (
        "DetachScreenRouteEntityFromResolvedActorPair",
        "Disables route-entity attachment mode, clears the corresponding "
        "screen-lane state, and notifies the currently attached actor pair "
        "through virtual +0x208 when still valid.",
    ),
    # (0x141C077B0 ResolveScreenRoutePoseSeedOrDefault: superseded by the
    #  detailed 2026-07-05 entry in the fire-ray section below.)
    0x141C06F20: (
        "InsertRouteChildAndRecomputeActiveCameraDescriptor",
        "Core screen-route entity update worker. Inserts or refreshes a route "
        "child entry, carries forward the best available follow-state from the "
        "current child list, and commits the resulting active camera "
        "descriptor/follow payload back into the route entity.",
    ),
    0x141C0E9F0: (
        "SelectFirstRouteChildAcceptedByCameraManagerCustom",
        "Walks the route entity's child-control list at +0x520. Uses each "
        "child's owner callbacks at +0x70 and the CameraManagerCustom bundle "
        "callback at virtual +0x38 to choose the first acceptable child, then "
        "caches that child's descriptor payload into route-local state.",
    ),
    0x1413BA140: (
        "HandleRetainedPlayerCameraMessage",
        "Opcode/hash-dispatch handler for retained-player camera state. One "
        "branch builds camera/transform state from the current live player, one "
        "branch resolves a player/entity record and caches its (handle,id) at "
        "this+0x658, and one branch re-activates that cached retained entity.",
    ),
    0x1413B9EA0: (
        "ReactivateCachedRetainedPlayerEntity",
        "Reads cached retained-player (handle,id) from this+0x658, validates "
        "the live entity, marks it active via byte +0x72, then forwards the "
        "owning object into downstream retained/camera update paths.",
    ),
    0x1414538E0: (
        "BuildRailVehicleRetainedCameraStateRecord",
        "Builds retained rail/vehicle camera-state records, not a generic "
        "second-camera owner command. Exact engine-hash branches now match "
        "`npc-started-vehicle-jump`, `get-speed`, `on-animation-started`, "
        "`on-animation-done`, `slow-to-stop`, `rail-scrub`, `get-spline-id`, "
        "`get-surface-pat`, and `transfer-to-spline`; an additional branch "
        "resolves a retained/live pair through `get-rail-controller`, derives "
        "a transform from the live object, and forwards it into "
        "`ApplyRetainedRailControllerCameraCommand(..., edx=4)`.",
    ),
    0x141444010: (
        "ResolveRetainedVehicleClassFromPair",
        "Hashes and dispatches the retained query `get-vehicle-class` against "
        "the live object resolved from a retained (handle,id) pair, returning "
        "the resulting boxed/typed value when the pair is valid.",
    ),
    0x141444C50: (
        "ResolveRetainedRailControllerFromPair",
        "Hashes and dispatches the retained query `get-rail-controller` "
        "against the live object resolved from a retained (handle,id) pair, "
        "then returns the resulting rail-controller-like object when valid.",
    ),
    0x14145BAD0: (
        "ApplyRetainedRailControllerCameraCommand",
        "Applies a retained rail-controller/vehicle camera command. Stores "
        "the command type at +0x574, mirrors the payload into +0x9F0, toggles "
        "state bits at +0x572, hashes `go-ragdoll`, and dispatches the "
        "resulting record through the resolved live object via virtual +0xD0. "
        "Used from the rail/vehicle retained-camera state builder with command "
        "types 1/2/3/4.",
    ),
    0x1417BF750: (
        "AppendRetainedCameraRecord",
        "Appends a 0x30-byte retained-camera command/state record into a "
        "grow-only array, preserving the embedded handle/id and flag bytes.",
    ),
    0x141634F20: (
        "QueueRetainedCameraWorkItem",
        "Allocates and enqueues a retained-camera work item/object, links it "
        "into the owner's queue, then dispatches through a virtual method on "
        "the new item.",
    ),
    0x1416350A0: (
        "QueueRetainedCameraPayloadCommand_Type02",
        "Builds and enqueues a larger retained-camera command with inline "
        "payload data copied from the caller-supplied record. Marks queue bit "
        "0x2 and can dispatch immediately through the new item's vtable.",
    ),
    0x141635210: (
        "QueueRetainedCameraSimpleCommand_Type04",
        "Allocates and enqueues a simple retained-camera command of type 0x4. "
        "Stores the requested command id in the queue entry and mirrors it into "
        "the new item at +0x20.",
    ),
    0x141635310: (
        "QueueRetainedCameraSentinelCommand_Type04",
        "Allocates and enqueues a type-0x4 retained-camera command with "
        "sentinel id 0x7FFFFFFF and no explicit payload id.",
    ),
    0x1416354E0: (
        "QueueRetainedCameraSimpleCommand_Type08",
        "Allocates and enqueues a simple retained-camera command of type 0x8.",
    ),
    0x1416355B0: (
        "QueueRetainedCameraParameterizedCommand_Type10",
        "Allocates and enqueues a retained-camera command of type 0x10 with "
        "two caller-provided integer parameters copied into the new item.",
    ),
    0x1416D1330: (
        "InitRetainedCameraCommandContext",
        "Initializes a retained-camera command context, resolves an attached "
        "runtime node by command id, caches it at +0x50, and normalizes an "
        "aligned pointer/state value into +0x40.",
    ),
    0x1417A4B00: (
        "ConstructRetainedCameraCommandBus",
        "Large owner/manager constructor for the retained-camera command bus. "
        "Initializes command-record blocks, queue storage, descriptor tables, "
        "and multiple owner-side arrays/flags through roughly +0x5B0.",
    ),
    0x1417A4AB0: (
        "ResolveRetainedCameraPrimarySource",
        "Returns owner+0x58 when present, otherwise falls back to owner+0x50. "
        "Used by bus initialization and later query/dispatch helpers.",
    ),
    0x1417A5A90: (
        "ResetRetainedCameraCommandBusState",
        "Flushes descriptor buffers referenced by owner+0x4E0, clears queued/"
        "active bus state around +0x4E8/+0x500/+0x510/+0x51C, and resets "
        "several owner-side pointers while leaving subsystem enabled.",
    ),
    0x1417A5C10: (
        "HasRetainedCameraActiveRoot",
        "Checks whether the bus has active entries and whether the primary "
        "root reachable from owner+0x4F8 resolves to a non-null runtime node.",
    ),
    0x1417A5C40: (
        "DispatchRetainedCameraActiveRootVFunc10",
        "Resolves the active root from owner+0x4F8 and invokes its virtual "
        "slot +0x10 when available.",
    ),
    0x1417A5CA0: (
        "CheckRetainedCameraRouteForCommand",
        "Tests the active retained-camera route for a command id using helpers "
        "under the root node reachable from owner+0x4F8 and owner+0x530.",
    ),
    0x1417A5D50: (
        "ResolveRetainedCameraCommandResult",
        "Resolves a command-specific result/value through the active retained-"
        "camera root and writes it to caller output when found.",
    ),
    0x1417A5DD0: (
        "QueryRetainedCameraCommandInt",
        "Queries an integer-like value through the active retained-camera root "
        "using owner+0x530 and writes zero on failure.",
    ),
    0x1417AEDE0: (
        "DispatchRetainedCameraCommandBool",
        "Builds a compact retained-camera context snapshot from the root object, "
        "then tests command handling through root branches at +0x38/+0x40. "
        "Returns a boolean-like result via the downstream root handlers.",
    ),
    0x1417AEE60: (
        "ResolveRetainedCameraCommandObject",
        "Builds the same compact retained-camera context snapshot, then "
        "resolves a command-specific object/result through root branches "
        "+0x38/+0x40.",
    ),
    0x1417AF120: (
        "BuildRetainedCameraRootContextSnapshot",
        "Builds a compact 0x30-byte snapshot from a retained-camera root "
        "object, including timing/scalar state, route ids, and flags derived "
        "from the attached bus at root+0x60.",
    ),
    0x141844140: (
        "HandleRetainedCameraEventType04_UpdateGlobals",
        "Small event/callback-style stub for retained-camera event type 4. "
        "Reads a global context pointer at +0x1C0 and forwards it into "
        "UpdateRetainedCameraRouteGlobals.",
    ),
    0x141844170: (
        "ConstructRetainedCameraRouteGraph",
        "Monolithic retained-camera route-graph constructor. Allocates and "
        "links a chain/tree of typed route nodes and callbacks rather than "
        "acting as a simple runtime matcher API.",
    ),
    0x141844DE0: (
        "ConstructRetainedCameraRouteGraph_InternalBlockA",
        "Internal block inside ConstructRetainedCameraRouteGraph; one of "
        "several typed route-node allocation/link stages, not a standalone "
        "external function.",
    ),
    0x141845590: (
        "ConstructRetainedCameraRouteGraph_CommandIdLeafBlock",
        "Internal route-graph construction block associated with command-id "
        "leaf handling. This address lies inside the larger constructor body, "
        "not at a standalone function boundary.",
    ),
    0x141845640: (
        "ConstructRetainedCameraRouteGraph_LinkedDescriptorLeafBlock",
        "Internal route-graph construction block associated with "
        "descriptor-linked leaf handling. This address is an internal stage of "
        "the monolithic constructor.",
    ),
    0x1418457A0: (
        "BuildRetainedCameraRouteResolutionRecord",
        "Standalone helper that builds a compact route-resolution record from "
        "a resolved route candidate. Scans runtime registries and writes "
        "result indices/ranges into the output structure.",
    ),
    0x141845790: (
        "BuildRetainedCameraRouteResolutionRecord_Thunk",
        "Small thunk/adapter that forwards into "
        "BuildRetainedCameraRouteResolutionRecord.",
    ),
    0x141845980: (
        "UpdateRetainedCameraRouteGlobals",
        "Standalone retained-camera route-state helper reached from event/callback "
        "paths. Updates a small global state block from the supplied record/context; "
        "likely route/debug/global sync rather than the core matcher itself.",
    ),
    0x141845990: (
        "UpdateRetainedCameraRouteGlobals_InternalBlock",
        "Internal block inside UpdateRetainedCameraRouteGlobals, not a "
        "standalone query-object matcher function.",
    ),
    0x1409D6C90: (
        "BuildRetainedCameraStateWrapper",
        "Thin wrapper around BuildRetainedCameraStateRecord that stores the "
        "resulting record plus auxiliary state at object offsets +0x130..+0x170.",
    ),
    0x140745CD0: (
        "ToggleManualCamera1Binding",
        "Registered as `Manual Camera 1`. For mode/state 4, toggles "
        "screen-route entity 0 between 0x141C07640 detach and 0x141C074E0 "
        "attach based on whether route+0x2A8/+0x2B0 already resolves to the "
        "same live actor.",
    ),
    0x140746150: (
        "ToggleManualCamera2Binding",
        "Registered as `Manual Camera 2`. For mode/state 4, toggles "
        "screen-route entity 1 between 0x141C07640 detach and 0x141C074E0 "
        "attach using the same cached actor-pair validation as the screen-0 "
        "path.",
    ),
    0x14074D200: (
        "RegisterManualCameraAndJoypadHandlerCluster",
        "Builds and registers the Manual Camera 1/2, Joypad 1/2 Controls Game, "
        "Swap Joypad Screens, and Bind Manual Cam To Player(/Parent) cluster.",
    ),
    0x140746260: (
        "HandleJoypad1ControlsGameForManualCamera2",
        "Registered as `Joypad 1 Controls Game`. For screen-route entity 0, "
        "either drives 0x141C07420 when a cached actor pair is live or only "
        "toggles the lane-state bands under 0x143FE1088.",
    ),
    0x140746340: (
        "HandleJoypad2ControlsGameForManualCamera2",
        "Registered as `Joypad 2 Controls Game`. Mirrors/toggles bit 0x10 "
        "between paired objects, then either drives 0x141C07420 for a live "
        "cached actor pair or clears/toggles the same lane-state bands under "
        "0x143FE1088.",
    ),
    0x140746460: (
        "RefreshManualCameraResolvedActorCache_Primary",
        "Manual-camera owner/focus update path. Uses the cached actor pair at "
        "0x143D76C10/18, calls the cached target virtual slot +0x208 when a "
        "live actor exists, updates the base screen-route entity byte +0x153, "
        "and otherwise rebuilds the cache through 0x141C0DFD0.",
    ),
    0x1407465E0: (
        "RefreshManualCameraResolvedActorCache_Sibling",
        "Sibling manual-camera owner/focus update path that shares the same "
        "0x143D76C10/18 actor-pair cache and 0x141C0DFD0 fallback build.",
    ),
    0x14073C9E3: (
        "CommitGameplayCameraPriorityDescriptorViaRouteEntity_Internal",
        "Internal gameplay-facing caller block above 0x141C0DFD0. Uses "
        "0x141C0D4B0 to select the current route child, compares that child "
        "against descriptor/sentinel objects, then pushes stack-built "
        "descriptors through 0x141C0DFD0.",
    ),
    0x14073C400: (
        "CopyCameraOverlayDescriptorStateAndMergeFlagBits",
        "Copies the 0x50-byte camera-overlay descriptor state and only merges "
        "the low two flag bits at +0x4C, leaving the rest of the payload "
        "byte-for-byte identical.",
    ),
    0x14073B4E0: (
        "CameraManagerCustom_NoOpCallback0",
        "Top CameraManagerCustom vtable slot. Returns immediately and acts as "
        "a startup-published no-op observer callback.",
    ),
    0x14073B4F0: (
        "CameraManagerCustom_NoOpCallback8",
        "Second CameraManagerCustom vtable slot. Also returns immediately; the "
        "bundle's live behavior starts at later slots, not here.",
    ),
    0x14073B650: (
        "IsCameraManagerCustomDescriptorStateKind2",
        "Small CameraManagerCustom vtable predicate. Returns true only when "
        "the descriptor state's dword at +0xC equals 2.",
    ),
    0x14070C620: (
        "AllocOrInitCameraControlCinemaFollow3E0",
        "Alloc-or-init wrapper for a 0x3E0 CameraControlCinemaFollow object. "
        "Optionally allocates 0x3E0 bytes through 0x1415A0E90, runs the "
        "CameraControlStick base constructor, installs the "
        "CameraControlCinemaFollow vtable at 0x142983288, and clears the "
        "subclass follow-state fields around +0x350/+0x3A8/+0x3B0/+0x3B8.",
    ),
    0x14070C6E0: (
        "DestroyCameraControlCinemaFollowAndFree3E0",
        "Freeing destructor for CameraControlCinemaFollow. Restores the "
        "CameraControlStick vtable at 0x142BB1D98, clears screen index +0x124, "
        "runs shared camera-control cleanup through 0x1416A3800, and frees "
        "0x3E0 bytes when the low destructor flag is set.",
    ),
    0x14070C750: (
        "InitCameraControlCinemaFollowSubclass",
        "CameraControlCinemaFollow-flavored init wrapper. Calls "
        "InitCameraControlStickBaseForScreenAndConfig, then seeds additional "
        "follow-state fields around +0x398/+0x39C/+0x3A4/+0x3A8 for the "
        "screen-specific follow behavior.",
    ),
    0x140750D20: (
        "AllocOrInitCameraControlSpider520",
        "Alloc-or-init wrapper for a 0x520 CameraControlSpider object. Runs "
        "the CameraControlStick base constructor, installs the "
        "CameraControlSpider vtable at 0x14298B5A0, initializes the embedded "
        "subobject at +0x3A0 through 0x141D9EF50, and clears subclass state "
        "around +0x380/+0x388/+0x4B0..+0x4F0.",
    ),
    0x140750E70: (
        "InitCameraControlSpiderSubclass",
        "CameraControlSpider-flavored init wrapper above "
        "InitCameraControlStickBaseForScreenAndConfig. Builds an 8-block CRC "
        "over the static overlay template at 0x142BEA360, publishes the "
        "result through 0x141E776A0, and stores the produced handle at +0x390 "
        "with scalar defaults at +0x3B0/+0x480.",
    ),
    0x14073C620: (
        "NormalizeSpecialCameraControlDescriptorToContextualLook",
        "CameraManagerCustom vtable method. For special camera-control "
        "descriptors such as edge, cover, "
        "knocked-down, evade, monkey-bars, balance/off-balance, free-rope, "
        "shield, follow, and vehicle, rewrites the target state through "
        "0x141C04E90 using `kCameraContextualLook`. Also special-cases "
        "`kCameraStrafe` when byte +0x120 is clear.",
    ),
    0x14073C760: (
        "ShouldAcceptCameraOverlayCandidateForRouteEntity",
        "CameraManagerCustom vtable method. Overlay-compatibility gate over a "
        "route entity and candidate camera descriptor. Rejects "
        "melee/melee-override sentinels, checks the current preferred route "
        "child through 0x141C0D4B0, and combines 0x14073BF80 / 0x14073C0E0 "
        "filter results before accepting the candidate.",
    ),
    0x14073C8A0: (
        "UpdatePerScreenCameraOverlayPriorityStateAndCommitRouteDescriptors",
        "CameraManagerCustom vtable method. Per-screen gameplay camera-overlay "
        "updater keyed by route+0x124. Uses the zero-initialized dynamic "
        "state bank at 0x143D763E0 with stride 0x2B0, chooses between overlay "
        "sentinels such as `kCameraStrafe`, `kCameraSwim`, and `kCameraZoom`, "
        "and commits the resulting stack-built descriptors through "
        "0x141C0DFD0.",
    ),
    0x14073CDD0: (
        "BuildPerScreenCameraOverlayFollowStateFromRouteDescriptor",
        "CameraManagerCustom vtable method. Builds/refreshes a 0x50-byte "
        "per-screen overlay follow-state from a source descriptor and route "
        "entity. Hash-matches `CameraControlZoom` and "
        "`CameraControlScreenShot`, consumes the static tuning banks at "
        "0x1430732F8 with stride 0xC0, and outputs the resolved follow state "
        "used by the gameplay camera overlay path.",
    ),
    0x14073B780: (
        "DestroyLargeCameraControlOverlaySubclassAndFree1FA0",
        "Freeing destructor for the live 0x1FA0 overlay subclass produced by "
        "0x14073B6E0. Tears down the embedded subobject at +0x910 through "
        "0x141DA5AA0, restores the CameraControlStick-class vfptr, resets "
        "screen index +0x124, then runs the shared camera-control cleanup at "
        "0x1416A3800. Frees 0x1FA0 bytes when the low destructor flag is set.",
    ),
    0x14073C460: (
        "InitializeGlobalCameraManagerCustomBundle_3053850",
        "Startup initializer for the global CameraManagerCustom-flavored bundle "
        "at 0x143053850. Seeds the bundle's first qword with vfptr "
        "0x142987DB8 and clears the adjacent cached pointers/flags/scalars "
        "through 0x1430538B8.",
    ),
    0x14073C520: (
        "ResetGlobalCameraManagerCustomBundle_3053850",
        "Reset/teardown companion for the global bundle at 0x143053850. "
        "Re-seeds the first qword with vfptr 0x142987DB8, then frees and "
        "clears the cached heap pointers stored in the neighboring qword slots.",
    ),
    0x14073B6E0: (
        "AllocOrInitLargeCameraControlOverlaySubclass1FA0",
        "Alloc-or-init wrapper for a 0x1FA0 large camera-control subclass in "
        "the overlay/route family. Allocates 0x1FA0 when needed, runs the "
        "shared large CameraControlStrafe-flavored constructor at 0x140772E20, "
        "then overwrites vfptr 0x142987E00 on the resulting object.",
    ),
    0x14073E3C0: (
        "DoesScreenRouteCurrentCameraChildMatchDescriptor",
        "Resolves the screen-route entity by index through 0x141C06420, "
        "selects the current preferred camera child via 0x141C0D4B0, and "
        "checks whether the child's descriptor at +0x1A8 matches the "
        "descriptor pointer supplied by the caller.",
    ),
    0x141C04F80: (
        "InitializeTwoScreenRouteEntityPairTable",
        "Startup/EvaluateCameraRequests helper that loops exactly two screen "
        "indices, builds selector-backed runtime packets through "
        "0x1417C32A0 -> 0x1417C3790, and stores the resulting (ptr,id) pairs "
        "into the two-entry screen-route table at 0x14697AD50.",
    ),
    0x141C04E90: (
        "ApplyCameraTargetStateFromDescriptorWithControlFallback",
        "Packages a camera-control object's current descriptor state plus an "
        "optional live actor pair and forwards it to "
        "ApplyCameraTargetStateFromDescriptorAndActorOverride. Used by the "
        "special-camera normalization path to remap descriptors such as "
        "`kCameraContextualLook`.",
    ),
    0x1416A3800: (
        "DestroyCameraControlBaseAndRestoreCoreVtable",
        "Shared camera-control cleanup path. Temporarily installs an internal "
        "camera-control vtable, asserts if a live pair remains in +0xD0 when "
        "the global bypass is clear, then restores the core CameraControl "
        "vtable at 0x14297E9B0.",
    ),
    0x1416A3920: (
        "CacheCameraControlProcessTokenIdIfMissing",
        "Shared camera-control finalize/init helper. If +0xD8 is zero, queries "
        "the process/control token through virtual +0x28, appends a dash plus "
        "the current global index, interns the token through 0x1416A2CE0 / "
        "0x1417AA820, and stores the resulting id at +0xD8.",
    ),
    0x1416A3C80: (
        "RelocateCameraControlEmbeddedPointersAcrossAddressRange",
        "Shared relocation helper for CameraControl-derived objects. Slides the "
        "embedded pointers at +0xB0, +0xC0, and the nested payload behind "
        "+0xC0 when they fall inside the caller-provided address range, then "
        "marks byte +0x3C bit 0x80.",
    ),
    0x141C13A80: (
        "InitCameraControlAnimatedPhotographModeSubclass",
        "CameraControlAnimatedPhotographMode-flavored init wrapper. Copies the "
        "config scalar at +0x80 into +0x398, then tail-runs "
        "InitCameraControlStickBaseForScreenAndConfig.",
    ),
    0x141F523F0: (
        "DestroyCameraControlStickAndFree320",
        "Freeing destructor for CameraControlStick. Restores the "
        "CameraControlStick vtable, clears screen index +0x124, runs shared "
        "camera-control cleanup through 0x1416A3800, and frees 0x320 bytes "
        "when the low destructor flag is set.",
    ),
    0x141F521B0: (
        "ConstructCameraControlStickBaseAndAssignUniqueScreenId",
        "Primary in-place constructor for the 0x320 CameraControlStick base. "
        "Installs the CameraControlStick vtable at 0x142BB1D98, seeds the "
        "descriptor/follow bands through +0x1A8..+0x314, and assigns a unique "
        "screen-local control id at +0x124 via the global counter pair "
        "0x146DB1628/0x146DB1630.",
    ),
    0x141F52660: (
        "InitCameraControlStickBaseForScreenAndConfig",
        "Primary CameraControlStick init path. Copies screen index from "
        "config+0xA0 into +0x128, seeds default descriptor `kCameraNone` into "
        "+0x1A8, initializes the route/follow bands around "
        "+0x1D0..+0x240/+0x300..+0x314 from per-screen globals, and finalizes "
        "the token id through CacheCameraControlProcessTokenIdIfMissing.",
    ),
    0x141F6C1A0: (
        "InitCameraControlFixedLookAroundSubclass",
        "CameraControlFixedLookAround-flavored init wrapper. Copies the config "
        "scalar at +0x80 into +0x368, then tail-runs "
        "InitCameraControlStickBaseForScreenAndConfig.",
    ),
    0x140772E20: (
        "ConstructLargeCameraControlStrafeBase1FA0",
        "Shared in-place constructor for a 0x1FA0 large camera-control family "
        "whose installed vfptr 0x14298E820 resolves by RTTI to "
        "CameraControlStrafe. Calls ConstructCameraControlStickBaseAndAssignUniqueScreenId, "
        "installs 0x14298E820, seeds extended state across +0x330..+0x5A0 and "
        "+0x1CA0..+0x1EF0, and initializes the embedded band at +0x7C0 via "
        "0x140773180.",
    ),
    0x141C04CE0: (
        "ApplyCameraTargetStateFromDescriptorAndActorOverride",
        "Finalizes a camera target-state object from an incoming descriptor plus "
        "an optional resolved actor override. Copies transform/scalar payload, "
        "refreshes the retained (ptr,id) pair, and resolves a downstream runtime "
        "object without an obvious static Player-only class check.",
    ),
    0x141C075DC: (
        "CommitResolvedRouteOverrideIntoAttachedTargetPair_Internal",
        "Internal writeback block after ResolveScreenRouteTargetAndCommitDescriptor. "
        "Stores the returned (ptr,id) into route+0x2A8/+0x2B0 and sets the "
        "per-screen activity byte at 0x14383DE48[route+0x124].",
    ),
    0x141C051F0: (
        "RefreshAllScreenRouteEntitiesFromStartupTable",
        "Iterates the two-entry screen-route table at 0x14697AD50, resolves "
        "each live route entity, notifies the global observer at 0x1438929B8 "
        "through virtual +0x8 when present, then calls route-entity virtual "
        "+0x48 on each live entry.",
    ),
    0x141C06420: (
        "ResolveScreenRouteEntityByIndex",
        "Takes a screen/route index 0..1, loads the corresponding (ptr,id) "
        "pair from the two-entry table at 0x14697AD50, validates the live "
        "entity, marks byte +0x72, and returns the resolved route entity. "
        "This is a screen-route resolver, not a raw CameraManager getter.",
    ),
    0x141C0D4B0: (
        "SelectPreferredCameraChildFromRouteEntity",
        "Scans a route entity's child list at +0x100, validates live children, "
        "and returns the preferred camera child while filtering on child mode "
        "stored at +0x1B0. The caller-provided byte flag relaxes the default "
        "mode filtering.",
    ),
    0x141C0DFD0: (
        "ResolveScreenRouteTargetAndCommitDescriptor",
        "Builds and commits a camera target descriptor into a screen-route "
        "owner. Uses the lock at route+0x2C0, resolves the actor through "
        "(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124 screen_index), "
        "writes through route+0x2F0 via "
        "ApplyCameraTargetStateFromDescriptorAndActorOverride, and then "
        "notifies an external observer. The wide direct-call fanout shows "
        "this is a generic gameplay camera-route worker, not just a manual "
        "camera debug stub.",
    ),
    0x14073E330: (
        "PushCameraDescriptorToExplicitScreenRoute",
        "Generic per-screen route push. Takes an explicit screen index in ecx, "
        "a descriptor pointer in rdx, a follow-state block in r9, resolves "
        "the route through ResolveScreenRouteEntityByIndex, and forwards the "
        "pair into ResolveScreenRouteTargetAndCommitDescriptor. This is not "
        "screen-0-only; callers can feed screen 0 or 1.",
    ),
    0x1413CB6F0: (
        "RefreshContextualLookDescriptorForBothScreenRoutes",
        "Two-screen loop inside a higher camera-control owner. Iterates ebx "
        "from 0 to 1, resolves each route, requires the preferred child token "
        "at child+0x1A8 to match the `kCameraContextualLook` sentinel "
        "(0x1430535E0), then pushes a zeroed follow descriptor through "
        "PushCameraDescriptorToExplicitScreenRoute for both screens.",
    ),
    0x141C0E590: (
        "ApplyDescriptorToActiveRouteChildByMode",
        "Selects the first active route child in route+0x520 whose byte +0x78 "
        "is set and whose mode id at +0x18 matches edx, then stages that "
        "child through 0x141C0E500 and forwards the supplied descriptor into "
        "0x141C0ECC0. This is a real mode-based route-child updater, not just "
        "a descriptor cache poke.",
    ),
    0x141C0ECC0: (
        "RecomputeRouteStateFromDescriptorAndSelectedChild",
        "Heavy route-state recompute worker. Locks route+0x2C0, gathers the "
        "preferred child through 0x141C0D4B0 and 0x141C0E9F0, compares that "
        "against route+0x2F0 / route+0x300 override state, and rebuilds the "
        "effective route follow/descriptor payload. This sits downstream of "
        "screen-1-capable descriptor pushes and is the stronger candidate for "
        "stable second-view state than 0x141C0DFD0 alone.",
    ),
    0x140238500: (
        "SwitchAnimatedCameraModeForScreenRouteOwner",
        "Mounted per-screen camera-mode owner worker rooted at player+0x36C0. "
        "Resolves the screen route from owner+0x84, compares the requested "
        "mode id against owner+0x88, dispatches a virtual +0x10 mode-change "
        "callback, and then builds an apReference-extra keyed descriptor/state "
        "record around the current animation contribution id. This sits above "
        "the route-child updater and is part of the animated gameplay camera "
        "stack, not a generic route helper.",
    ),
    0x140238800: (
        "UpdatePlayerAnimatedCameraControlOwnerAndRouteState",
        "Heavy player-mounted animated-camera worker for the owner at "
        "player+0x36C0. Uses the current/secondary actor pairs at +0x70/+0x78 "
        "and +0x60/+0x68, strings 'player-animated', "
        "'animated-camera-control', 'CameraAnimationDirector', and "
        "'apReference-extra', then feeds the resulting descriptor into "
        "0x141C0E590. It also directly consumes and clears the transient "
        "primary pair at +0x60/+0x68 by copying a zeroed inline pair back "
        "into the director after the route/descriptor update. The normal and "
        "fallback paths both end up rebuilding route-child state and "
        "materializing gameplay camera objects under 0x143FE1088.",
    ),
    0x140237E60: (
        "RebuildAnimatedCameraDirectorFallbackDescriptorAndClearPendingPair",
        "Fallback/reset helper for PlayerCameraAnimationDirector. Resolves the "
        "screen route from owner+0x84, refreshes the hidden descriptor track "
        "through 0x1416860A0, marks owner+0x8C active, builds a "
        "'player-animated' fallback descriptor, and feeds it toward the route "
        "camera stack. This sits on the same reset branches as "
        "UpdatePlayerAnimatedCameraControlOwnerAndRouteState and is part of "
        "the consume-and-clear lifecycle for the transient primary pair.",
    ),
    0x140C30290: (
        "ConstructPlayerCameraAnimationDirectorSubobject",
        "In-place constructor for the PlayerCameraAnimationDirector object "
        "stored at player+0x36C0. RTTI on vtable 0x142A04C38 resolves to "
        "PlayerCameraAnimationDirector. Zeroes the primary pair "
        "+0x60/+0x68, seeds the secondary pair +0x70/+0x78 from incoming "
        "player-source rdx->+0xA8/+0xBC when that Player object is present, then "
        "initializes the mode fields +0x80/+0x84/+0x88 and the embedded "
        "contribution band at +0x90..+0xD8. This makes the ctor itself the "
        "first concrete writer of the stable secondary pair family.",
    ),
    0x140237760: (
        "DirectorHasPendingActorPairOrResolvedContribution",
        "Predicate over PlayerCameraAnimationDirector. Returns true when the "
        "pending actor pair at +0x60/+0x68 resolves to a live actor, or when "
        "the current contribution id at +0x80 is not 0xFF. Used directly by "
        "camera descriptor push paths before they fall back to other player "
        "state sources.",
    ),
    0x140C303E0: (
        "AdvanceDirectorContributionRingBaseIfCurrentEntryWithinBounds",
        "Small PlayerCameraAnimationDirector helper that only adjusts the "
        "contribution-ring base pointer at +0xD8 when the current entry falls "
        "within a supplied [r8,r9) window. This does not touch either actor "
        "pair family and is part of the contribution bookkeeping surface, not "
        "a pending-pair seeder.",
    ),
    0x140C349D0: (
        "ResetDirectorContributionStateUsingPrimaryAndSecondaryPairs",
        "PlayerCameraAnimationDirector method that validates both pair "
        "families. If the primary pair at +0x60/+0x68 resolves, increments "
        "the contribution epoch at +0xE4 and may force +0x80 back to 0xFF. "
        "Then clears the contribution ring state at +0xD0/+0xE0, revalidates "
        "the secondary pair at +0x70/+0x78, marks that live actor through "
        "+0x72, and refreshes the contribution timestamp at +0xF0.",
    ),
    0x140239100: (
        "ValidateInlineActorPairAtOffset0",
        "Small inline actor-pair validator used on PlayerCameraAnimationDirector "
        "subfields such as +0x60. Accepts (ptr,id) only when the live actor at "
        "ptr->+8 still matches the stored id at +8.",
    ),
    0x140C34AC0: (
        "AdvancePlayerCameraAnimationDirectorContributionState",
        "Contribution-ring worker for PlayerCameraAnimationDirector. Validates "
        "the secondary actor pair at +0x70/+0x78, marks the resolved actor "
        "live through byte +0x72, compares contribution age against +0xF0, "
        "and updates the contribution queue/state fields at "
        "+0xD0/+0xD8/+0xE0/+0xE4. It does not seed either actor pair family, "
        "so +0x60/+0x68 and +0x70/+0x78 must be established earlier.",
    ),
    0x140C35CF0: (
        "NoopPlayerCameraAnimationDirectorVirtualSlot38",
        "No-op PlayerCameraAnimationDirector method that immediately returns. "
        "This confirms the vtable slot at +0x38 is not part of pending-pair "
        "materialization.",
    ),
    0x141E0ED10: (
        "SetLocomotionControllerModeAndActiveStateLabel",
        "Mode-switch helper over the locomotion/controller object rooted at "
        "owner+0x15B8. Compares incoming mode against controller+0x34, maps "
        "the mode to labels such as None / NavPoly / NavLedge, checks whether "
        "the incoming owner object is currently active through its bitset, "
        "publishes that active-state change through 0x141DE05D0('SetActive', "
        "...), and finally stores the new mode back into +0x34.",
    ),
    0x141E0EDF0: (
        "ApplyLocomotionControllerSnapshotAndResolveModeAuxState",
        "Consumes a 0x30-byte snapshot block for the owner+0x15B8 controller. "
        "Copies incoming transform/state into +0x40/+0x50/+0x60, clears "
        "+0x70/+0x78, and then runs mode-specific follow-up helpers to "
        "materialize auxiliary state under +0x70 when the current mode "
        "requires it. This is a concrete snapshot/apply seam above the old "
        "MoveTo path, not just nav-goal setup.",
    ),
    0x141E18A70: (
        "ApplySharedSourceSnapshotIntoOwner15B8AndPublishDescriptor",
        "Concrete shared block above the owner+0x15B8 controller. Pulls a "
        "snapshot from source+0x230/+0x240/+0x250, resolves the controller via "
        "(owner+0x1D0)+0x15B8, applies mode through 0x141E0ED10 and snapshot "
        "through 0x141E0EDF0, then publishes the result through 0x141E16CF0 "
        "and the 0x14189AF80/0x14189AF30 descriptor-storage pair.",
    ),
    0x141E18D00: (
        "PublishOwnerStateWithoutSharedSnapshotIntoDescriptorStore",
        "Sibling owner-side block next to 0x141E18A70. Does not pull the "
        "shared source snapshot from +0x230/+0x240/+0x250. Instead it refreshes "
        "owner-local mode/state around +0x258/+0x2F8/+0x300, optionally calls "
        "0x141E16CF0, runs 0x141E14890 over owner+0x260, and then publishes a "
        "different descriptor kind through the same 0x14189AF80/0x14189AF30 "
        "storage pair.",
    ),
    0x141E1D110: (
        "AllocateSmallOwnerDescriptorNode_TypeD110",
        "Allocates a 0x20-byte helper object, stores owner back-pointer at "
        "+0x10, clears +0x18, and installs vtable 0x142B898A0 "
        "(constructor entry 0x141E1D190). Used as one of the descriptor-node "
        "types later published through 0x14189AF80.",
    ),
    0x141E1D5C0: (
        "AllocateSmallOwnerDescriptorNode_TypeD5C0",
        "Sibling 0x20-byte helper-object allocator with owner back-pointer at "
        "+0x10 and vtable 0x142B89850 (constructor entry 0x141E1D640). Used by "
        "the same publication/storage family as 0x141E1D110.",
    ),
    0x141E1F890: (
        "AllocateSmallOwnerDescriptorNode_TypeF890",
        "Another sibling 0x20-byte helper-object allocator with owner back-"
        "pointer at +0x10 and vtable 0x142B897D0 (constructor entry "
        "0x141E1F910). This is the concrete descriptor-node type passed from "
        "0x141E18A70 into 0x14189AF80.",
    ),
    0x141E17240: (
        "ResetOwnerSharedSnapshotSourceState",
        "Reset/helper block for the same owner family as 0x141E18A70. Clears "
        "the owner state bands at +0x220..+0x250, resets mode-dependent data, "
        "re-seeds controller-side state through owner+0x1D0/+0x15B8, and then "
        "continues into owner+0x15C8 follow-up work. This is the null-source "
        "fallback used before 0x141E18A00 publishes the small descriptor node.",
    ),
    0x141E181D0: (
        "CheckFirstTapWindowForActionPackKindAC710201",
        "Special gate for action-pack objects with kind tag 0xAC710201. Uses "
        "owner-side time windows at +0x228/+0x248/+0x250, compares them "
        "against the current controller time source under owner+0x1D0+0xE0, "
        "and blocks the rest of the install path when the first-tap window is "
        "still active.",
    ),
    0x141E15A00: (
        "ProcessCommandStopAndStandThroughCurrentActionPack",
        "Owner-side stop-and-stand command path. Builds a snapshot through "
        "0x141E179D0, publishes it through 0x14189AF80/0x14189AF30, then "
        "releases the current action-pack selection through 0x141E18450 and "
        "clears owner+0x2F8 back to -1. String anchors identify this family as "
        "ProcessCommand_StopAndStand.",
    ),
    0x141E15C80: (
        "UpdateStateMovingAndSeedBackupActionPack258",
        "Heavy owner-side moving-state updater. Validates current moving "
        "inputs, resolves the moving action-pack source from owner+0xF0, "
        "stores its id into owner+0x258, copies the shared snapshot into "
        "owner+0x260..+0x280, and updates nearby moving-state bands such as "
        "+0x2E0, +0x440, and +0xEB8..+0xECC. String anchors identify blocked "
        "and disappeared MoveToActionPack / MoveAlongSpline failure cases.",
    ),
    0x141E158B0: (
        "ExitCurrentOwner80CommandStateAndReleaseActionPack",
        "Owner-side exit/cleanup path for the command state described by "
        "owner+0x80. Logs the current owner+0x208 phase, refreshes owner "
        "publication state, inspects the current action-pack object through the "
        "controller, and then routes command-specific cleanup such as stop/"
        "stand and release through the shared action-pack helpers.",
    ),
    0x141E16920: (
        "PublishExitOfReservedActionPackStateAndReleaseCurrentSelection",
        "Owner-side exit path for the reserved-action-pack state "
        "(owner+0x300 == 2). Publishes either the active descriptor node or a "
        "fallback one, refreshes owner+0x238 from controller time, releases the "
        "current action-pack selection through 0x141E18450, and advances "
        "owner+0x208 into the post-exit phase.",
    ),
    0x141E18350: (
        "ValidateCurrentActionPackSourceAndSetOwner304Kind",
        "Resolves the current packed handle at owner+0x2F8, validates the live "
        "action-pack source through source virtual +0xB0 against owner+0x1D0, "
        "stores source kind source+0x60 into owner+0x304, and then runs the "
        "special kind-0xAC710201 gate plus the reserve/prepare path before "
        "marking owner+0x300 = 1 on success.",
    ),
    0x141E18450: (
        "ReleaseCurrentActionPackAndPendingHandleSlots",
        "Release/reset helper for the current action-pack selection. Releases "
        "the live source resolved from owner+0x2F8 through source virtual "
        "+0xD0, clears the five pending handle slots at owner+0x2E4..+0x2F4, "
        "and resets owner+0x300 back to 0.",
    ),
    0x141E18700: (
        "EnterReservedActionPackAndPublishSharedSnapshot",
        "Takes a validated action-pack source object, records owner+0x300 = 2, "
        "copies action-pack data and transforms, applies the shared snapshot "
        "from source+0x230/+0x240/+0x250 into owner+0x260..+0x280, and then "
        "publishes the result through the same owner-side descriptor/storage "
        "tail used by 0x141E18A70.",
    ),
    0x141E189C0: (
        "RefreshOwner238FromControllerE0TimeSource",
        "Small helper that clears the temporary bit under owner+0x1D0+0x1510 "
        "when needed and refreshes owner+0x238 from the controller time source "
        "at owner+0x1D0+0xE0+0x10.",
    ),
    0x141E14890: (
        "StoreMovementMode444AndPreparedSnapshot290",
        "Shared owner-side setter for the prepared movement snapshot. Accepts "
        "a prepared descriptor in r8 and a mode id in edx, validates actor "
        "context through owner+0x1D0 and 0x14386FD00, stores the mode in "
        "owner+0x444, copies the descriptor snapshot into "
        "owner+0x290/+0x2A0/+0x2B0, derives owner+0x2D0 from the current "
        "actor basis, and then runs the heavy follow-up path used by many "
        "ordinary gameplay callers. This is a central ingress for live "
        "movement-state updates, but it still does not write owner+0x80 "
        "directly.",
    ),
    0x141E15760: (
        "DispatchOwner80CommandStateAndPublishOwner120Mirror",
        "Small owner-side dispatcher over owner+0x80. Sets owner+0x208 = 2, "
        "routes mode 1 into 0x141E158B0 and modes 2..4 into 0x141E15C80, then "
        "copies the resolved descriptor fields from owner+0x80 into "
        "owner+0x120..+0x1B0, refreshes owner+0x228/+0x218 from controller "
        "time, and tail-calls 0x141DE0690.",
    ),
    0x141E1A040: (
        "UpdateOwner80StateDescriptorAndDispatchMovementMode",
        "Owner-side dispatcher over the state descriptor stored at owner+0x80. "
        "Uses the current mode value there to branch into moving-state update "
        "logic such as 0x141E15C80, then copies the resolved descriptor fields "
        "from owner+0x80 into owner+0x120..+0x1B0, refreshes owner time fields "
        "+0x228/+0x218, and marks publication flags.",
    ),
    0x141E1A000: (
        "UpdateOwnerMovementStateMachineAndPublishCurrentDescriptor",
        "Broader owner-side movement-state update loop above 0x141E1BA20 and "
        "0x141E1A040. First runs the large queued-entry chooser "
        "0x141E1BA20, then dispatches owner+0x80 through 0x141E15C80 or "
        "0x141E158B0 when needed, mirrors the resolved descriptor into "
        "owner+0x120..+0x1B0, refreshes owner+0x228/+0x218, advances state "
        "flags under +0x468/+0x472, and handles several follow-up cases such "
        "as disappeared traversal action-packs and reserved-action-pack "
        "cleanup. This currently looks like the strongest ordinary-Chloe "
        "movement-state loop above the prepared setter 0x141E14890.",
    ),
    0x141E19F90: (
        "NavStateMachineUpdateBool",
        "Real function start for NavStateMachine::Update(bool). Owns the broad "
        "movement-state update loop whose internal label at 0x141E1A000 runs "
        "0x141E1BA20, dispatches owner+0x80, and then continues into "
        "0x141E1AA20 and other moving/action-pack follow-up work.",
    ),
    0x141E1AA20: (
        "ProcessPathFindResultsAndSwitchPreparedMovementState",
        "Owner-side follow-up inside the broader update loop 0x141E1A000. "
        "Works from the current prepared mode at owner+0x444 together with the "
        "current, reserved, and backup action-pack handles at +0x2F8/+0xE90/"
        "+0x258, logs ProcessPathFindResults strings, releases invalid or "
        "disappeared action-packs when needed, and feeds 0x141E14890 with "
        "replacement modes 0, 1, 2, or 4 through compact descriptors built by "
        "0x141E06C00.",
    ),
    0x141E1BA20: (
        "BuildMovementModeFromQueuedEntriesAndFeedPreparedSetter",
        "Large owner-side chooser over queued entries and current action-pack "
        "state. Unpacks candidate entries through 0x141E06C00, compares them "
        "against owner+0x290 and current action-pack state, and then feeds "
        "0x141E14890 with concrete mode ids 0, 1, 2, 3, or 4 depending on the "
        "accepted path. This is currently the strongest ordinary-Chloe path "
        "above the prepared movement snapshot setter.",
    ),
    0x141E1C820: (
        "ResumeNavigationAndBuildActionPackHandoffSnapshot",
        "Owner-side resume path in the nav state machine. Scans resumable "
        "navigation entries, builds a handoff snapshot through 0x141E179D0, "
        "and then publishes the result through the same descriptor/storage tail. "
        "String anchors identify this family as ResumeMoving / Resume.",
    ),
    0x141E18A00: (
        "ResolveOwner2F8SourceAndApplySharedSnapshotIfPresent",
        "Owner-side dispatcher that resolves the current source handle stored at "
        "owner+0x2F8 via 0x141DF35F0. If the source is absent, it falls back to "
        "0x141E17240 and publishes a small descriptor node of type "
        "0x141E1D110. If the source is present, it consumes source fields "
        "+0x170, +0x230, +0x240, +0x250, updates owner fields such as +0x2FC, "
        "+0x468, and then enters the shared snapshot/apply block at "
        "0x141E18A70 using descriptor node type 0x141E1F890. The source now "
        "reads as a live ActionPack-like object, not an anonymous snapshot "
        "blob.",
    ),
    0x141E18120: (
        "AbandonInterruptedStateAndRebuildMode4Descriptor",
        "Cleanup path identified by the string 'Abandoning interrupted state "
        "%s'. Resets the owner+0x1D8 helper back to the small node type "
        "0x141E1D110, optionally releases the current action-pack selection, "
        "rebuilds a fresh descriptor from owner+0x1D0 through virtual +0x1C8, "
        "feeds mode 4 into 0x141E14890, and then republishes through "
        "0x141DE0690.",
    ),
    0x141E1E4F0: (
        "UpdateStateMovingAndSwitchPreparedModes",
        "Ordinary moving-state update family identified by UpdateStateMoving "
        "strings. Uses the current prepared mode at owner+0x444 together with "
        "the moving action-pack state, can feed mode 2, mode 3, or mode 0 "
        "back into 0x141E14890, checks arrival and waypoint changes, and then "
        "continues through success/failure handling for the moving command. "
        "This currently looks like one of the strongest ordinary movement "
        "families above the prepared movement setter.",
    ),
    0x141E1F300: (
        "PostRootLocatorUpdateAndSwitchPreparedModes",
        "Post-root / interrupted-state update family identified by "
        "PostRootLocatorUpdate strings. Builds temporary descriptors from "
        "owner+0x360/+0x370/+0x380, feeds modes 2, 1, or 0 into "
        "0x141E14890, and then drives publication plus post-root follow-up "
        "checks. Strong live path, but currently looks narrower than the main "
        "moving-state updater.",
    ),
    0x141E20280: (
        "UpdateStatePlayingEnterAnimationAndFeedMode4",
        "Action-pack enter-animation state update identified by "
        "UpdateStatePlayingEnterAnimation strings. If a live current "
        "action-pack exists it feeds mode 4 into 0x141E14890 and publishes the "
        "result; otherwise it falls into failure/exit handling. Useful for "
        "classification, but not the strongest generic movement ingress.",
    ),
    0x141261140: (
        "UpdatePlayerCameraAnimationDirectorFromAnimationContributionFlags",
        "Heavy animation-driven update path above player+0x36C0. Writes "
        "director+0x88 and optional contribution age at +0xF0, gates on bits "
        "under r13+0x50 plus local animation-token filters, and is currently "
        "the only confirmed direct caller of "
        "AdvancePlayerCameraAnimationDirectorContributionState.",
    ),
    0x141C07420: (
        "SetScreenRouteAttachLaneState",
        "Small helper that stores route+0x128 = bool and flips the paired "
        "lane-state bands under 0x143FE1088 + 0xDB0 using 0x141837530.",
    ),
    0x141C074E0: (
        "AttachScreenRouteWithManualFallbackDescriptor",
        "Attach side for the manual-camera path. Sets route+0x128 = 1, flips "
        "the paired 0x143FE1088 lane-state bands, and if route+0x2A8/+0x2B0 "
        "does not already resolve to a live actor, builds fallback descriptor "
        "0x14373A820 and pushes it through "
        "ResolveScreenRouteTargetAndCommitDescriptor.",
    ),
    0x141C07640: (
        "DetachScreenRouteManualBinding",
        "Detach side for the manual-camera path. Clears camera+0x128, flips the "
        "paired 0x143FE1088 lane-state bands, optionally calls retained target "
        "virtual +0x208(...,2), and clears per-screen activity bytes.",
    ),
    0x141C07E20: (
        "WriteManualOrDebugCameraFollowStateIntoResolvedActor",
        "Manual/debug-camera follow writer. Resolves the actor cached under "
        "camera+0x100, validates it through 0x141F53500, then writes "
        "transform/follow state into actor+0x3A0/+0x3B0 and actor+0x1D0..+0x21C. "
        "Observed callers include the manual-camera cluster plus debug-flavored "
        "`teleportplayer` / top-camera selection paths, so this is useful but "
        "not yet the final gameplay camera seam.",
    ),
    0x140EDE5E0: (
        "SwitchPrimaryCameraRetainedBindingByRetainedSlotState",
        "Gameplay-facing manager-0 retained-camera switch. If retained state is "
        "absent, calls 0x1410F3350 then detaches the primary screen-route "
        "entity through "
        "0x141C07640; if retained state is present, calls "
        "RefreshPrimaryRetainedPlayerCamera (0x1410F3DD0) then attaches through "
        "0x141C074E0. Both branches finish by resolving slot 0 via "
        "0x1410F0F60.",
    ),
    0x14015BB60: (
        "CopyLargeCameraStateStruct_0x3F8",
        "Bulk copier for a large camera-state struct spanning at least "
        "0x3F8 bytes. Includes the byte range around +0x150 and the "
        "target-state block around +0x2F0.",
    ),
    0x140882F00: (
        "GetCameraStateFlag153",
        "Tiny accessor that returns byte +0x153 from a camera-state object.",
    ),
    0x140882F10: (
        "BuildRuntimeCameraStateWithRetainedTarget",
        "Builds or refreshes a runtime camera-state object that includes "
        "spatial state, embedded retained target pairs, and flag byte +0x153.",
    ),
    0x140883530: (
        "ActivateEmbeddedRetainedTargetAndUpdateCameraState",
        "Validates the embedded retained pair at +0x80/+0x88, marks the live "
        "entity active if found, then continues into the main camera-state "
        "update path at 0x140883570.",
    ),
    0x140883570: (
        "UpdateRuntimeCameraState_GatedByPrimaryCameraFlag153",
        "Main runtime camera-state update path. Immediately queries "
        "the primary screen-route entity and gates behavior on route+0x153 "
        "before continuing normal simulation over the local camera-state "
        "object; a non-zero flag takes the early-return epilogue rather than "
        "a separate retained-specific worker path.",
    ),
    0x14088AC70: (
        "SyncCameraStateOwnerCompanionRetainedFlags",
        "Validates the retained/live pair cached at owner+0x5F0/+0x5F8, "
        "compares the companion objects at +0x198, propagates retained-camera "
        "slot activity through 0x141754C10, and then dispatches follow-up "
        "companion updates through 0x141754340 or 0x141754820.",
    ),
    0x14088AD40: (
        "UpdateCompanionCameraRigFromTrackedEntity",
        "Builds a companion-camera rig from the tracked entity at owner+0x1A8/"
        "+0x1B0, writes transforms into owner+0x108 and owner+0x420, queries "
        "four child nodes through the embedded camera-state object at +0x4E0, "
        "and marks the owner active via byte +0x440.",
    ),
    0x141BB8C30: (
        "RelocateCompanionCameraRigOwner",
        "Large relocation helper for the companion camera-rig owner. Rebases "
        "tracked pointers at +0x188/+0x198/+0x1A0, a hashed node list behind "
        "+0x1A8/+0x288, multi-pointer clusters at +0x3F8/+0x400 and +0x218, "
        "and dispatches relocation into nested child objects.",
    ),
    0x141BB95C0: (
        "DestroyCompanionCameraRigOwner",
        "Destructor path for the companion camera-rig owner. Tears down nested "
        "runtime objects at +0x218/+0x238, notifies external systems using the "
        "cached retained pair at +0xA8/+0xBC when byte +0x16D is set, and "
        "continues with deeper cleanup of companion and child-node state.",
    ),
    0x141BBDD30: (
        "PushCompanionCameraRigPoseToChildNodes",
        "Pushes the owner's cached pose/state vectors at +0x100/+0x120/+0x140 "
        "into child runtime nodes obtained from the object at owner+0x1A0. "
        "Iterates the child-node pointer table at +0x4F8/+0x500 and seeds each "
        "eligible node's transform block when bit 0x400000000000 is set.",
    ),
    0x141C07FC0: (
        "SeedCameraStateFlag153FromGlobalToggle",
        "Writes camera-state byte +0x153 from the shared global toggle at "
        "0x143682FA1.",
    ),
    0x141837530: (
        "JoypadState_UpdateEnabledBinding",
        "Updates or registers the Joypad-enabled byte at JoypadState+0xBE0. "
        "The nonzero mode hashes the literal Joypad and binds that byte through "
        "the property system; it does not write a camera-state blob.",
    ),
    0x141754C10: (
        "SyncObjectBit02ByActiveRetainedCameraSlots",
        "Reads the two-byte global retained-camera slot gate at "
        "0x14383DE48[0..1] and sets or clears bit 0x2 in the target object "
        "through 0x141753DB0 depending on whether either camera slot is active.",
    ),
    0x141754460: (
        "ClearFlockingChildByte1A0ByModeId",
        "Finds the flocking/player entry with matching mode id at +0x18 inside "
        "the companion object and clears byte +0x1A0 on each linked child node.",
    ),
    0x141754820: (
        "ClearFlockingFlag0x40AndCompanionBit0",
        "Clears bit 0x1 on the companion object and clears child-node flag "
        "0x40 in each linked flocking/player entry.",
    ),
    0x141754940: (
        "SetFlockingChildFlag0x80ByModeId",
        "Finds the flocking/player entry with matching mode id and ORs flag "
        "0x80 into each linked child node's +0x18 flags.",
    ),
    0x141754EF0: (
        "DrawControl_SetChildInstanceFlagsByMask",
        "Iterates DrawControl+0x8/+0x14 meshes (stride 0x250) and ORs the "
        "requested mask into every child render instance +0x18 flags. Mask "
        "0x40 excludes the target renderable from the main view.",
    ),
    0x141754FF0: (
        "DrawControl_ClearChildInstanceFlagsByMask",
        "Exact inverse of 0x141754EF0: iterates every DrawControl child render "
        "instance and clears the requested mask from instance+0x18. Used with "
        "mask 0x40 by set-instance-flag-draw-in-main-view(true).",
    ),
    0x1403342C0: (
        "Script_SetInstanceFlagDrawInMainView",
        "Handler registered for set-instance-flag-draw-in-main-view. Resolves "
        "an arbitrary script target object and its DrawControl at object+0x198; "
        "true clears child instance flag 0x40, false sets it. RDI retains the "
        "target object at the false-branch callsite 0x1403343D3.",
    ),
    0x141ECEFE0: (
        "NdAttachableObject_Init",
        "NdAttachableObject initialization path. Owns an independent DrawControl "
        "at object+0x198 and establishes attachment parent state around "
        "+0x4E0/+0x4E8; used by hair/look items and weapon renderables.",
    ),
    0x141BBACC0: (
        "BindUpperWrapperIntoPlayerCompanionBehaviorAndDrawControlState",
        "Initializes behavior bindings between the rig owner and its companion "
        "flocking/player object at +0x198. Reads mode-dependent booleans from "
        "source descriptors, updates companion-object flags, and dispatches "
        "child-node state changes through 0x141754460/0x141754940. In the real "
        "Player path it is fed from 0x1410CF370 with the extended wrapper "
        "packet at 0x1417C35A0 / vtable 0x1428D5D60 as the third argument and "
        "a draw-control context as the fourth. Direct disassembly now also "
        "shows selector-gating through wrapper+0x10->+0x30 and owner "
        "capability checks before it mutates player+0x198 and builds late "
        "owner arrays, which makes this a downstream upper behavior/draw-"
        "control bind stage rather than the source of Player ownership or the "
        "authored id pair later used by 0x140CC1450.",
    ),
    0x141BB8BC0: (
        "GetCurrentUpperPlayerStateIdFromLiveActorAndWriteOutDword",
        "Real Player vtable slot +0x300. If player+0x190 is non-zero, resolves "
        "the live actor through 0x1417D1170, reads a compact state id from "
        "live_actor->+0x18->+0x10, and writes it to the caller-provided dword. "
        "This is a compact current-state getter used by 0x140CC1450 before the "
        "late `proto`-promotion branch; it is not itself the upper apply seam.",
    ),
    0x1410F1840: (
        "ResolveRetainedPlayerModeFromPair",
        "Seeds an output mode id from the global default at 0x143EEBE7C, then "
        "optionally overrides it from the retained pair's runtime object at "
        "+0x48 via the shared value-reader callback 0x143682D30.",
    ),
    0x1410F18A0: (
        "ResetRetainedPlayerRuntimeGlobals",
        "Clears the retained-player runtime global block rooted at "
        "0x143EEC160/0x143EEC220..0x143EEC262 and seeds the default scalar at "
        "0x143EEC240.",
    ),
    0x140159C00: (
        "LookupSelectorMetadataInRuntimeQueryTable",
        "Binary-searches the selector table at object+0x68 for the hashed "
        "runtime selector id in r8d and returns selector metadata through the "
        "out parameters. Used immediately before retained-entity virtual query "
        "dispatch.",
    ),
    0x141D21C80: (
        "DispatchRetainedEntityRuntimeCommand_0x538",
        "Validates the retained/runtime pair at object+0x60, resolves the live "
        "entity, builds a temporary descriptor through 0x1417C32A0 or "
        "0x1417C35A0 depending on the mode flag, and calls the entity-side "
        "virtual slot +0x538 with that descriptor. The extended 0x1417C35A0 "
        "wrapper therefore is not just shape-compatible with the upper Player "
        "external_state seam; it is already an accepted runtime-command "
        "packet on the generic entity-side interface. Returns a boolean-like "
        "success result.",
    ),
    0x141D21DE0: (
        "ResolveLiveEntityFromRetainedPair60",
        "Resolves the live entity referenced by the retained/runtime pair at "
        "object+0x60, validates entity+0xBC against the stored runtime id at "
        "pair+0x38, marks the entity active via byte +0x72, and returns the "
        "live entity pointer on success.",
    ),
    0x141D21E20: (
        "ResolveLiveEntityFromRetainedPair60_GatedByRegistryBit",
        "Extends ResolveLiveEntityFromRetainedPair60 with a second registry-bit "
        "gate through the entity-owned bitset at entity+0x30 and global index "
        "0x146920B54. Returns null when the live entity exists but is not "
        "enabled in that registry mask.",
    ),
    0x141D21F90: (
        "BuildRetainedEntityQueryRecord_0x30",
        "Builds a 0x30-byte query/result record from the retained/runtime pair "
        "stored at object+0x60. Resolves the active live entity through "
        "ResolveLiveEntityFromRetainedPair60_GatedByRegistryBit, queries the "
        "entity through virtual slot +0x530 using selector metadata from "
        "LookupSelectorMetadataInRuntimeQueryTable, and writes a compact output "
        "record with default scalar/vector fields plus an optional returned id pair.",
    ),
    0x141D231E0: (
        "BuildRetainedEntityParentToJointTransform",
        "Builds a fallback or entity-derived transform for the retained/runtime "
        "pair at object+0x60. Resolves an attachment-capable runtime object, "
        "looks up the selector metadata for 'ParentToJoint', queries the "
        "runtime object, and composes the returned transform with the owner's "
        "local transform when successful.",
    ),
    0x1417C4800: (
        "CheckSelectorHashEnabledInBitsetRegistry",
        "Checks whether the hashed selector id in edx is enabled in the target "
        "bitset registry at rcx. Uses a global hash-bucket table at "
        "0x14383F198 and then tests the corresponding bit in the owner bitset.",
    ),
    0x141DE55E0: (
        "DispatchNestedActionStateViaControlObject_A0",
        "After a guard through owner virtual +0x5E8, resolves the nested "
        "control object from owner+0x15C8 via virtual +0x58 and dispatches the "
        "requested action state in edx through nested virtual +0xA0.",
    ),
    0x141DE5630: (
        "ForwardToNestedControlObject_A8",
        "Resolves the nested control object from owner+0x15C8 via virtual +0x50 "
        "and tail-calls its virtual +0xA8 handler.",
    ),
    0x141DE5660: (
        "ForwardToNestedControlObject_B0",
        "Resolves the nested control object from owner+0x15C8 via virtual +0x50 "
        "and tail-calls its virtual +0xB0 handler when the nested object is present.",
    ),
    0x141DE5690: (
        "SetNestedInjuredLayerFlagByCompanionTable",
        "Hashes the selector string 'injured-layer', scans the companion table "
        "at owner+0x1A0 (+0x298 entries, count +0x2A4, stride 0x18), and writes "
        "a 0/1 byte for the matching entry.",
    ),
    0x141DE57F0: (
        "GetNestedInjuredLayerFlagByCompanionTable",
        "Hashes 'injured-layer', scans the same companion selector table at "
        "owner+0x1A0, and returns whether the matching entry's flag byte is non-zero.",
    ),
    0x141DE5980: (
        "ForwardToNestedControlObject_178",
        "Resolves owner+0x15C8 via virtual +0x20 and tail-calls the returned "
        "nested object's virtual +0x178 handler.",
    ),
    0x141DF0000: (
        "QueryControlBlendScalarFromTypeOrIndexedActionState",
        "Checks the control/type object at owner+0x15C0 against 0x143738130, "
        "gates through owner virtual +0xC20, and otherwise returns either an "
        "indexed scalar from owner+0x1A38 (index owner+0x19E0) or the fallback "
        "scalar at owner+0x1760.",
    ),
    0x141DF00A0: (
        "QueryNestedControlFloat_A8OrDefault",
        "Resolves owner+0x15C8 via virtual +0x78 and tail-calls the returned "
        "object's virtual +0xA8, falling back to the default float at "
        "0x142BE8FC4 when no nested object is available.",
    ),
    0x141DE59B0: (
        "StoreBlobIntoNestedControlObject_188",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards the caller blob "
        "pointer in rdx into the returned nested object's virtual +0x188, then "
        "returns the original blob pointer.",
    ),
    0x141DE59E0: (
        "GetControlTypeRuntimeId_15C0_14C",
        "Reads the control/type object at owner+0x15C0 and stores its dword "
        "field +0x14C into the caller-supplied output pointer.",
    ),
    0x141DE5A00: (
        "GetControlActionStateId_1748",
        "Returns the current dword action/state id cached at owner+0x1748.",
    ),
    0x141DE5A10: (
        "SetControlActionStateIdAndForward_168",
        "Stores edx into owner+0x1748, resolves owner+0x15C8 via virtual +0x28, "
        "and forwards the same state id into the returned nested object's "
        "virtual +0x168.",
    ),
    0x141DE5A40: (
        "ForwardStateIdToNestedControl_158",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards edx into the "
        "returned nested object's virtual +0x158.",
    ),
    0x141DE5A70: (
        "ForwardStateIdToNestedControl_160",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards edx into the "
        "returned nested object's virtual +0x160.",
    ),
    0x141DE5AA0: (
        "ForwardToNestedControlObject_E0",
        "Resolves owner+0x15C8 via virtual +0x20 and tail-calls the returned "
        "nested object's virtual +0xE0.",
    ),
    0x1406B3640: (
        "PatrolScriptSkill_BuildAndApplyExtendedControlRoutingPath",
        "Heavy sibling path in the Patrol@ScriptSkill router at 0x1406B3D70. "
        "Builds a large runtime descriptor stack, allocates/attaches "
        "intermediate objects, updates owner+0x2830 and owner+0x2010, clears "
        "bit 0x8 in owner+0x15AA on success, and writes a scalar into "
        "companion child field +0xD0.",
    ),
    0x1406B3940: (
        "PatrolScriptSkill_BuildAndApplyCompactControlRoutingPathByStateId",
        "Sibling path used by the same Patrol@ScriptSkill router when a "
        "low-cardinality state id in edx is available. Rebuilds local "
        "transforms/state from the owner, consults current locomotion payload "
        "fields around +0x24B0..+0x24C8, and feeds the result into the same "
        "post-routing owner updates.",
    ),
    0x1406B3D70: (
        "PatrolScriptSkill_RouteControlPathByProbeFlagAndStateId",
        "Entry router in the Patrol@ScriptSkill vtable block at 0x14297B260. "
        "Calls 0x1406B2390 to obtain a probe flag and scalar, dispatches either "
        "the extended path or the state-id path, then clears bit 0x80 in "
        "owner+0x1D57 and pushes the scalar into companion child field +0xD0. "
        "This is an AI patrol layer, not the first direct-control seam for a "
        "playable Nadine.",
    ),
    0x1406B2390: (
        "ProbeControlRoutingThresholdsAndSelectStateId",
        "Computes a distance/threshold probe from owner transform and the "
        "pending locomotion command block at owner+0x2470, writes a bool flag "
        "to the caller byte pointer, optionally writes a scalar to the caller "
        "float pointer, and returns a low-cardinality state id used by the "
        "0x1406B3D70 control router.",
    ),
    0x140462020: (
        "ExecuteAuthoredControlSelectionDescriptorModes",
        "Large consumer of the authored task-node/control-selection descriptor "
        "returned by 0x141765580. Branches on descriptor mode 0/1/2/3, builds "
        "selection masks, mutates the global byte table at 0x143D44880, caches "
        "chosen candidate ids, and eventually emits owner-local state near "
        "+0x500/+0x510.",
    ),
    0x140463A70: (
        "RefreshOwnerDecisionBitFromAuthoredDescriptor",
        "Small owner-side wrapper around 0x140462020. Refreshes owner+0x20/"
        "+0x28 from runtime timing pointers, clears owner+0x30 state bits, "
        "executes the authored descriptor consumer, and latches the returned "
        "decision bit back into owner+0x30 bit 0.",
    ),
    0x140463AC0: (
        "UpdateOwnerStateThroughAuthoredDescriptorAndRuntimeGates",
        "Large owner-side update wrapper around 0x140462020. Builds selector "
        "hash state, consults capability gates, conditionally resolves retained "
        "slot 0 and compares transforms, executes the authored descriptor path "
        "when owner+0x30 bit 0 is clear, then continues into mode/timer/object "
        "gates driven by owner+0x30 flags and runtime state.",
    ),
    0x1404641A0: (
        "ResolveTwoSelectorIdsAndPushOwnerRuntimeState",
        "Owner-family sibling in the same 0x14293E240 dispatch table. Resolves "
        "two selector ids through 0x140159C00 using runtime+0xD4 and authored "
        "hash blocks, then pushes the results into owner/runtime state via "
        "0x140461500.",
    ),
    0x1404643C0: (
        "UpdateOwnerThresholdGatedSelectorState",
        "Threshold-gated sibling in the same owner dispatch family. Samples "
        "runtime timing/state from runtime+0xA0, derives a scalar, resolves "
        "selector ids through 0x140159B40/0x140159C00, pushes state through "
        "0x140461500, and falls back to owner+0x40 state-blob notifications.",
    ),
    0x140464710: (
        "ResetOwnerRuntimeStateBlocksAndFlushPendingNode",
        "Owner-family reset/teardown sibling. Clears runtime+0x2830 and "
        "runtime+0x15E0 state blocks, emits a debug/state record if runtime+0x4038 "
        "is live, and flushes the pending node at owner+8->0x30 back into "
        "runtime+0x1F78.",
    ),
    0x140464930: (
        "BuildOwnerMaterializedStateObjectFromActorRuntime",
        "Materialize/build sibling in the same owner dispatch family. Queries "
        "the actor-like runtime object through runtime+0xA0 virtual +0xA10, "
        "builds a large state descriptor on the stack, feeds it through "
        "0x141C9ED50, materializes an intermediate object via 0x14048B270, and "
        "attaches the result back to the owner/runtime through 0x14048F260.",
    ),
    0x141C9ED50: (
        "DispatchOrResolveActorStateDescriptorIntoRuntimeSlot",
        "Consumes a structured actor-state descriptor and a target runtime slot. "
        "Either routes immediately through 0x1417F8910 when the descriptor type "
        "matches a small built-in table, or scans a live registry/roster, "
        "activates matching entities, and dispatches them back through the same "
        "0x1417F8910 path.",
    ),
    0x14048B270: (
        "ConstructMaterializedOwnerStateObject_0x4D0",
        "Ctor/builder for a 0x4D0 materialized owner-state object. Initializes "
        "the object, sets vtable 0x142941FB0, seeds the embedded subobject at "
        "+0xB0 via 0x14048AE80, and optionally copies a small 0x20-byte state "
        "header plus virtual payload from the source descriptor.",
    ),
    0x14048B200: (
        "AllocateAndConstructMaterializedOwnerStateObject_0x4D0",
        "Small allocation wrapper for the 0x4D0 materialized owner-state "
        "variant. Either forwards an existing descriptor into "
        "ConstructMaterializedOwnerStateObject_0x4D0 or allocates 0x4D0 bytes "
        "through the shared allocator and then runs the same constructor.",
    ),
    0x14048B3E0: (
        "DestroyMaterializedOwnerStateObject_0x4D0",
        "Freeing destructor for the 0x4D0 materialized owner-state variant. "
        "Tears down the embedded helper at +0xC0, releases the object-local "
        "state host, and optionally frees the 0x4D0 allocation when requested.",
    ),
    0x14048B450: (
        "TeardownMaterializedOwnerStateObject_0x4D0",
        "In-place teardown for the 0x4D0 materialized owner-state variant. "
        "Releases the embedded helper at +0xC0 and then tears down the object-"
        "local state host without freeing the parent allocation.",
    ),
    0x14048B4A0: (
        "CopyMaterializedStateHeaderAndPayload",
        "Copies the compact 0x20-byte materialized-state header and clones the "
        "embedded payload object through its virtual copy slot.",
    ),
    0x14048F180: (
        "DetachMaterializedStateObjectFromOwnerRuntime",
        "Shared detach primitive for owner+0x30 materialized state objects. "
        "Calls the old object virtual +0x68 when active, writes its state word "
        "back into runtime+0x1F78, clears owner+0x30, and opens the shared "
        "materialized-state update window through global timing/flag state.",
    ),
    0x14048F260: (
        "AttachMaterializedStateObjectToOwnerRuntime",
        "Attaches a materialized state object to owner+0x30, syncs it against "
        "runtime+0x1F78 and runtime+0xE0 timing state, toggles object flags, "
        "stores the owner pointer at object+0x28, and calls the object virtual "
        "+0x60 activation hook.",
    ),
    0x14048F320: (
        "SwapMaterializedStateObjectOnOwnerRuntime",
        "Local owner+0x30 swap helper. Detaches the previous materialized state "
        "object without the global update-window bookkeeping used by "
        "DetachMaterializedStateObjectFromOwnerRuntime, clears its owner pointer "
        "and active bits, then binds and activates the replacement object using "
        "the same object+0x28/object+0x38 setup as the main attach primitive.",
    ),
    0x14048F410: (
        "NotifyMaterializedStateObjectOfTrackedEntityChange",
        "Pushes a tracked live-entity pair into the shared materialized-state "
        "globals at 0x143D45E60, then conditionally notifies the current "
        "owner+0x30 object through virtual +0x50 or +0x48 depending on its "
        "local flags and mode bits.",
    ),
    0x14048BF60: (
        "RefreshMaterializedOwnerStateFromDescriptorAndReadiness",
        "0x4D0-variant refresh/finalize path. Resolves a descriptor from "
        "object+0xB4, calls the owner-side readiness gate at 0x140423440, "
        "sets owner+0x15AA bit 0 when the gate fails, queries the nested "
        "control family through owner+0x15C8 virtual +0x88, conditionally "
        "marks object+0x4C8, runs 0x14048EEF0, then tail-calls 0x140458630.",
    ),
    0x140423330: (
        "CheckOwnerNestedControlReadiness_Minimal",
        "Owner-side readiness gate used ahead of higher-level state updates. "
        "Requires owner+0x15C8, checks the nested control family via "
        "owner+0x15C8->(+8)->+0x28 virtual +0x60 and the collection helper "
        "0x1404C1060, then blocks on several owner virtual gates (+0x4C0, "
        "+0x7D0, +0xA28) before consulting owner+0x1808 virtual slot 0.",
    ),
    0x140423440: (
        "CheckOwnerNestedControlReadiness_WithRuntimeCooldown",
        "Stricter owner-side readiness gate. Requires owner flag "
        "owner+0x57C bit 0x2, owner+0x15C8, nested control readiness from "
        "owner+0x15C8->(+8)->+0x28 virtual +0x60, owner+0x15C8->(+8)->+0xC8 "
        "virtual +0xE0, and the collection helper 0x1404C1060. It then "
        "enforces a runtime cooldown using owner virtual +0xA10 and "
        "owner+0x2308 vs runtime+0xE0->0x10 before consulting owner+0x1808 "
        "virtual slot 0.",
    ),
    0x1404C1060: (
        "AnyNestedControlCollectionEntryReportsFlag68",
        "Iterates a small array-like collection (entries at +0x8, count at "
        "+0x10) and ORs together each non-null entry's virtual +0x68 result.",
    ),
    0x1404C10C0: (
        "AnyNestedControlCollectionEntryReportsFlag70",
        "Sibling of 0x1404C1060 that iterates the same collection and ORs "
        "each non-null entry's virtual +0x70 result.",
    ),
    0x1404C1120: (
        "BuildNestedControlCollectionFlag70Bitset",
        "Builds a two-qword bitset of collection entries whose virtual +0x70 "
        "returns true. Uses the same entry array/count layout as "
        "0x1404C1060/0x1404C10C0.",
    ),
    0x141DEF5D0: (
        "DispatchOwnerLateDecisionObjectSlot08WhenPrimaryAllows",
        "Owner-local proxy into the object at owner+0x1808. Calls virtual slot "
        "+0x0 first; when that returns false, tail-calls the same object's "
        "virtual slot +0x8 with (decision_object, owner).",
    ),
    0x141DEF610: (
        "DispatchOwnerLateDecisionObjectSlot20WhenPrimaryAllows",
        "Owner-local proxy into owner+0x1808. Calls virtual slot +0x0 first; "
        "when that returns true, tail-calls slot +0x20 with (decision_object, "
        "owner, const_float@0x142BEA0A8).",
    ),
    0x141DEF650: (
        "DispatchOwnerLateDecisionObjectSlot18WhenPrimaryAllows",
        "Owner-local proxy into owner+0x1808. Calls virtual slot +0x0 first; "
        "when that returns true, tail-calls slot +0x18 with "
        "(decision_object, owner).",
    ),
    0x141DEF690: (
        "ForwardToOwnerLateDecisionObjectPrimary",
        "Thin wrapper that resolves owner+0x1808 and jumps directly to its "
        "virtual slot +0x0 with (decision_object, owner).",
    ),
    0x140CA17E0: (
        "PublishRuntimeStateFromDecisionNodeLikeObject",
        "Large state-publication path that expects a node-like object with a "
        "parent/runtime pointer at +0x8. Reads broad owner/runtime state from "
        "that parent, builds multiple transform/state payloads, and publishes "
        "them through downstream runtime/debug channels.",
    ),
    0x141DE1600: (
        "ConstructOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "Constructor-like path for the owner/runtime class that contains the "
        "nested control family at +0x15C8, control-mode fields around +0x1748, "
        "and the late-decision node slot at +0x1808. Installs vtable "
        "0x142B82F00 and seeds the larger state cluster through +0x19D8.",
    ),
    0x141DE1CA0: (
        "DestroyOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "Freeing destructor wrapper for the same owner/runtime class. Calls "
        "the in-place teardown at 0x141DE1CE0 and frees the 0x1C00-byte "
        "allocation when requested.",
    ),
    0x141DE1CE0: (
        "TeardownOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "In-place teardown for the owner/runtime class rooted at vtable "
        "0x142B82F00. Reinstalls the class vtable, releases the nested "
        "control collection at +0x15C8, frees the owned cluster at +0x15D0, "
        "and clears additional owned state in the +0x1700 region.",
    ),
    0x141DE3C80: (
        "RelocateOwnerRuntimeClass_WithDecisionNodeSlot",
        "Relocate/update path for the same owner/runtime class. Rebases fields "
        "including +0x15A0, +0x1738, +0x1740, +0x1808, +0x19A8 and relocates "
        "the object stored at +0x19D8 via its virtual +0x48.",
    ),
    0x141DE3AC0: (
        "VtableRelocateOwnerRuntimeClass_WithDecisionNodeSlot",
        "Vtable-facing relocate/update method for the same owner/runtime class. "
        "Rebases fields from +0x15B0 upward, including nested-control-adjacent "
        "pointers and owned state used by the control/decision stack.",
    ),
    0x141DE2180: (
        "SetupOwnerRuntimeClassOwnedObject_1728_AndActivateState",
        "Large class-local setup/activation method. Clears owner+0x174C, sets "
        "bit 0x1 in owner+0x15A8, resets owner+0x1728, and uses a global "
        "callback to construct/store an owned object back into +0x1728.",
    ),
    0x141DE99D0: (
        "RefreshOwnerRuntimeClassFromRetainedLiveObject",
        "Class-local retained/live refresh method. Clears owner+0x1500/+0x1508, "
        "resolves a retained/live object through the global retained table "
        "using owner+0x108/+0x10C, copies its leading payload into owner+0x110, "
        "and combines it with owner transforms around +0x120/+0x130.",
    ),
    0x141DE73D0: (
        "ResolveOwnerControlMode1748_WithRuntimeDecisionGates",
        "Large class-local resolver that starts from owner+0x1748 and applies "
        "additional runtime gates before returning a low-cardinality control "
        "mode. Used before dispatching nested control virtual +0x168.",
    ),
    0x141DE8B03: (
        "UpdateOwnerRuntimeStateFromDecisionNodeAndNestedControl",
        "Large owner-runtime update method that combines the late-decision "
        "object at owner+0x1808 (virtual +0x10), nested control state at "
        "+0x15C0/+0x15C8, retained/live refresh, per-entry nested-control "
        "virtual +0x38 fanout, injured-layer publish, and a final owner "
        "virtual +0xE08 call.",
    ),
    0x1403DD180: (
        "GetMaterializedStateSlotOwner",
        "Tiny getter that returns the managed materialized-state slot at "
        "owner+0x1F80.",
    ),
    0x140409160: (
        "GetAttachedMaterializedStateObject",
        "Returns the currently attached materialized state object from the "
        "managed slot at owner+0x1F80 -> +0x30.",
    ),
    0x140409170: (
        "DetachAttachedMaterializedStateObject",
        "Thin owner-side wrapper over DetachMaterializedStateObjectFromOwnerRuntime "
        "using the managed slot at owner+0x1F80.",
    ),
    0x140409180: (
        "AttachMaterializedStateObjectViaOwnerSlot",
        "Thin owner-side wrapper over AttachMaterializedStateObjectToOwnerRuntime "
        "using the managed slot at owner+0x1F80.",
    ),
    0x1404091A0: (
        "ClearAttachedMaterializedStateObject_Local",
        "Local clear path for the currently attached materialized state object. "
        "Replays the object virtual +0x68 when needed, writes the state word "
        "back into owner+0x1F78, and clears slot+0x30 without opening the "
        "global detach/update window.",
    ),
    0x1404280D0: (
        "RelocateMaterializedStateOwnerCluster",
        "Relocates the owner cluster that includes +0x1F78/+0x1F80/+0x1F88/"
        "+0x1FB0/+0x1FF8. Invokes per-object relocate virtuals and then "
        "rebases in-range pointers by the supplied delta.",
    ),
    0x1403E0400: (
        "QueryMaterializedStateSelectionRecord",
        "Builds a small output record by querying the attached materialized "
        "state object through virtual +0x30, then falling back through "
        "owner+0x1FF0 virtual +0x68 and 0x1403E02C0 when needed.",
    ),
    0x1403F9570: (
        "GetSelectorCapableAttachedMaterializedStateObject",
        "Returns owner+0x1F80->+0x30 only when the attached materialized "
        "object exists and its selector/capability id at +0x20 is enabled "
        "through CheckSelectorHashEnabledInBitsetRegistry (0x1417C4800).",
    ),
    0x14043B710: (
        "HasSelectorCapableAttachedMaterializedStateObject",
        "Boolean helper over owner+0x1F80->+0x30. Returns true only when the "
        "attached materialized object exists and its selector/capability id at "
        "+0x20 is enabled through CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x1404B7D50: (
        "ReplaceAttachedMaterializedStateObject_WithVariant540",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x540 bytes, constructs the replacement through 0x14045C0D0, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1404B7E30: (
        "ReplaceAttachedMaterializedStateObject_WithVariant48",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x48 bytes, constructs the replacement through 0x14045B8D0, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x140473590: (
        "ConstructBehaviorScavengeMaterializedStateVariant570",
        "Constructs a 0x570 BehaviorScavenge materialized-state object. Seeds "
        "its local behavior block, installs the 0x14293F7D0 vtable, initializes "
        "the +0x40 subobject, and attaches the caller-supplied snapshot/source "
        "payload through 0x140473020.",
    ),
    0x140473460: (
        "ConstructBehaviorScavengeMaterializedStateVariant570Base",
        "Shared base ctor for the 0x570 BehaviorScavenge materialized-state "
        "family. Initializes the base behavior object through 0x14048EC20, "
        "installs vtable 0x14293F7D0, initializes the +0x40 subobject, and "
        "seeds tail fields around +0x538..+0x560 before caller-specific payload "
        "copy runs.",
    ),
    0x140480230: (
        "ConstructBehaviorShootMovingMaterializedStateVariant9B0",
        "Constructs the 0x9B0 materialized-state variant tagged with the "
        "BehaviorShootMoving label. Seeds the generic base, installs its vtable, "
        "copies the caller payload into +0x40 and +0x570-side state, and "
        "materializes shot-moving flags around +0x980..+0x9A1.",
    ),
    0x14048A260: (
        "ConstructBehaviorTurnToFaceMaterializedStateVariant70",
        "Constructs the 0x70 materialized-state variant tagged with the "
        "BehaviorTurnToFace label. Seeds the generic base and copies the small "
        "turn-to-face payload into +0x40..+0x64.",
    ),
    0x1404B7F00: (
        "InstallBehaviorScavengeMaterializedStateVariant570FromLocoSnapshot",
        "Indirect BehaviorScavenge install path for owner+0x1F80. Builds a "
        "0x570 materialized-state payload from the owner's locomotion snapshot "
        "and optional actor/source input, detaches the current slot object, "
        "constructs the replacement through 0x140473590, then reattaches it "
        "through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1404B80C0: (
        "DispatchBehaviorScavengeOwnerVirtualD58WithPreparedState",
        "BehaviorScavenge-side dispatcher that materializes a small state block "
        "through 0x141DE1280 and then calls owner virtual +0xD58 with the "
        "prepared payload and external source object.",
    ),
    0x1404B8180: (
        "RegisterBehaviorScavengeMetadataRoots",
        "Publishes the BehaviorScavenge label pointer and the paired metadata "
        "root at 0x142952020 into globals 0x143048978/0x143048970.",
    ),
    0x140665923: (
        "ReplaceAttachedMaterializedStateObject_WithVariant9B0",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x9B0 bytes, constructs the replacement through 0x140480230, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1403DB300: (
        "GetOwnerStateHelper_1FD8",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FD8.",
    ),
    0x1403DB310: (
        "GetOwnerStateHelper_1FE0",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FE0.",
    ),
    0x1403DB320: (
        "GetOwnerStateHelper_1FE8",
        "Tiny owner accessor that returns the actor-bound runtime helper "
        "stored at owner+0x1FE8.",
    ),
    0x1403FC050: (
        "GetOwnerStateHelper_1FD8_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FD8.",
    ),
    0x1403FC060: (
        "GetOwnerStateHelper_1FE0_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FE0.",
    ),
    0x1403FC070: (
        "GetOwnerStateHelper_1FE8_Duplicate",
        "Duplicate tiny owner accessor for the actor-bound runtime helper at "
        "owner+0x1FE8.",
    ),
    0x1403FC080: (
        "GetOwnerStateHelper_1FF8",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FF8.",
    ),
    0x1403FC090: (
        "GetOwnerStateHelper_1FF8_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FF8.",
    ),
    0x1404024E0: (
        "InitOwnerStateHelper_1FE8",
        "Zero-initializes the small actor-bound runtime helper later stored at "
        "owner+0x1FE8. Clears the leading handle/id pair and owned pointers at "
        "+0x28/+0x30.",
    ),
    0x140643570: (
        "BindOwnerStateHelper_1FE8_ToActorRuntime",
        "Owner-side binder for the 0x48 helper stored at owner+0x1FE8. Seeds "
        "it from the owner's retained/live pair at +0xA8/+0xBC, initializes "
        "its local state id at +0x10, clears +0x28/+0x38, and continues into "
        "the deeper actor-bound runtime setup path.",
    ),
    0x14041F1A0: (
        "RefreshOrSelectCurrentStateObject_1FF0",
        "Npc/Player skill think selector. Scans skill candidates; installs current "
        "Skill* at owner+0x1FF0; logs Entering/Exiting skill; calls Clear on failure.",
    ),
    0x140664690: (
        "Skill_IsAuthorityForRootTransform",
        "Skill vtable +96. Always returns 1 (base/Script/Idle/NetSkill). Negated in "
        "Npc_SyncControlHostEntityRootOnNetSkip clear-path — clear branch dead for "
        "standard skills.",
    ),
    0x1401D4F10: (
        "Player_Vtable4256_ReturnFalse",
        "Player vtable +4256 (0x10A0). Always returns 0; gates per-player "
        "ControlHostHub_RefreshEntity6EProbeFlags in GameplayWorld_RefreshControlHostEntity6EFlags.",
    ),
    0x140CC1DA0: (
        "Player_IsAliveForSkillAndDispatch",
        "Player vtable +2144 (0x860). Health/alive predicate; used in skill exit and "
        "3E59D0 entity6E refresh gate with player+5545 bit1.",
    ),
    0x140416AB0: (
        "GetCurrentStateObject_1FF0",
        "Tiny owner accessor that returns the currently installed gameplay-state "
        "selection object at owner+0x1FF0.",
    ),
    0x140416AC0: (
        "GetCurrentStateObject_1FF0_Duplicate",
        "Duplicate tiny accessor for the currently installed gameplay-state "
        "selection object at owner+0x1FF0.",
    ),
    0x140416AD0: (
        "GetCurrentStateObjectLabel_1FF0",
        "Returns current_state_object+0x70 from owner+0x1FF0, or a fallback "
        "label pointer when no current object is installed.",
    ),
    0x140416AF0: (
        "CopyCurrentStateSelector_1FF0",
        "Copies the current gameplay-state selector/hash from "
        "owner+0x1FF0->+0x78 into caller storage, defaulting to 0 when no "
        "current object is installed.",
    ),
    0x140416B10: (
        "GetCurrentStateSignedId236C",
        "Returns owner+0x236C, which is synchronized from the current "
        "gameplay-state object's signed id at +0x7C.",
    ),
    0x140416B20: (
        "InstallOrApplyCurrentStateRecord",
        "Owner-side install/apply entry for a current-state record. Clears any "
        "attached materialized state at owner+0x1F80->+0x30, seeds owner "
        "runtime state through 0x141DE9E60, refreshes owner transform caches "
        "at +0x4160..+0x4180, validates/logs the record, updates owner cache "
        "fields +0x4600..+0x460A, and on the zero-state branch dispatches the "
        "heavy nested-control apply path 0x141DE4F90. NOTE: this is a SHARED "
        "character virtual; it sits at NadineBuddy vtable slot +0xC48 (the slot "
        "earlier seen producing the 'native teleport' effect, which is really a "
        "state-record apply side effect). So NadineBuddy already exposes this "
        "control-apply ingress that reaches 0x141DE9E60.",
    ),
    0x141D63420: (
        "CharacterManager_AddCharacter",
        "CharacterManager::AddCharacter(rcx=manager=*(0x143898630), rdx=character). "
        "Array base manager+0, capacity manager+8, count manager+0xC; 16-byte "
        "entries {character+0xA8 ptr, character+0xBC id}. Dedupe-scans, writes "
        "array[count], increments count, then notifies observers at "
        "manager+0x10..+0x18 via node->vtable[0](node, character). This is the "
        "real CharacterManager registration path, but it is NOT the campaign "
        "blocker for second-player routing: Nadine is already registered there. "
        "The separate local-player resolver lives under 0x143FE1070.",
    ),
    0x141D63640: (
        "CharacterManager_RemoveCharacter",
        "Inverse of AddCharacter: scans the {ptr,id} array for the character, "
        "compacts it down, decrements count at manager+0xC, notifies observers "
        "via node->vtable+8(node, character), and calls character->virtual+0x810 "
        "(0x141FEBE20).",
    ),
    0x141D63C50: (
        "CharacterManager_GetCharacter",
        "Index accessor: asserts index<count(manager+8), entry=array[index] "
        "(index<<4 + manager+0), validates entry.ptr->(+8)->+0xBC==entry.id, "
        "returns entry.ptr. This is the CharacterManager array accessor itself; "
        "do not conflate it with the separate local-player resolver owner at "
        "0x143FE1070.",
    ),
    0x140486EB0: (
        "BehaviorTeleport_ComputeAimBasisAndSeedOwnerStateViaDE9E60",
        "BehaviorTeleport-specific aim/transform virtual (sole vtable slot ptr "
        "at 0x142941490; RTTI = BehaviorTeleport). Validates a retained slot via "
        "0x14386FD00, builds an orientation/aim basis from runtime objects "
        "using record +0xA0/+0xC0/+0xF0 quaternion math, then seeds owner "
        "runtime state through the control bridge 0x141DE9E60. This confirms "
        "that one direct path above 0x141DE9E60 is AI behavior logic, not raw "
        "player input.",
    ),
    0x14041C7E0: (
        "ActivateCurrentStateObject_1FF0",
        "Owner-side activation path for owner+0x1FF0. Clears owner state bits, "
        "dispatches current_state_object virtual +0x30, and clears bit 0x8 in "
        "current_state_object+0x80.",
    ),
    0x14041C820: (
        "ClearCurrentStateObject_1FF0",
        "Owner-side teardown/reset path for owner+0x1FF0. Releases nested "
        "attachments, dispatches current_state_object virtual +0x38, syncs "
        "owner flags and signed ids from +0x80/+0x7C, clears owner+0x1FF0, "
        "and drops a mode bit in owner+0x1FA8.",
    ),
    0x140416F90: (
        "ReportAndClearCurrentStateObject_1FF0",
        "If owner+0x1FF0 exists, reports its +0x70 label through "
        "0x1403CF8B0 and then jumps into ClearCurrentStateObject_1FF0.",
    ),
    0x140417040: (
        "CheckOwnerReadinessViaNestedControlAndHelper1FF8",
        "Compact owner-side readiness gate. First queries nested control at "
        "owner+0x15C8 via virtual +0x50/+0xC8, otherwise falls back to owner "
        "virtual +0x8C0, touches helper owner+0x1FF8 via virtual +0xA0, and "
        "under negative mode byte in owner+0x1FA8 dispatches nested-control "
        "virtual +0x58/+0xC0 before returning success.",
    ),
    0x14041C900: (
        "ReconcileCurrentStateObjectAndNestedControl",
        "Owner-side monitor/reconcile path over current_state_object at "
        "owner+0x1FF0, nested control at owner+0x15C8, and owner caches "
        "+0x1EC0/+0x1EC4. Uses owner bytes +0x1D51/+0x1D52 as pending gates, "
        "logs current labels and state names, compares nested-control state "
        "through virtual +0x50/+0xB0, and can dispatch current_state_object "
        "virtual +0x28 or nested-control virtual +0xA0/+0xD0.",
    ),
    0x14041CBF0: (
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode",
        "Large owner-side actor-state metric builder. Validates the authored "
        "descriptor at owner+0x108/+0x10C, pulls a runtime object through "
        "owner virtual +0xFB8, combines its orientation with owner transforms, "
        "applies late-decision gating through owner+0x1808 and owner virtual "
        "+0x1018, selects nested-control branches via owner+0x15C8, and "
        "stores the resulting scalar back into helper state under owner+0x1A0 "
        "and owner virtual +0xCE8.",
    ),
    0x14041FA90: (
        "RefreshOwnerRuntimeHelperBundle_1FC0_1FD8_1FE8_1FF8",
        "Owner-side update over the helper bundle after +0x1F80. Refreshes the "
        "attached materialized-state slot, conditionally updates helpers "
        "+0x1FC0/+0x1FD8/+0x1FE8/+0x1FF8, rebinds the actor-bound helper "
        "owner+0x1FE8 to the current live actor, then combines late-decision "
        "and nested-control scalar paths into the helper state under "
        "owner+0x1A0 and owner virtual +0xCE8.",
    ),
    0x14041CED0: (
        "ResolveOwnerReferenceTransformViaHelper1FC0OrRetainedPlayers",
        "Produces a reference transform for the owner by first consulting the "
        "helper at owner+0x1FC0, then selector-capable helper objects, and "
        "finally falling back to retained player slots 0/1 through "
        "0x1410F0D10 before reading the chosen actor transform at +0x100.",
    ),
    0x14041D0A0: (
        "PublishOwnerRuntimeRecordsFromHelper1FC0Candidates",
        "Large owner-side publish/refresh pass over helper owner+0x1FC0. Locks "
        "the helper, enumerates candidate entries through 0x140640360, filters "
        "them through 0x14063CF00 and retained/live matching, synthesizes large "
        "runtime record blocks, and pushes those blocks into the current live "
        "actor through virtual +0xD0.",
    ),
    0x14041D710: (
        "CheckOwnerReadinessViaHighLevelObjectOrNestedControl",
        "Owner-side readiness gate that first tries a higher object through "
        "owner virtual +0x9B0 and two subqueries via 0x14128C910, then falls "
        "back to nested control owner+0x15C8 via virtual +0x80/+0x180/+0x128, "
        "with additional modulation by owner virtual +0x4C0, global mode, and "
        "owner byte +0x1D0B.",
    ),
    0x14041D820: (
        "CheckOwnerHelper1F90Availability",
        "Availability gate over owner+0x1F90. If helper mode +0x840 is not 1, "
        "it resolves an owner object through virtual +0xE20, passes it into "
        "0x1405F3F10, and requires the returned object to have byte +0x189 "
        "set before reporting success.",
    ),
    0x14063D800: (
        "RefreshHelper1FC0HostAnchorsFromPreferredCandidate",
        "Post-pass over owner+0x1FC0 that selects a preferred candidate entry, "
        "then refreshes four runtime anchors behind owner+0x1FB0->+0x10. It "
        "computes world-space vectors from helper entries and live actors, "
        "writes them into anchor regions around +0x460/+0x4D0/+0x540/+0x5B0, "
        "and toggles per-anchor active flags at +0x6E.",
    ),
    0x1404370C0: (
        "RunHelper1FC0HostAnchorPostPass",
        "Thin owner-side wrapper that forwards owner+0x1FC0 into "
        "RefreshHelper1FC0HostAnchorsFromPreferredCandidate as part of the "
        "regular gameplay-state update loop.",
    ),
    0x140436F20: (
        "SyncHostAnchors620_690FromCurrentActorAndStateVector",
        "Owner-side anchor sync that updates two host anchor regions at "
        "owner+0x1FB0->+0x10 +0x620/+0x690 from the current actor and helper "
        "owner+0x1FF8, gated by owner bytes +0x1D54/+0x1D56 and actor tag "
        "0xAC710200.",
    ),
    0x140439E60: (
        "RunGeometryThresholdGateForNestedControlAction",
        "Heavy geometry/threshold gate that combines current-actor transforms, "
        "nested-control state from owner+0x15C8, owner transform +0x1C8, and "
        "helper-resolved reference geometry. If the final scalar falls below "
        "threshold, it seeds owner-side scalar state and triggers a nested-"
        "control action through virtual +0x78.",
    ),
    0x14043A2F0: (
        "PrepareHighLevelActionTargetsFromOwnerState",
        "Large high-level action/target preparation pass over the owner state. "
        "Combines owner byte +0x1D59, owner virtual +0x9B0, the current-state "
        "object at +0x1FF0, helper availability through +0x1FC0/+0x1F90, "
        "retained/live actor capability bits, global table lookups via "
        "+0x1E84, and candidate-array iteration to select valid target/action "
        "state.",
    ),
    0x14043AC60: (
        "ContinueHighLevelActionTargetPreparation",
        "Sibling to PrepareHighLevelActionTargetsFromOwnerState. Reuses the "
        "same owner byte +0x1D59, +0x1E84 table, owner virtual +0x438/+0x9B0, "
        "capability-bit tests, and candidate-array scans to continue the same "
        "high-level action/target-preparation family.",
    ),
    0x14043B380: (
        "UpdateOwnerDescriptorCapabilityFlag18B0",
        "Clears owner+0x18B0, resolves the authored descriptor at "
        "owner+0x108/+0x10C, follows descriptor+0x68 into a retained/live "
        "runtime object, and sets +0x18B0 if that object exposes the required "
        "capability bit.",
    ),
    0x14043B490: (
        "EnsureDefaultAnimStateNpcInfoKnowledgeRecord",
        "Owner-side helper that resolves or creates the shared knowledge record "
        "for 'default-anim-state-npc-info' through 0x1417651D0 when the "
        "required subrecord is not already present on the current owner-side "
        "runtime object.",
    ),
    0x14043B5B0: (
        "EnsureDefaultAnimStateNpcInfoKnowledgeRecord_Variant",
        "Sibling helper to EnsureDefaultAnimStateNpcInfoKnowledgeRecord that "
        "builds the same 'default-anim-state-npc-info' record id and resolves "
        "it through 0x1417651D0 from a nearby owner/runtime path.",
    ),
    0x14043B740: (
        "PublishResolvedKnowledgeRecordsIntoSink",
        "Resolves the owner's current knowledge/state record and additional "
        "records from owner+0x2388 through 0x141765030, then publishes each "
        "resolved record into an external sink via 0x1417F9150.",
    ),
    0x140425BC0: (
        "CheckOwnerDecisionGateViaActorStateScalar",
        "Higher-level owner decision gate that first tries a cached live object "
        "at +0x23D8/+0x23E0, otherwise requests a direction/target vector via "
        "0x141DE6500 and feeds it into "
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode.",
    ),
    0x1404586B0: (
        "InitMaterializedStateHost",
        "Initializes the object-local materialized-state host used by the 0x4D0 "
        "variant. Seeds the embedded sub-vtable at +0x40 and clears the owned "
        "runtime/object pointers up through +0xA0.",
    ),
    0x1404588A0: (
        "TeardownMaterializedStateHost",
        "Tears down the object-local materialized-state host. Releases the "
        "owned runtime/object pointers at +0x50/+0x90 and restores the host "
        "vtable state.",
    ),
    0x141765580: (
        "ResolveGlobalTaskNodeThresholdRecord",
        "General resolver that looks up hashed/global task-node records using "
        "the strings 'global' and '*start-task-node*'. Returns either a direct "
        "registry hit or a fallback/global task node record consumed by "
        "ProbeControlRoutingThresholdsAndSelectStateId and other systems.",
    ),
    0x141765030: (
        "ResolveKnowledgeRegistryEntryById",
        "Global registry lookup for authored knowledge/state records by "
        "integer id. Searches sorted and hashed secondary containers and "
        "returns the matching registry entry payload when present.",
    ),
    0x1417651D0: (
        "ResolveOrCreateKnowledgeRegistryEntryById",
        "Wrapper over ResolveKnowledgeRegistryEntryById that returns an "
        "existing knowledge/state entry when present and otherwise enters the "
        "creation/assert path for a missing registry record.",
    ),
    0x141DF35F0: (
        "ResolveRuntimeObjectFromPackedHandle",
        "Resolves a packed runtime handle/id through the global handle tables "
        "and returns the live runtime object only when the handle and object "
        "id still agree.",
    ),
    0x141DE4F90: (
        "ApplyStateRecordThroughNestedControlAndActorRuntime",
        "Heavy owner-side apply path for a current-state record object. Builds "
        "a temporary control/runtime block from owner+0x15B8, resolves/evicts "
        "competing selection through owner+0x15C0+0x2F8, deactivates the "
        "previous nested-control node through virtual +0xC0, applies the new "
        "one through virtual +0xE0 with packed owner transforms, then commits "
        "the record-object transform through owner virtual +0xC48 and finalizes "
        "via owner+0x15C0/+0x15B8, including a final 0x141E0EDF0 snapshot push.",
    ),
    0x141DE5190: (
        "ApplyPreparedNavCharacterStateRecordWithBlendScalars",
        "Prepared-state wrapper above 0x141E15030. Copies the incoming compact "
        "record, injects two extra scalar inputs into the prepared payload, "
        "forces record kind 4, then routes the result into owner+0x15C0 "
        "through the shared nested-control sink.",
    ),
    0x141DE5430: (
        "ResolveSharedNestedControlRuntimeObjectBlock310",
        "Owner-side helper that resolves nested-control handles at +0x2F8 and "
        "+0x258 through 0x141DF35F0, verifies that both point to the same live "
        "runtime object, and then returns the follow-up control block at "
        "owner+0x15C0+0x310.",
    ),
    0x141DE6500: (
        "BuildOwnerDirectionVectorFromStateSlotsAndDescriptor",
        "Synthesizes a world/reference vector from owner state slots "
        "+0x18F0/+0x1990, validates the authored descriptor at +0x108/+0x10C, "
        "refreshes owner+0x110, and combines that descriptor offset with owner "
        "transform +0x120 before writing the final vector to caller storage.",
    ),
    0x14063CF00: (
        "CheckHelper1FC0CandidateAgainstRetainedPair",
        "Helper-side filter for owner+0x1FC0. Enumerates helper entries, "
        "matches them against an incoming retained/live pair, and rejects "
        "special entry kinds identified by state byte +0x190 before returning "
        "a bool-like match result.",
    ),
    0x14063C260: (
        "FindHelper1FC0EntryMatchingRetainedPair",
        "Helper-side lookup that scans the owner+0x1FC0 entry pool, resolves "
        "each enabled entry through virtual +0x10, and returns the first entry "
        "whose retained/live pair matches the input pair.",
    ),
    0x140640360: (
        "CollectHelper1FC0EntriesByStateKind",
        "Enumerates the entry pool behind owner+0x1FC0, filters entries by the "
        "requested state-kind byte at entry->virtual +0x10 -> +0x190, and "
        "appends matching entries into a bounded output list.",
    ),
    0x14063E890: (
        "PublishPreparedStateRecordIntoHelper1FC0",
        "Helper-side publish/apply path for a prepared owner/action record. "
        "Locks helper+0xC0, scans watcher nodes around +0x1D8 via virtual "
        "+0x28, allocates and initializes a watcher object when needed, then "
        "dispatches the incoming record through watcher virtual +0x30.",
    ),
    0x1406427E0: (
        "InitHelper1FC0PublishedRecordObject",
        "Initializes the helper-side published record object used by "
        "PublishPreparedStateRecordIntoHelper1FC0. Installs the object vtable, "
        "clears embedded retained/live pairs and helper subobjects, and seeds "
        "a unique object id.",
    ),
    0x14040DB30: (
        "CacheSelectedActionTargetSnapshot",
        "Caches a selected action target back into owner state. Copies target "
        "transform/orientation blocks into owner+0x1ED0..+0x1F30, stores the "
        "target retained/live pair at +0x1F48/+0x1F50, and sets owner byte "
        "+0x1D55 bit 0x2.",
    ),
    0x140404240: (
        "LatchOwnerPerceptionBit40FromHelperType6",
        "Helper-driven owner latch that resolves a +0x1FC0 entry, requires "
        "entry kind 6 and several inactive helper predicates, then sets "
        "owner+0x1D59 bit 0x40.",
    ),
    0x140407910: (
        "CheckCurrentActorGlobalSlotBusy",
        "Scans the global actor-slot table for the current actor identity and "
        "reports whether the matching slot is already occupied or otherwise "
        "busy according to per-slot flags and counts.",
    ),
    0x140421E70: (
        "ProcessOwnerPerceptionEventTagAndUpdateRecentState",
        "Processes owner event tags returned through virtual +0x10D8. Handles "
        "two special hashes by setting owner+0x1D59 bits 0x1/0x2, snapshotting "
        "current runtime handles into +0x2190/+0x2198, and rebuilding derived "
        "vectors at +0x2450/+0x2460 from the current descriptor and event "
        "source position.",
    ),
    0x140422100: (
        "CheckOwnerRecentPerceptionBit1Fresh",
        "Reports whether owner+0x1D59 bit 0x1 is still fresh by comparing the "
        "current runtime handle against the snapshot stored at +0x2190 within "
        "a short time window.",
    ),
    0x140422140: (
        "CheckOwnerRecentPerceptionBit2Fresh",
        "Reports whether owner+0x1D59 bit 0x2 is still fresh by comparing the "
        "current runtime handle against the snapshot stored at +0x2198 within "
        "a short time window.",
    ),
    0x14043B380: (
        "UpdateOwnerDescriptorCapabilityFlag18B0",
        "Clears owner+0x18B0, resolves the authored descriptor at "
        "owner+0x108/+0x10C, follows descriptor+0x68 into a retained/live "
        "runtime object, and sets +0x18B0 if that object exposes the required "
        "capability bit.",
    ),
    0x14043B740: (
        "PublishResolvedKnowledgeRecordsIntoSink",
        "Resolves the owner's current knowledge/state record and additional "
        "records from owner+0x2388 through 0x141765030, then publishes each "
        "resolved record into an external sink via 0x1417F9150.",
    ),
    0x1417F9150: (
        "CommitResolvedKnowledgeRecordIntoSinkSlot",
        "Sink-side slot/registry commit. Resolves a destination slot from the "
        "incoming record key, writes record+0x10 into the sink value array, "
        "writes the record pointer into the parallel pointer array, and then "
        "finalizes the sink through 0x1417FA5A0.",
    ),
    0x1417FA5A0: (
        "FinalizeOrRefreshStructuredSinkState",
        "Large sink-side finalize/refresh helper used after slot insert/clear. "
        "Builds a CRC/hash-like accumulator over sink entries and metadata and "
        "refreshes the sink's derived state.",
    ),
    0x141E63660: (
        "BroadcastKnowledgeRecordsFromOwnerManager1FA0",
        "Manager-wide outward broadcast over owner+0x1FA0. Iterates entry "
        "records, resolves live actors, builds compact hashed records, and "
        "pushes them into those actors through virtual +0xD0.",
    ),
    0x141EC4E90: (
        "RunWorldSpaceProbeBetweenTransforms",
        "World/probe-system query between two transform vectors plus an "
        "external config/output record. Validates both vectors, packages them "
        "for the world query layer, and dispatches the final probe through "
        "0x1420A7D30.",
    ),
    0x14123F260: (
        "ResolveGlobalKnowledgeStateRecordById",
        "Global table/cache-driven resolver for authored knowledge/state "
        "records. Resolves a record object by id through registry helpers and "
        "returns the record whose flag block is later consumed by owner "
        "perception/action code.",
    ),
    0x1406AD290: (
        "AdvanceControlRoutingAfterActorStateScalarSuccess",
        "Second-stage control-routing transition reached after "
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode succeeds. Clears "
        "transient bits around +0x27D0/+0x26A4, then evaluates a follow-up "
        "record/current-object path and nested-control gate before reporting "
        "the next control-routing status.",
    ),
    0x141DE9E60: (
        "SeedOwnerRuntimeFromIncomingStateRecord",
        "Large owner-state seeding routine that validates an incoming state "
        "record against the authored table at 0x14386FD00, passes the matching "
        "descriptor into owner virtual +0x1E8, copies record transforms into "
        "owner+0x130/+0x140, dispatches owner+0x15B8 helpers, resets broad "
        "owner state mirrors around +0x175C..+0x19FC, refreshes live retained "
        "actor state, and marks companion/runtime bits after install.",
    ),
    0x141DE4D20: (
        "ApplyPreparedNavCharacterStateRecordThroughNestedControlSink",
        "Copies a compact prepared state block into owner+nested-control state, "
        "writes owner+0x1748 from the record mode id, then forwards the payload "
        "into 0x141E15030 through owner+0x15C0.",
    ),
    0x141E15030: (
        "ConsumePreparedNavCharacterStateRecordAndRefreshNestedControl",
        "Heavy nested-control sink for prepared NavCharacter state records. "
        "Validates owner actor identity, compares the incoming record against "
        "current control state, updates owner+0x110/+0x120 live transforms, "
        "rebuilds control deltas, emits NavMesh-tagged diagnostics, and commits "
        "accepted state into owner-side nested-control storage.",
    ),
    0x1410F1910: (
        "SpawnPlayerFromRetainedDescriptor",
        "Retained spawn-player path that consumes an explicit descriptor block "
        "and then commits spawn state through 0x1410F14C0. It now also reads "
        "as a richer Player-facing packet builder around "
        "`*player-starting-weapons*` and the "
        "`TrySpawnPlayerAtContinue(): m_pistol=%s, m_longGun=%s, m_rope=%s, "
        "useCheckpointed=%d` branch. Nearby literals tie it directly to "
        "TaskManager::SpawnPlayer / TrySpawnPlayerAtContinue and "
        "player/camera spawner lookup.",
    ),
    0x140B56090: (
        "HandleNetSpawnCoopBuddy",
        "Network/retained coop-buddy spawn path. Resolves retained roster "
        "entries, materializes live entity state, and feeds player-labelled "
        "spawn descriptors into the shared retained spawn stack.",
    ),
    0x140B580C0: (
        "SelectRetainedCoopPlayerLookHash",
        "Chooses a player-labelled retained coop look/descriptor variant based "
        "on runtime state gates and global mode selection.",
    ),
    0x1410F3350: (
        "SpawnPlayerFallback",
        "Fallback retained spawn-player path. Builds default transforms, "
        "initializes shared player-labelled registries, then calls "
        "CommitResolvedPlayerSpawn and post-spawn runtime gating on the "
        "resolved live entity.",
    ),
    0x141D22420: (
        "ResolveRetainedRosterEntity",
        "Resolves a live entity from a retained roster/slot entry. Walks nested "
        "lists, checks script/runtime flags, and can recurse until a valid live "
        "entity handle is found.",
    ),
    0x141EB8950: (
        "DestroyManagedPointerArrayNodes_2NPlus3",
        "Iterates the managed pointer array at owner+0x4A0 with logical length "
        "(2*N+3) derived from owner+0x4A8, calling each non-null node's "
        "virtual +0x28 method before tail-calling the parent teardown.",
    ),
    0x141EB7CE0: (
        "ConstructCompanionCameraRigOwner_Allocating",
        "Allocating constructor for the companion camera-rig owner. Requests "
        "a 0x4E0-byte object, installs the vtable at 0x142B99A98, and zeros "
        "the managed child-node cluster at +0x4A0..+0x4C0.",
    ),
    0x141EB7E70: (
        "ConstructCompanionCameraRigOwner_InPlace",
        "In-place constructor variant for the same 0x4E0-byte owner type. "
        "Calls the shared base constructor, installs the vtable at "
        "0x142B99A98, and clears +0x4A0..+0x4C0.",
    ),
    0x141EB7EC0: (
        "DestroyCompanionCameraRigOwner_Freeing",
        "Freeing destructor for the 0x4E0-byte companion camera-rig owner. "
        "Destroys the managed object at +0x4B8, tail-calls the base teardown, "
        "and conditionally frees the storage when the low bit of the dtor "
        "flags is set.",
    ),
    0x141EB8890: (
        "AttachManagedNodeGraphToCompanionCameraRigOwner",
        "Builds or acquires the managed node graph stored at owner+0x4B8 "
        "through virtual slot +0x790, links it back to the owner via "
        "0x141E60E80, then continues shared owner initialization through "
        "0x141BBACC0.",
    ),
    0x14087D390: (
        "InitCompanionCameraRigOwner_ModeFlag_0x10000000",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x10000000 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x1414FBC20: (
        "InitCompanionCameraRigOwner_ModeFlag_0x01000000",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x01000000 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x1402A2450: (
        "InitCompanionCameraRigOwner_ModeFlag_0x00000800",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x00000800 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x14088B250: (
        "ConstructDerivedCompanionCameraRigOwner_0x500",
        "Constructor for a 0x500-byte derived companion camera-rig owner. "
        "Calls the shared base constructor, installs vtable 0x1429B32F8, "
        "clears the inherited managed child-node cluster at +0x4A0..+0x4C0, "
        "and adds derived fields at +0x4E0/+0x4E8.",
    ),
    0x141EB89E0: (
        "RelocateManagedPointerArrayOwner",
        "Relocates pointer fields and pointer-array entries for a managed owner "
        "object. Adjusts owner+0x4B8, the nested pointer at [owner+0x4B8]+0x60, "
        "the pointer array at owner+0x4A0 sized as (2*N+3), then tail-calls "
        "the parent relocation helper.",
    ),
    0x141EB8AE0: (
        "RefreshManagedPointerArrayNodesIfClean",
        "When owner+0x4A0 and owner+0x1A0 are present and owner+0x4C0 is clear, "
        "iterates the managed pointer array at owner+0x4A0 with logical length "
        "(2*N+3) and refreshes each node through 0x141E4D760 while keeping the "
        "cached state vector at +0x110 in sync.",
    ),
    0x1411DE2E0: (
        "ConstructPlayerTaggedMeleeScriptObserver_0x1C0",
        "Player-tagged melee observer constructor/factory. CRCs the string "
        "'Player', compares it against the incoming runtime descriptor, then "
        "allocates a 0x1C0 observer object, installs vtable 0x142A69798, "
        "and seeds default runtime/transform fields for the melee script "
        "observer family.",
    ),
    0x14124AE60: (
        "SeedMeleeObserverTrackedActorPairsAndBaseScalar",
        "Copies incoming tracked actor/runtime pairs into the melee observer "
        "object at +0xB0/+0xB8 and into the linked runtime owner cache, then "
        "derives the observer's base scalar at +0xC4 from the actor-side "
        "graph under +0x1A0.",
    ),
    0x14124AFC0: (
        "UpdateMeleeObserverKnowledgeAndSettings",
        "Main update path for the player-tagged melee observer. Resolves the "
        "live actor from the retained/runtime pair at +0xB0/+0xB8, queries "
        "actor virtual slots +0x5C8/+0x9B0/+0xBE0, resolves authored records "
        "through 0x14123F260, hashes the melee-settings vs net-melee-no-ap "
        "paths, packages the state through 0x1411E00D0/0x1411E0230, and "
        "stores the resolved state id at +0xC0.",
    ),
    0x14124C060: (
        "PushMeleeObserverActorTransformIfReady",
        "Resolves the melee observer's live actor from +0xB0/+0xB8, requires "
        "the readiness gate at 0x14124C0E0, then pushes a transform from the "
        "actor-side graph into 0x14189F410.",
    ),
    0x14124C0E0: (
        "CheckMeleeObserverActorState10Ready",
        "Resolves the observer's live actor/runtime pair, marks it active, "
        "walks actor+0x1A0 through 0x14172B7F0 and returns true only when "
        "0x14189E170 reports state id 0x10 for the observer's cached state "
        "slot at +0xC0.",
    ),
    0x14124C180: (
        "GetMeleeObserverScaledWeightFromLiveActor",
        "If the melee observer passes 0x14124C0E0, resolves another actor-side "
        "node through virtual +0x70, reads node+0x168, and multiplies it "
        "against the observer's base scalar. Otherwise falls back to the "
        "default scalar at +0xC4.",
    ),
    0x14189E170: (
        "QueryActorGraphStateByteByStateId",
        "Looks up an actor-side state id in the primary 0x100-byte table at "
        "graph+0x140 and the fallback 0x1C-byte table at graph+0x460, then "
        "returns the corresponding one-byte state marker used by the melee "
        "observer readiness path.",
    ),
    0x14189F410: (
        "ExtractActorGraphTargetTransformRecord",
        "Builds a compact target/transform record from an actor graph. Uses "
        "the selector in out_record+0x8/+0xC, prefers a matching live object "
        "from 0x14386FD00, copies its first 0x20 bytes when valid, and then "
        "mixes in local actor-graph transform/state fields.",
    ),
    0x1418A5CD0: (
        "ResolveActorRootCandidateEntryByStateId",
        "Seeds a compact selector record from actor_root+0x90 through "
        "0x1418A6010, prefers actor_root->0x38 and falls back to +0x40, then "
        "dispatches candidate resolution through 0x14193D250.",
    ),
    0x14193CD10: (
        "FindRecursiveCandidateEntryByPointerAndStateId",
        "Recursively walks 0x40-byte candidate entries. Follows child "
        "containers when flag 0x40 is set; otherwise matches the incoming "
        "candidate pointer and rejects entries whose state id already equals "
        "the requested one.",
    ),
    0x14193CDB0: (
        "ValidateCandidateEntryAgainstSelectorRecord",
        "Validates a candidate entry against the compact selector record built "
        "by the melee observer path, including gating bits in entry+0x4 and "
        "boolean selectors at record+0x2D/+0x2E.",
    ),
    0x14055D940: (
        "HandleNpcHolsterWeaponViaNestedControlObject",
        "Builds the npc-holster-weapon command record, resolves the nested "
        "control object through owner+0x15C8->virtual +0x58, and then routes "
        "the action either through 0x141DE55E0 (+0xA0) or direct nested "
        "virtual +0xB8 depending on the local action-state value.",
    ),
    0x141C965A0: (
        "RegisterInteractSelectorRegistryEntries",
        "Bootstrap registration path for interact-related selector ids. "
        "Hashes authored strings such as 'interact-command-id' through "
        "0x14014D910 and stores global selector/type/size metadata records "
        "under 0x146C742A8..0x146C742E4.",
    ),
    0x1408BDF40: (
        "BuildInteractHookRuntimeStateFromLiveObject",
        "Gameplay-facing interact/rope runtime builder. Hashes selector ids "
        "including 'interact-hook', 'interactable-def', and "
        "'rope-holster-offset', resolves the gated live runtime object "
        "through 0x141D21E20, packages its +0xA8/+0xBC retained-runtime pair, "
        "and feeds the resulting records through owner-side build/apply paths "
        "including virtual +0xE8, 0x141ECEFE0, virtual +0x20, and 0x141C9ED50.",
    ),
    0x1408C4A80: (
        "UpdateWinchHookActionStateFromRuntimeSelectors",
        "Large winch-hook action/state update path. Consumes selector ids "
        "such as 'winch-hook-attach', 'attach-winch-hook', "
        "'winch-hook-secure', and 'interactable-def', resolves actor/runtime "
        "pairs, materializes records through 0x141C9ED50/0x141C9EB30, and "
        "writes the resulting action state back into owner/runtime fields "
        "around +0x12F0..+0x1330, +0x14F8, +0x1511/+0x1514/+0x1518, and "
        "+0x17B8.",
    ),
    0x140E964F0: (
        "RefreshHookStateRecordsAndCooldownFlags",
        "Owner-side hook-state refresh helper. Walks compact records under "
        "object+0x1A0, resets/refreshes per-entry cooldown/state fields, and "
        "clears cached output at owner+0x220/+0x228 when the current hook "
        "state no longer matches the active runtime ids.",
    ),
    0x14124E7C0: (
        "RoutePlayerGrabWinchHookStateIntoNestedActionDispatch",
        "Actor-aware bridge from the player-grab-winch-hook family back into "
        "the shared nested action dispatcher. Resolves and validates the live "
        "actor through virtuals +0x8C0/+0xFA8/+0x860, compares it against the "
        "cached actor-side object at +0x2E8, then calls 0x141DE55E0 with a "
        "binary action-state argument.",
    ),
    0x140484C20: (
        "EvaluateCurrentActionAgainstNestedControlAndStateBundle",
        "Shared owner-side current-action gate. Starts from owner+0x15C8, "
        "checks cached live runtime pairs at +0x23D8/+0x23E0, special runtime "
        "tag 0xAC710201, and owner virtual +0x7D0; if those do not short-"
        "circuit, probes nested virtual +0xA8 and falls back to 0x141DE55E0. "
        "Then continues through helper/state objects under +0x1FB0/+0x1FF8/"
        "+0x1FA8 and owner virtuals +0xFD0/+0x478.",
    ),
    0x14058B900: (
        "HandleCombatActionStateThroughNestedControlAndActorGraph",
        "Large combat/action-state handler. Resolves live actor/runtime "
        "identity, filters a state-id list, forces low-cardinality nested "
        "state through 0x141DE5A10 when owner+0x24C8<3, commits packaged "
        "records through 0x141E0EDF0/0x141E0ED10, uses actor-graph helpers "
        "0x1418B7060/0x14189E5A0/0x14189CC80, and routes follow-up work "
        "through nested virtuals +0x178/+0x170/+0xB0.",
    ),
    0x14058BA40: (
        "ApplyCombatActionMode1SnapshotAndSelection",
        "Live branch inside 0x14058B900. Applies a full snapshot into "
        "owner+0x15B8 through 0x141E0EDF0, switches controller mode 1 through "
        "0x141E0ED10, then resolves actor-graph selection data through "
        "owner+0x1E0 and 0x1418B7060.",
    ),
    0x14058BC70: (
        "ApplyCombatActionMode2SnapshotAndFinalizeSelection",
        "Sibling live branch inside 0x14058B900. Applies a blended snapshot "
        "into owner+0x15B8, switches controller mode 2, then finalizes the "
        "nested-control side through 0x141DEF910 after selecting actor-graph "
        "data via 0x1418B7060.",
    ),
    0x14124ED20: (
        "PublishActorObserverRecordFromPrimaryRetainedPlayerAndScalarQuery",
        "Observer-style publish path built on the primary retained player. "
        "Resolves slot 0 through 0x1410F0D10, fetches the shared scalar query "
        "through 0x141DF0000, compares world-space transforms and thresholds, "
        "and when enabled publishes a structured record through the sink at "
        "0x143FDF520 via virtual +0xA8.",
    ),
    0x14124F700: (
        "PublishSiblingActorObserverRecordThroughSink90",
        "Sibling observer-style publish path. Resolves live actors from "
        "retained/runtime pairs, computes transform-derived thresholds, calls "
        "the shared scalar query 0x141DF0000, and when enabled publishes a "
        "structured record through the same sink object 0x143FDF520 but via "
        "virtual +0x90 before forwarding the result to 0x1411DF370.",
    ),
    0x140437F80: (
        "ResolveAndApplySpecialTaggedStateCandidateOrFallback",
        "Special-tag state-application helper. Builds owner transform, "
        "classifies it through 0x142145490, forwards the 0x2010 case into "
        "0x1404380F0 with tag 0xAC710200, otherwise resolves a candidate from "
        "global registries and applies it through 0x141DE4F90 when "
        "candidate->virtual +0xB0(owner) succeeds.",
    ),
    0x1404380F0: (
        "ResolveAndApplySpecialTaggedStateCandidateByTag",
        "Builds an owner transform, resolves a special-tag candidate object "
        "through 0x1409DA630(tag, transform), validates it through virtual "
        "+0xB0, and commits it through 0x141DE4F90.",
    ),
    0x140394B20: (
        "QueueVehicleControllerSetDriverEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'set-driver'. Captures target vehicle state through virtual +0xF8, "
        "packages role/index data, hashes the event string, and continues into "
        "the shared vehicle-controller commit path.",
    ),
    0x140395000: (
        "QueueVehicleControllerAddPassengerEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'add-passenger'. Builds the same event record shape as the driver "
        "path but for passenger entry, then continues into the shared "
        "vehicle-controller event commit path.",
    ),
    0x140395430: (
        "QueueVehicleControllerPassengerEnteredEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'passenger-entered'. Captures the current vehicle state and packages "
        "the passenger-entered event into the same shared controller path used "
        "by set-driver/add-passenger.",
    ),
    0x14059D740: (
        "BuildPassengerFrontEntryStateAndCommitVehicleControllerRecord",
        "Concrete passenger-entry builder. Validates a live actor, measures "
        "entry transforms, hashes the authored state 'hero-passenger-a-enter-"
        "front', stores it into controller state at +0xA0, writes a role-like "
        "value at +0xB0, updates entry flags at +0x12C, and commits entry "
        "transforms back into controller fields.",
    ),
    0x1406B19F0: (
        "UseTeleportIntoVehicleScriptPathFromVehicleActionRuntime",
        "Runtime user of the TeleportIntoVehicle script-skill family. Reads "
        "pending vehicle-action state, packages the TeleportIntoVehicle string "
        "block, and feeds it into the larger scripted vehicle-entry path.",
    ),
    0x1414115B0: (
        "NotifyPlayerEnteringVehicleAndPublishEntryRecordToActor",
        "VehicleCtrl::NotifyPlayerEntering path. Hashes 'vehicle-entry-def', "
        "builds packaged entry records from the incoming VehicleEntryDef, "
        "serializes them through 0x1417F8B00, and when a matching live actor "
        "is present publishes the resulting entry record through actor "
        "virtual +0xD0.",
    ),
    0x14059DAB0: (
        "SelectVehicleExitSideAndDispatchSeatStateUpdate",
        "Sibling vehicle seat-state method. Chooses between authored exit-side "
        "strings, uses the same seat/controller object as the passenger-entry "
        "builder, dispatches through nested control virtual +0xD0, and caches "
        "a resulting value/timestamp into object+0x48.",
    ),
    0x14059DC30: (
        "GateVehicleSeatStateByCapabilityHashAndDispatchNestedControlA0",
        "Small seat-state applicability gate. Requires the target actor to "
        "carry capability bit 0x143D43FA4, matches action hash 0x465123BF, "
        "then resolves nested control through owner+0x15C8->virtual +0x58 and "
        "tail-dispatches into nested virtual +0xA0.",
    ),
    0x14059F199: (
        "CallSeatStateExitSelectorOnEmbeddedVehicleSeatState",
        "Thin outer-owner wrapper. Resolves the embedded vehicle seat-state "
        "subobject at owner+0x690, forces the binary side flag to 1, and "
        "jumps into SelectVehicleExitSideAndDispatchSeatStateUpdate.",
    ),
    0x14059F1E0: (
        "UpdateVehicleActionOwnerAndRefreshEmbeddedSeatState",
        "Large outer vehicle-action owner update. Reads the embedded seat-state "
        "object at owner+0x690, queries nested control +0xB0 through "
        "owner+0x15C8, stores the resulting low-cardinality state in "
        "owner+0x69C, checks action/capability gates, and then continues into "
        "the larger owner runtime path.",
    ),
    0x14039EB80: (
        "ResolveQualifiedRailVehicleActorFromPackedPair",
        "Resolves a live actor from a packed retained/runtime pair and then "
        "requires the selector capability 'RailVehicle' through "
        "CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x14039EC50: (
        "ResolveQualifiedDriveableVehicleActorFromPackedPair",
        "Resolves a live actor from a packed retained/runtime pair and then "
        "requires the selector capability 'DriveableVehicle' through "
        "CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x1413571B0: (
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes",
        "Large player/HUD runtime update. Combines player-state booleans, "
        "CameraManager state, and authored scalars such as "
        "'grenade-throw-time', 'player-live-time', 'is-aiming', and "
        "'weapon-accuracy', then publishes them through object-local sinks "
        "around +0x1D98 and the big state block at +0xF0.",
    ),
    0x14009BC80: (
        "RegisterSelector_PlayerIsAimingFirearmAtNpc",
        "Registration stub for the authored selector "
        "'player-is-aiming-firearm-at-npc?'. Not a live runtime consumer.",
    ),
    0x14090AB80: (
        "UpdateBuddyMeleeCoordinatorAttackInternalAndPublishAttackRecords",
        "Large buddy attack-behavior path anchored by nearby strings "
        "'BuddyMeleeCoordinator::AttackInternal', 'pAttackList', and "
        "'Buddy Attacking Behavior'. Seeds the +0x1D90..+0x1E70 block, "
        "resolves an object into +0x1D98, and publishes structured attack "
        "records through the global sink 0x143FDF520 via virtual +0x90/+0xC8.",
    ),
    0x141356F80: (
        "UpdateHudRuntimePublishes_CameraInside_GrenadeMove_BlindVis",
        "HUD/runtime presentation path anchored by nearby strings "
        "'Camera Inside', 'GrenadeMove', 'vis: blind', 'vis: %s', and "
        "ghud.cpp. Publishes a structured record into the global sink "
        "0x143FDF520 via virtual +0x38.",
    ),
    0x14013A110: (
        "UpdateIggyAllocFreeTelemetryAndPublishSinkEvents",
        "Allocator/telemetry runtime path anchored by nearby strings "
        "'Iggy Alloc', 'Iggy Free', and flash allocator overflow warnings. "
        "Builds small structured records, publishes them into the global sink "
        "0x143FDF520 via virtual +0x28, and continues through local object "
        "virtuals +0x38/+0x90.",
    ),
    0x1403EF6D0: (
        "BufferQualifiedWeaponFireStateSampleInOwnerRuntimeArray",
        "Live runtime user of the 'weapon-fire' string id. Matches the incoming "
        "runtime id against the hashed 'weapon-fire' token, gates on actor "
        "capability bits and object+0x1DB8 virtuals, computes a derived vector "
        "sample, and stores the result plus retained/runtime metadata into the "
        "owner's buffered state array around +0xC00..+0xC30.",
    ),
    0x141436E70: (
        "ResolveWeaponFireObserverStateFromLiveRetainedPlayer",
        "Higher-level runtime user of the 'weapon-fire' token. Builds the same "
        "hashed id, pushes it through nearby runtime helpers, resolves retained "
        "player slot 0, and changes its qualification path when the multiplayer/"
        "split byte in dword_143E07470 is active. This proves split activation "
        "affects weapon observation, but does not prove slot-1 occupancy alone "
        "blocks aim/fire. It then computes a world-space result and forwards it "
        "through 0x141434DA0 with a toggled binary state.",
    ),
    0x1404CA550: (
        "EnumerateLivePlayerActorsForClimbChecks",
        "Collects valid Player actors. In campaign it reads retained slots 0 and "
        "1 directly; in multiplayer it scans the ten NetGameManager slots. Its "
        "only direct code caller is AiClimbController::IsBlocked, so this is not "
        "the local weapon/aim gate.",
    ),
    0x1404CEFC0: (
        "AiClimbControllerIsBlocked",
        "AiClimbController::IsBlocked. Enumerates live players through "
        "0x1404CA550 and tests Edge, EdgeAimNew, PeekOver and Melee geometry "
        "against them while evaluating a climb segment.",
    ),
    0x141432B90: (
        "ResolveOwnerWeaponObserverScalarByStateId",
        "Reads owner+0x624 as a current state id, resolves a cached "
        "'weapon-art-defs-online-hash' registry through 0x1414FA800, looks up "
        "the matching record via 0x141764B50 / 0x141434E90, and returns its "
        "leading float scalar with a fallback default when the record is absent.",
    ),
    0x141434E90: (
        "ResolveOwnerWeaponObserverRecordByStateId",
        "Pointer-returning sibling of 0x141432B90. Resolves the cached "
        "'weapon-art-defs-online-hash' registry, uses owner+0x624 as the key, "
        "and returns the matching state/observer record pointer.",
    ),
    0x141434DA0: (
        "UpdateOwnerWeaponObserverStateByModeFlag",
        "Reads owner+0x624 as the current state id, optionally hashes the "
        "authored token at 0x142A854B0 ('helicopter-igc-mg'), resolves a "
        "cached 'weapon-art-defs-hash' registry through 0x1414FA380, and "
        "routes the resulting state id through 0x141614E40 under a binary "
        "mode/toggle flag.",
    ),
    0x1414FA380: (
        "GetCachedWeaponArtDefsHashRegistry",
        "Cached accessor for the registry keyed by the '*weapon-art-defs-hash*' "
        "string family.",
    ),
    0x1414FA800: (
        "GetCachedWeaponArtDefsOnlineHashRegistry",
        "Cached accessor for the registry keyed by the "
        "'*weapon-art-defs-online-hash*' string family.",
    ),
    0x1414FA9A0: (
        "GetCachedWeaponDryFireEventRegistry",
        "Sibling cached accessor keyed by the 'frags-event-weapon-dry-fire' "
        "string family.",
    ),
    0x141614E40: (
        "LookupRegistryRecordPointerById",
        "Generic id->record lookup over a registry with record_count at +0x0, "
        "id table at +0x8, and record pointer table at +0x10. Uses open-"
        "addressing style probing and returns the matched record pointer.",
    ),
    0x1414FAC10: (
        "GetWeaponObserverRecordField00ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x0.",
    ),
    0x1414FAC50: (
        "GetWeaponObserverRecordField08ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x8.",
    ),
    0x1414FAC90: (
        "GetWeaponObserverRecordField10ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x10.",
    ),
    0x1414FACD0: (
        "GetWeaponObserverRecordField18ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x18.",
    ),
    0x1414FAD10: (
        "GetWeaponObserverRecordField20ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x20.",
    ),
    0x1414FAD50: (
        "GetWeaponObserverPrimaryObjectField2B0ByStateId",
        "Resolves the '*weapon-art-defs-online-hash*' record by state id and, "
        "if record+0x0 is a live object pointer, returns that object's +0x2B0 "
        "field.",
    ),
    0x141520750: (
        "BuildUsingJointDifferenceStateRecord",
        "Builds a large structured state record from input vectors, the current "
        "actor/runtime pair, and a downstream registry record. Used at the tail "
        "of the higher 'weapon-fire' observer path after owner-local observer "
        "state has been updated through 0x141434DA0.",
    ),
    0x140A40A30: (
        "BuildUsingJointDifferenceStateRecordAndResetWrapperTail",
        "Thin wrapper over 0x141520750. Builds the shared UsingJointDifference "
        "record, stores one extra pointer at +0x130, and clears/defaults tail "
        "fields around +0x140/+0x150/+0x160/+0x170.",
    ),
    0x141520890: (
        "ProcessUsingJointDifferenceStateRecordForCalcStartPhase",
        "Heavy downstream consumer of the shared UsingJointDifference record "
        "family. Anchored by nearby authored tokens 'calc-start-phase-*' and "
        "'DcCalcStartPhaseUsingJointDifference'; reuses actor/runtime pair "
        "queries and continues into a larger produced-state/application path.",
    ),
    0x140A40AB0: (
        "SelectSpawnerOrGroupEntryByValue",
        "Searches spawner/group-related tables and a fallback lookup path to "
        "resolve a matching entry/index for the requested value. Separate from "
        "the real UsingJointDifference start-phase transition family.",
    ),
    0x1417F8910: (
        "PublishStructuredStateBlockToActorVirtualD0",
        "Actor-facing publish/apply helper. If the target is valid and not "
        "blocked by target+0x40 bit 0x1, it acquires/releases guards through "
        "0x1416A2E80/0x1416A2ED0 and forwards the structured block through the "
        "target object's virtual +0xD0; otherwise it writes the fallback block.",
    ),
    0x1402EF0A0: (
        "ConsumeResolvedPhaseStateAndForwardToRuntimeHandler",
        "Heavy runtime consumer that starts from a 0x1417651D0-resolved record "
        "id and vector payload, packages several actor/runtime pairs plus a "
        "large phase/state block, resolves live actor identity, and forwards "
        "the result into 0x1402EF420.",
    ),
    0x1402EF420: (
        "ApplyPhaseTransitionPayloadToRuntimeState",
        "Heavy downstream apply path for the shared UsingJointDifference / "
        "calc-start-phase family. Selects branch/state ids from record fields "
        "+0x90/+0x94/+0x9C/+0xA0/+0xA8, rebuilds world-space phase/orientation "
        "bases, dispatches transition instructions, writes actor-side "
        "transition fields, and fans follow-up logic across retained player "
        "slots 0 and 1.",
    ),
    0x1402EE240: (
        "BuildPhaseTransitionInstructionDispatchRecord",
        "Builds a large phase-transition dispatch record from the incoming "
        "state block, hashes local descriptor tokens, resolves a live "
        "actor/runtime pair, and prepares the instruction payload later "
        "consumed by 0x1402EF420.",
    ),
    0x1401781E0: (
        "BuildWorldBasisVectorFromRuntimeTransform",
        "Resolves a runtime object through the retained/runtime table at "
        "0x14386FD00, copies its cached transform at +0x10 when valid, and "
        "emits a world-space basis vector from the object's orientation and "
        "translation.",
    ),
    0x1415D23B0: (
        "BuildNormalizedPhaseBasisFromTwoVectors",
        "Normalizes and cross-combines two incoming vectors into a packed "
        "phase/orientation basis block used by the downstream transition "
        "application family.",
    ),
    0x142177E50: (
        "BuildInterpolatedVectorFromThresholdRecord",
        "Converts a packed threshold/range record into an interpolated vector "
        "block using fields around +0x0/+0x4/+0x8/+0x10/+0x14/+0x18/+0x20/"
        "+0x24/+0x28. Used directly under 0x1402EF420.",
    ),
    0x141BE20C0: (
        "DispatchPhaseTransitionInstructionListToActorState",
        "Consumes a live actor/runtime pair plus a small instruction list, "
        "iterates 0x20-byte entries with type-like values 0/1/2, and routes "
        "them into nested transition writers such as 0x141BE1E60 or recursive "
        "self-dispatch.",
    ),
    0x141BE2D90: (
        "WriteActorTransitionTargetVector50_58",
        "Resolves the target actor through a runtime pair, sets bit 0x8 in the "
        "actor-side transition record, and writes three floats to +0x50/+0x54/"
        "+0x58.",
    ),
    0x141BE2E70: (
        "WriteActorTransitionScalar5C",
        "Sibling actor-side transition writer that resolves the same runtime "
        "pair, sets bit 0x8, and writes a scalar to +0x5C.",
    ),
    0x141BE2F50: (
        "WriteActorTransitionScaledShort2C",
        "Sibling actor-side transition writer that resolves the same runtime "
        "pair and writes a scaled short/integer field to +0x2C.",
    ),
    0x1417423B0: (
        "BuildPhaseTransitionBasisFromObjectTransforms",
        "Rebuilds a world-space phase/orientation basis from two object-backed "
        "transforms plus scalar parameters, then returns the derived block "
        "through 0x141742290.",
    ),
    0x141BE4EA0: (
        "FinalizeActorPhaseTransitionRecordFromBasis",
        "Consumes the rebuilt phase payload plus live actor/runtime data, "
        "recomputes orientation/basis state, queries actor-side objects through "
        "virtual +0x168, and finalizes a structured transition record through "
        "0x141BDFE70.",
    ),
    0x141BDFE70: (
        "ResolveActorPhaseTransitionResultState",
        "Actor-side phase-transition result resolver/finalizer. Consults owner "
        "state around +0x960/+0x987/+0x990/+0x998/+0xA28, queries authored "
        "state through 0x141764B50/0x141765030/0x14209A8B0/0x14209A740, emits "
        "explicit status codes, and may recurse into itself or sibling "
        "0x141BDF510 for multi-entry transition selection.",
    ),
    0x141BDF510: (
        "AcquireParticleRootDataSlotForTransitionEffect",
        "Sibling helper under the phase-transition family that descends into "
        "ParticleManager/ParticleRootData allocation. Computes a slot-like "
        "index from owner+0x58, probes owner-local bitsets around +0x3B0/"
        "+0x430/+0x4B0/+0x630, acquires a claim path around owner+0x390, and "
        "drives ParticleRootData allocation/retiring/active assertions.",
    ),
    0x141F84640: (
        "DispatchRetainedSlotPhaseFollowupIntoCameraShakePath",
        "Retained-slot follow-up path reached from 0x1402EF420 after phase "
        "state application. Gates on global runtime state, checks per-slot "
        "thresholds, packages records through 0x1417C3790, and continues into "
        "camera-shake / explosion-shake / shoot-shake follow-up via "
        "0x141BEB0F0.",
    ),
    0x141BEB0F0: (
        "DispatchCameraShakeOrExplosionShakeFromPhaseFollowup",
        "Downstream camera-shake/explosion-shake dispatcher under the retained-"
        "slot phase follow-up branch. Resolves effect records and routes into "
        "0x141BE8FF0/0x141BE9AE0/0x141BE9B90 using global effect registries "
        "rooted around 0x146921700 and 0x146922710.",
    ),
    0x141F83CC0: (
        "QueueOrRefreshCameraShakeEntryBySlotAndSource",
        "Maintains capped per-slot camera-shake entries in global arrays around "
        "0x146AA5A20/0x146AA5A28. Updates or appends timing/strength/source "
        "records for the camera-shake follow-up path.",
    ),
    0x14209A740: (
        "ResolveOwnerAuthoredStateNodeById_Primary90",
        "Owner-local dual-hash resolver used under 0x141BDFE70. Resolves a "
        "state node by id through the primary hash rooted at owner+0x90 and "
        "cross-validates an optional linked node through the secondary hash "
        "family at owner+0x398/+0x428.",
    ),
    0x14209A8B0: (
        "ResolveOwnerAuthoredStateNodeById_Primary98",
        "Sibling owner-local dual-hash resolver used under 0x141BDFE70. "
        "Resolves a state node by id through the primary hash rooted at "
        "owner+0x98/+0x128 and validates the linked object at node+0x18 "
        "through the secondary hash family at owner+0x398/+0x428.",
    ),
    0x141EF59C0: (
        "FilterPrimary90AuthoredStateNodesByModeMasksAndBuildObserverPayload",
        "External consumer of 0x14209A740. Iterates authored-state ids, "
        "filters resolved nodes by long capability/category masks at node+0x30 "
        "using mode-specific constants 0x200000000000 / 0x100000000000, then "
        "builds downstream observer payload objects via "
        "0x14036BD80/0x1415A0E90/0x14163F930.",
    ),
    0x141F564E0: (
        "GateObserverPublishPathByPrimary98AuthoredStateBit3",
        "External consumer of 0x14209A8B0. Canonicalizes an authored-state id, "
        "resolves the linked node, extracts bit3 from node+0x12, and uses it "
        "as an early enable/override gate before the larger observer/publish "
        "runtime path continues.",
    ),
    0x141BDC880: (
        "ConsumeTransitionResultAndCacheResolvedEntityPair",
        "Actor-local direct consumer of 0x141BDFE70. Validates the returned "
        "(ptr,id,status)-style transition record, marks the resolved object "
        "with bit 0x10, caches the transition result into the local consumer, "
        "and mirrors or resolves a live entity handle/id pair into a sibling "
        "slot.",
    ),
    0x141BDFFD0: (
        "IterateAuthoredTransitionIdsAndRefreshLocalTransitionState",
        "Looped direct consumer family around 0x141BDFE70. Rebuilds structured "
        "transition inputs from authored id arrays and refreshes local owner "
        "state around +0x984/+0x990/+0x998/+0xA28 from the returned transition "
        "results.",
    ),
    0x141BE07F0: (
        "IterateGlobalAuthoredTransitionIdsAndRefreshSingletonState",
        "Global/singleton sibling of the local 0x141BDFFD0 family. Invokes "
        "0x141BDFE70 through the global owner context rooted at 0x146920CA0 "
        "and refreshes global transition state around "
        "0x146921624/0x146921630/0x146921638/0x1469216C8.",
    ),
    0x141BE4DF0: (
        "BuildCompositeTransitionResultFromResolvedEntityState",
        "Higher transition-result builder over 0x141BDFE70. Seeds one result "
        "slot, derives extra entity/runtime state through validated handle "
        "pairs and entity->virtual+0x168, then feeds the derived state into a "
        "second transition-result resolution pass.",
    ),
    0x141BD4DD0: (
        "InitializeParticleManagerGlobals_920CA0_920CF0_920CF8",
        "Anchors to ParticleManager::Init in shared/src/gamelib/render/"
        "particle/particle.cpp. Initializes the global ParticleManager-owned "
        "context at 0x146920CA0, the separate manager-owned authored-state/"
        "event resolver at 0x146920CF0, and the compact short-id helper at "
        "0x146920CF8.",
    ),
    0x141BD8BE0: (
        "ParticleManager_UpdateEvents_FilterAuthoredStateNodes",
        "ParticleManager::UpdateEvents-side consumer of the global "
        "0x146920CA0/0x146920CF0/0x146920CF8 family. Iterates manager-owned "
        "entries, resolves authored-state ids through 0x14209A740, filters "
        "them by node+0x30 mask under the current manager mode, and feeds the "
        "resulting structured state into 0x141BE4EA0.",
    ),
    0x141BDF020: (
        "RefreshParticleManagerFrontObject",
        "Small helper over the ParticleManager-owned global context family. "
        "Clears an optional owned object at context+0x20, queries the current "
        "front object through 0x14160C120, and conditionally routes into the "
        "deeper manager-owned runtime gate around 0x141A81A90.",
    ),
    0x141A81B80: (
        "CheckContext130PolicyBitsAgainstNode30Masks",
        "Shared policy helper used from the ParticleManager-owned branch. "
        "Reads policy bits 8/9 from context+0x130 and tests them against the "
        "long authored-state capability masks 0x100000000000 / "
        "0x200000000000 at node+0x30, returning a boolean gate.",
    ),
    0x141BDC640: (
        "ProcessPendingParticleRootTransitionEntriesAndConsumeTransitionResults",
        "Large local processor above 0x141BDFE70 inside the same ParticleManager "
        "/ ParticleRootData owner family. Walks a pending-entry cluster at "
        "owner+0x668/+0x670/+0x6F0/+0x6F8, resolves authored-state ids through "
        "owner_context+0x50 -> 0x14209A8B0, falls back into "
        "0x141D21F90/0x141D231E0 when needed, then consumes and caches "
        "transition results or unlinks cleared entries from the local list.",
    ),
    0x141BDCED0: (
        "RefreshParticleRootOwnerRuntimeObject30AndDerivedStateBytes",
        "Large local refresh path in the ParticleManager/ParticleRootData owner "
        "family. Rebuilds/refreshes the runtime subobject rooted at owner+0x30, "
        "copies a broad state image from owner fields around +0x994..+0xA2C, "
        "updates derived flags/counters around +0x987..+0x98C/+0x9C1, and feeds "
        "the same local owner family later consumed by 0x141BDC640.",
    ),
    0x141BDD400: (
        "ConstructParticleRootOwnerRuntimeSubobject30AndSeedLinkedStateBlocks",
        "Constructor/builder for the large local runtime/root subobject stored "
        "at owner+0x30 in the ParticleManager owner family. Also seeds linked "
        "owned blocks around owner+0x1E0/+0x1F8/+0x200 before the later "
        "refresh/transition paths consume them.",
    ),
    0x141BDD3E0: (
        "InvokeQueuedOwnerRuntimeRefreshCallbackRecord",
        "Thin callback adapter over a compact 0x18 record. Reads owner/context "
        "at +0x0/+0x8 and a byte flag at +0x10, then tail-calls into "
        "0x141BDDbe0.",
    ),
    0x141BDDBE0: (
        "RefreshAndCommitParticleRootOwnerRuntimeShell",
        "Central refresh/commit wrapper for the current ParticleManager local "
        "owner family. Derives local flags around +0x987..+0x98C, builds "
        "owner+0x30 via 0x141BDD400, refreshes it via 0x141BDCED0, runs "
        "follow-up local methods, then commits the resulting runtime object and "
        "staging state back into the outer shell object at owner->0.",
    ),
    0x140164420: (
        "InitializeQueuedOwnerRuntimeRefreshCallbackRecord_Global",
        "Global/bootstrap producer for the compact callback record consumed via "
        "0x141BDD3E0. Allocates a 0x18 record, stores owner/context at +0x0/+0x8 "
        "and a byte flag at +0x10, then publishes callback target 0x141BDD3E0.",
    ),
    0x141BDE160: (
        "BuildQueuedOwnerRuntimeRefreshCallbackRecord_Local",
        "Local producer for the same compact callback-record form used by "
        "0x141BDD3E0. Stores owner/context pointers and byte flag, then routes "
        "execution toward the central wrapper 0x141BDDBE0.",
    ),
    0x141BDE250: (
        "ResetAndRebuildParticleRootStateBanksAndQueues",
        "Local reset/rebuild pass under the central 0x141BDDBE0 wrapper in the "
        "ParticleManager family. Clears outer-shell staging pointers, zeroes "
        "owner+0x630 and owner+0x430, rebuilds missing state entries through "
        "0x141BD7D00, and continues through root-data list/queue state around "
        "+0x798/+0x818/+0x828.",
    ),
    0x141BD79D0: (
        "ParticleManager_FreeRootDataAndPromoteBitsetState",
        "FreeRootData-side state/bank update in the ParticleManager family. "
        "Scans owner+0x5B0, cross-checks presence bits in "
        "owner+0x3B0/+0x430/+0x4B0/+0x530, updates the live root-data record "
        "from owner+0x58, then writes back promoted bits and marks owner+0x98E.",
    ),
    0x141BD8020: (
        "ParticleManager_CreateRenderDataFromRootStateBanks",
        "CreateRenderData-side derived-batch builder for the same particle root "
        "bank family. Starts from owner+0x3B0, validates consistency against "
        "owner+0x430 and owner+0x630, resolves live root-data records through "
        "owner+0x58, and writes compact tuples into produced helper storage.",
    ),
    0x141BD74B0: (
        "ParticleManager_UpdateFreeRetiredRootDataFromPendingBitsetBanks",
        "Early gate over the particle root-state bank family. Starts from "
        "owner+0x4B0 with fallback into owner+0x528, derives the next candidate "
        "slot index, checks corresponding presence bits, and advances into the "
        "free/retired root-data update pass.",
    ),
    0x140EF6EB0: (
        "BuildMoveModeTransitionRecord_PlayerEnterBalanceMode_GlobalPath",
        "Gameplay-facing runtime producer that hashes the literal "
        "`move-mode-transition`, pairs it with `player-enter-balance-mode`, "
        "resolves/publishes a small structured record through 0x143682D30 and "
        "0x1417D5F50, then seeds a follow-up runtime object/reference slot.",
    ),
    0x140F7D220: (
        "BuildMoveModeTransitionRecord_PlayerEnterBalanceMode_IndexedPath",
        "Sibling gameplay-facing move-mode transition producer. Walks a local "
        "indexed owner/list, then hashes the same `move-mode-transition` + "
        "`player-enter-balance-mode` pair and builds the same style of small "
        "structured runtime record.",
    ),
    0x141271D20: (
        "MatchPlayerMeleeButtonAndGrappleReactionTransitionHashes",
        "Gameplay-facing matcher over explicit player/melee button strings. "
        "Checks hashed names including melee-r1-hold/release, "
        "melee-lmouse-release, melee-rmouse-hold/release, and "
        "`transition-player-into-appropriate-grapple-reaction`, then routes "
        "toward the local melee command queue / command-dispatch family.",
    ),
    0x141272860: (
        "DumpMeleeCommandQueueDebugView",
        "Large debug/inspection path for the same owner family as the melee "
        "command queue. Anchors on the banner "
        "`============== Melee Command Queue ==============` and "
        "`*melee-button-names*`, then iterates/prints structured queue state.",
    ),
    0x141272A50: (
        "DispatchPlayerMeleeButtonHoldReleaseAndGrappleReactionCommands",
        "Live gameplay-facing dispatcher over the melee command queue family. "
        "Probes active button/transition hashes through 0x141E39D50 and, on "
        "match, either inserts queued command entries through 0x141273FA0 or "
        "updates existing entry scalar state through 0x1412743F0. Entry from "
        "PlayerMeleeController tick 0x141279DF0 with (controller+0x150, player).",
    ),
    0x141279DF0: (
        "PlayerMeleeController_DispatchQueuedButtonCommandsTick",
        "Once per engine-clock tick on PlayerMeleeController: reads player backref "
        "at +0x108, compares +0x400 clock stamp, then calls 0x141272A50 on the "
        "queue base at controller+0x150.",
    ),
    0x141279E50: (
        "MeleeCommandQueue_ResolveCommandAgainstPlayer",
        "Vtable +0x1F8 queue resolver. Forwards owner+0x150 queue state, the "
        "requested command descriptor, and owner+0x108 Player to 0x1412712D0.",
    ),
    0x14127A250: (
        "MeleeCommandQueue_QueryMeleeR1",
        "Vtable +0x220 query: hashes literal `melee-r1`, stores the command id "
        "through its int* argument, then invokes the queue resolver at +0x1F8.",
    ),
    0x141273FA0: (
        "PushOrRotateMeleeCommandQueueEntry",
        "Maintains a 16-slot queue of 0x40-byte melee/action command entries. "
        "If requested and an identical command id already exists with zero "
        "secondary scalar, updates that entry in place; otherwise shifts older "
        "entries backward and inserts a new front command containing payload "
        "vectors, scalar values, and a small byte state.",
    ),
    0x1412743F0: (
        "UpdateExistingMeleeCommandQueueEntryScalar",
        "Walks the same 16-slot melee command queue as 0x141273FA0, finds a "
        "matching command id, and updates only the secondary scalar at "
        "slot+0x34.",
    ),
    0x14183E2D0: (
        "Joypad_IsKeyboardMouseInputModeActive",
        "Returns false when the global input manager suppresses PC input; "
        "otherwise returns Joypad+0xBE1, maintained by the native input poll "
        "from keyboard/mouse versus gamepad activity.",
    ),
    0x141274470: (
        "ConstructMeleeCommandQueueOwner",
        "Constructor for the large owner object behind the explicit melee "
        "command queue. Installs vtable 0x142A6C2E8, zeroes the queue/control "
        "state, and anchors on the banner "
        "`============== Melee Command Queue ==============`.",
    ),
    0x1412746B0: (
        "InitializeProcessNpcMeleeManagerOwnerState",
        "Large melee/gameplay manager initializer anchored by the literal "
        "`ProcessNpcMeleeManager`. Seeds external references at "
        "owner+0x108/+0x110/+0x130, resolves/caches authored ids including "
        "`player-attacks`, `player-throws`, `*camera-melee-settings*`, "
        "`*camera-melee-settings-net*`, and `stealth-edge`, builds another "
        "owned runtime object at owner+0x820, and initializes broad local "
        "melee state around +0x6C0/+0x6D0/+0x6F0/+0x7B8/+0x840/+0x854/+0x860.",
    ),
    0x141274E20: (
        "TeardownProcessNpcMeleeManagerOwnerState",
        "Teardown/reset path on the same 0x142A6C2E8 ProcessNpcMeleeManager "
        "family. Releases owned object +0x130 and owned runtime object +0x820, "
        "then drops back to the smaller common base layout.",
    ),
    0x141274EC0: (
        "MarkCachedProcessNpcMeleeManagerTargetEntityActive",
        "Small cached actor-pair helper in the ProcessNpcMeleeManager family. "
        "Checks cached pair/state at +0x138/+0x140 and, when the object id "
        "matches, marks the resolved live entity active via entity+0x72.",
    ),
    0x141283A20: (
        "DestroyProcessNpcMeleeManagerOwnerState",
        "Freeing-dtor style sibling for the 0x142A6C2E8 ProcessNpcMeleeManager "
        "family. Reuses the same release logic for +0x130 and +0x820, then "
        "optionally frees the outer 0x870 owner object.",
    ),
    0x14125C600: (
        "PublishMountedMeleeState1F68ThroughCameraAndPresentation",
        "Large live path over outer_owner+0x1F68. Marks the local state active, "
        "pulls state data through virtual +0x48, packages actor/runtime pairs, "
        "then publishes through the primary screen-route entity / 0x141C0DFD0. "
        "Later uses virtual +0x60 on the same mounted object as a gate.",
    ),
    0x14125E650: (
        "SelectMountedMeleeStateBetween1F68_1F70_AndQueueOwner1F60",
        "Large sibling control-flow path over the mounted melee graph. "
        "Consults outer_owner+0x1F70 through virtuals +0x40/+0x48/+0x38/+0x30, "
        "mixes that with outer_owner+0x1F68, outer_owner+0x1D98, and the queue "
        "owner at +0x1F60 (virtual +0x210), then chooses the follow-up melee/"
        "control path.",
    ),
    0x140DE5CC0: (
        "ConstructMountedPlayerFallAndFreeRopeState1DF8",
        "Constructor for the mounted outer_owner+0x1DF8 object. Installs "
        "0x142A1F690 and seeds hashed/authored state from player-fall/free-rope "
        "anchors such as DiveFall, run^fall-high, desired-fall-state, "
        "free-rope-grip, and time-in-air.",
    ),
    0x140E14490: (
        "ConstructMountedEdgeRopeStaffStateSet1E58",
        "Constructor for the mounted outer_owner+0x1E58 object. Installs "
        "0x142A216C8 and seeds a broad authored edge/corner/wall-rope/"
        "free-rope/staff state set used by the same outer player-control graph.",
    ),
    0x140FCCD20: (
        "ConstructMountedWaterCurrentAndSlideState1E20",
        "Constructor for the mounted outer_owner+0x1E20 object. Installs "
        "0x142A3C490, builds a mounted 0x130 child at +0x138, and anchors to "
        "water-current / slide-barrier / player-slide-barrier-settings / "
        "default-water-forces data.",
    ),
    0x140E4ECA0: (
        "InitializePlayerMeleeControlCoreSubobject_FromOuterOwner1D60",
        "Initializer for the large embedded block under the outer root owner at "
        "+0x1D60. Seeds the 0x142A26CB0 family inside the same rdata region as "
        "player-melee-control.cpp and resets a large player-facing control/"
        "observer state block.",
    ),
    0x1410CA720: (
        "AssembleLargePlayerOwnerSubsystemGraph",
        "Large assembly pass that mounts a long run of player-facing subobjects "
        "into one outer owner. This same pass wires player-melee-control, "
        "look-at IK, inspect/investigate, perch, bump-bend-align, flame/"
        "flashlight, drag-edge, drag-rope, fall, slide, and other mounted "
        "player-control subsystems.",
    ),
    0x140E5DDF0: (
        "ConstructPlayerControlRootOwner1D60_InPlace",
        "Standalone in-place constructor for the 0x142A268C0 root owner family "
        "that later lands in outer_owner+0x1D60. Installs the root table, "
        "initializes the embedded +0x120 player-melee-control core through "
        "0x140E4ECA0, and seeds the same large state block later seen in the "
        "outer assembly pass.",
    ),
    0x1410484F0: (
        "ConstructMountedPlayerInvestigateReadableOwner1E98",
        "Large in-place constructor for the mounted outer_owner+0x1E98 Player "
        "subsystem. Current best read is the Player-investigate/read-mode/"
        "readable-object owner: it lives in recurring per-player update loops "
        "through virtuals +0x430/+0x438/+0x220, and its virtual +0x208 already "
        "participates in Player-tagged gameplay gating.",
    ),
    0x140DCB1A0: (
        "ConstructMountedPlayerCoverEvadeOwner1EB0",
        "Constructor for the mounted outer_owner+0x1EB0 Player subsystem. "
        "Current best read is the Player cover/evade/dodge-roll owner: "
        "surrounding authored/runtime anchors include player-cover.cpp, "
        "player-cover-actions.cpp, player-cover-states.cpp, player-evade.cpp, "
        "use-surface-type-probe, monkey-base, and frags-event-dodging-rolling.",
    ),
    0x140CEDCA0: (
        "ConstructMountedPlayerSubsystem1EE0_Size320",
        "Constructor for the mounted outer_owner+0x1EE0 Player subsystem. "
        "Current best read is a live sibling/activation partner for the same "
        "cover-evade family as 1EB0, but the label stays slot-level until the "
        "class role is pinned harder.",
    ),
    0x14105E220: (
        "ConstructMountedPlayerSubsystem1E68_Size340",
        "Constructor for the mounted outer_owner+0x1E68 Player subsystem. "
        "Semantic role is still unresolved; current value is structural, as "
        "this sits in the same dense Player-side region as investigate/read-"
        "mode and cover/evade owners.",
    ),
    0x141301710: (
        "ConstructMountedPlayerSubsystem1DA0_Size52C0",
        "Constructor for the mounted outer_owner+0x1DA0 Player subsystem. "
        "This is a major Player-side owner by size and placement in the real "
        "Player assembly pass, but it remains semantically unresolved.",
    ),
    0x1410B4B10: (
        "ConstructMountedPlayerSubsystem1D88_Size4B0",
        "Constructor for the mounted outer_owner+0x1D88 Player subsystem. "
        "Builds repeated entry/state storage and helper blocks at +0x2C0 and "
        "+0x380; this matches its later role as a heavy readiness/blocker "
        "owner in the mounted Player qualification family.",
    ),
    0x140D64BF0: (
        "ConstructMountedPlayerSubsystem1DB8_Size15C0",
        "Constructor for the mounted outer_owner+0x1DB8 Player subsystem. "
        "Lives in the same broad Player interaction/combat/presentation band; "
        "kept slot-level until stronger semantic anchors are found.",
    ),
    0x140D64FB0: (
        "RefreshMountedPlayerCooldownAnchorFromOwnerState",
        "Initializes a tiny owner-local helper, probes actor-root state, and "
        "refreshes a cached baseline from owner->0xE0->0x10 when the local "
        "probe or scalar threshold requires it.",
    ),
    0x140D65030: (
        "EvaluateMountedPlayerQualificationHashesAndFallback",
        "Player-side qualification gate that consults outer_owner+0x1D88, "
        "outer_owner+0x1DB8, owner virtual +0x8C0, and fallback helper "
        "0x1410F75C0 before allowing the path.",
    ),
    0x140D65290: (
        "EvaluateMountedPlayerExecutionReadinessCluster",
        "Companion gate to 0x140D65030. Combines outer_owner+0x1D88, "
        "outer_owner+0x1E50, outer_owner+0x1DB8, and outer_owner+0x1D98 "
        "through a compact Player-side readiness/execution test.",
    ),
    0x1410F75C0: (
        "CheckOwner2E0HashAgainstKnownMountedPlayerActionFamilies",
        "Fallback hash-family checker used by 0x140D65030. Builds two known "
        "hash sets, asks the owner for a +0x2E0 record, and rejects paths "
        "when that record already belongs to one of the known action families.",
    ),
    0x140D654F0: (
        "EvaluateAggregateMountedPlayerExecutionPolicy",
        "Aggregate Player-side execution policy gate. Succeeds only when a "
        "family of lower threshold/cooldown checks stays clear, including "
        "0x140D65940, 0x140D65540, 0x140D65740, and 0x140D65B20.",
    ),
    0x140D65B20: (
        "CheckMountedPlayerCooldownWindowByRegistryScalar",
        "Resolves a hashed registry scalar, scales it, and compares it against "
        "the delta between owner->0xE0->0x10 and a cached helper baseline at "
        "+0x8. Used as the cooldown/window member of the mounted Player "
        "execution policy family.",
    ),
    0x1410DD0FC: (
        "InvokePlayerOwnerAssemblyFromPlayerInitChain",
        "Direct callsite where the higher-level player.cpp-anchored init path "
        "invokes 0x1410CA720. This is currently the strongest upward link from "
        "the mounted player-control graph into the real Player construction/"
        "initialization chain.",
    ),
    0x1410DC160: (
        "InitializeTopLevelPlayerOwnerFromProcessSpawnInfo",
        "Large top-level Player construction/init pass on the outer owner. "
        "RTTI/vtable layout now confirms this is a real Player virtual method at "
        "slot +0x58 of vtable 0x142A4BFE8. Seeds broad Player state, creates "
        "the owner-local +0x3BD0 runtime object through 0x1410DE520, invokes "
        "0x1410DD0FC -> 0x1410CA720 to assemble the mounted graph, and then "
        "continues higher-level finalize/setup work through +0x1DF0, +0x1A8, "
        "+0x1B10, +0x3D98, and +0x3C20.",
    ),
    0x1410DE520: (
        "InitializeOwnerLocalRuntimeObject3BD0_Size190",
        "In-place initializer for the owner-local runtime/control object "
        "stored into outer_player+0x3BD0 during the top-level Player init "
        "pass. Seeds vtable 0x142A4BFD8 and two repeated state bands with "
        "0x1000-sized capacity fields, making +0x3BD0 one of the strongest "
        "upper promotion/rebind anchors above the mounted graph. Its local "
        "+0x108 field belongs to this sibling owner-local object, not to the "
        "top-level Player descriptor/id pair later consumed by 0x140CC1450.",
    ),
    0x1410DE740: (
        "FinalizeTopLevelPlayerOwnerWithExternalSpawnState",
        "Thin Player-side finalize/apply wrapper adjacent to the +0x3BD0 "
        "initializer cluster. Logs through outer_player+0x3C20 using the "
        "player.cpp / Player::Player(void) / Player::AllocateAnimData(...) "
        "literal block, then does little beyond calling "
        "0x140CC1450(outer_player, external_state) and running the paired "
        "cleanup/finalize at 0x1415A1B80. This is therefore a stable Player-"
        "side wrapper around the stronger promotion seam below it.",
    ),
    0x1413C04A0: (
        "RunSecondaryRetainedPlayerSpawnAndNotifyOuterOwner240",
        "Independent upper runtime caller of 0x1410F1910. Builds local retained "
        "spawn state, publishes a large local retained/runtime snapshot through "
        "0x141C88050, validates the returned retained/live pair, and if valid "
        "dispatches outer-owner virtual +0x240 with the incoming context. "
        "Sibling upper methods around 0x1413BC7E0 reuse the same owner family "
        "with virtual +0x238 and +0x258, and RTTI now pins that family to "
        "TaskManager / TaskManagerPlayMovie. The slot family is therefore a "
        "real retained-notify surface, but not a direct Player promotion hook: "
        "+0x240 is flashlight-oriented and +0x238 is menu/main-menu bootstrap. "
        "Surrounding literals tie this path to TaskManager::SpawnPlayer and "
        "the checkpoint/continue branch of task-manager.cpp.",
    ),
    0x1413BF7B0: (
        "TaskManager_PublishRetainedFlashlightRuntimeRecords_Slot240",
        "TaskManager / TaskManagerPlayMovie virtual +0x240 implementation. "
        "Consumes an incoming packet field at +0x14, validates live actor "
        "identity through the global retained slot table 0x143EEBE80, hashes "
        "`spawn-flashlight` and later `turn-on-flashlight`, then publishes "
        "structured records into live actors through virtual +0xD0. This is "
        "a real upper retained/runtime TaskManager path, but it is flashlight/"
        "state publication, not the direct Player promotion hook for Nadine.",
    ),
    0x1413C3310: (
        "TaskManager_BootstrapMenuAndMainMenuRuntimePublishOnce_Slot238",
        "TaskManager / TaskManagerPlayMovie virtual +0x238 implementation. "
        "One-time gated initializer that hashes `menu` and `main-menu`, "
        "resolves/creates manager-side records through the 0x141CBA1B0 / "
        "0x141CBAC10 / 0x141CBACC0 / 0x141CBAE10 family, and publishes a "
        "compact packet through 0x141CB95B0. This looks like a TaskManager "
        "menu/main-menu bootstrap/publish stage, not a Player ownership "
        "binder.",
    ),
    0x1413BCAE0: (
        "TaskManagerPlayMovie_ActivateRetainedCameraOverlayState_Slot258",
        "TaskManagerPlayMovie override for virtual +0x258. Touches global "
        "state around 0x143FE10D8, drives 0x1417D5F50, calls 0x1410F3DD0, "
        "and sets this+0x1F0=1, which makes it look like a specialized "
        "play-movie / retained-camera / overlay activation helper rather than "
        "a general Player promotion hook.",
    ),
    0x1410DE430: (
        "InitializeOwnerLocalFixedEntryTable1E_Size7A0",
        "Sibling upper-layer owner-local object initializer beside "
        "0x1410DE520. Zeros repeated pointer fields every 0x40 bytes, seeds "
        "object+0x10 = self+0x20, and sets object+0x18 = 0x1E, which looks "
        "like a fixed-entry table/list owned by the same top-level Player "
        "band above the mounted graph.",
    ),
    0x1410DE660: (
        "StampUpperPlayerVirtualFamilyVftableBFC0",
        "Tiny vptr stamper for the upper Player virtual-family object whose "
        "family block begins at 0x142A4BFC0. The surrounding assembly pass "
        "allocates this sibling upper-layer object and stores it at "
        "outer_owner+0x2018, which ties 0x1410DC160 and 0x1410DE740 to the "
        "same upper class family as the +0x3BD0 registry object. The raw "
        "table should be treated as a mixed upper-family block rather than a "
        "clean function-only vtable read.",
    ),
    0x1410C6BA0: (
        "GetUpperPlayerVirtualFamilyObject2018",
        "Trivial getter that returns outer_owner+0x2018. This pins +0x2018 as "
        "a first-class upper-layer runtime object in the same Player band as "
        "+0x3BD0 rather than a transient setup pointer.",
    ),
    0x140CBFB30: (
        "InitializeSharedBaseCtorBeforePlayerVftable",
        "Shared base ctor reached first from 0x1410C8FD0 before the real "
        "Player vtable is stamped. Zero-initializes the common action/"
        "gameplay band, including +0x15B4/+0x15C8/+0x1680/+0x1748/+0x1808/"
        "+0x1B10, and initializes the embedded object at +0x1A20. It does "
        "not visibly seed the later descriptor/id pair at player+0x108/"
        "+0x10C.",
    ),
    0x140CBFF50: (
        "InitializeSharedPlayerPolicyStateFromUpperInitPacket",
        "Shared upper/base init stage beneath the visible Player-only phases. "
        "Consumes a compact incoming packet through arg3+0x28, copies packet "
        "fields into player+0x15A4/+0x15A8/+0x15AC/+0x15B0, merges packet "
        "bit0 into player+0x161A, allocates/builds player+0x1B10, and then "
        "continues through 0x141E994C0 while seeding broader shared state "
        "around +0x16A0/+0x1680/+0x16AC/+0x16D0/+0x1A8. A direct pass also "
        "shows a copy from shared source 0x143FE1088 into the upper Player "
        "band +0x18B0..+0x1910, including source+0x108 -> player+0x18E0; "
        "this still does not directly seed player+0x108/+0x10C.",
    ),
    0x1410C9F20: (
        "InitializeEmbeddedPlayerAuthoredDescriptorState100",
        "Small in-place initializer for the embedded Player subobject at "
        "player+0x100. Writes vtable 0x142AB08C8, clears late fields around "
        "+0x110/+0x122/+0x128, but does not itself seed the authored key later "
        "consumed at player+0x108/+0x10C.",
    ),
    0x1410C9F50: (
        "ZeroEmbeddedPlayerAuthoredDescriptorKey_8_10",
        "Tiny helper immediately adjacent to the embedded +0x100 initializer. "
        "Clears the key fields at object+0x8 and object+0x10 and returns the "
        "same pointer. No direct rel32 callers have surfaced yet, but the "
        "shape matches the hidden authored pair inside player+0x100.",
    ),
    0x1416860A0: (
        "RefreshEmbeddedPlayerAuthoredDescriptorState100FromSharedTable",
        "Consumes the embedded Player subobject rooted at player+0x100. Uses "
        "the authored key at +0x108/+0x10C to resolve 0x14386FD00, copies the "
        "matched 0x20-byte record into +0x110, then derives follow-up vector/"
        "transform state into +0x130/+0x140. In the real Player::Init path "
        "this is called very early with rdx sourced from incoming_packet+0x20, "
        "making player+0x100 the strongest current container for the hidden "
        "promotion pair.",
    ),
    0x1416858E0: (
        "BuildCompactDescriptorCarrierFromRuntimeObject50_54",
        "Builds a small carrier record from a runtime object that exposes "
        "dword +0x50 and word +0x54. Seeds carrier header words with "
        "0x7FCCCCCC, copies +0x50/+0x54 into the compact descriptor key at "
        "+0x8/+0xC when present, and clears the local transform/quaternion "
        "bands. In Player::Init this feeds 0x1416862B0 and refreshes the "
        "embedded authored descriptor state under player+0x100, not the "
        "canonical Player live pair at +0xA8/+0xBC.",
    ),
    0x141685B40: (
        "InitializeEmptyExtendedDescriptorCarrier",
        "Initializes the larger empty descriptor carrier used by the wide "
        "0x50-byte descriptor banks under Player bringup and sibling gameplay "
        "paths. Seeds the same 0x7FCCCCCC headers, zeroes local state, and "
        "copies the default vector bands from 0x1428805D0.",
    ),
    0x1416862B0: (
        "RefreshEmbeddedPlayerAuthoredDescriptorFromCompactCarrier",
        "Consumes the compact descriptor carrier built by 0x1416858E0 and "
        "refreshes the embedded Player authored-descriptor object rooted at "
        "player+0x100. Validates the keyed entry against 0x14386FD00, copies "
        "resolved records into +0x10/+0x20, rebuilds derived basis/vector "
        "state at +0x30/+0x40, and leaves the canonical Player live pair at "
        "+0xA8/+0xBC untouched.",
    ),
    0x141E7C600: (
        "CompareEmbeddedDescriptorCarrierOrderingAndWarn_141E7C600",
        "Small comparison/logging helper immediately after the ret stub at "
        "0x141E7C5F0. Compares two carrier objects through their virtual +0x28 "
        "results, logs a warning when ordering is unexpected, and returns a "
        "boolean-like permit byte. The real Player::Init call still targets "
        "the stub at 0x141E7C5F0, so this body is a sibling implementation, "
        "not the active early bringup step.",
    ),
    0x1413BF090: (
        "BuildExtendedRuntimeStateWrapperAndDispatchRetainedEntityCommand",
        "Upper retained/runtime helper that resolves an object through the "
        "global registry, builds a local extended packet through 0x1417C35A0, "
        "then dispatches it through 0x141D21C80 / entity virtual +0x538. This "
        "is one of the clearest retained-side producers of the same wrapper "
        "family later matched by the upper Player apply path.",
    ),
    0x140A90E50: (
        "GetNetGameManagerInlineSlotBand43D0",
        "Returns the inline 0x43D0 per-slot band at manager+0x3C0+idx*0x43D0. "
        "Runtime readback from slot 1 shows session/map/mode string state "
        "such as `NET_MAP_REMNANTS_FOREST_VIP_TDM`, so this is not the "
        "materialized tracker object used by the spawn path.",
    ),
    0x140A90E70: (
        "GetNetGameManagerTrackerPointerArrayBase",
        "Small getter for the pointer-family rooted at manager+0x3A0. Treat "
        "this as an upper pointer-array helper, not as the real inline slot "
        "object seam.",
    ),
    0x140A90E80: (
        "GetMeNetPlayerTrackerByScreenIndex",
        "Screen-indexed accessor in the NetGameManager family. The assert "
        "strings explicitly mention `NetGameManager::GetMeNetPlayerTracker`, "
        "but runtime probing showed that treating this as a direct safe "
        "tracker getter can yield code-like pointers instead of a live slot "
        "object. Keep it demoted below the explicit inline-slot helper "
        "0x140A90E50 until a clean tracker-object contract is proven.",
    ),
    0x140A90F40: (
        "FindNetPlayerTrackerByRuntimePlayerObjectAndPlayerHash",
        "Scans the two-slot NetGameManager tracker family for a tracker entry "
        "that matches the hashed `Player` token and the supplied runtime "
        "player object. This is a broader generic tracker lookup than "
        "GetMeNetPlayerTrackerByScreenIndex and appears all over gameplay code.",
    ),
    0x140A38FA0: (
        "DriveDualSlotNetPlayerBringupFromCtxA_WithCtxBAvailabilityGates",
        "Upper dual-slot bringup caller above 0x140A90550. It does inspect "
        "ctxB=0x143E0EF00 through 0x14194D5F0 and count/selection gates, but "
        "the actual AddOrUpdate calls at 0x140A392EC/0x140A3936A still pass "
        "rdx=&0x143E08C80 and source entries resolved from ctxA. This makes "
        "ctxB a gating/availability input here, not the direct source context "
        "for manager-side slot creation.",
    ),
    0x140A9AAD0: (
        "NetGameManager_RespawnPlayer",
        "Manager-side respawn path identified by the `NetGameManager::RespawnPlayer` "
        "string xref. Works on a live tracker object, stages fresh state into "
        "tracker+0x500..+0x540 and tracker+0x840/+0x844, then either hands "
        "control to NetPlayerTracker_StartOrQueueSpawnPlayer with edx="
        "tracker+0x4F8 or queues a deferred respawn through tracker+0xA00/"
        "+0xA04 with timing in tracker+0x4C0/+0x4C8.",
    ),
    0x140A90550: (
        "NetGameManager_AddOrUpdateNetPlayerTracker",
        "Heavy manager-side tracker bringup/update helper from the "
        "`NetGameManager::AddOrUpdateNetPlayer` string family. Scans the "
        "0x43D0 tracker array, allocates or selects a tracker object, seeds "
        "tracker state from incoming spawn/loadout data, writes payload into "
        "tracker+0x7C0..+0x7DF/+0x7E0/+0x7B8/+0x1DC/+0x7BC, writes the "
        "effective screen/player index into tracker+0x1E0, derives "
        "tracker+0x1E4 from the `NetCharacter-` token family, and stores the "
        "tracker in manager+0x3A0/+0x3B0 under the matching local-player "
        "conditions. Real callers now pin the low call shape as: rcx=global "
        "manager 0x143DD70C0, rdx=shared context 0x143E08C80, r8=source "
        "object, r9d=slot, stack arg5=flag. Also note that direct callers "
        "still apply additional post-call tracker initialization, so this is "
        "the low create/update worker rather than the whole stable bringup "
        "sequence by itself. PIVOT SEAM: resolves the source via ctxA "
        "(lea rcx,[0x143E08C80]; call 0x14194E510) at 0x140A9075E, NOT ctxB. "
        "Arg5 is an is-local-player flag: when set, slot+0x1DC is taken from a "
        "local-id counter byte at 0x14304172A (then incremented) and slot+0x1E8 "
        "records local-vs-remote. is_local varies per caller (0x140A58287->0, "
        "0x140A392EC->r15b, 0x140A3A02C/06E->r14b); the caller passing 1 is the "
        "ready local-second-player construction to mirror for campaign co-op.",
    ),
    0x140ACBE10: (
        "ConstructNetSharedContextBase_0x800Entries",
        "Base constructor used by the global shared-context objects at "
        "0x143E08C80 / 0x143E0EF00. Seeds a large 0x800-backed entry family, "
        "zeroes the embedded owner pointer at +0x8, and initializes the tail "
        "helper block through 0x140AC62D0.",
    ),
    0x140072A50: (
        "InitializeCtxAObjectAndInstallVtable_1429E61F0",
        "Early startup builder for ctxA at 0x143E08C80. Calls the shared "
        "base constructor 0x140ACBE10 and then installs the ctxA-specific "
        "vtable 0x1429E61F0.",
    ),
    0x14007C100: (
        "InitializeCtxBObjectAndInstallVtable_1429EF890",
        "Early startup builder for ctxB at 0x143E0EF00. Calls the shared base "
        "constructor 0x140ACBE10, zeroes the embedded owner field at +0x8, "
        "installs the ctxB-specific vtable 0x1429EF890, and seeds "
        "0x143E10E88.",
    ),
    0x140B1E050: (
        "ResetCtxBObjectAndClearPendingSelectionGlobals",
        "Global ctxB reset helper. Calls 0x14194AF00 on 0x143E0EF00 and then "
        "clears the staged pending-selection globals around "
        "0x143E10E80/0x143E10E90/0x143E10EC4..+0x10. It does not consume the "
        "incoming rcx and is not itself the owner-teardown path.",
    ),
    0x140B1E100: (
        "PublishCtxBPendingSelectionVector",
        "Small ctxB-side publisher for pending selection state. Marks the "
        "family dirty, stores the candidate pointer into 0x143E10E90, and "
        "optionally mirrors the vector block into 0x143E0F618..+0x38 before "
        "clearing 0x143E10EC8.",
    ),
    0x140B1E400: (
        "ProcessCtxBCandidateSyncAndStagePendingSelection",
        "Major ctxB lifecycle/synchronization pass. Scans candidate objects, "
        "compares them against the current-selected pointer at ctxB+0x18 "
        "(global 0x143E0EF18), and on mismatch stages a pending selection into "
        "0x143E10E90 plus 0x143E0F618..+0x38 before calling 0x140BF1FF0(1,0). "
        "This is a pending-selection publisher, not the final writer of "
        "ctxB+0x18.",
    ),
    0x140B1E890: (
        "RunCtxBSelectionStateMachineAndDispatchPendingCommit",
        "Large ctxB-side state machine keyed by 0x143E10E84. Walks ctxB/ctxA "
        "selection bands, publishes intermediate records through "
        "0x140A59540/0x140141840, and reaches the first confirmed pending "
        "commit handoff at 0x140B1ECBC.",
    ),
    0x140B1DF30: (
        "ReturnFixedCtxBSelectionToken_0xA",
        "ctxB vtable slot +0x28 from the concrete vtable installed by "
        "0x140B1DF80. Returns fixed token 0xA. This corrected the earlier "
        "misread that mapped ctxB->virtual +0x28 to 0x140B204E0.",
    ),
    0x140B1ECBC: (
        "DispatchCtxBPendingSelectionToGlobalCommitService",
        "First confirmed pending->commit handoff for ctxB. Loads the staged "
        "candidate from 0x143E10E90, calls *(0x143880318)->virtual +0x48 with "
        "(&ctxB=0x143E0EF00, pending=r8), then clears 0x143E10E90 and emits a "
        "follow-up packet through 0x142204E50. The remaining unknown is the "
        "service-side implementation of the +0x48 commit.",
    ),
    0x140B1EE70: (
        "DispatchFixedCtxBSelectionTokenThroughGlobalService28",
        "Active campaign request path for ctxB. First calls ctxB->virtual "
        "+0x28, which is the fixed-token slot 0x140B1DF30 returning 0xA, then "
        "forwards that token into *(0x143880318)->virtual "
        "+0x28(service, &ctxB, token, 1). This is the live request surface "
        "above the service-side commit family.",
    ),
    0x140B204E0: (
        "BuildHeavySelectionPayloadFromGlobalObjectList",
        "Neighboring heavy request/payload builder in the local ctx/service "
        "family. Computes a CRC-like token from a fixed authored string, folds "
        "in optional world/runtime data, iterates the global object list "
        "rooted at 0x143FE1128+0x3D0, and packages the result through "
        "0x141C9ED50. Current vtable decode no longer ties this worker to "
        "ctxB->virtual +0x28.",
    ),
    0x140B1E220: (
        "EmitCtxBSelectionOrOwnerSourceEvents_0x104",
        "ctxB-side notification/emission helper. With a direct source pointer "
        "it emits one 0x104 packet through 0x140A59540; without one, if "
        "ctxB+0x8 is bound, it first rebuilds the owner/fallback source list "
        "through 0x14194D5F0 and emits 0x104 for each non-current source. "
        "This is downstream of owner bind, not the bind itself.",
    ),
    0x140B21DE0: (
        "ResolveCtxBSecondPlayerSelectionForSplitRefresh",
        "Helper used by the split/local-player refresh family. Checks the "
        "ctxB source-count path through 0x14194CFB0, falls back through the "
        "sub-sign-in descriptor block at 0x143071008 when needed, and "
        "otherwise returns the active/pending ctxB selection. This keeps the "
        "stock split player-2 source tied to ctxB rather than a campaign-"
        "native Nadine path.",
    ),
    0x140B21E70: (
        "HasCtxBResolvedSourceWithFlag42",
        "Post-bind consumer gate for ctxB. Requires both ctxB+0x18 and "
        "ctxB+0x8, builds a source list through 0x14194D5F0(&ctxB, out, 0), "
        "resolves each entry through 0x14194E510, and returns success once a "
        "resolved object has byte +0x42 set.",
    ),
    0x140B21F10: (
        "IsCtxBSourceListUnavailableOrEmpty",
        "Companion post-bind consumer gate for ctxB. If ctxB+0x18 or ctxB+0x8 "
        "is missing it returns true/unavailable; otherwise it calls "
        "0x14194D5F0(&ctxB, out, 0) and returns false once at least one "
        "source entry is available.",
    ),
    0x140AEAAFC: (
        "NotifyCtxBOwner58ThenRebuildSources_0x114",
        "Live owner-consumer path for ctxB. If ctxB+0x8 is bound, calls "
        "owner->virtual +0x58(&ctxB, 1), then immediately rebuilds the ctxB "
        "source list through 0x14194D5F0. This is a strong downstream anchor "
        "for the real ctxB owner contract.",
    ),
    0x140835A70: (
        "DispatchCtxBOwner70ForIndexedSource",
        "Loads an indexed source entry, then if ctxB+0x8 is bound calls "
        "owner->virtual +0x70(&ctxB, source_id_from_entry+0xE8). This pins a "
        "second live method on the concrete ctxB owner interface.",
    ),
    0x14083E580: (
        "DispatchCtxBOwner70ForResolvedSource",
        "Sibling owner-consumer path that resolves a source entry and then "
        "calls owner->virtual +0x70(&ctxB, source_id). Another live user of "
        "the same bound ctxB owner interface.",
    ),
    0x1409F9B20: (
        "RunNetStartupCallbackThatBindsGlobalSelectionService",
        "Outer startup callback used by the net/front-end lifecycle. The "
        "restart path in 0x1409FBA60 takes this address and passes it through "
        "0x1415B43B0 -> 0x1415B4870 -> 0x1415B47C0 as a callback record. Its "
        "inner bringup branch at 0x1409FA94E installs the mode-dependent "
        "global selection service and performs the shared-context binds.",
    ),
    0x1409FA94E: (
        "InitializeModeDependentGlobalSelectionService_143880318",
        "Inner bringup block inside 0x1409F9B20. Installs a mode-dependent "
        "global selection service into 0x143880318 (and sibling slots "
        "0x143880300/0x143880320), binds it through 0x1409EB500 with an "
        "upper context packet, then runs the service virtual +0x8 bringup "
        "path. 0x1430641DC is the Net Init Error Testing selector. If init "
        "succeeds and 0x1430641DC!=0x14, startup keeps variant A; only "
        "failing init or 0x1430641DC==0x14 constructs variant B and "
        "overwrites 0x143880318. The later startup bind calls "
        "service->virtual +0x20 exactly three times: for 0x143E08C80, for "
        "0x143E0AC40, and for ctxB=0x143E0EF00 via rsi set at 0x1409FA6DE. "
        "This supersedes the earlier misread that startup skipped ctxB.",
    ),
    0x1409EB500: (
        "CopyUpperPacketHeaderIntoGlobalSelectionService",
        "Tiny startup helper used by the mode-dependent global selection "
        "service. Copies the first 0x20 bytes plus trailing dword from the "
        "upper packet in r8 into the live service object at 0x143880318 "
        "before the service virtual +0x8 bringup stage runs. Those exact "
        "owner+0x8..+0x28 bytes are what 0x14194D5F0 later clones into "
        "ctxB+0x1D00 for the single-entry fallback source path.",
    ),
    0x1409EE7D0: (
        "HandleFrontEndStageTokenForNetShutdown",
        "Upper token handler in the net/front-end lifecycle. Hash-matches the "
        "incoming stage token against strings including 'added-to-stage' and "
        "'popped'; on the matched transition it can drive the broad "
        "0x1409FD360 shutdown path.",
    ),
    0x1409EE9B0: (
        "RunMultiplayerFrontEndShutdownFlow",
        "Large multiplayer/front-end state machine above the global net "
        "shutdown path. References tokens such as 'ss-fmenu-multi-main', "
        "'PAGE_MULTIPLAYER_CONNECTING', and 'part-module/part-multiplayer', "
        "and calls 0x1409FD360 from that upper lifecycle band.",
    ),
    0x1409F78B0: (
        "HandleNetUpdateTokenAndMaybeRestart",
        "Upper event/token handler keyed by the hash of 'update'. Works "
        "through the 0x14383E560 interface (virtual +0x30/+0x38), and on the "
        "restart branch calls 0x1409FD360 before continuing into the paired "
        "net re-entry worker at 0x1409FBA60.",
    ),
    0x1409F7C60: (
        "HandleNetUpdateOrPoppedTokenAndMaybeRestart",
        "Sibling upper event/token handler above the same global shutdown "
        "family. Checks hashes for 'update' and 'popped', drives "
        "0x14383E560->virtual +0x40 on one branch, and can call 0x1409FD360 "
        "plus 0x1409FBA60 from that front-end/net lifecycle.",
    ),
    0x1409FBA60: (
        "NdiNetInit",
        "CONFIRMED by decoded strings: NdiNetInit from game/net/flow.cpp "
        "(!g_ndiStartNetInit, 'Trying to initialize net while it's active', "
        "PAGE_MULTIPLAYER_CONNECTING, popup_mp_connecting, pMainPageStack). "
        "This is the multiplayer net-init flow. Around 0x1409FD068 it builds an "
        "0x80-byte ndjob record with callback field 0 = 0x1409F9B20 (priority "
        "3), then schedules it via 0x1415B43B0/0x1415B4870/0x1415B47C0. The "
        "ctxB owner rebind is therefore gated behind multiplayer net-init; "
        "single-player campaign never runs this, which explains live "
        "ctxB+0x8 == 0.",
    ),
    0x1415B43B0: (
        "NdjobAllocWorkBatch",
        "ndjob (corelib/job/job.cpp) work-batch allocator from scheduler global "
        "0x1437E9700. Generic job system, not net-specific. Used by NdiNetInit "
        "before staging the 0x1409F9B20 record.",
    ),
    0x1415B4870: (
        "NdjobCopyJobRecords",
        "Copies one or more 0x80-byte ndjob job records into the batch. In the "
        "NdiNetInit path this stages the record whose callback field 0 is "
        "0x1409F9B20.",
    ),
    0x1415B47C0: (
        "NdjobFinalizeAndSubmitBatch",
        "Finalizes the ndjob batch and tails into enqueue 0x1415B4560. Callback "
        "is not invoked inline.",
    ),
    0x1415B4560: (
        "NdjobEnqueueToLane",
        "ndjob per-lane enqueue. Global 0x1437E9700 holds a pointer to ndjob "
        "scheduler state; lane array at state+0x4FC0, stride 0x2200, depth cap "
        "0x400. Priority-inserts the task-group pointer at lane+0xC8 (count at "
        "lane+0); lane id = group+0x4C, affinity mask = group+0x38; wakes "
        "workers via import 0x1425D6668. Drain side is the generic ndjob worker "
        "pool, which always runs.",
    ),
    0x1409FD360: (
        "ShutdownNetFrontEndAndTeardownGlobalSelectionService",
        "Broad net/front-end shutdown path, not just a local ctxB helper. "
        "Toggles the category-0x16 subsystem gates, resets ctxB/ctxA active "
        "entries, clears the bound owner globals at 0x143E0EF08/0x143E08C88, "
        "tears down 0x143880318 through virtual +0x18/+0x0, and clears the "
        "service aliases. Direct callers now sit in upper front-end/net "
        "transition handlers rather than the local ctxB object family.",
    ),
    0x1409FDD70: (
        "TeardownGlobalSelectionService_143880318",
        "Inner teardown block inside 0x1409FD360. Resets the concrete shared "
        "contexts, calls service virtual +0x18 and then virtual +0x0 when "
        "the service is live, and finally clears 0x143880318 plus the "
        "sibling alias at 0x143880300.",
    ),
    0x140072A54: (
        "InitializeGlobalNetSharedContextA_143E08C80",
        "Early startup init for the concrete global shared-context object at "
        "0x143E08C80. Calls ConstructNetSharedContextBase_0x800Entries and "
        "then swaps in a context-specific vtable.",
    ),
    0x14007C104: (
        "InitializeGlobalNetSharedContextB_143E0EF00",
        "Early startup init for the sibling global shared-context object at "
        "0x143E0EF00. Reinitializes the object, explicitly zeroes the "
        "embedded owner field at +0x8 (global 0x143E0EF08), and installs its "
        "context-specific vtable.",
    ),
    0x14194CD20: (
        "ResetNetSharedContextActiveEntries_12x0x1C0",
        "Reset/teardown helper for the shared-context objects. Iterates the "
        "12-entry 0x1C0-stride family rooted at +0x800, clears active "
        "entries, and resets their +0xE0/+0xF0 state.",
    ),
    0x14194AF00: (
        "HardResetSharedContextEntryBandsAndOwnerState",
        "Hard reset for the ctxA/ctxB-style shared-context objects. Clears the "
        "12 active 0x1C0-stride entry bands, resets current-selection state "
        "at +0x18/+0x200/+0x208/+0x20C/+0x1ED0..+0x1ED4, zeroes the embedded "
        "owner field at +0x8, re-seeds default bytes around +0xA1/+0xB0..+0xF0 "
        "and +0x7C0/+0x748, and resets the pending-token/output fields near "
        "+0x1F54.",
    ),
    0x14194CFB0: (
        "CountCtxBActiveSourcesOrFallbackOne",
        "Counts active source entries for the second shared-context family. If "
        "ctxB+0x18 is null, falls back to the single-entry path; otherwise it "
        "counts active 0x1C0-stride entries rooted at ctxB+0x800.",
    ),
    0x14194D5F0: (
        "BuildCtxBSourceListFromOwnerOrFallback",
        "Central source-list builder for ctxB. If ctxB+0x18 is null, it "
        "materializes a single fallback entry at ctxB+0x1D00 by copying "
        "owner+0x8..+0x27 plus dword owner+0x28 from the pointer in ctxB+0x8. "
        "This exact layout matches the startup header copy into "
        "0x143880318 via 0x1409EB500, which strengthens the reading that the "
        "fallback owner can be the global selection service object itself. "
        "Otherwise it enumerates the active "
        "0x800/0x1C0-stride entry family and builds the output list.",
    ),
    0x14194E3B0: (
        "PublishKind70SourceSummaryIntoCtxOwnerAndRefreshPayload",
        "Pushes a selected source-summary pointer block through ctx+0x8 "
        "owner->virtual +0x80(ctx, summary_ptr, 0x70). Then refreshes the "
        "kind-0x70 payload behind either the fallback entry at ctx+0x1D00+0xFC "
        "or the currently selected active entry's +0xFC payload. Used by the "
        "split/session refresh path after it chooses a stock player-2 source, "
        "so this is source-summary publication, not a direct camera-manager "
        "handoff.",
    ),
    0x14194E510: (
        "ResolveKind70PayloadFromCtxSourceEntryOrFallback",
        "Given a ctx object and one source-entry pointer, resolves only the "
        "kind-0x70 payload. If ctx+0x18 is null, it falls back to ctx+0x1ED4 "
        "when ctx+0x1ED0 == 0x70. Otherwise it scans the 12-entry 0x1C0-stride "
        "family at ctx+0x800, matches the incoming source entry by string "
        "identity, and returns entry+0xFC when that entry's kind dword at "
        "+0xF8 is 0x70. This is the concrete record layer consumed by post-bind "
        "ctxB users and by the stock split player-2 summary path.",
    ),
    0x14194C870: (
        "GetActiveSharedContextEntryByOrdinal",
        "Returns the active 0x1C0-stride entry at ctx+0x800 + index*0x1C0 "
        "when that slot's active byte +0xE0 is set, otherwise null. Live "
        "callers use this as a stock entry-selection helper before comparing "
        "against current/fallback ctx state.",
    ),
    0x14194C980: (
        "GetCtxCurrentSelectedOrFallbackEntry",
        "Returns the current-selected 0x1C0 entry for a shared-context object. "
        "If ctx+0x18 is null it falls back to ctx+0x1D00; otherwise it scans "
        "the 12-entry family at +0x800 and returns the entry matching the "
        "current id dword at ctx+0x1EC4.",
    ),
    0x14194DB30: (
        "DispatchCtxBOwner58OrReturn2",
        "Small ctxB wrapper. If ctxB+0x8 is bound, tail-calls "
        "owner->virtual +0x58(ctxB, bool_flag); otherwise returns 2.",
    ),
    0x14194DB70: (
        "DispatchCtxBOwner60OrReturn2",
        "Small ctxB wrapper. If ctxB+0x8 is bound, tail-calls "
        "owner->virtual +0x60(ctxB, bool_flag); otherwise returns 2.",
    ),
    0x14194DB90: (
        "DispatchCtxBOwner98",
        "Thin ctxB wrapper for owner->virtual +0x98(ctxB).",
    ),
    0x14194DBB0: (
        "DispatchCtxBOwnerA0",
        "Thin ctxB wrapper for owner->virtual +0xA0(ctxB).",
    ),
    0x14194DBD0: (
        "DispatchCtxBOwnerA8",
        "Thin ctxB wrapper for owner->virtual +0xA8(ctxB, token).",
    ),
    0x14194DBF0: (
        "StoreCtxBTokenAt200ThenDispatchOwner78",
        "Writes the incoming token to ctxB+0x200, then if ctxB+0x8 is bound "
        "tail-calls owner->virtual +0x78(ctxB, token). This confirms another "
        "live owner slot above the previously anchored +0x50/+0x58/+0x70/+0x78 "
        "band.",
    ),
    0x1419739E0: (
        "BindSharedContextIntoVariantASelectionServiceAndWriteOwner8",
        "Variant-A global selection-service bind worker (vtable +0x20). Finds "
        "or allocates a free 0x8D68-stride service slot at service+0x50, "
        "stores the incoming context pointer into that slot, and then writes "
        "context+0x8 = service. Combined with 0x1409EB500 + 0x14194D5F0, this "
        "now looks like a real bind of ctxB to the service object itself, not "
        "to a separate service-entry pointer. This is the first confirmed "
        "non-null writer for the embedded shared-context owner field.",
    ),
    0x141973A90: (
        "RebuildVariantASelectionServiceRequestTable",
        "Variant-A service worker reached from the global request family "
        "(vtable +0x28). Rebuilds the request/payload store at service+0x24600 "
        "from authored/time-derived state and writes compact records into that "
        "service-local table.",
    ),
    0x141974E10: (
        "DispatchDerivedSelectionTokenThroughVariantAService28",
        "Exact variant-A implementation of the global service request slot "
        "(vtable +0x28). Accepts (service, ctx, derived_token, bool_flag), "
        "resets the ctx entry, stores the token in ctx+0x208, seeds "
        "ctx+0x1ECC=0x100, and emits a larger 0x128 request payload into the "
        "variant-A service-local table at service+0x24600.",
    ),
    0x141975870: (
        "CommitCtxSelectionPendingCandidateThroughVariantAService48",
        "Variant-A implementation of the global service commit slot "
        "(vtable +0x48). Finds the bound ctx entry, clears ctx+0x18, resets "
        "ctx active-entry state, packages ctx+0x1ED0/+0x1ED4 plus the pending "
        "candidate pointer, publishes a 0x98-byte payload into service+0x24600, "
        "and updates the service-local queue/global slot family. This is still "
        "packet/queue-heavy and does not end in a plain ctx+0x18 assignment.",
    ),
    0x141975C00: (
        "ToggleCtxSelectionStateAndEmitVariantAEntryPayload",
        "Variant-A service-side helper over a bound ctx entry. Toggles the "
        "byte at ctx+0x1ECC, and when ctx+0x18 is live emits a compact 0x13E "
        "payload into the variant-A service-local table at service+0x24600. "
        "This looks like a bool/toggle request path, not the token-carrying "
        "campaign service->+0x28 call shape.",
    ),
    0x141979A60: (
        "BindSharedContextIntoVariantBSelectionServiceAndWriteOwner8",
        "Variant-B global selection-service bind worker (vtable +0x20). Finds "
        "or allocates a free 0x9570-stride service slot at service+0x4B8, "
        "stores the incoming context pointer into that slot, and then writes "
        "context+0x8 = service. This is the sibling non-null writer for the "
        "embedded shared-context owner field.",
    ),
    0x14197AE50: (
        "CommitCtxSelectionPendingCandidateThroughVariantBService48",
        "Variant-B implementation of the global service commit slot "
        "(vtable +0x48). Finds the bound ctx entry, resets entry-local state, "
        "calls service->virtual +0x10, packages ctx+0x1ED0/+0x1ED4 plus the "
        "pending candidate pointer, writes ctx+0x18 = pending, then waits "
        "through repeated service->virtual +0x10 calls until ctx+0xB8 flips. "
        "If unresolved, stores -1 in ctx+0xBC and finally sets ctx+0xB8=1.",
    ),
    0x14197ABC0: (
        "DispatchDerivedSelectionTokenThroughVariantBService28",
        "Exact variant-B implementation of the global service request slot "
        "(vtable +0x28). Accepts (service, ctx, derived_token, bool_flag), "
        "resets the ctx entry, stores the token in ctx+0x208, clears the "
        "entry-local request state at +0x8D68, seeds the 0x2332 request lane "
        "through 0x141920B00, and drives local follow-up through ctx->virtual "
        "+0x48 plus 0x14194CA10.",
    ),
    0x14197B0C0: (
        "EmitCtxSelectionTokenThroughVariantBEntryPayloadTable",
        "Variant-B service-side helper over a bound ctx entry. If ctx+0x18 is "
        "live, packages the caller-supplied token in r8d together with the "
        "current selected object pointer into a compact 0xD1 payload at "
        "entry+0x8D28.",
    ),
    0x14194E0D0: (
        "DispatchCtxBStateWithWildcardMinus1",
        "Tiny ctxB dispatcher that re-enters the main ctxB virtual +0x10 "
        "path with r8d=-1, i.e. a wildcard/any-state request.",
    ),
    0x14194E0E0: (
        "DispatchCtxBCurrentDesiredStateThroughVirtual8",
        "CtxB helper that scans the 12-entry table rooted at ctxB+0x8E8, "
        "matches the current desired id from ctxB+0x1EC8, and then dispatches "
        "that id through the ctxB virtual +0x08 path.",
    ),
    0x14194DE00: (
        "DispatchCtxBRequestedStateAcrossEntryTable",
        "Main ctxB dispatch path for a requested state id. Walks the 12-entry "
        "0x1C0-stride table at ctxB+0x8E8, checks current-selection and entry "
        "ids, and either forwards through 0x143FE1080->virtual +0xB0 or "
        "re-enters ctxB virtual +0x10 for matching entries.",
    ),
    0x14194DEE0: (
        "RebuildCtxBEntrySelectionSetAndDispatch",
        "Heavier ctxB rebuild/select path over the same 12-entry entry table. "
        "Uses the 0x1436866C0 service plus 0x141982510 to rebuild per-entry "
        "selection state before dispatching follow-up work. Still no plain "
        "direct writer to ctxB+0x18 is visible here.",
    ),
    0x14194C550: (
        "EmitCtxBSelectionPresentEventThroughPacketBus",
        "CtxB publish path reached through ctxB virtual +0x60. When "
        "ctxB+0x718 is live, emits a packet/event through 0x142204E50. "
        "Consumes current selection state but does not write ctxB+0x18.",
    ),
    0x14194C5F0: (
        "EmitCtxBCurrentSelectionPresentEvent",
        "Sibling ctxB publish helper. Checks whether current-selection state "
        "exists and emits another packet/event through the same upper packet "
        "bus family. No direct current-selection assignment here either.",
    ),
    0x14194C690: (
        "EmitCtxBSelectionPresentEventWhenCurrentBound",
        "Another small ctxB packet publisher. If ctx+0x18 is non-null, emits "
        "a packet/event through 0x142204E50. This consumes live selection but "
        "does not assign it.",
    ),
    0x14194C730: (
        "RefreshCtxBThroughOwner50ThenSelf60",
        "Bridge between the ctxB object and its bound owner. If ctxB+0x8 is "
        "live, calls owner->virtual +0x50(ctxB), then re-enters ctxB virtual "
        "+0x60. This is downstream of the confirmed startup bind, not the "
        "first owner-writer.",
    ),
    0x1407DF190: (
        "GetCtxBCurrentSelectionIfLive",
        "Concrete ctxB vtable slot +0x20 from 0x1429EF890. Returns ctxB+0x18 "
        "only when byte +0xC8 is set, otherwise returns null. This cuts off "
        "the false bind hypothesis around upper callers that only invoke "
        "ctxB->virtual +0x20.",
    ),
    0x140B204A0: (
        "SetCtxBTokenBitAndDispatchOwner78",
        "Concrete ctxB vtable slot +0x38 from 0x1429EF890. Toggles bit 0 in "
        "ctxB+0x200 from dl, loads the bound owner from ctxB+0x8, and "
        "tail-calls owner->virtual +0x78(ctxB). This proves the ctxB object "
        "consumes an already-bound owner and does not perform the bind here.",
    ),
    0x140B204D0: (
        "IsCtxBTokenBitClear",
        "Concrete ctxB vtable slot +0x40 from 0x1429EF890. Returns the "
        "inverted low-bit state of ctxB+0x200.",
    ),
    0x140B20240: (
        "MarkCtxBSelectionPresentAndGlobalDirty",
        "Concrete ctxB vtable slot +0x80 from 0x1429EF890. If ctxB+0x18 is "
        "live, sets ctxB+0x1F80=1; always sets global dirty byte "
        "0x1437E7513=1.",
    ),
    0x140B20260: (
        "PublishCtxBModePacketAndSetActiveBit",
        "Concrete ctxB vtable slot +0x88 from 0x1429EF890. Sets ctxB+0x10=1, "
        "packages a large token-keyed payload from a retained/runtime/object-"
        "list band, and publishes it through 0x141C9ED50.",
    ),
    0x140B20760: (
        "MarkCtxBGlobalDirtyFlag7513",
        "Tiny ctxB slot that only marks the global dirty byte "
        "0x1437E7513=1.",
    ),
    0x140B20710: (
        "ClearCtxBPendingLatchAndOptionallyDispatchOwner78",
        "Concrete ctxB vtable slot +0x48 from 0x1429EF890. Clears "
        "ctxB+0x1FD0, and when the global gate at 0x1437E6F39 is set writes "
        "ctxB+0x200=1 and tail-calls owner->virtual +0x78(ctxB,1); then "
        "clears ctxB+0x10.",
    ),
    0x140B20770: (
        "ResetCtxBRuntimeFlagsAndTailRefresh",
        "CtxB runtime reset slot. Clears a small set of globals plus local "
        "ctx fields around +0x10/+0x1FD0, then tails into the broader "
        "refresh/reset helper at 0x140ACC140.",
    ),
    0x140B222B0: (
        "InitNetPlayerDataStartingValues",
        "Seeds a split/net-side NetPlayerData block from starting-value script "
        "queries such as *net-starting-gold*, *net-starting-xp*, and the "
        "*net-starting-mission-0/1/2* family. Reached from split login/logout "
        "and wider session-state refresh helpers.",
    ),
    0x140B22C00: (
        "CopySplitSubPlayerDescriptorBlock",
        "Copies the 0x24-byte split sub-player descriptor block. Uses the "
        "live global descriptor at 0x143071008..+0x28 when split state is "
        "active, otherwise falls back to a default descriptor payload. This is "
        "the stock sub-player descriptor feed used by split login and by the "
        "ctxB fallback selector; it is not itself a live local-player actor "
        "pointer path.",
    ),
    0x140B22D00: (
        "SetNetPlayerDataSlaveFlags",
        "Split-side NetPlayerData helper anchored by the assert "
        "'m_isSlave == isSlave' in NetPlayerData::Init. Validates the "
        "incoming slave-state byte at +0x9604 and stores the companion flag "
        "byte at +0x9605.",
    ),
    0x140B9DE90: (
        "HandleNetLogoutSplitScreenCommit",
        "Split logout commit/reset helper. Reinitializes the split-side "
        "NetPlayerData blocks through InitNetPlayerDataStartingValues, clears "
        "the split availability/login state, and conditionally re-runs the "
        "large split refresh at 0x140ACC140.",
    ),
    0x140ACC140: (
        "RefreshSplitScreenSummaryFromSessionAndCtxB",
        "Large split/local-player refresh helper reached from split login, "
        "split logout, and broader session-state transitions. Rebuilds summary "
        "state under 0x1437E7740, consults ctxB through 0x14194CFB0 and "
        "0x140B21DE0, then publishes kind-0x70 source summaries back through "
        "0x14194E3B0 for both ctxA and ctxB. This keeps the stock player-2 "
        "source tied to the ctxB selection family, but it still operates on "
        "descriptor/summary state rather than directly producing a live "
        "local-player-1 actor pointer or camera-manager owner.",
    ),
    0x140C021C0: (
        "QueryNetIsSecondPlayerAvailable",
        "Script command handler for net-is-second-player-available?. Reads the "
        "runtime split-availability state rather than booting split mode.",
    ),
    0x140C02220: (
        "HandleNetLoginSplitScreen",
        "Script command handler for net-login-split-screen. Gated by "
        "*net-allow-sub-sign-in*, the runtime availability byte, and the "
        "split login state; copies the split descriptor block, enables split "
        "render state, seeds NetPlayerData, and refreshes the stock "
        "player-2/session summary path. Runtime probing shows this boots split "
        "state and CameraManager(1) in campaign, but stock local-player-1 "
        "resolution can still remain null until separately patched.",
    ),
    0x140C02420: (
        "HandleNetLogoutSplitScreen",
        "Script command handler for net-logout-split-screen. Hands off to the "
        "split logout/reset helper 0x140B9DE90.",
    ),
    0x140B1FAF0: (
        "ConsumeCtxBCurrentSelectionAndEntryCounters",
        "CtxB-side refresh/consume path that checks ctx+0x18 plus counters at "
        "+0x1EC4/+0x1EC8 and 0x1436866C0-backed state before proceeding. It "
        "uses current selection but does not directly assign it.",
    ),
    0x140B1E1A0: (
        "CheckCtxSelectionPermitAgainstCtxACtxBCounts",
        "Upper permit gate that consults both shared-context families through "
        "0x14194CFB0 and returns a bool-like result for whether selection "
        "work should proceed.",
    ),
    0x140A90A00: (
        "NetGameManager_AddOrUpdateNetPlayerCinema",
        "Cinema-flavored wrapper above NetGameManager_AddOrUpdateNetPlayerTracker. "
        "The function carries the `NetGameManager::AddOrUpdateNetPlayerCinema` "
        "string, finds or creates a tracker, seeds spawn/loadout payload, "
        "stores tracker+0x1DC=incoming_r8d and tracker+0x1E0=0 for the "
        "local-player/cinema path, mirrors the incoming 0x40 payload into "
        "tracker-local bands around +0x159/+0xD9/+0x119, hashes the fixed "
        "`NetCharacter-` token to derive tracker+0x1E4, and returns the "
        "tracker pointer. Useful as a payload template, but not as a slot-1 "
        "seam because it hardcodes local slot 0.",
    ),
    0x140A58310: (
        "NetGameManager_UpdateOrCreateLateJoinerTrackerFromExternalRecord",
        "Higher late-joiner bridge that consumes an external player-like "
        "record. Reuses an existing tracker by matching record+0x18/+0x1C "
        "against tracker+0x1DC/+0x1E0, otherwise builds through the "
        "`late-joiner-%d` token family and falls back to "
        "NetGameManager_AddOrUpdateNetPlayerCinema or "
        "NetGameManager_AddOrUpdateNetPlayerTracker, then copies the wider "
        "state block record+0x20..0x3C into tracker fields +0x86C..+0x884. "
        "Retail disassembly now pins additional external-record fields: a "
        "19-entry WORD array at +0x40, paired signed-byte array at +0x66, "
        "and later state at +0x7C/+0x80/+0x84/+0x88/+0x89/+0x8C/+0x90/+0x91/"
        "+0xB4/+0xC0/+0xC8.",
    ),
    0x140A58190: (
        "NetGameManager_CreateOrReuseLateJoinerTrackerFromCompactRecord",
        "Compact late-joiner bridge above the tracker layer. Builds the local "
        "packet through `late-joiner-%d`, then either routes into "
        "NetGameManager_AddOrUpdateNetPlayerCinema or resolves a source object "
        "and falls back to NetGameManager_AddOrUpdateNetPlayerTracker.",
    ),
    0x140A4DDA0: (
        "NetGameManager_CreateCinemaTrackerFromSingleIdRecord",
        "Thin cinema-only wrapper above NetGameManager_AddOrUpdateNetPlayerCinema. "
        "Requires the same global gate, only consumes record+0x18 as the id, "
        "builds the local 0x40 packet, calls the cinema wrapper, and then "
        "stamps the usual tracker flags.",
    ),
    0x140A4E7DC: (
        "NetGameManager_CreateCinemaTrackersFromIdArray10",
        "Bulk thin wrapper above NetGameManager_AddOrUpdateNetPlayerCinema. "
        "Iterates a compact 10-entry id array, builds the same local 0x40 "
        "packet through the `cinema-player-%d` token family for each entry, "
        "calls the cinema wrapper, and stamps the same tracker flags.",
    ),
    0x140B346B0: (
        "NetPlayerTracker_StartOrQueueSpawnPlayer",
        "Thin tracker-side gate above NetPlayerTracker_SpawnPlayer. Clears "
        "pending bits at +0xA7C/+0x814, checks tracker readiness through "
        "virtual +0x18 and current live-entity state, and then either "
        "tail-calls NetPlayerTracker_SpawnPlayer or queues deferred spawn "
        "state in the tracker fields around +0xA00/+0xA04/+0x4C0/+0x4C8.",
    ),
    0x140B31F60: (
        "NetPlayerTracker_SpawnPlayer",
        "Heavy net-player spawn routine. Resolves the look id, derives online "
        "starting-weapon records, checks startup placement modes such as "
        "`start-on-edge` / `start-in-cover` / `start-in-swim`, and then calls "
        "PromoteRetainedPlayerSlot to materialize the real Player object for "
        "the requested tracker slot. The retained slot comes from tracker+0x1E0, "
        "later upper authored/runtime production consumes tracker+0x1E4, and "
        "the body snapshots tracker+0x840/+0x844 into +0x848/+0x84C before "
        "continuing. The returned Player object is then used directly through "
        "fields such as +0x1D88, which makes this one of the strongest built-in "
        "second-player bringup paths found so far.",
    ),
    0x140B44EC0: (
        "BuildNetPlayerTrackerRespawnTransformBlock",
        "Builds the staged respawn transform block later stored into "
        "tracker+0x500..+0x540. Prefers a valid live actor pair from "
        "tracker+0x8/+0x10 and copies live_actor+0x100/+0x120/+0x140; "
        "otherwise falls back through tracker+0x20, which on this path is "
        "just the 16-byte reference-position carrier later consumed by "
        "0x141685C30 rather than a compact actor-pair record. Static recovery "
        "now also shows this fallback still funnels into the same crashing "
        "builder contract, so it does not bypass the remaining lifecycle gap.",
    ),
    0x140B45030: (
        "InitializeCompactActorPairRecord",
        "Zero-initializes the small actor-pair record used by the upper "
        "respawn and actor-selection helpers. Layout is dword at +0x0, "
        "qword at +0x8, dword at +0x10.",
    ),
    0x140B45040: (
        "BuildCompactActorPairRecordFromCurrentContext",
        "Builds the compact actor-pair record from current context. Either "
        "publishes a resolved actor key through the manager lookup path or "
        "falls back to a direct live object pair (object+0xA8 / object+0xBC).",
    ),
    0x140B45100: (
        "BuildCompactActorPairRecordFromSourcePair",
        "Normalizes an incoming compact actor-pair/source pair into the same "
        "record shape used by respawn and actor-selection helpers. Prefers "
        "the manager lookup/key path and falls back to direct object+0xA8 / "
        "object+0xBC storage when needed.",
    ),
    0x140B451B0: (
        "AreCompactActorPairRecordsEquivalent",
        "Compares two compact actor-pair records. Accepts either matching "
        "dword keys or matching live object pair identity.",
    ),
    0x140B411F0: (
        "ResolveNetPlayerPostSpawnSelectorFromPlayer854",
        "Returns the integer selector used after promotion from player+0x854, "
        "falling back to a hashed default if the field is zero.",
    ),
    0x140B412D0: (
        "ResolveNetPlayerPostSpawnSelectorFromPlayer858",
        "Returns the integer selector used after promotion from player+0x858, "
        "falling back to a hashed default if the field is zero.",
    ),
    0x140B347C0: (
        "NetPlayerTracker_NetRespawn",
        "Tracker-side respawn entry identified by the `NetPlayerTracker::NetRespawn` "
        "string xref. With r8b=1 it copies the caller-supplied 0x50 respawn "
        "transform block from r9 straight into tracker+0x500..+0x540 and "
        "bypasses BuildNetPlayerTrackerRespawnTransformBlock. With r8b=0 it "
        "falls back to the older tracker+0x8/+0x10 or tracker+0x20 source "
        "paths. This makes it a cleaner first local patch seam than the "
        "manager wrapper above it.",
    ),
    0x141685BC0: (
        "BuildRespawnTransformBlockFromSeedRecord",
        "Builds the 0x50 transform/orientation block from a compact seed "
        "record and then resolves authored transform overrides through "
        "0x1416860A0.",
    ),
    0x141685C30: (
        "BuildRespawnTransformBlockFromReferenceTransformAndSeedRecord",
        "Builds the 0x50 respawn transform block from a reference transform "
        "plus a compact seed record. This is the helper used by live callers "
        "that later pass r8b=1 into NetPlayerTracker_NetRespawn. The live "
        "tracker-side caller uses tracker+0x20 as the reference transform "
        "carrier and a stack seed shaped as: dword(+0), qword(+4), dword(+0xC), "
        "vec4(+0x10), vec4(+0x20). Raw disassembly shows the helper only "
        "consumes the first 16 bytes at rdx as reference position. Runtime "
        "testing has already ruled out three naive sources (`actor+0x100`, "
        "inline slot+0x20, and global source0+0x20`), so the remaining "
        "unknown is the caller-side lifecycle that prepares a valid source "
        "band before this helper becomes safe.",
    ),
    0x140BDB2E0: (
        "NetEventRespawnPlayer_Fill",
        "Builds the concrete respawn event payload from a tracker plus optional "
        "BoundFrame and boolean flag. Serializes tracker identity into "
        "event+0x18, has-transform into event+0x1C, optional transform data "
        "into event+0x20..+0x60, and the final byte flag into event+0xC8.",
    ),
    0x140BDB440: (
        "NetEventRespawnPlayer_HostFill",
        "Host-side fill/validation pass for NetEventRespawnPlayer. Derives "
        "tracker-side spawn mode/timing fields into event offsets "
        "+0xB0/+0xB8/+0xBC/+0xC0/+0xC4.",
    ),
    0x140BDB620: (
        "SendNetEventRespawnPlayer",
        "Send/broadcast helper for NetEventRespawnPlayer. Allocates the event, "
        "calls the fill helpers, and packages the message for the net-event "
        "transport layer.",
    ),
    0x140BDBF70: (
        "SendNetEventSignalRespawnPlayer",
        "Sends the lighter respawn-signal event with tracker pointer, boolean, "
        "and timeframe payload, using the same net-event transport family as "
        "SendNetEventRespawnPlayer.",
    ),
    0x140B6B970: (
        "HandleNetEventRespawnPlayer_ResolveTrackerAndCallRespawn",
        "Upper net-event style handler that resolves tracker/event ids, "
        "walks the NetGameManager tracker array, and is the only direct "
        ".text caller of NetGameManager_RespawnPlayer. Useful as a conceptual "
        "bridge above the tracker layer, but still less direct than the "
        "manager-side AddOrUpdate/Respawn entrypoints for a local runtime patch.",
    ),
    0x140BBFD70: (
        "NetEventCharacterMove_Execute",
        "Virtual +0x18 of NetEventCharacterMove. Resolves the target tracker "
        "and Player, converts event animation bytes into a NetSnapshot, then "
        "publishes that snapshot to the Player. The earlier tracker lifecycle "
        "label was incorrect.",
    ),
    0x141E994C0: (
        "SharedBasePlayerUpperHandoffIntoDrawControlAndUpperStateBands",
        "Large shared/base upper Player handoff called from 0x140CBFF50 after "
        "player+0x15A4..+0x15B0 and player+0x1B10 are initialized. It seeds "
        "fixed upper defaults around +0x1510/+0x1530/+0x1550, invokes "
        "0x141BB4280 as the broader draw-control/upper-lifecycle stage, then "
        "continues through player virtual +0x788, resets late flags, "
        "materializes player+0x4A8, and allocates a fresh 0x2B0 upper owner "
        "family. This makes it a strong shared/base handoff into the upper "
        "Player lifecycle, still upstream of mounted verb subsystems but "
        "already downstream of the compact init packet.",
    ),
    0x1410C6F00: (
        "ConstructPlayer_Size49E0OrInitializeExistingStorage",
        "Alloc-ctor wrapper for the RTTI-backed Player class. If rdx is non-null, "
        "jumps directly into 0x1410C8FD0; otherwise allocates 0x49E0 bytes and "
        "then calls 0x1410C8FD0.",
    ),
    0x1410C8FD0: (
        "InitializePlayerCtorAndStampPlayerVftable",
        "Real in-place Player constructor. Calls base ctor at 0x140CBFB30, "
        "stamps the RTTI-backed Player vtable 0x142A4BFE8, and zero-initializes "
        "the broad Player layout including the upper owner bands and mounted "
        "subsystem slots.",
    ),
    0x1410C6F60: (
        "ConstructSiblingPlayerHelperForGlobalSlotBF90",
        "Allocates a 0x20-byte helper object, stamps vtable 0x142A0A678, and "
        "stores the incoming owner/context pointer at +0x10. This helper is "
        "registered by 0x1400AA940 into the global slot family at "
        "0x143EEBF90/0x143EEBFA0.",
    ),
    0x1410C6FC0: (
        "AttachGlobalPlayerHelperBF90ToOwnerIfSupported",
        "Checks the owner-side object at rcx+0xC0, asks it about the global "
        "slot family rooted at 0x143EEBF90 through virtual +0x198, and on "
        "success stores that global slot pointer back into the owner's +0xC0 "
        "subobject while setting local flags at +0x3D/+0x3E. This makes "
        "player+0xC0 look like a live Player-side helper/state registry, not "
        "dead ctor-only storage.",
    ),
    0x1410CF370: (
        "Player_InitializeDrawControl_FromProcessSpawnInfoAndResolvedLook",
        "Upper Player init stage identified by the embedded "
        "Player::InitializeDrawControl(const ProcessSpawnInfo &, const "
        "ResolvedLook &) string. Real Player vtable slot +0x718. Logs through "
        "outer_player+0x3C20, binds the incoming extended wrapper packet "
        "(0x1417C35A0 / vtable 0x1428D5D60) plus the draw-control context "
        "through 0x141BBACC0, wires player+0x1B10 to that same context, "
        "applies companion flag masks 0x200 and 0x800, resets local "
        "draw-control bands at +0x3B0/+0x3B8/+0x3C0, and returns the incoming "
        "ProcessSpawnInfo-like state pointer. This is now the clearest upper "
        "Player-side consumer of the wrapper family, but it behaves as a "
        "downstream draw-control/behavior bind stage rather than the place "
        "where the real Player authored identity is created.",
    ),
    0x1410CF470: (
        "Player_InitializeCompositeBody",
        "Upper Player init stage identified by the embedded "
        "Player::InitializeCompositeBody(void) string. Called directly from "
        "0x1410DC160, builds additional mounted runtime owners, and continues "
        "the upper Player bringup after the early draw/control path.",
    ),
    0x141E584D0: (
        "CompositeBody_SetCollisionFilter",
        "Iterates the manager's 0x110-byte collider entries at manager+0x48, "
        "writes the requested collision-filter value to entry+0xC8, and "
        "publishes the low six filter bits to the attached rigid body or queues "
        "a physics refresh through 0x141CF6ED0. Player_InitializeCompositeBody "
        "calls this with value 4 after tagging every entry as player. The nearby "
        "entry+0xC4 byte is not this filter field.",
    ),
    0x140A243C0: (
        "NetPlayer_InitializeCompositeBodyWithCollisionSettings",
        "Resolves net-player-collision-settings or its ally variant, builds the "
        "Player composite body, tags each 0x110-byte collider entry at +0x78 as "
        "netcharacter, sets collision layer 5 through "
        "CompositeBody_SetCollisionFilter, then switches the body state to 2.",
    ),
    0x141EC95E0: (
        "Physics_InitializeCollisionFilters",
        "Constructs the 64-layer HavokGamelibFilter matrix and wraps it in "
        "HavokPairCollisionFilter before installing it on the physics world. "
        "Its layer-mask calls explicitly disable character pairs 4<->4, "
        "4<->5, and 5<->5.",
    ),
    0x1421F86B0: (
        "HavokGroupFilter_DisableLayerPairs",
        "Symmetrically clears collision permissions for every layer selected "
        "by mask A against every layer selected by mask B in the 64-row matrix "
        "at filter+0x48.",
    ),
    0x140CE7ED0: (
        "PlayerBumpController_Update",
        "Campaign Player-bump update. Builds a Crowd::ProbeResult through "
        "PlayerBumpController_BuildCrowdProbeResult, tries to start an authored "
        "bump action through PlayerBumpController_TryStartBumpAction, and falls "
        "back to PlayerBumpController_UpdateCrowdAdjustment when no action starts.",
    ),
    0x140CE1E20: (
        "PlayerBumpController_BuildCrowdProbeResult",
        "Builds the Player-facing Crowd::ProbeResult from Player transform and "
        "movement through 0x1406DD350, then updates the bump probe state. This "
        "path is separate from Havok rigid-body contact filtering.",
    ),
    0x140CE9530: (
        "PlayerBumpController_UpdateCrowdAdjustment",
        "Fallback Crowd::ProbeResult consumer used when no authored bump action "
        "starts. It adjusts crowd movement and handles no-adjust-to-player; the "
        "hero-bump action names are built at 0x140CE2520, not here.",
    ),
    0x1406DD350: (
        "Crowd_UpdateProbe",
        "Crowd::UpdateProbe from crowd-util.cpp. Maintains a 64-byte ProbeResult, "
        "accepts an existing non-self candidate by actor capability or SimpleNpc "
        "selector, and otherwise performs the spatial collision query.",
    ),
    0x140CE2520: (
        "PlayerBumpController_BuildBumpActionCandidate",
        "Builds the 128-byte Player bump-action candidate, including directional "
        "hero-bump-%s%s%s-a/b/c hashes and movement/contact eligibility flags. "
        "SimpleNpc is optional target-specific data, not a requirement for Player.",
    ),
    0x140CE8E10: (
        "PlayerBumpController_TryStartBumpAction",
        "Consumes Crowd::ProbeResult, calls the bump-action candidate builder, and "
        "starts PlayerBumpAction through 0x140CE39E0 when the candidate flags allow it. "
        "Returns false to permit crowd movement adjustment.",
    ),
    0x140CDCC70: (
        "PlayerBumpController_IsContactEligible",
        "Checks ProbeResult distance, contact geometry/relative movement through "
        "0x140CDC900, bump cooldown, and optional SimpleNpc velocity before allowing "
        "an authored bump action.",
    ),
    0x141E36910: (
        "SimpleGridHashManager_RegisterObject",
        "Registers an NdGameObject in the shared simple-grid hash, stores its live "
        "handle, grid cell, velocity and radii, and returns a SimpleGridHashId.",
    ),
    0x141E37080: (
        "SimpleGridHashManager_UpdateGridHash",
        "Validated by its embedded source signature. Verifies that a supplied "
        "SimpleGridHashId still belongs to the same NdGameObject and updates its "
        "grid cell, transform, velocity and radii.",
    ),
    0x140CC1450: (
        "ApplyProcessSpawnInfoStateIntoTopLevelPlayerOwner",
        "Top-level Player apply/finalize path reached from 0x1410DE740. "
        "Validates a lookup record against owner ids at +0x108/+0x10C, using "
        "player+0x164 as the runtime-state slot kind and optionally forcing "
        "kind 0x80 when global 0x143E07473 is set and player+0x161A bit0 is "
        "set. After copying accepted state back into outer_player+0x110 it "
        "builds a transform block from +0x110/+0x120/+0x130/+0x140 and "
        "commits through 0x1418C4840. That callee immediately consumes "
        "external_state+0x40/+0x48/+0x58 and seeds them into the runtime "
        "slot, which makes the packet shape concrete. The path then "
        "conditionally refreshes a `proto`-tagged upper state into "
        "player+0x1A0 through 0x1417CD200/0x1417CD3D0 and finally sets "
        "player+0x2D1=1. The same +0x108/+0x10C pair is later re-resolved by "
        "multiple upper Player helper families through the shared table "
        "0x14386FD00, so this is a stable upper authored-record key rather "
        "than a one-shot apply detail. The consumed external_state is now "
        "best read as a real ProcessSpawnInfo-derived packet family seeded "
        "earlier by 0x1410F14C0; the closest currently pinned shape match is "
        "the extended wrapper family at 0x1417C35A0 / vtable 0x1428D5D60, "
        "not the returned materialized object from 0x1417C3790.",
    ),
    0x140CC18D0: (
        "SeedPlayer15B4FromIncomingContext58E0",
        "Real Player vtable slot +0x738. If the incoming context pointer is "
        "non-null, copies context+0x58E0 into player+0x15B4; otherwise clears "
        "player+0x15B4. This is a narrow upper-context seed helper used from "
        "0x1410FE840 after the typed context block is built, not a broad apply "
        "or promotion hook.",
    ),
    0x1410DE890: (
        "GetPlayerCollisionSettingsRecord",
        "Player vtable slot +0x850. Lazily resolves the authored "
        "`player-collision-settings*` record into global 0x1437423D0 and "
        "returns it. The earlier upper-rebuild/player+0x3828 interpretation "
        "was incorrect and must not be used as a lifecycle call.",
    ),
    0x141BBA490: (
        "BuildUpperPlayerStateOwnerAt1A0ThenRunPlayerSlot710",
        "Real Player vtable slot +0x700. Allocates a 0x2B0 upper state owner "
        "through 0x1415A0E90 -> 0x141728690, stores it into player+0x1A0, "
        "links player+0x1A8->+0x10 back to that owner, optionally queries "
        "Player slot +0x708 for a small classifier/count, then feeds "
        "player+0x1A0, player+0x1A8, player+0x160, and the +0x708 result into "
        "0x141728720 before immediately calling Player slot +0x710. This "
        "makes +0x700 the entry point of a distinct upper state-build/publish "
        "band over player+0x1A0/+0x1A8. It is now clearly neighboring the "
        "upper +0x6F0/+0x718 lifecycle, but not yet proven to sit inside the "
        "same +0x850 rebuild ordering.",
    ),
    0x141BBA2A0: (
        "ApplyUpperActiveStatePayloadIntoPlayer1A8AndSetSelectorGateFlag",
        "Real Player vtable slot +0x6F8. Consumes an incoming upper payload "
        "record, copies vector/scalar fields into the active upper state at "
        "player+0x1A8 (+0x150..+0x194), derives a delta vector, and checks a "
        "selector/capability through player+0x30 via 0x1417C4800. On success "
        "it raises the byte flag at the caller-provided helper offset "
        "+0x2D0. This makes +0x6F8 a concrete upper active-state payload "
        "applicator sitting after the +0x718 wrapper bind in the sibling "
        "upper lifecycle block.",
    ),
    0x1410CE260: (
        "PublishUpperPlayerOverlayReadJointAndReferenceState",
        "Real Player vtable slot +0x710. Consumes the upper state owner at "
        "player+0x1A0 together with the active upper state at player+0x1A8 "
        "and publishes multiple authored upper Player families: "
        "`anim-net-player`, `*player-overlay-priorities*`, "
        "`*player-default-overlays-count*`, `*player-default-overlays*`, "
        "`apReference`, `align`, `run-chloe`, and `base-read-joints-layer`. "
        "Early in the path it resolves knowledge/state ids through "
        "0x1417651D0/0x141765030 and publishes them into the sink at "
        "[active_state+0x60] via 0x1417F9150. Later it builds the owner at "
        "player+0x4260, seeds typed subrecords through 0x141728D90/0x14172A720/"
        "0x14172ACD0, and only after those upper overlay/reference layers are "
        "prepared does control continue into the animation-snapshot stage at "
        "player+0x3FD8. This is therefore a real upper Player state-build/"
        "publish phase, not a side helper. Direct vcall scans currently show "
        "it only being entered from Player slot +0x700.",
    ),
    0x141308680: (
        "UpperPlayerBlock_Drive700And708ThenEnterMounted1E98VerbPath",
        "Upper block that invokes Player slot +0x700, immediately invokes "
        "Player slot +0x708, then continues into the mounted Player subsystem "
        "at player+0x1E98 through virtual +0x190. This makes the upper "
        "+0x700/+0x708 band a live feeder into a concrete mounted Player verb "
        "owner rather than an isolated helper path.",
    ),
    0x141728690: (
        "InitializeUpperPlayerStateOwner2B0",
        "In-place initializer for the 0x2B0 upper state owner allocated by "
        "Player slot +0x700. Zeroes the main bands, allocates a 0x200 scratch/"
        "entry area at +0x78, and clears the late fields through +0x2AC. The "
        "result is the owner later stored into player+0x1A0.",
    ),
    0x141728720: (
        "PopulateUpperPlayerStateOwnerFromActiveStateAndClassifier",
        "Populates the 0x2B0 upper state owner built by 0x141728690. Mirrors "
        "an optional retained/live pair into +0x38, stores the active upper "
        "state object at +0x20, copies classifier/count fields from the "
        "incoming active-state descriptor into +0x214/+0x218, synchronizes a "
        "live runtime object through 0x141780990, forwards a compact state "
        "summary through 0x1418BBEB0, and allocates the per-entry table at "
        "+0x30 using the classifier/count passed from Player slot +0x708. "
        "This is the concrete build step that prepares player+0x1A0 before "
        "Player slot +0x710 publishes overlay/read-joint/reference state.",
    ),
    0x141BB4AA0: (
        "UpperPlayerLifecycleBlock_738_6F0_700_718",
        "Sibling upper block that explicitly chains Player slots "
        "+0x738, +0x6F0, +0x700, and +0x718 in one continuous path. It caches "
        "the compact state from +0x738 into player+0x170 (and may seed "
        "player+0x190), resolves an authored record through 0x1417CD080, "
        "stores the +0x6F0 result into player+0x1A8, then runs the upper "
        "state-owner band through +0x700 before continuing into +0x718. This "
        "is currently the strongest concrete upper lifecycle anchor above the "
        "mounted Player graph that includes both the direct apply seam and the "
        "new +0x700 state-owner band.",
    ),
    0x141BB4280: (
        "PlayerUpperDrawControlLifecycleAndAmbientOccluderStage",
        "Containing Player-side stage that owns the block at 0x141BB4AA0. It "
        "seeds look-randomizer / prop-set style ids at player+0x178/+0x17C, "
        "threads the upper lifecycle through +0x238, +0x738, +0x6F0, +0x700, "
        "+0x718, +0x6F8, and +0x4A8, and then delegates ambient-occluder "
        "selection plus draw-control mesh refresh through Player slot +0x740. "
        "This makes it a real upper draw-control/finalization stage inside "
        "the broader Player/NdGameObject bringup, not a direct ownership/"
        "promotion hook.",
    ),
    0x141BCD340: (
        "ResolvePlayerAmbientOccluderSelectionAndRefreshDrawControlMeshes",
        "Real Player vtable slot +0x740. Resolves authored ambient-occluder "
        "ids around `character-ambient-occluder-array`, `ambient-occluders`, "
        "and `*characters-ambient-occluders-map*`, writes the selected id to "
        "player+0x180, optionally refreshes the draw-control object at "
        "player+0x198 via 0x1417567C0, then scans draw-control meshes and "
        "updates ambient-occluder usage through 0x141756570/0x141755B10. "
        "This pins +0x740 as a draw-control/ambient-classification stage, "
        "not an ownership or verb-dispatch seam.",
    ),
    0x140CC4BF0: (
        "PlayerBase_SpawnHatWithIdFromMap",
        "Exact source assertion names PlayerBase::SpawnHatWithIdFromMap. It "
        "resolves the selected authored look, creates tracked prop processes "
        "in Player+0x1758 (16-byte entries), adds resolved meshes to the "
        "Player+0x198 DrawControl, records their ids in Player+0x1858, and "
        "materializes optional look VFX. This is the per-hat worker called by "
        "0x140CC5890.",
    ),
    0x140CC5890: (
        "PlayerBase_RebuildHatAttachmentsFromIds",
        "Rebuilds Player hat/hair attachments from Player+0x15AC/+0x15B0. "
        "It kills up to 16 old tracked processes at Player+0x1758, removes "
        "their mesh ids at Player+0x1858 from the parent Player+0x198 "
        "DrawControl, clears both tables, resolves both authored hat ids, and "
        "calls PlayerBase_SpawnHatWithIdFromMap for the selected entries. "
        "Callers include Player initialization, ChangeLookId, and direct "
        "hat-id setters.",
    ),
    0x1410FE840: (
        "Player_ChangeLookId",
        "Exact source assertion names Player::ChangeLookId(StringId). It tears "
        "down and reconstructs the Player DrawControl/composite body, builds a "
        "ProcessSpawnInfo, then reinitializes the owner's prop/process control. "
        "The stock PreFrame loop at 0x14015CE1C consumes Player+0x3D10 to call "
        "this function outside object Update.",
    ),
    0x141E61010: (
        "NdPropControl_Init",
        "Exact source assertion names NdPropControl::Init. It resolves look2 and "
        "prop records from ProcessSpawnInfo/owner state and builds the pending "
        "native prop-process list through NdPropControl_BuildPropSpawnList.",
    ),
    0x141E61860: (
        "NdPropControl_BuildPropSpawnList",
        "Exact source assertion names NdPropControl::BuildPropSpawnList. It maps "
        "resolved prop ids to authored definitions and appends pending spawn "
        "records consumed by the follow-up materialization pass.",
    ),
    0x141E63D70: (
        "NdPropControl_SpawnPropFromDefinition",
        "Exact source assertion names NdPropControl::SpawnPropFromDefinition. It "
        "builds native spawn state from an authored prop definition, invokes the "
        "owner's virtual process creator, and attaches the spawned process.",
    ),
    0x1401186C0: (
        "RegisterProcessCharClothFactory",
        "Registers the exact ProcessCharCloth type string with the generic "
        "process registry and creator ProcessCharCloth_Factory at 0x141F41470.",
    ),
    0x141F3DEE0: (
        "ProcessCharCloth_Constructor",
        "RTTI-backed constructor: installs ??_7ProcessCharCloth@@6B@ and "
        "initializes the class-specific 0x880..0x960 tail.",
    ),
    0x141F3E060: (
        "ProcessCharCloth_Init",
        "Exact source assertions name ProcessCharCloth::Init. It consumes the "
        "spawn descriptor, resolves the attach joint and parent handle at "
        "+0x4E0/+0x4E8, builds cloth render/physics state, and marks +0x870 "
        "initialized.",
    ),
    0x141F3E6E0: (
        "ProcessCharCloth_UpdateRoot",
        "Exact source assertions name ProcessCharCloth::UpdateRoot. It resolves "
        "the parent Player handle and updates cloth root transforms/wind data.",
    ),
    0x141F3F7A0: (
        "ProcessCharCloth_EventHandler",
        "Exact source assertions name ProcessCharCloth::EventHandler; it applies "
        "cloth simulation and visibility/event parameters to the owned bodies.",
    ),
    0x141F41470: (
        "ProcessCharCloth_Factory",
        "Factory registered for ProcessCharCloth: allocates 0x980 bytes and "
        "runs ProcessCharCloth_Constructor, or constructs into supplied storage.",
    ),
    0x141308220: (
        "UpperPlayerVerbStage_EdgeAimReload_And_IdleReach0",
        "Containing upper Player verb stage around the block at 0x141308680. "
        "Hashes authored tokens such as `edge-aim-reload`, "
        "`idle-reach0-stamina`, and `idle-reach0`, gates through the Player "
        "qualification owner at +0x1D88, reuses the owner-local registry at "
        "+0x3BD0, then drives Player slots +0x700/+0x708 and continues into "
        "the mounted Player subsystem at +0x1E98. This is strong evidence "
        "that the +0x700 state-owner band is reused by concrete upper verbs, "
        "but the stage itself is still downstream of the real promotion/apply "
        "surface.",
    ),
    0x140CC17A0: (
        "ResolveEffectivePlayerStateKindIntoOutDword",
        "Real Player vtable slot +0x4A8. Writes an effective upper state-kind "
        "dword to the caller-provided output. In the normal case it returns "
        "player+0x174; under the global gate at 0x143E07473 it instead hashes "
        "the `Player` token and writes that override. This slot is used in the "
        "same sibling upper lifecycle block after the +0x6F8 payload "
        "application path.",
    ),
    0x1401D48D0: (
        "ReturnTrue_PlayerUpperPermit768",
        "Real Player vtable slot +0x768. Trivial `return true` permit gate "
        "used after the +0x6F8 path in the sibling upper lifecycle block.",
    ),
    0x1401D48E0: (
        "ReturnTrue_PlayerUpperPermit760",
        "Real Player vtable slot +0x760. Trivial `return true` permit gate "
        "queried by Player slot +0x700 before the +0x708 classifier stage.",
    ),
    0x14102F6B0: (
        "LookupHashedEntryInPlayerOwner3BD0Registry",
        "Consumer of the owner-local object at outer_player+0x3BD0. Uses "
        "object+0xB0 as bucket count, object+0x20 as bucket table, and linked "
        "entries with hashed ids at entry+0x10, which pins +0x3BD0 as a real "
        "hashed owner-local runtime registry rather than a passive state blob.",
    ),
    0x14177F7F0: (
        "AllocateGlobalRuntimeStateSlot380AndSeedRetainedPair",
        "Generic global runtime-state slot allocator used by the top-level "
        "Player ProcessSpawnInfo path and several non-Player callsites. Claims "
        "a 0x380-byte slot from the global pool at 0x1466D0200, resets it, "
        "copies an optional retained/runtime pair into +0x1A8, stores the "
        "incoming type/context id into +0x208, zeroes +0x20C, and may "
        "allocate an auxiliary payload into +0x210.",
    ),
    0x1418C4840: (
        "ApplyExternalStateAndTransformBlockIntoRuntimeStateSlot",
        "Generic apply/commit helper over the 0x380 runtime-state slots "
        "allocated by 0x14177F7F0. Copies source links from external_state "
        "into +0x20/+0x130/+0x138 and +0x18/+0x1A0, using external_state "
        "payload fields at +0x40/+0x48/+0x58. Initializes the auxiliary "
        "subobject at +0x200, converts the incoming transform block into the "
        "committed spatial block at +0x1C0..+0x1F0, and seeds late defaults "
        "across +0x280..+0x320 before returning the populated slot object. "
        "That packet shape lines up more naturally with the extended wrapper "
        "family initialized by 0x1417C35A0 than with the returned object "
        "family materialized by 0x1417C3790.",
    ),
    0x1417CD200: (
        "LookupUpperPlayerStateRecordByAuthoredHash",
        "Global authored-state lookup keyed by a hashed token. Searches the "
        "registry rooted at 0x1466EC480 / 0x143862600 and returns the matched "
        "upper Player state record object, used by 0x140CC1450 to resolve the "
        "`proto`-tagged state before writing it into player+0x1A0.",
    ),
    0x1417CD3D0: (
        "FinalizeOrActivateUpperPlayerStateRecord",
        "Follow-up helper over an upper Player state record. Uses the record "
        "field at +0x24 and internal registry state to retain/finalize the "
        "record, and is called after 0x140CC1450 stores a freshly resolved "
        "state into player+0x1A0.",
    ),
    0x1410DB040: (
        "ValidateTopLevelPlayerActionContextAgainstProcessSpawnInfoState",
        "Broad top-layer Player preflight validator over ProcessSpawnInfo-style "
        "input. Now clearly reads as a gesture/verb permit gate: it checks the "
        "current outer-owner state across +0x1A0, +0x1D88, +0x1D50, +0x1D48, "
        "+0x1E58, +0x1FF0, and +0x3D98 and emits structured failure reasons "
        "such as `cinematic is playing`, `player has two-handed weapon in "
        "hands`, `player is aiming`, `player is in an IGC`, `player is "
        "crouched`, `player has flashlight/torch/flare`, `character is in "
        "melee`, and other locomotion/gesture restrictions.",
    ),
    0x1410DBF10: (
        "AdaptTopLevelPlayerValidationResultIntoIncomingRecord",
        "Small wrapper around 0x1410DB040. The incoming record now reads as: "
        "record+0x0 = live actor, record+0xC = minimum accepted result "
        "threshold or -1 wildcard, record+0x10 = optional out pointer for the "
        "chosen failure token. Feeds that record into the top-layer Player "
        "preflight validator and copies the produced reason/result pointer "
        "back through record+0x10 when the threshold rejects the action.",
    ),
    0x1410DC120: (
        "ClassifyUpperPlayerStateTagIntoSmallEnum",
        "Reads the authored/state tag at incoming_record+0x4 and maps four "
        "known hashes into a compact result enum 0..3, else returns -1. Sits "
        "directly above the top-level Player init family around 0x1410DC160.",
    ),
    0x1410DBFA0: (
        "InitializeUpperPlayerOwnerBridgeWrapperBF50",
        "Seeds a tiny wrapper object with vtable 0x142A4BF50 and stores the "
        "real outer Player owner in object+0x8. This wrapper is then used to "
        "expose upper Player surfaces without passing the raw owner directly.",
    ),
    0x1410DBF80: (
        "AcceptWildcardIncomingUpperStateOrResolveBridgeIndexedSubstate",
        "If incoming_record+0xC is -1, returns success immediately. Otherwise "
        "tail-calls the bridge-wrapper virtual at +0x10, which resolves one of "
        "the compact indexed upper Player substates under owner+0x1A0.",
    ),
    0x1401B8420: (
        "BuildFixedTwoEntryUpperPlayerDescriptorFromBridgeWrapper",
        "Shared bridge-wrapper virtual that jumps into 0x141E52590. Builds a "
        "compact descriptor object with count=2 and a secondary authored-id "
        "array, which is then consumed by the upper Player manager at +0x3FD8 "
        "during registration.",
    ),
    0x1401B8430: (
        "ReturnFalseFromBridgeWrapperVirtual",
        "Trivial bridge-wrapper virtual that returns false. Sits beside the "
        "shared descriptor-builder slot in the 0x142A4BF50 wrapper family.",
    ),
    0x1410DBFC0: (
        "GetUpperPlayerActionPolicy2018FromBridgeWrapper",
        "Returns wrapper->owner->+0x2018 from the tiny upper Player bridge "
        "object at 0x142A4BF50.",
    ),
    0x1410DBFE0: (
        "GetIndexedUpperPlayerSubstateFromOwner1A0ViaBridgeWrapper",
        "For indices 0..1 resolves an indexed substate entry through "
        "wrapper->owner->+0x1A0 -> +0x58 -> +0x318 + 0x30*index. This exposes "
        "compact upper Player substate via the same bridge wrapper family.",
    ),
    0x1410DC020: (
        "GetUpperPlayerFinalizeLogBand3C20FromBridgeWrapper",
        "Returns wrapper->owner + 0x3C20 from the tiny upper Player bridge "
        "object at 0x142A4BF50.",
    ),
    0x1410DC030: (
        "RelocateOuterPlayerPointerInsideBridgeWrapperIfInRange",
        "Bridge-wrapper relocate helper. If wrapper->owner falls inside the "
        "supplied [old_base, old_end) image/window, rewrites object+0x8 to "
        "new_base + (owner-old_base).",
    ),
    0x1410DC050: (
        "DestroyBridgeWrapperAndOptionallyFree",
        "Tiny freeing dtor for the upper Player bridge wrapper. Stamps the "
        "wrapper-local vtable 0x1428DF6B0 and optionally releases the 0x10-byte "
        "object when dl&1 is set.",
    ),
    0x1410C6B90: (
        "GetUpperPlayerManager3FD8",
        "Trivial getter that returns outer_owner+0x3FD8. This pins +0x3FD8 as "
        "another formal upper Player manager slot beside +0x2018 and +0x3BD0.",
    ),
    0x141E65C60: (
        "ConstructUpperPlayerManager3FD8_Size5E70",
        "Allocator wrapper for the large upper Player manager stored at "
        "outer_owner+0x3FD8. Allocates size 0x5E70 and then jumps into "
        "0x141E66090 for in-place initialization.",
    ),
    0x141E66090: (
        "InitializeUpperPlayerManager3FD8",
        "In-place initializer for the large upper Player manager at "
        "outer_owner+0x3FD8. Stamps vtable 0x142B91B60, stores the outer owner "
        "in +0x10, seeds large internal tables and state bands, and prepares "
        "late links around +0x830/+0x838/+0x840 and +0x5E50/+0x5E60.",
    ),
    0x141E668C0: (
        "RegisterUpperPlayerBridgeIntoManager3FD8AndBuildEntryHandlers",
        "Manager-side registration path for outer_owner+0x3FD8. Stores the "
        "incoming owner-derived value in +0x18, stores the tiny upper bridge "
        "wrapper in +0x20, calls wrapper->virtual +0x18, derives an entry "
        "count through manager->virtual +0x38, allocates arrays in +0xD8/+0xE0, "
        "and builds per-entry handler objects keyed by authored/state tags. "
        "The current body allocates at least one trivial compact handler shell "
        "and one heavier 0x290 per-entry handler that stores the bridge wrapper "
        "at +0x288 and owner-derived state bands across +0x1C0..+0x284.",
    ),
    0x141E52590: (
        "BuildFixedTwoEntryUpperPlayerAuthoredDescriptor",
        "Shared upper bridge/helper that allocates a compact descriptor object "
        "with count=2, allocates a two-entry dword array, and fills it with "
        "two fixed authored ids derived from short baked byte sequences before "
        "returning the descriptor to manager-side registration paths like "
        "0x141E668C0.",
    ),
    0x141E65BE0: (
        "GetRegisteredUpperPlayerBridgeEntryCountFromManager3FD8",
        "Reads manager+0x840 and returns DWORD [that_object+0x0]. Used by the "
        "manager registration path as the entry count for upper bridge-derived "
        "handler arrays.",
    ),
    0x141E66D50: (
        "BindUpperPlayerAnimSnapshotManager3FD8FromInitPacketAndOwner4260State",
        "Top-level upper-manager bind path called directly from Player init. "
        "Consumes a compact typed packet/builder plus owner-local state at "
        "outer_player+0x4260, derives authored hash families, stores a "
        "compact object into manager+0x838, and dispatches handler->virtual "
        "+0x10 across all registered upper manager entries. The typed packet "
        "entries wired into this path now resolve to AnimSnapshotLayer setup "
        "families, so +0x3FD8 should be treated as an upper animation/"
        "snapshot manager rather than the final Player ownership surface.",
    ),
    0x141E69AA0: (
        "ResetUpperPlayerAnimSnapshotManager3FD8PerEntryStateAndInvalidateHandlers",
        "Upper Player manager phase used from the high-level Player update path. "
        "Clears repeated per-entry counters/pointers across the manager body "
        "and then iterates registered handlers in +0xD8 via handler->virtual "
        "+0xD8, passing owner/context state from +0x10, +0x170, +0x838, and "
        "+0x850. This is now best read as the invalidate/reset phase of the "
        "upper Player AnimSnapshotLayer manager.",
    ),
    0x141E69BD0: (
        "RebuildUpperPlayerAnimSnapshotManager3FD8EightSlotAggregateAndPublishToHandlers",
        "Upper Player manager rebuild/publish phase. Reinitializes the shared "
        "state band at +0x170, builds a temporary 8-slot aggregate from "
        "owner-derived state, and then pushes that aggregate back into all "
        "registered handlers through handler->virtual +0xE0. Current typed "
        "handlers and nearby strings tie this phase to upper AnimSnapshotLayer "
        "publication rather than to direct Player ownership rebinding.",
    ),
    0x141A77430: (
        "RelocateAnimSnapshotLayerSetupEntry04PointersIfInRange",
        "Relocation helper for the typed `0x04` upper bind packet entry. "
        "Rebases embedded pointers at +0x10/+0x18/+0x40/+0x48 when they fall "
        "inside the moving range, matching the entry vtable at 0x142B35140.",
    ),
    0x141A774B0: (
        "CheckAnimSnapshotLayerSetupEntry04HasAttachedSources",
        "Readiness/helper check for the typed `0x04` upper bind packet entry. "
        "Returns true when either +0x40 or +0x48 is non-null.",
    ),
    0x141A774E0: (
        "AssertThenDispatchAnimSnapshotLayerSetupEntry04ViaVirtual58",
        "Diagnostic/assert wrapper for the typed `0x04` AnimSnapshotLayer "
        "setup entry. Validates the variant, then tail-dispatches the real "
        "operation through entry virtual +0x58.",
    ),
    0x141A77550: (
        "SeedAnimSnapshotLayerSetupEntry04PrimarySource",
        "Compact initializer for the typed `0x04` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the primary source "
        "pointer in +0x40.",
    ),
    0x141A77580: (
        "SeedAnimSnapshotLayerSetupEntry04SecondarySource",
        "Compact initializer for the typed `0x04` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the secondary source "
        "pointer in +0x48.",
    ),
    0x141A77A20: (
        "RelocateAnimSnapshotLayerSetupEntry08PointersIfInRange",
        "Relocation helper for the typed `0x08` upper bind packet entry. "
        "Rebases embedded pointers at +0x10/+0x18/+0x40 when they fall "
        "inside the moving range, matching the entry vtable at 0x142B352E0.",
    ),
    0x141A77A80: (
        "CheckAnimSnapshotLayerSetupEntry08HasAttachedSource",
        "Readiness/helper check for the typed `0x08` upper bind packet entry. "
        "Returns true when +0x40 is non-null.",
    ),
    0x141A77AA0: (
        "AssertThenDispatchAnimSnapshotLayerSetupEntry08ViaVirtual58",
        "Diagnostic/assert wrapper for the typed `0x08` AnimSnapshotLayer "
        "setup entry. Validates the variant, then tail-dispatches the real "
        "operation through entry virtual +0x58.",
    ),
    0x141A77B50: (
        "SeedAnimSnapshotLayerSetupEntry08PrimarySource",
        "Compact initializer for the typed `0x08` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the primary source "
        "pointer in +0x40.",
    ),
    0x141A77B90: (
        "AppendResolvedAnimSnapshotLayerRecordIntoUpperPacketWriter",
        "Takes a resolved authored record from the entry source object, packs "
        "it into the destination packet writer as a `0x0A000C`-shaped chunk, "
        "and advances the writer cursor by 10 dwords.",
    ),
    0x140CB5050: (
        "WriteKeyedByteFlagIntoPlayerOwner3BD0Registry",
        "Resolves/creates a keyed entry inside outer_owner+0x3BD0 and writes "
        "a one-byte payload into it. Used by upper Player action-policy paths "
        "to publish compact verb/state flags like ReloadWeapon, RechamberWeapon, "
        "kRopeClimb, and piton-stab-fx.",
    ),
    0x141099520: (
        "BuildUpperPlayerGesturePermitContextFromMountedGraph",
        "Upper Player verb/gesture policy builder. Gathers state from mounted "
        "siblings +0x1E50 and +0x1E58, owner-local runtime state at +0x4320, "
        "and higher Player surfaces +0x1DF0, +0x1D78+0x120, and +0x3BD0 to "
        "produce a local gesture/action context block.",
    ),
    0x141099175: (
        "QueryUpperPlayerGesturePermitFamily2018WithBuiltContext",
        "Calls 0x141099520 to build a local gesture/action context, then feeds "
        "that context into the upper Player family object at outer_owner+0x2018. "
        "This is a live upper PlayerGesturePermit/action-policy path above the "
        "mounted graph, not just a lower weapon-action helper.",
    ),
    0x14109BE19: (
        "ConsumeUpperGesturePermitResultAndPublishKeyedPolicyFlag",
        "Consumes the compact result returned by the upper +0x2018 family, "
        "stores the returned scalar/state-id into local verb state, and then "
        "publishes a keyed one-byte policy/action flag back into outer_owner+0x3BD0. "
        "This ties the upper gesture/action-permit family directly to the "
        "owner-local registry surface.",
    ),
    0x140CB5050: (
        "WriteKeyedByteFlagIntoPlayerOwner3BD0Registry",
        "Takes rcx = outer_owner+0x3BD0, edx = hashed key, r8 = pointer to a "
        "one-byte payload, resolves/allocates storage through the internal "
        "tables at object+0xB8, copies the byte, and then dispatches a "
        "follow-up registry update. This makes +0x3BD0 an owner-local keyed "
        "byte/flag registry, not just a passive lookup cache.",
    ),
    0x1411A65C0: (
        "ConstructMountedPlayerSubsystem1DE0_Size6090",
        "Constructor for the mounted outer_owner+0x1DE0 Player subsystem. "
        "Seeds several large internal tables/subregions and calls the mounted-"
        "player action-family hash checker 0x1410F75C0 during setup. This is "
        "a substantial member of the real Player action-owner graph.",
    ),
    0x140F187B0: (
        "ConstructMountedPlayerSubsystem1D48_Size690",
        "Constructor for the mounted outer_owner+0x1D48 Player subsystem. "
        "Lives in the same early Player::Init band as the later qualification/"
        "policy owners and should be treated as part of the same promotion map.",
    ),
    0x14100D5A0: (
        "BuildMountedPlayerSubsystem1D50",
        "Direct builder for outer_owner+0x1D50 in the same early Player::Init "
        "mount band surrounding the later qualification and presentation "
        "owners.",
    ),
    0x141301710: (
        "ConstructMountedPlayerSubsystem1DA0_Size52C0",
        "Heavy mounted Player subsystem constructed into outer_owner+0x1DA0. "
        "This sits in the same investigate/read-mode/presentation region as "
        "other real Player owners, not in a helper-only bucket.",
    ),
    0x140FF5560: (
        "BuildMountedPlayerSubsystem1DE8",
        "Direct builder for outer_owner+0x1DE8 in the main Player::Init "
        "assembly pass. This is another formal mounted Player subsystem.",
    ),
    0x140F61490: (
        "DispatchMountedPlayerCallbackIntoOuter1FB0ReadyPath",
        "Small callback helper that resolves an owner-side object, queries "
        "outer_owner+0x1FB0 via virtual +0x78, and if present tail-calls the "
        "same dispatch object via virtual +0x48. This is a direct ingress into "
        "the outer Player verb-dispatch hub.",
    ),
    0x140F614E0: (
        "RouteAuthoredPlayerEventIdIntoOuter1FB0Dispatch",
        "Callback-family dispatcher under vtable 0x142A35090. Reads an "
        "authored event id from the incoming record and routes directly into "
        "outer_owner+0x1FB0 via virtual +0x100 or +0x128, with a few compact "
        "event ids only toggling local callback state bytes.",
    ),
    0x140F615B0: (
        "ConstructMountedPlayerEventCallback_40",
        "Allocating constructor for the compact authored-event callback object "
        "with vtable 0x142A35090. Stores the outer Player owner pointer and "
        "feeds the callback family that routes events into +0x1FB0.",
    ),
    0x140F61610: (
        "InitializeMountedPlayerEventCallbackInPlace",
        "In-place initializer for the compact authored-event callback family "
        "with vtable 0x142A35090.",
    ),
    0x140F5F920: (
        "ConstructMountedPlayerDispatchHub1FB0_Size3E0",
        "Allocating constructor for the real outer_owner+0x1FB0 dispatch hub. "
        "Allocates 0x3E0 bytes, clears the block, and tail-calls the in-place "
        "initializer at 0x140F5F980.",
    ),
    0x140F5F980: (
        "InitializeMountedPlayerDispatchHub1FB0",
        "In-place initializer for the real outer_owner+0x1FB0 dispatch hub "
        "with vtable 0x142A34E10. Seeds the state bands later consumed by the "
        "outer verb orchestrator and authored-event callback family.",
    ),
    0x140F60680: (
        "ProcessOuterPlayerMountedVerbOrchestrator",
        "Large outer Player-side verb orchestrator. Chooses among mounted "
        "subsystems (+0x1DE0/+0x1E98/+0x1E50/+0x1EB0/+0x1DB8), uses the "
        "+0x1FB0 dispatch object heavily, and commits through owner virtuals "
        "+0xBD0/+0xC78/+0xC98/+0xCB8.",
    ),
    # --- Input pipeline (2026-06-16 capstone trace; toward redirecting control) ---
    0x141A99F40: (
        "XInputGetStateWrapper",
        "Thin per-pad wrapper: reads pad-object index at +0x0, calls "
        "XInputGetCapabilities(idx) then XInputGetState(idx, &pad+0x20); sets "
        "connected byte at pad+0x4. Pad object layout: index +0x0, connected "
        "+0x4, caps +0xC, XINPUT_STATE +0x20 (sThumbLX +0x28).",
    ),
    0x141A9A790: (
        "InputManager_PollPads_SingleActivePad",
        "Pad poll loop of the single global input manager (vtable base "
        "0x142B37E50, ctor 0x14179C280, size 0x70). Iterates 4 pad slots at "
        "mgr+8..+0x20; mgr+0x28 = active pad; mgr+0x58 = round-robin scan "
        "counter. GetCapabilities gates GetState per index. Picks the FIRST "
        "connected pad with input as the active pad -> drives THE one player "
        "(campaign = single-player input, no pad-index->player-index routing).",
    ),
    0x141A99FD0: (
        "InputManager_NormalizeStick",
        "Normalizes a pad's XINPUT_STATE into the shared input-state: reads "
        "pad+0x28/+0x2A (int16 LX/LY), /32768, clamps, writes normalized LX/LY "
        "to dst+0x10/+0x14 (RX/RY follow). dst (rdx) = the input-state GLOBAL at "
        "module RVA 0x66E4220.",
    ),
    0x14179C280: (
        "InputManager_Init",
        "Creates the single global input manager (heap, 0x70) + outer 0xf0 "
        "object; registers a Steam callback. Called once from the engine init "
        "0x140155760 (no per-player loop) => single-player input architecture.",
    ),
    0x14179C170: (
        "Input_AnyAxisNonzeroPredicate",
        "Returns whether any of several float axes (rbx+0/4/8/0xc, "
        "+0x4E8..+0x504) are non-zero (deadzone/has-activity test). Used by the "
        "focus dispatch to decide if a consumer has live input.",
    ),
    0x14179E310: (
        "Input_FocusDispatch",
        "Distributes the input-state (global 0x66E4220) to the consumer array "
        "0x146B2C620 (count 0x146B1B55C). For each consumer calls "
        "*(consumer)+0x30(input-state); FIRST that returns true claims focus "
        "(stored at 0x146B2C618) and the loop breaks. This is the UI/overlay "
        "input-focus stack (null in plain gameplay); gameplay player-control is "
        "the downstream default path.",
    ),
    0x14183B410: (
        "Input_ScaleCopyVirtual",
        "Converts one source input block into a destination Joypad. The direct "
        "analog copy is gated by g_InputWindowFocused and GameInfo keyboard mode; "
        "keyboard replacements exist only for destination indexes 0 and 2. "
        "Destination Joypad 3 therefore needs the narrow background analog route.",
    ),
    0x141836640: (
        "JoypadState_Construct",
        "Constructs the 0xBE9-byte per-slot Joypad state used at "
        "FrameState+0xDB0+slot*0xBF0, then calls JoypadState_Reset.",
    ),
    0x1418369C0: (
        "JoypadState_Initialize",
        "Initializes a per-slot Joypad state, including its callback array and "
        "command tables, then calls JoypadState_Reset.",
    ),
    0x141836BB0: (
        "JoypadState_Reset",
        "Resets the per-slot Joypad state and installs the joypad-default command.",
    ),
    0x14179EA90: (
        "Input_ScaleCopyThunk",
        "Thunk: forwards to Input_ScaleCopyVirtual(a4, a3, a5); then optionally "
        "sub_141836BB0(a4) if a1+0x10 set.",
    ),
    0x140289150: (
        "FrameState_PerSlotInputApply",
        "Per-slot input/camera apply = FrameState vtable+0x20 (FrameState = "
        "*(g_LocalPlayerInputManager_Ptr 0x143FE1088), RTTI FrameState, vtable "
        "RVA 0x28FB968). Args (mgr, vtbl, inputblock(rbx), slotstruct(a4), "
        "slot(a5)). Processes per-slot camera heading (cos/sin) into the input "
        "block and calls Input_ScaleCopyVirtual. PER-SLOT ENABLE GATES: byte at "
        "FrameState+0x70B8+slot and struct at FrameState+0x6FC8+0x28*slot; both "
        "set for slot 0 (active), 0 for slots 1-5 (idle). Activating slot 1 "
        "(gates + player binding + feeding its input block 0x1466E4220[166*1]) "
        "is the native per-viewport/per-player path to a real player 2.",
    ),
    0x140289A90: (
        "Input_BlendAxisByThresholds",
        "Pure arithmetic helper used by the circular scripted-input branch. "
        "Selects or linearly blends bounded axis constants around a threshold; "
        "it has no state writes and does not activate a player slot.",
    ),
    0x1402893D0: (
        "FrameState_PerSlotInputApply_Branch",
        "Branch of the per-slot apply taken when FrameState+0x6FC8+0x28*slot is "
        "set (slot has an active per-slot input struct).",
    ),
    0x14179E6C0: (
        "Input_PerPlayerDispatch_MgrArg",
        "Sibling of Input_FocusDispatch taking the manager as arg a1. Same body: "
        "UI focus loop over consumer array, then a 6-SLOT loop applying each "
        "slot's input-state block &qword_1466E4220[166*idx] via "
        "(*mgr+0x20)(mgr,*mgr,inputblock,slotstruct,slot). This is the engine's "
        "native PER-PLAYER input distribution (6 slots; slot 0 active in "
        "campaign, slots 1-5 idle). Manager = *(0x143FE1088); slot structs at "
        "mgr+0xDB0 + slot*0xBF0. The loop cursor is mgr+0xE88 and the input-block "
        "selector is cursor+0x18 = Joypad+0xF0. Per-slot apply = manager vtable+0x20.",
    ),
    # --- Player fire and native network-animation path (2026-06-29) ---
    0x140E2C730: (
        "PlayerFireController_Fire",
        "Confirmed live campaign fire core (game/player/player-fire.cpp). "
        "controller+0x108 is the owning Player; R8 is the active weapon and "
        "RDX points to FireArgs. Runtime hooks fired for both local roles.",
    ),
    0x140A46D70: (
        "SendNetEventEmitProjectileBullet",
        "Multiplayer-gated wrapper for NetEventEmitProjectileBullet. It is not "
        "the campaign fire source and produced no events in campaign runtime.",
    ),
    0x140A45520: ("NetEventEmitProjectileBullet_Ctor", "Constructor for the projectile-bullet network event."),
    0x140A45B40: ("NetEventEmitProjectileBullet_Read", "Deserializes a projectile-bullet network event."),
    0x140A45C90: ("NetEventEmitProjectileBullet_Write", "Serializes a projectile-bullet network event."),
    0x140A45E80: ("NetEventEmitProjectileBullet_Execute", "Executes a received projectile-bullet network event."),
    0x1418BB6D0: (
        "BuildAnimationEffectEntryList",
        "Builds 32-byte animation-effect entries containing an id, pointers, "
        "and flags. The output is not a joint-transform array and must never "
        "be copied between processes.",
    ),
    0x140BC4E60: (
        "NetEventCharacterMove_FillAnimCmdBitStream",
        "Serializes the native animation command list into a 4096-bit stream. "
        "Handles clips, blending, pose/snapshot commands, hierarchy and output "
        "commands; this is the current source-side synchronization target.",
    ),
    0x140BC2970: (
        "NetEventCharacterMove_FillPlayer",
        "Fills a CharacterMove event for a Player and invokes "
        "FillAnimCmdBitStream. Requires a live NetPlayerTracker, so the core "
        "serializer must be called directly in campaign.",
    ),
    0x140BC3760: ("NetEventCharacterMove_FillNpc", "NPC CharacterMove event fill path; also invokes FillAnimCmdBitStream."),
    0x140BBD730: (
        "NetEventCharacterMove_Ctor",
        "In-place constructor for the 0x610-byte NetEventCharacterMove object; "
        "initializes its 512-byte animation bit buffer and event fields.",
    ),
    0x140B87B00: (
        "NetSnapshot_Ctor",
        "In-place constructor for the native NetSnapshot wrapper. Initializes "
        "fields through +0x710; allocate at least 0x720 bytes.",
    ),
    0x140BC11A0: (
        "NetEventCharacterMove_ToNetSnapshot",
        "Exact event-to-snapshot converter. Sets snapshot+0x688 to event+0x1C, "
        "copies the animation byte count/time, flags, weapons and movement "
        "metadata. This closes the local animation replication chain.",
    ),
    0x140BC2240: (
        "NetEventCharacterMove_FillWeaponState",
        "Reads eight live WeaponController slots from Player+0x1D88, records "
        "their compact art ids and states at event+0x3DC/+0x3FC, and fills "
        "selected weapon, throwable and detailed per-weapon state fields.",
    ),
    0x140BC1660: (
        "NetEventCharacterMove_PublishSnapshotToCharacter",
        "Builds a NetSnapshot from a CharacterMove event and publishes it to "
        "the supplied character through the native state-record path.",
    ),
    0x140B87820: (
        "NetSnapshot_GetAnimArtGroup",
        "Resolves one of 32 configured player/NPC animation art groups from "
        "*net-player-anim-art-groups* or *net-npc-anim-art-groups*.",
    ),
    0x1410D6530: (
        "Player_InitializeNetAnimArtGroups",
        "Clears Player+0x3FC8 and, when the multiplayer mode byte is set, "
        "allocates a 32-pointer table and fills it through "
        "NetSnapshot_GetAnimArtGroup. Campaign leaves this field null.",
    ),
    0x140A00BB0: (
        "NetAnimCmdGenerator_Initialize",
        "Initializes NetAnimCmdGenerator ownership and its own 32-entry "
        "animation art-group table before resetting snapshot storage.",
    ),
    0x140A01970: (
        "NetAnimCmdGenerator_RefreshAnimArtGroups",
        "Rebuilds the generator's 32-entry animation art-group table.",
    ),
    0x140BBD9D0: ("NetEventCharacterMove_Dtor", "Destructor for the 0x610-byte NetEventCharacterMove object."),
    0x140BBE5C0: ("NetEventCharacterMove_Read", "Deserializes NetEventCharacterMove."),
    0x140BBF000: ("NetEventCharacterMove_Write", "Serializes NetEventCharacterMove."),
    0x1403D01B0: (
        "NetAnimCmdGenerator_Ctor",
        "Initializes the NetAnimCmdGenerator embedded in each Player at +0x1B30.",
    ),
    0x140A090A0: ("NetAnimCmdGenerator_Dtor", "Destroys the Player-embedded NetAnimCmdGenerator."),
    0x140A01E50: (
        "NetAnimCmdGenerator_AddSnapshot",
        "Copies a received animation snapshot (up to 0x200 bytes) into the "
        "Player-embedded generator queue. Snapshot time is at +8 and payload "
        "pointer at +0x688 in the source wrapper.",
    ),
    0x140A02890: ("NetAnimCmdGenerator_ProcessSnapshot", "Consumes one queued native animation snapshot."),
    0x140A02820: ("NetAnimCmdGenerator_ProcessPendingSnapshots", "Pumps pending snapshots through ProcessSnapshot."),
    0x140A038B0: ("NetAnimCmdGenerator_GetOldestIceSnapshot", "Returns the oldest queued ICE animation snapshot."),
    0x140A09340: (
        "NetAnimCmdGenerator_DualSnapshotNodePool_Allocate",
        "Allocates a dual-snapshot node used by the native animation generator.",
    ),
    0x140A07160: (
        "NetAnimCmdGenerator_UpdateFlagsAndWeapons",
        "Applies snapshot flags and weapon state while reconstructing remote "
        "animation. This may solve visible weapons together with animation.",
    ),
    0x141BB7960: (
        "Player_PublishWeaponInventoryEntry",
        "Player virtual +0xD0 weapon-entry sink. Resolves the weapon controller "
        "through virtual slot 24 and forwards the structured entry to "
        "WeaponController_ApplyInventoryEntry; defers while unavailable.",
    ),
    0x141E72540: (
        "WeaponController_ApplyInventoryEntry",
        "Applies one structured weapon inventory entry through immediate, "
        "replacement or deferred controller paths. The entry is more than a "
        "bare held-weapon id.",
    ),
    0x1410EDFB0: (
        "Player_KickPlayerDepenetrateWeapons",
        "Resolves targRWrist/targLWrist/rightHand/leftHand skeleton joints and "
        "writes wrist weapon-attachment transforms through the embedded object at "
        "Player+0x3530 into Player+0x3600/+0x3608.",
    ),
    0x14023AEA0: (
        "InputObject_MatchesActiveLocalJoypad",
        "Returns true when object+0x44 has bit 0x80000 and object+0x50 matches the "
        "active joypad index from g_LocalPlayerInputManager (+0x48/+0x50). Gates "
        "weapon-attachment refresh at Player+0x3530.",
    ),
    0x140A80E50: (
        "WeaponArtId_ToNetworkIndex",
        "Maps Weapon+0x860 authored art id to the compact network inventory "
        "index. Used independently by projectile events and all eight entries "
        "in NetEventCharacterMove_FillWeaponState.",
    ),
    0x140A81050: (
        "NetGameManager_GetWeaponIdByStatIndex",
        "Inverse compact-index mapping. Embedded source assertions identify "
        "NetGameManager::GetWeaponIdByStatIndex at net-game-manager.cpp:534-535; "
        "indices >=19 resolve through the cached online weapon definition map.",
    ),
    0x1410A4D40: (
        "PlayerWeaponController_GiveWeapon",
        "Exact embedded signature: PlayerWeaponController::GiveWeapon(WeaponInfo*, "
        "bool, bool, BuffSource const*). Controller vtable slot +0x158.",
    ),
    0x1410A1F90: (
        "PlayerWeaponController_GetWeaponBySlot",
        "Controller vtable slot +0xB0. Bounds slot to 0..22 and resolves the "
        "live weapon process handle; ProcessWeapon authored id is at +0x860.",
    ),
    0x1410A1C10: (
        "PlayerWeaponController_SwitchToWeapon",
        "Controller vtable slot +0x40. Validates the requested inventory slot, "
        "runs the concrete switch implementation at vtable +0x48, records the "
        "requested slot at controller+0x130 when the third argument is set, and "
        "returns success independently of later visual attachment.",
    ),
    0x1410A28A0: (
        "PlayerWeaponController_ApplyWeaponSwitch",
        "Controller vtable slot +0x48 and the concrete weapon switch state "
        "machine. Resolves the requested and current weapon objects, applies "
        "holster/unholster transitions, and rejects invalid controller states.",
    ),
    0x1410A49E0: (
        "PlayerWeaponController_GetHandAttachedWeaponSlot",
        "Implementation behind controller vtable slot +0x78. Scans inventory "
        "slots and returns the slot whose attachment resolves to rightHand or "
        "leftHand and whose attachment owner is this Player; returns -1/20 fallback.",
    ),
    0x1410A4CE0: (
        "PlayerWeaponController_GetRequestedInHandsSlot",
        "Controller vtable slot +0x90. Returns controller+0x130, which records "
        "the requested in-hands slot but does not prove visual attachment.",
    ),
    0x1410A1FE0: (
        "PlayerWeaponController_GetCurrentWeapon",
        "Controller vtable slot +0xB8. Resolves slot 19 when controller+0x120 "
        "equals 19; otherwise resolves the current slot byte at +0x12C.",
    ),
    0x1410D67B0: (
        "Player_GetActiveWeapon",
        "Player vtable slot +0x8C0. Resolves the special slot-19 weapon or the "
        "PlayerWeaponController current weapon, with a screen-route fallback.",
    ),
    0x140E28A30: (
        "PlayerFireController_UpdateInputAndFire",
        "Per-frame fire controller update. Reads fire-hold/fire-press/fire-release "
        "from the owner's Joypad and runs the weapon-use, input and shot-readiness gates.",
    ),
    0x140E2ACA0: (
        "PlayerFireController_CheckFireInput",
        "Resolves Player vtable +0x8C0 active weapon, validates weapon state, "
        "then consumes controller+0x1E8 hold or +0x1E9 press before action exclusions.",
    ),
    0x140E2ADF0: (
        "PlayerFireController_CheckActionExclusions",
        "Rejects firing during authored Grenade, Grenade-Add, Grapple, "
        "Grapple-Add and gun-up-layer action states.",
    ),
    0x140E2B220: (
        "PlayerFireController_CheckShotReadiness",
        "Final active-weapon readiness check: rejects Drive and incompatible "
        "player states, then checks weapon type, ammo and shot timing.",
    ),
    0x1410B15A0: (
        "PlayerWeaponController_CanUseWeapon",
        "Controller vtable slot +0x260. Broad player/action/camera/traversal gate "
        "called by PlayerFireController_UpdateInputAndFire before a shot.",
    ),
    0x1401B7600: (
        "WeaponInfo_InitializeDefaults",
        "Initializes the 0xC0-byte WeaponInfo used by the developer give-weapon "
        "path; authored weapon id is subsequently written at +0x80.",
    ),
    # --- Input block-selector bus + HUD is-aiming publish chain (2026-07-04) ---
    0x14179D780: (
        "FrameState_ConstructJoypadSlots",
        "FrameState/input-manager constructor part that initializes the six "
        "per-slot Joypad states via JoypadState_Initialize(slot, selector, "
        "mode): selectors are {0,1,0,1,0,1} and mode is 3 for slots 0-1 (real "
        "local-player lanes) and 0 for slots 2-5. The selector byte lands at "
        "Joypad+0xF0 and picks WHICH g_InputStateBlocks entry the slot "
        "consumes in Input_PerPlayerDispatch: even slots read block 0, odd "
        "slots read block 1. Slot 3 (co-op puppet joypad) therefore shares "
        "input block 1 with slot 1 = the engine's local-player-1 lane; "
        "anything left in block 1 outside slot 3's conversion is read as "
        "local-player-1 input (co-op crosshair/aim HUD leak root cause).",
    ),
    0x140C91970: (
        "PlayerHud_ScreenStatePublishUpdate",
        "Per-frame player/HUD state publish driven from the global player "
        "handle at 0x143EEBE80 (entry stub 0x1401656B0). Reads the screen "
        "index at a1+0x4010, then runs "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes on the per-screen "
        "HUD state array 0x143EF1E30 (0x100 stride). Also binds "
        "g_LocalPlayerInputManager_Ptr+0xE0 into type-512 HUD records.",
    ),
    # --- Per-screen HUD reticle publisher family (2026-07-04) ---
    0x14135A650: (
        "PlayerHudScreen_ReticleProcessPublisher",
        "Per-screen HUD reticle publisher over g_PerScreenPlayerHudState "
        "(screen/slot index at state+0xF8): spawns/kills the ss-hud-reticle "
        "process and publishes reticle-show-time / update-reticle-aspect. "
        "Reads GetRetainedPlayerEntityBySlot(*(state+0xF8)) and the slot's "
        "Joypad at g_LocalPlayerInputManager_Ptr+0xBF0*(slot+2). A screen-1 "
        "(co-op puppet) publish spawns the crosshair onto the single shared "
        "viewport — gated by the co-op mod when *(state+0xF8)!=0.",
    ),
    0x14135AF20: (
        "PlayerHudScreen_AmmoReticlePublisher",
        "Sibling per-screen HUD publisher (same state+0xF8 screen index): "
        "publishes total-ammo and also spawns ss-hud-reticle / "
        "reticle-show-time records. Called from "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes. Gated by the co-op "
        "mod for screen!=0.",
    ),
    0x14135C050: (
        "PlayerHudScreen0_BlindfireReticlePublisher",
        "Blindfire reticle publisher hardwired to HUD screen 0 through the "
        "fixed aliases qword_143EF1F20 (= screen-0 state+0xF0) and "
        "qword_143EF1F28 (= screen-0 state+0xF8): show-blindfire-reticle, "
        "ss-hud-blindfire-reticle, blindfire-weapon-accuracy publishes.",
    ),
    0x1413B42E0: (
        "PlayerHudScreen_ReticlePublishCaller",
        "Fills the per-screen HUD record (flags from the player handle pair "
        "at g_PlayerHandleForHudPublish[2*idx]) and tail-calls "
        "PlayerHudScreen_ReticleProcessPublisher with "
        "&g_PerScreenPlayerHudState[32*idx].",
    ),
    0x1401656B0: (
        "PlayerHud_GlobalHandleUpdateEntry",
        "Frame entry that validates the global player handle "
        "(ptr+id pair at 0x143EEBE80, id checked against object+0xBC), sets "
        "object+0x72=1 and calls PlayerHud_ScreenStatePublishUpdate. A "
        "sibling handle at 0x146A8C838 gets vtable+0x1E0 instead.",
    ),
    # --- Sniper scope reticle (is-scope-visible) path (2026-07-05) ---
    0x140C177F0: (
        "SniperReticleManager_QueryScopeVisible",
        "Resolves the PROCESS-GLOBAL ss-hud-sniper-reticle-manager (string "
        "0x142A03110) via the property registry dword_146466FE0, then queries "
        "its 'is-scope-visible?' property (mgr+168 owner, id mgr+188; property "
        "vfunc +0x208). Returns bool = full-screen sniper scope reticle should "
        "draw. Prologue 48 89 5C 24 08 ... 55. ONLY caller = "
        "ReticleManager_Update. The property is global: it goes true when EITHER "
        "the local player OR the co-op Second player enters scoped-rifle ADS, so "
        "on the single physical viewport a Second-player scope leaks here. The "
        "co-op mod detours this (kScopeVisibleQueryRva 0xC177F0) and forces 0 "
        "when the LOCAL player is not scoped (Player+0x1548==0).",
    ),
    0x140C193A0: (
        "ReticleManager_Update",
        "Per-frame reticle/HUD manager update in the main HUD chain "
        "(HudUpdate_Main 0x140C149A0). Operates on the primary screen: reads "
        "g_PlayerHandleForHudPublish (local player) and ResolveScreenRouteEntity"
        "ByIndex(0). Sole caller of SniperReticleManager_QueryScopeVisible "
        "(0x140c19abc) — the scope reticle draw is gated on that query result.",
    ),
    0x140C149A0: (
        "HudUpdate_Main",
        "Main per-frame HUD update: evaluates Joypad commands "
        "(Joypad_IsCommandActive), resolves the strafe/aim camera "
        "(CameraControlStrafe vtable off_143054560), and calls "
        "ReticleManager_Update. Reads g_LocalPlayerInputManager_Ptr HUD state.",
    ),
    # --- Co-op aim/shot direction source (2026-07-05) ---
    0x140E2C730: (
        "PlayerFireController_Fire",
        "Confirmed live campaign fire core (game/player/player-fire.cpp). Owner "
        "controller at arg+0x108; weapon passed separately. AIM/SHOT DIRECTION "
        "is resolved per the FIRING PLAYER's screen index: "
        "ResolveScreenRouteEntityByIndex(*(controllerOwner+0x4010)) then "
        "SelectPreferredCameraChildFromRouteEntity(...,0), checking the child "
        "descriptor at +0x1A8 against off_143054560 (aim/strafe camera kind) "
        "(e.g. line ~999 unzoomed-sniper-shots branch, and the main direction "
        "path). So a co-op Second player (screen index 1 at +0x4010) fires along "
        "its OWN screen-1 aim camera, which the mod does not drive -> wrong "
        "direction. The transform is read via CameraManager_GetBlendedCamera"
        "Transform from g_PerScreenCameraTransformCache[0x400*idx] / a blended "
        "child transform (NOT CameraManager+0x1B0). The mod only blends peer "
        "camera yaw into the body quat (Player+0x140); the real fix must drive the "
        "screen-1 aim camera CHILD (CameraControlStrafe) orientation.",
    ),
    0x141C06650: (
        "CameraManager_GetBlendedCameraTransform",
        "Extracts a screen's current camera transform into the out buffer for "
        "consumers (e.g. PlayerFireController_Fire aim direction). Picks the "
        "active camera child via SelectPreferredCameraChildFromRouteEntity; if a "
        "single camera is active copies g_PerScreenCameraTransformCache "
        "(0x1465FDAA0, stride 0x400 per screen index *(mgr+292)), else builds a "
        "blend from mgr+0x1A0, then CameraState_CopyTransformAndParams -> out. "
        "The out camera 4x4 matrix is at out+0x350; params (fov) at out+0x390. "
        "Co-op aim-fix intercept: for screen index 1 overwrite out+0x350 rotation "
        "from peer.camera_quat.",
    ),
    0x14014BDA0: (
        "CameraState_CopyTransformAndParams",
        "Copies a camera state's 4x4 transform matrix (dst+0x350..+0x390, four "
        "xmmword) plus following params (+0x390 fov, +0x3C0.. and byte flags) "
        "from src to dst. Used by CameraManager_GetBlendedCameraTransform to emit "
        "the resolved per-screen camera transform.",
    ),
    # --- Fire ray source chain (2026-07-05) ---
    0x140E2BCD0: (
        "PlayerFireController_BuildFireArgsAndFire",
        "Sole caller of PlayerFireController_Fire. Builds FireArgs: seeds the aim "
        "ray via ResolveScreenRoutePoseSeedOrDefault(&seed, *(owner+0x4010)) "
        "(the firing player's screen index), cooks it through sub_140CADD60 into "
        "the FireArgs buffer, applies cover/strafe context (sub_1410E2950(idx)), "
        "then calls Fire. THE aim-direction entry point for the co-op Second "
        "player (screen index 1).",
    ),
    0x140E16DF0: (
        "PlayerGrenadeController_Construct",
        "Player grenade thrower init: resolves *grenade-default-arc-art* and "
        "*grenade-l1-r1-release-window* settings; owner stored at this+0x108 "
        "(a1[33]), owner link +0x3BD0. Sibling of the fire controller cluster.",
    ),
    0x140E17AC0: (
        "PlayerGrenadeController_ResolveContextSettingsAndPose",
        "Resolves the grenade-aim settings id (*aim-grenade* / *aim-grenade-move* "
        "/ *aim-grenade-cassie* / *zipline-grenade* / cover variants) and the "
        "GrenadeMove move-set for the current movement context, fetching the "
        "camera pose seed TEN times via ResolveScreenRoutePoseSeedOrDefault"
        "(&seed, *(owner+0x4010)) — the SAME per-screen fire-ray source as "
        "PlayerFireController_BuildFireArgsAndFire. Called by the trajectory "
        "solver 0x140E1B000 ('Grenade Settings: %s' debug print). The co-op "
        "pose-seed hook for screen index 1 therefore fixes both bullet fire and "
        "grenade arc/throw direction for the Second player.",
    ),
    0x1414115B0: (
        "VehicleCtrl_NotifyPlayerEntering",
        "VehicleCtrl::NotifyPlayerEntering(const DC::VehicleEntryDef*): hashes "
        "'vehicle-entry-def' (0x142926668), resolves controller sub-objects via "
        "0x14142B890(...,2/3), packages 0x150 records, copies the VehicleEntryDef "
        "payload (+0xA8/+0xBC), serializes via 0x1417F8B00, and pushes the entry "
        "record into the live rider actor via actor->virtual+0xD0. The bridge for "
        "co-op vehicle entry (driver/passenger seat).",
    ),
    0x140B54F30: (
        "ScriptCmd_NetTaskComplete_MpOnly",
        "'net-task-complete!' handler — MP-ONLY: gated on the net-mode flag "
        "HIBYTE(dword_143E07470); its 'local' helper sub_140A5D3F0 is actually "
        "SendNetEventStartNetTask. Does NOTHING in campaign. This is the MP "
        "objective/valve layer, NOT the campaign progression path (see "
        "GameTaskGraphMgr_Update). Do not use for co-op campaign task sync.",
    ),
    0x141CB9820: (
        "GameTaskGraphMgr_Update",
        "GameTaskGraphMgr::Update (per-frame): drains and applies the campaign "
        "task-graph COMMAND list at g_GameTaskGraphCommandList (0x146A514A8, count "
        "0x146A51568, stride 0x28), advancing active subnodes (objectives). This "
        "IS the campaign progression backbone (script 'task-complete' issues "
        "commands via AddCommand). Co-op Option 1 follower-apply target: replay "
        "the driver's relayed task-graph commands here.",
    ),
    0x141CB95B0: (
        "GameTaskGraphMgr_AddCommand",
        "GameTaskGraphMgr::AddCommand (find-or-add). Searches g_GameTaskGraph"
        "CommandList (base 0x146A514A0, count g_GameTaskGraphCommandCount, stride "
        "0x28) for a matching 40-byte command; if absent appends it (assert "
        "'m_numQueuedTaskCommands < kMaxQueuedTaskCommands', kMax=5) copying "
        "record fields +0..+36. Record schema: +0 TYPE (1/2/3 = kinds; built by "
        "wrappers 0x141CB9700/9760/97C0), +4 subnode id (*(subnode+8)), +8 graph "
        "id (*(graph+8)), +12 param, +16 (-1 for types 2/3), +24 qword, +32 byte, "
        "+36 int. Co-op Option 1 follower-apply: build the driver's relayed "
        "command record and call this; GameTaskGraphMgr_Update then applies it.",
    ),
    0x141CB9700: (
        "GameTaskGraphMgr_AddCommand_Type1",
        "Typed AddCommand wrapper: builds a TYPE-1 task-graph command record from "
        "(graph a2, subnode) with param a4, then calls GameTaskGraphMgr_AddCommand.",
    ),
    0x141C8E680: (
        "TaskManager_UpdateAndQuery",
        "Large task/objective manager (0xF29) over the global task state at "
        "dword_146DAE7F0 (task-play-time / task-complete records dword_146DAE7F8"
        "..+0x14). Campaign progression backbone: scripted scenes and SsInstances "
        "gate on task state, so syncing tasks advances co-op narrative at the "
        "right level.",
    ),
    0x1408B1350: (
        "ScriptQuery_InteractableHasBeenInspected",
        "'interactable-has-been-inspected?' query core. Resolves the interactable "
        "by StringId, calls vtbl+0x248 to get its state object, returns "
        "state+0xD0 bit 1 = inspected flag. Co-op (story-authority Option 1): "
        "inspect/interact sync = relay {interactable StringId, kind} and set this "
        "flag on the follower's matching object (StringId is deterministic across "
        "copies) — no net session / NetEventInteractable needed.",
    ),
    0x140A45E80: (
        "NetEventEmitProjectileBullet_Execute",
        "Applies a received emit-projectile-bullet net event: reads campaign-valid "
        "globals (g_LocalPlayerInputManager_Ptr, g_PlayerHandleForHudPublish, "
        "g_PerScreenPlayerHudState), maps the weapon via "
        "NetGameManager_GetWeaponIdByStatIndex (static cached table, not a session "
        "singleton), spawns the real ProjectileBulletInstant + net-fire-effect. "
        "Not inherently session-locked except shooter resolution; documents the "
        "native bullet-replication primitive.",
    ),
    0x140A81050: (
        "NetGameManager_GetWeaponIdByStatIndex",
        "net-game-manager.cpp:534 inverse compact stat-index -> weapon-id map. "
        "Pure static lookup: GetCachedWeaponDryFireEventRegistry + "
        "g_StandardWeaponArtIds prefix + cached online weapon def map for "
        "index>=19. No session singleton deref (campaign-available).",
    ),
    0x14073E9C0: (
        "Camera_ResolveRemapByModeAndId",
        "Resolves a camera-remap entry by StringId from an authored registry "
        "table, selecting the table by mode: *net-camera-remaps* when net-mode "
        "(HIBYTE(g_NetModeFlags)), else default / *dlc-camera-remaps* / "
        "*devmenu-camera-remaps*. Confirms camera remaps are StringId-deterministic "
        "and natively networked in MP. Basis for co-op puzzle-camera spectate: the "
        "same remap resolves object-anchored on both copies.",
    ),
    0x140A162C0: (
        "NetCharacter_EventHandler",
        "NetCharacter::EventHandler — dispatches NetCharacterSsAnimateEventContext "
        "and calls NetAnimCmdGenerator_AddSnapshot. Confirms the native animation "
        "snapshot CAPTURE is bound to NetCharacter (a net-session object): in "
        "campaign the local Player is not a NetCharacter, so no snapshots are "
        "generated natively — full NetSnapshot animation sync needs a standalone "
        "capture against the Player's AnimControl. The APPLY half "
        "(ProcessSnapshot/UpdateFlagsAndWeapons) runs on a plain NdGameObject.",
    ),
    0x1408AC710: (
        "ScriptState_SuppressInteractableSystem",
        "'suppress-interactable-system' get/set (fact-backed, byte_1437E5A82). "
        "Gates the whole interactable system. Co-op: set on the FOLLOWER so its "
        "use/interact button never starts inspections/puzzles/pickups/conversation-"
        "pieces/treasure — the narrative-trigger disable for the non-driver "
        "(pairs with the joypad per-command disable for other action triggers).",
    ),
    0x14006D4D0: (
        "ScriptCmd_InteractableCameraRemapEnable",
        "'interactable-camera-remap-enable' handler (sibling ...-disable): turns "
        "on the interaction/puzzle camera remap (framed *remap-camera-int-* shots) "
        "for the interactable. The scripted 'puzzle camera'; in co-op the "
        "spectating follower shows it via the synced scene (object-anchored remap) "
        "or a screen-0 camera-transform relay during the spectate window.",
    ),
    0x1406B14D0: (
        "Npc_TeleportIntoVehicleSkill",
        "Npc TeleportIntoVehicle skill ('TeleportIntoVehicle: Teleporting %s into "
        "the car'; checks '%s is sitting in the car'). Teleports an Npc straight "
        "into a vehicle seat (no pathing) and runs the native enter (seat AP bind "
        "+ enter anim + role). RECOMMENDED path for a co-op passenger button since "
        "the puppet is an Npc — resolves the seat AP and does bind/anim/role for "
        "us; may need the puppet skill/behavior system momentarily active.",
    ),
    0x1403946D0: (
        "VehicleCtrl_BuildAddPassengerEvent",
        "Builds the 'add-passenger' event (vehicle-controller family "
        "0x140394B20..0x140395560): assigns the passenger role to a rider on the "
        "vehicle controller. set-driver is the driver counterpart; passenger-"
        "entered fires on completion.",
    ),
    0x1400BAB30: (
        "ScriptQuery_CarGetRiderByRole",
        "'car-get-rider-by-role' handler: returns the vehicle's rider actor for a "
        "role (driver/passenger). Confirms the vehicle owns its riders by role; "
        "riders bind to the seat via apReference + bindSpawner so a seated "
        "passenger follows the vehicle transform natively (co-op: only the vehicle "
        "object needs syncing).",
    ),
    0x14059D740: (
        "VehicleCtrl_BuildPassengerEntryState",
        "Passenger-seat entry state builder: hashes 'hero-passenger-a-enter-front' "
        "(0x142965D18) into controller state +0xA0, writes role at +0xB0 and an "
        "entry-state flag bundle at +0x12C, and commits target-actor entry "
        "transforms around +0x50/+0x70/+0x80. Best hook for seating the co-op "
        "Second player as passenger (with add-passenger/passenger-entered "
        "0x140394B20..0x140395560 and set-driver for the driver).",
    ),
    0x140D9C700: (
        "Vehicle_PlayerDrivingStateUpdate",
        "Publishes/updates the 'player-driving' state (car anim overlay / IGC "
        "driving flag) for the driving player. Part of driveable-car-controller / "
        "driveable-vehicle-controller. In two-copy co-op the driver is local on "
        "its own copy, so this runs natively there; the passenger copy only "
        "relays the vehicle object transform.",
    ),
    0x140BC9730: (
        "NetEventInteractable_ClusterAnchor",
        "One of the game/net/net-event/net-event-interactable.cpp functions "
        "(cluster ~0x140BC97xx). The engine's native replicated INTERACTABLE "
        "net-event type (sibling files: net-event-melee/npc/player/weapon/"
        "character-move/connection). Candidate transport for co-op inspection/"
        "interaction sync (story-authority model). Needs a standalone-apply check "
        "(works without a live net session?) before reuse.",
    ),
    0x141213C40: (
        "ScriptQuery_GetPlayerMeleeTarget",
        "'get-player-melee-target' handler. Reads the LOCAL player "
        "(g_PlayerHandleForHudPublish) -> player+0x1F60 (combat/melee "
        "controller) -> virtual +0x1C8 -> result+0xD8 = current melee target "
        "entity id. Target is chosen locally by melee-attack-rater (sub_1411F7370) "
        "from nearby enemies, not networked. Co-op: the Second player melees via "
        "its own controller over its local enemy set, so correct melee needs "
        "enemy position sync, not a melee event channel.",
    ),
    0x1411F7370: (
        "MeleeAttackRater_RateTargets",
        "melee-attack-rater.cpp core (0x1861). Rates candidate melee targets by "
        "geometry/angle/distance; shared by player and NPC melee behaviors "
        "(many callers). Produces the locally-selected melee target stored on the "
        "combat controller (player+0x1F60 -> +0xD8).",
    ),
    0x141279F10: (
        "MeleeCommandQueue_QueryMeleeSquare",
        "PlayerMeleeController vtable +0x210. Resolves melee-square (or the MP "
        "hold/release variants) and forwards the hash to queue validator +0x1F8. "
        "PlayerMoveStick uses a true result to enter the free melee animation path.",
    ),
    0x141275910: (
        "PlayerMeleeController_TryAttack",
        "Source literal PlayerMeleeController::TryAttack. Builds a target-rating "
        "request, calls MeleeAttackRater_RateTargets, and commits the selected "
        "attack through controller virtuals +0x208/+0x1D8 only on success.",
    ),
    0x1410F5740: (
        "Player_TryConsumeQueuedMeleeActions",
        "Shared Player action dispatcher. Reads Player+0x1F60, queries square, "
        "triangle, R1, and circle through vtable +0x210/+0x218/+0x220/+0x228, "
        "then routes accepted R1 commands to PlayerMeleeController_TryAttack.",
    ),
    0x1410FC9F0: (
        "Player_TryStartFreeMeleeAnimation",
        "Sole code caller is PlayerMoveStick_MainUpdate at 0x140F07A0A after "
        "MeleeCommandQueue_QueryMeleeSquare succeeds. Applies explicit feature, "
        "network-mode, action-state, and Player+0x1DE0 controller gates before "
        "dispatching Player animation decision/publish virtuals.",
    ),
    0x1402E6E10: (
        "ProcessExplosion_Init",
        "ProcessExplosion::Init (game/process-explosion.cpp): initializes the "
        "explosion process spawned by a detonating projectile/grenade (siblings "
        "Active::Enter/Update, ApplyDamageToObject, CalculateDamageToApply, "
        "spawn-explosion-effects-locator). Spawned via the process system (no "
        "direct callers). In co-op, the Second player's natively thrown grenade "
        "detonates per copy -> native explosion visuals; no relay layer needed. "
        "Debug: 'Control Grenade Explosions Manually', 'Spawn Test Explosions'.",
    ),
    0x140E1B000: (
        "PlayerGrenadeController_SolveTrajectory",
        "Grenade arc/trajectory solver: resolves settings via 0x140E17AC0, "
        "broadcasts gravity/speed scalars from settings +0x18/+0x1C/+0x20 into "
        "SIMD and integrates the arc; cached solver state at this+0x3D0 "
        "(a1[122]). Six callers = arc preview draw + toss commit paths "
        "(0x140E19600, 0x140E1F440, 0x140E1F5B0, 0x140E1FB50, 0x140E23920, "
        "0x140E24A00).",
    ),
    0x141C077B0: (
        "ResolveScreenRoutePoseSeedOrDefault",
        "CameraManager::GetHandle(idx) semantics (assert string, table "
        "0x14697AD50). blk = *(mgr+0xB0); requires gate bytes blk+85 && blk+86 "
        "and a dword_14697AE34 bit inside the bitset at *(blk+8)+32; then copies "
        "the 32-byte POSE SEED at blk+0x60 = [position float4][orientation quat "
        "float4] to out. Gate failure -> default pos(0,0,0) + identity quat "
        "(xmmword_1428805D0). This seed is the FIRE RAY origin+direction consumed "
        "by PlayerFireController_BuildFireArgsAndFire — the co-op Second player's "
        "screen-1 seed is never driven by the mod (wrong-direction shots). Co-op "
        "fix: hook here and override out for idx==1 from the peer's relayed "
        "screen-0 seed.",
    ),
    0x140E8F990: (
        "GunMoveController_ApplyCameraYawToMoveDirection",
        "GunMoveController vtable +0x38. Resolves the owner's screen-route camera, "
        "rotates the supplied direction by camera yaw, normalizes it, and writes "
        "the world-space result to GunMoveController+0x150.",
    ),
    0x140E8F720: (
        "GunMoveController_SelectAimIdleTurnState",
        "Consumes GunMoveController+0x140/+0x150/+0x160 and the current action id. "
        "Returns idle/move states 0/1, aim-idle state 2, or turn-in-place states "
        "3/4 according to the body-versus-target direction.",
    ),
    0x140E8FD00: (
        "GunMoveController_UpdateTurnYawFromDirection",
        "Reads GunMoveController+0x150, computes and smooths target yaw against the "
        "owner Player runtime world basis, writes the yaw quaternion through the "
        "Player+0x100 transform helper, then commits via runtime vtable +0x208.",
    ),
    0x140E90590: (
        "GunMoveController_UpdateActionState",
        "GunMoveController vtable +0x50. Calls the turn-yaw writer for active turn "
        "states, selects the next state via 0x140E8F720, and builds the authored "
        "aim-idle/aim-idle-rotate action graph via 0x140E8D140.",
    ),
    0x1416A5790: (
        "Player_IsCurrentActionStateHash",
        "Reads Player+0xC0, resolves the active state record through two pointer "
        "levels, and compares the authored state hash at [record+0x10]+0x18. "
        "GunMoveShield and its action-set builder use it for exact state names.",
    ),
    0x1410B2480: (
        "PlayerWeaponController_IsShieldDrawn",
        "PlayerWeaponController vtable +0xE0. In network mode returns the cached "
        "controller+0x13C byte; in campaign requires selected slot 1 and a live "
        "weapon whose class hash is ProcessShield.",
    ),
    0x140EFB750: (
        "ScreenPlayer_IsShieldDrawn",
        "Resolves the Player from g_PlayerHandleForHudPublish[screen] and calls "
        "PlayerWeaponController vtable +0xE0. This is a shield-specific query, "
        "not a generic screen-active or weapon-ready predicate.",
    ),
}

# Data/global names (set via ida_name on data addresses).
DATA_NAMES = {
    0x142A27658: (
        "PlayerGunMoveShield_Vtable",
        "MSVC RTTI .?AVGunMoveShield@Player@@; UpdateAndDispatch is slot +0x148 "
        "at 0x140E84440.",
    ),
    0x143742720: (
        "g_CachedOnlineWeaponDefinitionMap",
        "Cached handle for *weapon-gameplay-defs-online*. First qword resolves "
        "to a map with count at +0 and authored weapon-id array at +8.",
    ),
    0x143E04A00: (
        "g_StandardWeaponArtIds",
        "Fixed 19-entry authored weapon-id prefix used by both directions of "
        "the NetGameManager weapon stat-index mapping.",
    ),
    0x1437EA62C: (
        "g_InputWindowFocused",
        "Global input-focus byte maintained by Input_UpdateWindowFocusFlag and "
        "WM_SETFOCUS/WM_KILLFOCUS. Both per-player dispatch and "
        "Input_ScaleCopyVirtual require it before native conversion.",
    ),
    0x146A514A0: (
        "g_GameTaskGraphCommandList",
        "Campaign task-graph command list, base address (count "
        "g_GameTaskGraphCommandCount 0x146A51568, kMax 5, stride 0x28 = 40-byte "
        "records: +0 type(1/2/3), +4 subnode id, +8 graph id, +12 param, +16, +24 "
        "qword, +32 byte, +36 int). Appended by GameTaskGraphMgr_AddCommand, "
        "drained by GameTaskGraphMgr_Update to advance objectives. Co-op Option 1 "
        "relays these command records.",
    ),
    0x146A51568: (
        "g_GameTaskGraphCommandCount",
        "Entry count for g_GameTaskGraphCommandList (0x146A514A8).",
    ),
    0x143E07470: (
        "g_NetModeFlags",
        "Net/game mode flags. HIBYTE gates MP-only paths (e.g. "
        "ScriptCmd_NetTaskComplete_MpOnly, net-task verbs) — false in campaign. "
        "Useful to distinguish campaign vs MP session in mod code.",
    ),
    0x1465FDAA0: (
        "g_PerScreenCameraTransformCache",
        "Per-screen camera transform cache, stride 0x400 (64 xmmword) per screen "
        "index. CameraManager_GetBlendedCameraTransform copies entry "
        "[0x400*idx] as the screen's current camera transform when a single "
        "camera child is active. This (not CameraManager+0x1B0) is what "
        "PlayerFireController_Fire reads for aim/shot direction, so the co-op "
        "Second player's screen-1 entry governs where it shoots.",
    ),
    0x143ECEAA4: (
        "g_DebugCheatFlags_AmmoGrenades",
        "Built-in debug cheat flags: byte +1 is Infinite Ammo and byte +3 is "
        "Infinite Grenades. The circular scripted-input branch sets both as "
        "unrelated side effects, not as input activation.",
    ),
    0x143FE1088: (
        "g_LocalPlayerInputManager_Ptr",
        "Pointer to the native local-player/input manager. Per-player input "
        "distribution iterates 6 slots here (Joypad objects at *+0xDB0 + i*0xBF0; "
        "the internal loop cursor is Joypad+0xD8); "
        "per-slot input apply = its vtable+0x20. Slot 0 = active player; slots "
        "1-5 idle in campaign = the dormant native multiplayer input path.",
    ),
    0x1466E4220: (
        "g_InputStateBlocks",
        "Array of 6 per-player input-state blocks (166 qwords / 0x530 bytes "
        "each). Stick normalizer 0x141A99FD0 writes the active pad's normalized "
        "stick to block+0x10/+0x14. Read by the per-player dispatch "
        "0x14179E6C0/0x14179E310.",
    ),
    0x146B2C620: (
        "g_InputFocusConsumerArray",
        "Array of UI/overlay input-focus consumers (count g_InputFocusConsumerCount "
        "0x146B1B55C). First whose vtable+0x30 returns true claims focus "
        "(g_InputFocusCurrent 0x146B2C618). Null in plain gameplay.",
    ),
    0x146B1B55C: ("g_InputFocusConsumerCount", "Count for g_InputFocusConsumerArray."),
    0x146B2C618: ("g_InputFocusCurrent", "Currently-focused UI input consumer (null in gameplay)."),
    0x1437E0250: (
        "g_EngineClock",
        "Pointer to the engine clock. The current 64-bit tick at +0x310, "
        "multiplied by g_EngineClockToSeconds, is the timestamp used by "
        "NetEventCharacterMove and NetAnimCmdGenerator.",
    ),
    0x142BE8AE8: (
        "g_EngineClockToSeconds",
        "Float scale converting g_EngineClock+0x310 ticks to the native "
        "animation/network timestamp.",
    ),
    0x143EEBE80: (
        "g_PlayerHandleForHudPublish",
        "Global {pointer, id} handle (id at +8, validated against object+0xBC) "
        "consumed once per frame by PlayerHud_GlobalHandleUpdateEntry to run "
        "PlayerHud_ScreenStatePublishUpdate. ~830 xrefs across the binary — "
        "behaves like THE primary local-player handle singleton.",
    ),
    0x143EF1E30: (
        "g_PerScreenPlayerHudState",
        "Per-screen player/HUD state array (0x100 stride), indexed by the "
        "screen index at player+0x4010. Target object of "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes: is-aiming, "
        "weapon-accuracy, reticle-show-time records are committed into "
        "[screen]+0xF0 block offsets ~+0x3B18/+0x3B30.",
    ),
}


def ensure_function(ea):
    function = ida_funcs.get_func(ea)
    if function is None:
        ida_funcs.add_func(ea)
        function = ida_funcs.get_func(ea)
    return function


def apply_labels():
    # A previous melee pass accidentally put the dispatcher function name on an
    # interior instruction, forcing IDA to suffix the real entry with `_0`.
    ida_name.set_name(0x141273B20, "", ida_name.SN_NOWARN)
    for ea, (name, comment) in FUNCTIONS.items():
        ensure_function(ea)
        ida_name.set_name(
            ea,
            name,
            ida_name.SN_FORCE | ida_name.SN_NOCHECK,
        )
        ida_funcs.set_func_cmt(ea, comment, False)
        ida_bytes.set_cmt(ea, comment, False)
    for ea, (name, comment) in DATA_NAMES.items():
        ida_name.set_name(
            ea,
            name,
            ida_name.SN_FORCE | ida_name.SN_NOCHECK,
        )
        ida_bytes.set_cmt(ea, comment, False)


def main():
    import os
    import traceback

    log_path = os.environ.get(
        "TLL_APPLY_LOG",
        r"C:\users\vladimir\desktop\TLLCoopResearch\reports\apply_status.txt",
    )
    log = open(log_path, "w", encoding="utf-8")
    rc = 0
    try:
        n_fn = len(FUNCTIONS)
        n_data = len(DATA_NAMES)
        apply_labels()
        log.write(f"apply_labels OK: {n_fn} functions, {n_data} data names\n")
        # Save the open database (IDA 9.x). flags=0 keeps the DB; this repacks
        # the .i64. (Old DBFL_KILL via _ida_loader was unreliable here.)
        path = idc.get_idb_path()
        log.write(f"idb_path={path}\n")
        ok = ida_loader.save_database(path, 0)
        log.write(f"save_database returned {ok}\n")
    except Exception as e:  # noqa: BLE001
        rc = 1
        log.write("ERROR: " + repr(e) + "\n" + traceback.format_exc())
    finally:
        log.flush()
        log.close()
    ida_pro.qexit(rc)


main()
