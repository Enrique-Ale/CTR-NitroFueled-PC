# Backports from bbport 0.5 and 0.51 (2026-10-11)

SacredFire branched from bbport at 0.4 (merge base `daa7165`). Upstream moved to 0.51
(`3c57da4`) in seven squashed release commits, about 84,000 added lines. Most of that is
Bloodborne-only (online play through shadNet, native pages in the game's System menu, mouse
look, DLSS, the game profile for every Bloodborne edition) or needs the new memory model
(`BB_PC_MODEL=1`, the layer memory module, occlusion queries, the indirect dispatch guard), which
is not viable on the Steam Deck (GTT memory). A whole-tree merge would also bring the Bloodborne
tree back. So backports are taken per file; this note records what was taken on branch
`backport-0.51`, what was left for a hand merge, and what was judged not applicable, so the
question does not have to be answered from upstream's 126 KB of changelogs again.

## Taken (branch `backport-0.51`)

Every upstream file diff below applied cleanly to our tree; the mixed files were filtered to
the general hunks.

- **Background pipeline compilation** (`BB_ASYNC_PIPELINES=1`, vk_pipeline_cache.cpp/h; **opt-in
  here, upstream has it on**). A new graphics pipeline of a pass whose render target was drawn to
  in each of the last 8 frames is created on worker threads (`BB_ASYNC_PIPELINE_THREADS`, default
  cores/4 clamped to 1..4) while its draws go without it for a frame or two; full-screen passes
  (12 indices or fewer), indirect draws, one-time renders and loading screens (fewer than 300
  draws in the previous frame) still compile at once. The statistics line `GPU: N pipelines
  compiled in the background, M draws went without theirs meanwhile, K waited for one` prints
  every 5 s while it happens. Our vk_rasterizer.cpp passes `indirect = true` for indirect draws,
  as upstream's does. Left out: the two profile fields upstream added to the shader profile
  (`paged_buffers`, `buffer_copy_shader_hash`), which belong to the layer memory module and the
  Bloodborne game profile.

  Why opt-in: six 4-minute runs on the Deck without input (license screen, intro videos,
  attract loop), `BB_FRAME_STATS=1`:

  | run | shader cache | background compilation | result |
  |---|---|---|---|
  | 1 | new build | active (67 pipelines in the background) | segfault after ~3.3 min |
  | 2 | warm | on, nothing to compile | clean |
  | 3 | warm | off | clean |
  | 4 | cleared | active | 3 `Bad PM4 packet ... dword 0x1` in one buffer, segfault after ~3 min |
  | 5 | cleared | off | clean |
  | 6 | cleared | active, `BB_WRITE_LOG=1` | clean (timing changed; nothing captured) |
  | 7 | cleared | off (the new default); the user played into a race | `Bad PM4 ... dword 0x1` at a 2-dword packet header during the race load, then the game hung (window unfocused at the time); killed by the 4-minute timeout before the thread dump |

  Run 7 shows the clobber without background compilation too, and it hung the game instead of
  crashing it: the clobbered packet was followed by an `EVENT_WRITE_EOS` writing 1 and a
  `WAIT_REG_MEM` for 1 at the same label (`0x10f6113890`); the decoder skipped the right number
  of dwords, frame statistics stopped after that. Whether the unfocused window played a part is
  not known. To get the thread stacks next time: run without the outer `timeout`, and when it
  hangs send `kill -ALRM <pid>` (the runtime's watchdog handler dumps every thread's frames and
  exits); `kill -USR2` dumps only the thread that receives it. The pre-backport build is
  `dist/SacredFire-x86_64.AppImage` (v0.1) for an A/B of the same scenario.

  Both segfaults are the GPU thread in `RefreshGraphicsStages` reading a shader at an address
  whose high register is 1 (`0x110073e6200`, `0x11107768700`: pgm_hi 1, pgm_lo a plausible
  direct-memory address): the clobbered-dword bug of STATUS.md, which until now had only been
  seen on packet headers, landing on a SET_SH_REG payload. The clobber is not caused by the new
  code (the background threads only create Vulkan pipelines), but the changed timing makes it
  fire within minutes, so the feature stays off until the writer of the 1 is found. Logs:
  `logs/backport-smoke*.log`.
