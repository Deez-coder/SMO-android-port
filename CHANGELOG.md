# Changelog

Every build handed out gets its own number (`android:versionName` in `android/AndroidManifest.xml`, shown on the
launcher and written into exported logs). The APK is named after it: `odyssey-<version>.apk`.

## Beta 0.3

The third public release. This entry lists everything added and fixed since 0.2. The test build entries below (0.2.1 to 0.2.15) have the measurements and the details of each change.

**Faster.** Results depend on the phone; these are the two phones measured.
- **Galaxy S24 Ultra (Snapdragon 8 Gen 3, Adreno 750):** Cascade, standing at the start looking forward, 100% scale, the same settings file, started cool: **0.2 ran 43.8-44.9 fps, 0.3 runs 58.1-59.5** (against the 60 fps limit, so the real gain is larger).
- **Xiaomi 2406APNFAG (Dimensity 8300, Mali-G615 MC6), a tester:** about **40 fps, fairly stable**, at handheld resolution, the same with Steady game speed on or off (0.2.14: 26-30 with it on, 37-41 off). A 30 fps limit holds, and loading is noticeably faster.

**Performance, every GPU**
- **Fewer render passes:** small textures switch to "read" as soon as their own pass ends, so the main scene is no longer cut into 4 passes, and a clear stays inside the pass that uses it. S24 Ultra: 50 passes a frame instead of 62, GPU time 23.6 ms instead of 28.1 (0.2.3). A clear followed by a switch to targets that include the cleared image is now done by the next pass (0.2.5).
- **Constant buffers as uniform buffers,** so the driver keeps material and scene values in its constant registers instead of loading them per pixel. S24 Ultra: 49 fps instead of 41 at the same GPU clock (0.2.5).
- **Shader constants written into the shaders** (fragment and compute shaders), so the GPU reads nothing from memory for them (0.2.7).
- **Less CPU work per draw:** state is only set when it differs from the draw before, descriptors are written with one update template per pipeline (helps most on GPUs without push descriptors, like Mali), and pipeline lookups are faster (0.2.7).
- **The GPU starts big passes sooner:** the frame goes to the GPU in pieces as soon as the GPU has run out of work, between passes after 15 draws and inside a pass after 100, on every Android GPU. Before, only Adreno got small pieces, and on Mali the GPU sat idle and lowered its clock (0.2.7, 0.2.15).
- **The final picture pass has one shader per upscaler,** so Bilinear no longer carries the others' code (0.2.5).

**Mali and other GPUs without BC textures**
- **About 1 GB less memory:** BC textures are re-packed to ETC2 / EAC blocks of the same size instead of being unpacked to 4-8 times their size. Every texture read moves 2-8 times fewer bytes (0.2.9).
- **No more memory growth over time on Arm's driver:** command buffers give their memory back at each reset. Before, the game grew by about 490 MB an hour until it ran out of memory (0.2.5).
- **Black, grainy ground (issue 11):** R16G16_SNORM textures, which Mali-G615 cannot filter, are read with a nearest filter (0.2.1). A test of one suspect, not a confirmed fix.
- Adreno uses the GPU's own min / max / saturate instructions in translated shaders; Mali keeps the exact form, which avoids white and black blocks in the bloom (0.2.5).

**Graphics and display**
- **Upscalers** for any scale under 100%: Bilinear, Bicubic (Catmull-Rom), AMD FSR 1 and Snapdragon GSR 1, each with its own sharpness setting (0.2.3).
- **Performance overlay:** the fps counter can also show average fps, 1% low and frame time, each switchable, with a size setting (0.2.4).

**Game speed and frame rate**
- **Steady game speed:** the game keeps its normal speed below 60 fps by running 1-3 logic steps a frame, down to 20 fps. No fps lost on the S24 Ultra (0.2.6).
- **Simpler frame rate options:** the 60 / 30 choice is gone. What is left is an FPS limit (20-60) and Steady game speed, with a yellow warning when the limit is under 60 and Steady game speed is off (the game would play in slow motion). An old "30 fps" setting becomes a limit of 30 with Steady game speed on (0.2.8).

