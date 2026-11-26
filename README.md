the one official source of this port, anywhere else is NOT done by me
# SMO Android Port

*Super Mario Odyssey* (version 1.0.0) running natively on Android, built on the static recompilation from
[smo-recomp](https://github.com/Coz-0/smo-recomp).

The game runs natively. Its ARM64 code is translated to C ahead of time and compiled for Android with the NDK, with no CPU interpreter, JIT or compatibility layer in between, and this project's runtime takes care of the rest: the console's system libraries, NVN graphics (on Vulkan), shaders, audio, input and the file system.

There's no game code, no game assets, no keys and no firmware in this repo. You need your own copy of the game and keys dumped from your own console.

Made with AI. Most of the code was written by Claude, Anthropic's AI assistant (through Claude Code), with the project's creator directing it, reviewing it and testing it on a real phone. Expect some rough edges, and if something breaks, please send logs (see below).

Community: join the [Discord server](https://discord.gg/b9GN2Ypktw) to talk about the port, get help and share how it runs on your phone.

## Status: beta 0.6

The game boots, plays and saves. Here's how it runs on the dev's phone, on the phone's own driver.

Galaxy S24 Ultra (Snapdragon 8 Gen 3 / Adreno 750): about 58 to 60 fps in Cascade at 100% scale.

More device benchmarks will be added here as more logs come in.

Your results will likely be different. Speed depends a lot on the phone's chip, its GPU driver, how warm it gets and your settings, so we'd love to hear how yours does. Custom drivers are optional. The first start builds the graphics for your phone behind a progress bar, which takes a few minutes, but only once (and again after an update that changes the shaders). When you update, install the new APK over the old one. Uninstalling deletes those built graphics and your game files, and the next start has to build everything again.

New in 0.6: two players (the touch controls as player 2 beside a controller), widescreen on by default, fewer loading stutters on Mali and PowerVR phones, fixes for a black picture on some Mali phones and a black screen at start, and a check for damaged game dumps at import. The full list is in [CHANGELOG.md](CHANGELOG.md).

## What you get

Game. Import your own game right in the app: your `prod.keys`, then your NSP or XCI of 1.0.0 (base game, no update). The keys stay in the app's private storage until you remove them. Steady game speed keeps the game at its normal speed even below 60 fps, and the FPS limit goes from 20 to 60. Saves can be exported and imported as a .zip (your current save is kept as a backup). RomFS mods can be turned on and off in the app.

Graphics. A Vulkan 1.3 renderer with docked (1080p) or handheld (720p) resolution, scale presets or your own resolution scale, upscalers (bilinear, bicubic and more) for any scale under 100%, FXAA, sharpening and anisotropic filtering, plus an FPS counter and a performance overlay (average fps, 1% low, frame time). On phones wider than 16:9 the game fills the screen (widescreen, on by default). It uses your phone's own Vulkan driver by default. On Snapdragon you can also load custom AdrenoTools driver packages (like Mesa Turnip).

Frame generation (beta). One extra frame is made after each real one, so the game shows 60 frames a second while running at 30. It looks smoother, and the controls react like at 30 fps. Set it to Off, On or Auto in the app or the in-game menu. Auto turns it on only while your phone can't hold 60 but can keep up with frame generation. Quality High gives Mario, enemies and other moving objects their own motion, and Fast follows only the camera for slower GPUs.

CPU boost. Keeps the phone's CPU clocks up so heavy scenes hold the frame rate, set to Auto, Always or Off. It does what you set, and the app shows a note about its heat and battery cost while it is on.

Controls. On-screen touch controls with Switch-style button icons, and a layout editor to move and resize every button and stick. Any controller Android sees as a gamepad works, with button rebinding in the app. The house button on the touch controls (or the controller's Home button, rebindable) opens the in-game menu, where you can change settings while the game is paused, restart, or exit. Exit waits for any save in progress, then takes you back to the app's menu. Motion controls use your phone's sensors, and the sensors stay off while motion controls are off. For the game's two-player mode, the touch controls can be player 2 (Cappy) while a controller is player 1 (Mario), or two controllers can play.

When something goes wrong. Advanced, then Export logs, makes a .zip with the game's logs, a crash log, your settings and your phone model. Every 10 seconds the log notes the fps, 1% low, CPU clocks, heat, frame generation's state, and where each frame's time went (GPU, game logic, shader building), and it also notes your phone's RAM and memory use, so a slowdown or a stutter on your phone can be tracked down. No saves, keys or game files in there.

## What you need

Android 11 or newer on 64-bit ARM. Phones with an ARMv8.2 processor or newer use the main APK, and older ARMv8.0 or 8.1 phones (like Snapdragon 680 / 665 / 662) use the `-armv8.0` APK. The app tells you if you need it.

Vulkan 1.3 (the app warns you if your phone's driver is older), about 8 GB of free space for the game files, and your own *Super Mario Odyssey* 1.0.0 dump and `prod.keys` from your own console.

## Getting started

Install the APK and open it. On the Game tab, use Import prod.keys, then Import from NSP / XCI. Then press Play.

The settings live on the Graphics, Controls, Advanced and Mods tabs.

## Building it yourself

The full steps are in [BUILDING.md](BUILDING.md). In short: extract your dump with `scripts/setup.py` (your own keys and game), lift the game's code to C with `tools/lifter` using `SMO_LAYOUT=android`, then compile and package it with `tools/android/build_game.py`, `build_runtime.py` and `build_apk.py` (`--release` gives you a signed release APK). Set `SMO_ARCH=armv8.0` for the ARMv8.0 build.

The lifted code is made from your own copy of the game, so an APK built this way contains code derived from the game.

## Found a problem?

Please report it here on GitHub: open an [issue](https://github.com/Deez-coder/SMO-android-port/issues) with your phone model, what happened, and the .zip from Advanced, Export logs. Reports from phones other than the S24 Ultra help the most, especially Mali and Exynos ones.

You can also ask for help on the [Discord server](https://discord.gg/b9GN2Ypktw).

## Notice

SMO Android Port is made by holdup.lethimcook (quandaile on Reddit), with code written by Claude (Anthropic's AI assistant) under their direction. It builds on the static recompilation from [smo-recomp](https://github.com/Coz-0/smo-recomp) by Coz-0.

The source is licensed under GPL-3.0-or-later. If you copy, fork or redistribute this project or a modified version of it, you must keep this notice and the license, say that your version is based on SMO Android Port (with a link to this repository), and mark what you changed.

The official source is https://github.com/Deez-coder/SMO-android-port. Builds from anywhere else are not made by us.

## Legal

This project is not affiliated with, endorsed or sponsored by Nintendo. *Super Mario Odyssey* and *Nintendo Switch* are trademarks of Nintendo and are named only to say what this software works with. This repository contains only source code and documentation under GPL-3.0-or-later (see [LICENSE](LICENSE)). It contains no game code, binaries, assets, keys or firmware, and it does not help anyone obtain them. You must own the game and dump it yourself.

## Credits

Big thanks to everyone here.

holdup.lethimcook (Discord), quandaile (reddit): creator of the Android port. Direction, design, testing and play-testing.

u/CAPELINI_W (reddit): extensive testing on Mali, and patience with me.

Claude (Anthropic's AI assistant, used through Claude Code): co-developer. Wrote much of the Android port's code (the Android host, the Vulkan renderer's work for phones, the launcher, frame generation, the graphics options) and the measurements in PROGRESS.md.

[smo-recomp](https://github.com/Coz-0/smo-recomp) by Coz-0: the static recompilation of Super Mario Odyssey (the lifter, the runtime, the NVN-to-Vulkan renderer and the shader translator) that this port is built on.

[ssbu-recomp](https://github.com/Denkishi/ssbu-recomp) by Denkishi: the design and runtime smo-recomp builds on.

[SuperMarioWonderRecomp](https://github.com/hesanaltdoesgithub/SuperMarioWonderRecomp): the reference for the Android host (NativeActivity, a fixed guest mapping, an APK built without Gradle).

[LAS Recomp](https://github.com/peiikai/LASRecomp) by peiikai (MIT): read for its phone GPU findings. Chunked submissions and the split pass barriers come from there.

[Power Moon Recomp](https://gitlab.com/zzzarathustra-dev/power-moon-recomp) by u/zzzarathustra-dev (MIT): the optimizations and findings borrowed for this port, including the strict vertex math and the game's GPU timers in Switch ticks. Licence in `docs/licenses/`.

[Super Odyssey PC Recomp](https://github.com/Simon3Sarkis/super-odyssey-pc-recomp) (MIT, by its contributors and those of LAS Recomp and Power Moon Recomp): frame generation's camera motion vectors follow its camera reading and motion vector shader.

[nxnative](https://github.com/seedds/nxnative): its issue tracker explained the see-through triangle in the Cascade waterfall mist. Read for ideas only, none of its code is included.

[OdysseyDecomp](https://github.com/MonsterDruide1/OdysseyDecomp) by MonsterDruide1 and contributors: the decompilation project, used as a reference only. None of its code is included.

[libadrenotools](https://github.com/bylaws/libadrenotools) by bylaws (BSD-2-Clause): loads custom Vulkan drivers.

[Mesa](https://mesa3d.org/)'s Turnip driver and K11MCH1's [AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers) builds: the custom driver tested.

The Eden, yuzu/suyu and Ryujinx emulators: their sources and documentation were consulted for console behaviour, Maxwell shader instructions and how emulators handle drivers, pipelines and resolution. Where used, they are cited in the code, and their code is not copied.

[shaderc / glslang](https://github.com/google/shaderc) (Google, Khronos): GLSL to SPIR-V on the phone.

[stb_truetype](https://github.com/nothings/stb) by Sean Barrett: font rendering.

[Unicorn](https://www.unicorn-engine.org/) and [Capstone](https://www.capstone-engine.org/): reference CPU emulation and disassembly for testing the lifter.

[SDL3](https://github.com/libsdl-org/SDL): controllers in the Windows build.

FXAA after Timothy Lottes (NVIDIA). The sharpening follows the idea of AMD FidelityFX Contrast Adaptive Sharpening.

The Android NDK and SDK, and the Vulkan SDK.

The Windows project's own README is kept in [docs/PC_README.md](docs/PC_README.md).