- **Texture collector under VRAM pressure** (texture_cache.cpp/h, bbport_toggles.h,
  vk_instance.cpp). Under pressure an image is evicted once unused for
  `BB_GC_PRESSURE_IDLE_SECONDS` (5) instead of after 80 submissions; over the critical mark it
  goes after 1 s, and only when still over the mark after that does the collector take what was
  used a moment ago. Upstream found the old rule evicting textures that were on screen a moment
  earlier and re-uploading them when the camera turned back (frame rate falling the longer the
  game ran on 4 to 8 GB cards). On discrete GPUs the driver's budget is polled once a second and
  the old per-submission eviction returns for 2 s when usage is over 92 % of it; the Deck is an
  integrated GPU, so that branch is skipped there. Also from these hunks: `BB_VRAM_LIMIT_MB=N`
  (the driver's budget taken as at most N MiB, for testing small-card behaviour),
  `BB_IMAGE_WATCH=addr` and `BB_IMAGE_SYNC_LOG` diagnostics, GPU write bindings now look only at
  the images starting at the written address instead of walking the whole range, and the line
  `GPU: <model>, <N> MiB VRAM; <vendor> <driver> (...), Vulkan <version>` at the top of every
  log. Left out: the new-model hunk in `DownloadImageMemory` (upstream issue 122), the DLSS
  extensions and the occlusion-query feature.
- **Scheduler chunk swap** (vk_scheduler.cpp/h; upstream issue 100). Handing recorded commands
  to the recording threads left the current chunk null for a moment; a fault handler recording
  on that thread then read address 0x20000. Replacement chunks are swapped in first and
  `CurrentChunk()`/`OrderedChunk()` never return null. The same diff adds
  `BB_PASS_BREAK_TRACE=1` (who ended a render pass that began again with the same state) and the
  `CarriedScope` hooks that upstream's occlusion queries use; they are null here.
- **Small mips of tiled textures** (tile.h, tiling.cpp, tile_manager.cpp, image_info.cpp/h,
  host_shaders/tiling.comp; shadPS4 #5196). A mip smaller than a macro tile is stored micro
  tiled, at most 4 slices thick, as the hardware does; the tiling shader takes a per-mip bit mask
  instead of a slice count, the 128-bit pixel order inside a micro tile is corrected and
  `Thin3DThinPrt` maps to the PRT 3D array mode. Worth watching in CTR for distant-texture
  shimmer and wrong colours on the smallest mips.
- **Fault handler registry** (gpu/shim/core/signals.h). Two handlers with the same priority were
  treated as equal by `std::set` and the second one was dropped.
- **Swapchain recreated through `oldSwapchain`** (vk_swapchain.cpp; upstream PR 93). NVIDIA's
  Wayland WSI rejected a fresh swapchain on the same surface ("Surface already has a tearing
  controller").
- **Dear ImGui private to libbbgpu.so** (gpu/CMakeLists.txt; upstream PR 113). The library's
  ImGui symbols were in the global scope; a Vulkan layer carrying its own ImGui (vkShade) bound
  to ours and aborted. Relevant here because MangoHud is such a layer.
- **prepare.py**: `eboot.bin` dumped as a plain ELF (no SELF wrapper) is accepted (upstream
  issue 49); the SELF error names the likely cause; analysis.json is written as UTF-8 and the
  console streams replace characters they cannot encode (the title's ™ on CJK Windows locales,
  upstream issue 55). `tests/test_prepare.py` passes.

## Left for a hand merge (our files diverged)

- Atomic save writes (src/runtime_file.c, runtime_savedata.c; upstream issue 64): a save file
  opened for writing goes to `<name>.<n>.bbtmp`, is fsynced and renamed into place on close, so a
  crash mid-save leaves the old file. The same diff adds a case-insensitive fallback for game
  files not found, `BB_SAVE_TRACE=1` and `BB_FILE_TRACE=1`. CTR writes SYS-DATA.sav the same way.
- Stick dead zone and calibration (src/runtime_pad.c; upstream PR 71): `BB_PAD_DEADZONE` (5 of
  127), `BB_PAD_DEADZONE_OUTER`, neutral position measured for Switch Pro clones whose axes rest
  off centre. Our runtime_pad.c changed a lot for the controller work (07-controllers.md).
- AppImage size (packaging/default.nix): only RADV (without LLVM) and ANV instead of all of Mesa,
  `bb-probe` stripped of debug info, MangoHud without its 32-bit half and xdg-utils, libdecor
  without the GTK 3 plugin, GTK4 without GStreamer, printing and tracker. Upstream went from 925
  to 399 MB; ours is 841 MB with the same contents.
- build.sh (upstream issue 124): the FSR-Vulkan submodule is reset and patched again when the
  previous version's patch is still applied. run.sh: `RADV_DEBUG=nohiz` set automatically on
  Polaris and Vega (`BB_AUTO_NOHIZ=0` keeps it off); larger driver shader caches
  (`MESA_SHADER_CACHE_MAX_SIZE`, `__GL_SHADER_DISK_CACHE_*`).
- Overlay: a "Quit the game" button (press twice) for pad-only sessions (upstream issues 74,
  116); `BB_DISPLAY` monitor selection in window.cpp (upstream issue 69). Both files diverged
  here (our patches menu, the window icon).
- Launcher: *Copy* and *Export log…* on the log page (upstream PR 78).

## Not applicable

shadNet online play (gpu/bbnet; its public servers serve Bloodborne), the native System-menu
pages (bbport_game_menu.cpp hooks Bloodborne's menu code), mouse look (maps to Bloodborne's
right stick), DLSS, the Bloodborne game profile and edition table, the UI composition shader
list, the layer memory module and sparse arena work, occlusion queries, the indirect dispatch
guard, the PM4 self-test and the new command-processor decoder (gpu/cp). Two of those are worth
a second look later: upstream now runs `COND_EXEC`, `SET_PREDICATION` and `MEM_SEMAPHORE` on the
GPU timeline "for porting other games", and its `BB_WATCH`/`BB_DCB_STATS` diagnostics could help
with our clobbered-packet-header mystery (STATUS.md). The online module could become interesting
if paired with a custom server for CTR; shadNet's protocol (`gpu/bbnet/shadps4/shadnet/
shadnet.proto`) is the starting point for that.
