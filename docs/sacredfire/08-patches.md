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
