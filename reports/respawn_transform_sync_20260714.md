# Remote Player respawn transform synchronization (2026-07-14)

## Status

`REJECTED` one-shot-only repair; revised continuous reconciliation is ready for
the second runtime prediction below.

## Exact hypothesis

The sender publishes a valid post-respawn position, but the receiver leaves the
remote Player at its pre-respawn position because the authoritative teleport bit
is not consumed. In `g_clone_freerun`, a stationary peer whose displacement is
less than four metres receives neither a hard snap nor a correction nudge.

## Competing hypotheses

1. The sender's retained slot remains stale and continues publishing the old
   pre-respawn position.
2. The new position arrives, but the existing teleport marker is lost and the
   stationary free-run dead zone preserves the old remote transform.
3. The remote Player itself is recreated or removed, so transform application is
   running against a stale receiver object.
4. World/local transform writes occur, but a later native lifecycle stage restores
   the old position.

## Independent static evidence

1. Wire contract: `StatePacket.flags bit 4` is explicitly the authoritative
   teleport marker. `send_local_player_state` sets it when a sampled local position
   jumps by more than 2.5 metres.
2. Producer/consumer data flow: `publish_peer_state_snapshot` stores that bit in
   `g_state_net.pending_authoritative_teleport`, but before this fix no code read or
   cleared the atomic. The intended one-shot signal was dead state.
3. Receiver branch: `apply_puppet_transform_post` in `g_clone_freerun` snaps only
   above four metres. Below that it nudges only while
   `valid_network_movement(pkt) && move_magnitude > 0.01`; a stationary respawned
   peer therefore leaves the puppet exactly where it stood.
4. Sender gap: velocity/discontinuity detection previously ran only when the
   sample interval was below 0.5 seconds. A checkpoint/location load takes longer,
   after which the code replaced its previous position without setting teleport.
   A changed retained Player object was not tracked either.

This selects candidate 2, with the sender-gap portion of candidate 1 as a second
contributing defect. Continuing packet-driven remote actions after the user's
respawn observation argues against candidate 3. Candidate 4 remains falsifiable
by the post-update snap prediction.

## Fix

- Sender: mark teleport when the retained local Player identity changes, on any
  position jump above 2.5 metres, or when a gap of at least 0.5 seconds resumes at
  a position displaced by more than 0.25 metres. Zero derived velocity on that
  discontinuity.
- Receiver: atomically consume `pending_authoritative_teleport` in
  `apply_puppet_transform_post` and include it in the existing hard-snap branch,
  independent of the four-metre threshold and movement input.

## Falsifiable runtime prediction

After running away from the checkpoint and reloading once, the sender emits one
`LOCAL-RESPAWN-TELEPORT` and the peer emits one matching
`PUPPET-RESPAWN-SNAP`. The remote avatar appears at the checkpoint immediately
while standing still. It must not drift back to the old position, emit repeated
snap lines during ordinary running, or disturb the native run/idle animation.

If the sender line contains the new checkpoint position but the visual puppet
returns to the old one after the post-update snap, candidate 4 wins and the next
step is a later transform owner. If no sender line appears, local lifecycle
identity/position capture must move below the respawn finalization point.

## First runtime result: one-shot snap is insufficient

Both processes emitted sender and receiver events, so the wire marker and
post-update consumer are confirmed. The receiver snapped to the checkpoint
coordinates, but the user still saw the remote actor at its old position.
`PUPPET-POS` then showed the exact failure: with `peerMove=0`, horizontal error
grew repeatedly from roughly one metre to four metres, triggered the old distance
snap, and began growing again. Thus candidate 4 wins in a continuous form: native
locomotion resumes integrating its old target after each one-shot write.

The sender's Player-pointer discriminator is also **REJECTED**. The retained
resolver changes object pointers repeatedly during reload while positions remain
identical, producing many false `objectChanged=1` teleports and resetting the
position history. Position discontinuity remains the stable signal.

## Revised fix and prediction

The sender no longer treats pointer replacement itself as a teleport. It retains
position history across those replacements and marks only a measured spatial
discontinuity or a displaced sample after a long load gap. On the receiver, the
existing bounded free-run nudge now runs for stationary peers as well as moving
ones; this closes later native drift without frame-by-frame hard pinning.

After reload, one authoritative snap may occur, then `PUPPET-POS horizontal`
must converge below the 3 cm deadband and stay there while `peerMove=0`. There
must be no repeated same-position `LOCAL-RESPAWN-TELEPORT` lines and no recurring
four-metre hard snaps. If error still oscillates, the retained table is selecting
multiple live Player instances and reconciliation must be keyed to the exact
`update_object` rather than a fresh retained lookup.
