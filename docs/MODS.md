# File mods

*En español: [MODS.es.md](MODS.es.md).*

CTR Nitro-Fueled keeps its data in `archives/` of the game folder: IGA archives (`*.pak`) holding
the game's IGZ (Alchemy) files, next to the Bink videos (`*.bk2`). A file mod replaces whole
archives (or adds ones). `scripts/mods.py` overlays the enabled mods on the game folder at every
start; the game folder itself is never written.

## Installing a mod

Put each mod in its own folder inside the mods folder:

| Install | Mods folder |
|---|---|
| Windows portable | `mods\` next to the launcher (`BB_DATA_DIR`) |
| AppImage | `~/.local/share/sacredfire/mods/` |
| From the repository (`run.sh`) | `mods/` in the repository, or `BB_MODS_DIR` |

Unpack ZIP/7z downloads first. Accepted layouts (names in any case):

```text
mods/
  My kart/
    archives/
      loadingscreen_octane.pak
  Wrapped/
    CUSA13795/archives/...      (also app0/, CUSA14876/ or Image0/)
  Just archives/
    juicedomain_hub.pak         (bare *.pak files go to archives/)
    readme.txt
  Downloaded v1.2/
    Downloaded v1.2/archives/...   (one wrapper folder, as an archive extracted into its name)
```

Any other data folder the game folder has (other than `sce_module` and `sce_sys`) can be replaced
the same way. Readme files, pictures and folders that are not game folders stay out of the game.
The start log says what each mod brought: `Mods: My kart: 1 files`, then
`Mods: 1 game files replaced, 0 added`. A mod that should replace files but says `0 game files
replaced` has the wrong paths inside.

## Order and switches

New mod folders are enabled and appended in alphabetical order. `mods.json` in the data folder
keeps the order and the disabled ones; the mod lower in `order` wins:

```json
{"order": ["First", "Second"], "disabled": ["First"]}
```

When two mods replace the same file the log names both (`Mods: archives/x.pak: First -> Second`).
Archives are never merged: the last one wins whole. `BB_MODS_ENABLED=0` turns all mods off,
`BB_MODS_CONFIG` names another `mods.json`.

A folder `CUSA13795-mods/` beside the game folder (shadPS4's convention) is loaded first. A mods
folder that holds `archives/` itself is loaded as one mod.

```sh
BB_GAME_DIR=/path/to/CUSA13795 BB_MODS_DIR=/path/to/mods bash run.sh
BB_GAME_DIR=/path/to/CUSA13795 BB_MODS_ENABLED=0 bash run.sh
```

`BB_MOD_TRACE=1` (with `BB_MODS_DIR`) logs the first reads of the mods' files.

## Limits

- **Ultrawide** (Settings > Display > Aspect ratio 21:9 or 32:9) edits 13 archives in its own
  prepared view: `h100_hub`, `juicedomain_hub`, `juicedomain_permanent`, `juicedomain_race`,
  `juicedomain_race_flareday`, `juicedomain_race_flarenight`, `juicedomain_startscreen`, `legal`,
  `loadingscreen_octane`, `octane_startscreen`, `update`, `update2`, `update_hub2`. Mods apply on
  top of that view: a mod that replaces one of these archives wins and that part of the screen
  goes back to 16:9.
- The executable and system files (`eboot.bin`, `sce_module/`, `sce_sys/`) are refused; code
  changes are patches (`patches/`, shadPS4 XML). Symbolic links inside a mod are refused.
- Mods must be made for the PS4 game (CUSA13795 1.21, or CUSA14876 for the European edition).
  Archives from the Switch or Xbox versions are a different platform's build and are not expected
  to load. Some of the European edition's files differ from the USA ones in their dependency
  records, so a USA mod may need porting (`tools/igz_merge.py` merges a USA edit into an EU file).

## How it works

The overlay is a tree of links in the data folder (`out/mod-game-*`, beside the game on another
Windows drive without the symlink privilege): every game entry is linked, and each folder a mod
touches is opened one level so only the replaced files point at the mod. The game reads the tree
as `/app0`, read-only. It is removed when the game ends; a tree left by a crash is not reused.
