# Changelog

Every build handed out gets its own number (`android:versionName` in `android/AndroidManifest.xml`, shown on the
launcher and written into exported logs). The APK is named after it: `odyssey-<version>.apk`.

## Beta 0.2

The second public release. Everything since 0.1.0 is in the entries below; these are the main points.

**60 fps mode (Snapdragon).** On a Galaxy S24 Ultra, native driver, Cascade Kingdom's flag view: from 44 fps to about 57, 1% low from 31
to about 42, at a softer picture (800 wide there). It is not a locked 60. Under 60 the game plays a little slower than normal. The 30 fps mode holds
30.0 in the same view.

**Mali.** One tester, on a Mali-G615 phone (Xiaomi, MediaTek), played the build this one is made from (0.1.8) and reports no graphical
issues and good performance. No other Mali phone has been reported on. Reports are welcome.

**Changes from 0.1.8:**
- **Mali diagnostics are off by default,** and an existing "Auto" setting counts as off. Before, every Mali player got a short hitch about once a minute
  and the odd slightly wrong test frame, only to collect logs. Testers can turn them on in Graphics > Diagnostics. Everything else is the same
  as the build the tester played. This change has not run on a Mali phone.

**Also since 0.1.0:** effects no longer vanish after dying (tested on Snapdragon), the white-out fix, translated shaders handle "not a number"
values as the Switch does, compute shaders work on GPUs with other thread group sizes, and much more detail in exported logs.

## Beta 0.1.8 (60 fps work, Mali visuals)

Includes the 0.1.7 work, which was only ever a test build on one phone.

**60 fps mode.** Measured on a Galaxy S24 Ultra, Cascade Kingdom's flag view, native driver, 170 s runs:

| | fps | 1% low | GPU clock |
| --- | --- | --- | --- |
| 0.1.7 | 44 | 31 | 231 MHz |
| 0.1.8, mailbox | 57 | 42 | 310 MHz |
| 0.1.8, vsync on | 57-58 once settled | 38 | 310 MHz |

60 is not locked yet, and the game runs slightly under full speed when it is under 60. Busier places and real play have not been
measured on this build. Other phones have not run it at all.

- **GPU work is sent in smaller pieces (Adreno):** a frame is sent to the GPU about 14 times instead of a few, so the GPU waits less and
  the phone keeps it at a higher clock. This was the biggest gain.
- **Steadier render size:** the game's own resolution control now sees real GPU timings and was changing size about 5 times a second.
  It now moves in small steps. In Cascade it settles at 800 wide, so the picture is softer than before.
- **Free frame pacing in the 60 fps mode:** a frame that misses a refresh no longer waits for the next one. About the same average, with smaller
  dips. The 30 fps mode is unchanged and holds 30.0 in the same view.
- **Log:** each `perf:` line also gives the GPU clock where the phone allows reading it (Adreno).

**Mali.** A tester's report on 0.1.6: with "Mali format fix" Off the visual errors went away, with fps a little lower.

- **The option is gone:** the format swap it controlled is now simply off. An old setting in `graphics.ini` is ignored.
- **The test that changed between starts is gone:** Mali phones now always use the stricter position math and the math guard, the combination the
  tester's clean captures came from. The swap and the stricter math changed around the same time in that test, so which one cured it is
  not known yet.
- **First start on Mali:** every vertex shader is translated once more, so expect a slow first load, once.
- Tester's answer on this build: no graphical issues, good performance.

## Beta 0.1.6 (Mali test build)

A first attempt at the white glows, and more detail in the logs about the gaps in Mario and Cappy. Neither has run on a
Mali phone yet, so whether the glows are gone is not known. Please test.

- **Glow attempt (Arm GPUs only):** a few math operations have no defined answer on phone GPUs when given zero or a
  negative number (logarithm, square root, inverse), and the game's shaders use them for bright highlights. They now give
  a fixed answer on Mali. Values that are not real numbers (NaN, infinity) can also no longer be written into the picture,
  where the bloom would spread them. On a Galaxy S24 Ultra with this forced on, Cascade and Sand look the same and measure
  the same as without it. That shows it does no harm there, not that it cures anything.
- **Automatic captures twice as often (Arm GPUs only):** every 1800 frames instead of 3600, and the last 6 are kept instead
  of 3. Expect the short hitch about once a minute at 30 fps. Graphics > Diagnostics > Off still stops it.
