# SMO Android port: progress log

Build steps: [smo-android/BUILDING.md](smo-android/BUILDING.md).

## Approach

The base is static recompilation with [smo-recomp](https://github.com/Coz-0/smo-recomp) (GPL-3.0), at commit
`1ca3c00`, the last commit that supports game version 1.0.0:

- The game's AArch64 code is translated ahead of time to C and compiled with the Android NDK. Nothing is emulated or
  interpreted at run time, and no Windows compatibility layer is involved.
- Console services, NVN→Vulkan graphics, Maxwell→GLSL→SPIR-V shaders, audio and input come from smo-recomp's native
  runtime, ported here to Android.
- [OdysseyDecomp](https://github.com/MonsterDruide1/OdysseyDecomp) is about 15.8% matched and serves as reference only.
- [SuperMarioWonderRecomp](https://github.com/hesanaltdoesgithub/SuperMarioWonderRecomp) was the reference for the
  Android host: NativeActivity, fixed guest mapping, Gradle-free APK.

The dump is base 1.0.0. Its `main` NSO hash matches the known retail hash, so the keys and the dump are good.

## Status (2026-10-28)

**Tested on the x86_64 emulator** (Android 35, gfxstream Vulkan 1.3 on an RTX 4060):

- The APK installs. The app finds the pushed game files and starts the recompiled game.
- The game boots through `nninitStartup`, 183 static constructors and `nnMain`.
- Fonts load; files load from the RomFS.
- AAudio starts (48 kHz stereo, 120 ms buffer).
- The Vulkan device comes up; shaders are translated and compiled through shaderc on the device.
- Seen on screen: the first-start menu ("Start / Start in Assist Mode") and the boot screen "Try playing with detached
  Joy-Con controllers". Orientation and 16:9 shape are correct, at 59–60 fps once the shader cache is built.
- The crash and missing-file paths print useful reports.

**Not tested yet:**

- arm64 on a real phone. The arm64 APK builds and its library loads under the emulator's ARM translation, but the
  game has not been run on arm64.
- Input of any kind (touch, controller, keyboard). The adb key press did not move the boot screen on, which is also
  what a loading screen would do.
- Gameplay past the boot screen; audio output (the emulator ran with `-no-audio`); performance on a phone.

## Done

- Toolchain (JDK, SDK, NDK 28, emulator), all checksum-verified.
- Dump decrypted, RomFS extracted.
- Full lift: 3.13M instructions, 0 untranslated; 1,336 SDK functions.
- `SMO_LAYOUT=android` memory layout: guest at 0x40_0000_0000 instead of 0x71_0000_0000, which Android already uses.
- Platform layer `runtime/plat.h` plus `plat_posix.c`; the whole runtime ported off Win32:
  - threads and locks, memory (memfd heap), files, time zones, sockets, crash reports.
- Android host (`runtime/android/`):
  - NativeActivity, Vulkan surface (handles losing and regaining the window), AAudio.
  - Gamepad, keyboard, touch, accelerometer and gyro input.
  - `controls.ini`; `env.txt` for debug settings.
- Renderer changes for GPUs without desktop-only extensions:
  - The vertex layout is part of the pipeline key when `VK_EXT_vertex_input_dynamic_state` is missing.
  - z is remapped in vertex shaders when `VK_EXT_depth_clip_control` is missing.
  - RGBA swapchain; identity pre-transform; picture fitted to its aspect ratio with bars.
- Build scripts in `tools/android/`:
  - `build_game`, `build_shaderc`, `build_runtime`: build the library.
  - `build_apk`: aapt2, zipalign, apksigner.
  - `push_data`: copies your game files to the device.

## On a real phone (Galaxy S24 Ultra, Snapdragon 8 Gen 3 / Adreno 750, Android 16), 2026-10-28

- **Runs.** Boot → Joy-Con notice → first-start menu → "Start" pressed through the touch controls → the opening
  cutscene (Bowser's airship) in full 3D.
- **Menus run at 60 fps.**
- **Shaders:**
  - First start, empty cache: the menu after about 2 min.
  - Second start: the menu after 30–45 s; 1.2 GB of shader cache.
  - The 30 s holds are gone.
- **The phone uses the faster renderer paths.** Adreno has dynamic vertex input, depth-clip control and BC textures.
- **Cutscene: 20 fps.** Frame = 50 ms, all on the render thread, one step after another:
  - replaying the game's ~580 draws: 26–33 ms;
  - waiting for the GPU: 12 ms;
  - present: 9–12 ms.
  - The GPU itself needs only about 12 ms per frame.
  - The render thread is about 50% busy and the game's main thread 14%: they wait on each other, not on the hardware.
  - Not the cause (tested): occlusion results waited for at present, pinning the game threads to three cores.
- **Cutscene speed-up, same day: 20 → 25–26 fps.**
  - The swapchain was being rebuilt every frame by a window-size check that didn't allow for rotation; present went
    from 12 ms to 0.8 ms.
  - The render thread now gets the Cortex-X4 core by default.
  - Repeated "set the same render targets" commands no longer end the render pass.
  - Remaining cost per frame (about 40 ms):
    - draw replay, about 28 ms: mostly Adreno driver calls for about 580 draws and 70 render passes;
    - GPU waits, about 12 ms: `OP_REPORT_VALUE` forced compute results back before writing the value.
  - **Fixed the same day: 25–26 → 33–35 fps.** Report values now wait in a queue until their compute results land,
    or until the render thread runs out of work.
    - Draw replay is about 14 ms per frame.
    - About 7 ms is the game itself waiting for its GPU work, at the render thread's idle point.
    - Audio silence dropped from about 1.4 s to 0.1–0.55 s per 2 s.
    - The picture is unchanged.
  - Diagnostics: `SMO_VK_WAITWHO=1`.
- **Gameplay reached, then crashes, 2026-10-28.**
  - Cap Kingdom loads and runs at 40–58 fps (50–275 draws per frame).
  - About 30 s in, the game opens its camera-tutorial video (`content:/MovieData/PlayGuideMovieRStick.mp4`). The
    game assumes videos always open: `MovieDecoderPlayer::OperateThreadFunction` calls the decoder without a check,
    so with no player it crashed.
  - **Fixed: video player on Android** (`runtime/android/android_movie.c`, AMediaExtractor + AMediaCodec, generated
    from `nnmovie_mf.c`'s protocol code). The tutorial clip (H.264 480×272) plays and loops in game.
  - **Open:** in that first gameplay scene, input reaches the game but Mario doesn't react. Confirmed in the game's
    own pad reads: A, B and both sticks. Menus react to the same path. It's unknown whether this moment allows
    movement at all; needs a check by hand on the phone.
  - Adreno 750 refuses two of Cap Kingdom's pipelines with VK_ERROR_UNKNOWN. Their draws are now skipped. The
    attachment formats are supported and the SPIR-V is valid; the cause is still open.
  - Earlier stops left no log line; `plat_exit` now drains the log first.
- **Gameplay works by touch.** Confirmed by the user on the phone: Mario moves and jumps.
- **On-screen controls and an fps counter** are drawn by the present pass.
- **Turnip driver (Mesa), 2026-10-28.** K11MCH1's v26.0.0 R8 is loaded through libadrenotools (built from source).
  - Same Cap Kingdom scene, about 1,000 draws per frame:
    - Qualcomm driver: 24–26 fps, replay 23.4 ms;
    - Turnip: 33–35 fps, replay 15.6 ms.
  - The stage-library path (graphics pipeline libraries) is unusable on a phone: it ran out of memory and stalled on
    precompiling. Android defaults to whole pipelines.
  - After an hour of tests the phone reported thermal status 2, and replay doubled from CPU throttling. Measurements
    must be taken on a cool phone.
  - **For 60 fps (16.7 ms):**
    - draw replay must roughly halve (spread over threads, or fewer driver calls per draw);
    - the ~6 ms of GPU waiting must overlap with other work.
  - **Findings from the 60 fps work (end of 2026-10-28):**
    - The background shader workers were predicting pipelines for every shader variant: 44,845 on Turnip, 77% of all
      CPU, the phone throttling. Fixed: `SMO_VK_CPREDICT`, off on Android.
    - **The GPU needs about 19.5 ms per frame** in Cap Kingdom with Turnip, so it alone exceeds 16.7 ms. The figure is
      the same at resolution scale 0.75, so the cost is per pass or per barrier, not per pixel. That points at:
      - render-pass handling on the tiled GPU (each of about 70 passes may go through binning);
      - full-pipeline barriers (`image_barrier` uses ALL_COMMANDS → ALL_COMMANDS for every layout change).
    - Load-clears (`LOAD_OP_CLEAR` for passes that start with a clear) are in, but gave no measurable change.
    - **Next:**
      1. Test `TU_DEBUG=sysmem` and `TU_DEBUG=gmem` (in `config/env.txt`) to see whether binning is the cost. A run
         was started but interrupted.
      2. Narrow the barriers to the stages that are actually involved.
      3. Multithreaded draw replay for the CPU side (replay about 15–19 ms).
    - **The phone heats fast:** status 2–3 after one or two runs. Measure only at status ≤ 1 (the scripts in
      `tools/android/` take a few minutes per run).
  - **Why fps sticks at 30 (later the same day):** with vsync, a frame that misses 16.7 ms waits for the next refresh,
    so the rate drops straight to 30. The game also runs per frame, so below 60 it plays in slow motion.
    - Per-pass GPU timing (`SMO_VK_PASSTIME=<frame>`), Turnip, Cap Kingdom: 131 passes, 16.4 ms of GPU work, and 8 ms
      of gaps where the GPU waits for the render thread to record. **The render thread (CPU) is the limit.**
    - Render-thread profile: copying the game's buffers into the stream ring is about 20% (about 15 MB per frame,
      re-copied every batch); then the command-list scan, pipeline lookup and sampler setup.
    - **Tried and removed:** keeping ring copies across batches for pools the game must flush (CPU_CACHED,
      CPU_NO_ACCESS). It saved only 0.4 of 14.8 MB per frame, because the per-frame data lives in CPU_UNCACHED pools.
      It also blacked out the 3D scene (stale copies). Not worth pursuing.
    - **Next candidates:** multithreaded replay or cheaper per-draw work on the render thread; real GPU timestamps for
      the game's dynamic resolution (the 3D image is 800x450, which causes the aliasing).
  - **Hidden GPU waits removed (later the same day, commit 100a264): Cap Kingdom 23-25 -> 28-32 fps.**
    - Turnip's `vkGetFenceStatus` is a kernel wait. Polls that must not wait now read a VkEvent each command buffer
      sets at its end.
    - The idle step no longer waits for compute results.
    - No wait for occlusion counts at the present. The counts are written to game memory one whole presented frame
      at a time (`SMO_VK_OCC_HOLD`); without that, light glows flickered (confirmed fixed by the user).
    - Conditional renders whose counts are not in yet use the previous frame's decision.
    - The look-ahead scan runs on its own thread.
    - Result: the render thread waits ~0.1 ms per frame for the GPU (was 7-12). Replay is 16-19 ms per frame; the GPU
      needs ~16.4 ms.
  - **Tried without an fps change (commit 0022e14):** ARMv8.2 target, `Ctx *restrict`, a lock-free indirect-call
    cache, NVN lookup locks, `ANativeWindow_setFrameRate(60)`. All kept; all correct.
  - **Tried and dropped:** keeping ring copies across batches (only 0.4 of 15 MB per frame was eligible; it broke
    the picture); async present (`SMO_PRESENT_ASYNC=1`, no clear gain).
  - **The limit now: the game's own threads.** No thread is near full load (render 55%, main 45%, workers 7-21%).
    The main thread spends ~42% of its time in the game's own nn::os timed-event waits (for its worker threads).
    Frames take ~35 ms as a chain of threads waiting on each other.
    - **Next:** record which guest thread waits for which per frame (nn::os events, mutexes, wake-up latency) and
      fix what lengthens the chain: worker priorities, core placement, slow wake-ups.
    - Also: frames below 60 make the game run in slow motion, which is why 50-60 fps still feels laggy; frame pacing
      uses display.c's own 60 Hz clock, not the display's refresh.
  - **2026-10-29:**
    - The phone's own Qualcomm driver is now nearly as fast as Turnip in the same spot (25-30 vs 28-31 fps), so
      phones without Turnip (MediaTek, Exynos: Mali) are on a usable path. Turnip stays optional (only loaded when
      its driver is in `files/driver`).
    - Android performance hints (`android_perf.c`, ADPF, Android 13+) for the render thread and the game's threads:
      no measured change in Cap Kingdom in standard game mode (off 32-35 fps, on 31.5-33). Kept: harmless, and
      other phones' governors may respond to it.
    - Samsung's performance game mode gave no measured change in that spot either; the user saw 40-60 fps in it
      while looking away from the town (far fewer draws).
    - Controllers: tested with injected gamepad keys at the title menu (selection moves, touch controls hide). Device
      id -1 had been taken for the back gesture; fixed. Sticks and triggers need a real controller to test.
    - Touch: a right camera stick (`camera_touch = stick`, the default; `drag` for the old behaviour).
    - `phone_play.sh` presses A twice: a single press was sometimes taken before the title menu accepted input.
    - Touch controls on/off: a ring at the top centre (saved as `touch = off | auto` in controls.ini). Tested.
    - **Wait map (`SMO_WAITMAP=1`, os_threads.c):** per frame at ~32 fps the game's main thread (shown as "host")
      waits ~5 ms for the view-update worker and ~3-4 ms for the presentation thread; wake-ups are fast (50-350 us);
      the workers are idle most of the time. The main thread's own work (~20 ms a frame) is the critical path.
    - Main thread on the Cortex-X4 and the render thread on A720s (`SMO_OS_HOST_CPUS=98 SMO_RENDER_CPUS=64`): worse
      (replay 23.5 ms, 27.6-28.7 fps). The default stays.
    - The sampler list sort (`smo_fast.c`) now runs in host code on Android too: libsmo.so is linked with
      `--wrap=f_400072e61c`. `SMO_FAST_CHECK=1` in gameplay: every sort equal to the game's. Frame rate unchanged in
      Cap Kingdom (29.4-30.6 fps, within run-to-run spread).
    - Then: the sort as a stable insertion sort (the same order; `SMO_FAST_CHECK=1` in gameplay: all equal), the
      NVN object and label tables at 65,536 chains (they were 5% and 3% of the main thread). Still ~30 fps.
    - **Why main-thread savings do not show:** the median frame is ~32-33 ms, two refreshes. Free pacing
      (`SMO_PACE=free`, by time instead of refreshes; opt-in) changed nothing (29.4-29.8 fps), so the quantising wait
      is in the game's own frame loop: its presentation thread waits for the vsync event, and the main thread waits
      for that thread (wait map: ~3-4 ms a frame).
    - Correction: the ~32 ms frames are real work, not refresh rounding (free pacing gives a smooth 25-45 ms spread).
      The game's own translated code is 64% of the main thread: next big lever is the translator keeping registers
      in locals (several sessions).
- **2026-11-02: 30 fps full-speed mode, launcher, mods.**
  - `frame_rate = 30` (graphics.ini, launcher Graphics page): presents every second refresh, and
    sead::GameFrameworkNx::procCalc_ runs twice per frame (`smo_framerate.c`, `--wrap=f_4000731fdc`). Tested:
    steady 30.0 fps, two logic steps per frame; audio silence 20-140 ms per 2 s against 135-535 ms in normal mode
    at ~30 fps, so the game runs near full speed. Picture correct.
  - Launcher (`android/java/io/github/smoport/LauncherActivity.java`, javac + d8 in build_apk.py): Game page (status,
    Play, import game files from a folder), Graphics, Controls, Advanced (driver .zip install/remove, phone's own
    driver), Mods (install .zip, on/off). Tested on the phone: shows the installed game and Turnip driver; Play
    starts the game. Settings write graphics.ini / controls.ini (new keys: fps_counter, driver).
  - Mods: `mods/<name>/romfs/...` (or `mods/<name>/...`) replace the game's files, alphabetical order, `mods.ini`
    switches them off (nnfs.c view_path). Files only a mod adds are not in directory listings. Not tested with a
    real mod yet.
  - **Registers in locals (`tools/lifter/regcache.py`), tried and not kept:** 69,470 of 74,183 functions changed,
    compiled and played correctly, but no measurable fps change in matched views (old 32.3-34.1, new 33.3-33.7 fps at
    1,195 draws; 42 vs 42 at ~834). The reloads it removes hit the L1 cache. The game code was reverted; the pass is
    kept for later.
  - **Test spread:** the camera after loading differs from run to run (834 vs 1,195 draws in Cascade Kingdom,
    noticed by the user), so only runs with the same draws per frame are compared.
  - In the ~834-draw view: GPU 10.5 ms, render thread replay 10.8 ms, frame 23.6 ms (42 fps): the game's own
    threads set the pace.
  - `anti_aliasing = fxaa` (graphics.ini, launcher): FXAA on the final picture in the present pass. Tested, works.
  - The 3D at 800x450 is not caused by our report timestamps: with timestamps that made every frame look quick, the
    passes stayed 800x450 (that option was removed again).
  - **Why resolution_scale never did anything in SMO:** the scale applied only to textures of 1920x1080 >> k (the
    PC project's first game); SMO's are 1600x900 / 800x450 (docked) and 1280x720 (handheld). Fixed (rs_screen_class)
    and the limit raised to 2.0 (an emulator-style resolution multiplier). At 1.5 the game started and the scaled
    targets were made, no errors; not yet seen in gameplay (the phone locked while unattended).
  - The game's own cap (al::GraphicsQualityInfo::calcRenderResolution [0xa58394], wrapped: smo_quality.c,
    `dynamic_resolution = on | off`) gives the full 1600x900 asked for in Cascade Kingdom: the 800x450 3D is the
    game's own choice there, not a quality drop. Kept as an option, not in the launcher.
  - Tested at 1.5 in gameplay: part of the picture is drawn unscaled over the scaled rest (two overlapping
    copies of the scene; seen by the user too). SMO's sizes are now behind a development switch
    (`SMO_VK_SCALE_SMO=1`) and the launcher's slider is back at 25-100% (it does nothing in SMO yet) until the
    scaled path is right for SMO: next, find which pass or copy keeps the game's coordinates.
  - **Fixed (2026-11-02):** the doubled picture came from SMO's 1600x900 HDR colour target, a 2D array texture
    (NVN target 4) of one layer: the scale took only targets 1 and 5, so it stayed at the game's size while its
    partners in the same passes were scaled (a diagnostic logs such mixed passes). Arrays of one layer are scaled
    now; compute images (NVN_TEXTURE_FLAGS_IMAGE) keep the game's size. No mixed passes left; picture correct.
  - **`resolution_scale = auto` (the default):** docked 1920x1080 (1.2 x the game's 1600x900, the console's output),
    handheld the game's 1280x720. Measured at 1,195 draws: 31.1-31.7 fps at 1.2, 31.0 at 1.5, against 32.3-34.1 at
    1.0. Launcher: presets Console / 1.5x / 2x and a 25-200% custom slider.
  - **Supersampling:** when the picture is larger than its place on the screen, the present pass averages an n x n
    grid (n up to 4) over each screen pixel's footprint instead of one bilinear sample. 2x: clearly smoother
    edges; 26.5-26.7 fps in 60 fps mode at ~1,145 draws (vs ~31 at 1.2); in the 30 fps mode 2x and 1.5x hold 30
    (1% lows 25.6 / 24.4) at ~838 draws.
  - **Sharpening** (`sharpening = 0..100`, launcher slider): contrast-adaptive, in the present pass after
    supersampling / FXAA. Tested at 50% with 2x in the 30 fps mode: 29.9 fps, crisp detail, no halos.
  - yuzu's source could not be read for this (GitHub answers 451 for the yuzu mirror); the approach follows the
    general emulator design (resolution multiplier, downscale filter, FXAA/SMAA, sharpening).
  - Not yet: importing straight from a dump (NSP/XCI + the player's keys) on the phone. The APK holds code
    translated from the player's own game: for that player only, not for sharing.
  - **Build note:** the game and runtime must be built with `SMO_LAYOUT=android`. Without it, the PC lift is
    compiled and the game stops at its first indirect call.
- **Audio choppy in the cutscene.** About 1.5 s of every 2 s is silent. The output never ran dry, so the game itself
  produces sound in step with its frame rate: no gaps at 60 fps in the menu, about 75% gaps at 20 fps. This should
  follow the frame rate fix.
- **The red/blue swap in some UI colours** (teal panel shown olive) happens on the phone too: a real bug.
- **Samsung's navigation bar stays visible.** Needs immersive mode (a small Java activity).

## Frame rate investigation (2026-11-03, S24 Ultra, release build)

Goal: find what keeps gameplay below 60 fps before changing anything. Test settings: 60 fps mode, scale 1.0, no
AA, no sharpening. Each run starts cool (thermal status 0-1), enters gameplay, resets the camera (L) so every run has
the same view (831-841 draws per frame, screenshots match), then measures 60 s.

| Run | What it changes | fps (last 60 s) | render-thread replay per frame |
| --- | --- | --- | --- |
| base | nothing | 40.0 (steady) | 11.3 ms |
| free | `SMO_PACE=free`: paced by time, not by display refresh | 42.5 rising to 51.7 | 10.5 ms |
| spin | `SMO_SPIN=2,5`: two busy threads from gameplay on, so the governor clocks up | 44.5, then 60.0 | 10 ms, then 5.2 ms |
| spinfree | both | 60.0 (58.3 at the end) | 3.2-4.1 ms |

**Finding: low CPU clocks are the main cause.** With the same work and view, the render thread's replay takes 11.3
ms at the clocks the governor picks and 3.2-5.2 ms at high clocks. When clocks are high, the frame holds 60 fps.
- Every thread of the game waits on another for part of each frame (main thread about 56% running, render thread
  about 55%), so none looks busy to the WALT governor. It runs the cores at about 55% of their maximum (render thread
  on cpu7 at ~1.5 of 3.34 GHz, main thread ~1.75 of 3.15 GHz). The ADPF hint session exists but doesn't raise them
  enough.
- In the spin run the jump to 60 came about 35 s after the spinners started: replay halved at that moment, so the
  render thread had reached a fast clock (probably moved onto a busy cluster, or cpu7 ramped up). spinfree reached
  60 within 5 s.
- Emulators like Eden keep their CPU threads busy (JIT, spin-waits), which incidentally keeps the clocks high. Here
  the threads sleep while they wait, so the governor drops them.

**Second factor: refresh pacing.** Free pacing alone gave +2.5 fps at first and +12 fps once clocks drifted up. The
main thread's sync waits on display refresh (`display_wait_refresh`, ~9.5% of its time) and on a synchronous round
trip to the render thread (`gl_run` in sync_wait, ~6%), so the render thread's replay sits on the critical path.

**Not the cause:** the GPU (waits 0.1-0.2 ms per frame; passes ~14 ms at scale 1.0 run in parallel), shader
compiling (none during the runs), heat (all runs started cool and the fast runs got faster, not slower).

**Caveat:** this view has ~835 draws. Busier views reach ~1,200 draws (31-34 fps base earlier); full clocks have not
been tested there yet.

**Fix plan, in order:**
1. Keep the clocks up during gameplay without spinners: lower the ADPF target or report work time instead of frame
   interval, `APerformanceHint_setPreferPowerEfficiency(false)`, a short spin before sleeping on the render-thread
   and sync waits (so the threads look busy and hand off faster), and keep the render thread on the big cores.
2. Take the render-thread replay off the main thread's critical path: make sync_wait / present asynchronous where
   the game doesn't need the result, and signal fences early.
3. Pace by time in gameplay (free pacing) with a frame-time cap instead of waiting for display refresh.
4. Faster replay itself: draw preparation is 6.4 ms of the 11.3 at low clocks (descriptor set work per draw).
5. Later: lifted-code overhead (context stores, lane-by-lane vector loads, NaN fixups).

**Clock fix, first round (2026-11-03, in progress).** All numbers on the phone's own driver (`SMO_DRIVER=system`)
from now on: players without a custom driver are the target (Mali included). Same view as above (~835 draws).
- Samsung's Adreno driver is much slower on the CPU than Turnip: 32.5 fps vs 40 (replay 15.5 vs 11.3 ms, draw
  preparation 9.8 vs 6.4 ms).
- Spinning before blocking (plat.h `PLAT_SPIN_WHILE`, off by default): `SMO_SPINWAIT_US` on every thread did nothing
  at 0.5 ms, little at 2 ms, and at 20 ms crowded out the work (menus at 30 fps). `SMO_SPINHOT_US=16000` only on the
  threads that set the frame rate (render thread, presenting thread, the thread that waits for the window sync):
  32.5 → 41.9 fps on the system driver (Turnip: 40 → 53.7). With free pacing as well: 43 rising to 49.6.
- `SMO_PERF_BOOST=1` (no power-efficiency preference, a workload rise every frame) with an 8 ms target: 41.2 vs 40.1.
  No real effect.
- The clusters still average 1.5-1.7 GHz even with three threads 100% busy, although the caps are ~2.9 GHz and the
  threads have no uclamp limit. WALT's `hispeed_freq` on the prime core is 1.59 GHz. Every spinning run sped up
  30-40 s into play, so the 60 s windows caught a ramp: longer runs are next.
- Render thread on the system driver: `vkResetCommandBuffer` cost ~1.2 ms a frame (the driver frees and reallocates
  its memory). Each slot now has its own command pool, reset whole (`vkResetCommandPool`). Built and installed, NOT
  yet run: the phone's battery ran out at the start of the test.
- Testing note: stopping a background test from the agent left the `sh` script running, so twice two scripts drove
  the phone at once. Some Turnip numbers above (53.7) may be affected; the system-driver numbers were rerun cleanly.

`SMO_SPIN` (runtime/android/android_perf.c) stays as a measuring aid: it does nothing unless set in env.txt, and
starts only when `<files>/config/spin_now` exists.

## Launcher: touch layout, controller buttons, save data (2026-11-03)

- **Touch layout editor** (LayoutEditorActivity.java): landscape, in the same full-screen window as the game. Drag any
  control (buttons, d-pad arrows, SHAKE, the two sticks' resting places), Smaller / Bigger (50-250%), Put back,
  Reset all, Save. Only moved controls get a line: `pos_<control> = x y size` in controls.ini. The runtime reads them
  (nnpad_android.c, also while the game runs). A touch away from the buttons goes to the stick on its side of a line
  0.10 to the camera's side of the middle between the two sticks (0.45 of the width for the layout as made, as before).
- **Controller buttons**: Controls page, one row per console button. Tap, press the controller button. The key moves
  from whichever button had it to the old key of this one (no button is left without a key). Written as
  `bind_<button> = <keycode>`; a button bound elsewhere no longer answers its own key. Sticks and analog triggers keep
  their axes.
- **Save data**: Game page. Export zips `userdata/save` (the committed save, files under `save/`). Import takes a zip
  with the files at the top or in one folder (an export, or a save folder from elsewhere), keeps the current save as
  `userdata/save-backup-<time>`, removes a pending `save.commit` and any `save.journal.*` / `save.work.*` of the old
  save, then puts the new one in place (the old one is put back if that fails). Refused while the game runs (libsmo.so
  is in the app's process): the dialog offers to close the app.
- **Checked on the emulator** (Android 15 x86_64, own AVD `odyssey_test`): editor drag / resize / save wrote the
  expected lines; returning to the launcher keeps the Controls page; rebinding with injected gamepad keys (swap
  included); export then import of test files, an emulator-style zip, backup and clean-up as above. The runtime side
  by a test program built from nnpad_android.c, run on the emulator: 13 of 13 checks (moved and resized controls,
  touches at the new places, bound keys, unbound keys keep their defaults).
- **Not yet checked on a phone**: the editor's places against the game's own drawing, a real controller, a real save.
- **prod.keys on its own button** (Game page, step 1): the file is checked for `header_key`, then kept as
  `prod.keys` in the app's internal files folder (only this app can read it), shown as "imported", with a Remove
  button. Step 2, Import from NSP / XCI, reads the keys from there and drops the parsed copy when the import ends; it
  asks for step 1 first if there are no keys. `android:allowBackup="false"` keeps the keys (and the game files) out
  of Android's cloud backup. Checked on the emulator with a made-up keys file: a file without `header_key` is refused;
  the keys survive an app restart and a reinstall; the NSP button uses them (a non-game file fails in the importer);
  Remove deletes them. Not yet run with real keys and a real NSP on a phone.
- **Logs for reports**: the runtime keeps the run before as `log.prev.txt` (a crash's log survives starting again),
  and every 10 s of play writes a line like `perf: 55.3 fps, 1% low 19.7, worst frame 51.5 ms over 553 frames (60 fps
  mode, scale 0.75); CPU MHz cpu0 .., cpu4 ..; thermal none` (android_perf.c: frame gaps of 1 s or more are pauses
  and are left out; the 1% low is the average of the slowest 1% of frames; one clock per cpufreq policy; the thermal
  state from AThermal). With the GPU and driver lines already printed at start, a log from another phone says how
  the game ran there. Advanced page, **Export logs**: a .zip with info.txt (app and Android version, phone, chip, ABIs,
  custom driver), log.txt, log.prev.txt, the settings files and the app's own logcat (main, system, crash; Android
  gives an app only its own lines). No save, keys or game files. Checked: the perf line by a test program built from
  android_perf.c on the emulator (made-up frames: 1% low 19.7 for 1% of frames at 50 ms; a 1.5 s pause left out; the
  emulator has no CPU clocks, so they read 0), the export on the emulator (contents as listed, logcat only this app's).
  Not yet on a phone: the perf line in a real game, readable CPU clocks.

## Beta check on the phone (2026-11-03, S24 Ultra, release build 0.1.0)

Everything added since bf3d443 run on the phone, with the player's own settings (30 fps mode, resolution auto, FXAA,
sharpening 75):

- **Play**: boots, reaches gameplay, 30.0 fps. Turnip: 1% lows 22-27 in play. The phone's own driver: 30 fps, 1% lows
  about 25 (one window 16.9). The per-slot command pools (vkResetCommandPool) work on both drivers.
- **perf lines** read real cluster clocks (cpu0 / cpu2 / cpu5 / cpu7 policies) and the thermal state ("moderate":
  the phone was warm from charging). `scale` now prints "auto" for resolution_scale = auto.
- **Export logs**: info.txt (SM-S928B, QTI SM8650, Android 16, Turnip v26.0.0 R8), both logs with perf lines,
  settings, logcat of the app only. With Turnip, logcat holds two "Access denied finding property
  vendor.mesa.spirv.*" warnings per shader compile (the driver's own, harmless).
- **System bars (fixed)**: the theme hid only the status bar. With button navigation, the navigation bar sat over
  the right edge in landscape, over A, ZR and R, and the layout editor's window ended at it. New GameActivity.java
  (a NativeActivity subclass) and the editor hide the system bars (immersive, swipe to show) and lay out under the
  cutout, so both use the whole display. First version crashed the editor: Window.getInsetsController() is null
  before setContentView on this phone; getDecorView().getWindowInsetsController() works.
- **Layout editor**: A dragged to 0.750 0.358 was saved as such, drawn there in the game, and taps there took the game
  from the menu into play (taps at the old place did nothing).
- **Controller buttons**: ZR rebound with an injected gamepad R1: `bind_ZR = 103`, `bind_R = 105` (the swap).
- **Save**: export of the real save (2 MB) byte-identical to the save; import of that zip: identical files, the old
  save kept as `save-backup-<time>`, a stale `save.work.<pid>` removed; the game loads it into play.
- **prod.keys + NSP import** with the player's own keys and 5.6 GB NSP: about 15 s on the phone. Every file name and
  size equals the PC extraction (the PC also has dump/base/control.nacp and dump/version.json, not used); all program
  files and 199 sampled game files have the same MD5. The game plays on the imported files. The earlier pushed copy
  had 4 files fewer.

## Leaving the game, button letters (2026-11-03)

- **Exit**: a house button at the top of the touch controls (movable in the layout editor) and a controller key
  (Controls tab, "Exit the game (asks first)", Home / Guide by default, `bind_EXIT`) open a popup in the app's own style:
  Keep playing or Exit to the menu. The game is held while it is open (display_menu_hold). Exit waits for a save
  commit in progress (`fs_quiesce`, nnfs.c: the fs lock fs_Commit holds, then the commit lock the launcher's save
  import uses), then the activity finishes and android_main ends the process.
- The game now runs in a process of its own (`:game`), so leaving it ends only the game and the app's menu stays.
  The launcher finds the game by that process ("Close the game" in the save-import warning ends it).
- Button icons in the style of the console's own button glyphs, drawn by our own code (signed distances in the
  present-pass shader, no image): circled A B X Y, L R ZL ZR tabs, hollow + and -, arrow d-pad, a stick-press R3, L and R
  in the sticks' knobs, the house for Exit. A dark fill with a white outline and icon; white with a dark icon while
  pressed. On the S24 Ultra the title screen holds 30.0 fps with the controls drawn and with them off.
- Checked on the S24 Ultra: the house opens the popup over the title screen, the picture stands still while it is open
  and moves again after Keep playing, Exit to the menu brings the app back with only the `:game` process gone and the
  log shows the save wait; the controller Home key (`input gamepad keyevent BUTTON_MODE`) opens it too. Rebinding
  Exit on the emulator writes `bind_EXIT`. Touches during the black boot screen do nothing: the touch controls start
  when the game sets up its controllers.

## Start-up time: the pipeline cache was never written on Android (2026-11-03)

- Every phone log of the beta check showed the same thing: the ~4,400 pipelines listed in `pipes.bin` made again at
  every start in 85-128 s ("pipelines made ahead"), and the game's first frame held in `program_set` for that long, so
  the title screen came after 1.5-2 minutes. No log had "pipeline cache saved": the cache file (8 MB) was never
  written back. The exit path that writes it (vkd_shutdown) never runs on Android (the process ends with `_exit`), and
  the held loading screen that writes it belongs to the stage-library path (SMO_VK_GPL=2), which phones do not take.
- Now `nvn_vk_pcache_save` (nvn_vk_m4.c) writes it: when the game goes to the background (GameActivity.onStop, a
  thread of its own), when it is left with Exit (after the save wait), and once after a start whose pipelines made
  ahead took 10 s or more (some were not in the cache). Skipped when the driver's data is the size of the file.
- Measured on the S24 Ultra (the phone's own driver): the start that built the cache wrote it after its pipelines
  made ahead (32 MB, read out in 13 ms, written in 29 ms, no frame lost); Exit then found nothing new. The next start:
  pipelines made ahead in 2.6 s instead of 113 s, the game's frames from about 8 s after Play, the title screen at 18 s
  (was about 2 minutes).
- **Loading screen** (GameActivity.showLoading): the game's window stays empty until its renderer shows a frame, and
  Samsung's opening animation stopped half way on it. A window of its own (not focusable, not touchable) now covers it
  at once in the app's colours: the icon, "Preparing graphics... n%" while the pipelines are made ahead (polled from
  `nvn_vk_load_progress`), "Starting the game..." after, and a note on a slow first start. It fades out once the game's
  frames have reached the window for 0.6 s (`nvn_vk_window_presents`).
- The game's window keeps the screen on (FLAG_KEEP_SCREEN_ON): the phone locked itself during a 2-minute first start.

## The map below 30 fps: heat, not the code (2026-11-03)
- Reported: under 30 fps on the map screen after a long session. The two play logs (over an hour of testing while
  charging) show thermal "light" all along with the big core capped at ~1.8 GHz (2.6-3.4 when cool): 22-28 fps
  stretches, the render thread's batch replay 15-18 ms, ~0 ms waiting for the GPU (CPU-bound on lowered clocks).
- MEASURED, phone cooled to thermal status 0, the same build and the user's own graphics.ini (vsync mailbox, scale 1.00,
  fxaa, sharpening 75, 30 fps): Cascade gameplay 30.0 fps (1% low 21-26, replay 11-18 ms), the map ('-') 30.0 fps
  (1% low 27-28, replay 6.3 ms). No code change; today's additions (icons, pipeline cache, loading screen, the :game
  process) do not cost frames.

## Heat: the touch controls held the GPU clock high (2026-11-03)
- Method: `heat.sh` (scratchpad tooling, not in the repo). Each run cools the phone to thermal 0 and SKIN ≤ 35 °C, plays
  3 min of Cascade with the phone on a cooler, and samples GPU busy / clock (`gpu_clock_stats` residency) and the HAL
  temperatures. `SMO_VK_PASSTIME=<frame>` now also times the present pass.
- MEASURED before, with the user's graphics.ini: the present pass took 20-25 ms of GPU time a frame, the game's own
  60 passes ~5 ms. GPU 84-91 % busy at 680-834 MHz; after 3 min AP 48-49 °C, SKIN 40.4-40.7 °C.
- Ruled out one at a time (present pass still 22-24 ms each): FXAA and sharpening off; the compositor rotating the
  picture instead of the pass; the frame's work submitted before the acquire; the acquire's fence waited for on the CPU
  (0.00 ms: the swapchain image is free); a one-colour shader for the picture; drawing into an image of our own and
  copying it. An empty pass (no draws) and a pass without the touch controls' draw were fast.
- Cause: the touch controls' shader indexed constant arrays (the letters' strokes, `GS[43]`, `GR[9]`) with a variable.
  On the Adreno 750 (system driver) that cost ~20 ms a frame for ~20 small squares. Fix: the tables are in the overlay's
  uniform block (`k_ov_gs`, `k_ov_gr`, OV_BYTES 2048). The fps counter's `SEG[10]` became packed constants.
- MEASURED after, the user's graphics.ini, touch controls shown: present pass 2.65 ms, GPU 231 MHz 100 % of the time
  (47 % busy), 30.0 fps (1% low 22-26). After 3 min AP 40.9 °C, SKIN 36.2 °C. The screenshot matches the old one
  (letters, icons, fps counter).
- With a controller the touch controls hide (auto mode), and that case was already cool before the fix (run without
  the overlay draw: 231 MHz, AP 41.6 °C). The fix matters while the controls are on screen.
- The display rate: touches raised the panel to 120 Hz. Now `setFrameRateBoostOnTouchEnabled(false)` (Android 15+) and
  the 60 Hz vote with COMPATIBILITY_DEFAULT (FIXED_SOURCE behind `SMO_FRAME_RATE_VIDEO`). With them, 10 of 13 runs
  stayed at 60 Hz after 8 swipes (three went to 120). Not settled.

## The user's own setup: controller, passthrough charging (2026-11-03)
- adb over Wi-Fi (wireless debugging, paired), so the GameSir X3 Pro could sit in the USB-C port. `heat.sh` with
  `PAD=wait`: the user presses A on the title menu (an injected `input gamepad` key joins as a virtual player 1 that
  the menu ignores). On USB to the PC the battery mostly drained during play (down to -1.0 A): those runs had almost no
  charging heat.
- MEASURED after 3 min, cooler on, touch controls hidden by the controller:
  - flag view (845 draws), no charging: 30.0 fps, GPU 231 MHz, AP 40.2, SKIN 35.2, BAT 29.2 °C.
  - waterfall view (1170-1196 draws), no charging: 24-27 fps, AP 42.9, SKIN 37.0, BAT 30.9 °C.
  - waterfall view, passthrough charging (battery +2.0..2.6 A): 25-28 fps, AP 44.5, SKIN 38.4, BAT 31.2 °C. A
    power-saving overlay (the phone's app lock) may have been on in this run; the fps matched the uncharged run.
- So: charging adds ~1.5 °C in 3 min and costs no frames. Thermal status stayed "none" in every run.
- The waterfall view's fps loss is the CPU: render thread 21.5 ms a frame (draw preparation 13.0), the game's busiest
  thread ~60 % and the render thread ~40 % of a 37 ms frame, apparently one after the other. In that run neither ran
  on the prime core (cpu7); both were on the medium cores at 0.5-2.0 GHz. In the flag view one sat on cpu7. Next:
  thread placement (the ADPF session / affinity) and how far the game thread and the render thread overlap.

## Heavy views: CPU boost (2026-11-03)
- Same waterfall view, 1,310-1,350 draws, 30 fps mode, system driver. Each idea was switched at run time in one
  session, so the comparison is at the same view and temperature.
- Threads made by the render thread inherited its pin to cpu7: pw_main x6, wd_main and pf_threads all queued on the
  one prime core. `plat_posix.c` thread_main now widens a one-CPU affinity to every other CPU. Built, not measured on
  its own.
- Present queued to the render thread (`SMO_PRESENT_ASYNC`): the game thread's wait-present went from 21 ms to 0, but
  the frame rate stayed at 25.5-26. The frame is the render thread's replay (~21 ms, draw preparation ~13 ms) at low
  clocks. The switchable test hook was removed.
- A utilization floor for the frame's threads (sched_setattr UTIL_CLAMP_MIN): Android refuses it to apps (EPERM).
- Hot threads spinning 16 ms before they block (`plat_spinhot_ns`): 25.5 -> 29.3-29.8 fps, 1% low 24-27, replay
  21 -> 16 ms, mid cores 0.5-1.4 -> 1.8-2.0 GHz. Cost: the chip warmed ~2 °C a minute (44.2 -> 46.8) where it cooled
  ~2 °C without; SKIN 38.2 -> 39.1.
- So it is automatic: graphics.ini `boost = auto` (default; launcher Performance, "CPU boost"), in
  `android_perf.c` boost_frame. It turns on after two 1 s windows 5% over the frame target, holds at least 10 s, then
  goes off to look; a still-heavy view turns it back on and doubles the hold (up to 2 min).
- MEASURED with auto in the waterfall view: on at 24.1-25.6 fps, 28.7-30.0 fps while on, holds 10/20/40/80 s. At the
  title menu (122 draws, 30.0 fps) it stayed off, clocks 0.8-0.9 GHz.
- `boost = on` ("Always"): MEASURED at the title, the log says on, the two frame threads 68-71 % busy (45-55 % without),
  clocks 1.3-1.4 GHz. Not measured in the heavy view or for heat. `boost = off`: no spin.
- In 60 fps mode the target is 16.7 ms, so auto turns on below ~57 fps. Not measured in 60 fps mode yet.

## Mali testers: haze, white glow blobs, flashing (2026-11-04)
- Two testers, both MediaTek with Mali and the system driver: MT6897 (Dimensity 8300, Mali-G615 MC6, Vulkan
  1.3.247) and MT6899 (Dimensity 8400, Mali-G720 MC7). In Cap Kingdom: washed-out haze, large white glow blobs that
  shouldn't be there, flashing; tester 1 also has blocky stair-step edges around objects. The Bowser cutscene looked
  right on tester 2. A third tester (Poco F6, Adreno 735, stock driver, no log yet) needed Turnip for graphical issues.
- What both Mali logs share and the S24 (Adreno 750) does not: no BC (CPU decode), no VK_EXT_depth_clip_control
  (depth remap in the shaders). No error lines unique to Mali; no BC6H textures in use.
- Emulator (gfxstream on an RTX 4060, also no depth_clip_control) with `SMO_VK_NO_BC=1`: the opening cutscene renders
  correctly, so the CPU BC decode and the depth remap are not the cause; the BC4/BC5 snorm decoder also checks out
  against the spec. What remains is the Mali driver itself.
- Most likely cause: the translator emitted Maxwell FMNMX as GLSL `min`/`max` and `.SAT` as `clamp(f, 0.0, 1.0)`. Both
  are undefined for a NaN in GLSL; Maxwell gives the other operand (FMNMX) and 0 (SAT). NVIDIA and Adreno happen to
  return the non-NaN value; Mali keeps the NaN, and a NaN in the HDR picture spreads through the bloom into blobs.
  Fixed in `mx_translate.c` (FMNMX with isnan checks, saturate as `f > 0.0 ? min(f, 1.0) : 0.0`, the half-float pair
  likewise); MX_VK_VERSION 8 -> 9, so every phone translates its stages again once (a first start's shader work).
  NOT tested on Mali: no Mali device here.
- Also changed: with a geometry stage, the depth remap goes on that stage at every EmitVertex instead of on the
  vertex stage before it (a geometry stage may build its positions from what the vertex stage left in gl_Position);
  only SMO's lightning effects have one. And `nvn_vk_fmt:` log lines name every format the GPU cannot filter
  linearly or blend (a format the game samples or blends anyway), for the next Mali reports.

## Draw preparation without push descriptors (2026-11-04)
- Mali-G615's driver (tester 1) and the emulator have no VK_KHR_push_descriptor, so set 0 (the buffers) was allocated
  and written at every draw: 1337 sets a frame on tester 1, draw preparation 10.5 ms of an 18 ms replay. On the
  emulator, a profile of the render thread (simpleperf) put 44% in vkAllocateDescriptorSets and 12% in
  vkUpdateDescriptorSets.
- Now (`dyn0_pick`, `dl0_get`, draw_prepare):
  - up to `maxDescriptorSetStorageBuffersDynamic` of set 0's buffers are STORAGE_BUFFER_DYNAMIC: all of them when the
    device allows that many, else the uniform slots and storage buffers first;
  - the draw passes their offsets at the bind, and a set is reused for every draw of the command buffer with the same
    layout, buffers, ranges and remaining offsets (full list comparison, as for the texture sets);
  - pipelines with the same bindings share one set layout (set 0 without push descriptors, set 1 on every device),
    so draws of different pipelines share sets too. The one layout of library pipelines (GPL) is left as it was.
  - `SMO_VK_DYN0=0` turns the dynamic buffers off (sets are still reused when the whole list matches).
- Emulator, same Cap Kingdom play view (481 sets a frame before): draw preparation 54 -> 36 ms a frame, batch replay
  58.5 -> 39.9 ms, 14 -> 20 fps; sets allocated 426 -> 194 a frame. The frames match the old build's within the
  run-to-run differences of that view (an idle animation; mean difference 0.2-0.8 of 255 against the old build and
  between two runs of the new one).
- NOT measured on a phone. Devices with push descriptors (Adreno, Mali-G720) change only in the shared set 1 layouts.
  Without push descriptors the set layouts differ from before, so the driver's pipeline cache misses once (first start
  after the update makes its pipelines again).

## Mali, round 2: format limits and 32-lane warps (2026-11-04, beta 0.1.2)
- 0.1.1 on the Mali-G615 tester: faster, haze unchanged. The photos are Cascade (examples 7-9, 11: left part of the
  screen shaded, right part washed out, with a hard edge) and Sand Kingdom (example 10).
- Ruled out: robust image access (Vulkan 1.3 makes it mandatory; every reported feature is enabled); reduced
  precision (the translator emits no mediump/RelaxedPrecision); the BC CPU decode and depth remap (earlier).
- The tester's log: Mali-G615 cannot blend R32_SFLOAT (100) or R32G32B32A32_SFLOAT (109), cannot linear-filter
  R16G16_SNORM (78). The game blends into 100 (Cap: program #26327, 256x128, SRC_ALPHA/ONE_MINUS_SRC_ALPHA: a temporal
  smoothing) and 109 (Cascade: program #26723, 1x1, two attachments, ONE+ONE: an accumulation, most likely the
  exposure's scene brightness). Undefined on Mali; the leading suspect for the overexposure. 0.1.2 draws them
  without blending (pipe_build; `SMO_VK_UNSUP_BLEND=1` keeps the blend). Emulator with the limits imitated
  (`SMO_VK_SIM_FMT=100:b,109:b,78:l`): Cascade's average brightness 160-161 with and without. The exact fix if the
  haze stays: those textures as R16F/RGBA16F when the GPU cannot blend the 32-bit format.
- The nearest fallback for 78 is opt-in (`SMO_VK_UNSUP_NEAREST=1`): forced on program #21752's BC3 unit on the
  emulator it whited out the whole screen through the bloom.
- Found and fixed on the way: Maxwell lane semantics. 18 of the 68 compute stages (alComputeShader.sharcb:
  alComputeCullPrepass, triangle culling) read SR_LANEID; 6 also use SHFL and VOTE ballot. The translator took
  gl_SubGroupInvocationARB, which runs 0-15 on Mali and 0-63 on Adreno. Now `MX_LANE` = linear local index & 31 in
  compute (exact on any GPU); SHFL/ballot go through `mx_shfl` / `mx_ballot` over the 32-lane window of the subgroup
  (exact for subgroups >= 32, made of consecutive invocations; on a 16-lane subgroup only within each half).
  An exact exchange across halves needs barriers, which the shuffles' non-uniform control flow does not allow.
  On the emulator these stages are not dispatched in Cap or Cascade (only EffectPtcl's particle compute stages are),
  so this is not the haze. MX_VK_VERSION 10 -> 11.
- Compute groups are split to `mx_max_invocations` (the device's limit, in the stage hash when not 1024), not 1024.
- New logs: subgroup size and compute limits at start; `nvn_vk_cs:` each compute program at its first dispatch (code
  hash: match against build/shaders_all from tests/check_all_shaders.py --extract-only).
- Note: the first start after a version bump on the emulator sat on the controller-style screen for 19,500 frames
  while 12,000 modules were translated; the warm start reached the level.

## Mali, round 3: measuring on the phone (2026-11-05, beta 0.1.3)

The tester reported 0.1.2 "the same if not worse". The warp fix does not touch these scenes (the culling compute
shaders do not run in Cap or Cascade, only EffectPtcl's), and turning off the undefined blends did not help. No more
blind fixes: the new build measures the fault on the phone that shows it.

- **`diag_present` / `diag_pass_end` / `diag_target` (nvn_vk_draw.c).** On ARM GPUs (or `SMO_VK_DIAG=1`; a number over 1
  is the distance in frames, default 3600; `diagnostics = off` stops it), after 240 frames of 150+ draws, one round:
  E: one undisturbed frame, every colour target read at the frame's end. P: two frames later, every colour target
  read after each pass (a flush and a wait each), and the frame's draws logged. If P looks right where E looks wrong the
  fault is ordering (barriers, tile memory), not a shader. Each target gets an `nvn_vk_diag:` line (per-channel
  min/max/mean, NaN and infinity texels, % over 1 and over 16) and a small BMP (E 400 wide, P 160; floats as v/(1+v), NaN
  magenta, infinity cyan). Files go to `<files>/diag`, or `SMO_SHOT_DIR` when set; the last 3 rounds are kept.
- **Log export** adds `diag/*` to the zip. Emulator: 316 files, 2 rounds of smaller pictures, zip of 2.8 MB.
- **`mali_format_fix` (`fmt_sub`, default on; `= off` or `SMO_VK_FMT_SUB=0` to compare).** Only where the GPU lacks the feature, and only for render
  targets (texture flags 0x8 or 0x40, not packaged: the game writes texels into 4x4 R32F textures and makes 128x128 RGBA32F
  ones without these flags): R16G16_SNORM to R16G16_SFLOAT, R32F to R16G16_SFLOAT (green reads 0), RGBA32F to RGBA16F.
  With that the 0.1.2 blend-off does not apply to them. Emulator with `SMO_VK_SIM_FMT=100:b,109:b,78:l`: Cascade looks
  right and the targets hold the same values as before (for example the 1x1 target: 0.09003 against 0.09).
- **Device log:** driver name and version, `shaderClipDistance`, `maxClipDistances`, float controls.
- A healthy Cascade frame on the emulator, for comparing with a Mali capture: 62 passes, no NaN or infinity anywhere;
  800x450 R11G11B10 up to about 2100, 400x225 RGBA16F up to about 190, 800x450 R16F up to 65504 (far depth); 1x1
  RGBA32F exposure 0.09.
- **Adreno compiler crash from 0.1.2 (fixed).** On the S24 Ultra, `vkCreateComputePipelines` crashed (null pointer in
  libllvm-qgl.so, offset 0x463ed4) on the 6 compute stages with warp 3, 8 to 40 s into a first start. Tested one at a
  time on the phone: new ballot with the old shuffle compiles, new shuffle with the old ballot crashes. Removing the
  dynamic index, the runtime size test and the local-invocation lane did not help, as long as `subgroupShuffle` stayed
  in a function. As a macro it compiles. So with 32 lanes or more (`mx_subgroup_wide`, part of the stage hash) `mx_shfl`
  is a macro and a stage that exchanges values takes its lane from the subgroup (`gl_SubgroupInvocationID & 31`).
  Subgroups under 32 (Mali) keep the 0.1.2 functions, which that tester's phone compiled. These stages do not run in
  Cap or Cascade, so their results are not checked anywhere. `nvn_vk_cs:` now logs each compute pipeline before it is
  made, so the next compiler crash names its stage. `MX_VK_VERSION` 15.
- **Capture bug found on the S24:** reading a target submits. When `cmd_submit` itself ended the pass, it then set its
  event on the command buffer already submitted: a crash in vulkan.adreno.so, tolerated by the emulator's NVIDIA driver.
  `pass_end` now begins a new command buffer after a capture.
- **Measured on the S24 (Cascade, 30 fps mode):** 4 rounds, 62 passes each, worst frame 401 ms, 3 rounds zip to about
  8 MB. No NaN or infinity in any target, E and P agree, the pictures match the scene. Without the switch the capture
  stays off on Adreno and 30 fps holds. A healthy 1x1 RGBA32F target there: red 0.245, alpha 1.
- **Testing on a real phone:** `run_shots.sh` with `SMO_START_STAGE` autosaves into that phone's save; back it up first
  and restore it after (done here, the save is identical again). TaskStop stops the tool's shell but not the script's
  `sleep`: a stopped run later force-stops whatever game is running.
- **The white-out: sampler pool exhausted (fixed).** Emulator, Cap Kingdom at the "Look Around" hint: in some runs the
  picture went white and stayed white. An earlier note here blamed 0.1.2's blend-off. That was wrong, drawn from too few
  runs of an intermittent fault: it also happened with no limits simulated and the capture off (1 of 4), and not with
  the limits simulated in others.
  - Mechanism, from the logs: the bloom chain draws mip N+1 of a 200x112 R11G11B10 texture from mip N, choosing the
    level with a sampler clamped to lod N..N (`texture()`, fs #26330 = a plain copy). In white runs these binds carry
    sampler ID 0, which is never registered, so `bind` used the default sampler (nearest, no clamp): automatic LOD, so
    the pass reads the level it is drawing to. That level holds 65020 (the format's maximum), and the chain spreads it.
  - Cause: `agl::driver::NVNMgr::registerSampler` [main 0x77623c] finds a sampler's slot by binary search on a 16-bit
    hash of bytes 8..0x60 of the NVNsampler, and answers ID 0 when no slot is free (3839 slots, IDs 257..4095). The port
    filled only the first 48 bytes (`SamplerParams`), so the hash covered leftover memory, and missed filters and wrap
    modes, which sit in the unhashed first 8 bytes. Nearly every registration took a new slot.
  - Fix: `smp_initialize` clears the rest of the 0x60 bytes and copies those 8 bytes into it. `SMO_SAMPLER_KEY=0` brings
    the old behaviour back for tests. New log lines: `smo_fast: sampler pool:` and `nvn_vk: ... not registered`.
  - Without the fix: m1 white, pool 3838 of 3839, over 140,000 binds with ID 0; m2 normal, about 2200 in use, 78,000 new
    slots in 280 s, cursor wrapping again and again. With it (5 runs: Cap 3, Cascade, Sand): under 192 slots, 0 binds
    with ID 0, no NaN or infinity, final picture mean 0.0448 as in the good runs. White in about 1 of 3 runs before.
  - The "dozen samplers a frame" that `smo_fast.c` speeds up were this fault. Not measured yet: the frame time gain on a
    phone. `nvn_gl.c` is shared with the PC build, which got the same change and was not run.
  - Not established: whether this causes the Mali spots or problem 0 below. Both could follow from a full pool (any
    effect gets the default sampler), and the new log line would show it in a tester's log.
- **`mali_format_fix` stays on, without proof that it helps.** It makes blending and filtering defined on Mali, where
  0.1.2 left filtering undefined and turned blending off. Measured on the emulator: no visible effect either way.
- **Not explained: the spots.** Testers on Mali describe random spots around the map with giant white blooms, not a
  white screen. Sand Kingdom on the emulator with the limits
  simulated, with the fix, and without limits: same picture statistics (red mean 0.4126, 0.4124, 0.4126), no NaN or
  infinity, no patches. The simulation takes the feature flags away, not the hardware's behaviour: the emulator's GPU
  still filters R16G16_SNORM, so that suspect cannot be tested here. A capture from a Mali phone should show the first
  pass where the spots appear.
- NOT tested on a Mali phone. Tester steps: New shaders on Wait, Cascade and Sand, export after each; then Mali format
  fix Off and the same again.

## Mali, round 4: the tester's 0.1.4 logs (2026-11-05)

Mali-G615 MC6, driver r44p1, Android 15. Tester: the white spots are still there; menus load much faster; a little fps
lost. Two runs in the zip, 3 capture rounds, screenshots 20-24.

- **What the captures show is not settled.** None of the 3 rounds has the full-screen wash of screenshots 20 and 23
  (screenshot 20 has the same coin count as round 4, so it is from about that time). E and P agree, no infinity, HDR
  ranges as in the healthy reference (800x450 up to 2144, 400x225 up to 167). A first note here called them all normal,
  and the user corrected it: the rounds do hold white the user reads as the fault, and also real waterfall mist.
  - Mist: the S24 and emulator show large white mist in the 400x225 effects buffer at Cascade's falls too, with the same
    ranges (S24 up to about 200, mean about 2.6), so white beside the falls is not by itself the fault.
  - Not mist, candidates: pale blocky patches on the stone in round 4. They are already in the lit HDR target before the
    effects buffer and bloom (`r004_E_013`), so those passes do not make them. There is no healthy capture of that spot, so
    they are not proven wrong. Also a lattice of dots in the 200x112 bloom target (`r004_E_021`), a blue triangle at the
    right of the final picture, and white shards in the G-buffer's fourth target (`r004_E_016`).
  - Next: the user marks which white areas are the fault; a capture taken when the wash is on screen; a capture of the
    same spot on the S24 to compare.
- **The tester's photos (`tester's logs/`, all builds so far) show three kinds of fault, not one spot:**
  - Huge soft glow around a point, with a yellow wash in some: examples 1-4, 6-9, 17, 18, 20, 23 (Cap and Cascade). This
    has not been captured. The shape fits a very bright source spread by the bloom chain.
  - Bright patches with jagged, stair-stepped edges on the ground: 10, 16, 19, 22 (Sand). The edges point to a
    low-resolution input such as a shadow map, and away from bloom. Not proven.
  - A dead-straight vertical line between dark and bright: 11.
  - Triangular gaps in Mario and Cappy on the title screen (15, 21, 24), found by the user: the menu background shows
    through them, so they are missing triangles, not dark ones. A first note here called this menu fine, which was wrong.
  - Fine: the pre-rendered cutscene video (5).
- **The gaps: what has been checked (2026-11-06).**
  - The 32-lane compute culling idea has no support: `nvn_vk_cs: compute program` (printed when a dispatched program has
    lane flags) is in neither the Mali logs nor the S24's.
  - **The title screen has no EQUAL draw at all** (S24, one frame captured with `SMO_VK_PASSSHOT`, 122 draws). It draws its
    models in a depth-only pass (`chanmask 0`, programs from #26282) and then a colour pass (`chanmask f`, from #26301), both
    LEQUAL with depth writes. So rounding (mx_translate's `invariant gl_Position` note describes that fault on title-screen
    Mario) can only make gaps there when the colour z lands behind the depth-only z. Still the leading idea, not proven.
  - The automatic capture needs 150 draws a frame, so the title screen had never been captured on Mali; the logged EQUAL
    draws are gameplay.
  - The S24 does not reproduce the gaps with `SMO_VK_NO_DCC=1` (Mali's depth path), with or without
    `SMO_VK_NO_INVARIANT=1`, judged by eye from title screenshots, and the counts below stay at their healthy level. Mali
    hardware is the only way to get the answer.
- **0.1.5 measures it (`nvn_vk_eq:` lines, on by default on Arm, `SMO_VK_EQTEST=1` elsewhere).** Occlusion queries over 5
  frames compare each draw as it is with the same draw with z pulled nearer (`nvn_vk_draw.c`, before `gsq_poll`). Also a line
  every 600 frames with the draws per depth test, and up to 3 capture rounds of screens with 20 to 149 draws.
  Healthy reference on the S24 (Adreno 750), `SMO_VK_NO_DCC=1 SMO_VK_NO_BC=1`:
  - Title, LEQUAL colour draws: near misses within 256 units -0.04% to 0.11%; the counts move by about 1000-1300 samples by
    themselves (of 220,000-370,000), which is more than the near misses.
  - Cascade, EQUAL: 354,000 samples pass, 355,600 within 16 units (0.35%), 366,900 within 256 (3.4%). Cascade's LEQUAL colour
    draws: below the noise. Read a Mali log against these numbers, not against zero.
  - Control, `SMO_VK_EQTEST_CTRL=1` on the title: 100%, so the measurement can see the fault.
  - How to read the Mali log: title near misses far above the reference mean rounding, and the `LEQUAL vs #...` lines name
    the draws; at the reference level with gaps on screen, rounding is ruled out and the triangles are missing for another
    reason (vertex or index data), which the title capture round should then show.
  - Ready for the next step, off in 0.1.5: `SMO_VK_PRECISE=1` marks vertex and geometry float results `precise`
    (NoContraction). Tested only in that it compiles and draws on the S24. Use a `SMO_VK_CACHE` of its own and bump
    `MX_VK_VERSION` if it becomes the default. `SMO_VK_NO_INVARIANT=1` likewise needs its own cache.
- **Working idea, unproven:** the lighting stage makes some surfaces far too bright, and bloom spreads that into the glow.
  In its favour, the blocky patches of round 4 are already in the lit HDR target before bloom. Against it, no captured
  frame holds a source bright enough, because HDR ranges match healthy runs.
- **The user on photo 22 (Sand): the fault is "weird random and weirdly shaped shadows".** Leading suspect, not yet tested:
  - The shadow mask is the 800x450 RGBA8 target (green = sun visibility; magenta in the BMPs = green 0). It is drawn by one
    full-screen program (`#26730` in Cascade on Mali, `#26321` in Sand on the emulator; stage spv `9fd704ab3a2c76bf` on Mali
    and S24), which reads the 1024x1024 DEPTH32F sun map with a LINEAR, non-compare sampler (min 5, mag 1).
  - Filtering D32_SFLOAT is optional in Vulkan, and the check in `bind` covered colour only, so the logs could never have
    named this. A Unity bug report describes D32_SFLOAT with bilinear falling back to point on Android, Mali phones
    included. Not known yet for the G615.
  - Added, uncommitted, in no build handed out: a `nvn_vk_fmt: ... depth texture ...` line saying whether the GPU can filter
    it, and `SMO_VK_SIM_DEPTH_NEAREST=1` to read such textures nearest on any GPU.
  - Next: finish the emulator comparison (Sand, `STAGE=SandWorldHomeStage`, with and without the switch, at least 260 s
    each: 110 s ends while the stage still loads; the first attempt showed nothing). Stopped when the user had to leave.
    Then a tester build to get the log line from the G615.
- **Capture gap:** it reads colour targets only, so the sun's shadow maps (depth) have never been seen on Mali.
- **False alarm:** the solid olive 0x13 target in the handheld rounds is not broken normals. It is a second target of that
  format holding values near zero (under 0.03, most likely motion vectors); the normals target is the first one and is
  fine. The healthy docked emulator and S24 frames list only one 0x13 target at the frame's end, so the two modes differ.
- **Ruled out as the cause of the white:**
  - Sampler pool: 116 in use of 3839, 0 unregistered binds, and the white is still seen.
  - Format fix alone: with it on, 0 blend or filter limit hits, and screenshot 20 is white anyway (matched by coin count).
- **New side effect of the format fix:** with it on, 2 to 4 NaN texels in the G-buffer's second target (NVN 0x13, 800x450;
  R16G16_SNORM cannot hold NaN, the R16G16_SFLOAT made for it can). None in the two rounds with it off. No NaN in any
  later pass of that frame, so not shown to cause anything, but a NaN in the G-buffer is the sort of thing that could
  feed a bloom spot. Worth clamping or checking.
- **Leads, none proven:**
  - Shot 20 shows white blocks at the bottom, which may be the low bloom levels (their passes render one level while
    sampling another of the same image). Shots are 320 wide, so this is a reading of very few pixels.
  - Round 4 shows blocky pale patches on stone; the CPU BC decoder (arm64 only runs it on Mali) is a suspect. It has no
    `char` or SIMD differences between x86 and ARM, and on the emulator it shows no patches. `SMO_VK_NO_BC=1` on the S24
    would run it on arm64 with a GPU that shows no spots.
  - Blend-off on RGBA32F: the 1x1 target reads 0.015 with alpha 0 with the fix off, 0.11 with alpha 1 with it on.
- **Fps: the two runs are not comparable.** First run: handheld, resolution scale auto, mailbox, median 28.6 fps. Second:
  docked, resolution scale 2.00 (four times the pixels of 1.00), vsync on, Vulkan memory 3673 against 2176 MB, median
  24.8 fps over only 6 lines, some of them with shaders still compiling. No evidence that 0.1.4 itself costs fps.
- **Menus faster:** nothing in the logs measures it. The sampler fix and a warm pipeline cache (45 MB read at start) are
  both possible.
- **Effects lost for good:** not seen in these 3 rounds, whose final frames are lit. Not checked with a death on Mali.

## Mali, round 5: glow attempt and more evidence (2026-11-06, beta 0.1.6)

Nothing here has run on a Mali phone. The S24 runs only show that it does no harm with Mali-like settings
(`SMO_VK_NO_DCC=1 SMO_VK_NO_BC=1 SMO_VK_GUARD=1`).

- **Audit of the translator against Maxwell:** shifts, BFE/BFI, F2I, FMNMX and saturate were already exact. MUFU was not:
  `log2(x <= 0)`, `inversesqrt(x <= 0)`, `sqrt(x < 0)` and `1/0` are undefined in GLSL. Compiled `pow(x, y)` is usually
  `exp2(y * log2(x))`, and specular hits x = 0 often, so an answer other than `-inf` could make exploding bright spots.
  Unproven.
- **`mx_guard` (on for vendor 0x13B5, `SMO_VK_GUARD=0/1`, part of the stage hash):** MUFU helpers `mx_lg2 mx_rcp mx_rsq
  mx_sqrt` with defined answers, and `mx_ok` at the fragment exit: a bit test turns NaN and infinity into 0 before the alpha
  test. Bit test because `isnan` may always be false where Vulkan does not promise NaNs. Results on the S24 against the 0.1.5
  run `v5g`: Cascade lit target means 2.39/7.00/2.43 with it, 2.39/7.00/2.43 without; no NaN or infinity in either.
  Sand matches between guard off and on. Frame rate unchanged at 30 fps in Cascade. If 0.1.6 logs show no change in the glows,
  the guard can come out.
- **False alarm:** a nearly black `screen.png` in Sand, in guard-on runs. It is the phone's accidental touch overlay, which
  dims the whole display: the touch buttons and fps counter go dark too, and the game's own frames stay at brightness 73.
- **`mx_precise`, alternating (Arm, or `SMO_VK_ALT=1`; `SMO_VK_PRECISE=0/1` fixes it):** counter in
  `../config/precise_test.txt`, even-numbered starts on. The log line `nvn_vk: test build:` says which.
  **Take the alternation out before a public release.**
- **Capture additions:** `nvn_vk_diag: Z` lines and `r###_Z_###.bmp` (depth aspect copied directly, scaled targets at their
  own size; low values bright, exactly 0 and 1 black; 640 wide on screens under 150 draws, 160 otherwise; no picture when
  all one value or the same as the pass before). `nvn_vk_tri:` pipeline statistics per draw, at most 300 draws in each of 8
  rounds. `nvn_vk_hash:` on screens under 150 draws, at most 1500 lines: code hash of each stage (the same on every phone),
  SPIR-V hash, index list and vertex stream checksums. `DIAG_KEEP` 6, `g_diag_gap` 1800. On the S24 a Cascade round zips to
  about 2.6 MB, so 6 rounds are about 16 MB.
- **Title depth on the S24 (healthy reference):** 1600x900, values 0.1885 to 0.2875, Mario and Cappy solid with no holes.
- **Not kept:** a Capture button press starting a capture round. Two scripted taps never reached it, and the tester's
  screenshots with automatic rounds cover the need.
- **How to read the tester's logs:** holes in the title `Z` picture mean the fault is before colour (vertex or index data,
  or the depth-only pass). Compare `nvn_vk_tri:` and `nvn_vk_hash:` with the S24's lines for the same stage hash. A difference
  between precise on and off points at rounding.

## Other ports worth reading (2026-11-06)

[LAS Recomp](https://github.com/peiikai/LASRecomp) (Link's Awakening; develop on the highest `beta_<n>` branch, `main` lags)
and [Power Moon Recomp](https://gitlab.com/zzzarathustra-dev/power-moon-recomp) (SMO 1.0.0, x64, forked from LAS). Both MIT:
keep their notices if code is taken. Read only, not built or run. Numbers below are theirs, none measured here.

**[nx2android](https://github.com/shirai91/nx2android)** (read 2026-11-13; README, RESEARCH.md, docs/ARCHITECTURE.md; not built or run):
"a compatibility layer for porting Switch games to Android, one game at a time", the Android port of nxnative (iOS). The user's own
dump and keys go through a rewriter (patch_nso.py patches the main module), and a host runtime (libdmhost.so) runs the game's own
arm64 code with the system services stood in for and NVN replayed on Vulkan; shaders are translated ahead of time to SPIR-V with a
fork of Ryujinx's translator (MIT). About 50 games in its table, SMO among them (v0.2.0, update 1.4.1, 60 fps, tested on a Lenovo
Legion Y700, Snapdragon 8 Elite / Adreno 830). **The GPU must sample BC textures in hardware: Adreno and Xclipse, not Mali or
PowerVR** (its RESEARCH.md lists Mali as future work; several games want Turnip). Created 2026-10-24, very active (commits on
2026-11-13), ~1.5k stars, 206 forks. README says MIT like nxnative (nxnative/LICENSE; GitHub shows no root license file). Ideas to
read later: its GPU clock handling (#307), frame pacing hooks, pipeline-cache prewarm.

**nx2android's Vulkan backend compared with ours** (read 2026-11-13, commit 00f41ec, `nxnative/dm/android/gfx_vk_*.c` and
`perf_android.c`; nxnative/LICENSE is MIT, "Copyright (c) 2026 seedds": keep that notice with any code taken):
- *GPU cost:* nothing to take for Mali. Every attachment is LOAD/STORE (only a whole clear becomes a load op), every image stays in
  `VK_IMAGE_LAYOUT_GENERAL`, and a whole-pipeline barrier comes before every pass. We already have load-clears, discards, light
  barriers and real layouts. They don't run on Mali at all.
- *Their GPU clock lesson (perf_android.c):* "a frame held at 30 fps lets the GPU governor lower the clock until the work fills the
  frame, so its GPU time says little". This fits the tester's Mali: GPU busy stays at ~17-18 ms whatever the draws or the 3D size.
  We can't read his clock ("GPU clock not readable"). They report the GPU time to ADPF on Android 15
  (`APerformanceHint_reportActualWorkDuration2` with `AWorkDuration_setActualGpuDurationNanos`). We report only a total, and his
  phone (Android 15) lacks the Android 16 workload-rise call. Untested whether MediaTek's power HAL uses the GPU time.
- *Pacing:* adaptive rate 60 -> 40 -> 30. A 40 fps step asks the window for 120 Hz, and a frame gets a desired present time
  (`VK_GOOGLE_display_timing`). Our window always asks for 60 Hz, so fps_cap 40 shows its frames for 1 and 2 refreshes by turns on
  a panel left at 60.
- *Render thread (not the limit on Mali; tester 0.3.4 in game: pass begin 0.75-1.0, buffers 1.6-3.0, descriptors 0.4-0.6, command
  buffer starts 0.2-0.3 ms a frame):*
  - VkRenderPass objects plus imageless framebuffers instead of dynamic rendering. Adreno spent ~40 us per vkCmdBeginRendering.
  - Command buffers reset on the completion thread, not at begin.
  - The per-stage descriptor set memo: we have a smaller one.
  - Async pipelines and prewarm, and the per-draw state cache: we have both already.
- *SMO correctness notes in it:*
  - Vertex fetch past a stream's bound size (alNoiseShader, Cascade mist): we handle it already (vtx_need).
  - The Cap Kingdom fog wave solver writes a 128x128 RGBA16F linear target that the CPU and the fog's vertex shader read; they copy
    such targets back to the pool. This is a lead to check against issue 11 (black grain in Cap on the same Mali), not verified.

**0.3.6 and the way to 60 on Mali (2026-11-13):**
- 0.3.6 handed to the tester. He was asked for: run 1 normal; run 2 with Game Turbo set to performance mode; run 3 with the FPS limit at 40.
- **His 0.3.4 frame (Cap Kingdom, judging by its targets):** GPU busy 17.6 ms. In-pass time is ~13 ms over 95 passes; the rest is
  idle time between passes inside the command buffers. In-pass time by target:
  - G-buffer 320x360: 4.6 ms (18 passes, 729 draws)
  - 128x128 RGBA16F: 1.5 ms (20 one-draw passes: the fog wave solver, with the 256x128 R32F pass)
  - 256x256: 1.5 ms (18 passes)
  - present 720x1600: 0.75 ms
  - bloom and exposure chains (160x180 down to 5x5): ~1.5 ms
  - everything else: ~2 ms

  No single target is worth a big cut. 80 of the passes draw 1-2 things, so the cost is a fixed price per pass on Mali.
- **S24, SMO_VK_PASSSHOT=5400 in Cascade:** 57 passes.
  - The passes in order: shadow 1024 depth; exposure 2x1; 128x128 x3; two rounds of 256x256 depth plus two one-draw 0x5 passes;
    G-buffer 800x450; light, sky and transparent passes; a bloom pyramid 100x56 -> 12x7 (3 passes a level, then back up, 16 passes);
    tonemap; 1600x900 HUD; present.
  - The G-buffer came out as 10 passes of 100 draws, ended by cmd_submit. That is the "GPU ran out of work" split, which fires on
    every check in a pass-shot frame because the tool waits for the GPU after each pass. In normal frames it happens ~1 time a frame
    (tester) to ~4 times (S24 Mali imitation), so it is not the tester's GPU cost.
- **S24 Mali imitation, 0.3.6, Cascade:** 41-44 fps, GPU busy 11.5-11.9 ms, 51 passes, 847 draws.
  - Render thread: buffers 1.7-1.9, descriptors 1.7-1.9, command buffer starts 1.65-1.8, after 1.4-1.5, pass begin 1.2-1.3,
    rest 1.7-1.9, waiting for the GPU 2.2-2.4 ms.
  - The stream ring copies 9.3 MB a frame in 6,300 ring_put calls (65% cached within a batch). Every batch starts a new
    generation, so unchanged vertex and constant data is copied again every frame.
  - Mali has no VK_EXT_external_memory_host (tester log: external_memory_host=NO), so letting the GPU read game memory directly
    would need nx2android's AHardwareBuffer pools (a large change).
- **Levers left, in order:**
  1. The GPU clock: the tester's Game Turbo run will show it.
  2. The game's own work in Mali-only options (the wave solver every other frame, fewer bloom levels). These change the picture,
     so they are the user's call.
  3. Zero-copy or cached static buffers for the render thread (large).

**Shader optimiser A/B (in progress, 2026-11-13):** `SMO_VK_OPT_FS` (spirv-opt on fragment and compute shaders) has been off
everywhere since 0.1.8, and only because of first-start compile time; it was never measured for GPU speed on Mali. LAS saw a large
Mali-G610 gain from it, on a shader in pc-loop form, and our translator emits that form for unstructured shaders (mx_translate.c
`any_jump && !structured`).
- **S24, Cascade, OPT_FS=1** (cache copied to `cache/vk_opt` on the phone, kept there for the next run): 59.4-59.7 fps, GPU busy
  12.1-12.9 ms at 231 MHz and 8.7-10.3 at 310 MHz. The picture looked normal and spirv-opt refused no stage.
- **Same view A/B** (forward standing view, 1209-1210 draws, GPU at 310 MHz in both runs):

  | | GPU busy | fps | thermal |
  |---|---|---|---|
  | OPT_FS=0 (base1) | 9.36-9.89 ms | 58.4-59.1 | light |
  | OPT_FS=1 (opt2) | 9.08-9.69 ms | 55.7-57.6 | moderate (CPU capped at 2035 MHz) |

  That is about 1-2% less GPU time: noise. The lower fps came from heat, not from the optimiser. **Adreno's own compiler already
  does what spirv-opt does.** On Mali it is unproven: LAS's 1.5 -> 30 fps was one pathological pc-loop shader. Not worth a tester
  build that recompiles ~25,000 shaders on his phone. Keep it as a later launcher test only if his clock test rules the clock out.
  The `cache/vk_opt` copy (1.9 GB) is still on the S24 and can be deleted.

**Tester on 0.3.6** (log odyssey-logs-20261113-0716; his words: "5 fps improvement", "much more stable now", the same with and
without Game Turbo, which changes nothing on his phone):
- `log.txt` is 0.3.6; `log.prev.txt` is an older build (its hint line has no GPU-time part), in a session with the CPU capped again
  (cpu7 at 400 MHz most of the time).
- **FPS limit 40:** a steady 39.2-39.9 fps (1% low 24-26). His screen offers 60, 144, 120 and 90 Hz, but **it stayed at 60 Hz:
  Xiaomi ignored the 40 fps window rate**, so 40 still alternates one and two refreshes on his phone.
- **FPS limit 60, end of the run:** 44.1-45.3 fps (1% low 24-33), **GPU busy 14.6-15.2 ms, against 17.1-18.0 in 0.3.4**, in the
  same handheld mode. The game's drawing did not change, so the likely cause is the GPU-time hint (accepted: "taken") raising the
  GPU clock. That is the clock effect nx2android describes.
  - Frames are now "mixed": GPU 15 ms, render thread ~11 ms of work (14.8 with 3.8 of waiting), game logic 4 ms of work, frames
    22 ms.
  - The GPU stands idle ~7 ms a frame. **Next target: better overlap between the render thread and the GPU.**

**0.3.7: Cap fog readback** (runtime/smo_linread.c; nvn_gl.c nvn_front_linear_readback; nvn_vk_draw.c vk_linread;
nvn_stub.c nvn_stub_pool_memory; link wraps of f_4000a2b794 / a2b81c / a2b8e0 in tools/android/build_runtime.py):
- The game reads three textures this way: 128x128 RGBA16F (the fog waves), 256x128 R32F and 512x512 format 0x5. All carry NVN
  flags 0x40 (linear render target) with stride 1024.
- vk_invalidate_texels treats only flags 0x10 as linear. On 0x40 textures it wrote the swizzled layout, so the game got values
  like 140 and -102. vk_linread writes rows at the stride.
  - The 512x512 0x5 texture has stride 1024, less than its row, so it is skipped.
- Measurements on the S24, Cap Kingdom:

  | Version | Cap fps | Note |
  |---|---|---|
  | Wait for the GPU at each read | ~30 | |
  | No wait, but a pool lookup and a walk of every texture record at each read | 24-34 | game logic 30-34 ms of work |
  | (pool, offset) cached per frame | 56 | game logic 4.1 ms |
  | Readback off | 54-57 | |

- With the fix, fog reads return 0.533-0.539, which matches Super Odyssey PC Recomp's documented 0x384f texel.
- The visible ripple interaction is not checked yet.

**Frame generation research (2026-11-13, nothing implemented; the user asked for research only):**

Power Moon Recomp's GitLab project was taken down. Its developer recommends the fork
https://github.com/Simon3Sarkis/super-odyssey-pc-recomp (MIT: LAS Recomp, Power Moon Recomp and Super Odyssey PC Recomp
contributors; read at ed09d53, 2026-11-13). Clone with `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never git -c credential.helper=`:
a plain clone of the old GitLab address opened a sign-in popup on the user's PC.

- **Frame interpolation** (`runtime/nvn/interp.c`, the same file as in 0.2.0): RT64-style.
  - The frame's command lists run again with each draw's uniforms blended between the last frame and this one.
  - Draws are matched by programs, draw arguments and target size; draws sharing a key are paired by their nearest uniforms.
  - Cuts are detected (camera cut, warp, level change).
  - Every in-between costs a whole frame of GPU work, so it is no use on a GPU-bound Mali. Its draw matching could give
    per-object motion vectors.
- **Native 90/120 fps** (`hle/odyssey_pose.c`, `odyssey_120.c`): draw-only frames between gameplay ticks, with skeletal poses
  evaluated at the fraction of a tick. Also a whole extra frame of GPU work.
- **Camera motion vectors, already built for their FSR 3.1, XeSS and DLSS upscalers** (`runtime/nvn/vulkan/dlss_probe.c`):
  - Camera only; there are no object motion vectors yet.
  - The camera comes from hooks in the game's code (`hle/odyssey_debug.c`):
    - SMO 1.0.0: the main draw's camera copy at 0x70004ccbc8 calls `al::getProjection` (x19 + 0x18, view 0) and
      `sead::Projection::getDeviceProjectionMatrix`, with the view 3x4 at the copied camera + 8 (x23).
    - `GraphicsSystemInfo::updateViewGpu` at 0x7000879c98 (x1 = view 0; x2 + 8 is the view, x3 + 0x4c the projection).
    - 1.4.1 addresses are in their `odyssey_v141.h`.
  - On the CPU it combines the last view-projection with the inverse of the current one. An 8x8 compute shader then writes RG16F
    motion from the depth: `old = previousClip * (ndc, z, 1)`, and motion = `(old.xy / old.w - ndc) * size * (0.5, -0.5)`.
  - This is exactly the cheap part of our extrapolation plan, and the hook points should hold for our port, since it runs the
    same 1.0.0 code.
- **Cap Kingdom fog readback (`docs/FOG-READBACK.md`): a real bug in our port too.**
  - The fog's height queries read the GPU-made half-float fog texture on the CPU
    (`al::getF32FromLinearTextureF16`, 1.0.0 at 0x7000a2b81c, its MemoryPoolMap return at 0x7000a2b86c).
  - Without a readback the game sees zeroes, and Mario's and Cappy's ripples in the fog are rejected (their trace: request 800
    gets w0=0, height difference 2500).
  - They read the one requested texel back on the render thread at that MemoryPoolMap call. nx2android copies such linear
    targets back as well. We have neither (no match for the function or a readback in runtime/).

**0.3.8 render-thread work (2026-11-13; S24, Mali's path, Cascade forward view, 1206-1209 draws):**
- **simpleperf works on this APK** (it is debuggable and profileable). Record from adb with
  `simpleperf record --app com.mobileodyssey.game -e cpu-clock -f 1000 --duration 15 -o /data/local/tmp/x.data`, and add
  `--call-graph fp` for callers. Read it on the phone with
  `simpleperf report -i ... --comms render --sort dso,symbol`, then resolve libsmo offsets with llvm-addr2line on the unstripped
  `build/android/arm64-v8a/lib/libsmo.so` (kept per handed-out build: `build/android/symbols/libsmo-<version>.so`).
- **Render thread profile:**
  - libsmo 38%, the Adreno driver 34%, libc 26% (memcpy 10%).
  - Ours: replay ~9% (load of each record header), ring_put 8% (cache miss on R.acache), buf_ref ~5% (the CB list walks),
    nvn_front_handle 3.4%, nvn_front_list 2.8%, index_max 2.7%.
  - The calloc/free on the render thread is inside Qualcomm's vkCmdBeginRendering: an Adreno cost, not Mali's.
- **Kept:**
  - `cond_outstanding`: OP_RENDER_ENABLE's unconditional cb_sync became a sync only when a queued report or a CB in state 1 or
    2 overlaps the counts. The narrow check gave 46.4-48.2 fps against 38.3-45.1, render thread 14.9 vs 18.1 ms, GPU wait
    0.5 vs 2.2. About 320 compares a frame no longer wait, and the screenshots match.
  - The normal path: 57.3-58.8 fps (at the cap). `SMO_VK_COND_NARROW=0` is the old behaviour.
  - buf_ref's bounds test `g_cb_lo/hi`.
  - The wait tally `SMO_VK_WAITWHO` is now on by default, and SyncWait queue statistics are logged.
- **Tried, no gain, off by default:**
  - the ring_put front cache (`SMO_VK_RFRONT=1`);
  - one index binding per command buffer with firstIndex (`SMO_VK_IBONCE=1`);
  - the async present (no gain in the same view: the game's wait moves to the window texture's wait; SyncWait queues only
    0.04-0.08 ms, and 83-89% of its fences are done when it runs).
- **Caution for A/B runs:** the default SMO_SEQ lands in either the 847-draw or the 1206-draw view. Use the forward-view SEQ in
  both runs. The Mali-path profile (boost off) also runs with low, moving CPU clocks.

**Tester on 0.3.8** (log odyssey-logs-20261113-1006; his words: "I didn't see anything strange in the fog", "performance, well, it's the
same"):
- **Run 1 (`log.prev`):** light frames only (3D 1280x720, GPU 3.5-10 ms, 60 fps), not the 0.3.6 heavy area (1150-1343 draws).
- **Run 2 (Cap Kingdom):** 52-58 fps, 315-331 draws, 56 passes.
  - GPU busy 14-16 ms: GPU-bound.
  - Render thread 5-6.6 ms; its GPU wait 0.3 ms. This is mostly the light draw load.
  - The narrow conditional compare applied only ~2 times a frame (one stretch had 364).
  - Fog reads work on his phone (256x128 R32F copied back).
  - The wait tally shows only small waits: vk_idle, cmd_begin, occ_done, nvn_vk_idle, at most 0.17 ms a frame.
- **So 0.3.8's change is not tested on his phone yet.** It needs a run in the 0.3.6 heavy area. Cap's GPU fixed cost (~15 ms with 56
  passes) is the same picture as before.

**More 0.3.8 measurements (2026-11-14):**
- **Ring copies by kind** (nvn_vk_ringkind, S24 Mali path, Cascade forward view): constant and storage buffers 2.37 MB a frame,
  vertex streams 9.67 MB. The draws read 74% of the vertex ranges copied, so trimming vertex copies to the range a draw reads saves
  at most ~2.5 MB a frame, less once the ring cache keys change. Modest; not built.
- **Cap, handheld, S24** (SMO_VK_PASSSHOT=4500): 87 passes. The 3D scene is 320x360 (as on the tester's phone). ~70 passes draw one
  thing: the fog wave solver (16 x 128x128 RGBA16F) and its mip chain 64 -> 2, 256x256 / 128x128 effect passes, two shadow
  blurs, and the bloom pyramid 80x90 -> 5x5 and back.
- **The tester's 848x480 G-buffer** (3.3 ms) is in one sample only: frame 3000, on arrival in Cap. That frame renders the whole
  scene twice (848x480, then 320x360), so it is a transition, not a steady cost.

**Frame generation shake, 0.3.11 (2026-11-14):** the user saw the world and Mario shake with frame generation on (HUD steady).
Two causes found and fixed, both checked against the real frames (details in docs/FRAME_GENERATION_PLAN.md):
- The depth is -1..1 (z = 2d - 1), not 0..1: the vectors were about twice the real motion. Fitted per screen strip, k = 1.00 now.
- The made frame goes one logic step ahead (1 / the frame's steps), not half a frame: ~4.5% of frames at the FPS limit 30 have
  1 or 3 steps.
- The made frames now sit 0.43-0.50 of the way to the next real frame (ideal 0.5), ~0.33 after 3-step frames.
- Step 4, first version (same build): Mario's own motion from his position (PlayerActorHakoniwa::movement wrap, a capsule in
  the motion shader). The user played both versions twice: with it "def looks better"; without it Mario doubles (afterimage).
  The one-frame broken strip he uncovers (seen in crops) the user does not notice: its fix is written down in the plan, not urgent.
- Check scripts (copied out of the scratchpad): tools/android/fg_checks/ (mvfit.py: vector scale against the real frames from
  the raw vectors; fgeven.py: where made frames sit between real ones; mariochk.py: Mario in real and made frames;
  p48_runner.sh and graphics_c30.ini: the runner and the test ini; their paths point at the old scratchpad, adjust them).
- **Next (agreed with the user 2026-11-14, for the next session):** 1. measure the GPU time of the motion pass and the warp on
  the S24's Mali path, then on the tester's phone; 2. step 5: a frame generation on/off option in the in-game menu, so the
  tester can try it. Later: the screen edge stretch, the strip behind Mario, interpolation, the multiplier.
- Seen once (user, a test boot in Cascade): post effects looked missing, like the old respawn bug; the next run looked normal.
  Screenshots from 2026-11-12 (before frame generation) look the same, so not frame generation. Watch the shots for it.

**0.3.12 (2026-11-15): frame generation's cost measured, and a setting.**
- GPU timestamps (nvn_vk_fgtime, every 300 made frames): S24 ~4.6 ms a real + made pair (motion 0.65, HUD copy 0.27, warp 1.0,
  the made frame's present pass 2.6). GPU busy 14.2 -> 18.2 ms. Same on the Mali path. Details in docs/FRAME_GENERATION_PLAN.md.
- graphics.ini frame_generation (app Graphics page and in-game menu, "Frame generation (test)", live). While on: FIFO (swapchain
  made again on a change), the game paced at 30 (display.c), steady speed, window 60 Hz. Tested through graphics.ini with the FPS
  limit at 60: 2.03 steps a frame, full speed. NOT tested: the live switch in the in-game menu.
- Next: the tester's phone (the nvn_vk_fgtime line gives the Mali cost); make the made frame's present pass cheaper (the largest
  part); test the live menu switch.

**Tester's 5-minute idle log (0.3.8, Mali-G615, handheld, steady speed, no fps cap; tester's logs/odyssey-logs-20261115-0945.zip):**
- The 0.3.8 conditional-render change works: 45-74 conditional renders a frame compared without waiting; the render thread's GPU
  wait at the heavy spot fell from 3.8 ms (0.3.6) to 1.3 ms. Left: occ_done (occlusion results the game reads, ~0.75 ms a frame)
  and vk_idle / nvn_vk_idle (queue finish), resolved with build/android/symbols/libsmo-0.3.8.so (no line numbers in it).
- Steady state: ~36 ms a frame (~28 fps), GPU busy 12.6 ms, render thread 12.5 ms, game logic 33-35 ms of which ~25 ms WAITING
  ("the parts wait for each other, largest the GPU 34%"). SyncWait (game fences) is not it: 0.21 ms a call, 94% already done.
  Logic work (~8) + render thread (12.5) + GPU (12.6) ~ the frame: the three run one after another instead of overlapping.
  Logic steps per frame spread over 1/2/3 almost evenly (uneven pacing).
- NEXT (speed, separate from frame generation): find what the game's logic waits ~25 ms on (a guest event / condition variable /
  message queue: probably the game's own draw thread, whose time the perf line does not show). If the parts overlapped, frames
  could approach ~13-20 ms on his phone. A per-thread timing build for the tester would answer it.

**Per-object motion (frame generation step 4b), work in progress 2026-11-15 (paused for a PC restart):**
- Built: runtime/nvn_vk_objmo.c (included by nvn_vk_motion.c). G-buffer draws noted (mo_capture in draw_prepare, mo_note_draw
  after the draw), uniform copies in a 4-part keep buffer, matched with the last frame, moving ones drawn again into the motion
  image with a motion vertex stage (the translator's GLSL run twice: previous buffers, then current; mo_vs_text). Depth check in
  the fragment stage (SMO_VK_MO_TOL, default 3e-5). Cap 24 draws (SMO_VK_MO_CAP), skinned (integer inputs) and GPU-written-vertex
  draws first, nearest to the camera first (first own 3x4 matrix), rigid within a vertex budget (SMO_VK_MO_RIGID 12000). Copies of
  GPU-written vertex streams at the G-buffer pass's end (mo_pass_ended) and previous-vertex inputs (g_mo_ploc). Occlusion
  queries per replay; keys that wrote nothing twice are left out but retried every 32 frames (mo_cov_dead).
- Verified: on still objects the replayed motion equals the camera motion (median ratio 1.00); with Mario running the made frame
  moves him (the capsule did not). Cost on the S24 ~1.4-2.0 ms for 24 draws (~30 us a draw, not per vertex).
- The user's free play (before the latest fixes): Mario and a chain chomp still at 30, Mario doubled.
- Findings: Mario is skinned in his vertex stages (bones in c6, integer inputs 0x800), NOT by compute (the GPU-written-vertex
  path finds only small streams, 10-16 KB a frame). He is drawn TWICE in the G-buffer pass (programs 264xx and 265xx). His
  pieces near ~90 units, his cap probably the rigid 2736/3117-vertex draws with a model matrix in c7.
- Open problem: Mario's main body mesh (10440 vertices, both sets) does not pass the depth check, so it gets no motion (its key is
  then marked dead); smaller pieces of one set pass exactly (|depth difference| < 4e-6), the other set's pixels are ~2.5e-4..6e-4
  deeper than the scene depth (~25-55 units). Next: find what the body's depth differs by (a per-draw measurement with no other
  draw painting over it: SMO_VK_MO_MARK=1 + SMO_VK_MO_TOL=1 let a later terrain draw cover everything; replay only the
  near-Mario draws for that test), e.g. a depth bias of the game's draw (R.polyoff) not applied in the replay.
- Test switches: SMO_VK_FG_OBJ=0 (off: Mario's capsule), SMO_VK_MO_MEASURE, SMO_VK_MO_MARK (marker = 1000 + draw number, y = depth
  difference x 1e5), SMO_VK_MO_TOL, SMO_VK_MO_CAP, SMO_VK_MO_ALL, SMO_VK_MO_MAX, SMO_VK_MO_NOPIX, SMO_VK_MOTION_SHOT also saves
  motion_<f>_cam (before the replay). Mario-running script: SEQ "... @draws:1000-1400/20 60:u/3000"; the stage's frames start at
  ~895, he runs freely from ~950 to ~1300 (frames 980-1160 are good shots); @goto does not work (no player tracking table).
- Uncommitted, internal build still numbered 0.3.12 (installed on the S24). Not handed out.

**Per-object motion, 2026-11-16:** the depth check now reads a snapshot of the depth at the G-buffer pass end (mo_dsnap: later passes had written over Mario's body), Mario's duplicate second set of draws is skipped (same count and own data), the replay counts its pixels (occlusion queries) and leaves out draws that write none. Mario's pixels covered: 236/507, 182/328, 412/791 at three running frames (was 6/835 at worst). The user compared frame generation on and off in free play at 30 real frames (S24): on looks better. The live menu switch works (8 switches in one run; test runner p49.sh swaps the test ini in as graphics.ini, since the menu writes that file).

**Logs for the tester (0.3.13, 2026-11-16):** nn_waitmap on by default (every 30 s; LOGIC = waits inside a logic step, with three return addresses: map main+offset with build/functions/main_functions.tsv), waits that end while spinning recorded too, nn_threads (CPU use per thread over 30 s and its last processor; top clocks once), smo_framerate logic waiting by kind, nvn_vk_fgtime real-frame pacing.
- S24 findings: the logic waits on events signalled by the game's helper threads: view update core 1 / core 2 (al::waitUpdateCalcView from al::Scene::movement, ~150 + 25 ms/s), clipping (al::ClippingActorHolder::update, ~40 ms/s), draw buffer update (~7 ms/s). The helpers are only 2-7% busy; "view update core 1" last ran on cpu3 with the Presentation Thread (40%) and the SoundThread (11-14%): it probably waits for its turn on a shared processor. guest-main 97% (much of it spinning). NEXT (speed): test helper placement or priority (console core model in os_threads.c console_cores / apply_sched) once the tester's log shows the same on Mali.

**0.3.14 (2026-11-16), after the tester's 0.3.13 log:** (1) FG collapse on Mali (47 -> 5-10 fps, render thread ~170 ms) was the per-object motion hashing the big shared buffers per draw: mo_slot dedup table + 8-byte hash, S24 0.6 ms a frame. (2) Shaking (user saw it on a test build): a window-view cache keyed by (VTex*, VkImage) went stale; now a VView (vtype -7) in vt->views, the HUD texture's view likewise (vtype -8). (3) Handheld HUD: the game draws into the top-left 1280x720 of the 1600x900 window texture (present crop) and the HUD into a 1280x720 texture (SMO_VK_PASSTIME: pass 0x3d 1280x720 32 draws, then 1 draw into the window). fg_make now takes the crop (vk_present2 computes it as the present does), the warp is dispatched over the crop, the HUD test is fg_hud_shape (same shape, at least half size). Checked with SMO_VK_MOTION_SHOT in handheld (hp4) and docked (dk1): HUD in place and sharp. The user confirmed on the S24: no shaking, animations smooth (after the forward pass). Test note: p49 INI must be a C:/ path (a /c/ path is not found by adb with MSYS_NO_PATHCONV).

**0.3.14, forward pass (2026-11-16):** the user saw Mario/enemy animations at 30 with FG while the camera was smooth. Cause (old since 0.3.13): the warp looked up motion AT the made pixel (2 fixed-point steps from x0 = q), which for a moving part converges to the background where the part goes and to the part itself where it was -> parts stayed put (saved frames: made = previous real pose). Not the mo_slot cache: write tracking via host_wrote (mo_host_wrote) found 0 buffers rewritten mid-frame (kept as a guard). Fix: the motion pass also writes view distance (R32F, from the G-buffer depth snapshot when fresh); fs_run pushes each motion pixel to x - t m with one imageAtomicMin of (log2 dist 12 bits | vector 10+10 bits at 1/8 px, 1023,1023 = too long), then fills gaps (farthest landed neighbour, else one step back) into a finished RG16F image the warp texelFetches. S24: forward pass ~1.3 ms + 0.2 ms in the motion pass per pair (A/B mu5 vs mo5 with SMO_VK_FG_SCATTER=0). Saved frames show in-between poses; fast gloves smear a little. NEXT if Mali cost is too high: run the forward pass at half resolution.

**Tester 0.3.14 log (2026-11-16):** feels like 60, latency unchanged, rare Mario doubling (also without FG; user: ignore for now). HUD copy 0.3 ms in handheld, FG GPU ~4 ms a pair, BUT real frames only 50-75% on time (38-51 ms), render thread 25-35 ms the limit, 3D at 640x360, per-object CPU 5-14 ms (S24 0.6). Cause (likely): keep buffer in the first host-visible coherent type = uncached on Arm, CPU reads (hash, mo_dist, matrices) at uncached speed. 0.3.15: mo_keep_new asks for HOST_CACHED, hash from guest memory. S24 OK (type 6, cached). Mali gain to be measured.

**Tester 0.3.15 log (2026-11-17):** keep buffer type 0 flags 0x7 (no coherent+cached type on his Mali), per-object CPU ~4 ms (from 5-14), FG frames 46-50 ms, 30-50% on time, frame = logic + render + GPU in series (FG off same scene: 31 ms, render 10.5, GPU 12.5). nvn_vk_rt present 0.8 -> 9.5 ms with FG (double counted the made present). 0.3.16: g_mo_shadow (malloc CPU copy for every CPU read), nvn_vk_present line (g_ptm in vk_present2: motion+objects, barrier, acquire, present call, made frame built, its present; each with waiting-for-GPU from g_tm[0]). S24: made frame built 12.6 ms of which 11.9 waiting for the GPU = 30 fps pacing (99.7% on time). NEXT: read the tester's nvn_vk_present line; if the made-frame wait is large on Mali too, it is the FIFO/display queue, not work.

**0.3.17 (2026-11-17):** user saw afterimages with the camera close to Mario/props (maybe old). Packed forward vector was +-64 px at 1/8; beyond -> fallback to the old look-up. Now sqrt scale (x 22.58, +-512 px) in fs_init's shader. S24 Mario run and pan OK. To check in play.

**0.3.18 (2026-11-17):** close-camera orbit ghost found with adb screenrecord (user rotates, rec1/rec2 in scratchpad; ffmpeg extracts frames): made frames only; disocclusion gap filled along the bg vector = into Mario. Fill pass: if the fetch lands on dist < 0.85 x the gap's own -> f = 0. Then a faint outline = AA-contaminated bg pixels at the edge moved with bg: final vectors whose source has a nearer pixel within 2 px get pushed 2 px away. Then a seam in Mario's shape against textured bg (gap filled unmoved): last real frame kept (g_fgprev_*: window texture or HUD pre snapshot, copied after the warp), flagged gaps (ffin.z) fetched from it at x + (t+1) f; flagged only if not sky (dist < 1e30) and the last frame's dist (aux[4], copied in fs_run) at that spot is as far. User verdict: very minimal, good enough. Recording is the best test method for visual FG artifacts (user plays, PC records).

**0.3.19 (2026-11-17):** (1) HAT: mo selection merged skinned+rigid nearest-first, cap 32, rigid budget 40000 (nvn_vk_objmo.c); the cap meshes (#26298 2736 + 3117 v, c7) now drawn again; user confirmed. (2) MIST TRIANGLE: SMO_VK_DIAG captures (user stands at the spot; rounds in files/diag, log per draw) -> appears in pass 33 = the half-size mist composite (vs/fs #23331, one draw, fullscreen tri, 3 inputs: 800x450 R32F distance, 400x225 R32F, 400x225 0x29 mist); not in the mist RGBA (new _a.bmp) nor the depth inputs. Cause per nxnative issue 558: 9 vertices drawn from a 36-byte stream, console reads zeros past the bound size; our VTXGROW copied guest memory after it. Fix: vtx_zero_tail (bound bytes + zeros), SMO_VK_VTXGROW=1 default, 2 old. User: triangle gone. Ruled out: restart (0xffffffff with 16-bit), depth clamp, scale, cond renders (SMO_VK_COND_ALL=1), no-depth clamp (reverted). (3) MIST DISAPPEARING off-centre: still open, NOT frame gen/cond/clamp/scale. Lead to check: at scale 0.9 pass 32 (mist into 400x225 0x29) has a 360x203 depth target (scaled) vs unscaled colour: render area 360x203, mist missing in the right/bottom strip (a real bug, but the flicker stayed at 1.0). Test switches added: SMO_VK_DCLAMP=0, SMO_VK_COND_ALL=1, nvn_vk_restart log.
- ARM/Vulkan facts for users: main APK -march=armv8.2-a; -armv8.0 APK for v8.0/v8.1 (the launcher only warns v8.0); Vulkan 1.3 required (dynamicRendering + synchronization2), Turnip possible on Adreno 6xx/7xx; game 1.0.0 only.
- Coins (user): may look 30 fps with FG, later (probably rigid objects left out like the hat was).

**0.3.20 (2026-11-17):** mo cap 64, rigid budget 80000 (SMO_VK_MO_ALL=1 measured: ~100 moving draws, GPU 2.35 vs 1.83 ms, render thread 0.72 vs 0.56 ms; a third write no pixels). Issue "mist strip at scale < 1" dropped: the diag log prints colour targets at game size (t->b.width when rs) and depth at image size; no MIXED pass logged. Mist disappearing off-centre: also ruled out occlusion precision (PRECISE used when supported) and same-pass depth feedback (mist draws sample the separate 400x225 R32F copy). NEXT: diag capture with the mist vanished vs visible, compare pass 32 draw counts (fewer draws = the game culled them on the CPU; same draws, no pixels = GPU side). nxnative docs/issues: nothing SMO-specific about it.
- Reports to follow up (2026-11-17): issue 10 (FG on Mali) also being tested by a user with an Infinix Hot 60 Pro; a user saw fps drop to single digits, suspects switching drivers a few times (logs coming). Tester: send odyssey-0.3.19.apk (Mali FG CPU fix + nvn_vk_present line).
- User plan (2026-11-17): finish the gameplay issues, then the planned work (FG settings menu with a time budget, helper threads), then a release.

**Mist disappearing (Cascade), 2026-11-17 captures dg3/dg4 (scratchpad mist_rounds.py, mist_tri.py):** the game submits the same 39-40 mist particle draws (vs #3308-3349, into the 400x225 NVN 0x29 target) in every captured frame, vanished angle or not: not game-side culling. In all 6 kept rounds of the vanished angle the end-of-frame images (E, from the NORMAL frame before the round) have no mist and the mist target is empty, while the per-pass images (P, the frame where every pass ends and the GPU is waited for, plus readbacks) have the mist and composite it: a GPU synchronization bug, particle inputs not ready in normal frames. Candidates: the particles' GPU-computed data (compute dispatch + copy-back to guest memory), a GPU-written 4x4 NVN 0x25 texture (handle 110200107) read in the VERTEX stage of the mist draws and of the composite (no writer seen in the frame log). Per-draw pipeline statistics (SMO_VK_DQ_FMT=0x29): half of the emitters have all triangles clipped away. TEST NEXT: SMO_VK_WAIT_PASS=1 (0.3.20 dev build: cmd_wait_all + cb_land after every pass) at the vanished angle; if the mist comes back, narrow it down (compute copy-backs vs barriers).
**Single-digit fps report (Xiaomi 25097RP43G, SM8735 / Adreno 825, Android 16, Turnip Gen8, public 0.3):** the collapse starts exactly when PlayGuideMovieCapThrow.mp4 (the throw-Cappy tutorial clip, 1.8 s, loops until Cappy is thrown) starts, and lasts while it loops; logic steps went 3 -> 30-57 ms and steady speed then ran 2.7 steps a frame. The other session (no video) ran at 60. Suspect android_movie.c: pump_track decodes synchronously on the calling guest thread under the extractor lock, waiting on AMediaCodec in 5 ms steps. Asked him whether it recovered after throwing Cappy; plan an Action Guide video test on the S24 with 0.3.20 logs (nn_threads, waitmap). Likely fix: decode on a background thread.

**Mist: parked (2026-11-17).** CPU-side wait before the mist pass (SMO_VK_WAIT_FMT=0x29 mode 1, or WAIT_PASS=1) fixes it; a full GPU barrier (mode 4), the fence copy-back, the pending-read wait and the attachment barrier do not. So it is CPU-side work done at the wait (likely GPU-reported values written into guest memory at slot completion and read by a later mist draw). The wait costs fps, the user chose to leave the issue. Next time: find which value (cq_resolve / report / counter writes in slot_wait) the mist reads.

**Heat and battery, CPU boost measured (2026-11-17, S24, cooler off, Cascade gameplay, 60 fps mode, scale 0.90, each run from cool, ~2.5 min of play; scratchpad boostab.sh):**
- boost on (Always, the Android default): 59.7-60.0 fps, 1% low 37-50; render and guest-main 98% CPU; AP 35.2 -> 47.1 C, SKIN 34.3 -> 39.6; headroom 0.82-0.83 at the end.
- boost auto: 58.6-60.0 fps, 1% low 32-47; it turned itself on after loads and dips to ~57 and stayed on most of the run (threads 97%); AP 35.4 -> 45.8, SKIN 34.2 -> 38.9; headroom 0.80-0.81.
- boost off: 50.6-52.9 fps, 1% low 29-40; render 61%, guest-main 59%; mid cores 0.84-1.6 GHz; AP 35.3 -> 43.0, SKIN 34.3 -> 37.7; headroom 0.76.
- So the spin buys ~8 fps for ~4 C of AP in 2.5 min. The ADPF session reports the present interval as work (equal to the target at 60, so no signal when on time) and had no effect on Samsung clocks earlier (SMO_PERF_BOOST test); uclamp is refused to apps.
- Other ports: LAS Recomp (Android) has no heat code on Android; it holds the game's own 30 fps mode (full speed) while frames do not fit 60 and lowers resolution by late frames; its heat rules (resolution ceilings per heat) read only the iOS thermal state. Google ADPF guidance: lower the workload by thermal headroom before 1.0. Switch emulators: nothing useful (Eden even has "force maximum clocks").

**Heat and battery, what does NOT work without losing fps (2026-11-17, S24, cooler off, same Cascade run, scratchpad spinab.sh):** the user does not want fps or resolution forced down (no heat rules that step down, no battery-saver preset), only savings that cost nothing. Boost Always, per spin variant (fps, AP rise, headroom, render / logic work per frame-step):
- bare loop (default): 59.6-60, +11.9 C, 0.82, 8.9 / 4.0 ms. ISB (SMO_SPIN_PAUSE=1): first run +10.0 C, repeat +12.3 C, same fps and work: noise, no gain. WFE (=2, evtstrm on, SEV from the setters): 57-59.6 fps, clocks jumpy (WALT counts cycles, a clock-gated core looks idle): worse.
- SMO_SPIN_HOT_MASK (1 render, 2 presenting, 4 window-sync waiter; default 7): 5 and 3 same heat (+11.8 / +11.9); 1 (render only) +9.8 C but game logic 5.8 vs 4.0 ms per step and render 9.4 vs 8.9: a cost hidden by the 60 cap, phones under 60 would lose fps.
- Conclusion: the heat is the high cluster clocks the spin buys, the same clocks that make the fps; nothing to save in the spin itself. Boost stays a player choice (launcher text now says the cost). The display ran at 60 Hz in all gameplay runs (no gain there). Audio: AAudio low latency, 96-frame bursts (500 callbacks/s), left as is (a larger burst adds sound latency).
- Done: motion sensors off with motion = off, 100 Hz otherwise (android_main.c). Real savings now need less CPU work per frame (render thread draw preparation, game logic), which also raises fps.

**Render thread CPU work (2026-11-17, S24 system driver, Cascade gameplay, boost on, simpleperf pr1 / pr2 in the scratchpad):**
- The render thread is ~56% spin (vdso clock_gettime, boost); the real work (~8 ms a frame) is: Adreno driver ~2.5 ms, libc ~2.4 ms, libsmo ~2.1 ms. Inclusive: draw_prepare 23% of all samples (pass_begin 9% of it, vkCmdBeginRendering 5.1% with the driver's calloc/memset; buf_ref/ring_put 4.7%; vkCmdPushDescriptorSetWithTemplate 2.8%), vkResetCommandPool 5.7% (~0.9 ms), present 3.3%, submits 2.2%. libsmo self: replay 28%, draw_prepare 13%, ring_put 11%, vk_draw_elements 6%.
- Per frame in gameplay: 6.8 command buffers (2.7 from in-pass splits, 1.3 submits between passes), ~200 descriptor sets, 7.1 MB streamed, 5,100 ring_put calls (63% cached), one new generation per frame (batch).
- TRIED, no gain, off: pool resets on a helper thread (SMO_VK_BG_RESET=1; rs_* in nvn_vk_draw.c). Six runs: render 8.33/8.17 off vs 8.48/8.08 on, Mali path 8.08 vs 8.17; profile with it on: the reset gone, but kgsl_sharedmem_alloc during recording (~2%), vkCmdBeginRendering 7.8%, slot_wait 4.7% (waiting for the helper).
- MEASURED, idea dropped: copies kept across frames for pools the CPU cannot write (CPU_NO_ACCESS) or must flush (CPU_CACHED): only 0.06 MB of 6.94 MB a frame; 6.88 MB are in CPU_UNCACHED pools, which must be copied every frame. Removed the measurement again.
- The main thread (guest-main): 66% spin, 30% the game's own recompiled code, libc 3%: no quick target there.
- Next candidates (all drivers): fewer render passes begun per frame (each vkCmdBeginRendering allocates in the driver; in-pass splits add 2.7 a frame: measure whether a split only when the GPU is idle could be rarer without the GPU going idle), replay's per-record cost, ring_put lookups. Lifted-code overhead in the game logic is the larger long-term target.

**0.3.21 release work (2026-11-18, S24 with a cooler):**
- Frame generation costs (fgA/fgB, user FG ini, normal and Mali paths alike): motion 0.47, objects 2.2, HUD copy 0.23, warp 2.35, made present pass 2.76 ms: 8.0 ms a pair; real frames 99-100% on time.
- New: frame_generation = auto (fg_auto_tick in nvn_vk_motion.c, gfx_framegen_auto_set), frame_generation_quality = fast (gfx_framegen_fast: fs_run and mo_obj_on off), per-object time budget (mo_budget, g_mo_cap_dyn). Tests ftA/ftS/ftS2/ftF/ftB in the scratchpad (ft.sh).
- RAM: GL mtrack 3.05 GB / dumpsys gpu 2.83 GB against our allocator's 1.47-1.57 GB. Descriptor pools ÷8 (SMO_VK_DPOOL_DIV, default 8): 2312 -> 1744 MB in the same probe order (memprobe.sh), 1487 MB in fincheck.sh; SMO_VK_CB_RELEASE every reset: 2464 MB and fps down. Native heap 3.3 GB malloc with 2.8 GB swapped (the guest memory: zero pages mostly; not touched). Still unknown: the rest of the driver's share (pipelines 4367, shader modules 15983 kept).
- Ideas not shipped: ring 256 MB host-visible (texture uploads go through it too, a smaller one risks hitches at loads).

**Exynos 2400 / Xclipse 940 report (2026-11-18, tester's logs odyssey-logs-20261118-1743.zip, Galaxy S24 SM-S721B, Android 17, public 0.3, boost off, shader_wait skip):**
- First gameplay: the container scan handed 10,475 variations (100 MB) to the 8 workers, 5,814 stages made; frames 85-170 ms while the game's parts took ~8 ms (CPU taken by the workers); 59-60 fps after ~2 min. One-time (the session before ended with a 14 KB cache).
- Three freezes of 5+ s at level loads (watchdog reports at frames 3308, 3458, 5715): WorldResourceLoader in nvnTexturePoolRegisterTexture, render thread at place 0, 2,600 background shader jobs queued. The driver reports textureCompressionBC off, so EVERY BC texture is re-encoded to ETC2 on the CPU (tex_etc.c). Likely cause, not measured (no per-texture timing in his log).
- CPU clocks pinned at 1440 MHz on all clusters for minutes (Samsung power mode / game optimizer?). Formats 0x34 (depth) and 0x13 have no linear filtering there: sampled nearest.
**Xiaomi 25097RP43G / SM8735, Turnip Gen8 V39, 0.4 (log odyssey-logs-20261118-2210.zip):** Cap Kingdom 58-60 fps; Cascade average 50-58 with 1% lows 2-12 and 171 hitches in 16 min, 38 of 54 s of them pipeline holds on the render thread (~25 ms a pipeline on Turnip). His shader cache was EMPTY ("no ../cache/vk/pipes.bin: a first run", pipeline cache 0 KB, first-start preparation 195 s) while his 0.3 logs the day before had 15,128 KB and 1,008 pipelines with the same driver; the translation (first_prep_marker inputs) did not change in 0.4. So the cache folder was emptied (Android Clear cache, a cleaner app such as Xiaomi Security, low storage, or a reinstall). Our weak spot: the caches live in ../cache (the app's external CACHE dir, which Android and cleaners may empty at any time).
- PLANNED (user: no fix before the problem is understood): ask him (1) did he clear cache / storage, use a cleaner, or reinstall; (2) is Cascade better the second time. Likely fix: move ../cache/vk (pipelines, pipes.bin, spv, first_prep marker) under files/ (with a one-time move of an existing cache) so Clear cache and cleaners leave it alone.
- PLANNED, Fast quality smoother (user idea): Fast already computes Mario's capsule motion (nvn_vk_motion.c step 4, used while per-object motion is off) but skips the forward pass, so it does nothing. Options to measure: (1) Fast + forward pass (Mario moves smoothly as a whole, limbs at 30; cost forward pass + last-frame copy), (2) forward pass at half resolution (Fast and High on weak GPUs), (3) a few nearest objects redrawn in Fast (4-8). Measure on the S24 and ideally the G99 tester.
- PLANNED after the 0.4 release, with the native BC build: a test of building the shaders at first start behind the progress bar instead of during play. The Exynos log: first-start preparation on, but the game is not held for the container scan (SMO_VK_HOLD_SCAN=0), so 10,475 variations were built while he played (5-24 fps for ~2 min, the workers on every core). Compare SMO_VK_HOLD_SCAN=1 (time at the bar, fps after) on the S24 and, if possible, on a tester's Mali / Exynos. Also noted: shader_wait = skip only applies in and around fights (he still had 95 holds, 10 s in one window), so "Wait" stays the default.
- PLANNED after the 0.4 release (user: "keep it planned for later"): a test build that uses BC1-BC5 (and BC7 if offered) natively where vkGetPhysicalDeviceFormatProperties lists them with SAMPLED_IMAGE_FILTER_LINEAR, even with the feature bit off, and re-encodes only the rest. First read his 0.4 log: the new line "BC formats the driver still offers one by one". Possibly also a per-texture re-encode time in the log.

**2026-11-19: 0.4.2 (code 54), built and installed on the S24, NOT handed out yet (no message sent to the tester yet; the ARMv8.0 APK of the latest state is not built).**
- The Xiaomi tester's 0.4.1 log (odyssey-logs-20261119-1052.zip; new game from the start to the dinosaur in Cascade, boost off, CPU cores Free): CRASH in al::ExecutorActorCalcView::execute -> al::calcViewModelSystem on a null actor (view update helpers), host SIGSEGV fault 0x88. That is the failure the console core model exists for (os_threads.c comment: free scheduling broke the game before). LESSON: never turn the console core model off as a fix. 0.4.2 removes Free (a graphics.ini that says free runs fixed and logs it).
- His "memory leak" is not a leak: textures are freed (14,891 / 756 MB by the end), live textures in Cascade 4,500-5,200 (S24: 4,400), our Vulkan memory stays at its high-water mark (blocks are reused, not returned). But his phone has 7.5 GB: free RAM fell from 3.1 GB to ~500 MB, his process kept only 73 MB private resident (the rest swapped), and helper wake-ups grew with heat and memory pressure EVEN UNDER FREE (200 us at 900 MB free and no heat, 4 ms at moderate, 11 ms at severe just before the crash). So the slow Sand Kingdom is the phone running out of RAM and heat, not the pinning.
- S24 Fixed vs Free (0.4.1): logic waiting 6.5-8.1 vs 1.5-1.7 ms a step, wake-ups 780 vs 66 us, render thread 7.4-9.1 vs 10.7-11.1 ms. Interesting, but Free crashes the game, so not usable as is.
- BIG MEMORY WIN (0.4.2, on by default): shader modules released as soon as the stage is made (SMO_VK_MODREL default 2). Phones without pipeline libraries (S24 Qualcomm, Turnip, most) kept every module before (16,082 / 24,853). S24 Cascade: total PSS 5.90 -> 4.64 GB (-1.26 GB), native heap 1.65 GB + 259 MB swapped -> 0.76 GB, fps same, hitches 5 -> 2, start-up pipelines 3.0 -> 2.2 s, only 52 modules made again.
- Stream ring 128 MB on phones under 10 GB of RAM (largest single range measured 11 MB; SMO_VK_RING_MB). Ring line logs the largest range.
- BUG FOUND AND FIXED (every phone): compute write masks were in the stream ring and could be overwritten by a ring wrap before cb_land read them -> wrong compute results copied over game memory. The first S24 run with the 128 MB ring crashed in the game's audio (al::AcLSoundHandle::setMainSend on a null handle, never seen in dozens of runs); 6 repeats without the fix did not crash (rare). Now g_wm_buf (2048 x 16 bytes, one slot per g_cdisp entry). With the fix + 128 MB ring: 3 runs (Cascade x2, Mali path + FG), no crash, copy-back as before, picture OK.
- Pipeline cache also saved in loading screens on every phone (m4_quiet_save; 0.4.1). The tester's empty caches came from reinstalls / cleared storage: the cache is in files/cache (working dir files/bin), which Android's Clear cache does NOT touch.
- FIRST-START TEST (user's idea: build shaders behind the progress bar before play), test switch SMO_VK_FIRST_PREP_SCAN=1 (nvn_vk_m4.c fp_scan_on / nvn_vk_fp_scan_hold; nvn_gl.c queue_present waits during the scan half). Runs with separate empty caches (SMO_VK_CACHE=../cache/vk_fs_x, deleted afterwards): today's default (fsA): bar 151 s (game held 123 s), then mostly 60 fps but 10 hitches = 13.1 s in the first minutes. Scan behind the bar WITHOUT holding the game (fsB, an early version): bar 204 s, hitches 4.3 s but the game already played during the scan at 39-57 fps. fsC (meant to hold) ran the old library (the build had failed): 12.2 s of hitches, 12,449 variations. fsD (holding, fixed build) was INTERRUPTED at program 8,348 when the user had to stop. NEXT: rerun fsD (560 s, fresh SMO_VK_CACHE folder), compare bar time and the first minutes' hitches with fsA, then decide whether to make it the default (or a launcher choice). The current 0.4.2 build on the phone has the switch OFF by default.
- STILL TO DO for 0.4.2: the fsD result; changelog line for the first-start switch; ARMv8.0 APK of the final state; message for the Xiaomi tester (install 0.4.2, CPU cores gone, play as normal, send log: expect much less memory use).

**2026-11-26 (later): 0.5.8, touch as player 2 (GitHub issue 48). Built, installed on the S24, committed; not handed out.**
- How the game starts two-player mode (OdysseyDecomp: StageSceneStateStartSeparatePlay; title menu "Resume 2P"): its guide ("Play as a Team"), then ControllerAppletFunction::connectControllerSeparatePlay -> nn::hid ShowControllerSupport (players 2..2); our nnhid.c answers success when two npads are connected; then rs::changeSeparatePlayMode, player 2 = al::getPlayerControllerPort(1). Every npad is reported as FullKey (Pro Controller); fine for separate play (only Joy-Con single changes its button mapping).
- nnpad_android.c: controls.ini touch_player = 1 | 2 (g_cfg.touch_p2). touch_player(): the touch controls' player = 1 (index) when touch_p2 and a controller is on player 1, else 0; nnpad_present / nnpad_get merge touch, motion and the back gesture into that player; ctl_for skips player 2 for new controllers when touch_p2; touch_visible keeps the controls shown while they are player 2. Launcher Controls page and in-game menu: "Touch controls are: Player 1 / Player 2".
- Test (coop2, user's own save, controls.ini with touch_player = 2, controller faked with adb "input gamepad keyevent" = device -1): title -> Resume 2P -> guide -> A: "hid: ShowControllerSupport(players 2..2): 2 attached, first 0"; Cascade Kingdom with both players' prompts; touch Y + touch stick flew Cappy off Mario's head while the fake controller held Mario (screenshot coop2/t6.png). The pause menu could not be opened at the very start of a new game (the opening, before Cappy joins). Two real controllers not tested (adb gives one device id); their path is unchanged.
- Save and controls.ini restored, md5 checked. The cutscene audio delay of #35 (Snapdragon 680, 4 GB) left for later: likely that phone's video decoding.

**2026-11-26: 0.5.6 checked on the S24, issue 39 explained, 0.5.7 (texture cache, BC formats one by one). Built, installed on the S24, committed; not handed out.**
- 0.5.6 on the S24 (scripted new game to Cap Kingdom, user's graphics.ini, save backed up and restored, md5 same): normal play; no "format fix" lines on Adreno (R16G16_SNORM filters there).
- #39 DYNAMIC RESOLUTION THEORY DISPROVED: S24 with SMO_RES_FIX=640 (res640) holds the G-buffer pass's colour 0 at max 35-51 (resfull: 83), no fog. The first watch line's "(was 0)" only means the first read of that target (it holds last frame's light). The real jump on the Pixel is program #12028 (the light pass, fs ba7d183ccaf7e05a): max 3.789e4 -> 3.942e4, MEAN 44.8 -> 465.9 (blended, additive). Its GLSL (emulator dump program__12028_fragment_stage.glsl) ends with the game's own clamp min(x, 2048) / max(0, x) through Maxwell FMNMX semantics (a NaN gives the other operand: 2048), so NaN inside the lighting turns a pixel full white. NaN sources there: log2(r16) with r16 = 1 - saturate(...) (a Fresnel / specular power exp2(5 * log2(1 - x)); 1 - x = 0 facing the light), 1/x, inversesqrt. Maxwell: log2(0) = -inf, exp2(-inf) = 0; PowerVR (guard off, "keeps denormals 0, flushes 0") leaves it undefined. 0.5.6's guard for PowerVR (mx_lg2 / mx_rcp / mx_rsq) answers as Maxwell: likely the fix. Reply to the tester drafted (try the next build).
- NEW TESTER LOG (odyssey-logs-20261124-1750.zip, no bug report): Infinix X6885, Helio G99 (MT6789: 6 x A55 2.0 GHz + 2 x A76 2.2 GHz), Mali-G57 MC2 r54p1, app 0.5, docked, scale 0.60, FG on fast, steady speed: median 12.6 fps (earlier session 18.6). Findings: (1) "textures" up to 45 ms a frame of the render thread while areas load (BC -> ETC2 on the CPU); (2) LOGIC WAITS ~620 ms/s ON THE GAME'S OWN HELPERS (描画バッファ更新 282, ビュー更新(コア2) 138, クリッピング 101, ビュー更新(コア1) 48 ms/s): the console core model put cores 0 1 2 on cpu7 (A76), cpu5, cpu4 (A55s) because the fastest cpu (cpu6) is kept for the render thread; so on two-big-core chips console cores 1 and 2 (the view update, clipping and draw buffer update helpers) run on little cores. LEAD, NOT CHANGED (the console core model must stay; any new mapping needs a measurement on such a phone): e.g. a phone with < 4 fast cores could share or give up the render thread's own core. (3) Mostly GPU-bound otherwise (Mali timestamps likely undercount; FG's "made frame built ... waiting for the GPU" is backpressure, not a bug; 16 command buffers = ~2 frames at 7 submissions).
- 0.5.7 TEXTURE CACHE (nvn_gl.c etcc_*): re-encoded ETC2 / EAC levels kept in <files>/cache/etc (v1_<hash>_<fmt>_<w>x<h>x<layers>_<levels>.etc, hash of every input byte), writer thread, cap 1 GB, textures >= 4 KB; "tex_etc:" log line every 10 s. S24 with SMO_VK_NO_BC=1 SMO_VK_BC_EACH=0-equivalent path (etc1/etc2/etc3, Cap Kingdom start): first start 24.4 s of re-encoding (258 MB, 94 ms a MB on the S24's cores), 24 hitches, worst 1.5 s; second start 2.7 s re-encoding (small textures under the old 32 KB cut) + 273 MB read in 1.9 s, 12 hitches, worst 0.25 s; third (4 KB cut) 0.37 s + 297 MB in 2.3 s, 6 hitches (normal BC path: 7).
- 0.5.7 BC FORMATS ONE BY ONE (tex_vkfmt, bc_native_fmt): without textureCompressionBC, a BC format the driver offers (sampled + linear filter + transfer src/dst) is used as it is; only the others are converted. PowerVR D-Series and Xclipse offer BC1-5 and lack BC6H / BC7. S24 test "each" (SMO_VK_NO_BC=1 SMO_VK_SIM_FMT=143:x,144:x,145:x,146:x SMO_ETC_CACHE=0; new sim flag x = not sampled): 8 BC formats read natively, 8 levels re-encoded (0.0 MB), 6 hitches, picture right (shot compared with the normal run). SMO_VK_BC_EACH=0: everything converted as before. NOTE for tests: SMO_VK_NO_BC=1 alone now uses the S24's own BC formats again; add SMO_VK_BC_EACH=0 for the full CPU path.
- Issues: #48 (Moto Edge 50 Pro, thanks + asks for touch as player 2 beside a controller as player 1: the game's co-op Cappy; feature idea, reply drafted), #40 another user fine at 30 fps (Dimensity 7300). No new bug reports.
- Phone cleanup after the runs: files/functions, cache/etc (S24 does not use it), diag, shots removed; the user's save and graphics.ini restored (md5 checked).

**2026-11-22 (later): 0.5.6 work (not built for the phone yet; the phone is busy with another project: no adb on it).**
- #33 DONE: GameImporter checks the RomFS hash tree (IVFC) while it extracts: upper levels first (small), the RomFS's blocks from the same decrypted bytes as they are written, files now written in stored order. PC test with the user's NSP (javac + main()): 32 s for the game and the Help pages, output byte-identical to build/romfs and build/manual (diff -rq). One byte flipped at 3.0 GB / 5.5 GB of the file (test class overriding readFile): "the game dump is damaged: ObjectData/SandWorldUnderground000Ground000.szs does not match its checksum..." and StageData/ForestWorldHomeStageMap.szs. A failed import now deletes its half-written romfs/ and manual/.
- #39: the glow watch (nvn_vk_watch, after diag round 1) names the draw: the FIRST draw of the G-buffer pass, program #26390 (vs code hash 03337f876101c658, fs cef38963d5f362dc), writes max 3.84e4 at 47,166 into the empty 320x360 target, mean 71.9. The same program draws the big terrain (fragment ubo2 4.8 4.5 3 1). Its hashes do not match any build/shaders_all/*.mxs (the runtime hashes the control section's length, the dump the delimited code), so the GLSL comes from an emulator run with SMO_VK_GLSLDUMP. PowerVR ran with math guard and strict vertex math OFF (Arm only); 0.5.6 turns both on for vendor 0x1010 (in the stage hash already, so PowerVR translates again once).
- Discord report, "A34 5G" but the log says SM-M546B = Galaxy M54 (Exynos 1380, Mali-G68, driver r38p1, Android 15), app 0.5: 3D picture black, HUD and fps counter fine. Its driver cannot linear-filter R16G16_SNORM (NVN 0x13) and the game samples a 640x720 one with a linear filter (nvn_vk_fmt lines, programs #26128 / #26160). The Mali-G615 r44p1 phones that look right CAN filter it (their logs list only 0xa and 0x2e as limited); the Mali-G57 r54p1 log of 11-17 cannot either. Clip distances are missing on all of them, so not the cause. 0.5.6: fmt_sub's R16G16_SNORM -> R16G16_SFLOAT swap on by default where the filter is missing; the R32F / RGBA32F swaps (what hurt the G615 tester in 0.1.8) stay off (SMO_VK_FMT_SUB=1 all, =0 none).
- Mishap, fixed: build_apk.py --abi x86_64 --release names its output odyssey-<version>.apk and overwrote odyssey-0.5.5.apk; the test APK is now build/android/test-x86_64-release.apk and odyssey-0.5.5.apk was rebuilt from the 0.5.5 arm64 library and the committed Java (GameImporter taken from HEAD for the build). Use a debug x86_64 build for emulator tests where the installed app allows it.
- #39, the shader of the first draw (fragment SPIR-V 26ce4886aecf38bd, found as program__-161 in an emulator GLSL dump by its spv hash: fnv(0xcbf29ce484222325 ^ stage ^ 0x5601, GLSL)) is an alpha-tested depth draw: it writes vec4(0, 0, 0, 1) to colour 0 only, and the draw's colour write mask is 0. So the 3.84e4 texels were in the target BEFORE the pass's draws, and they are not a shader result. Render targets are cleared to 0 when made and carried only between images of the same shape, and colour attachments are LOADed. What the Pixel had and the S24 does not: the game's dynamic resolution at 640x360 (smo_quality: render size stepped 1280 -> 640 wide; the G-buffer targets 640x720 drawn in their 320x360 corner) and the textures made again at 1462 / 1669 with contents carried. Known: some passes break when the game draws below its targets' full size (2026-11-19, intro effects). The guard for PowerVR stays in 0.5.6 as a safety, the fog is NOT fixed.
- PHONE TEST PLAN when the user frees the phone (back up the save first): (1) S24, SMO_RES_FIX=640 and SMO_VK_DIAG=900 in env.txt, Cap Kingdom: does colour 0 of the G-buffer pass hold values in the thousands (nvn_vk_watch / diag lines) or a white fog? If so the bug is ours and reproducible; find which texels and where they come from (the right / bottom part of a 640x720 target outside the 320x360 corner, read by the light pass's filter or the bloom). (2) 0.5.6 install, normal play: check the import still works (re-import is not needed) and nothing changed on Adreno (fmt_sub does not apply there: R16G16_SNORM filters on Adreno).
- Emulator testing stopped at the user's request (memory: phone-over-emulator).
- Emulator (odyssey_test, -gpu host, -read-only) now holds the game files and the 0.5.5 x86_64 test build; adb always with ANDROID_SERIAL=emulator-5554 while the phone is plugged in.

**2026-11-22: GitHub issues 33, 39, 40 (0.5 logs, downloaded with the user's OK, text files and a few diag BMPs), 43, 46, 47.**
- #33 (Galaxy S26 / Exynos 2600, Xclipse 960, SPAL 25.3.21, Android 17): after the re-import the shaders reach 100% and the title works; Start on a new game aborts on FileLoadThread (nn::diag::AbortImpl via f_4000b59c30 < f_400074077c < f_4000b622bc < f_4000753438, the SZS decompressor). Last file: content:/ObjectData/KoopaTexture.szs, read 0x200000 at offset 0, first bytes 8f 45 51 60 44 d7 80 8f... Our copy (build/romfs, 16438206 bytes) starts 59 61 7a 30 ("Yaz0"). So his copy of that file is damaged (same in log.prev.txt); his first 0.4 crash was also a bad SZS header. GameImporter's AES-CTR read and readFile look right and other players pass this file at every new game, so a bad dump (or a bad write) is likely. IDEA (not done): GameImporter could check the RomFS against the section's IVFC hash tree while extracting and name a damaged dump at import time instead of a crash in game.
- #39 (Pixel 10 Pro XL / Tensor G5, PowerVR D-Series DXT-48-1536, driver 25.3@6908880, no denormals kept or flushed, no EDS3): Diagnostics round 1 at frame 1905, 88 passes, Cap Kingdom, game drawing at half width (320x360 / 800x900, its own dynamic resolution, 14-30 fps). The white fog = a few HDR texels at 3.6e4..3.8e4 in the 320x360 R11G11B10 (nvn 0x3f) colour 0 of the G-buffer pass (#41, 5 targets: 0x3f, 0x13, 0x5 R32F, 0x25, 0x13); mean 66 with only 1.9% over 1. They are there right after pass #41 in P (per pass) as in E (frame end), so not ordering: a fragment shader value on this GPU. The light pass (#53) lifts the mean to 483, the bloom chain (#37..#50) spreads them; final 1600x900 picture (r001_E_057) washed out in white from the top left. R32F colour 2 holds 0..0.0022 plus 56485 infinity texels (likely sky; no reference yet). NEXT: the draws of pass #41 are logged (programs #26390, #26293, #26387...); find which program writes the huge colour 0 (per-draw capture or a clamp test), compare the same scene on the S24.
- #40 (motorola edge 5G UW 2021, SM7325, WN-Turnip 1.19 reported as "Adreno 7c+ Gen 3", 7.4 GB RAM with 1.46 GB free at start, 0.3-0.8 GB free later, scale 0.75): menus and the first area at 60 fps now; in the kingdom 14-41 fps with 0.4-1.1 s hitches, nearly all "making pipelines and shader stages" holds (up to 1002 ms), game logic 13 ms a step with 2-3 steps a frame. Asked him to try the phone's own driver and close other apps; second visits should be smoother (cache saved: 23 MB).
- #43: the zip attached is byte-identical to #42's (Infinix, MediaTek, app 0.4). Asked for his own log.
- #46 (Vulkan 1.1 phone, log on MediaFire, not downloaded) and #47 (4 GB phone, crash after the loading screen, no log): replies drafted. #31 and #32 confirmed fixed by their reporters. #35's reporter: cutscene audio plays ahead of the video (not looked into).

**2026-11-21 (late): 0.5.5 TESTED on the S24.** .var index: first start on empty test caches 170 s (game held 90 s) with it, 193 s (held 116 s) with SMO_VK_VARINDEX=0 (one pair, index run first). Widescreen: the user played: "runs and looks fine"; gameplay fills 2340x1080 (scene aspect 1.7778 -> 2.1667, layouts squeezed 0.821). First version also widened the title screen, whose full-screen layout left the title's 3D scene (white, black) at the sides: now TitleMenuScene (its call returns to main 0x5059b8) keeps 16:9 with black bars (g_ws_active), stage/demo/ending scenes are widened. Default ON (o->widescreen = 1; launcher Resolution card and in-game menu toggle, restart). The user asks for the HUD at the screen's corners: NOT done yet (layouts stay 16:9 in the middle).

**2026-11-21 (evening): 0.5.5 in progress, NOT tested on a phone yet (the user unplugged it).**
- .var index (nvn_vk_m4.c var_index_*): vstage_build and variants_known opened stage/<s>_<hb>.var for every stage whether it existed or not (on FUSE external storage each failed open is a round trip; another port's logs of this runtime counted ~92,000). Now the stage folder is listed once at prewarm_start and each written .var is added (lock-free 2^18 table); a stage without a file is not opened. SMO_VK_VARINDEX=0: as before. To measure: first start on an empty SMO_VK_CACHE folder, with and without (bar time, "first-start preparation finished after"), then a warm start (holds 0, pipelines made ahead as before).
- Re-copies within a batch (1.47 MB of the 13 MB a frame) looked at and dropped: they are ranges a logged host write changed (UpdateUniformBuffer etc.); the earlier copy may still be read by earlier draws, so a new copy is right.
- WIDESCREEN TEST (graphics.ini widescreen = on, default off, runtime/smo_widescreen.c, our own design): wrap al::getSceneFrameBufferMainAspect [0x9cf324] (3 caller files, 7 calls) to answer the window's aspect for a 16:9 frame buffer (Hor+ cameras into the same 16:9 picture, squeezed); the present pass stretches the picture over the whole window when widescreen is on (nvn_vk_draw.c, g_win_aspect); wrap nn::ui2d::DrawInfo::SetProjectionMtx [0xafeca0] (9 calls; it copies the 64-byte matrix to DrawInfo+16) and scale the matrix's first row by (16/9)/aspect for 16:9 orthographic layouts (m11/m00 = 16/9), so the HUD ends undistorted in the middle 16:9. Risks to check: layouts drawn into 16:9 textures would be squeezed; screen-space effects; the frozen screen capture; touch coordinates are unchanged (the overlay is ours). LoadProjectionMtx [0xafed08] only copies the matrix out. Callers of getSceneFrameBufferMainAspect: only the scene inits (TitleMenuScene, StageScene x2, DemoScene, DemoChangeWorldScene, EndingScene, WorldWarpHoleScene), each followed by al::setCameraAspect [0x9d05a4]: the cameras and the culling that follows from their projection all take the wider aspect (no objects culled at the new edges expected). The window aspect is known from the first present (boot screen), before TitleMenuScene::init.

**2026-11-21: the Retroid Pocket 4 Pro's extension list (0.5 log, odyssey-logs-20261121-1054.zip).** Mali-G77 MC9, Vulkan 1.1.177, 68 extensions. Missing: KHR_dynamic_rendering, KHR_synchronization2, EXT_extended_dynamic_state and _state2, KHR_maintenance4, KHR_copy_commands2, KHR_format_feature_flags2, EXT_pipeline_creation_cache_control. Present: timeline semaphores, descriptor indexing, buffer device address, renderpass2, imageless framebuffer, depth/stencil resolve, scalar block layout, subgroup size control, float16/int8, custom border colour, transform feedback. A path for it needs render passes and framebuffers instead of dynamic rendering, all of today's dynamic state (cull, front face, topology, depth/stencil, vertex strides) in the pipeline keys, and the old barrier and copy calls: a second renderer core. Not planned.

**2026-11-21 (later): 0.5.1, GitHub issues 40-43 and an S24 performance baseline.**
- #40 (Moto Edge 5G UW 2021, SM7325, WN-Turnip reports "Adreno 7c+ Gen 3", 0.4): a first start (empty cache), 863 pipeline holds / 82.6 s in 10 min, available RAM down to 136 MB, working set 3.6 GB. 0.5 covers it (memory -1.3 GB, first-start scan). #41 = the Retroid Pocket 4 Pro (Vulkan 1.1), same log as before. #43 (SD 7+ Gen 3 / Adreno 732, 3-15 fps, crashes): no log, asked for one. Reply drafts: build/android/GITHUB_REPLIES-0.5.1.md (not posted).
- #42 (Infinix X6720B, MT6835, Mali-G57 MC2, r38p1, 0.4): black screen with the touch buttons for ~1 min, then the player left. Log: the game in the Joy-Con-screen program hand-over, each program HELD ~20 ms (stages not in the cache although the first_prep marker was there), thermal severe from the start, GPU 10 ms for 8 draws. FIXED in 0.5.1: set_over_budget turns the start into a first-start preparation when the game was held 5 s before program 26185 (log "first-start preparation after all"); GameActivity.watchPrep polls for 3 min after the overlay hid and brings the bar back. Tested: SMO_VK_CACHE=../cache/vk_t42 with only the two marker files: the bar came back at program 1096 after 5.0 s and filled (programs 6418 at +30 s).
- S24 baseline (base51: system driver, scale 1.00, FG off, boost on, Cascade forward view 1210 draws, 3D 800x450 by dynamic resolution, started 32 C): fps median 58.6, 1% low median 32.9; GPU busy 11.9 ms, render thread 11.1 ms wall (draw prep 6.33), game logic 4.4 working + 8.7 waiting.
- simpleperf (prof51, 15 s at the same view): 80% of all samples are the boost spin (render 41%, guest-main 38%, __kernel_clock_gettime); real CPU work about 0.4 of a core. So the S24 is not CPU bound there; the render thread's "ms" include its waits. The spin's heat was already tested in 0.3.22 (WFE = lower fps, ISB = no gain): with boost = on the heat is the clocks.
- TRIED AND DROPPED (2026-11-21): FXAA at the picture's size before the upscale (a pre-pass into a 1600x900 RGBA8 image, the present pass then without FXAA). Same view, same build, SMO_PP_FXAA_PRE on/off: present 1.25 ms + pre-pass 0.50 ms = 1.75 ms against 1.74-1.82 ms the old way; fps and GPU busy equal. FXAA was only ~0.5 ms of the present pass, and the extra pass's write and read cost the same. The rest of the present pass (1.25 ms at 1080x2340: bilinear, sharpening, the touch controls and the text in the same pass) is what costs. Code reverted. The phone has 0.5.2 (code 58) = 0.5.1 code (the number was taken by the test build).
- Call-graph profile of the render thread (cg52, same view, 6 s at 2 kHz, --call-graph fp): plat_event_wait spin 36% (render_thread waiting for the next job), the rest real work: memcpy 6.3%, vkResetCommandPool 6.0% (slot_pools_reset from cmd_begin; moving it to a helper was tried in 0.3.22, SMO_VK_BG_RESET=1: no gain, Qualcomm's driver moves the cost into recording), vkCmdDrawIndexed 5.4%, vkCmdBeginRendering 5.0%, push descriptors 5.0%, scudo calloc/free 3.6% (inside the driver). The earlier 15 s profile (prof51) caught the render thread almost only spinning: the share varies with the moment. The GPU's idle gaps between passes follow the replay (chunked submissions already submit as soon as the GPU runs dry, 0.2.15). The S24 has little left on the CPU side that is ours; the per-draw cost is mostly Qualcomm's driver. Next leads: a log from GitHub issue 43 (Adreno 732, 3-15 fps: a real fps problem), and the Mali path's costs on a Mali log.
- FRAME TIMELINE (tl52, SMO_FRAMETL=6000,300, same view, tools/dev/frametl.py): per frame (ms from the refresh) post 8.54, replay 10.53 (p90 12.5), present 0.45, done 19.61, the game's draw 4.65, margin -7.6: the frame (gap 17.02) is a chain of the batch's post, our replay, the present and the game's next draw, just over one refresh; 187 of 298 frames waited for a later refresh (58-59 fps instead of 60). ASYNC PRESENT A/B (tla52 SMO_PRESENT_ASYNC=1 against tls52, back to back): fps median 58.1 / 57.7, 1% low 37.8 / 36.8, the game's waiting 1.8 / 9.3 ms a frame, render thread 12-13 / 11-12 ms: the game stops waiting but the frame stays ~17 ms: the limit is our replay plus the GPU work behind it, as in the earlier tests (PROGRESS 0.1.x, 0.3.8). frametl.py finds no rows in an async timeline (it expects the waiting present). Peanuts Native (igawa6/peanuts-native, GPL-3.0 APK): its zero-copy guest memory (Vulkan memory placed at guest addresses) is the radical answer to the per-frame copies; EXT_external_memory_host is NO on every driver in our logs; VK_EXT_map_memory_placed not checked yet (add it to the extension log).
- userfaultfd probe (runtime/android/android_uffd_probe.c, logged once at start, kept so testers' logs show their kernels): S24 (Android 16): "userfaultfd opened, UFFDIO_API with write-protect refused (Invalid argument)": the syscall is allowed but the kernel has no write-protect mode for it, so option (b) of the vertex-copy lead is out here; (a) mprotect + SIGSEGV stays.
- MALI PATH ON THE S24 (mali52: SMO_VK_PUSH=0 SMO_VK_VIDS=0 SMO_VK_CB_RELEASE=1, same view): 59.0 fps; descriptor sets cost little (vkAllocateDescriptorSets 2.5% + vkUpdateDescriptorSetWithTemplate 2.3% of the render thread). Largest item of ours on both paths: memcpy 6.3-7.6% of the render thread.
- THE BIGGEST LEAD LEFT (2026-11-21): the upload ring copies 13.1 MB a frame (cpy52: vertex streams 9.66 MB, of which the draws read 74%; constants and storage 2.37 MB), ring_put 65% cache hits. R.gen starts a new generation at EVERY batch (GEN_BATCH, nvn_vk_draw.c ~line 5024: "guest memory may have changed since the last batch"), so every mesh is copied again every frame, even unchanged ones: ~580 MB/s of memcpy at 60 fps, plus DRAM traffic (battery, heat) and the GPU reading host-visible memory. Textures avoid this on Windows with GetWriteWatch (nvn_gl.c P3); Android has no write watch (textures there are skipped by pool flags). Options for buffers on Android: (a) write-protect guest pages of ranges copied last frame (mprotect) and catch the game's first write per page in the SIGSEGV handler (unprotect + mark dirty); kernel writes into protected pages (read() from files into the game heap) would fail with EFAULT, so nnfs reads and every syscall target need the pages unprotected first: the risky part; (b) userfaultfd write-protect (UFFD_WP) if the kernel and SELinux allow it for apps: no syscall problem, but availability unknown; (c) hash each range against last frame's copy (reads the data anyway, saves the write and the ring space). Expected: most of the 9.66 MB gone, memcpy 7.6% -> ~1-2% of the render thread, less memory traffic on every phone. Not started.
- Pass times (frame 12000): 52 passes, 12.96 ms in passes, 11.91 ms of GPU idle between them (first to last 24.9 ms); main scene 5 passes 8.29 ms; OUR present pass 1.73 ms at 1080x2340 (bilinear + FXAA + sharpening 75: FXAA and sharpen per screen pixel); gap before pass 29 3.2 ms. Ideas: FXAA at the picture's size before the upscale (fewer pixels, the usual place for FXAA; needs a look check), fewer submission gaps.

**2026-11-21: the Xiaomi Sand Kingdom tester (25097RP43G / SM8735, Turnip Gen8 V39) on 0.4.2, log odyssey-logs-20261121-1935.zip.** He: "performance much better". 26 min, clean exit. fps median 59.9 (p10 57.0, min 48.2), 1% low median 42.8 (p10 10.6), 1 window under 50 fps, 44 windows with a frame over 100 ms; pipeline holds 172 / 11.5 s. His 0.4 log: median 57.2, 1% low median 11.2, 20 windows under 50, holds 1065 / 54 s in 16 min; 0.4.1: median 55.8, 1% low median 8.9, 31 under 50, holds 1037 / 61 s in 15 min. MAIN REASON: the pipeline cache. The 0.4 and 0.4.1 runs started with an EMPTY cache (0.4.1: 0 KB read, first-start preparation ran: wiped again before 0.4.1, likely a reinstall); 0.4.2 read 38.6 MB and made 2263 pipelines ahead in 3.7 s, so the update over 0.4.1 kept the cache. Memory as the system sees it did NOT change much: available RAM min 484 MB (0.4: 416, 0.4.1: 453), Vulkan memory 1520 MB (0.4.1: 1360). The areas played are not in the log (no stage names), so part of the gain may be areas he had seen. Still left: 172 pipelines built mid-game (about 67 ms each), the case for a shipped pipeline list. Cosmetic: resolution_scale = auto prints "0.00 is outside 0.25 .. 1.0" (auto is 0 in gfx_options; the warning should skip it).

**2026-11-21: Retroid Pocket 4 Pro tester (new; log odyssey-logs-20261119-1702.zip in tester's logs, app 0.4).** MediaTek MT8797Z (Dimensity 1100), Mali-G77 MC9, Android 13, 7.9 GB, no custom driver. Both runs end at "nvn_vk: no usable Vulkan 1.3 device" (driver Vulkan 1.1.177, 0x08001000): plat_exit(5) at start, seen as a crash. Also a launcher crash on 11-18 in importPackage: Dialog.dismiss on a DecorView not attached (the activity was rebuilt during the import; the import finished, "game files installed"). The OpenGL renderer is desktop GL (NVIDIA only), no fallback; no custom driver for this Mali. Added in 0.4.3 (code 55; 0.4.2 = the 12:46 build already sent to the Xiaomi tester, rebuilt exactly and committed as ea2ab40, symbols libsmo-0.4.2.so): the launcher warns before Play when FEATURE_VULKAN_HARDWARE_VERSION < 1.3 and no custom driver is in use ("Start anyway"); dismissSafely for the import and background dialogs; nvn_vk.c lists the 1.2/1.3 parts as extensions (dynamic_rendering, synchronization2, extended_dynamic_state 1/2, maintenance4, ...) and all extensions of an unusable device. NEXT: ask him for a log with 0.4.3 ("Start anyway"); if his driver lacks VK_KHR_dynamic_rendering and extended dynamic state, a Vulkan 1.1 path means render passes and fixed pipeline state: large. Not tested on a device below 1.3 (the S24 has 1.3); 0.4.3 installed on the S24 (launcher opens, Play starts the game without the warning: the S24 reports Vulkan 1.3, feature version 4206592).

**2026-11-21: FIXED (user watched), the first frog capture's frozen cutscene and its coloured right side.** runtime/smo_capture.c (new), wraps in build_runtime.py: f_40009d35b8, f_4000a80000, f_4000793860.
- Mechanism of the freeze: al::requestCaptureScreenCover(actor, n) [0x8f0b14] -> scene ScreenCoverCtrl (scene +184; +0 frame count, +4 capture request; requestCaptureScreenCover [0x9d3628], update [0x9d364c] counts down to 0 and clears +4). HakoniwaSequence::exePlayStage (each logic step): al::isRequestCaptureScreenCover -> ScreenCaptureExecutor::requestCapture(true, 0) [0x9d35b8] (entry +8 capture, +9 draw = 0) and resetRequest; al::isOffDrawScreenCover (count == 0) -> resetCaptureScreenCover (count -1) and offDraw() [0x9d3604] (+9 = 0, +24 = 0, NOT +8). HakoniwaSequence::drawMain -> tryCaptureAndDraw [0x9d34a0]: if +9 drawCaptureImage; if +8 copyImageFromFrameBuffer [0xa80000], +8 = 0, +9 = 1. At two steps per draw the count hit 0 and offDraw ran before the draw; the draw then copied and showed the frozen frame with nothing left to turn it off (the user: picture frozen, sound plays on). Fix: the requestCapture wrap calls smo_frame_draw_after_step() (smo_framerate.c, the same flag as the cube map change), so the draw follows the requesting step.
- WRONG THEORY, kept as a test only (SMO_CAPTURE_UV=1, off by default): the coloured area as a size mismatch in the copy. In freezefix1 every copy logged was capture 1600x900 from source 1600x900, uv scale 1 (the capture is made at the frame buffer size, the source is the display frame buffer, not the 800x450 3D target; the 1280x720 pass in the capdraw log was something else). The log now names only copies whose sizes differ. The copy: copyImageFromFrameBuffer draws the source (RenderBuffer +32 TextureData) with agl::utl::ImageFilter2D::drawTexture [0x793860] at uv scale (capture w / source w, min(minHeight, h) ratio): a 1:1 copy of the capture-sized corner. Capture 1280x720 (the frame buffer's logical size, made in HakoniwaSequence::init), source 800x450 (the game remakes its 3D targets at the dynamic resolution; res_hold keeps it low): scale 1.6, read past the right/bottom edges. With SMO_CAPTURE_UV=1 the drawTexture wrap passes uv scale 1, 1 when the sizes differ. drawCaptureImage scales by view size / capture size (1 here).
- Test: freezefix1 (new game, user's graphics.ini: frame generation on, scale 0.90, dynamic resolution on): the cutscene animated to "You captured a frog" (screenshot), the user watched: "much better", a small purple tint left at the bottom right (not looked into: maybe the capture's own scaled image (1280x720 drawn at 1152x648 with resolution scale 0.90) or the filter's edge; check with SMO_CAPTURE_UV logs and a scale 1.00 run). Earlier band_s100 (scale 1.00, no fix): the cutscene froze on one picture, so resolution scale was not the cause of the freeze.
- freezefix1 log: 16 capture requests logged before the frog (scene changes, the intro: the log cap), each followed by its copy. The purple tint at the bottom right: cause unknown. Logs: tester's logs/s24-frog-freeze-2026-11-20/band/freezefix1-log.txt.gz, freezefix1.mp4.

**2026-11-20/21 (night): the first-capture band, narrowed to the screen capture copy (not fixed). Cube map check changed.**
- Cube map check (smo_framerate.c, wrap of al::ShaderCubeMapKeeper::update [0xa02adc]): the keeper is ONE persistent object, so a pointer or a request-name check missed the frog tower (its request is "Default" again). Now it compares the keeper's state after the real update: +176 (current cube map info) and +808 (capture byte); a change with a non-null info ends the frame's logic steps after that step (owed steps go to g_acc). The tower's cube maps were drawn at its entry (frame 6194 in the capdraw run). The user is not sure whether the tower lighting issue is still there: check it with a user-watched run.
- The band is NOT live rendering: it is in the frozen picture the game captures when Mario throws the cap at the first frog (StageSceneNrvDemoHackStartFirst, DemoHackFirstDirectorNrvDemo shows the captured frame until the trance). Its colour changes per run, the user saw: yellow-white, bright blue/cyan wall on the right half, an all-black picture until the trance, now (band_fix4) a magenta tint on the right side. Changing colour = an image area that is never written (old memory).
- capdraw run (draw log frames 6545-6575, capture at frame 6547; tester's logs/s24-frog-freeze-2026-11-20/band/capdraw-drawlog-frames6545-6575.txt.gz, cappass.py lists passes). In frame 6547 there is ONE extra pass: target tex 0xb400007b4f5b84b0, fmt 0x3f, viewport 1280x720 (the capture image), one draw vs/fs #23325, reading tex 0xb400007b4fa26110 (scene colour, fmt 0x3f, descriptor 800x450) and 0xb400007b4fa233b0 (fmt 0xa, 800x450). Our real sizes: the game sets viewport 800x450 on the main-view targets, we render them at 720x405 (resolution_scale 0.90), and the 1280x720 capture image was rendered at 1152x648. So the copy reads 800x450 textures that hold 720x405 of picture and writes 90% of a 1280x720 image: both leave unwritten areas on the right and bottom. That fits "never written" colours but explains a 10% strip, not half the screen; the frozen picture may be shown later through a path the log did not cover (the capture image is not bound by any draw in frames 6547-6575: look for a copy/blit or a later frame).
- NEXT: (1) a run with resolution_scale 1.00 (g_user_scale100.ini) and SMO_DYNRES=0 at the capture, user watching: if the band goes, the fix is in how scaled targets are sampled or copied by full-size textures; (2) find where the capture image (mem 0x75a3bf4000) is read after frame 6547 (log more frames, or copy/blit commands); (3) the draw #23325 has a mesh vertex stream (3D positions), not a quad: read al::ScreenCapture::copyImageFromFrameBuffer [0xa80000] to see what the game draws there and with which uv.
- Phone state: game closed, save restored (Common.bin/File1.bin 23:44 backup), functions/shots/env removed. Recordings and logs: tester's logs/s24-frog-freeze-2026-11-20/band/.
- Builds: odyssey-0.4.2.apk (23:29, installed on the S24) and odyssey-0.4.2-armv8.0.apk (2026-11-21 00:03) both have the +176/+808 cube check. Nothing committed or pushed; GitHub reply drafts in build/android/GITHUB_REPLIES-0.4.2.md not posted.

**2026-11-20 (night, later): FIXED, the intro's ambient light at two steps a frame.** al::LiveActorKit::update (each logic step): clearGraphicsRequest, actors/demos (requestCubeMap: keeper +752 priority, -1 none, +760/+768 names), updateGraphics -> al::ShaderCubeMapKeeper::update [0xa02adc]; the next draw renders the cube maps (nvn_vk_cube log: 60 passes of 128x128 in one frame). smo_framerate.c wraps update (-Wl,--wrap=f_4000a02adc): a changed request ends the frame's logic steps after that step (owed steps to g_acc). Test: 60 cube passes at the intro's first frame with frame generation on (frame 825), steps 2.002 a frame, recording bright like the 1-step run. The user watched it on 2026-11-20 and confirmed: looks normal. The real look of the intro is BRIGHT and soft (I had taken the dark dramatic shadows for correct: wrong). OPEN: whether the band in the first-capture cutscene and the respawn bug (now carried by g_keep) are the same mechanism; the per-frame pass list showed no alternating passes otherwise. New diagnostics: nvn_vk_cube lines (passes into cube maps), the cube map request lines.

**2026-11-20 (night): THE INTRO'S MISSING SHADING / BLOOM AND BOWSER'S BLACK FLASHES = TWO LOGIC STEPS PER DRAWN FRAME (NOT fixed). The "fixed" note below was wrong: the user watched the intro and the effects were missing (my screenshot comparisons could not see what he sees).**
- User-watched tests (the intro, scripted new game, his settings: frame generation on): frame generation OFF (60 fps, 1 step a frame) = both issues gone; frame generation High or Fast = both back; frame generation off + fps cap 30 + steady_speed OFF (1 step a frame, slow motion) = bloom fine; frame generation off + fps cap 30 + steady_speed ON (2 steps a frame) = both back. So the cause is smo_framerate.c running sead::GameFrameworkNx::procCalc_ twice before one procDraw_ (frame generation turns steady speed on by itself). The Switch never runs the game that way. It would also hit any phone below 60 fps with steady speed on.
- Ruled out (each with the user watching or by log): the 0.4.2 drawing changes (MODREL, first-start scan, ring, quiet save, descriptor pools), resolution_scale 0.90 vs 1.00, dynamic resolution (SMO_DYNRES), the game's quality controller (SMO_QUALITY_NO_REDUCE=1 held it in Reduce: still flat), texture freeing (SMO_VK_TEXFREE=0) and exact occlusion counts (SMO_VK_OCC_SYNC=1), our render thread replaying the frame late (SMO_CALC_DRAIN=1: the extra step waits for the render thread: still flat). Frame generation never made a frame in the intro (fg_make returned early: no motion vectors there), so its made frames are not involved.
- Likely mechanism (not proven): some effect updates in turns per logic step (even/odd step, or only a step followed by a draw), so with two steps per draw the draw always meets the same parity. NEXT: compare the render passes per frame at 1 step vs 2 steps (SMO_VK_PASSTIME / passshot over consecutive frames: a pass that appears every other frame at 1 step and never or always at 2 steps names the effect), then find its counter in the game code (the respawn bug's cube maps and the pause menu re-render are a hint: ambient light cube maps / probes).
- Test switches added today (all off by default): SMO_RES_SUBVIEW=0, SMO_QUALITY_NO_REDUCE=1, SMO_CALC_DRAIN=1; logs: smaller views, the quality controller's shader-variation and bloom calls. Test settings in scratchpad: g_user_cap30.ini (cap 30, steady on, fg off), g_user_nofg.ini, g_user_fgfast.ini, g_user_scale100.ini. Recordings: tester's logs/s24-frog-freeze-2026-11-20/intro_recording_quality_blocked*.mp4.

**2026-11-20 (late): intro cutscene checked again: fixed.** Screen recordings from the first frame of the intro (caprec.sh, marker DemoSceneNrvPlay seen), the user's settings (frame generation on, dynamic resolution on) against SMO_DYNRES=0: the same shading at 0.3, 1, 2, 3, 4, 6, 9, 13, 18, 23 s. ARMv8.0 APK rebuilt after the occlusion fix (0 LSE atomics). Phone cleaned (env.txt, functions, shots, diag, freeze.txt removed; save restored).

**2026-11-20 (evening): the bright band in the first-capture cutscene, not solved.**
- The occlusion freeze fix holds: 4 more full Cap Kingdom runs (frog10, fc_def, fc_full, fc_oldsub) all reached step 93 after the capture, no freeze. Each had 20+ queries without a result (log capped at 20). Per-query log (g_occ_qinfo): the queries were BEGUN AND ENDED in submission 41173 (all 42,314 submissions finished) and come from just two frames (6567, 6721): our bookkeeping is right; the driver ran them and still reports no result. Root cause unknown (driver? a query in a pass the driver drops?).
- The band: a bright area over the right half (soft edge) in the first-capture cutscene (only the FIRST capture plays it: a scripted new game is needed, capseq.txt; with the user's save the capture has no cutscene). Its colour changed between runs: yellow-white (frog10), bright blue (fc_def): looks like a part of an image that is never drawn (old memory). Seen in 2 of 4 runs. NOT the 0.4.2 view-size change (the game asked for no smaller views in this scene: smo_quality now logs "a smaller view asks ..." and has SMO_RES_SUBVIEW=0 for the old behaviour). The runs without the band (SMO_DYNRES=0, SMO_RES_SUBVIEW=0) stayed on the close-up for the whole 25 s recording, so their recordings may end before the band's moment: those comparisons are not conclusive. NEXT: record the whole cutscene (60 s, caprec.sh takes a step marker), and a diag pass capture (SMO_VK_DIAG / pass shots) at that moment to see which pass writes the right half; SMO_VK_FRAMEGEN=0 run to rule out frame generation.
- Tools added: caprec.sh (scratchpad; waits for a new game process and a sequence step, then screenrecord 25 s). Recordings: scratchpad frog10_capture.mp4, fc_def.mp4 (band), fc_full.mp4, fc_oldsub.mp4.

**2026-11-20: 0.4.2 state (built, installed on the S24, NOT handed out; ARMv8.0 APK rebuilt at 11:2x but BEFORE the occlusion fix: rebuild it).**
- FIXED, endless loading screens (GitHub 34, 31): runtime/smo_asyncfunctor.c wraps al::AsyncFunctorThread::start [0xa2bc4c] (-Wl,--wrap in build_runtime.py): the game sends the job and THEN clears the done flag (+24); a fast worker's "done" was wiped. Proof: SMO_ASYNC_TEST=1 (game order + 20 ms gap) hung 2/2 in HakoniwaStateDeleteSceneNrvDeleteScene, =2 (fixed order + gap) 0/2; plain fix 0/12 new-game starts (before: 3/26).
- FIXED, intro cutscene flat shading: smo_quality.c res_hold gave only the main view the held size; smaller views kept the game's raw cap. Now all views get min(asked, held cap). Screencaps at the same moments matched dynamic_resolution = off. The user still thinks the intro looks unshaded in places: NEXT recheck from the very first second of the intro (introcaps.sh takes 2/5/8/12/20/30 s; add 0-1 s), with frame generation on as in the user's ini.
- FIXED, freeze after the first frog capture: occ_take read occlusion queries with VK_QUERY_RESULT_WAIT_BIT; queries with no result -> driver waits forever (render thread asleep in vulkan.adreno.so under vk_submit -> occ_resolve; main thread in nvnTextureFinalize -> gl_run, Presentation Thread in queue_present -> gl_run). Now WITH_AVAILABILITY, unwritten = 0 samples, logged. frog7 run: got through; 20+ unwritten queries at frame 6690 (capture at 6491), all in ONE long count entry, segments 655360..672859, unwritten from 661556 on (log capped at 20). OPEN: (a) WHY those queries have no result (a fold of a long-open count? queries reset by vkResetQueryPool while a later command buffer still writes them? segment numbering past a 65536 wrap? 655360 = 10 x 65536); (b) the user saw a YELLOW LIGHT over half the screen while Mario is pulled into the frog, likely the wrong 0 count deciding a glow: try the entry's previous count instead of 0, or find the root cause. Logs kept in "tester's logs/s24-frog-freeze-2026-11-20".
- New diagnostics: watchdog report has host call stacks of render / Presentation / main threads (hostrt.c host_stack_of_thread: tgkill SIGRTMAX-3, the thread unwinds itself; debuggerd -b needs root), the main thread sample (boot_main_sample), current nn::os waits per thread, the loading-screen trigger (60 s of <= 24 draws).
- Test tools (scratchpad): p50.sh (EMPTY_SAVE clears the save before each run, restores the user's after; NEVER start two at once), capseq.txt (Cap Kingdom new game to the first capture, ~10 min), introcaps.sh, files/functions/main_functions.tsv must be pushed to the phone for @nerve steps (REMOVED from the phone at the end of 2026-11-20 with the other test files; push it again for the next test).
- The phone's stay_on_while_plugged_in keeps resetting to 0; set svc power stayon usb before tests. Battery saver was on in one frog run (low_power); check settings get global low_power before tests.
- GitHub reply drafts: build/android/GITHUB_REPLIES-0.4.2.md (not posted; user reviews first).

**2026-11-19 (late): three bugs reproduced on the S24 (0.4.2 build, user's graphics.ini: frame generation on, dynamic resolution on). Test tools: scratchpad p50.sh (= p49 without SMO_START_STAGE; EMPTY_SAVE=1 clears the save before each run, the user's save is restored after), capseq.txt (tests/cap_kingdom.seq flattened), introcaps.sh (timed screencaps after DemoSceneNrvPlay; WARNING: it can match the PREVIOUS run's log.txt and fire early), frzwatch.sh (debuggerd -b at a freeze). The phone needs files/functions/main_functions.tsv for @nerve steps (pushed today), and env.txt lines may now be 16 KB (android_main.c, was 512).**
- INTRO CUTSCENE EFFECTS MISSING = DYNAMIC RESOLUTION. Same moments of the intro, screencaps side by side: SMO_DYNRES=1 (the game caps its 3D size; the slow first frames drop it to 800x450, then it climbs back 1 step per 8 s) is flat, no shadows; SMO_DYNRES=0 (full size) has deep shadows and rim light. The user saw it flat, then "suddenly good mid-cutscene" = the size climbing back. So some pass breaks when the game renders below its targets' full size (on the console the same game code works). NOT the keep list (raised 128 -> 1024 images anyway, 64 MB cap unchanged; the intro still looked flat). NEXT: find which pass (a capture at 800 wide vs 1600: shadow / light / AO passes and how their viewport or UV scale is handled), not "turn dynamic resolution off" (costs fps on weak phones). The dyn + frame generation off run did not reach the intro (it hung, below), so FG's part is not yet ruled out.
- ENDLESS LOADING (= GitHub 34, 31): 2 of about 8 new-game starts hung right after Start on the mode select. Report: every game thread idle in ReceiveMessageQueue (FileLoadThread, DecompThread, WorldResourceLoader, InitSystem, DeleteScene...), scene init threads finished, no SetAllocator of the next scene. Main thread sample: 97% in sead::GameFrameworkNx::waitForGpuDone_ -> sead::DisplayBufferNvn::waitAcquireDone (plain frame pacing): nothing polls, the sequence waits for work nobody does. In the good run FileLoadThread is next signalled by WorldResourceLoader; in the hung runs only by InitSystem, i.e. the world resource load is never requested. Suspects: a lost wake-up or message in os_threads.c (queues look right: mutex + cond + broadcast), an object at a reused guest address (obj() keyed by address, alias of system events), or a memory-order problem between game threads. NEXT: log the sequence's state (nerves of HakoniwaSequence / its loading state) in a hung run; log nn::os traffic of WorldResourceLoader's queue.
- FREEZE AFTER THE FIRST FROG CAPTURE: once in 2 full Cap Kingdom runs (cap3): frames stopped for good at frame 6591 right after the capture (FG on, the render size had just gone 1600->1440->1280->1440->1600). Presentation Thread stuck inside our nvnQueuePresentTexture, render thread not in its present (place 0), no Vulkan errors. Host stacks not captured yet (the repeat run hung in loading instead). NEXT: repeat with frzwatch.sh (does debuggerd -b work on this release app? unknown).

**2026-11-19 (evening): GitHub issues after the 0.4 release, read from their logs (downloaded with the user's OK, text files only).**
- #32 FIXED in 0.4.2: crash entering Shiveria Town (Infinix X6870, MediaTek MT6878, Android 16). The town's big screen plays a Snow video; SnowRace, SnowWithShip and SnowWithoutShip are the only game videos with moov AFTER mdat; that phone's extractor (our in-memory AMediaDataSource) opened SnowWithShip with 0 tracks -> crash in MovieDecoderPlayer::OperateThreadFunction. Now android/mp4_faststart.inc moves moov in front in memory (stco/co64 shifted); checked: same ffmpeg frame md5 for all three, World02 untouched; S24 run in SnowWorldTownStage played SnowRace with the moved index, no crash. 0 tracks now returns "cannot open" to the game.
- #34 (S26 Ultra SM8850, system driver, Android 17) and #31 (Xiaomi MT6989 Mali-G720, PanVK, 0.3) = endless loading screen after doors/pipes: in both logs the old scene is torn down (movie SetAllocator(0)) and the new one never starts (no SetAllocator(new)); logic runs 0.1 ms steps; no loader thread uses CPU; the waits the reporter quoted are idle threads. nn_waitmap only records FINISHED waits, so the stuck thread was invisible. 0.4.2: the watchdog also reports after 60 s of only a loading screen (<= 24 draws a frame, not during the first-start bar), and each guest thread line shows its current nn::os wait (how long, the game callers by frame pointers). NEXT: ask both for a 0.4.2 log of the hang. The user also sees a freeze on SOME devices at the first frog capture cutscene and at a door in Cap Kingdom: maybe the same bug; keep in mind.
- #33 (Galaxy S26 Exynos 2600, first start, German): abort in sead::ExpHeap::tryAlloc (Halt) inside ParallelSZSDecompressor::tryDecompFromDevice <- al::ResourceSystem::createCategoryResourceAll <- Application::init, with ~3.2 GB (0xc26b3fa0) in the registers: the archive's header was not a valid Yaz0 header, i.e. the file's bytes are bad (game files read with plain pread). Likely a bad import or a mod. 0.4.2 crash reports name the last file opened and its first 16 bytes. Ask him to re-import (no mods) and send a 0.4.2 log.
- #39 Pixel 10 Pro XL (PowerVR): his video shows the picture washed out in white fog that flashes with movement, 14-16 fps, Cap Kingdom. 0.4.2 turns the Diagnostics option on for PowerVR (vendor 0x1010) too. Ask for a log with Diagnostics on.
- #35 (Moto G Play 2024, SD680, 4 GB): closes at the opening cutscene; no log (4 GB RAM is the likely limit; ask for a log). #30 (Honor Magic 8 Lite, Chain Chomp Cave second area crash, "not in the version before"): no log, ask for one.
- 0.4.2 rebuilt (main + ARMv8.0, 0 LSE atomics outside helpers), CHANGELOG updated. Phone tip: the S24 had stay_on_while_plugged_in 0 again; set back with svc power stayon usb.

**0.4.1 test build (code 53, 2026-11-18) for the Xiaomi Sand Kingdom tester:** launcher CPU cores Fixed / Free (cpu_cores), pipeline cache saved in loading screens (m4_quiet_save), affinity log lines. His test on 0.4 (boost off, steady speed off): Sand 16-45 fps (from 5-12), helper wake-ups 1.2 ms avg (from 2.0), and his main thread ran on cpu7 and the render thread on cpu2 although pinned elsewhere. S24 Fixed vs Free (Cascade, boost on): logic waiting 6.5-8.1 vs 1.5-1.7 ms a step, wake-ups 780 vs 66 us, render thread 7.4-9.1 vs 10.7-11.1 ms, both 60 fps. NEXT: his 0.4.1 log with Free in Sand Kingdom; if Free wins there, consider Free (or the render thread alone on its core with the helpers free) as the default after measuring a render-bound phone (Mali) both ways.

**Phone state 2026-11-18: 0.4 (code 52) is the release build**, installed on the S24 (symbols build/android/symbols/libsmo-0.4.so; APKs build/android/odyssey-0.4.apk and odyssey-0.4-armv8.0.apk; notes build/android/RELEASE_NOTES-0.4.md). The user publishes it. The next build handed out must be 0.4.1 (code 53) or later. Open: the Action Guide closes at once (no video opened; not looked into), the Cascade mist, frame generation on weak Mali (tester logs with 0.4 will show Auto and the object budget).

**Phone state 2026-11-17 (later):** the S24 has 0.3.21 (code 51, background video decoding). Symbols: build/android/symbols/libsmo-0.3.21.so. The next handed-out build must be 0.3.22 (code 52) or later.
**Phone state 2026-11-17:** the S24 has 0.3.20 (code 50). Symbols: build/android/symbols/libsmo-0.3.20.so. The next build must be 0.3.21 (code 51) or later; never uninstall (game data). Nothing committed.

- **Mali, LAS (`docs/vulkan-gpu-defaults.md`, `vulkan/shaders.c`, `vulkan/exec.c`):** spirv-opt on fragment and compute only
  (one interior on a Mali-G610 with Arm's driver: 1.5 to 30 fps; that shader was the pc-loop form); split pass barriers
  (14.8 to 13.65 ms); hot constant buffer words as push constants (measured on Turnip and PanVK; Arm's driver promotes them
  itself, so may gain less). We set no optimisation level in `vk_shaderc_android.c`.
- **Holes in models:** vertex stages stay unoptimised, because spirv-opt made pre-pass and colour pass depths differ. PM's
  `vulkan/shaders.c` puts NoContraction on every vertex float operation in the SPIR-V, because `precise` only covers what
  glslang traces back through register code (our 0.1.6 `fres` test has the same limit). LAS: NVIDIA ran the math after
  `unpackHalf2x16` in fp16, differently per shader, so vertex stages convert halves by bits. We use `unpackHalf2x16` in
  every stage. Mali is not shown to do this.
- **Game timers (PM `exec.c`):** report timestamps are Switch GPU ticks (614.4 MHz; the game multiplies by 625/384), and host
  time at translation counts command translation as GPU time. We write host ns at translation, so the quality controller
  may see frames as too slow. Fits the 800x450 seen with dynamic resolution on. Not tested.
- **Diagnostic to copy:** PM's `ODYSSEY_WATCH_MAX`: read an R11G11B10F target back after each draw and log the draw that first
  passes a value. Would name the draw that starts a glow.
- **Possible crash:** PM forces the lock on in `sead::Heap::Heap` (main+0x743fa0 region, `tools/odyssey_patches.py`): an
  unlocked heap failed allocations with threads in parallel. `os_threads.c:156` describes the same failure and handles it in
  thread scheduling, so this is low priority.
- **Not useful on a phone:** graphics pipeline libraries (the G615 has none), FSR, frame interpolation, no occlusion culling.

## 60 fps on the S24: the GPU clock decides (2026-11-06, 0.1.8)

Galaxy S24 Ultra, system driver, Cascade flag view (about 846 draws), 60 fps mode, phone cooled to thermal status 0 before each
run, last windows of 150-170 s runs.

- **Adreno's governor (`msm-adreno-tz`):** steps 231 (lowest used), 310, 366, 422, 500 ... 1000 MHz. It rises only at about 84% busy
  and falls below about 70%. High clocks come only after loading screens, then decay. Readable by the app in
  `/sys/class/kgsl/kgsl-3d0/` (`clock_mhz`, `gpu_busy_percentage`, `gpu_clock_stats`; `gpuclk` denied). Same settings, 800 wide: **56.6 fps
  at 310 MHz, 45.7 at 231.** The `perf:` lines in the log now end with the clock.
- **Did not move the clock:** reporting the frame as GPU work to the hint session (44.5 fps); useless compute work each frame, 8/24/64
  groups (40/35/40); bursts at 231 MHz (44-47); holding 310 with compute when busy fell (42-47). All removed.
- **Did:** chunked submissions (LAS's lesson). `SMO_VK_SUBMIT_DRAWS=15` is about 14 submits a frame: wait-other 4-5 ms to under 1 ms, and
  310 MHz in every run so far. 40 draws: 55-58 fps, less steady. 8 draws: 57.2, no gain. Default 15 on Adreno, 150 elsewhere.
- **Game's resolution controller:** with real GPU timers it swung 800-1600 about 5 times a second, and the game remakes its targets at each
  size. `res_hold` (`smo_quality.c`) follows it in steps of 96 wide: down after 1 s under, up after 8 s of room (doubling to 128 s when a
  step up is taken back within 16 s). Settles at 800 wide in Cascade. Fixed sizes at draws 15: 800 58.0 fps, 960 54.4, 1120 48.5.
- **Free pacing:** alone no gain (43-46 fps, the clock stays at 231). With chunking and the limiter: 57.0 fps against 56.2, 1% low 41.8
  against 33. Default in the 60 fps mode on Android, `SMO_PACE=refresh` to go back. Busy stays 76-81%, so it does not reach the next clock.
- **What is left for 60:** frame about 17.4 ms; render thread replay about 10.7 ms (chunking added 1-2), GPU about 12.7 ms at 310. Render
  thread: libsmo 39%, Adreno driver 35%, libc 25%. Ideas in order: `SMO_VK_PUSH=0`, faster hash than byte-wise `fnv`, less malloc in replay,
  LAS's key memo and recorder thread. 62% of CPU samples are the boost's spin in vdso: a heat cost, and thermal moderate after about 2 min
  caps the CPU. `SMO_SPINHOT_US=0` is not an answer (30 fps).
- **Wrong turns:** free pacing alone, polling GPU stats as the cause, "45 is refresh rounding". Compare clocks first when two runs disagree.

- **Defaults are slower than the numbers above (0.2, one run each, same view):** the user's settings (mailbox, boost on, scale 1.00, FXAA, sharpening
  75, system driver): 58.1 fps, 1% low 44.9, 310 MHz, 77% busy. Fresh defaults with the system driver (clock pacing, boost auto, scale auto, no
  FXAA or sharpening): 44.6 fps, 1% low 33.6, **231 MHz**, 82% busy, wait-other 8.4 ms. Defaults on Turnip: 57.2 fps. Chunking alone does not hold 310, then.
  Which setting makes the difference is not tested. Lead: FXAA and sharpening add GPU work and may carry the governor over its 84% line. A fresh
  `graphics.ini` says `driver = custom`, which picks Turnip whenever a package is installed: set `driver = system` for native tests.

- **0.2 fresh installs on Android start on those settings** (mailbox, boost on, scale 1.00, FXAA, sharpening 75): 57.9 fps, 1% low 40.8, 310 MHz in a 200 s run on
  the file the game writes (driver set to system, this phone has Turnip installed). Which of the five makes the difference is still not tested. Not run on Mali.

## Mali tester's result on 0.1.6 (2026-11-06)

Xiaomi 2406APNFAG, Mali-G615 MC6, r44p1. Tester: format option ON = visual errors stay, fps a little better; OFF = errors gone, fps a little
worse. Log `tester's logs/thesolvedmaliissuelog.zip`: start 2, strict math ON, formats native, captures clean; start 3, strict math OFF, no
captures, ini at export says on. The earlier captures (stripes, blocky shadows, white smears) are from a start with no surviving log.
Since 0.1.6 alternated strict math each start, which of the two cured it is **not settled**. Fps there fell to 10-27 in Sand, hitches up
to 925 ms.

0.1.8 ships that clean combination for good on Arm: the format swap (`fmt_sub`) is off and no longer an option (launcher and `graphics.ini` key
removed, an old `mali_format_fix` line is ignored; `SMO_VK_FMT_SUB=1` for tests), guard and strict math on, no alternation. Optimiser
(`SMO_VK_OPT_FS`) and split barriers (`SMO_VK_SPLIT_BARRIER`) off until the state is confirmed: the optimiser kept a cold first start on the
Joy-Con screen (7,900 of 25,000 shaders in 5.5 min on the S24). **The tester played 0.1.8 and reports no graphical issues and good fps (by message, no log of that build seen; one phone).** 0.2 also turns Mali diagnostics and the near-miss test off by default. Next, for the lost fps:
swap only 0x0a and 0x2e and not 0x13 (SFLOAT does not clamp and can hold NaN), split barriers, optimiser for drawn shaders only. Chunking at
15 is unmeasured on Mali.

## After Beta 0.2: GitHub issues (2026-11-07, 0.2.1 test build)

**The Joy-Con screen (#12, #13, #14, #15).** The screen before the game's menu waits on `nvn_vk_program_set`, which holds the game's loader while translation
is behind; about 40,000 programs are handed over on a first start. 0.2.1 limits that hold to 90 s per start on Android (`SMO_VK_HOLD_BUDGET_S`, 0 = no limit). After that,
programs go to the background scan or are made at their first draw. A notice is shown while the game is held. S24 Ultra, empty test cache:

| Run | Held | Leaves the screen | Fps afterwards |
|---|---|---|---|
| 0.2, no limit | 119 s | 138 s | about 54 |
| limit 30 s | 30.0 s | 59 s | about 50-53 |
| limit 90 s | 90.0 s | 113 s | about 50 |
| second start on the cache of the 90 s run | 52 s | 71 s | 52-54 at the end |

Holds at draws 14.7 s and 12.6 s against 11.9 s. No errors; 1 watchdog report in each cold run, as in 0.2. The lower fps is probably the background scan (not confirmed). The second
start is above the usual 35 s because the first run stopped before the scan finished. No log from a stuck run exists, so the hold is not confirmed as what stops the affected phones.
The notice was seen on screen; its window has the not-touchable and not-focusable flags. Touch through it was not tried by hand.

**Black, grainy ground on Mali-G615 (#11, Poco X6 Pro, Cap Kingdom, handheld, 30 fps, system driver, 0.2).** Not understood yet. From the reporter's log and video:
the game's cap fell 1280 to 640 wide at about the time fps fell, the limiter followed (7 steps down, none up logged), and the bad frames in the video are at 26 and 24 fps. Log and video cannot be lined up exactly.
S24, Cap's start area, same settings (runs `c0`-`c5`), static camera, not the reporter's hill:
- At 640 wide alone, and at 640 with `SMO_VK_SIM_FMT=100:b,109:b,78:l`: no black. The share of dark pixels is the same as at full size. The tiny coloured specks show here too, so those may not be a bug.
- This setup falls to 640 on the S24 as well and stays there. The game's GPU timers span the frame across chunked submits, a likely reason the cap sits at its floor (not measured). `SMO_DYNRES=0` gives 1280x720 at 27-29 fps there, a clearly sharper picture, so 640 is not free of cost but neither is full size.
- 0.2.1 reads R16G16_SNORM (the G-buffer's second target, not filterable on that GPU) nearest. `SMO_VK_UNSUP_NEAREST=0` restores linear, `=1` applies it to every such format. The picture is fine on the S24 with limits imitated, which cannot imitate Mali's undefined result.
- Evidence against this suspect: the tester's clean 0.1.6 runs had that format native with linear reads, and this texture is half the width of the view, so reads interpolate at full size as well. It is the cheapest thing to rule out, not a finding. Needs a Mali run at that spot.

**First-start preparation (0.2.2).** Replaces the 0.2.1 limit and notice on a first start. `tools_vk`'s container store was never written (`nvn_vk_store.c` is a stub), so the preparation is the game's own hand-over on the Joy-Con screen. The program serial reaches about 26,185 by the end of that screen on the S24 and on the Poco alike (`FIRST_PREP_PROGRAMS`, game 1.0.0). `first_prep_done.txt` in the cache folder marks it done, so another cache folder (another driver) prepares again. S24: 151-154 s from nothing; closed at 60 s, the next start took 121 s; Skip at 35 s reached the menu within about 70 s, and the next start got to program 10,022 at 38 s against 5,075 at 35 s; later start: no bar, about 30 s on the Joy-Con screen. Touching the bar's window works (Skip was tapped by coordinates and by hand). Not run on a slow phone.

**Test input and baselines (found 2026-11-07).**
- `SMO_SEQ=@until-nerve:StageSceneNrvPlay:Z/45` never ends on the phone: `../functions/main_functions.tsv` is not there, so nerve tracking is off and Z is pressed every 45 polls for the whole run. Mario keeps jumping, the camera looks down, and the flag view in Cascade draws about 846 per frame. Standing still it draws 1205. All earlier Cascade numbers are of the jumping view. `@until:800-1400/20:Z/45` stops the presses once the stage is drawn.
- The S24's graphics.ini was on `resolution_scale = auto` for all of today's runs before `q3` (`jb`, `j1`-`j3`, `q1`), against 1.00 for the 57-58 fps runs of 2026-11-06, so those are not comparable. It was 1.00 at 22:56 on 11-06. Probably left by the "old defaults" test.
- Fps, 0.2.2, jumping, 100%, starting at 34°C (runs `a1`, `b1`, `a2`, `b2`, all 0.2.2): from 90 s 57.5, 56.7, 47.9, 52.2. Settled at 310 MHz all four run 56-58. The spread is how long the GPU stays at 231 MHz (45-48 fps): 90, 100, 175 and 135 s. Yesterday's 0.2 runs at 310 MHz: 57-59. `b1` and `b2` were meant to be 0.2, but `adb install -r` refuses the lower version code, so there is no same-day 0.2 run. Standing still, 100% (`q3`): about 34 fps, the clock stays at 231 MHz. A hotter run (`q4`, thermal moderate) falls to 47-51 after about 200 s as the clock drops to 231.

**#9 (Moto G52, Snapdragon 680).** SIGILL in `display_init`: the build uses ARMv8.2 (`-march=armv8.2-a`, `tools/android/android_env.py`) and that phone has ARMv8.0 cores. 0.2 does not change that. The flag is deliberate: LSE atomics, since outline helpers took 3-10% of the game threads on the S24.

## Render passes and upscalers (2026-11-09, 0.2.3 test build)

**Why passes split (SMO_VK_PASSTIME now prints the address of the code that ended each pass, and a line for each layout change that ends one).** Forward view of Cascade, S24 Ultra:
- The 4-target G-buffer pass was cut 3 times by `resolve_sampler`, which switched textures from COLOR_ATTACHMENT (2) to SHADER_READ_ONLY (5) at their first read: 128x128 (format 38), 2x1 (122), 256x256 (76), all drawn earlier in the frame, and later 400x225 (100, 37) and 200x113 (76). Each cut stored and reloaded 4 colour targets plus depth.
- The empty clear passes (0, 6, 10, 24, 33) came from `pass_begin` -> `vt_layout` -> `pass_end`, which performed the pending load-clears in an empty pass before the real one loaded the result.
- Fixes in `pass_begin` (`nvn_vk_draw.c`, "EARLY READ LAYOUT" and "KEEP THE LOAD-CLEARS"): the last pass's attachments that the new pass does not use go to SHADER_READ_ONLY between the passes, and pending clears are kept while the pass changes its own attachments' layouts. `SMO_VK_EARLYREAD=0` / `SMO_VK_LCKEEP=0` undo them.
- Runs `e1` (on) and `e0` (off), back to back, both at 310 MHz at the measured frame: 50 passes against 62; GPU first to last 23.55 ms against 28.10 (in passes 14.66 against 17.43); 49.8 fps against 44.3. The G-buffer is one pass of 7.2 ms. Same picture (screenshots side by side; only Mario's animation timing differs).
- Jumping view, 100%, started at 32.8°C (run `j4`): 56-59 fps at 310 MHz, GPU 12.6 ms per frame. Same range as 0.2.2.
- Not yet: the clear pass before the G-buffer (pass 11 now: depth cleared with 1 colour target, then the 4-target pass loads it). The pending clear does not carry over to a new target set.

**Upscalers (`upscaling` in graphics.ini, the launcher's "Upscaler"; `SMO_UPSCALE` for tests).** All in the present pass, so they enlarge the final picture (the HUD too), not the game's 3D on its own.
- bilinear (default), bicubic (Catmull-Rom, 9 bilinear reads), gsr (Snapdragon GSR 1 ported into the present shader, edge sharpness from `gsr_sharpness`), fsr (AMD FSR 1: `up_easu` draws EASU into an RGBA8 image at the picture's place on screen, then the present shader runs RCAS on it texel for texel; sharpness `fsr_sharpness`, 90% = 0.2 stops). The shaders come from Power Moon Recomp's adaptation (MIT); here EASU's taps stay inside the crop.
- Cost at 50% scale (800x450 to 1920x1080), forward view (runs `pb`, `pc`, `pf`, `pg`): present pass 2.49 ms bilinear + FXAA + sharpening, 2.49 bicubic, 2.19 GSR, all at 231 MHz. FSR's run was at 422 MHz: RCAS present 1.15 ms, EASU about 2.5 ms in the gap before it. The fps of these runs are not comparable (different GPU clocks).
- Look (screenshots at 50%): bilinear with FXAA and sharpening shows dark specks; bicubic is a little sharper; FSR is the smoothest and sharp; GSR is sharp with some stair-stepping. Not seen on a Mali phone.
- The present pass itself costs 2.2-2.5 ms at 231 MHz for 2340x1080. Worth a look later.

**Test runs.** `TaskStop` on the runner left its child processes going: they kept starting and stopping the game for about 20 minutes, broke a batch and changed the save. The save was put back from the backups (all identical). Stop the runner with `taskkill /T` on its root process.

## In-game menu and performance overlay (2026-11-09, 0.2.4 test build)

- InGameMenu.java replaces the exit question: a right-side panel, the game held behind it. Picture options are re-read by
  `gfx_options_reload_live()` (nativeReloadOptions), then the game runs for PREVIEW_MS (150 ms) so the held frame shows the
  change. `nativeMenuOpen` sets `g_menu_open`, so the dialog's focus does not count as "in the background" (otherwise
  nnmisc.c suspended the game and the preview never drew).
- The menu's views are made with a ContextThemeWrapper of Theme.DeviceDefault.NoActionBar (the launcher's theme). The game
  activity has Theme.Black, where a SeekBar is the old grey bar and a Switch is a bare "ON" text.
- The performance overlay (fps, AVG, 1% LOW, MS) is drawn in the present shader with a 5x7 font kept in the OV uniform
  block (a constant array indexed by a variable is slow on Adreno). Frame times: a 600-entry ring, 1% low = 1000 / p99.
- Tested on the S24 Ultra: upscaler changes show while the menu is open (log "upscaling = X" right after the tap, FSR's
  EASU pass starting), the overlay lines and size change live, the restart note shows for Console mode and hides when
  it is set back, Restart (two taps) ends the game process within 1 s and starts a new one 1 s later, Exit returns to the
  launcher. Not tested on a Mali phone.
- The overlay sits in the top left, under the default touch layout's ZL and L buttons.

## Uniform buffers and the 0.2.5 changes (2026-11-09, 0.2.5 test build)

- **Constant buffers were storage buffers** (`readonly buffer CBn { uint cn[]; }`); LAS and PM use `uniform CBn { uvec4 cbn[N]; }`.
  Now `mx_ubo` (mx_translate.c): graphics stages declare `uniform CBn { uvec4 cn_v[mx_ubo_vec4]; }` plus `uint cn_r(uint i)`, and
  `ubo_rewrite` turns every `cN[expr]` of the output into `cN_r(expr)` (brackets counted). Compute stages stay storage. Backend:
  `CB_DT` for the layouts (layout_bindings, gpl_universal), uniform descriptor writes with the range clamped to
  maxUniformBufferRange, `D.dyn0u` (dynamic uniform budget, dyn0_pick counts the two kinds apart, 64 in all), pools with uniform
  sizes, UNIFORM usage on the ring, the zero buffer and the guest-memory buffers, and the uniform offset alignment for buffers
  bound in place. Stage hash: "ubo1" + mx_ubo_vec4. On when maxPerStageDescriptorUniformBuffers >= 17, set >= 51, range >= 16 KB.
  S24 limits: 16777216 a stage and a set, 32 dynamic, 65536 bytes, offsets of 256.
- Runs `s6` (storage) and `u7` (uniform), Cascade forward view, 1205 draws, both at 231 MHz and about 78% busy: 41 against 49 fps;
  in passes 18.62 against 15.45 ms; G-buffer pass 9.45 against 7.52; first to last 27.26 against 23.68. Picture checked by
  screenshot. (`u6` landed in the down view, 846 draws, 59.5 fps: not comparable.)
- **c0 bug:** layouts and writes began at constant buffer 1, so a graphics stage reading c0 outside the storage descriptors was
  refused (VkResult -13, Qualcomm's driver; the same 6 pipelines in every run, draws left out). Now bound to R.zero.
- **Still there:** a gap of about 7 ms before pass 9 (after the 256x256 shadow passes, before the G-buffer's 843 draws) in both
  runs: the GPU waits for the render thread. The forward view is now partly CPU-bound (batch replay ~12 ms, draw preparation ~7 ms).
  The present pass is 2.5 ms at 231 MHz (bicubic, FXAA, sharpening, 2340x1080).
- Clear carry (OP_TARGETS, `clearcarry_on`): 3 clears a frame go to the next pass, 50 to 48 passes. Present pass per upscaler
  (specialisation constant UMODE): 2.56 to 2.48 ms. Fast min / max (Adreno only) and the Arm command buffer release: no
  measurable change on the S24 (the latter does not apply there).
- Test-method note: fps alone misleads on the S24 (the clock governor jumps after loading screens and decays); compare
  SMO_VK_PASSTIME at a frame both runs reach in the level (7000), the same draw count, and the clock in busy.txt.

## Steady game speed (2026-11-10, 0.2.6 test build)

- `smo_framerate.c` (the wrap of procCalc_, main 0x731fdc): with `steady_speed = on` each procCalc_ call runs the calc n times,
  n = floor(acc + 0.15) clamped to 1..3, acc += frame time x 60 (in steps), acc -= n, acc kept in [-0.5, 1]; a frame over 0.25 s
  resets acc to 1. The 0.15 keeps a frame rate wobbling around 60 at one step. Read every frame (gfx_options()->steady, live
  through gfx_options_reload_live and the in-game menu). The log prints steps per frame, how many frames had 1/2/3, and the time
  of a step every 1800 frames.
- Runs: `st1` (fps_cap 40, steady on): 1.505 steps a frame at 39.9 fps = 60.2 steps a second. `st2` / `st3` (uncapped, forward view,
  1205 draws, 231 MHz): off 48 fps / 1.000; on 48.6 fps / 1.23 = 59.8 steps a second. A step measures 11-13 ms on the main thread,
  but most of it is waiting (no fps lost with 23% more steps).
- In-game menu toggle tested: "changed in the game: ... steady_speed=on" right after the tap.
- Not done: above 60 fps (120 Hz screens) needs in-between frames (Power Moon Recomp's frame interpolation, MIT, LAS Recomp's core).
  Not tested: cutscene and audio sync with steps doubled (frame_rate = 30 has run two steps a frame since 2026-11-02 without reports).

## CPU per draw, shader constants, split passes (2026-11-10, 0.2.7 test build)

- **Profile (simpleperf, render thread, forward view, 0.2.6):** about 10-11 ms busy a frame: libsmo ~4.4 ms, Qualcomm's driver ~3.5,
  libc ~2.7 (memcpy 0.8: the ring copies 9.3 MB a frame because the copy cache is dropped at every batch; scudo malloc/free ~0.5;
  atomics 0.4). The boost spin (vdso clock_gettime) is on top. Fixed costs per chunk: vkResetCommandPool ~0.5 ms and
  vkCmdBeginRendering ~0.35 ms a frame even at the title (122 draws).
- **mx_c1_inline** (mx_translate.c CB(), stage mask, default 0x22 = fragment + compute): static c1 reads become literals from
  MxIn.cdata; dynamic reads (LDC, 6 sites in the whole game) keep the buffer. Stage hash "c1i1" (ssrc_hb, only with cdata).
  Main pass 7.34 vs 7.51 ms at 231 MHz (runs n1 / o2, separate sessions, same view). Vertex stages left out on purpose.
- **draw_prepare state cache (g_dc):** valid only within a command buffer and after a pipeline of the game (pipe_bound NULL resets it);
  push descriptors compared by their buffer infos, set 0 / set 1 by set and layout. **Descriptor update templates** per Pipe
  (dt[0], dt[1], made from the first draw's write list; push form with vkCmdPushDescriptorSetWithTemplateKHR). **Pipeline memo**
  and **hfast** for g_pipes and the set tables (never for disk names: fnv stays). In-session A/B (`SMO_VK_AB=600`, run `ab1`):
  draw preparation 6.01 vs 6.16 ms, replay 10.00 vs 10.26.
- **inpass_split** (after each draw, Adreno default 200, `SMO_VK_SPLIT_PASS`): cmd_submit inside a pass; pass_begin loads the
  targets in the next command buffer. Runs `sp1` (200) / `sp2` (100), A/B in one session, forward view 1203 draws: 58.10 vs 55.81 fps
  (the off windows fell to 50-53 when the clock dropped to 231) and 58.16 vs 57.85 (clock at 310 all along). Replay +0.7-1.1 ms
  (more submits). 100 gives no more than 200.
- **Test method:** separate-session pairs were spoiled twice: once by heat (thermal moderate, CPU at 1.8-2.3 GHz against 2.8-2.9)
  and once by the camera (526 vs 843 draws in the main pass). `SMO_VK_AB` (+ `SMO_VK_AB_WHAT` mask: 1 cache and templates, 2 split)
  switches by turns every N frames, so both states share view, heat and clocks. Getting the forward view: wait for the title (122
  draws), press Z until the loading screen (8 draws), then press nothing: `SMO_SEQ=@draws:110-130/20 @until:0-100/30:Z/45
  @draws:1000-1400/20`. A press after spawning makes Mario jump and the camera look down (the 777-draw view).
- **Runner mistake:** editing the running p30.sh (bash reads a script as it goes) made it run its final cleanup early and start
  another run; the save was put back by hand from the backup. Never edit a runner that is running: copy it first.
- **Next ideas:** the ring copies (zero-copy needs host-memory import, which Qualcomm's driver lacks: an AHardwareBuffer / dmabuf
  remap of the memory pools would be the way, risky); the per-chunk pool resets (one pool per frame, many command buffers);
  LAS's hot constant buffer words as push constants; the present pass (2.6 ms at 231 MHz with bicubic and sharpening).

## Frame rate options and switches (2026-11-10, 0.2.8 test build)

- frame_rate (60 / 30) removed from LauncherActivity and InGameMenu (and from RESTART_KEYS). The launcher migrates `frame_rate = 30` at
  start to `fps_cap = 30` + `steady_speed = on` and removes the line; gfx_options.c reads an old `frame_rate = 30` as fps_cap 30
  (fps30 is always 0 now; `SMO_CALC_TWICE` still forces two steps for tests). fps_cap is copied by gfx_options_reload_live, so the
  in-game slider applies at once (display.c frame_queued reads it every frame). Steady speed is NOT switched on by itself: the user
  asked for an explicit note and a yellow warning (fpsWarn in both activities: visible while fps_cap < 60 and steady_speed off).
- Switches: LauncherActivity.styleSwitch: a solid rounded GradientDrawable track tinted by state (ACCENT2 checked, 0xFF3A404C
  unchecked, SRC_IN), thumb white / light grey. The theme's own track is drawn see-through, so a tint alone looked dull.
- Tested on the S24: launcher Graphics page (no Frame rate choice, warning appears at limit 40 with steady off and goes when it is
  turned on, switch blue / grey), in-game menu (same; log "changed in the game: ... steady_speed=on fps_cap=40"). The user's
  graphics.ini and save were put back afterwards (md5 checked).

## Mali's CPU path on the S24, and submissions when the GPU runs dry (2026-11-11, 0.2.15)

- **Why:** the user asked for optimizations that help every phone, especially Mali, not just the S24. The S24 pass timing (SMO_VK_PASSTIME,
  frame 4000, 1600x900) showed 14.9 ms in passes and 12.0 ms of gaps where the GPU waits for the render thread. The passes split into 4 there
  (#11-#14) are the Adreno-only in-pass splits.
- **Mali logs:** the tester's 0.2.14 log and the issue logs (#11 G615 MC6, #14 / #16 G615 MC2, #17 Pixel 7 Pro G710, #19 G615 MC6, all 0.2)
  show 4-5 submissions a frame and 4-10 ms of draw preparation. The present pass is small there (0.7 ms of render-thread time). #14's first
  log ended at vkCreateSwapchainKHR failing with -1000072003 on the moto g86 (mailbox); its later log ran.
- **Mali's CPU path run on the S24** (`SMO_VK_PUSH=0 SMO_VK_VIDS=0`, test cache `SMO_VK_CACHE=../cache/vk_mtest` seeded with the spv / stage
  folders, removed afterwards; Cascade forward view, graphics_pt.ini at scale 1.00; draw preparation and fps from the last perf lines):

  | run | submits between passes / in-pass split | fps | GPU clock | draw preparation |
  |---|---|---|---|---|
  | Adreno path (baseline) | 15 / 200 | 59.7 | 310 MHz | 5.3 ms |
  | Mali path, Mali's old counts | 150 / off | 45.0 | 231 MHz | 5.75 ms |
  | Mali path | 15 / off | 47.8 | 231 MHz | 5.9 ms |
  | Mali path | 15 / 200 | 59.8 | 310 MHz | 4.8 ms |
  | Mali path, 0.2.15 rule | when the GPU has run dry | 58.4-58.9 | 310 MHz | 5.8 ms |
  | Adreno path, 0.2.15 rule | when the GPU has run dry | 59.0 | 310 MHz | 6.05 ms |

  Descriptor sets instead of push descriptors and the vertex layout in the pipeline key cost nothing that showed. The GPU waiting inside the
  big pass (and the governor's lower clock that follows) was the whole difference.
- **0.2.15 rule** (nvn_vk_draw.c gpu_starved / submit_chunk / inpass_split): the last command buffer given to the queue is checked through its
  end event. Between passes: submit after 15 draws if it has finished, at 150 anyway. Inside a pass: split after 100 draws if it has finished
  (checked every 10 draws). About 2 splits a frame in Cascade on both paths. Not run on a Mali phone: there each split stores and reloads the
  pass's images, which is why it waits for an idle GPU instead of a fixed count.
- **Test-runner note:** p46.sh RUNS lines need `SMO_GRAPHICS_CONFIG=../config/graphics_test.ini SMO_SEQ=@until:800-1400/20:Z/45` as separate
  words, or the game stays on the title menu; several env values in one quoted string become one env.txt line.

## Mali tester on 0.2.14 (2026-11-10, Xiaomi 2406APNFAG, Dimensity 8300 / Mali-G615 MC6, driver r44p1)

- **Tester's words:** no graphical bugs; "native resolution" 35-45 fps (before the update 35-42); with steady speed the counter is lower but the
  game "feels like 50-55"; the picture is blurry. Log: odyssey-logs-20261110-1105.zip (one session, handheld, scale 1.00 = 1280x720 on a
  2712x1220 screen, bilinear, AA off, diagnostics on).
- **ETC on a real Mali:** "BC textures: re-encoded on the CPU as ETC2 / EAC"; Vulkan memory 992 MB in Cascade (the Mali logs of #16 / #19 had
  about 2 GB); no visual problems reported. First real confirmation of 0.2.9's texture change.
- **Steady speed (cause of the low counter):** off: ~1 logic step a frame, 37-41 fps in Cascade (median frame 22-24 ms; the game thread waits
  ~10 ms a frame for the present, so the render / GPU side limits it). On: 1.4-2.0 steps a frame at ~11.6-13.6 ms each, 26-30 fps, full game
  speed. On the S24 steady speed costs nothing (GPU-bound with CPU to spare); here the extra calc steps take the frame. The S24's calc step is
  10-14 ms too, so the step's cost is the game's own logic, not this phone's CPU.
- **Hitches:** diagnostics = on made 14 capture rounds (every 1800 frames), worst frames 0.6-1.0 s. First start: preparation ended after 235 s
  at program 9643 ("nothing new for 15 s"; the game held 151 s, watchdog reports of 5-21 s without a frame), then 1,483 holds (58 s) and 1,384
  pipelines made during play.
- **Blurry:** handheld renders 1280x720, enlarged 2.1x to the screen with bilinear. Docked (1920x1080) or GSR would be sharper; neither measured
  on this phone.

## Mali tester on 0.2.15 (2026-11-11, same phone)

- **Tester's words (no log):** "around 40 fps fairly stable" at native handheld resolution, the same with steady speed on or off; the 30 fps
  limit holds; loading "much faster", "quite noticeable". Diagnostics were to be off.
- **Against 0.2.14:** steady speed on went from 26-30 fps to ~40 (full game speed at 40 fps means ~1.5 logic steps a frame, so the render side
  now overlaps them); off went from 37-41 to ~40 and steadier. Fits the submit-when-the-GPU-runs-dry rule (the S24's Mali-path test gave +30%),
  but one phone, by message. Why loading is faster is not measured: candidates are the earlier submissions while loading, the spin yield off,
  and diagnostics off. A log of this build would settle it.

## 0.3.1: issues 22 / 23, Mali costs (2026-11-12)

- **Issue 23** (no log): "heat on mali devices and lag when notification come". **Issue 22** (Infinix X6885, Helio G99 = MT6789,
  Mali-G57 MC2, newer driver with EDS3 / VIDS / push descriptors, Android 16, 0.3): blocky picture at resolution_scale 0.25; its log
  covers only the menus: GPU 11-12.4 ms a frame with ~2 submissions and almost no draws (the present pass at 2400x1080 is most of
  it); 30+ `nn::oe: application resumed after N ms` lines, many short (63-1217 ms) = focus losses that suspended the guest.
- **Focus:** nnmisc.c oe_window_state: on Android a window still shown counts as in focus (SMO_FOCUS_STRICT=1: before). Checked
  (run v031): `cmd statusbar expand-notifications` mid-game -> "lost the focus ... keeps running", no suspension.
- **Boost:** android_perf.c boost_gpu_bound: a 1 s window with GPU time >= 85% of the frame pauses the spin (auto and on).
- **Arm command pools:** RELEASE_RESOURCES every 64th reset per slot. S24 imitating the tester's driver (SMO_VK_PUSH=0 VIDS=0 EDS3=0,
  cache vk_mtest2, boost off, SMO_VK_CB_RELEASE=1): every reset 28.6-29.9 fps / render 20 ms; every 64th 40.1-42.5 / 17 ms;
  SMO_VK_STARVE=40,200 35.5-36.7; fixed 150 / no split 35.7-38.2 (all GPU 231 MHz, battery ~33 C). Without the release imitation
  (run mp2): 43 fps, render 15.3 ms (draw preparation 7.5), GPU 14.3, logic 9 = the tester's 0.2.15 numbers.
- **Profile of that path (mp2, symbolised with the unstripped lib via NDK simpleperf --symfs):** replay 8.5% self, draw_prepare
  53% incl. (buf_ref -> ring_put memcpy ~11%), pass_begin 18% incl. (vkCmdBeginRendering 9%), cmd_begin 11.8% (vkResetCommandPool
  11.3% on Adreno's driver, ~10 submissions a frame), cmd_submit 5.6%; calloc inside Adreno's driver ~6%.
- **Present pass:** spec constants FX / SH / SS / TX (pp_key, made lazily; SMO_PP_LEAN=0 = 0.3's shader). S24, PASSTIME frame 3900,
  plain ini (AA off, sharpen 0): 1.094 -> 0.841 ms at 231 MHz; FXAA + sharpen: 1.741 vs 1.938 at 310 (one frame, noise). Touch
  controls: 0.630 ms on vs 0.359 off at 310 (~0.15 ms of it). output_size (auto = game on non-Qualcomm with bilinear): swapchain
  sized to the picture (>= 720 rows), Android scales it; o_game 900x1950 0.616 ms @231 vs o_full 0.630 @310 (~0.85 @231). Picture,
  rotation and touch controls checked in a screenshot.
- Not run on a Mali phone: the Arm reset cost, the present-pass gains and output_size on a real HWC are expected, not measured.
- **Tester on 0.3.1** (same Xiaomi / Dimensity 8300, log odyssey-logs-20261112-0912; handheld, scale 1.00, AA off, bilinear,
  output_size auto = game: swapchain 1600x720 for a 2712x1220 window): no fps change he could see (heavy area 35-41 fps,
  as on 0.2.15); loading faster. Light area: 58.7 fps, GPU 15.7, render 3.8, logic 5.8, 3D 1280x720. Heavy area, steady
  off: frame ~26-27 ms = GPU 17.5-20, render thread 14.5-16.5, "game logic" 21-25, and the game's own cap lowered the 3D view
  to 640x360. Pairing each window with nvn_perf: logic ~= wait-present + 4-9 ms, so "game logic" counts the wait for the
  previous frame (the perf line's "likely limit: the game's logic" is wrong there; real logic ~5-8 ms a step, like the
  light area). GPU ~18 ms at 640x360 vs ~16 at 1280x720: the GPU cost barely follows the 3D size (fixed per pass / draw
  cost on Mali), so output_size (a part of the present pass) could not show. GPU and render thread don't overlap well
  (27 ms frame vs 18 / 15 ms parts). The imitation's "release every reset" cost (29 -> 41 fps) was not his phone's real
  cost (he was at ~40 already). Next: a render-thread breakdown in the log (Vulkan path; nvn_perf's columns are the GL
  path's, all 0), fix the limit label, then the Mali GPU cost per pass (load / store ops, pass count).
- **0.3.2 (diagnostic build):** `nvn_vk_rt:` line (RT_* parts in nvn_vk_draw.c, CNTVCT_EL0 clock, nested command buffer starts /
  submissions / pass ends left out of the part that contains them), GPU busy = union of command buffer spans (g_ts_busy_ns),
  logic waits (smo_t_in_calc: os_threads.c waits incl. the spin before them, nvn_gl.c gl_run / present waits). S24 Cascade, docked
  1.00, 3D 800x450: own path 59.9 fps, GPU busy 12.1-12.7 vs summed 12.8-13.2, render 8.6-9.3, logic 2.8-3.7 working + 7-9
  waiting. Mali imitation (boost off): 40.8-44.5 fps; GPU busy 11.5-12.4 vs summed 15.4-16.3 (the sum overstates by ~30% on
  that path); render 15.9-17.2 = buffers 2.1-2.4, descriptors 1.8-2.1, cmd starts 1.5-1.8, after-draw 1.4-1.7, pass begin
  1.1-1.3, rest 2.0-2.2, GPU wait 1.8-2.2; 1206 draws, 53.5 passes, 520 pipeline binds, 11-12 submissions (5.5-5.9 split a pass).
- **Tester on 0.3.2** (log odyssey-logs-20261112-1042; handheld 1.00, AA off, sharpening 60, output_size = FULL this time):
  heavy area 32-51 fps, game lowered the 3D view to 640x360. GPU busy 16-19.9 ms (summed 17-21: his GPU barely overlaps
  buffers), render thread 8-16 (GPU wait 1-3.4), logic 3.1-4.8 ms of work a step, the rest waiting. "Likely limit": the GPU, or
  mixed with the GPU largest (62-80% of the frame). GPU time hardly follows the draws: 719 draws 16.1 ms, 1528 draws 19.9 ms, so
  ~14-15 ms a frame is fixed (49-55 passes, splits 0.6-3.5). Menus: GPU 6.4 ms with output_size full vs 3.5 ms in the 0.3.1 log
  with auto (= game): the screen-size present pass costs ~3 ms on this GPU. Render thread on real Mali is smaller than the
  imitation's: buffers 1.1-2.5, after-draw 1.0-2.3, pipeline/state 0.6-1.1, descriptors 0.5-0.9, pass begin 0.5-0.9, cmd
  starts 0.15-0.34 (the 0.3.1 release fix shows), rest 0.4-3.6. Next: per-pass GPU times on his phone (SMO_VK_PASSTIME, sampled
  automatically) to find the fixed 14-15 ms; output_size auto and less sharpening for him meanwhile.
- **0.3.3 (diagnostic build):** SMO_VK_PASSTIME sampled automatically on Android every 3000 frames (PT_AUTO_EVERY; postponed
  while loading; =0 off), plus a summary by target. S24 Mali imitation frame 3000: 53 passes, 17.2 ms in passes, 17.2 between
  them (the GPU waiting for the render thread on that path), the G-buffer (800x450, 4 colour 0x3f + depth 0x34, 6 passes,
  842 draws) 12.3 ms incl. gaps; the present pass (1080x2340) 2.3 ms.
- **Tester on 0.3.3** (log odyssey-logs-20261112-1157, output_size full, sharpening 60): 4 pass samples. Frame 9000: 53 passes,
  15.2 ms in passes, 11.6 ms between them (first to last 26.8); a 0.14-0.3 ms gap before almost every pass, and one-draw passes
  into 256x256 / 2x1 targets cost 0.11-0.15 ms each. G-buffer 320x360 (5 colour + depth, 2 passes, 419 draws) 4.2 ms; present
  (1220x2712) 2.0-2.4 ms; shadow 1024x1024 1.3 ms. Log line: "split pass barriers off" on Arm since 0.1.8.
- **0.3.4:** split barriers on by default on Arm (graphics.ini pass_barriers = auto | light | full, launcher choice; env
  SMO_VK_SPLIT_BARRIER overrides). The 0.1.7 fallback turned them off for the session within a minute (vertex stage of #12190
  reads a 2x1 target; also 4x4 and 128x128 targets): now VTex.vread, and only those targets' barriers add VERTEX (+GEOMETRY).
  S24 imitation: light vs full 41-43 vs 42-48 fps (render-thread bound), gaps at frame 3000 16.0 vs 20.5 ms, screenshots equal.
- **Tester on 0.3.4** (log odyssey-logs-20261112-1325, output_size auto, sharpening 60; light barriers on in both sessions, the
  vread rule fired for 2x1 / 4x4 / 128x128 / 256x256 targets; no glitches seen; Full was not tried). Session 1 (log.prev,
  steady off, full CPU clocks): 44-47 fps, GPU busy 17.1-18.0 ms = GPU-bound, same as 0.3.2's 17-19: **no GPU gain from light
  barriers.** The per-pass gaps stayed (23.8 ms between passes vs 14.4 in them, frame 9000, 95 passes there): the gaps are mostly
  not barrier stalls; with ~100 timestamps a frame on Mali (first to last 38 ms vs 17.6 busy) the measurement itself makes much
  of them, so the gap figures are not to be trusted on Mali. Of 95 passes, 80 have 1-2 draws (128x128 x24, 256x256 x16, ...),
  73 of them LOAD their colour target. Session 2 (log.txt): 22-34 fps with GPU busy only 11-13.6, render 7-17, logic work 3-4:
  CPU clocks capped all session (cpu4 2300 instead of 3200, cpu7 400-2300 instead of 3350) with thermal "none": a system power
  mode, not the build. That is the "unstable 36 fps".
- **0.3.5 load skip (not handed out):** DONT_CARE for a colour target when the pass's first draw covers it (graphics.ini load_skip,
  launcher "Skip reloads", SMO_VK_LOADSKIP=0/1/check, SMO_VK_LOADSKIP_WHY=1). S24, Cascade, Mali imitation: **0 of ~71 colour loads
  a frame qualified** in every version of the rule. The game's full-screen draws: 3-vertex triangles reading 2 attributes (position,
  texture coordinate), or 6-index quads whose attribute-0 corners are +-0.5 (stride 32, float4): the vertex stage scales them, so
  coverage cannot be proved from the vertex data on the CPU. (Also: VStage.uses_alpha is set for every shader writing colour 0;
  the alpha test is on only when R.alpha_func is 1..7.) Without a CPU-side proof the skip would be a guess (garbage on Mali when
  wrong), so it stays unproven and gives nothing as written.

## Beta 0.3: release prep, update check, more informative logs (2026-11-11)

- **Update check (emulator odyssey_test, Android 15, started -read-only):** GitHub B0.1.0 -> GitHub B0.2 -> 0.2.1 -> 0.2.9 -> 0.2.14 each
  installed over the one before (`adb install -r`, Success, version codes 1 -> 11 -> 12 -> 23 -> 28); 0.2.14 -> 0.2.15 through a
  package-installer session (`pm install-create/-write/-commit`, the API installer apps use): Success, data kept. The Files-app tap path
  was not run (it needs "install unknown apps" granted). The tester's Xiaomi "conflict" is not reproduced and not explained by the APKs.
- **The tester's 0.2.15 log (odyssey-logs-20261111-0820.zip):** log.prev = handheld, 60 fps mode, auto scale: 59.6-59.9 fps early,
  41-46 in heavier play (draw preparation 5-7 ms, GPU waits 1.3-3 ms a frame). log.txt = docked (internal scale 1.20), cap 30, steady
  speed on: 17-25 fps with 300-900 ms hitches (pipelines being made, 2.25 logic steps a frame) and the render size dropping to 800 wide.
  That log could not say where a 38 ms median frame went: the render thread was busy ~4 ms and the logic ~10 ms, the GPU unmeasured.
- **Logs (0.3):** GPU timestamps on by default on Android (`SMO_VK_GPUTIME=0` off). A second perf line every 10 s: GPU ms per frame,
  render thread ms (and pipelines / stages made, holds), logic ms and steps, a "likely limit" guess, handheld / docked, the 3D size
  (smo_quality.c smo_render_size), steady speed, frames over 50 ms. `hitch:` lines for frames of 100 ms or more (at most one a second).
  Perf line: thermal headroom (AThermal_getThermalHeadroom, dlsym), battery temperature, GPU clock / load from kgsl, MediaTek GED or a
  devfreq node (the first perf line names which, or "not readable"). Totals: nvn_vk_draw.c nvn_vk_totals, smo_framerate.c
  smo_framerate_totals. **Checked on the S24 (runs log03, 100 s, and log03b, 45 s, Cascade, docked 100%, 60 fps mode):** GPU 3.2-3.8
  ms a frame in the menus, 4.4-16.8 in Cascade; the GPU clock and load read from kgsl, headroom 0.69-0.72; battery temperature not
  readable on Samsung (left out of the line). Hitch lines named their cause: 413 ms = render thread 370 ms with 53 holds (shaders
  being prepared), 116-121 ms = game logic (loading steps). Before the first 3D view the line says "no 3D view yet" (was 0x0).
  Not run on a Mali phone, so whether MediaTek's GED clock is readable there is unknown.
- **Version:** Beta 0.3 = versionCode 30. README: status 0.3 with both phones' results, results vary, GitHub issues only. Release notes
  draft (no Discord): ../release/RELEASE_NOTES_0.3.md.

## Vanishing map parts and first-start stutter (2026-11-10, 0.2.10-0.2.14 test builds)

- **Symptom (S24, 0.2.9, the user's play):** in Cascade at some camera angles ground and props vanished (sky visible inside their outline, like a
  mirror) and a yellow-white glow with black / blue speckles grew near them. Not reproduced by a scripted camera sweep at the start of Cascade.
- **Bisect in the user's play (env.txt pushed over adb, same spots each time):** fast min / max off (0.2.10): no change. Fresh shader cache
  (SMO_VK_CACHE=../cache/fresh): no change. SPLIT_PASS + STATECACHE + DTEMPLATE + EARLYREAD + LCKEEP + CLEARCARRY off: fixed. SPLIT_PASS off
  alone: not fixed. STATECACHE + DTEMPLATE off: fixed. STATECACHE off alone: fixed. 0.2.12 (disturbed descriptor sets): not fixed. Mask 14
  (descriptor and push-constant parts off): not fixed. Mask 114 (vertex input and vertex buffer parts off): fixed. 0.2.14 (pchg rule): fixed.
- **Cause and fix:** draw_prepare kept vkCmdSetVertexInputEXT / vkCmdBindVertexBuffers across a vkCmdBindPipeline when they matched the draw
  before. Spec-legal, but Qualcomm's driver (Adreno 750, driver 0x802fa029) loses them at the bind. `pchg` sets both again after every pipeline
  change. Which of the two the driver drops was not narrowed further. Not checked on Mali (the rule applies everywhere).
- **First-start preparation never ran again after 0.2.5 / 0.2.7:** cache/vk/first_prep_done.txt (2026-11-07) counted for good, so later
  translation changes were translated during play: 0.2.9 session 441 game holds (12.6 s), 0.2.10 1,453 (43 s), plus 700-900 pipelines made
  during play. The marker is now first_prep_done_<hash of the translation settings>.txt (first_prep_marker); checked: the bar shows on a fresh
  cache and finished in 215 s on the S24. The pipelines made during play remain (next stutter work).
- The user's cache/vk had grown to 11 GB (every test build's translations).

## Issues: BC as ETC2 on Mali, an ARMv8.0 build (2026-11-10, 0.2.9 test build)

- **BC -> ETC2 / EAC (tex_etc.c, TEX_DEC_ETC).** Every Mali log in the issues (#11, #16, #17, #19) says "BC textures: decoded on the CPU": RGBA8 for
  BC1-3/7, R8 / RG8 for BC4/5. #16 (Moto G86, Mali-G615 MC2, 8 GB) reached 2,012 MB of Vulkan memory and 3.6 GB peak working set, then the
  log ends (no crash line, logcat empty: most likely the low-memory killer). At the Joy-Con screen: 497 uploads, 566 MB on Mali against 129 MB
  on the S24. tex_vkfmt maps BC to ETC2 RGB / RGB A1 / RGBA8 and EAC R11 / RG11 (unorm, snorm, sRGB kept) when textureCompressionETC2 and
  linear filtering of those formats are there; nvn_front_tex_read re-encodes each slice into blocks of the same size (vt->bpb / bw / bh stay
  the BC ones, so vt_put copies blocks). BC6H keeps RGBA16F. `SMO_VK_BC_ETC=0` = the RGBA8 path; `SMO_ETC_THREADS=1..4` (default 4 for 512x512+).
- **Encoder.** ETC1 individual / differential in both flips (individual only when differential had to clamp the colour difference), table
  chosen on the luminance form of the error (q - 2md + 3m^2), exact error for the chosen one, one refit of the base colours. EAC: per table
  the multiplier spanning the block's range (and the next), base centred, nearest of 8 by a binary search on midpoints. No T, H or planar mode.
  Test program (scratchpad etctest/etc_test.c, run on the phone): with a reference decoder written separately from the Khronos spec, no
  invalid blocks, punch-through alpha exact; gradient 41.7 dB, EAC 52-60 dB; a harsh synthetic image with hard colour edges 25 dB (ETC1's
  limit: one colour per half block). Speed on one S24 big core (clocks unsteady, phone locked): RGB about 30 Mpixel/s, R11 about 16.
- **In the game (S24, SMO_VK_NO_BC=1 to imitate Mali, Cascade forward view, runs etcA / etcB / etcC):** Vulkan memory 1,376 MB (BC read by the
  GPU) / 1,312 MB (ETC) / 2,374 MB (RGBA8); uploads 1,581 / 1,451 / 2,454 MB; fps 58-60 in all three (scale 0.90 from the phone's ini, GPU 310
  MHz); worst frames while loading 0.2-0.4 s in all three. Crops of the cliff side by side: no visible difference.
- **#9, ARMv8.0 (SIGILL).** `SMO_ARCH=armv8.0` (android_env.py): -march=armv8-a -moutline-atomics -mtune=cortex-a53, folders
  build/android/arm64-v8a-v80, APK odyssey-<ver>-armv8.0.apk; build_apk writes assets/arch.txt; the launcher's Play checks /proc/cpuinfo
  (every Features line has "atomics") and on an ARMv8.0 phone with the normal APK shows a dialog naming the armv8.0 APK. Game compile 257
  objects (about 15 min), runtime 30 s. Disassembly scan (scratchpad scan_v81.sh: LSE, RDMA, LOR, dot product, fp16 arithmetic, RCpc,
  complex, PAC loads): normal lib 2,997 such instructions, armv8.0 lib 0 outside the __aarch64_* helpers (which check the processor); the
  adrenotools hooks 0. Not run on an ARMv8.0 phone.
- **First start.** Cold profile (run cold1, S24, boost on): the render and main threads spin in clock_gettime (boost), the workers about half
  the sampled time; thread states (cold2): 4.3 of 6 workers running on average. 15,546 programs and 6,090 stages in about 50 s. No lock to fix;
  glslang (parse, symbol maps, scudo malloc) is most of a stage's cost.
- **Against 0.2 (runs ab02w / ab02 / ab29, p43.sh):** odyssey-0.2.apk re-signed with version code 22 (reversion02.py: 0.2's manifest from git HEAD, its dex and libs
  unchanged), cache ../cache/vk02 warmed for 420 s, then 120 s cooled runs, SMO_GRAPHICS_CONFIG = the phone's ini at scale 1.00, forward view:
  0.2 43.8-44.9 fps at 310 MHz (35.4-36.4 at 231 MHz in the warm run), 0.2.9 58.1-59.5 at 310 MHz. 0.2.9 is code 23 so it installs over that.
- **Test runs:** the phone stays awake on USB now (the user asked; `svc power stayon usb`), the runners no longer turn it off.

## Open problems, most important first

0. **Post effects vanish after dying: fixed on Adreno in 0.1.4, Mali not tested.** On the user's S24 (Adreno): when Mario
   died the shading, bloom and lighting disappeared until the game was paused and unpaused after the respawn. Pausing at
   that moment showed a black screen where the blurred background belongs. Some users, probably on Mali, are reported to
   have the effects gone for good, cutscenes included.
   - 0.1.3's sampler fix did not cure it (the user tested).
   - Cause, from captures of the same view with the effects off and on: the same draws, but the G-buffer's colour target
     0 black on all geometry. After a respawn the game makes new texture objects over the same memory, same format and
     size (10 cube maps of 128x128 RGBA8, some others), and does not render them again until the pause menu does. On the
     console the contents are that memory; the port kept them in the old Tex's image, so the new ones read as zero.
   - Fix: `runtime/nvn_vk_draw.c`, the note above `vk_drop`. The image of a GPU-written texture waits in `g_keep` (at most
     128 images and 64 MB, oldest out first, no screen-sized targets) and `vt_carry` copies it into the next image at the
     same memory and shape. A later CPU write still wins. `SMO_VK_KEEPTEX=0` turns it off.
   - Tested: the user died on the S24 with 0.1.4 and the effects stayed on; the log shows the cube maps carried over.
     Emulator, Cap Kingdom, 2 minutes: no Vulkan errors, 32 carries of other textures in normal play, no picture comparison.
   - Not established: whether this is the cause of the permanent loss on Mali, or of the Mali spots.

1. **BC textures on phones without BC (Mali, PowerVR): decoded on the CPU since 2026-10-28.**
   - Output formats: BC1–3/7 → RGBA8, BC4 → R8, BC5 → RG8, BC6H → RGBA16F.
   - Tested on the emulator with `SMO_VK_NO_BC=1` against its GPU's own BC decoding (`tools/android/run_shots.sh`):
     the boot and menu frames match within ±5 levels; the only other difference was an animation phase.
   - Formats covered by that test: BC1/BC3/BC7 sRGB and BC4/BC5 unorm and snorm. BC6H is not tested yet.
   - Costs:
     - Texture memory about doubles (Vulkan memory 464 → 912 MB on the menus).
     - Decoding runs on the render thread when a texture is first used, which can cause hitches.
   - Possible improvements: decode on worker threads during loading screens, and transcode to ASTC/ETC2 to save memory.
   - Adreno GPUs usually read BC themselves.
2. **Some UI colours red/blue swapped on the emulator:** the teal panel, the orange step numbers. Neither BGRA
   textures nor vertex formats are the cause. The suspect is component swizzles on image views in gfxstream. Check on
   a real phone first.
3. **Frame rate on the emulator is set by its display path, not the game.**
   - When the picture is shown the right way up, each present costs about 110 ms (7–9 fps). This holds whether the
     compositor rotates the picture or the new pre-rotated present pass does.
   - The only fast configuration (present about 1.3 ms, 55–60 fps) shows the picture rotated wrongly
     (`SMO_VK_ROTATE_COMPOSITOR=1` with the old blit).
   - Rendering itself takes about 5 ms per frame. Next step: measure on a real phone (S24 Ultra).
4. **First-start shader work.** About 10,000 variants are translated and compiled; on the emulator this takes several
   minutes, at about 7 fps. Options:
   - prepare the shaders on the PC (`--prepare-shaders`) and push them with the game files;
   - give the container scan a lower priority on phones;
   - offer `shader_wait = skip`.
5. Touch overlay drawing: input mapping exists (`nnpad_android.c`) but nothing is drawn yet (`android_touch_shapes` is
   ready for a blended pass).
6. A launcher / settings screen for `graphics.ini` and `controls.ini`. Rumble, movies (MediaCodec), the Help web view.
7. The firmware's shared fonts. The device's fonts stand in, so the Nintendo button-symbol glyphs are missing.
8. The Windows build has not been rebuilt since the shared runtime changes.

## Legal and data handling

- No game data, keys, or anything derived from the dump is committed (`build/` is gitignored).
- The APK holds no game data or keys, but its `libsmo.so` is the game's own code lifted from the dump, so an APK
  built this way is the builder's own copy, like their game files.
- Key values were never printed.
- The game files go from the user's PC only to the user's own device.

### 0.5.5: widescreen HUD at the edges, findings (2026-11-21)
- Test 1 (SMO_WS_HUD=1 in config/env.txt): each 16:9 layout's "RootPane" (1280x720) widened to the window's shape in nn::ui2d::Pane::Pane
  [0xb07654, wrap]. Only 3 roots were caught in a stage, and the user saw no change in the coins or health. The Pane ctor is also
  called from inside c_00eb.c (Parts, other pane kinds), which a link wrap cannot catch, and some layouts may be built before
  the scene asks its aspect (g_ws_active still 0).
- The SMO ultrawide pchtxt in Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods (no license; read only, nothing copied) does:
  getSceneFrameBufferMainAspect [0x9cf324] returns 1720/720, and inside al::initDrawInfoDefault(DrawInfo *, GraphicsResource *)
  [0xa63c70] the calls to al::getLayoutDisplayWidth [0x9d142c, returns 1280] / Height [0x9d1434, 720] answer 1720 / 720: the
  layouts' orthographic projection gets wider, so the HUD keeps its shape in the middle. That is what our SetProjectionMtx squeeze
  already does; it does not move the HUD to the edges. Its other part scales the BootLoading layout's PicJoyCo / PicOther panes'
  width by 0.75 (stretched on the boot screen in that mod; we do not widen the boot screen).
- So moving the coins and health needs a per-element step. Next: a pane log (name, anchor byte, place) to find the HUD parts.
- Dropped (user, 2026-11-21: "not much of a problem"): the HUD stays undistorted in the middle 16:9. The SMO_WS_HUD test code was
  removed; to try again, the per-element route starts from a pane log in nn::ui2d::Pane::Pane (res+12 name, res+44 translate,
  res+76 size, res+9 base position) and needs a hook that also covers the in-object calls in c_00eb.c.
