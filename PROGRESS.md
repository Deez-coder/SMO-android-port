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
