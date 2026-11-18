# Frame generation plan

Status (2026-11-14): **step 1 is done and checked on the S24** (0.3.9: runtime/smo_camera.c, runtime/nvn_vk_motion.c). Steps 2-5
are not started. The user chose extrapolation first (option 1), with interpolation as a later setting (option 2). Planned settings:
frame generation off / on; multiplier 2x / 3x / 4x / Auto (by real fps and screen refresh); later, extrapolate / interpolate; it
pairs with the FPS limit.

**Step 2 (0.3.10, test switch `SMO_VK_FRAMEGEN=1`):** working on the S24 as a first version.
- After each real present, a compute warp makes the next frame from the window texture (HUD included) along the camera vectors,
  half a frame ahead. It is presented right after, through the same present pass; the swapchain goes FIFO.
- At the FPS limit 30 (steady speed): 30 real + 30 made frames a second, the game a steady 30 fps.
- Pacing (`SMO_VK_FG_LOG`): each made present comes ~12 ms after its real one, the next real one ~21 ms later, in every frame
  logged. Even.
- The direction is right: the made frame's content is shifted 20 px left of the real one during a turn whose vectors say +34 px
  (scene pixels at 1408 wide, ~19 px at the window's 1600).
- **What the user saw shake** (watching the run): the camera and the UI. The causes, from a saved real/made pair:
  - The HUD moves with the scene in every made frame. Step 3 fixes it.
  - Mario moves with the scenery in every made frame (the camera keeps him in place in the real ones). Step 4 fixes it, so
    **step 4 is required for SMO**, whose camera follows the player.
  - The right edge is stretched where new scenery comes in.
- `dumpsys SurfaceFlinger --latency` gave no frame history for the game's layer on this Android version, so the pacing was
  measured from our own presents.

**Step 3 (0.3.10): the HUD drawn where it is, working on the S24.**
- How SMO builds its last image:
  - It keeps one texture of the window texture's size (RGB10A2, linear) from frame to frame.
  - The HUD pass's FIRST draw puts this frame's scene into it, the other draws (layout shaders, blended) add the HUD.
  - One last draw copies it into the window texture with the sRGB curve (21 -> 81, 132 -> 190).
- So the texture is copied right after that first draw. A copy at the pass's beginning held the previous frame (HUD included)
  and gave a ghosted HUD and a made frame that did not move ahead.
- The HUD pass is found as the frame's last pass into a texture of the window's size that is none of the window's 3 textures,
  then only that texture is watched. Excluding only the last window texture made the detection flip every other frame.
- The warp takes a pixel as HUD where the texture changed during the HUD pass and keeps the real pixel there. Elsewhere it moves the
  copy and applies the sRGB curve.
- Result: the HUD identical in real and made frames, the scene 16 px ahead (frames 2000 and 2300), the colours matching.
- Cost: one copy of the HUD target and one break of the HUD pass a frame, plus the warp. Not measured on Mali yet.
- The fps counter shows "<real> FG <made>" (e.g. "29 FG 31"); AVG, 1% LOW and MS stay the real frames'. A fifth overlay line broke
  the overlay, which holds 4.

**0.3.11: one logic step ahead, not half a frame.** The user still saw the world shake (HUD steady). Four consecutive frames
(2000-2002, real and made, bands away from Mario) showed:
- The background never moves backwards: the warp's direction is right.
- The steps were uneven: about 24 px from a real frame to its made one, then 6-18 px to the next real one.
- The vectors were 32 px at frames 2000 and 2001 and 48 px at 2002: 3 logic steps instead of 2. At the FPS limit 30 with steady
  speed, 82 of 1800 frames had 1 or 3 steps (a real frame a little early or late).
- A fixed t = 0.5 pushes the made frame of a 3-step frame 1.5 steps ahead. The made frame is shown one refresh later and the game
  runs one step per refresh, so the right distance is one step: t = 1 / steps. The step count (smo_framerate.c) travels with the
  camera (smo_camera.c) in the PresentJob.
- Left as is: when a real frame comes late, its made predecessor stays up two refreshes. Making a second frame then (the Auto
  multiplier) is the remedy.

**0.3.11: the depth is -1..1 (z = 2d - 1), and that was the main shake.** Even with the step fix, the made frame sat 0.66-0.88 of
the way to the next real frame. A fit settled it (scratchpad mvfit.py, using the raw vectors `motion_<f>_<w>x<h>.rg16f`): the next
real frame is rebuilt from the last one moved by k x the vectors, and k is fitted per screen strip.
- z = d (0.3.9's choice): k = 0.6-0.7 everywhere, error 19-22 grey levels (23-24 unmoved).
- z = d with the focal length x 0.7: far rows fit, near rows did not (k 0.3-0.55).
- **z = 2d - 1: k = 1.00 in every strip, error 6.5.** z = 2d - 1 with focal x 0.7: worse (error 16-19).
- 0.3.9 chose z = d because its field looked more uniform. The camera orbits Mario, so the true field is not uniform: the vectors
  were about twice the real background motion (37 px against 17).
- The projection is a plain 16:9 one (x 1.5455, y 2.7475, vertical FOV 40 deg, near 50). The camera turns 1.30 deg per step.
- After both fixes: the made frame sits 0.43-0.50 of the way for 2-step frames, ~0.33 for 3-step frames (scratchpad fgeven.py).

**Step 4, first version (0.3.11): Mario only, from his position.**
- The scene depth is D32 with no stencil (format 126), so the game does not mark his pixels there.
- Instead: `PlayerActorHakoniwa::movement` [main 0x41dd34] is wrapped; after it, the actor's pose keeper (+0x50) holds his
  translation at +8 (the layout the fork's overlay reads). drawMain keeps it with the camera; the PresentJob carries it.
- The motion shader rebuilds each pixel's view-space point from the depth. Inside a capsule around Mario (radius 60, height 180,
  SMO_VK_FG_MARIO=<r>,<h>; feet at his translation, up +y) the point is moved back by his step before the clip-to-clip matrix.
  Push constants: 128 bytes (the depth mapping folded into the matrix and into A', B').
- Free play by the user on the S24 (fg26 with it, fg27 without; red-pixel centre, a rough measure): the made Mario sits about
  4-5 px from the ideal spot with it, about 8 px without. In crops, without it he stays put in the made frame and jumps in the
  next real one; with it he moves along his path.
- The user played both again (fg28 with, fg29 without): with it "def looks better"; without it Mario doubles, an afterimage
  (he stays put in every made frame, so two Marios alternate at 60 a second).
- Left: the strip he uncovers shows broken pixels (the gather finds his old pixels there: nothing behind him is in the frame);
  his limbs keep the real frame's pose. A fix for the strip: flag his pixels in the motion image, and where the gather lands on
  him from a pixel whose own vector is the background's, take the background beside him instead.

**GPU cost (0.3.12, S24 Adreno 750, Cascade, camera turning, 1600x900 window texture, 1080x2340 screen):** GPU timestamps
around frame generation's own work (nvn_vk_motion.c fgt_mark, the `nvn_vk_fgtime` line every 300 made frames):
- In the real frame: motion pass 0.63-0.73 ms (1312x738), HUD copy 0.26 ms.
- The made frame: warp 1.0 ms (1600x900), its own present pass 2.6 ms (upscaling, FXAA, sharpening, touch layer at screen size).
- In all ~4.6 ms a real + made pair; the frame's GPU busy went 14.2 -> 18.2 ms. The Mali path (push constants, vertex ids and
  extended dynamic state off) gave the same.
- So the made frame's present pass is the largest part. Ideas: warp the real frame's finished screen image instead (no second
  present pass, but the touch layer and HUD are in that image); or make the present pass itself cheaper (helps real frames too).
- A Mali-G615 is slower than the Adreno 750; expect roughly twice these times. The tester's real frame needs ~15-18 ms, so 30 real
  + 30 made should fit in 33 ms, without much to spare. To be measured on his phone (the log line is in every build).

**Step 5 (0.3.12): the setting.** graphics.ini `frame_generation = off | on`, in the app's Graphics page and the in-game menu
("Frame generation (test)"), live. While on (gfx_framegen_on: SMO_VK_FRAMEGEN wins):
- nvn_vk_motion.c fg_update (each real present) sets FIFO, or the mode chosen at the start, and makes the swapchain again.
- display.c paces the game at 30 a second. Without that, with the FPS limit at 60, the game drew 60 frames a second at 1 logic
  step each and half of them never reached the screen (measured: 1.003 steps a frame).
- smo_framerate.c runs steady speed; android_perf.c asks the window for 60 Hz and sets the hint target to 33.3 ms.

**Step 4b, every object (chosen 2026-11-15 by the user after seeing Mario's limbs and a chain chomp update at 30 while the
world shows 60).** Feasibility measured with SMO_VK_MO_MEASURE=1 (nvn_vk_motion.c mo_note_draw / mo_frame_end, S24, Cascade):
- The G-buffer pass has only 50-62 draws a frame.
- Draws match the frame before by key (vertex and fragment program + primitive + count + first/base vertex + instances, after
  Power Moon's interp.c): 99.7-100% with the camera still, 84-92% while it turns (things entering and leaving the view).
- Vertex uniform slots: c5, c6, c7, c9 differ per draw (each object's: its transform, bones...); c3, c4, c8, c10, c12-c16 are
  the same buffer for every draw (camera, pass).
- Matched draws whose own data changed: 18-22 a frame with the camera still (Mario's parts, animated things), 30-38 while it
  turns (some objects' data depends on the camera). A few thousand vertices each.

Design (to build):
1. For each G-buffer draw keep what a replay needs: programs, vertex input layout, the ring places of its vertex and index
   streams and of its uniform buffers, the draw arguments; and copies of its per-object vertex uniform data (c5 c6 c7 c9) into
   a buffer that lives one frame more.
2. At the present: match with the last frame (key; several draws of one key paired by the nearest per-object data).
3. A motion vertex shader per vertex program: the translator emits the game's vertex code twice, once as usual (gl_Position) and
   once with the per-object slots read from the last frame's copies (the previous clip position, kept as an output).
4. After the camera motion pass, the matched draws whose own data changed are drawn again into the motion image (RG16F): the
   fragment writes (previous ndc - ndc) in pixels where its depth equals the scene's (read in the shader, no depth test).
   Everything else keeps the camera motion; Mario's capsule (step 4) goes.
5. Cost to expect: ~20-40 small draws, vertex work twice, one cheap fragment shader. To be measured.

**Later (asked for by the user 2026-11-15): particles and effects.** They are drawn in passes after the G-buffer pass, so the replay
does not see them and they keep the camera motion (they update at the real frame rate). Two ways, to choose once Mario and the
creatures are done and the Mali cost is measured:
1. Replay the effect passes too (many particles are quads the game moves each frame). Transparency: a particle pixel mixes the
   particle and what is behind it, so one motion per pixel is partly wrong either way (DLSS frame generation has the same limit).
2. A small optical-flow pass only where the replay wrote nothing and the picture changed more than the camera explains (effects,
   water, anything over the draw cap): Lossless-Scaling-style guessing limited to those spots. GPU cost maybe 1-2 ms on Mali.

Step 1 findings that the next steps need:
- The camera: wrap `StageScene::drawMain` (main 0x4ccaf8) and call `al::getViewMtxPtr` and `al::getProjection` (scene + 0x18,
  view 0), then `sead::Projection::getDeviceProjectionMatrix`. `al::GraphicsSystemInfo::updateViewGpu` for view 0 gave a constant
  matrix, so it is not the gameplay camera.
- Depth in this port is -1..1 (z = 2d - 1), as in the fork (0.3.9 said 0..1; corrected in 0.3.11, see above).
- Qualcomm's driver ran 8x8 compute groups (SPIR-V 1.6 LocalSizeId) as 8x1. Use 1-D groups (64x1) for our own compute passes.
- Take the frame's FIRST pass with 4 or more colour targets and a depth target as the scene depth. Goal: Mali phones that render about 30-40 real frames a second show 60 on
screen, with almost no added input lag and the HUD always sharp.

## The idea

For each real frame the game draws, we make one extra frame and show it between real frames. We don't run the game's logic or
rendering again for it. Instead we take the newest real frame and move each pixel along its motion vector, half a frame forward
(**extrapolation**). Nothing waits for a future frame, so the extra lag stays small, unlike interpolation, which holds every real
frame back by one.

On a GPU-bound Mali the generated frame has to be cheap. That is why it is built from motion vectors and one image pass, and not by
drawing the frame again (Power Moon Recomp's interpolation and Super Odyssey PC Recomp's native 120 fps both draw it again, which
doubles the GPU's work).

## Where the parts come from

| Part | Source | Licence |
|---|---|---|
| The camera's view and projection matrices each frame | Super Odyssey PC Recomp, `runtime/hle/odyssey_debug.c` (SMO 1.0.0 hook addresses, see below) | MIT: keep its notice |
| Camera motion vectors from depth (small compute shader) | Super Odyssey PC Recomp, `runtime/nvn/vulkan/dlss_probe.c` (`reprojection`, `motion_init`) | MIT: keep its notice |
| Matching draws across frames (for per-object motion, step 4) | Power Moon Recomp `runtime/nvn/interp.c` (the same file is in the fork) | MIT: keep its notice |
| The frame generation itself (warp, hole filling, HUD, pacing) | ours | |

The fork: https://github.com/Simon3Sarkis/super-odyssey-pc-recomp (the developer of Power Moon Recomp recommends it; the original
GitLab project was taken down). Clone it with `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never git -c credential.helper=` so that no
sign-in window opens. Read it only; nothing of theirs is built or run.

## The camera (from the fork, SMO 1.0.0)

- `GraphicsSystemInfo::updateViewGpu` at main+0x879c98. With x1 = 0 (view 0), x2 + 8 is the 3x4 view matrix and x3 + 0x4c the 4x4
  projection.
- Or the main draw's camera copy at main+0x4ccbc8. Call `al::getProjection(x19 + 0x18, 0)`, then
  `sead::Projection::getDeviceProjectionMatrix`; the view matrix is at x23 + 8.
- In our port these are lifted functions, so they can be reached with the same link-time wrap we already use for
  `f_4000731fdc` and the fog readers (`-Wl,--wrap`, tools/android/build_runtime.py). Check first that the addresses hold in our
  build (main_functions.tsv).
- The previous view-projection times the inverse of the current one gives one clip-to-clip matrix, computed once a frame on the
  CPU. Per pixel: `old = M * (ndc, depth, 1)`, and motion = `old.xy / old.w - ndc`.

## Steps

1. **Camera motion vectors, debug view only.**
   - Wrap the camera hook. Find the scene's depth target (the G-buffer pass's depth; the renderer already knows every pass).
   - Run the motion shader at the end of the 3D scene, before the HUD pass.
   - Debug option: show the motion as colours. Check that panning the camera gives smooth, sensible colours, and that a still
     camera gives none.
2. **Extrapolated frames, without the HUD.**
   - Present an extra frame between the real ones. A compute pass reads the last scene colour, depth and motion, and writes each
     pixel half a frame ahead: a forward warp, or a backward gather using the motion of nearby pixels.
   - Holes (uncovered background, screen edges): fill them from neighbours, preferring the background (the larger depth).
   - Pacing: the generated frame goes out half a frame after the real one (display.c / the present thread). With a 30 fps cap on
     60 Hz, every other vsync.
3. **The HUD drawn fresh.**
   - The HUD is its own pass after the scene (1600x900 on the S24, 1280x720 / 720p on others). Keep the scene image from before
     the HUD.
   - Draw the HUD over the generated frame too: either the last HUD image composited with its alpha, or the HUD pass replayed
     (cheap: few draws).
   - This avoids the warped, smeared HUD that tools like Lossless Scaling show.
4. **Per-object motion (later).**
   - Use Power Moon's matching (programs + draw arguments + target size; draws with one key paired by their nearest uniforms) to get
     each object's matrices from the previous frame.
   - Add an output to the G-buffer vertex shaders (our translator writes every shader) for the previous clip position. That gives
     a motion target with Mario's and enemies' own movement.
   - This removes the smearing on fast-moving characters.
5. **Options.**
   - Frame generation: off / on, and a choice of extrapolate (low lag) or interpolate (fewer artifacts, about one real frame more
     lag). Interpolation shares every part above; it only waits for the next real frame and blends forward and backward warps.
   - A debug overlay to show motion vectors and holes.

## Costs and limits to expect

- **GPU:** the motion pass plus the warp pass, roughly 1-3 ms per generated frame on a Mali-G615 at the scene's size (the scene is
  small: 640x360 to 800x450 when the game lowers it). This must be measured on the tester's phone first. The budget at 30 real fps
  is 33 ms, and the game needs ~15-18.
- **Lag:** extrapolation adds almost none. Interpolation adds about one real frame (33 ms at 30 fps).
- **Feel:** it looks like 60 but still responds like the real frame rate.
- **Artifacts:**
  - edges of things that move on their own (until step 4);
  - transparent effects with no single depth (particles, water surfaces, fog, glows);
  - the strip that comes into view at the screen edge during fast turns;
  - camera cuts, warps and loading screens, which must reset (no motion across a cut: compare the matrices, as the fork's
    `direction < 0.92` reset does).
- **Game logic:** SMO steps once per real frame. With steady_speed at 30 real fps it runs 2 steps a frame, so the motion between
  real frames is two steps' worth. Extrapolate half of that.

## Not chosen, and why

- **Optical flow (as Lossless Scaling):** it needs nothing from the game, but it guesses motion from pictures. It is costlier on
  the GPU and worse on flat areas, repeating patterns and transparency. With the camera hooks available there is no reason to use it.
- **Drawing in-between frames again (Power Moon's interpolation, the fork's native 120):** the best quality, but it doubles GPU work.
  It could be an option for phones with spare GPU (Snapdragon 8-class at 120 Hz), never for Mali.
- **External tools:** LSFG-Android works from a screen recording (50-80 ms of lag, Adreno 7xx and newer). Qualcomm's Neural Fusion
  and Frame Motion Engine are Snapdragon only.