**Menus**
- **In-game menu:** the touch Exit button (or the controller's Exit key) opens a menu beside the paused game. Upscaler, anti-aliasing, the overlay and the touch controls change at once; resolution, console mode and the other start-up settings apply after "Restart now". Restart and Exit each ask for a second tap (0.2.4).
- Switches are blue when on and dark grey when off, in the launcher and the in-game menu (0.2.8).

**First start**
- **"Preparing graphics" progress bar** with a real percentage instead of the Joy-Con screen, and a "Skip for now" button after 30 s. Work done so far is kept if you close or skip (0.2.1, 0.2.2).
- The bar runs again after an update that changes how shaders are translated, instead of the game freezing at each new shader while playing (0.2.11).

**Older phones**
- **A second APK for ARMv8.0 processors** (Snapdragon 680, 665, 662 and older), which closed the normal APK at once. Install it over the normal one; files, settings and saves stay. The normal APK's launcher tells you if you need it (0.2.9).

**Fixes**
- Objects that were not drawn: shaders reading constant buffer 0 were refused by the driver (0.2.5).
- Parts of the map vanishing or turning into mirrors of the sky at some camera angles on Adreno (0.2.14).

**Updates**
- Every release, GitHub B0.1.0 and B0.2 included, installs over any earlier one without uninstalling. The build now guards the release key so this stays true (0.2.15).

**Logs (new in 0.3)**
- More detail in exported logs. Each 10 s of play now also gets a line saying where a frame's time went: the GPU's own time per frame (measured with GPU timestamps, now on by default on Android; `SMO_VK_GPUTIME=0` turns them off), the render thread's time and how much of it went to preparing shaders, the game's logic, and a best guess at what limits the frame rate (the GPU, the game's logic, the render thread, shaders being prepared, or the fps limit). It also gives handheld or docked, the 3D size the game draws at (and whether the game lowered it), Steady game speed on or off, and how many frames took over 50 ms. Every frame of 100 ms or more gets a `hitch:` line (at most one a second) with the same breakdown. The perf line adds the thermal headroom, the battery temperature and the GPU clock and load where the phone lets an app read them (Adreno, MediaTek's GED, devfreq; the first line says which one worked).

**Known issues:**
- Tested on two phones only (above). No older Snapdragon (Adreno 6xx), other Mali GPUs or PowerVR yet, and the ARMv8.0 APK has not run on an ARMv8.0 phone.
- Black, grainy ground in Cap Kingdom on a Poco X6 Pro (issue 11, Mali-G615): not reproduced, and the tester with the same chip does not see it, but not proven fixed.
- Effects vanishing after dying is fixed on Snapdragon; not checked on Mali.

Please report problems on GitHub (Issues), with the zip from the launcher's Export logs.

## Beta 0.2.15 (test build, not released)

**Mali and other non-Adreno GPUs: the GPU no longer waits for the CPU in big passes.** The render thread hands the frame to the GPU in pieces, so the GPU can start while the rest is still being recorded. How small those pieces were was tuned on Adreno only (every 15 draws between passes, and big passes split every 200 draws). Every other GPU kept 150 draws and no splits, so in a big pass (Cascade's main pass has about 800 draws) the GPU sat idle until the whole pass was recorded. The GPU's governor then saw an idle GPU and lowered its clock. Now every Android GPU hands over the next piece as soon as the GPU has run out of work: between passes after 15 draws, inside a pass after 100. A GPU that is still busy is left alone, so the extra store and reload a split costs is only paid when the GPU had nothing else to do.
- Measured on the Galaxy S24 Ultra running Mali's CPU path (`SMO_VK_PUSH=0 SMO_VK_VIDS=0`), Cascade, the same view and settings file at 100% scale: **45.0 fps with the GPU at 231 MHz before, 58.4-58.9 fps at 310 MHz now.** With 15 draws between passes but no splits: 47.8 fps.
- The other differences of Mali's path (descriptor sets instead of pushed descriptors, the vertex layout in the pipeline) cost nothing measurable in the same runs. The waiting GPU was the whole difference.
- Adreno's own path with the new rule: 59.0 fps at 310 MHz (59.7 with the old fixed counts; the same within run-to-run noise).
- Not run on a Mali phone yet. A split costs more on Mali (it stores and reloads the pass's images), which is why splits only happen when the GPU is idle. `SMO_VK_SUBMIT_DRAWS=<n>` / `SMO_VK_SPLIT_PASS=<n>` bring back fixed counts (0 = off).
- Every Mali log in the issues and the tester's 0.2.14 log show the same pattern: 4-5 submissions a frame and 4-10 ms of draw preparation on the render thread.

**Steady game speed: a test of yielding while spinning, off by default.** With boost on, the frame's threads spin before they sleep. 0.2.15 tried letting them give up the core now and then while spinning (`SMO_SPIN_YIELD=1`). In an A/B test inside one session (`SMO_SPIN_AB=<frames>` switches it every N frames and logs both halves), a logic step got shorter (9.23 to 8.75 ms) but frames got longer (33.87 to 35.09 ms at a 30 fps cap), so it stays off.

**Updates install over every earlier release.** Checked: the GitHub releases B0.1.0 and B0.2 and every test build from 0.1.0 to 0.2.15 have the same package name, the same signing key and rising version codes (1 to 29), so each installs over any earlier one without uninstalling. The build now keeps a copy of the release key beside the source tree, brings it back if `build/` is cleaned, and stops instead of signing with any other key (`SMO_NEW_RELEASE_KEY=1` starts a new one on purpose).

**Test switch:** `SMO_VK_VIDS=0` makes a GPU with dynamic vertex input take the path of one without (the vertex layout becomes part of the pipeline). With `SMO_VK_PUSH=0` an Adreno phone runs Mali's CPU path; use `SMO_VK_CACHE=<folder>` so the extra pipelines go into a cache of their own.

## Beta 0.2.14 (test build, not released)

**Parts of the map vanishing at some camera angles: fixed (Adreno).** Since 0.2.7, on the Galaxy S24 Ultra, ground and props in Cascade went invisible or turned into mirrors of the sky at some camera angles, with a yellow-white glow building up nearby. The cause was 0.2.7's per-draw state cache: it skipped setting the vertex layout and vertex buffers again when they matched the draw before, even right after a switch to another pipeline. Vulkan allows that, but Qualcomm's driver loses them at the switch, so the next object drew with no valid vertices. Now both are set again whenever the pipeline changes; draws in a row with the same pipeline still skip them, so the speed gain stays.
- Found by halving in the tester's own play, at the spots where it happened: all six 0.2.3-0.2.7 drawing switches off = fixed; pass splitting off alone = not fixed; state cache and descriptor templates off = fixed; state cache off alone = fixed; its descriptor and push-constant parts off = not fixed; its vertex layout and vertex buffer parts off = fixed; 0.2.14 with everything on = fixed.
- Ruled out on the way: fast min / max (0.2.10), an old shader cache (a new empty one did not help), pass splitting.
- `SMO_VK_STATECACHE=<mask>` (tests) now turns the cache's parts off one by one: 2 topology, 4 vertex layout, 8 vertex buffers, 16 set 0, 32 set 1, 64 push constants (decimal; 0 = all off).
- 0.2.12 also made the cache follow Vulkan's rule that binding one descriptor set with another layout can make the other set forgotten. That was not the cause, but it is kept.

## Beta 0.2.13 (test build, not released)

Test build: the state cache's parts can be switched off one by one (`SMO_VK_STATECACHE=<mask>`), to find which one made parts of the map vanish.

## Beta 0.2.12 (test build, not released)

Test build: the state cache sets a descriptor set again when a bind with another layout may have made the driver forget it. It did not fix the vanishing map parts.

## Beta 0.2.11 (test build, not released)

**"Preparing graphics" comes back after an update that changes the shaders.** The bar ran only once per phone: a file in the shader cache (`first_prep_done.txt`) said it was done, and it stayed done for good. But 0.2.5 and 0.2.7 changed how shaders are translated, so after those updates every shader was translated again while playing, and the game froze briefly at each new one. A Galaxy S24 Ultra session of 0.2.9 had 441 such waits (12.6 s in all) and 0.2.10 had 1,453 (43 s), on top of about 700-900 pipelines made during play. The marker file now names the translation settings it was made for (translator version, fast min / max, uniform buffers, constants in shaders, and the rest), so a change brings the bar back once. Checked on the S24: with a fresh cache the bar shows ("Preparing graphics... 24%" after 40 s). Whether this removes most of the stutter during play is not measured yet.

**Fast min / max on again for Adreno.** 0.2.10 turned it off as a test against the mirror-like surfaces some camera angles show in Cascade; the surfaces stayed, so it is back as in 0.2.9 (`SMO_VK_FAST_MINMAX=0` still turns it off). That problem is fixed in 0.2.14.

## Beta 0.2.10 (test build, not released)

Test build for the Galaxy S24 only: fast min / max (0.2.5) off on Adreno, to see whether it caused surfaces to turn into mirrors of the sky and a yellow glow at some camera angles in Cascade. It did not.

## Beta 0.2.9 (test build, not released)

**Against release 0.2.** Galaxy S24 Ultra, Cascade, standing at the start looking forward (about 1,200 draws a frame), 100% scale, the same settings file, started cool, GPU at 310 MHz, both measured on 2026-11-10 (0.2 installed again for it, with its own shader cache): **0.2 runs at 44-45 fps, 0.2.9 at 58-59.5 fps** (the 60 fps cap, so the real margin is larger). With the GPU at its lower 231 MHz step, 0.2 ran at 35-36 fps. The gain comes from 0.2.3 to 0.2.7 (fewer render passes, constant buffers as uniform buffers, shader constants inlined, less CPU per draw, split passes on Adreno). On Mali, 0.2.9 adds the texture change below.

**About 1 GB less memory on Mali and other GPUs without BC textures (issues 16, 10, 11).** Most of the game's textures are BC-compressed, which Mali GPUs cannot read. Until now they were unpacked to RGBA8 on the CPU: 8 times the memory for BC1, 4 times for BC3 / BC7, twice for BC4 / BC5. The Mali logs in the issues show it: 566 MB of textures on the Joy-Con screen where an Adreno phone has 129 MB, and about 2 GB of graphics memory on an 8 GB Moto G86 before the game was closed.
- Now each BC block is re-encoded as the ETC2 / EAC block of the same size, which every Android GPU reads (BC1 to ETC2 RGB or RGB A1, BC2 / BC3 / BC7 to ETC2 RGBA8, BC4 / BC5 to EAC R11 / RG11). Textures take the same memory as on a phone with BC, and every texture read moves 2-8 times fewer bytes, which matters most on the GPUs with the least memory bandwidth.
- Galaxy S24 Ultra imitating a GPU without BC (`SMO_VK_NO_BC=1`), Cascade, same view: graphics memory 1,312 MB against 2,374 MB with the old RGBA8 unpacking (1,376 MB with the GPU reading BC itself). The picture looks the same in side-by-side crops; fps the same on the S24, whose GPU has the bandwidth to spare. Loading hitches were alike in all three runs.
- The encoder is written for this port (no outside code) and uses up to 4 threads on large textures. Fine colour detail can lose a little against BC (ETC2 has one colour per half block); normal maps and masks (EAC) lose nothing visible.
- `SMO_VK_BC_ETC=0` brings back the RGBA8 unpacking. Not run on a Mali phone yet: the memory drop is measured, the fps gain on Mali is expected, not measured.

**A build for older phones (issue 9).** Phones whose processor is ARMv8.0 (Cortex-A53 / A73 cores: Snapdragon 680, 665, 662 and older) closed the game at once with SIGILL, because the APK uses ARMv8.2 instructions. There is now a second APK, `odyssey-<version>-armv8.0.apk`, built for ARMv8.0 (its atomics check the processor and use the faster instructions where they exist).
- Install it over the normal one; game files, settings and saves stay. Everyone else keeps using the normal APK.
- The normal APK's launcher now checks the processor: on an ARMv8.0 phone, Play shows a message naming the APK to download instead of starting a game that would close.
- Not run on an ARMv8.0 phone (none here). Checked by disassembly: the ARMv8.0 library holds none of the newer instructions outside the processor-checked helpers, and on the S24 it runs as the normal APK does (58-59.5 fps in the same Cascade view at 100%).

**First start (issues 12-15, 17-19, 21: stuck on the Joy-Con screen).** The logs of 17 (Pixel 7 Pro) and 19 (Mali-G615) show the game waiting while about 26,000 shader programs are translated, 47 s of a 58 s run on the Pixel, and the player leaving before it ended. Release 0.2 shows only the Joy-Con screen meanwhile; 0.2.2 and later show a "Preparing graphics" bar with a percentage and a Skip button (unchanged here). A profile of a cold first start on the S24 showed the shader workers busy (about 4 of 6 running at a time), so the wait is the work itself, not a lock.

## Beta 0.2.8 (test build, not released)

**Frame rate options simplified.** The "Frame rate: 60 / 30" choice is gone from the launcher and the in-game menu: Steady game speed does the same job for any frame rate. What is left:
- **FPS limit** (20-60, 60 = no limit), now also in the in-game menu, where it applies at once.
- **Steady game speed**, with a shorter description. If you set the FPS limit below 60, turn it on, or the game plays in slow motion. Both menus say so, and a yellow warning shows under the FPS limit while it is below 60 with Steady game speed off. The launcher's Game page shows the limit too, in yellow when the game would run in slow motion.
- If you had "30 fps, full speed" chosen, the launcher turns it into an FPS limit of 30 with Steady game speed on (the same result). An old `frame_rate = 30` line in graphics.ini counts as `fps_cap = 30`.

**Switches show whether they are on.** The track behind the switch is now blue when on and dark grey when off (it looked the same either way), in the launcher and in the in-game menu.

## Beta 0.2.7 (test build, not released)

Performance work aimed at every phone, not only the S24. Each part has a switch to turn it off if it causes trouble on a device.

**The GPU starts on big passes sooner (Adreno).** The commands of a frame already went to the GPU in small chunks, but only between passes, so the main scene pass (843 draws in Cascade's forward view) was recorded whole before the GPU could begin it: a gap of about 7 ms every frame with the GPU idle, which also kept Adreno's clock at its lowest step. Now a pass is split for the queue every 200 draws.
- Galaxy S24 Ultra, Cascade forward view (1203 draws), on and off by turns in one session: 58.1 fps against 55.8; when the GPU clock fell to 231 MHz the frames without it dropped to 50-53 fps while the split held 56-57. In a session where the clock stayed at 310 MHz: 58.2 against 57.9.
- On for Adreno, off elsewhere (not measured on Mali, where splitting a pass costs more memory traffic). `SMO_VK_SPLIT_PASS=<draws>` sets it, `0` turns it off.

**Shader constants written into the shaders.** The constants each shader carries with it (the game's compiler puts them after the code; 9,240 of SMO's 13,207 shaders read them) are now written into the shader text as numbers, so the GPU reads nothing from memory for them and the driver can fold them. LAS Recomp does this on Mali and Adreno. Fragment and compute shaders only, for now (vertex shaders are left alone, so depth stays identical between passes).
- S24, main scene pass at 231 MHz: 7.34 ms against 7.51 (about 2%). Adreno already keeps such values in its constant registers; GPUs that do less of that should gain more. Not measured on Mali.
- `SMO_VK_C1_INLINE=0` turns it off. **The first start after updating translates the fragment shaders again** (as after 0.2.5's update).

**Less CPU work per draw:**
- Topology, primitive restart, vertex input, vertex buffers, push constants and descriptor sets are set only when they differ from the draw before (in Cascade 97% of topology sets and about 10% of descriptor binds were repeats).
- Descriptors are written with update templates, one per pipeline (Ryujinx does the same; Arm recommends it). This helps most on GPUs without push descriptors, such as Mali-G615, where every new set used to be written field by field.
- The pipeline of the draw before is reused when the key is the same, and the in-memory tables hash 8 bytes a step instead of one.
- S24 (on and off by turns in one session, 842 draws): draw preparation 6.01 ms a frame against 6.16, batch replay 10.00 against 10.26. Small on Qualcomm's driver. Not measured on Mali.
- `SMO_VK_STATECACHE=0` and `SMO_VK_DTEMPLATE=0` turn the first two off. `SMO_VK_AB=<frames>` (test only) switches them off and on by turns.

## Beta 0.2.6 (test build, not released)

**Steady game speed.** A new switch (launcher: Graphics, Performance; in-game menu: Graphics; `steady_speed` in graphics.ini, off by default) keeps the game at its normal speed whatever the frame rate. The game advances one logic step per frame, so below 60 fps it used to play in slow motion (at 48 fps, 80% of its speed). With the switch on, each frame runs as many 1/60 s steps as the time since the last frame holds: at 40 fps it alternates 1 and 2.
- Galaxy S24 Ultra: capped at 40 fps, 1.505 steps a frame = 60.2 a second (full speed). Uncapped in Cascade's forward view: 48.6 fps with it on against 48 off (no fps lost), 1.23 steps a frame = 59.8 a second.
- Works down to 20 fps (at most 3 steps a frame). A frame longer than 0.25 s (loading, a hitch) is not caught up, so the game never races ahead.
- Changes at once from the in-game menu. Movement is a little less even than at a steady 60, because some frames move the world two steps. `frame_rate = 30` is used instead when set. `SMO_STEADY=0/1` overrides it for tests.
- Above 60 fps it is one step a frame, as before (going above 60 would need in-between frames, not done yet).

## Beta 0.2.5 (test build, not released)

**Constant buffers as uniform buffers, for every GPU.** The game's constant buffers (material and scene values every shader reads) were bound as storage buffers, so every pixel loaded each value from memory. They are now uniform buffers, as LAS Recomp and Power Moon Recomp bind them: the driver keeps values read at fixed offsets in its constant registers (Adreno's constant file, Mali's uniform registers).
- Galaxy S24 Ultra, Cascade, forward view (1205 draws), GPU at 231 MHz in both runs: **49 fps instead of 41**; GPU time in passes 15.5 ms instead of 18.6, the main scene pass 7.5 ms instead of 9.5. Same picture.
- On by default wherever the GPU allows 17 uniform buffers per stage (it falls back to the old way on its own otherwise). `SMO_VK_UBO=0` turns it off. Not run on a Mali phone yet.
- The first start after updating rebuilds the shader cache once (about 5 minutes on the S24, the game runs meanwhile).

**Missing draws fixed.** Shaders that read constant buffer 0 were refused by the driver (6 pipelines in Cascade alone, their objects not drawn) because the layouts started at buffer 1. Buffer 0 is now bound (to zeros, as compute shaders always had it).

**Smaller changes:**
- A clear followed by a switch to targets that include the cleared image is now done by the next pass (the depth clear before the main scene was a pass of its own): 48 passes a frame instead of 50. `SMO_VK_CLEARCARRY=0` turns it off.
- The final picture pass has one shader per upscaler, so Bilinear no longer carries GSR's and FSR's code (2.48 ms instead of 2.56). `SMO_PP_SPEC=0` turns it off.
- Adreno: min / max / saturate in the translated shaders use the GPU's own instructions, which already treat NaN the Switch's way (from LASRecompAndroid, MIT). Mali keeps the exact form (it keeps NaN, which made white and black blocks in the bloom). `SMO_VK_FAST_MINMAX=0/1`.
- Mali (Arm's driver): command buffers release their memory at each reset. Arm's driver otherwise kept it, and a process grew by about 490 MB an hour until it ran out of memory (found by LAS Recomp, #607). `SMO_VK_CB_RELEASE=0/1`.

## Beta 0.2.4 (test build, not released)

**In-game menu.** The touch Exit button and the controller's Exit key now open a menu on the right side of the screen, in the app's look, with the game paused and still visible on the left. It replaces the "Leave the game?" question.
- Changes that apply at once: Upscaler and its sharpness, Anti-aliasing, the performance overlay, and the touch controls (show, opacity, size, camera speed). After a picture change the game runs for about 0.15 s, so the paused picture shows the change.
- Resolution, Console mode, Anisotropic filtering and Frame rate are there too, next to the other graphics settings. They apply on the next start, and changing one shows "One or more changed options need a restart" with a "Restart now" button.
- "Restart" and "Exit" each take a second tap ("Again?"). Restart leaves the game the same way Exit does, then the app starts it again.
- Its sliders, switches, choices and cards are the launcher's own (same theme, sizes and labels), and the Graphics card has the launcher's order: Console mode, Resolution (Native / 1.5x / 2x), Custom scale.
- The top line shows the game's picture size and how much it is enlarged on screen, for example "Game picture 1200×675 → 1920×1080 on screen (1.6× upscale)".

**Performance overlay.** The fps counter can now also show average fps, 1% low and frame time (of the last 10 seconds or so), each switchable, with a size setting. It is set in the launcher's Display section and in the in-game menu. It is drawn by the game's final shader like the old counter, with a small pixel font for the labels.

## Beta 0.2.3 (test build, not released)

**Fewer render passes per frame, for every GPU.** Tile-based phone GPUs (Mali, Adreno and the rest) pay for every pass with a store and reload of its targets. Two causes of extra passes are fixed:
- The main scene (4 colour targets plus depth) was cut into 4 passes, because small textures drawn earlier in the frame were only switched to "read" when the main scene first sampled them. They are now switched as soon as their own pass is over. Turn it off with `SMO_VK_EARLYREAD=0`.
- A clear before a pass was done as an empty pass of its own whenever the target also had to change layout, and the real pass then loaded what had just been cleared. The clear now stays in the real pass. Turn it off with `SMO_VK_LCKEEP=0`.
- On a Galaxy S24 Ultra (Cascade, standing still and looking forward, GPU at 310 MHz): 50 passes instead of 62, the frame's GPU time 23.6 ms instead of 28.1, about 50 fps instead of 44. The picture is the same. Not run on a Mali phone yet; the gain should be larger there.

**Upscalers.** A new "Upscaler" choice in the launcher's Quality section (`upscaling` in graphics.ini). It is used wherever the picture is enlarged to the screen: any resolution scale under 100%, and a little even at 100% on a 1080p-tall screen. All four work on any GPU.
- Bilinear: as before, the default.
- Bicubic (Catmull-Rom): a little sharper, the same cost.
- AMD FSR 1 (EASU + RCAS, MIT), adapted from Power Moon Recomp's port: sharp, smooth edges. The costliest. On the S24 Ultra its extra pass took about 2.5 ms at 422 MHz, roughly 4-5 ms at the usual 231 MHz.
- Snapdragon GSR 1 (BSD-3-Clause): sharp edges in one pass. Slightly cheaper than bilinear with FXAA and sharpening (2.19 ms against 2.49 at 231 MHz, at 50% scale).
- Each has its own sharpening: "FSR sharpness" (90% = AMD's default) and "GSR sharpness" (50% = Qualcomm's default). The old Sharpening slider is for Bilinear and Bicubic. Bicubic, FSR and GSR replace FXAA where they enlarge.
- `SMO_UPSCALE=bilinear|bicubic|fsr|gsr` picks one for a test. The licences are in `docs/licenses/` and now also inside the APK (`assets/licenses/`).

## Beta 0.2.2 (test build, not released)

**A progress bar for the first start, instead of the Joy-Con screen.** The first start now shows "Preparing graphics" with a real percentage while the game's shaders are built.
- A "Skip for now" button appears after 30 s. Skipping starts the game sooner, with more stutter until the rest is ready.
- If you close the game or skip, the work so far is kept, and the next start gets through it faster. The bar starts from 0 again and climbs faster until it catches up.
- Once it has finished, it does not show again. The first start after updating shows it once, briefly if the shaders are already built.
- It does not make the wait shorter. On a Galaxy S24 Ultra with an empty cache it took about 151 s, a little more than the 138 s the Joy-Con screen took.
- The 90 s limit from 0.2.1 and its notice are replaced by this during the first start. Not tested on the slow phones that reported being stuck.

Fps on the S24 Ultra, Cascade, native driver, 100% scale, in the older tests' jumping view, started at 34°C: 56-58 once the GPU clock reaches 310 MHz, the same range as 0.2. The clock can take up to about 3 minutes to get there, and the fps is about 45-48 until it does. Standing still, with the camera looking forward, is about 34 fps at 231 MHz.

## Beta 0.2.1 (test build, not released)

**Stuck on the Joy-Con screen (issues 12, 13, 14, 15).** The game now waits for shader translation for at most 90 s per start, then goes on while
the rest is made in the background. A notice says what is happening while it waits. On a Galaxy S24 Ultra with an empty shader cache, the screen took
113 s instead of 138 s, and 59 s with the limit set to 30 s to imitate a slower phone. Fps is about 50 instead of 54 for the first minutes after a cold start.
Not tested on the affected phones, and no log from a stuck run exists yet, so this may not be what holds them.

**Black, grainy ground on Mali-G615 (issue 11).** R16G16_SNORM, which that GPU cannot filter, is now read with a nearest filter instead of an
undefined linear one. This is a test of one suspect, not a confirmed fix. On the S24 Ultra, with Mali's limits imitated, the picture stays fine, but
that phone never showed the bug. Not run on a Mali phone yet.

## Beta 0.2

The second public release. Everything since 0.1.0 is in the entries below; these are the main points.

**60 fps mode (Snapdragon).** On a Galaxy S24 Ultra, native driver, Cascade Kingdom's flag view: from 44 fps to about 57, 1% low from 31
to about 42, at a softer picture (800 wide there). It is not a locked 60. Under 60 the game plays a little slower than normal. The 30 fps mode holds
30.0 in the same view.

**Mali.** One tester, on a Mali-G615 phone (Xiaomi, MediaTek), played the build this one is made from (0.1.8) and reports no graphical
issues and good performance. No other Mali phone has been reported on. Reports are welcome.

**Changes from 0.1.8:**
- **New defaults on a fresh install:** Presentation Mailbox, CPU boost Always, Custom scale 100%, FXAA, Sharpening 75%. In one test spot in Cascade on the S24 Ultra
  these ran at 58 fps and the old defaults at 45. The phone runs warmer with CPU boost on Always. If you already have settings, they stay as they are. These defaults have not
  run on a Mali phone.
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