- **Depth pictures:** each capture now also saves the depth of each pass, which shows whether a hole in a model is there
  before any colour is drawn. The title screen gets the largest ones.
- **Logs:** triangle and pixel counts for each draw in up to 8 captures, and on the title screen a checksum of each model's
  data and shaders, so a Mali log can be compared with the S24's line by line.
- **Test that changes between starts (Arm GPUs only):** every second start draws with stricter position math (`precise`),
  which may or may not close the gaps. The log says which one a start used. Please look at the title screen on two starts
  in a row and say whether the gaps differ.
- First start after updating, on Mali: every shader is translated again once. The first start with the stricter math
  translates the vertex shaders once more. Expect both to take longer. Other GPUs are not affected.
- Everything in 0.1.5 is still here.

## Beta 0.1.5 (Mali diagnostic build)

No fix in this one: it measures the gaps in Mario and Cappy on the title screen, so the next build does not have to guess.

- **Near-miss count (Arm GPUs only):** the game draws models twice, once for depth and once for colour. A few times per
  session it counts how many pixels the colour draw loses by only a few rounding steps. Tested on a Galaxy S24 Ultra, which
  shows no gaps: 0.1% or less on the title screen, 0.35% within 16 steps in Cascade, and 100% when the depth match is broken
  on purpose. A count near those healthy numbers means rounding is not the cause. It has not run on a Mali phone yet.
- **The test frames can look slightly wrong:** one or two single frames each round, up to 24 rounds in a session.
- **Title screen capture:** screens with few draws, like the title, now also get the pass-by-pass capture on Mali. They
  never did before, so the gaps had not been captured.
- **Depth texture line:** the log now says whether the GPU can smoothly filter depth textures. That is a lead for the
  odd shadows in Sand.
- Everything in 0.1.4 is still here.

## Beta 0.1.4 (hotfix of 0.1.3)

- **Effects no longer vanish after dying:** after a respawn the shading, bloom and lighting disappeared until the game
  was paused and unpaused. The game makes new textures for its ambient light on respawn and expects the old contents to
  still be in that memory, as they are on a Switch. In the port they started out black. They now carry over. Tested on
  a Galaxy S24 Ultra (Adreno 750) by dying in play: the effects stay on.
- **Mali phones:** some players were reported to lose the effects for good, cutscenes included. This may be the same
  fault, but no Mali phone has tested it yet. The same goes for the random white spots.
- Everything in 0.1.3 is still here, including the Mali diagnostics. This build replaces it for testers.
- Up to 64 MB more graphics memory can be in use, holding those contents until the game needs them again.

## Beta 0.1.3 (Mali diagnostic build)

0.1.2 did not fix the glow, haze and blown-out patches on Mali. This build fixes a real bloom bug found on the way, and
measures what is left on the phones that show it. Whether it cures the Mali spots is not known yet: please test.

- **White-out fixed (all GPUs):** the picture could turn white at some point during play and stay that way. The game
  keeps one slot per distinct texture sampler and finds it again by a key made from the sampler's memory. The port left
  part of that memory unset, so the game hardly ever found its slots again: it made thousands of new ones (Cap Kingdom
  on the emulator: 78,000 in under 5 minutes, now under 200). In some runs all 3839 slots filled up. New samplers then
  got an ID that means nothing, and the bloom read the texture it was drawing to. Seen on the emulator in about 1 run in
  3 without the fix, and in none of the 5 runs with it (Cap, Cascade, Sand). The same fault may be behind the Mali spots
  or the missing post effects, because any effect can get the wrong sampler when the slots run out. That is not tested.
- **Less work per frame:** those needless slots each cost a sort of the whole list on the main thread. Not measured on a
  phone yet.
- **Diagnostics (Mali GPUs only):** about once a minute of play one frame is measured: for every render target, the
  range of its values and how many pixels hold "not a number" or infinity, plus a small picture. There is a short
  hitch when this happens. The last 3 captures go into the exported logs (`diag/` in the zip, `nvn_vk_diag:` in the
  log). Graphics > Diagnostics > Off stops it.
