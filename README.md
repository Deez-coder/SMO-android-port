the one official source of this port, anywhere else is NOT done by me
# SMO Android Port

*Super Mario Odyssey* (version 1.0.0) running natively on Android, built on the static recompilation from
[smo-recomp](https://github.com/Coz-0/smo-recomp).

And no, it's **not an emulator**. No CPU interpreter, no JIT, no compatibility layer. The game's ARM64 code gets translated
to C ahead of time and compiled for Android with the NDK, and this project's runtime takes care of the rest: the console's
system libraries, NVN graphics (on Vulkan), shaders, audio, input and the file system.

> **There's no game code, no game assets, no keys and no firmware in this repo.**
> You need your own copy of the game and keys dumped from your own console.

> **Made with AI.** Most of the code was written by Claude, Anthropic's AI assistant (through Claude Code), with the
> project's creator directing it, reviewing it and testing it on a real phone. Expect some rough edges, and if something
> breaks, please send logs (see below).

## Status: beta 0.3

The game boots, plays and saves! Here's how it runs on the two phones we've measured, both on the phone's own driver:

- **Galaxy S24 Ultra** (the dev's phone, Snapdragon 8 Gen 3 / Adreno 750): about 58–60 fps in Cascade at 100% scale, up
  from about 44 in beta 0.2.
- **Xiaomi 2406APNFAG** (u/CAPELINI_W's phone, MediaTek Dimensity 8300 / Mali-G615 MC6): about 40 fps, fairly stable, at
  handheld resolution, with no graphical issues. A 30 fps limit holds steady.

**Your results will likely be different.** Speed depends a lot on the phone's chip, its GPU driver, how warm it gets and
your settings. Other phones are untested, so we'd love to hear how yours does. Custom drivers are optional. The first
start builds the graphics for your phone, which takes a few minutes, but only once (and again after an update that
changes the shaders).

## What you get

**Game**
- Import your own game right in the app: your `prod.keys`, then your NSP or XCI of 1.0.0 (base game, no update). The keys
  stay in the app's private storage until you remove them.
- Steady game speed: the game keeps its normal speed even below 60 fps. FPS limit from 20 to 60.
- Save export and import as a .zip (your current save is kept as a backup).
- RomFS mods you can turn on and off in the app.

**Graphics**
- Vulkan 1.3 renderer.
- Docked (1080p) or handheld (720p) resolution, scale presets, or your own resolution scale.
- Upscalers (bilinear, bicubic and more) for any scale under 100%.
- FXAA, sharpening, anisotropic filtering.
- FPS counter and performance overlay (average fps, 1% low, frame time).
- Uses your phone's own Vulkan driver by default. On Snapdragon you can also load custom AdrenoTools driver packages (like
  Mesa Turnip).

**Controls**
- On-screen touch controls with Switch-style button icons, plus a layout editor to move and resize every button and stick.
- Any controller Android sees as a gamepad, with button rebinding in the app.
- The house button on the touch controls (or the controller's Home button, rebindable) opens the in-game menu: change
  settings while the game is paused, restart, or exit. Exit waits for any save in progress, then takes you back to the
  app's menu.
- Motion controls through your phone's sensors.

**When something goes wrong**
- **Advanced → Export logs** makes a .zip with the game's logs, a crash log, your settings and your phone model. Every
  10 seconds the log notes the fps, 1% low, CPU clocks, heat, and where each frame's time went (GPU, game logic,
  shader building), so a slowdown or a stutter on your phone can be tracked down. No saves, keys or game files in there.

## What you need

- Android 11 or newer, 64-bit ARM (older ARMv8.0 phones, like Snapdragon 680 / 665 / 662, use the `-armv8.0` APK; the
  app tells you if you need it)
- Vulkan 1.3
- About 8 GB of free space for the game files
- Your own *Super Mario Odyssey* 1.0.0 dump and `prod.keys` from your own console

## Getting started

1. Install the APK and open it.
2. **Game** tab: **Import prod.keys…**, then **Import from NSP / XCI…**.
3. Press **Play**. That's it!

The settings live on the **Graphics**, **Controls**, **Advanced** and **Mods** tabs.

## Building it yourself

The full steps are in [BUILDING.md](BUILDING.md). The short version:

1. Extract your dump with `scripts/setup.py` (your own keys and game).
2. Lift the game's code to C with `tools/lifter`, using `SMO_LAYOUT=android`.
3. Compile and package it with `tools/android/build_game.py`, `build_runtime.py` and `build_apk.py` (`--release` gives
   you a signed release APK).

The lifted code is made from your own copy of the game, so an APK built this way contains code derived from the game.

## Found a problem?

Please report it here on GitHub: open an [issue](https://github.com/Deez-coder/SMO-android-port/issues) with your phone
model, what happened, and the .zip from **Advanced → Export logs**. Reports from phones other than the S24 Ultra help the
most, especially Mali ones.

## Legal

This project is not affiliated with, endorsed or sponsored by Nintendo. *Super Mario Odyssey* and *Nintendo Switch*
are trademarks of Nintendo and are named only to say what this software works with. This repository contains only
source code and documentation under GPL-3.0-or-later (see [LICENSE](LICENSE)). It contains no game code, binaries,
assets, keys or firmware, and it does not help anyone obtain them. You must own the game and dump it yourself.

## Credits

Big thanks to everyone here:

- **holdup.lethimcook** (Discord),quandaile (reddit): creator of the Android port. Direction, design, testing and play-testing.
- **u/CAPELINI_W** (reddit): extensive testing on Mali, and patience with me.
- **Claude** (Anthropic's AI assistant, used through Claude Code): co-developer. Wrote much of the Android port's code
  (the Android host, the Vulkan renderer's work for phones, the launcher, the 30 fps mode, the graphics options) and
  the measurements in PROGRESS.md.
- [smo-recomp](https://github.com/Coz-0/smo-recomp) by Coz-0: the static recompilation of Super Mario Odyssey (the
  lifter, the runtime, the NVN-to-Vulkan renderer and the shader translator) that this port is built on.
- [ssbu-recomp](https://github.com/Denkishi/ssbu-recomp) by Denkishi: the design and runtime smo-recomp builds on.
- [SuperMarioWonderRecomp](https://github.com/hesanaltdoesgithub/SuperMarioWonderRecomp): the reference for the
  Android host (NativeActivity, a fixed guest mapping, an APK built without Gradle).
- [LAS Recomp](https://github.com/peiikai/LASRecomp) by peiikai (MIT): read for its phone GPU findings. Chunked
  submissions and the split pass barriers come from there.
- [Power Moon Recomp](https://gitlab.com/zzzarathustra-dev/power-moon-recomp) by u/zzzarathustra-dev (MIT): the
  optimizations and findings borrowed for this port, including the strict vertex math and the game's GPU timers in Switch
  ticks. Licence in `docs/licenses/`.
- [OdysseyDecomp](https://github.com/MonsterDruide1/OdysseyDecomp) by MonsterDruide1 and contributors: the decompilation
  project, used as a reference only. None of its code is included.
- [libadrenotools](https://github.com/bylaws/libadrenotools) by bylaws (BSD-2-Clause): loads custom Vulkan drivers.
- [Mesa](https://mesa3d.org/)'s Turnip driver and K11MCH1's
  [AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers) builds: the custom driver tested.
- The Eden, yuzu/suyu and Ryujinx emulators: their sources and documentation were consulted for console behaviour,
  Maxwell shader instructions and how emulators handle drivers, pipelines and resolution. Where used, they are cited in
  the code; their code is not copied.
- [shaderc / glslang](https://github.com/google/shaderc) (Google, Khronos): GLSL to SPIR-V on the phone.
- [stb_truetype](https://github.com/nothings/stb) by Sean Barrett: font rendering.
- [Unicorn](https://www.unicorn-engine.org/) and [Capstone](https://www.capstone-engine.org/): reference CPU emulation
  and disassembly for testing the lifter.
- [SDL3](https://github.com/libsdl-org/SDL): controllers in the Windows build.
- FXAA after Timothy Lottes (NVIDIA). The sharpening follows the idea of AMD FidelityFX Contrast Adaptive Sharpening.
- The Android NDK and SDK, and the Vulkan SDK.

The Windows project's own README is kept in [docs/PC_README.md](docs/PC_README.md).
