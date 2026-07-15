# Player aim: audio-path correction and real final-palette boundary (2026-07-15)

## Result

The previous `CharacterSkeletonRender_*` map at `0x141B5CE90`, `0x141B61530`,
and `0x141B757F0` is **REJECTED**. This is the AudioManager ARM render path. The
old cb/cd workspace injections therefore never exercised character skinning.

The real final matrix pair in FgAnimData is `+0x230/+0x238`. Earlier reports and
the bt/by probes incorrectly treated Hex-Rays decimal offsets `+560/+568` as hex
`+0x560/+0x568`. Decimal 560 and 568 are hex `0x230` and `0x238`.

## Competing hypotheses

1. **Selected STATIC:** relayed aim reaches the animation graph, but the final
   `FgAnimData+0x230` matrices do not contain the required camera-relative body
   yaw, or that yaw is overwritten while the palette is built.
2. The final `+0x230` matrices are correct and a later real renderer consumes a
   different buffer. This remains open until the true consumer is resolved.
3. `Player+0x140` root yaw was the correct owner, but previous writes happened
   after palette construction. The timing part remains possible; the old visual
   rejection did not test a pre-blend write.

## Independent evidence

1. **Source strings / subsystem identity.** `0x141B5D240` contains exact
   `AudioManager::RunRenderJob` assertions and `gamelib/audio/arm.cpp` paths.
   `0x141B61530` contains exact `void AudioManager::Render(void)` source strings.
   `0x141B65C50` prints `NCA System`, `Samplers`, `Submixing`, `Effects`,
   `Premastering`, and `Mastering` statistics.
2. **Call graph.** `AudioManager_RunRenderJob 0x141B5D240` schedules callback
   `0x141B5CE90`; that callback calls `AudioManager_Render 0x141B61530` and the
   NCA stats helper. `0x141B757F0` is called at `0x141B63CE6` from that audio
   render function while iterating stride-`0x6C0` ARM entries.
3. **Exact assembly/data flow.** `AnimManager_AsyncObjectUpdateJointBlendJob
   0x141A92A00` writes `[rsi+0x238] = [rsi+0x230]`, then publishes the new
   48-byte-per-joint buffer to `[rsi+0x230]` at `0x141A93410..0x141A93430`.
   Only afterward it resolves the retained owner and calls Player vtable
   `+0x410` and `+0x418` at `0x141A934D5/0x141A934E8`.
4. **Independent runtime consistency.** The old read-only palette probe found a
   valid moving 651-joint heap at `FgAnimData+0x230`, while values interpreted
   as a pointer at hex `+0x560` were float-like garbage. That observation matches
   the corrected assembly and disproves a separate skinning pointer at `+0x560`.

## Falsifiable next prediction

No runtime test is earned yet. First resolve the true consumer and multiplication
convention of the 3x4 matrices at `FgAnimData+0x230`.

Once that is static, a read-only discriminator should show one of two outcomes
while the sender rotates the camera in ADS:

- selected hypothesis: relayed aim weights move but a chosen upper-body/root row
  in the current `+0x230` palette lacks the predicted yaw delta;
- consumer alternative: the row contains that yaw delta, but the resolved render
  consumer reads another generation or another FgAnimData owner.

Do not reinstall hooks on `0x141B5CE90/0x141B63CE6`; they are audio hot paths.
