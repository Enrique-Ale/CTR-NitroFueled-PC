# SacredFire: Crash Team Racing Nitro-Fueled (PS4, CUSA13795 v1.21) as a native Linux x86-64 executable

Project root: /run/media/deck/sd/projects/SacredFire/. Starting point: bbport
(https://github.com/deadinside28/bloodborne_pc, GPL-2.0-or-later). Goal: take the game as far
toward a first boot as possible, then on toward a playable race.

This file is the handoff. Section 0 lists facts that were verified on 2026-10-09 so they need
not be rediscovered. Sections 1 to 8 are the work, in order. Section 9 is how to work and
report. Section 10 is the hard rule about the game files; read it first.

---

## 0. Verified facts (2026-10-09)

### 0.1 Machine and environment

- Steam Deck (AMD Custom APU 0932, 8 threads, 14 GiB RAM, RADV "VANGOGH", Vulkan 1.4).
  Only about 3 GiB of RAM was free during the survey: desktop apps (two Claude processes,
  ChatGPT, Discord) take ~2 GiB. A PS4 game maps several GiB of direct memory, so close
  other apps before run attempts and watch `free -g`.
- This session runs inside a **podman container named `archbuild`** (image
  `docker.io/library/archlinux:latest`, hostname `steamdeck`, root is fuse-overlayfs). The
  user `deck` has **passwordless sudo inside the container** (`sudo -n true` works), so
  `sudo pacman -S` installs packages into the container, not onto the host. This counts as
  the "distrobox container" the original plan preferred; no need to ask about Nix.
  Pacman's sync database is dated 2026-10-08. Nix and distrobox are absent. The container
  sees the display (`DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, `/tmp/.X11-unix/X0`) and the
  GPU (`vulkaninfo` lists RADV VANGOGH).
- Toolchain present: gcc 16.2, clang 22.1, cmake 4.4, ninja, make, pkg-config, python 3.14
  (no pytest; bbport's Python tests use `unittest`), git, glslang (`glslang`/
  `glslangValidator`) and glslc.
- Libraries present (pkg-config): vulkan 1.4.357, sdl3 3.4.16, fmt 12.2, libavcodec/
  libavformat/libavutil/libswscale/libswresample (ffmpeg 8), libxxhash 0.8.3, x11, xcb,
  wayland-client, freetype2 26.6 (for the libSceFont work). Headers present: vulkan,
  SDL3, xxhash, spirv/unified1 (spirv-headers).
- Libraries **missing** that gpu/CMakeLists.txt requires:
  - in the Arch repos (install with `sudo pacman -S --needed`): `boost` (header-only use:
    container/small_vector, static_vector, intrusive, icl, pool, preprocessor), `robin-map`,
    `miniz`, `zydis` (pulls `zycore-c`), `spirv-cross`.
  - not in the repos (header-only, fetch from GitHub into `out/deps/` and install with
    `cmake --install` into `out/deps/prefix`, then pass `-DCMAKE_PREFIX_PATH`): `magic_enum`
    (Neargye/magic_enum), `VulkanMemoryAllocator` (GPUOpen-LibrariesAndSDKs, needs its
    CMake config), `xbyak` (herumi/xbyak; used by
    gpu/shadps4/shader_recompiler/ir/passes/flatten_extended_userdata_pass.cpp).
- Disk: the SD card has 145 GB free. The container's /home overlay reports 33 GB free.

### 0.2 Game files

- `orig/CUSA13795/` is a read-only tree of 709 symlinks: the 1.21 update
  (`/home/deck/Emulation/storage/shadps4/games/CUSA13795-patch`, 8.5 GB) laid over the base
  (`.../CUSA13795`, 14 GB). shadPS4 plays the game from those two folders.
- `param.sfo`: TITLE_ID CUSA13795, APP_VER 01.21, CATEGORY gp, SYSTEM_VER 0x06008000
  (SDK 6.00), CONTENT_ID UP0002-CUSA13795_00-CRASHTEAMRACING1.
- `eboot.bin`: 52,783,970 bytes, decrypted fake-SELF (magic 4F 15 3D 1D), sha256
  `a12aefe380e8607e6c85f1c8bb72da184107d2c0d7b19a16e99f962d80b201de`.
- bbport's `game_check.image_sha256(game)` (sha256 of the mapped PT_LOAD image) on this dump
  is `2f2115893ac6656925d20a0221f118cff398fbd754cb9c10154451411dbcf8e6`. Bloodborne's is
  `071df19c...`. Use the CTR hash in step 4.
- `sce_module/`: libc.prx, libSceFios2.prx, libSceNpToolkit2.prx (914,118 bytes, 303
  imports, 2932 exports), libSceFace.prx, libSceHand.prx, libSceS3DConversion.prx. The eboot
  does not list Face/Hand/S3DConversion in its needed modules; ignore them.
- The eboot needs 52 modules and declares 53 libraries. Full list in `out/analysis.json`
  after step 2 (`needed`).

### 0.3 bbport, as cloned (HEAD daa7165 "0.4", 144 commits)

- Layout: `scripts/` (prepare.py, link_libc.py, link_modules.py, content_profile.py,
  game_check.py, mods.py, patches.py), `src/` (probe.c loader, runtime_*.c, runtime.h,
  import_names.inc), `gpu/` (vendored shadPS4 video core, `gpu/shim/` bbport modules,
  `gpu/third_party/` gcn, half, sirit, fonts + submodules fsr-vulkan and imgui),
  `third_party/LibAtrac9` (submodule), `tests/` (Python `unittest` + C/C++ tests),
  `launcher/` (GTK4), `patches/Bloodborne.xml`, `tools/`, `docs/`.
- Build: `bash build.sh` (CMake+Ninja for `out/gpu/libbbgpu.so`, then one gcc command for
  `out/bb-probe`; `--test` builds and runs the C tests). Python tests:
  `python3 -m unittest discover -s tests`. `run.sh` chains mods.py → prepare.py →
  link_libc.py → link_modules.py → content_profile.py → patches.py → build.sh → bb-probe.
- **Import resolution, as it is today** (the thing step 3 changes):
  - prepare.py writes every import's *scoped* symbol string (`NID#lib#mod`, where `lib` and
    `mod` are the eboot's own base-64 library/module indices, e.g. Bloodborne libc `#q#q`,
    CTR libc `#BI#z`) into `out/boot.bin` as 128-byte records.
  - probe.c (585 lines) calls `runtime_resolve(name, is_data)` for each import; unresolved
    functions get a lazy trap that prints `STOP: first unsupported PS4 import: <name>`.
    `--strict-imports` makes unresolved imports fatal at load.
  - `src/runtime.c:runtime_resolve` has ~18 hardcoded `strcmp(name, "NID#q#q")` lines, then
    chains `runtime_<area>_resolve(name)` for mutex, thread, content, time, sema, rwlock,
    memory, kernel, services, ajm, audio, pad, rtc, savedata, file, and finally
    `bbgpu_resolve`.
  - The area resolvers use `RUNTIME_LOOKUP(table, nid)` → `runtime_lookup`, which maps the
    scoped NID to a symbol *name* via `runtime_symbol()` (a linear scan of
    `src/import_names.inc`, 747 scoped entries generated for Bloodborne's eboot) and then
    matches the name against `RuntimeExport{name, function}` tables. So most tables are
    keyed by **symbol name**, and the only Bloodborne-specific part is import_names.inc's
    suffixes plus the hardcoded strcmp lines. A few files also carry scoped names directly:
    runtime_mutex.c (43), runtime_thread.c (19), runtime.c (18), runtime_rwlock.c (9),
    runtime_content.c (6), runtime_sema.c (6), runtime_memory.c (5), runtime_time.c (1).
  - `gpu/shim/bbgpu.cpp:bbgpu_resolve` compares only the bare NID (text before `#`), which is
    why GnmDriver (86) and VideoOut (14) already resolve for CTR.
  - `scripts/link_modules.py:module()` already decodes the suffix indices into
    `(library name, version)` and `(module name, ...)` from the PRX/eboot dynamic tags
    0x61000013/15 (libraries) and 0x6100000d/0f (modules), using `link_libc.encode_id`.
    Reuse this in prepare.py.
- **Bloodborne-specific hooks keyed on code offsets** are all opt-in or self-checking:
  `gpu/shim/bbport_gnm_hooks.cpp` (`PatchImage`, 518-entry table of libGnm offsets) returns
  immediately unless `BB_GNM_CENSUS=1` or `BB_GNM_OBSERVE=1`; `bbport_guest_hooks.cpp`
  (`BB_GUEST_IN_PLACE`, the PC memory model) and `bbport_heap_sites.cpp` (`BB_HEAP_SITES`)
  compare bytes before patching and skip on mismatch. The default mode
  (`BB_GUEST_IN_PLACE=0`, "as in 0.3") needs none of them. They must still be made
  explicitly inert for a non-Bloodborne image (step 4) so no one turns them on by accident.
- Other Bloodborne-specific pieces: `scripts/game_check.py` (hash + 1.09 check, called from
  prepare.py and the launcher), `scripts/patches.py` + `patches/Bloodborne.xml`
  (frame-rate, resolution and effect patches, compiled to `out/patches.bin`),
  `scripts/content_profile.py` (SKU/DLC profile for AppContent), `scripts/mods.py`, the
  upscaler/motion-vector/TAA modules in gpu/shim and gpu/shadps4 (controlled by settings,
  default FSR path expects Bloodborne's render targets), hardcoded guest addresses in
  `src/runtime_services.c`, `runtime_audio.c`, `runtime_ajm.c`, `runtime_pad.c`,
  `runtime_content.c`, `runtime_memory.c` (audit each `0x[0-9a-f]{6,}` constant: most are
  SCE error codes, a few are Bloodborne addresses).
- `src/runtime_services.c` (473 lines) already models "offline console, one local user
  (id 1) logged in but not signed in to PSN, network cable unplugged, dialogs complete
  immediately, PlayGo all-local", with explicit contracts for UserService, SystemService,
  Net/NetCtl/Ssl/Http, NpManager/NpAuth/NpLookup/NpScore/NpWebApi/NpMatching2/NpSignaling/
  NpTrophy, Voice, AudioIn, CommonDialog/MsgDialog/SaveDataDialog/NpProfileDialog/
  NpCommerceDialog/ImeDialog, PlayGo, Mouse, DiscMap. Extend this file; keep its style.
- The file layer (`src/runtime_file.c:do_open`) maps guest open flags straight to host
  flags including O_WRONLY/O_RDWR/O_CREAT. Step 1.4 adds the /app0 read-only guard.

### 0.4 Measured on the CTR dump (reproduced 2026-10-09; rerun in step 2)

With `BB_SKIP_GAME_CHECK=1`, unmodified prepare.py (2.2 s) and link_modules.py (1.8 s):

| quantity | value |
|---|---|
| image | 46,065,261 bytes, entry 0x80 |
| relocations | 473,760 (type 8: 471,972; type 1: 1,080; type 7: 708), all supported |
| imports | 713 from 52 modules |
| bound natively to libc.prx@0x2bf0000 and libSceFios2.prx@0x2d20000 | 184 |
| left to the host runtime | 618 (`link.json` → `unresolved_imports`) |
| fs→gs TLS patches in the linked modules | 3,747 |
| `libc _init_env verified RET` | True |

Host-side imports by library (from the eboot's own library table), with the count whose
aerolib name appears quoted anywhere in bbport's C/C++ (a rough lower bound of what is
implemented; the real number after step 3 will be higher because several tables are keyed by
scoped NID):

| library | imports | name quoted in bbport |
|---|---|---|
| libkernel | 146 | 49 |
| libSceGnmDriver | 86 | resolves by bare NID today |
| libSceNpToolkit2 | 80 | 0 |
| libSceFont + libSceFontFt | 39 + 2 | 0 |
| libSceNet | 30 | 29 |
| libScePosix | 29 | 1 |
| libSceHttp | 20 | 14 |
| libSceVideoOut | 14 | resolves by bare NID today |
| libSceNpManager | 12 | 8 |
| libScePlayGo | 12 | 4 |
| libSceAjm | 12 | 12 |
| libSceVoice | 11 | 11 |
| libSceSaveData | 10 | 8 |
| libScePad | 8 | 6 |
| libSceNpCommerce 7, libSceJson2 7, libSceUserService 6, libSceNpTrophy 6, libSceVideoRecording 6, libSceWebBrowserDialog 6, libSceAudioOut 6, libSceImeDialog 5, libSceNpAuth 5, libSceContentExport 5, libSceMsgDialog 5, libSceInvitationDialog 5, libSceSystemService 4, libSceSysmodule 4, libSceSharePlay 4, libSceSaveDataDialog 4, libSceAppContent 3, libSceNetCtl 3, libSceGameLiveStreaming 3, libSceAudioIn 3, libSceScreenShot 2, libSceSsl 2, libSceRandom 1, libSceCommonDialog 1, plus 4 with no library suffix (sceLibcHeapGetTraceInfo, sceDiscMapIsRequestOnHDD, one unnamed libSceDiscMap NID, Need_sceLibcInternal) | | |

- 609 of the 618 bare NIDs have a name in shadPS4's `src/core/aerolib/aerolib.inl`
  (171,520 lines, 59,307 `STUB("NID", name)` entries; fetch it from the shadPS4 repo at
  build time or vendor it under `tools/` with its GPL notice). The 9 unnamed: 8 are
  libSceNpToolkit2 C++ symbols (`cmvr0mOCX2Q`, `XIbXIDTEUpo`, `7zsee5IHLis`, `xotzrkLV-aU`,
  `YlpnKm99h3Q`, `9G7fpmodu1Q`, `SX9IOde7tLo`, `U4ERH3U6KoE`) and 1 is libSceDiscMap
  (`fJgP+wqifno`). Name them by brute force: NID = first 8 bytes of
  SHA-1(name + fixed suffix) base-64 (prepare.py's `nid()`); candidates are the obvious
  constructors/destructors/`get()` of the Toolkit2 Response/Request types already seen.
- libkernel names not found quoted in bbport (so to confirm after step 3): the scePthread*
  family, sceKernelCreate/Delete/Signal/Wait/PollSema, `__error`, `__tls_get_addr`,
  `__stack_chk_guard/fail`, sceKernelCreateEqueue, sceKernelIsNeoMode, sceKernelGetCpumode,
  sceKernelGetCurrentCpu, sceKernelGetdents, sceKernelDirectMemoryQuery,
  sceKernelCreate/Set/Wait/DeleteEventFlag, _sceKernelSetThreadAtexitCount,
  _sceKernelSetThreadAtexitReport, _sceKernelSetThreadDtors, pthread_cond_broadcast.
  libScePosix: pthread_mutex*/attr*/getschedparam/setschedparam/self/join/create_name_np,
  and BSD sockets (socket, bind, listen, accept, connect, send, recv, select, setsockopt,
  inet_ntop, inet_pton). Most pthread names exist in bbport under scoped NIDs; the sockets
  and sched calls are new.
- libSceNpToolkit2's 80 entries are C++ (`sce::Toolkit::NP::V2::...`): Core::init/term,
  InitParams/TermParams ctors/dtors, CallbackEvent copy/dtor, `Response<T>` ctor/dtor/`get()`
  for T in Empty, Session, SessionId, Commerce::Products, Commerce::ServiceEntitlements,
  Auth::AuthCode, UserProfile::NpProfiles, Friend::Friends,
  NpUtils::Notification::UserStateChange; `ResponseBase::getReturnCode/getState/
  getServerError`; Session::create/join/leave/update/getInfo + SendInvitation requests and
  their Request structs; Auth::GetAuthCode; Commerce::GetProducts/GetServiceEntitlements;
  UserProfile::GetNpProfiles; Friend::GetFriends; Session/SessionId/Friends/NpProfiles
  value types (ctor/dtor/operator=).
- libSceNpToolkit2.prx itself imports 303 symbols from 28 libraries: NpTus 41,
  NpMatching2 35, libkernel 33, LibcInternal 32, NpWebApi 20, NpManager 18, NpScore 18,
  NpTrophy 16, Http 14, NpUtility 10, NpCommerce 7, five dialog libraries 5 each
  (GameCustomData, Invitation, NpProfile, NpSnsFacebook, Signin), NpAuth 5, Rtc 4, Net 3,
  NetCtl 3, NetCtlForNpToolkit 3, NpManagerForToolkit 3, NpSns 3, Ssl 3, Sysmodule 3,
  UserServiceForNpToolkit 2, SystemService 1, UserService 1.
- libSceFont (41): sceFontMemoryInit/Term, CreateLibraryWithEdition, DestroyLibrary,
  SupportSystemFonts, SelectLibraryFt, SelectRendererFt, OpenFontSet, CloseFont,
  CreateRendererWithEdition, DestroyRenderer, BindRenderer, SetScalePixel,
  RenderSurfaceInit, RenderCharGlyphImageHorizontal, TextSourceInit/SetWritingForm/
  SetDefaultFont, CreateString/DestroyString, StringGetWritingForm/GetTerminateOrder/
  GetTerminateCode/RefersRenderCharacters/RefersTextCharacters, CreateWritingLine/
  DestroyWritingLine, WritingInit, WritingLine{Clear,GetOrderingSpace,GetRenderMetrics,
  RefersRenderStep,WritesOrder}, WritingGetRenderMetrics, WritingRefersRenderStep,
  WritingRefersRenderStepCharacter, Character{GetBidiLevel,GetTextFontCode,GetTextOrder,
  LooksWhiteSpace,RefersTextNext}.
- libSceJson2 (7): `sce::Json::Initializer` ctor/dtor/initialize/terminate,
  `sce::Json::MemAllocator` ctor/dtor/notifyError. Pure C++ ABI stubs, small.

---

## 1. Set up the repository (milestone: untouched bbport builds here)

1.1 Make this folder the bbport clone while keeping `orig/` and `plan.md`:
```bash
cd /run/media/deck/sd/projects/SacredFire
git init -b main
git remote add upstream https://github.com/deadinside28/bloodborne_pc.git
git fetch upstream
git reset --hard upstream/master        # orig/ and plan.md are untracked and stay
git submodule update --init --recursive # gpu/third_party/fsr-vulkan, gpu/third_party/imgui, third_party/LibAtrac9
```
Keep `LICENSE` (GPL-2.0-or-later) and the upstream history. Add a `SacredFire` section at
the top of README.md later; do not delete the Bloodborne README text.

1.2 `.gitignore`: append `orig/`, `out/`, `user/`, `pgo/`, `logs/`, `bbport.ini`. Check with
`git status --ignored` that nothing under orig/ is ever listed as untracked. Never commit
game files or anything derived from them (images, analysis.json, link.json, caches).

1.3 Dependencies (inside the `archbuild` container):
```bash
sudo pacman -S --needed boost robin-map miniz zydis spirv-cross
```
Write `tools/deps.sh` that clones pinned tags of magic_enum, VulkanMemoryAllocator and
xbyak into `out/deps/src/`, installs them with `cmake --install` into `out/deps/prefix`,
and is idempotent. Make `build.sh` export
`CMAKE_PREFIX_PATH=$PWD/out/deps/prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}` and call
`tools/deps.sh` when the prefix is missing. Keep the Nix path working (do not remove
shell.nix; add the three libraries to it as well for NixOS users).

1.4 Build the untouched tree: `bash build.sh` must produce `out/gpu/libbbgpu.so`,
`out/bb-probe`, `out/bb-gpu-capabilities`. Then `bash build.sh --test` and
`python3 -m unittest discover -s tests` must pass. Fix only environment problems (missing
dependency, compiler version warnings under `-Werror`); record each fix in
`docs/sacredfire/00-setup.md`. Try `out/bb-probe --vulkan-only` (the Vulkan smoke test) to
confirm the GPU is reachable from the container.

1.5 /app0 read-only guard (hard rule, section 10): in `src/runtime_file.c:do_open`, when the
resolved host path is under the `--app0` directory and the guest asks for O_WRONLY, O_RDWR,
O_CREAT, O_TRUNC or O_APPEND, return the SCE error for a read-only file system
(`0x8002001e`, EROFS as ORBIS errno 30) and print
`Runtime: refused write open(%s)`. Do the same for any unlink/rename/mkdir/truncate entry
points the file layer exposes when the path is under /app0. Add a unit test in
`tests/test_runtime.c` or a new `tests/test_file_guard.c`. Keep the scripts opening game
files with `read_bytes()`/`open(..., 'rb')` only (they already do; grep for `'w'` and
`write_bytes` to confirm every write goes under `--out`).

1.6 Commit: "SacredFire: repository setup, dependency script, /app0 write guard".

## 2. Reproduce the measurements (milestone: numbers in 0.4 confirmed from this tree)

```bash
BB_SKIP_GAME_CHECK=1 python3 scripts/prepare.py orig/CUSA13795 --out out
python3 scripts/link_modules.py orig/CUSA13795 --out out
```
Expect 713 imports / 473,760 relocations / 52 modules / 184 native / 618 host. Write
`tools/import_report.py` that reads `out/analysis.json`, `out/link.json` and the eboot's
library table (via `link_modules.module`) and prints the per-library table of 0.4 plus, per
import, whether the runtime resolves it (see 3.6 for how to ask the runtime). Keep the
report under `out/`; paste the summary into `docs/sacredfire/01-imports.md`. Also run the
scripts on a Bloodborne dump if one is available (ask; `BB_GAME_DIR`), otherwise rely on the
unit tests in `tests/test_prepare.py`, `test_link_libc.py` and the fixtures there.

## 3. Import resolution independent of per-executable suffixes

Design: the loader hands the runtime `bare NID` + `library name`, never the eboot's own
index suffixes. Names come from a table generated for the eboot being prepared.

3.1 prepare.py: after building `names` (the scoped import strings), decode each with the
same library/module tables `link_modules.module()` reads (move that decoding into a shared
helper in prepare.py, import it from link_modules.py). Write the imports into `boot.bin` in
a new record format: 11-byte NID + NUL, library name + NUL, in a 128-byte record as before,
with a new magic `BBPROBE3` so an old loader refuses a new image and vice versa. Keep the
`imports` list in analysis.json as `{"nid", "library", "module", "scoped"}` objects.

3.2 Name table: write `tools/gen_import_names.py` that takes `out/analysis.json` and
aerolib.inl (path argument; default fetches
https://raw.githubusercontent.com/shadps4-emu/shadPS4/main/src/core/aerolib/aerolib.inl
into `out/aerolib.inl` once) and emits `out/import_names.inc` with
`{"NID", "library", "symbol"}` rows for every import of this eboot and of the linked libc/
Fios2 modules that stay unresolved. Entries without a name get the symbol `"?"`. prepare.py
runs it as its last step. The runtime loads the table at start from `--import-names
out/import_names.inc` (plain text, parsed by probe.c) instead of compiling
`src/import_names.inc` in; keep the Bloodborne file as a fallback for `--cpu-only` tests, or
delete it once the generated path is proven on both games.

3.3 runtime: change `runtime_resolve(const char *name, int is_data)` to
`runtime_resolve(const char *nid, const char *library, int is_data)` and `runtime_symbol`,
`runtime_lookup`, `runtime_import_name` accordingly (lookup by NID, then by symbol name as
now). Replace every hardcoded `strcmp(name, "NID#x#y")` in runtime.c and the 7 other files
with the bare NID (and the library where two libraries share a NID with different
contracts; libkernel vs libScePosix same-name functions are already treated as one contract
by `runtime_lookup`). `bbgpu_resolve` already compares bare NIDs; give it the same
signature for symmetry.

3.4 The eboot-local `#lib#mod` scoping still matters in one place: link_modules.py binds
eboot imports to libc.prx/Fios2.prx exports by `(NID, library, module)` identity; leave that
as is.

3.5 Tests: extend `tests/test_prepare.py` with a fixture whose dynamic section declares two
libraries with different indices than Bloodborne's and check the decoded names; add a C test
that `runtime_resolve("bzQExy189ZI", "libc", 0)` returns `init_env` and that an unknown NID
returns 0. All existing tests stay green.

3.6 Diagnostics: give bb-probe `--list-imports`, which loads the image, resolves every import
and prints `resolved|unresolved <library> <symbol> <nid>` without running the game. The
import report of step 2 uses this. After 3.x, the Bloodborne import set must resolve exactly
as before (compare `--list-imports` output of upstream HEAD and the new tree if a Bloodborne
dump exists; otherwise compare the Bloodborne entries of `src/import_names.inc` run through
the new lookup).

3.7 Expected result for CTR: everything in the 0.4 table that bbport already implements
resolves (the user's earlier estimate was 269 more). Record the new resolved/unresolved
counts per library in `docs/sacredfire/01-imports.md`.

3.8 Commit per sub-step; "import resolution by NID and library name" as the summary.

## 4. Remove Bloodborne gating, keep Bloodborne working

4.1 `scripts/game_check.py`: turn the single supported image into a table
`SUPPORTED = {image_sha256: dict(title, version, name, profile)}` with both Bloodborne 1.09
(`071df19c...`) and CTR 1.21 (`2f2115893ac6656925d20a0221f118cff398fbd754cb9c10154451411dbcf8e6`).
`problem()` returns the profile name or a reason; prepare.py writes `game_profile` into
analysis.json and `out/profile.txt`. Messages for CTR: "needs the 1.21 update merged over the
base game" when param.sfo is CUSA13795 but the hash differs. Update `tests/test_game_check.py`.

4.2 `run.sh`: read the profile and skip, for CTR, `content_profile.py` (write an empty or
CTR-specific content.bin: CTR has no Bloodborne SKUs; check what `runtime_content.c` expects
when the file is absent and make absence legal), `patches.py` (`--patches` omitted; make
probe.c accept a missing patches file), the render-resolution/FPS logic, FSR asset steps and
`mods.py` (keep it only if it is a no-op without a mods dir). Default `BB_GAME_DIR` to
`orig/CUSA13795` when the profile is CTR, else `../CUSA03173`. Keep `BB_FPS`, `BB_RENDER_RES`
unset for CTR.

4.3 GPU side: add a `profile` field to the settings passed to `bbgpu_init` (or an env
`BB_GAME_PROFILE` exported by run.sh) and make these return early when it is not
Bloodborne: `BbGnmHooks::PatchImage`, `BbGuestHooks::Install`, `BbHeapSites`,
`BbFreeCheck` trap setup, the upscalers (FSR 3.1/4/4.1.1), motion vectors, TAA, the
render-scale proxies (`SceneTargets::Eligible`), DoF/SSAO/SSR effect toggles, and anything in
`gpu/shim/bbport_overlay.cpp` that reads Bloodborne scene constants. Default output for CTR:
present the game's own swap chain at its native size, no upscaler, overlay limited to the
FPS/stat lines. Grep for `0x8` guest addresses, `1.09`, `CUSA03173`, `Bloodborne` across
`gpu/shim`, `src`, `scripts`, `launcher` and list every hit with its disposition in
`docs/sacredfire/02-bloodborne-gating.md`.

4.4 Runtime constants: audit every `0x[0-9a-f]{6,9}` in `src/runtime_*.c`. Error codes
stay; guest addresses (e.g. the GPU range allocator in `bbport_guest_hooks.cpp`, any
`ImageBase + offset` in runtime_services.c / runtime_pad.c / runtime_audio.c) move behind
the profile check.

4.5 Bloodborne must still boot in principle: all Bloodborne tests pass, and `--list-imports`
on the Bloodborne entries is unchanged. Do not remove Bloodborne support (expensive to
reverse; ask first if something seems to require it).

4.6 Commit: "game profiles: Bloodborne 1.09 and CTR 1.21; Bloodborne-only features gated".

## 5. First boot: run to the STOP, implement what is hit

```bash
BB_GAME_DIR=orig/CUSA13795 bash run.sh --timeout 120
```
(`--timeout` seconds is a probe argument; `BB_SAVE_LOG=1` keeps the log in `logs/`.)

5.1 Loop: run; read the last `STOP: first unsupported PS4 import` or the crash
(`probe.c` prints the guest RIP relative to the image base 0x800000000; use
`tools/dump_view.py`/`eboot.elf` with objdump for the code around it); implement the
import in the matching `src/runtime_*.c` with an explicit contract comment; rebuild; repeat.
Order of appearance will likely be: libkernel thread/mutex/sema/event-flag/CPU-mode
queries, `sceSysmoduleLoadModule` for each module in the needed list (return OK for every
module the runtime models; the loaded-module handles are opaque), libSceNpToolkit2
Core::init, libSceFont setup, Json2 Initializer, UserService, SaveData, Pad, AudioOut,
VideoOut/Gnm, Rtc, Random, PlayGo, ScreenShot, VideoRecording, SharePlay, GameLiveStreaming,
ContentExport, WebBrowserDialog, InvitationDialog.

5.2 Contracts for an offline console with one signed-out user (extend runtime_services.c's
model): Np* calls return `NP_SIGNED_OUT` (0x80550006) or "state offline"; NetCtl state
disconnected; Http/Ssl init succeed, requests fail with HTTP_NETWORK; sockets: `socket()`
succeeds, `connect/bind/listen/accept` fail with `NET_ENETUNREACH`; `select` with no ready
fds returns 0 after the timeout; VideoRecording/SharePlay/GameLiveStreaming/ScreenShot report
"not available/prohibited" where the API has such a state and OK for pure
set-parameter calls; PlayGo: all chunks local (`scePlayGoGetLocus` → LOCAL_FAST), install
speed accepted; ContentExport/WebBrowserDialog/InvitationDialog: initialize OK, open returns
"not supported" status, `UpdateStatus` → FINISHED with user-cancelled result;
`sceKernelIsNeoMode` → 0 (base PS4) unless `BB_NEO=1`; `sceKernelGetCpumode` → 5 (6 CPUs
mode); `sceKernelGetCurrentCpu` → `sched_getcpu() % 7`; event flags: implement on
pthread mutex+condvar with the ORBIS wait modes (AND/OR, CLEAR_ALL/CLEAR_PAT), as bbport's
sema does; `sceKernelGetdents` over the host directory (the file layer has the path map);
`_sceKernelSetThreadAtexitCount/Report`, `_sceKernelSetThreadDtors` record the callbacks and
call them from the thread exit path (SDK 6.00 libc expects them); `sceLibcHeapGetTraceInfo`
→ fill zeros, return OK; `sceKernelDirectMemoryQuery` from the direct-memory map in
runtime_memory.c; `sceSysmoduleGetModuleInfoForUnwind` → not found.

5.3 No blanket stubs: each new entry has a comment `/* contract: ... */` saying what the
game expects and what we fake, and a line in `docs/sacredfire/03-imports-log.md`
(`symbol | library | what the game does with it | contract | how confirmed (STOP seen /
return checked in disassembly / behaviour in game)`).

5.4 Milestones to report: (a) loader reaches the eboot entry and libc init runs
(`_init_env`, `__cxa_atexit` seen); (b) first `sceVideoOutOpen` and first `SubmitFlip`
(first frame presented, take a screenshot via `tools/shot.sh` or an SDL screenshot key);
(c) main menu reachable with pad/keyboard; (d) one race with sound and a save written under
`user/`. Note timings and the RSS of bb-probe (`free -g`) at each.

5.5 When the GPU side fails (shader recompiler, unimplemented PM4, missing Vulkan format),
check whether upstream shadPS4 has a fix since b4e7ae7 (2026-10-02) and backport it into
`gpu/shadps4` with a `bbport:`/`sacredfire:` marker, as VENDOR.txt documents. CTR runs on
the Alchemy engine (Vicarious Visions, .igz/.igx archives), not FromSoftware's, so expect
shader opcodes, texture formats and PM4 usage the Bloodborne-tuned core has never seen.
Replacing the vendored renderer is out of scope; ask before anything of that scale.

## 6. libSceNpToolkit2: choose the route, then do it

Two routes, with the numbers from 0.4:

A. Link the dump's own `libSceNpToolkit2.prx` natively with link_modules.py (`--modules
libc.prx libSceFios2.prx libSceNpToolkit2.prx`). The game's 80 C++ entry points then bind
to real code. Cost: the PRX's 303 imports; LibcInternal (32) binds to libc.prx as today,
libkernel (33) to the runtime, but ~235 are Np/Http/Net/dialog functions of which
runtime_services.c has maybe 100 after step 3, so roughly 130 new contracts across NpTus,
NpMatching2, NpWebApi, NpScore, NpTrophy, dialogs, NetCtlForNpToolkit,
NpManagerForToolkit, UserServiceForNpToolkit. Also the PRX's `module_start` must run
(constructors, its own worker threads, timers) and every request it issues must fail
cleanly offline. Benefit: exact C++ ABI/layouts for free; Session/Friends/Commerce code
paths behave like a real offline console.

B. Implement the 80 entry points in C++ (`src/runtime_nptoolkit2.cpp`, compiled into the
runtime or into libbbgpu.so) as thin ABI shims: ctors zero their object, dtors no-op,
`Core::init` returns an error for "not signed in" or OK with state offline, every
`Request::*` call completes the `Response<T>` immediately with `getState()` = finished and
`getReturnCode()` = NP_SIGNED_OUT, `get()` returns a pointer to a zeroed T. Cost: the
sizes/layouts of `Response<T>`, the Request structs and CallbackEvent (take them from the
PRX: the ctors are in its export table, disassemble them for the member stores, or measure
from the game's stack usage around the calls). 8 NIDs are unnamed and need their names
brute-forced (0.4). Benefit: ~80 small functions, no network stack, no PRX threads.

Decision rule: start by running to the first Toolkit2 STOP and reading how the game uses
`Core::init` and `CallbackEvent`. If the game only polls Response objects and reacts to
error codes (most offline-capable titles do), take B. Take A only if the game depends on
Toolkit2 side effects we cannot see (callbacks delivering friend lists before the menu
opens). Write the comparison and the decision in `docs/sacredfire/04-nptoolkit2.md` before
implementing. Recommendation from the survey: B.

## 7. libSceFont: FreeType-backed implementation

Firmware `libSceFont.sprx`/`libSceFontFt.sprx` are not in the dump and are Sony system
files, so linking them the way libc is linked is not available here (and would carry
redistribution problems). Plan for B only: implement the 41 functions in
`src/runtime_font.c` over FreeType (present: freetype2 26.6; add `freetype2` to build.sh's
pkg-config list). Model: `sceFontMemoryInit` records the game's allocator callbacks and all
font memory goes through them; `OpenFontSet` maps the SCE system font set ids to a bundled
open font (DejaVu Sans / Noto Sans from the container, copied under `assets/fonts/` with
its licence; check `gpu/third_party/fonts` first, it may already have one);
`SetScalePixel` sets the FreeType size; `RenderCharGlyphImageHorizontal` renders into the
game's `RenderSurface` (8-bit alpha, pitch from `RenderSurfaceInit`); the String/Writing/
Character API is a layout engine: implement left-to-right single-direction layout (bidi
level 0), whitespace detection and line metrics from FreeType's advances. Confirm the
expected struct layouts from the game's use (it fills `SceFontRenderSurface`,
`SceFontGlyphMetrics`, `SceFontRenderMetrics`; the field layout is documented in public PS4
SDK headers, and shadPS4 has `libraries/font` stubs with the structs). Log each in
`docs/sacredfire/05-font.md`. Start it when the first sceFont STOP appears; the game very
likely needs it before the main menu (player names, menus).

## 8. Later: overlay file layer

Once the game boots: teach `runtime_file.c` (and the scripts' `game` path handling) to take
`--app0 base --app0-update patch` and resolve each guest path in the update first, then the
base, so the symlink tree in orig/ is no longer required. Directory listings
(`sceKernelGetdents`) merge both. Read-only in both.

---

## 9. How to work

- Work in small commits on `main` (this is a fresh repo; the upstream remote is for
  fetching only; never push to it).
- Keep `docs/sacredfire/` as the project log: `00-setup.md`, `01-imports.md`,
  `02-bloodborne-gating.md`, `03-imports-log.md`, `04-nptoolkit2.md`, `05-font.md`,
  `STATUS.md` (last milestone, current STOP, next step; update at every stop).
- No blanket "return 0" stubs. Each one states the contract it fakes.
- Tests: `bash build.sh --test` and `python3 -m unittest discover -s tests` before every
  commit; add tests for new script logic and for the /app0 guard.
- Stop and ask before anything expensive to reverse: replacing the vendored renderer,
  dropping Bloodborne support, changing the boot.bin format a second time, installing
  packages on the host (the container is fine), anything touching orig/.
- Memory: close other apps, watch `free -g`; if the game maps more direct memory than the
  Deck has, look at `BB_DMEM_MB` in run.sh/probe.c and at how runtime_memory.c sizes direct
  memory, and set it from the profile.
- When you stop, report: the last milestone reached, the exact STOP or crash (symbol,
  library, guest RIP, the last 30 lines of the log), and what you would try next.

## 10. Hard rule: never write into orig/

Never write, create, modify, move, rename or delete anything under orig/, or under the two
dump folders it points to (`/home/deck/Emulation/storage/shadps4/games/CUSA13795` and
`CUSA13795-patch`). No exceptions: no temporary files, no patched copies of eboot.bin in
place, no "just fixing" a link.

Why: orig/ is made of symlinks into the real game dumps, which shadPS4 also uses. A write
through a link silently changes the original file, and there is no other copy. The
directories are mode r-x, but the files behind the links are writable, so nothing stops a
bad write except you.

In practice:
- Open game files read-only (`O_RDONLY`, `'rb'`, `read_bytes()`), in the scripts and in the
  runtime's file layer. If the game asks to open a file under /app0 for writing, fail the
  call and log it (step 1.5); do not pass it through.
- Anything that must be modified is copied to out/ first and changed there.
- All generated output goes under out/ and user/ (and logs/); orig/, out/, user/ are in
  .gitignore; never commit game files or anything derived from them.
- If a step seems to require changing orig/, stop and ask.
- After any run, `find orig -newer plan.md` must print nothing.
