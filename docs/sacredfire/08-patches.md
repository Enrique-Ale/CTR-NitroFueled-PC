# Game patches (CTR)

`patches/CrashTeamRacing.xml` carries the CUSA13795 1.21 patches from
[illusionyy/PS-Game-Patch](https://github.com/illusionyy/PS-Game-Patch) (authors illusion and
TroyWarez): "60 FPS Unlock", "Disable Motion Blur", "Resolution Patch (1080p for PS4 Pro)".

- In-game menu (Insert or L3+R3), section "Game patches (restart required)": three checkboxes,
  saved to bbport.ini as `patch_60fps`, `patch_no_motion_blur`, `patch_pro_1080p` (0/1). The
  menu offers "Apply and restart game" when a choice differs from the one the game started with.
- At start run.sh (ctr profile) runs `scripts/patches.py --external-only --patches-dir patches
  --title-id CUSA13795 --app-version 01.21 --settings bbport.ini`, which compiles the selected
  patches (plus any `patches.json` selection) into `out/patches.bin`; bb-probe applies the writes
  to the image after relocation (`--patches`). Bloodborne's built-in patch logic is untouched.
- `BB_PATCHES_DIR` points at another folder of shadPS4-format XML files for this title.
- The 1080p Pro patch only makes sense with `BB_NEO=1`.

## 60 FPS on the Steam Deck (measured 2026-10-09)

- With a 60 Hz virtual vblank the game alternated between 16.7 and 33 ms frames (40-44 FPS in
  races, 50 in menus): a frame slightly late for the vblank costs a whole extra one. run.sh now
  sets `BB_VBLANK_HZ=480` when `patch_60fps=1`; menus then hold 57-60 FPS.
- Races stay at 37-45 FPS. In the default memory model the command-processing thread is the
  limit (idle 5%, ~1,400 draws/frame at ~10 µs each plus write-tracking faults). The experimental
  PC model (`BB_PC_MODEL=1`) frees that thread (idle 40%, no write faults) but commits the whole
  direct-memory arena as GPU-visible memory: amdgpu GTT 5.8 GB (1.8 GB in the default model),
  which on the Deck's shared 16 GB pushed 15 GB into swap; the game then waited 18.5 ms per frame
  for the previous frame. Not usable here without a bounded GTT budget.
- Threads during a race (old model): no single thread saturated; machine ~80% busy on 8 threads.
