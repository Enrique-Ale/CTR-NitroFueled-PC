# SacredFire status

Updated 2026-10-09.

## Milestones

| milestone | state |
|---|---|
| Loader reaches the eboot entry, libc.prx/Fios2/NpToolkit2 initialized | reached |
| First frame presented | reached: the license screen renders at 25-30 FPS, 51 draws/frame |
| Sound and input | reached: two audio ports on PipeWire, Steam Deck Controller through SDL3 |
| Saving | reached: user/savedata/1/CUSA13795/savedata0/SYS-DATA.sav written after the license screen |
| Main menu | reached (2026-10-09, run 12): after the license screens and Bink intro videos the game's own log shows menu/garage activity ("changeHeroAndOutfit", "Loading hero DriverNGin"), ~300 draws/frame at 30 FPS |
| One full race | not yet |

## How to run

```bash
bash run.sh
```
run.sh detects the profile (`scripts/game_check.py orig/CUSA13795` prints `ctr`), links
libSceNpToolkit2.prx, skips the Bloodborne patch/resolution steps and starts bb-probe. By hand:

```bash
python3 scripts/prepare.py orig/CUSA13795 --out out
python3 scripts/link_modules.py orig/CUSA13795 --out out --modules libc.prx libSceFios2.prx libSceNpToolkit2.prx
python3 tools/gen_import_names.py --out out
python3 scripts/content_profile.py orig/CUSA13795 --out out
bash build.sh
out/bb-probe out/boot-linked.bin --import-names out/import_names.txt --content-profile out/content.bin \
    --app0 orig/CUSA13795 --user user --timeout 0
```

`out/bb-probe out/boot-linked.bin --list-imports --import-names out/import_names.txt` prints how every
import binds without running the game (GPU imports show as unresolved there: they bind at GPU init).

## Current blocker

None (no STOP). The game runs through license screens and intro videos. Next: reach the main
menu, then a race; implement imports as they are hit (libSceFont and the NpToolkit2 request paths
have not been reached yet).

## Memory (menus, run 12)

RSS about 1.1 GB, VRAM about 1 GB, Vulkan allocations 1.6 GB; the Steam Deck's 14 GiB are enough
with the desktop apps closed.

## Known issues

- The license screen's accept needed more than one attempt (reported by the user); the save that
  follows was written, so the input path works. Watch for a repeat.
- Memory: the Steam Deck has 14 GiB; close other applications before running.
- Upstream Python tests `test_run_settings.*` call run.sh, which builds: never run the full
  `unittest discover` while a build is in progress.
