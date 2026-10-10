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
| Split screen | works: a second gamepad on port 2 joins (user-confirmed) |
| Game patches | 60 FPS and motion-blur-off switch from the menu; motion blur off confirmed working |

## How to run

```bash
bash run.sh
```
Game patches (60 FPS, no motion blur, Pro 1080p) are switched in the in-game menu (08-patches.md).
`BB_NEO=1 bash run.sh` runs the game in PS4 Pro mode (Neo: 7-CPU mode and the Pro's 5568 MiB direct
memory budget). run.sh checks the game (`scripts/game_check.py orig/CUSA13795` prints `ctr`), links
libSceNpToolkit2.prx, compiles the chosen game patches and starts out/sacredfire (the game executable, bbport's bb-probe renamed). By hand:

```bash
python3 scripts/prepare.py orig/CUSA13795 --out out
python3 scripts/link_modules.py orig/CUSA13795 --out out --modules libc.prx libSceFios2.prx libSceNpToolkit2.prx
python3 tools/gen_import_names.py --out out
python3 scripts/content_profile.py orig/CUSA13795 --out out
bash build.sh
out/sacredfire out/boot-linked.bin --import-names out/import_names.txt --content-profile out/content.bin \
    --app0 orig/CUSA13795 --user user --timeout 0
```

`out/sacredfire out/boot-linked.bin --list-imports --import-names out/import_names.txt` prints how every
import binds without running the game (GPU imports show as unresolved there: they bind at GPU init).

## Current blocker

None (no STOP). The game runs through license screens and intro videos. Next: reach the main
menu, then a race; implement imports as they are hit (libSceFont and the NpToolkit2 request paths
have not been reached yet).

## Memory (menus, run 12)

RSS about 1.1 GB, VRAM about 1 GB, Vulkan allocations 1.6 GB; the Steam Deck's 14 GiB are enough
with the desktop apps closed.

## Measured at the milestones (run 29, 2026-10-09; `free -g` total/used/free/shared/cache/available, game process RSS)

| milestone | time | free -g | RSS |
|---|---|---|---|
| (a) entry + libc init | 16:39:07 | 14/9/2/0/2/4 | 502,156 KiB |
| (b) first frames (24 FPS, 105 compiles) | 16:39:13 | 14/10/2/0/2/4 | 1,113,012 KiB |
| (c) driver select with the pad | 16:40:02 | 14/11/1/1/2/3 | 1,512,808 KiB |
| (d) race loading | 16:40:02 | 14/11/1/1/2/3 | 1,515,908 KiB |

## Known issues

- **Adventure hub sky stripes** (fixed 2026-10-09, run 31): the frame analyzer showed the final
  colour-grading pass sampling a 1024x32 lookup table built from height-1 render targets that the
  game samples as 1D textures; the cache gave those 1D views a separate image filled from guest
  memory (never written by the GPU), so the LUT was garbage and gradients banded. Fix: 1D images
  are lowered to 2D with height 1 in the SPIR-V backend (image type, coordinates, offsets,
  derivatives, size queries), views and images (the 1D part of shadPS4 #5133/#5171, without the
  cube-map changes that crashed RADV here). User-confirmed.
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

## Launcher and packaging (2026-10-09)

- `bash launcher/bb-launcher.sh` starts the GTK4 launcher (tested in the container after a full
  package upgrade repaired PyGObject); `python3 launcher/sacredfire_launcher.py --play` starts
  the game with the saved settings (tested: PS4 Pro mode on, Pro 1080p patch, two controller
  ports, 30 FPS).
- `packaging/appimage.sh` builds `dist/SacredFire-CTR-x86_64.AppImage` (841 MB) with `nix bundle`.
  Built and tested on 2026-10-09 in the archbuild container after installing single-user Nix
  (`pacman -S nix`, /nix owned by the user, nixos-unstable channel): the runtime and GPU library
  were rebuilt inside `nix-shell shell.nix` so their RUNPATHs name store paths, then bundled.
  `AppRun --play` from the extracted image boots the game with a fresh data directory
  (~/.local/share/sacredfire: generated files, saves, shader cache, bbport.ini). Two fixes came out
  of it: the package now carries tools/gen_import_names.py (run.sh needs it), and run.sh exports
  BB_GPU_USER_DIR and creates the user directory before the game starts (the GPU library builds
  its path table when it loads and fell back to ~/.local/share/shadPS4 on a first launch, which the
  per-launch shader cache clearing would have missed). Steps in packaging/appimage.sh's header.

## 2026-10-09, later: shader cache kept, executable renamed, game icon

- The shader/pipeline cache is kept across launches (two fixes in 06-fur.md: the geometry
  specialization comparison and the preload fetch shader). `BB_CLEAR_SHADER_CACHE=1` or the
  launcher's "Clear the shader cache at start" deletes it. User-confirmed: fur right on a fresh
  cache and on the next launch with the cache preloaded.
- The game executable is `out/sacredfire` (bbport's `bb-probe`; the packaged programs are
  `sacredfire`, `sacredfire-game`). `BB_PROBE` still names a wrapper around it.
- The window icon is the game's own `sce_sys/icon0.png`, read from the game folder at start
  (gpu/shim/window.cpp decodes the PNG with miniz; nothing from the dump enters the repository).
  launcher/install-desktop.sh and the launcher copy it into the user's icon theme as
  io.github.sacredfire.Launcher for the menu entry and the launcher window.
- AppImage rebuilt (2026-10-10) with the renamed executable, the icon and the cache fixes:
  `dist/SacredFire-CTR-x86_64.AppImage`, programs `sacredfire` (launcher) and `sacredfire-game`.
  Repository: https://github.com/T3CHNOLOG1C/SacredFire.