- **Mali format fix:** render targets in the three formats Mali cannot filter or blend are now made in formats it can,
  so the game's blending and filtering are defined there. This is correct by the Vulkan rules, but no test has shown it
  to help: nothing here could reproduce the random spots with giant white blooms, which have only been seen on Mali
  phones. **Graphics > Mali format fix** is On by default; Off gives the 0.1.2 behaviour, to compare. No effect on GPUs
  without these limits.
- **Crash fixed (Snapdragon):** 0.1.2 crashes within a minute of starting on a Galaxy S24 Ultra (Adreno 750), while
  preparing shaders: its compute shader change crashed Qualcomm's shader compiler. If you have 0.1.2 on a Snapdragon
  phone, update. The shaders compile again; the shaders concerned do not run in Cap or Cascade Kingdom, so what they
  draw has not been seen.
- **Logs:** the driver's name and version, clip distance support and how the GPU treats unusual float values.
- First start after updating: every shader is translated again once (expect a longer first load).
- Tested on the emulator (including simulated Mali format limits) and on a Galaxy S24 Ultra (Adreno 750). NOT tested on
  a Mali phone.

## Beta 0.1.2 (Mali test build)

0.1.1 made Mali faster, but the glow, haze and overexposure stayed (Cascade and Sand Kingdom in the testers' photos).
This build removes the places where the port relied on behaviour that Mali does not share with the Switch.

- **Blending the GPU cannot do** (Mali: 32-bit float textures, confirmed by the tester's log). The game blends into
  them in two effects: a smoothing step into a 256x128 texture (Cap Kingdom) and an adding-up step into a 1x1 texture
  (Cascade Kingdom; most likely the scene brightness that sets the exposure). Vulkan leaves such a blend undefined, so
  the Mali driver may have produced garbage there, which would over-expose the whole picture. Both are now drawn
  without blending (defined). On the emulator, Cap and Cascade look the same with this as without it.
- **Compute shaders on GPUs whose thread groups are not 32 wide** (Mali: 16, Adreno: 64 or 128). 18 of the game's
  compute shaders (its GPU triangle culling) number their work by the thread's place in a Switch warp of 32; the
  port took the phone GPU's number instead. Now the Switch's on every GPU. Values passed between threads (6 of those
  shaders) are exact with groups of 32 or more, on Mali except between the two halves of each warp. These shaders do
  not run in Cap or Cascade Kingdom on the emulator, so this is not expected to change those.
- **Compute group size:** the game's 1536-thread groups are split to fit the phone's own limit (it was a fixed 1024).
- **Smooth filtering the GPU cannot do** (Mali: one 16-bit format): left as the game asks and named in the log.
  `SMO_VK_UNSUP_NEAREST=1` filters it as nearest instead (a test switch; on the emulator it made things worse).
- **Logs:** the GPU's thread-group size and compute limits (`nvn_vk: subgroup of ...`), each compute shader the game
  runs (`nvn_vk_cs:`), effects that run into a format limit (`nvn_vk_fmt:`, with the frame number).
- First start after updating: every shader is translated again once (expect a much longer first load; the game can
  sit on the controller screen until it is done).
- NOT tested on a Mali phone. Please play Cap, Cascade and Sand Kingdom and send the logs either way.

## Beta 0.1.1 (hotfix for 0.1.0)

Mali / Dimensity fixes and less CPU work per draw. NOT tested on a Mali phone yet: logs from Mali testers are welcome.

- **Mali graphics:** haze, white glow blobs and flashing on Mali GPUs (Dimensity). The translated shaders now handle
  "not a number" values the way the Switch GPU does in min/max and saturate. Other GPUs let these values through
  harmlessly, but Mali kept them and the bloom spread them into blobs.
- **Geometry stages:** the depth fix for GPUs without `VK_EXT_depth_clip_control` now goes on the last stage before
  rasterization (it only matters for the lightning effects).
- **Faster draws without push descriptors** (Mali-G615 and similar): buffer descriptor sets are reused across draws
  (dynamic offsets) instead of being made for every draw. Pipelines with the same bindings share one layout.
- **Logs:** format-support lines (`nvn_vk_fmt:`) for GPUs that cannot filter or blend a format the game uses.
- First start after updating: shaders are translated again once, and on GPUs without push descriptors the pipelines
  are made again once (expect some stutter in that first session).

## Beta 0.1.0

First public beta.
