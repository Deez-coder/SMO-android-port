# Changelog

Every build handed out gets its own number (`android:versionName` in `android/AndroidManifest.xml`, shown on the
launcher and written into exported logs). The APK is named after it: `odyssey-<version>.apk`.

## Beta 0.4

The public release after 0.3 (version code 52). It holds everything in the test builds 0.3.1 to 0.3.21 below, plus:

**More in the logs** (no cost per frame: read once every 10 s or 300 frames). Every perf line now ends with frame generation's state (off, on, or auto and what Auto decided; quality; how many objects were drawn again and the budget's limit). A second memory line every 300 frames: the GPU memory the driver reports for the whole process (VK_EXT_memory_budget; not offered by the S24's driver) and the phone's RAM and how much is available. At the start: the phone's RAM; the low-memory note now says how much is available. The time budget's lines say how many real frames were late. The quality is logged when it is set or changed.

**CPU boost does what the player set, with a warning under it.** The pause of 0.3.1 (issue 23: the spin stopped while the GPU was the limit, in Auto and Always) is now off by default (`SMO_BOOST_GPU_CHECK=1` turns it on again): the user wants the setting obeyed. While boost is on (Auto or Always) the launcher's Graphics page shows a note: it raises the frame rate, the phone gets warmer and the battery drains faster, and if it gives no noticeable boost on the phone it might not be worth turning on.

**Log: BC formats one by one on GPUs without BC.** A tester's Galaxy S24 with the Exynos 2400 (Xclipse 940, Android 17, public 0.3) had every BC texture re-encoded to ETC2 on the CPU (the driver reports the BC feature off) and three 5 s freezes at level loads with the world loader registering textures; other ports note the Xclipse lacks only BC6H / BC7. The log now lists which BC formats the driver offers one by one, so a next build can read those natively. A log line at start only; not run on a phone yet (built while the S24 was disconnected).

**The object budget cuts only while real frames are late.** In the 0.3.21 build the S24 went from 64 to 36 objects at a replay cost of 2.8-3.5 ms with every real frame on time. Now fewer only while more than 5% of real frames are late and the cost is over the 3 ms budget, or the cost is over 6 ms; more again while real frames are on time. S24: 55-64 objects (as many as there were) with 98% of real frames on time; at resolution 1.70 it kept 57-64 (the objects cost 1.6 ms there; the GPU as a whole was the limit).

## Beta 0.3.21 (test build)

**Frame generation settings** (launcher Graphics page and in-game menu, live): **Off / On / Auto** and **quality High / Fast** (graphics.ini `frame_generation = off | on | auto`, `frame_generation_quality = high | fast`).
- Auto turns it on only while the game cannot hold 60 but a real and a made frame fit (5 one-second windows in a row at about 37-54 fps, and the GPU time of a real frame plus frame generation's own under 30 ms: measured once it has run, estimated from the render size before), and off again when it cannot keep up (under 85% of real frames on time for 4 s, under 50% for 2 s). It then waits 30 s before trying again, doubled up to 8 minutes when it failed again within a minute. Each switch is in log.txt (`nvn_vk_fg: auto`). Tested on the S24: stays off where 60 holds; at resolution 1.70 (47-52 fps) it came on, saw the real frames late and went off; switching on costs one hitch the first time (~160 ms: its shaders are made and the swapchain is made again for FIFO).
- Fast: made frames follow the camera only (no moving objects drawn again, no forward pass, no copy of the last real frame): S24 GPU 3.9 ms a pair instead of 8.0 (warp 0.82 ms instead of 2.35), real frames 100% on time. Meant for slower GPUs (the Helio G99 tester: warp 10.5 ms, per-object motion 5.7-6.7 ms of the render thread at High).
- **Moving objects by a time budget**: the number drawn again follows what the replay costs (render thread + GPU, 3 ms a frame; `SMO_VK_MO_BUDGET_US`, 0 = the fixed 64 as before), at most 64, at least 8 (nearest first). On the S24 it stays at 64; with a 1 ms budget it settled at 8-15 objects and their GPU time went 2.2 -> 1.0 ms.

**RAM: about 570 MB less GPU memory.** Each of the 16 command-buffer slots kept a descriptor pool made for 8192 sets while a command buffer takes ~30-200, and the driver keeps all of it. Pools are now 8 times smaller; when one runs out the next is made, as before (`SMO_VK_DPOOL_DIV`, 1 = as before). Measured on the S24 (Cascade gameplay, `dumpsys gpu --gpumem`): the game's GPU memory 2312 MB before, 1744 MB with the change in the same run order (1487 MB in the final check), fps unchanged (59.6-59.8); Mali's path (1809 MB) and frame generation (1825 MB) ran without errors. Releasing the command pools' memory instead saved nothing and cost fps, so it is not used. Reported: a phone with 8 GB had the system UI killed while playing.

**Videos decode on a thread of their own (single-digit fps while a tutorial clip played).** The game's thread that reads the video used to decode every frame itself, waiting on the phone's decoder in 2-5 ms steps. The throw-Cappy clip (480x544, 1.8 s) loops until Cappy is thrown, and every loop decoded it again: on a Xiaomi (SM8735 / Adreno 825, Android 16) the game's logic steps went from 3 ms to 30-57 ms while it played. A decoding thread now keeps each track a few frames ahead (about 16 MB of frames); the game's thread only decodes itself when nothing is ready. The "end of stream" log line now says how often the game still had to wait and for how long. `SMO_MOVIE_PREFETCH=0`: as before. Checked with a self-test (`SMO_MOVIE_SELFTEST=1`: 15 s after the start the throw-Cappy clip goes through the same extractor code, 4 passes with a seek back to the start each, then the extractor is destroyed) on the S24: all 55 samples every pass, the reader found 54 ready and waited once per pass (the first frame after the seek, 7.5-10.7 ms; 89 ms the very first time), against 13-20 waits and 85-100 ms per pass without the decoding thread; the thread ends cleanly. Not yet seen in the game's own tutorial clip (the Action Guide in the pause menu closes at once and opens no video; left for later).

**Battery: motion sensors only when used.** The accelerometer and gyroscope sent 200 readings a second each whenever the game had the focus, with motion controls on or off, and every reading woke a thread; the game keeps only the latest reading, once a frame. Now they are off while motion controls are off (launcher, controls.ini `motion = off`) and read 100 times a second when on (`SMO_SENSOR_US` to change).

**CPU boost: the launcher says what each choice costs** (Always: most fps, warmest, most battery; Auto: only while frames are late; Off: cooler, longer battery, fewer fps in heavy scenes). Defaults unchanged. Measured why there is no cheaper boost (S24, Cascade, ~2.5 min each from cool): the heat comes from the high clocks the spin buys, which are also the fps. A lower-power spin (ISB, `SMO_SPIN_PAUSE=1`) gave the same fps and the same heat in a repeat (+11.9 vs +12.3 C); WFE (`=2`) lost fps (57-59.6, the clocks jumped); spinning fewer threads (`SMO_SPIN_HOT_MASK`, 1 render, 2 presenting, 4 window sync; default 7) saved nothing, and the render thread alone made the game logic 45% slower per step (5.8 vs 4.0 ms), hidden by the 60 cap. The switches stay for tests, off.

**Render thread: two ideas measured and left off.** Resetting finished command buffers' pools on a helper thread (`SMO_VK_BG_RESET=1`, off) took vkResetCommandPool (~0.9 ms a frame) off the render thread, but the driver then allocated GPU memory while recording and began passes more slowly: render thread ~8.2 ms a frame either way in six runs (normal and Mali paths), no crashes. Keeping stream copies across frames for memory the CPU cannot write directly is not possible in SMO: 6.88 of the 6.94 MB copied a frame lie in CPU-uncached pools (the measurement was taken out again).

**Mist and fences, tests kept off by default.** The mist that vanishes in Cascade with the waterfall on the left of the screen comes back when the CPU waits for the GPU before the mist pass (`SMO_VK_WAIT_FMT=0x29`, mode 1), not with a GPU barrier (mode 4). That wait costs frame rate (CPU and GPU no longer overlap), so it stays off and the issue is parked. Also kept, on by default: compute results copied back at fences, reads of pending compute results wait for them, a barrier between passes that write the same attachments (`SMO_VK_FENCE_LAND=0`, `SMO_VK_PEND_WAIT=0`, `SMO_VK_ATT_BARRIER=0` turn them off). Neither fixed the mist; no fps change was seen in the S24 runs (59.4-59.9 fps).

## Beta 0.3.20

**Frame generation: more moving objects get their own motion.** At most 64 objects a frame are drawn again into the motion data (was 32), and a rigid budget of 80000 vertices (was 40000). Measured on the S24 in Cascade with every moving object drawn again (`SMO_VK_MO_ALL=1`, about 100 a frame): 2.35 ms of GPU and 0.72 ms of render thread, against 1.83 ms and 0.56 ms at 32, real frames 99% on time either way; about a third of those redraws draw nothing (hidden or off screen). The coins near Mario were already drawn again in 0.3.19 (the user had seen them at 30 on the build before); with 64 the ones further away are too. A budget by time instead of a number is planned with the frame generation settings.

**Checked and dropped:** the strip without mist along the right and bottom at a resolution scale below 100% (0.3.19 notes) does not exist: both of the mist pass's targets are scaled alike (the capture log gives colour targets at the game's size and depth targets at their own); no pass with targets scaled unalike was logged in either capture session.

## Beta 0.3.19

**Frame generation: Mario's cap moves with him in made frames** (the user on the S24: the hat looked late, shaking, as if at 30, while the rest of Mario was smooth).
- The cap is two rigid meshes (2736 + 3117 vertices). The moving objects drawn again into the motion data were chosen skinned ones first, then rigid ones within a vertex budget of 12000, at most 24 a frame: Mario's body (drawn twice by the game) and the skinned characters around filled the slots, and the cap only got the camera's motion. Now skinned and rigid ones are taken together, nearest to the camera first; at most 32 a frame and a rigid budget of 40000 vertices (the cost is per draw, not per vertex; on the Mali tester's phone all of them took 0.13-0.19 ms of GPU). Checked in the S24 log (both cap meshes drawn again, the GPU cost of the redraws unchanged at ~1.8 ms) and by the user in play: the hat goes smoothly with Mario.

**The see-through triangle in the Cascade waterfall mist is gone** (the user's screenshot; reported on other emulators too).
- Found with frame captures at the spot: the triangle is made by the pass that scales the half-size mist into the scene, one full-screen draw (program #23331). It draws 9 vertices from a vertex range of 36 bytes, 3 vertices. A vertex fetched past the range the game bound reads zeros on the Switch, so the 6 extra vertices make zero-area triangles there (found by the nxnative project for this same pass, its issue 558; the idea only, their code was not read). Since 0.1.x this port copied the game's memory after the range for such draws, and whatever followed became two more triangles that blended the mist in a second time: straight edges through the screen, moving with the camera, going with the mist. Such a stream is now its bound bytes followed by zeros as far as the draw reads (`SMO_VK_VTXGROW=2`: the old way). The user: the triangle is gone.
- Ruled out on the way, each by a session of the user's at the spot: primitive restart (SMO restarts at 0xffffffff with 16-bit indices: never on NVN either), depth clamp (`SMO_VK_DCLAMP=0`), resolution scale (1.0), the occlusion-based conditional draws (`SMO_VK_COND_ALL=1` draws all of them), no near/far clipping for draws without a depth target (tried, reverted).

**Still open:** the waterfall mist disappears or flickers when it is away from the centre of the screen (not frame generation; not the conditional draws, not depth clamp, not the resolution scale). Seen in the captures and to fix later: at resolution scale 0.9 the mist pass's depth target is made 0.9 times its size (360x203) while its colour target is not (400x225), so the mist is not drawn in a strip along the right and bottom edges.

**For testing:** frame captures (`SMO_VK_DIAG=<frames>` in env.txt) also save the alpha channel of targets that have one (`_a.bmp`); a log line when the game's primitive restart index is one Vulkan does not use.

## Beta 0.3.18

**Frame generation: no ghost of Mario when the camera circles him** (the user on the S24: with the camera close, rotating around Mario left a copy of his edge; 0.3.17's wider range did not fix it).
- Found with a screen recording of the user's test (60 fps, every other frame a made one): only the made frames showed a second copy of the back of Mario's head and cap. As the camera circles him, the background slides past and a strip of it that he hid comes into view in the made frame; the real frame has no picture of it, and the gap was filled by fetching along the background's motion, which points into Mario: his edge was copied into the gap. Now a gap's fetch that would land on something clearly nearer than the gap's own surface takes the picture as it is there instead (moving arms and legs keep the old fill, which fetches the background behind them).
- The user then saw only a slim, see-through outline; a second recording showed it as a thin line of the cap's colour in the sky, its shape shifted by the background's motion: the sky pixels right against Mario carry a little of his colour (FXAA, the 3D picture scaled to the window) and moved with the background. A background fetch right against something nearer is now taken 2 pixels farther into the background.
- A third recording showed what was left: against a textured background (the waterfall cliffs) a seam in Mario's shape, where the strip that came into view was filled with the background as it was, not moved like the background around it. That strip was hidden in this real frame but shown in the last one (as the camera circles, the background behind Mario passes from one side to the other), so each real frame's scene is now kept (one image copy a real frame) and the next made frame fetches such strips from it, one real frame further back along the background's vector. Only where the last frame showed something as far away there (its distance image is kept too), and never for the sky, whose vectors are not usable: a first try without these checks fetched whole copies of Mario into the sky.
- The user's verdict on the S24 after rotating the camera around Mario up close: "very minimal and pretty clean", good enough.

## Beta 0.3.17

**Frame generation: no afterimages when the camera is close** (the user saw them on the S24 with the camera right next to Mario or a prop, less shaky than before; possibly there before too).
- The forward pass (0.3.14) keeps each pixel's vector in 10 + 10 bits so that one atomic picks the nearest; they held up to 64 pixels a real frame at 1/8 pixel. What moved farther (things close to the camera, fast camera turns: in saved frames of a camera pan up to 15% of the pixels already moved more than 32) fell back to the old look-up, which leaves moving things where they were: an afterimage. The vector is now kept on a square-root scale: up to 512 pixels, 0.1 pixel steps near 0, 2 near 512 (half of that in the made frame).
- Checked on the S24: Mario running and a fast camera pan, real frames 98-100% on time; the made frames lie between their real frames with no doubling. The close-camera case itself is to be checked in play.

## Beta 0.3.16

**Frame generation on Mali, second step** (after the tester's 0.3.15 log, Dimensity 8300 / Mali-G615, handheld).
- 0.3.15's cached-memory request did not apply on his phone: his driver offers no memory that is both shared with the GPU, coherent and CPU-cached (the log: "keep buffer in memory type 0 (flags 0x7)"). Hashing the game's own memory alone brought the per-object motion's render thread time from 5-14 ms to about 4 ms a frame. Now every CPU read of those copies goes to a second copy in ordinary memory, whatever memory the driver offers (the GPU still reads its own copy).
- His frame with frame generation on was 46-50 ms (about 20 real frames a second; 30-50% on time), and on his phone a frame is the sum of the game logic, the render thread and the GPU: frame generation's extra render thread time goes straight into the frame time. The log's "present" part had grown from 0.8 to 9.5 ms but did not say why (it also counted the made frame's present twice). **New log line** every 300 frames, `nvn_vk_present:`: the motion pass and moving objects, the end-of-frame barrier, the acquire and the present call, and for frame generation the made frame's build and its present, each with the time spent waiting for the GPU. On the S24 it shows the made frame's build waiting 11.9 ms for the GPU a frame: there that is the 30 fps pacing (real frames 99.7% on time); on the Mali phone it will show whether the wait is real GPU work.
- Checked on the S24: per-object motion 0.54 ms a frame, real frames 99.7% on time, the moving objects still drawn into the motion data (7196 redraws wrote pixels, 4 did not).

## Beta 0.3.15

**Frame generation: less work for the render thread on Mali** (after the tester's 0.3.14 log, Dimensity 8300 / Mali-G615, handheld).
- The tester: "It feels and looks like 60 fps", input latency the same as without it, a few minor visual errors (Mario doubled in a photo; he sees that without frame generation too). The HUD is now kept apart in handheld on his phone (HUD copy 0.3 ms) and frame generation's GPU time is about 4 ms per real + made pair.
- But only 50-75% of his real frames came on time (38-51 ms each instead of 33), the render thread was the limit (25-35 ms a frame), and the game lowered its 3D size to its minimum (640x360). The per-object motion took 5-14 ms of the render thread a frame on his phone against 0.6 ms on the S24. Its copies of the game's buffers were kept in GPU-shared memory of the first host-visible kind, which on Arm's driver is not cached for the CPU, and the CPU read them back (their check for changes, the matching with last frame's draws) at uncached speed. Now the copies go into CPU-cached memory when the GPU offers it (the log says which: "nvn_vk_mo: keep buffer in memory type"), and the check reads the game's own memory instead of the copy.
- Checked on the S24 (whose driver already gave cached memory: the work stays at 0.55 ms a frame): keep buffer in memory type 6, CPU-cached; real frames 99% on time; saved frames of Mario running still show in-between poses. The gain on Mali is still to be measured in the tester's next log.

## Beta 0.3.14

**Frame generation fixes after the Mali tester's 0.3.13 log** (test feature, off by default).
- **Frame generation no longer drops the frame rate on slower phones.** On the tester's Dimensity 8300 (Mali-G615) 0.3.13 ran at about 47 fps without it and fell to 5-10 fps with it: the moving-objects motion read and checked the game's large shared buffers again for every draw, which kept the render thread busy for ~170 ms a frame. Each buffer is now copied and checked once a frame, with a faster check. On the S24 that work now costs the render thread about 0.6 ms a frame; real frames on time: 99%.
- **No more shaking.** The made frame's view of the real frame was briefly cached in a way that could go stale when the game made its window textures again, so some made frames showed an older picture (seen as shaking). The view now belongs to the texture itself. (Making it again every frame, as 0.3.13 did, had made the GPU wait for itself every frame.)
- **The HUD stays sharp in handheld mode too.** In handheld the game draws its picture into the top-left 1280x720 of its 1600x900 window texture and draws the HUD into a 1280x720 texture; 0.3.13 only looked for a HUD texture of the window's full size, so in handheld it moved the HUD with the picture (the tester's log: "HUD copy 0.000 ms"), and it treated the whole 1600x900 as the picture, so made frames were slightly stretched and their motion was off. Now the made frame is built inside the part the screen shows, and the HUD texture only needs the picture's shape. Checked on the S24 in both modes with saved real and made frames: the HUD is in place and sharp, the scene moved under it.
- **Mario's and the enemies' animations now move in made frames too** (they had stayed at 30 while the camera was smooth; the user noticed it on the S24). The moving objects' own motion was measured right (about 2000 of Mario's pixels got their own vectors), but the made frame was built by looking up the motion at each of its own pixels: the place an arm moves to still held the background's motion, and the place it left still pointed at the arm, so moving parts stayed where they were (saved frames of 0.3.13's way: each made frame had the pose of the real frame before it). Now every pixel is pushed forward to where it lands, the one nearest the camera wins where several land on the same place, and a place something moved away from takes the background next to it. Saved frames of Mario running now show in-between poses (arms and feet half way); fast-moving hands can smear a little. The user played it on the S24: animations smooth, no shaking. Cost on the S24: about 1.5 ms more GPU time per real + made pair (7.4-8.0 ms in all instead of 6.1-6.8). `SMO_VK_FG_SCATTER=0` (env.txt) brings back the old look-up for comparison.

## Beta 0.3.13

**Frame generation: characters and moving objects get their own motion** (test feature, off by default; "Frame generation (test)" in the menus).
- Before, a made frame moved the picture only along the camera's motion (and Mario's position), so Mario's arms and legs, creatures and other moving things stayed put in made frames and updated at 30. Now the moving objects of the scene are drawn again into the motion data with their position from the frame before (the game's own vertex code run with last frame's bones and transforms), so they move in made frames too. The user compared on and off on the S24 at 30 real frames: on looks better.
- Up to 24 objects a frame are drawn again: characters first (Mario, creatures), the nearest to the camera first, then small moving things. Objects that would not show (hidden, off screen) are left out automatically (each redraw counts the pixels it wrote). Plants swaying in the wind keep the camera motion.
- The check of which pixels belong to an object now uses the scene's depth as the main 3D pass left it (later passes wrote over Mario's body and hid him from it); Mario's second, duplicate set of draws is skipped.
- Cost on the S24: about 1.5 ms more GPU time per real frame (frame generation in all about 5.5 ms per real + made pair). On Mali: to be measured (the `nvn_vk_fgtime` log line shows it every 300 made frames).
- Still open: effects and particles keep the camera motion; Mario's cap is sometimes left out.

**More in the logs** (to find what holds the frame rate back on slower phones, without another build):
- Every 30 s: the game logic's longest waits, named (which object, which game thread woke it, and where in the game's code it waited, three levels deep), and every busy thread's CPU use and the processor it ran on; once, each processor's top clock. On the S24 this already shows the game's logic waiting for its "view update" and "clipping" helper threads (al::waitUpdateCalcView, al::ClippingActorHolder::update), which are barely busy themselves but share processors with other busy threads.
- With every 1800 frames: the logic's waiting split by kind (game objects, the renderer's jobs, the present, spinning).
- With frame generation on: how many real frames came on time and how many came late enough that their made frame stayed on screen two refreshes.

## Beta 0.3.12

**Frame generation is in the menus** (test feature, off by default): Graphics in the app and the in-game menu, "Frame generation (test)". It switches live, no restart.
- On: after each real frame one more is made from it (along the camera's and Mario's motion, the HUD kept sharp) and shown on the next refresh, so 30 real frames a second show as 60 on a 60 Hz screen. It looks smoother but still answers like 30.
- While it is on, the game is paced at 30 real frames a second, keeps its normal speed by itself (as with Steady game speed, whatever that switch says), and the window asks for 60 Hz; the FPS limit is not needed. Checked on the S24 with only the new switch on (FPS limit 60, Steady speed off): 2.03 logic steps a frame at 30 real frames, full speed. A first try without the 30 pacing let the game draw 60 frames a second, half of which never reached the screen.
- The fps counter shows real and made frames apart, e.g. `30 FG 30`.
- **GPU cost, measured on the S24 (Adreno 750)** with GPU timestamps around frame generation's own work: about 4.6 ms per real + made pair. In the real frame: motion pass 0.65 ms, HUD copy 0.27 ms. The made frame: warp 1.0 ms, its own present pass (upscaling, FXAA, sharpening at screen size) 2.6 ms. The frame's total GPU time went from 14.2 to 18.2 ms; the Mali code path gave the same on this phone. A Mali GPU will need more; the log line `nvn_vk_fgtime` reports it every 300 made frames.
- Switching it on and off in the in-game menu while playing works: tested on the S24 by the user, 8 switches in one run, each taking effect at once (the log shows "frame generation on / off" after each). A first try failed only because the test run had read a separate test settings file while the menu wrote graphics.ini.

## Beta 0.3.11 (internal, not handed out)

**Frame generation: the world no longer shakes** (test switch `SMO_VK_FRAMEGEN=1`). Two fixes, both checked on the S24 against the real frames:
- **The motion vectors were about twice too long.** The scene's depth is in the -1..1 convention (z = 2d - 1), not 0..1 as 0.3.9 concluded from a test that was the wrong one. Fitted against the pictures (the next real frame rebuilt from the last one moved along the vectors), the vectors now match at exactly 1.0x in every part of the screen; before they needed 0.6-0.7x. Every made frame overshot, so the world went forward too far and the next real frame barely moved.
- **The made frame is one game step ahead**, not a fixed half frame: it is shown one refresh later and the game runs one 1/60 s step per refresh. At an FPS limit of 30 with steady speed, about 1 frame in 20 has 1 or 3 logic steps instead of 2 (a real frame a little early or late); a fixed half frame pushed those made frames too far or not far enough. The game's step count now travels with each frame's camera.
- Result: the made frame sits 0.43-0.50 of the way to the next real frame for 2-step frames (ideal 0.50) and about 0.33 for 3-step ones (ideal 0.33). Before: 0.47-0.88.
- Test switches added: `SMO_VK_MOTION_SHOT` also saves the raw vectors (`motion_<frame>_<w>x<h>.rg16f`); `SMO_VK_MOTION_FOCAL=<s>` scales the projection (test only); `SMO_VK_MOTION_ZMAP=0` brings back z = d.
- **Step 4, first version: Mario's own motion.** Mario's position is read each logic step (`PlayerActorHakoniwa::movement`, kept with the frame's camera); pixels inside a capsule around him (radius 60, height 180 game units) get his movement on top of the camera's. In free play on the S24 the made frame's Mario now moves part of the way along his path (about 4-5 px from the ideal spot, against about 8 px without). Left: the strip he uncovers as he moves shows broken pixels for that one frame, and his arms and legs keep the real frame's pose. `SMO_VK_FG_MARIO=0` turns it off; `=<r>,<h>` sets the capsule.
- Still open: the strip behind a moving Mario, the screen edge stretch, GPU cost on Mali.

## Beta 0.3.10 (internal, not handed out)

**Frame generation, steps 2 and 3** (test switch `SMO_VK_FRAMEGEN=1`; off by default, nothing changes in normal play). After each real frame, one more frame is made and shown on the next refresh: at an FPS limit of 30, 60 frames reach the screen while the game runs at 30.
- The made frame is the real one pushed half a frame ahead along the camera motion (step 1's vectors). The swapchain switches to FIFO, so real and made frames each get one refresh; checked on the S24, real and made presents alternate evenly.
- The HUD is kept apart (step 3): the game's scene is copied before the HUD is drawn over it, the scene is moved, and the HUD is put back exactly where it is. On the S24 the HUD is identical in real and made frames, and the scene is 16 px ahead during a camera turn.
- The fps counter shows real and made frames apart: `29 FG 31` means 29 real and 31 made frames a second. AVG, 1% LOW and MS stay the real frames'.
- **Not ready for players:** Mario and anything else that moves on its own is moved with the scenery in made frames, so he jitters (step 4 will give objects their own motion). The screen edge where new scenery comes in is stretched. The GPU cost on Mali is not measured yet.

## Beta 0.3.9 (internal, not handed out)

**Frame generation, step 1: camera motion vectors** (docs/FRAME_GENERATION_PLAN.md). No frames are generated yet, and nothing changes in normal play: this is off unless a test switch asks for it.
- The game's camera is read once a frame in `StageScene::drawMain` through the game's own functions (view matrix and this frame's projection), and handed to the renderer with the frame it belongs to.
- At the present, a small compute pass turns the scene's depth and the camera of this frame and the last into per-pixel motion (RG16F, in pixels). The approach follows Super Odyssey PC Recomp's camera motion vectors (MIT; notices in the source).
- Checked on the Galaxy S24 Ultra in Cascade:
  - Still camera: exactly zero motion.
  - Steady camera turn: one coherent field, about 20 px a frame to the right, with parallax around Mario.
  - Computing them every frame left the frame rate at its 58-59 fps cap.
- Three problems found and fixed on the way:
  - This port's depth is 0..1 (the fork's is -1..1).
  - Qualcomm's driver ran 8x8 compute groups as 8x1, so only the top eighth of the image got vectors; groups are now 64x1.
  - A later pass with as many targets replaced the scene's depth, which gave NaN motion.
- Test switches: `SMO_VK_MOTION=1` (vectors every frame) and `SMO_VK_MOTION_SHOT=<frame>,...` (save a motion picture and a summary line).

## Beta 0.3.8 (test build, not released)

Includes 0.3.7's Cap Kingdom fog fix.

**The render thread no longer waits for the GPU before most conditional draws.** SMO decides whether to draw many objects from occlusion counts (about 320 decisions a frame in Cascade). Before comparing the counts, the renderer waited for every outstanding compute-shader result, even though compute shaders never write those counts. That stopped the render thread for about 2 ms a frame while the GPU caught up. Now it waits only when something still outstanding (a queued report, or compute results not yet landed) can write the two counts being compared; otherwise the counts in memory are already final. The Mali tester's render thread waited for the GPU about 3.8 ms a frame in 0.3.6, so this should matter more on his phone.

Checked on the Galaxy S24 Ultra, imitating Mali's driver path, Cascade, the same view in both runs:
- 46.4-48.2 fps with the change, 38.3-45.1 (mostly 39-40) without.
- Render thread 14.9 instead of 18.1 ms a frame; its wait for the GPU 0.5 instead of 2.2 ms.
- Screenshots of the same moment match (same objects drawn).

`SMO_VK_COND_NARROW=0` brings back the old wait.

Also:
- The renderer looks up compute results only when a range can overlap them (a bounds check before two list walks).
- New log lines for the next Mali logs: where the render thread waits for the GPU (`nvn_vk_waitwho`, every 300 frames), how long the game's fence waits take (`SyncWait`), and how many conditional draws skipped the wait (`nvn_vk_cond`).
- Two changes tested and left off by default because the same-view A/B showed no gain: a small front cache for buffer lookups (`SMO_VK_RFRONT=1`) and binding the index buffer once per command buffer (`SMO_VK_IBONCE=1`).
- Showing frames asynchronously (`SMO_PRESENT_ASYNC=1`) was A/B tested again in the same view: no gain. The game's wait only moves to its window-texture wait (display pacing). Our fence waits barely queue (0.04-0.08 ms; 83-89% of the fences have finished when their wait runs).

## Beta 0.3.7 (test build, not released)

**Cap Kingdom's fog reacts to Mario again.** The game works out the height of Cap Kingdom's fog surface by reading the fog's texture on the CPU (`al::getF32FromLinearTextureF16` and two sibling functions). On the Switch the GPU draws straight into that memory. In the port the drawing lives in a Vulkan image, so the game always read zeroes: it took the fog for flat at the wrong height and refused Mario's and Cappy's ripples in it. Now, when the game reads such a texture, the GPU's picture of it is copied back into the game's memory. The copy is recorded without waiting for the GPU and arrives a frame or two later, which is fine for a fog surface. Super Odyssey PC Recomp found and documented this (MIT); our implementation is our own.

Checked on the Galaxy S24 Ultra in Cap Kingdom:
- The fog reads now return real values (0.533-0.539; Super Odyssey PC Recomp documents the same texel as half-float 0x384f = 0.5386). Without the fix every read was 0.
- Speed is unchanged: 56 fps with the fix, 54-57 without it, in the same place.
- Two earlier attempts were too slow. Waiting for the GPU at each read gave 30 fps. Looking the texture up at every texel read (the game reads thousands a frame) took the game logic from 4 to 30 ms a frame. Each texture is now looked up and copied once a frame.
- Not checked yet: whether Mario's ripples visibly show in the fog.

`SMO_FOG_READBACK=0` turns it off.

## Beta 0.3.6 (test build, not released)

(0.3.5 was an internal test, never handed out.)

**A steady 40 fps on 120 Hz screens.** The window always asked the display for 60 Hz. With the FPS limit at 40, a screen at 60 Hz showed the frames for one and two refreshes by turns (16.7 and 33.3 ms), so 40 fps juddered even when the game kept an even 25 ms. Now a limit that doesn't divide 60 (40, 45, 50) is asked for as the window's rate. Android then picks a refresh rate it divides (120 Hz for 40, 90 Hz for 45) where the screen has one. A screen without such a rate stays at 60 Hz and plays as before. Limits that divide 60 (20, 30, 60) still ask for 60 Hz, to keep the heat fix of 0.3.x. The log lists the refresh rates the screen offers at start, and the rate in use 1.5 s after each change.

**The GPU's time goes to Android's performance hints (Android 15+).** Until now the hint session got only each frame's total time. Now it also gets the GPU's time, so a phone that uses it can raise the GPU's clock with the CPUs'. It's up to the phone whether it does. The log says whether the GPU time was accepted. The hints' target follows the FPS limit too (25 ms at 40 fps), so a capped game no longer asks for 60 fps clocks. `SMO_PERF_GPU=0` sends the total only, as before.

Both ideas come from nx2android's perf_android.c (MIT; no code copied).

Checked on the Galaxy S24 Ultra, Cascade:
- **FPS limit 40:** the screen offers 120, 80, 60, 48, 30, 24 and 10 Hz. With the limit at 40 it switched to 80 Hz (SurfaceFlinger's active mode during play), and frames held 25.0 ms (median; 40.0 fps, 1% low 33-36 in steady play). The GPU time reports were accepted.
- **No limit:** the window still asks for 60 Hz, and gameplay ran at 59.2-59.7 fps, the same as 0.3.3.
- **A side finding:** at the 40 limit the S24's GPU took 12-14 ms a frame for the same scene. With slack, the GPU's clock goes down. So the GPU time in a slow frame says less about the work than it seems.

## Beta 0.3.4 (test build, not released)

**Mali: the GPU no longer empties between the steps of a frame.** The Mali tester's 0.3.3 log timed every render pass of a frame on his Mali-G615: 15.2 ms of work in 53 passes, and 11.6 ms of the GPU standing idle between them. Before almost every pass there was a gap of 0.14-0.3 ms, because each pass waited for everything before it to finish completely (a "full" barrier), even passes of one draw into a 256x256 target. Now a pass waits only for the parts of earlier passes it actually reads ("light" barriers, as Arm recommends; LAS Recomp measured 14.8 to 13.65 ms a frame with them on a Mali-G610). This mode existed since 0.1.7 but stayed off on Mali. It also used to switch itself off within a minute of play, because a few of the game's vertex shaders read small render targets (the exposure value, 2x1, and some 4x4 and 128x128 ones). Now only those targets get a barrier that includes the vertex stage, and every other pass keeps the light one (the log names each such target once).

Checked on the Galaxy S24 Ultra imitating the Mali driver: the picture is the same with light and full barriers (screenshots of the same moment compared), and light barriers stay on for the whole run. The GPU's idle time between passes went from 20.5 to 16.0 ms in the measured frame, but fps stayed the same (41-44), since the S24 imitation is limited by the render thread and Adreno does not stall at barriers the way Mali does. The gain on Mali itself is not measured yet.

New setting **Pass barriers** (launcher, Graphics; `pass_barriers = auto | light | full` in graphics.ini): Auto = Light on Mali, Full elsewhere. Full is the old behaviour, to try if the picture shows glitches. It takes effect when the game starts. `SMO_VK_SPLIT_BARRIER=0/1` overrides it.

The per-pass GPU times of 0.3.3 stay in the log, so the next log shows how much of the gap is gone.

## Beta 0.3.3 (test build, not released)

A second diagnostic build for Mali phones: the same game as 0.3.2.

**Why:** the Mali tester's 0.3.2 log showed the GPU as the main limit, busy 16-20 ms per frame. Most of it is fixed: 719 draws took 16.1 ms and 1528 draws 19.9 ms, with the 3D view at 640x360. A frame has 49-55 render passes, and the log could not say which of them cost the time.

**New in the log:** once every 3000 frames (about a minute), the GPU time of every render pass of one frame is measured (`nvn_vk_passtime:` lines): the 15 most expensive passes, the passes grouped by their targets (size and formats) with their total time, and every pass in order with its size, formats, which attachments it loads or clears, its draws and what ended it. The measured frame waits for the GPU once, a short hitch every minute or so. `SMO_VK_PASSTIME=0` turns it off.

Checked on the Galaxy S24 Ultra, Cascade: the samples appear every 3000 frames, and fps is as before (own path 58.9-59.7 fps; Mali imitation 39.5-39.8, 0.3.2 40.8-44.5).

## Beta 0.3.2 (test build, not released)

A diagnostic build for Mali phones: the same game as 0.3.1, with a log that says where each frame's time goes.

**Why:** the Mali tester's 0.3.1 log (Dimensity 8300 / Mali-G615) showed about 27 ms per frame in a heavy area, made of about 18 ms of GPU time, 15 ms on the render thread and 22 ms of "game logic". None of them filled the frame, the log could not say where most of the render thread's time went, and the logic figure turned out to include waiting for the frame before.

**New in the log:**
- `nvn_vk_rt:` every 300 frames: draws, render passes, pipeline binds and submissions per frame (and how many of them split a pass), the time the GPU was actually busy, and the render thread's time split into the parts of each draw (before it, shader variants, textures, buffers, pass begin, pipeline and state, descriptors, after it), pass ends, command buffer starts, submissions, waiting for the GPU, the rest of the replay and the present.
- The `perf: per frame` line now shows the GPU's busy time beside its command buffers' summed time (on a tiled GPU the sum can count time twice), the render thread's wait for the GPU, and the game logic's waiting time with the logic's own work per step. "Likely limit" is judged on the parts' own work; when no part fills the frame it says so and names the largest.

Nothing else changed: the same settings, the same CPU boost rule. Checked on the Galaxy S24 Ultra, Cascade: the new lines appear and fps is unchanged (own path with boost on: 59.9 fps, 0.3.1 58.9; Mali imitation: 40.8-44.5 fps, 0.3.1 40.1-42.5). With boost on, the time the logic step spins before a wait counts as waiting (the S24's logic: 2.8-3.7 ms of work a step, 7-9 ms waiting).

## Beta 0.3.1 (test build, not released)

Fixes for GitHub issues 22 and 23, and work aimed at Mali phones.

**The game no longer freezes when a notification comes in (issue 23).** Some phones take the focus away from the game window for a pop-up notification, the notification shade, a floating window or the assistant, while the game is still on screen. The port treated that like the HOME menu and suspended the game until the focus came back: issue 22's log shows freezes of 60 ms to 1.2 s. Now the game keeps running as long as its window is shown; only leaving the app suspends it. Checked on the Galaxy S24 Ultra by pulling the notification shade down during play. `SMO_FOCUS_STRICT=1` brings back the old behaviour.

**Mali: command memory is no longer freed and allocated about 10 times a frame.** Since 0.2.5, on Arm's driver every command pool gave its memory back at each reset (against a slow memory growth), and since 0.2.15 a frame has about 10 submissions (the tester's logs: 8.6-10.7). Now each pool gives it back every 64th reset (about every 100 frames), which still keeps the growth bounded. Galaxy S24 Ultra imitating the tester's driver (no push descriptors, no dynamic vertex input, no extended dynamic state 3, memory given back at each reset), Cascade, GPU at 231 MHz: **28.6-29.9 fps before, 40.1-42.5 fps now**, render thread 20 ms a frame before, 17 ms now. Not run on a Mali phone yet (the cost on Arm's own driver may differ). `SMO_VK_CB_RELEASE_EVERY=<n>` sets the interval (1 = every reset).
- Also tried in the same setup, and kept as they were: submitting later when the GPU runs out of work (40 draws between passes, 200 in a pass: 35.5-36.7 fps) and 0.2.14's fixed counts (35.7-38.2 fps). `SMO_VK_STARVE=<between>,<in a pass>` sets the counts for tests.

**Less heat when the GPU is the limit (issue 23).** CPU boost keeps the frame's threads spinning so the CPU clocks stay up. When the GPU is what the frame waits for (its own time per frame, from the GPU timestamps, at least 85% of the frame), that spinning only heats the phone and takes thermal room from the GPU. Now boost pauses in every second where the GPU is the limit and starts again when it is not, for both Auto and Always. The log says when (`CPU boost paused, the GPU is the limit`). `SMO_BOOST_GPU_CHECK=0` turns the check off.

**A cheaper last pass, the one that draws the picture to the screen (issue 22).** A Mali-G57 MC2 spent about 12 ms a frame in the menus, even at 25% resolution scale, where almost nothing else is drawn. That pass runs at the screen's full size whatever the game's resolution.
- **New "Output size" setting** (launcher, Graphics > Quality; `output_size` in graphics.ini): Game's size hands the picture to the screen at the game's own size (at least 720 lines) and the phone's display hardware enlarges it at no GPU cost. On a 2400 x 1080 screen that is 1600 x 720 for handheld (56% fewer pixels) and 2000 x 900 for docked. The touch controls look a little softer. **Auto (the default) uses it on phones without a Snapdragon GPU when the upscaler is Bilinear;** Snapdragon phones and the other upscalers keep the full screen. S24 Ultra, docked, the pass at 231 MHz: 0.85 ms at full size, 0.62 ms at the game's size (900 x 1950).
- **The pass's shader carries only what the frame uses:** FXAA, sharpening, supersampling and the fps text are now specialisation constants, so the driver drops the code a frame cannot reach. S24 Ultra with FXAA and sharpening off, same GPU clock: 1.09 ms before, 0.84 ms now; with both on, the same within the noise of one frame. GPUs with fewer registers per thread, like Mali, should gain more. `SMO_PP_LEAN=0` brings back 0.3's shader.
- Measured on the S24 Ultra: the touch controls are about a quarter of this pass (0.63 ms with them, about 0.48 ms without at the same clock).

**Issue 22's picture.** The very blocky picture in issue 22 comes from its resolution scale, set to 25% (400 x 225 for docked). That is the setting working as asked. A 50-75% scale with the GSR or FSR upscaler looks much better for a similar cost; on that phone (Helio G99, Mali-G57 MC2) the last pass was the larger cost anyway, which Output size now cuts.

**Test switches:** `SMO_TOUCH_SHOW=0/1` hides or shows the touch controls without changing controls.ini; `SMO_OUTPUT=1/2` forces full or game output size.

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
