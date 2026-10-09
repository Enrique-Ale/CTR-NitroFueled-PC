# SacredFire status

Updated 2026-10-09.

## Milestones

| milestone | state |
|---|---|
| Loader reaches the eboot entry, libc.prx/Fios2/NpToolkit2 initialized | reached |
| First frame presented | reached: the license screen renders at 25-30 FPS, 51 draws/frame |
| Sound and input | reached: two audio ports on PipeWire, Steam Deck Controller through SDL3 |
| Saving | reached: user/savedata/1/CUSA13795/savedata{0,1}/SYS-DATA.sav written; user confirmed progress persists between launches (2026-10-09) |
| Main menu | reached (2026-10-09, run 12): after the license screens and Bink intro videos the game's own log shows menu/garage activity ("changeHeroAndOutfit", "Loading hero DriverNGin"), ~300 draws/frame at 30 FPS |
| One full race | reached (2026-10-09, run 14): arcade races play through with sound, the Steam Deck controller and saving (user report) |
| Adventure mode | plays after the sampler fix (user tried it; not played to the end) |

## How to run

```bash
bash run.sh
```
`BB_NEO=1 bash run.sh` runs the game in PS4 Pro mode (Neo: 7-CPU mode and the Pro's 5568 MiB direct
memory budget). run.sh detects the profile (`scripts/game_check.py orig/CUSA13795` prints `ctr`), links
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

## Measured at the milestones (run 29, 2026-10-09; `free -g` total/used/free/shared/cache/available, bb-probe RSS)

| milestone | time | free -g | RSS |
|---|---|---|---|
| (a) entry + libc init | 16:39:07 | 14/9/2/0/2/4 | 502,156 KiB |
| (b) first frames (24 FPS, 105 compiles) | 16:39:13 | 14/10/2/0/2/4 | 1,113,012 KiB |
| (c) driver select with the pad | 16:40:02 | 14/11/1/1/2/3 | 1,512,808 KiB |
| (d) race loading | 16:40:02 | 14/11/1/1/2/3 | 1,515,908 KiB |

## Known issues

- **Adventure hub sky** renders as concentric colour stripes (user screenshot, run 29); fur and
  everything else in the scene are right. Not investigated yet; capture the frame
  (BB_CAPTURE_TRIGGER) in that hub.
- **Clobbered packet headers**: in CTR a single dword inside a submitted command buffer is
  sometimes overwritten with 1 (`Bad PM4 packet ... [00000001]` where a SET_CONTEXT_REG header
  was). Dropping the rest of the buffer froze the game on the fence behind it; the decoder now
  resumes at the next packet header. Who writes the 1 is still unknown (BB_WRITE_LOG=1 lists
  writes into the range).

- **Fur/hair characters** (fixed 2026-10-09, see 06-fur.md): previously missing (driver select, run 12): bodies drawn with the fur material
  are missing; rigid parts (eyes, teeth, hats, kart) draw. The only GPU errors in the run are
  "image view type Color1D is incompatible with image type Color2D" (4 images) and one
  geometry-shader ring stride warning. The shadPS4 #5133/#5171 backport crashed RADV at pipeline
  creation and was reverted (patch kept in the session notes). Diagnosis and next step in
  06-fur.md: the fur geometry shader exceeds maxGeometryInputComponents (64) because ring reads of
  the second input vertex are mapped to extra attribute locations. Frame analyzer:
  `BB_CAPTURE_TRIGGER=out/capture_trigger BB_CAPTURE_DIR=out/capture`, `touch out/capture_trigger`.

- The license screen's accept needed more than one attempt (reported by the user); the save that
  follows was written, so the input path works. Watch for a repeat.
- Memory: the Steam Deck has 14 GiB; close other applications before running.
- Upstream Python tests `test_run_settings.*` call run.sh, which builds: never run the full
  `unittest discover` while a build is in progress.
