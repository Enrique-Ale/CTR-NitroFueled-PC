# SacredFire on Windows

SacredFire builds and runs natively on 64-bit Windows 10 (1903+) and 11 with the MSYS2 CLANG64
toolchain, from the same sources as the Linux build. The Windows layer (loader, memory, TLS,
exceptions, files, timers) comes from Supermedo's Windows port of bbport
([Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc), branch `windows`),
merged into SacredFire. Tested on Windows 11 24H2 with an NVIDIA RTX 3070.

## Build

Install [MSYS2](https://www.msys2.org) into `C:\msys64`, open the **MSYS2 CLANG64** shell, run
`pacman -Syu` (twice if it closes the window) and install the toolchain and libraries:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

magic_enum, miniz and xbyak (not packaged by MSYS2) are submodules under `gpu/third_party/`.
Then, in the repository (CLANG64 shell):

```bash
git submodule update --init --recursive
bash build.sh                      # out/sacredfire.exe, out/bb-gpu-capabilities.exe
```

## Game files

As on Linux: your own dump of CUSA13795 with the 1.21 update merged in. With the base game and
the update dumped to two folders, `tools/merge_update.py` makes the merged folder out of hard
links (no copy, the dumps stay untouched; same drive):

```bash
python tools/merge_update.py <path>/CUSA13795 <path>/CUSA13795-patch <path>/CUSA13795-1.21
```

## Run

`run.py` is the Windows `run.sh` (same variables; MSYS2's Python or a python.org Python):

```bat
set BB_GAME_DIR=C:\Games\CUSA13795-1.21
C:\msys64\clang64\bin\python.exe run.py
```

From a source tree it puts `C:\msys64\clang64\bin` (or `%MSYS2_ROOT%\clang64\bin`) on `PATH`
for the DLLs. The in-game menu (Insert, or L3+R3) works as on Linux; "Apply and restart" ends
the game with exit code 75 (`BB_RESTART_EXIT_CODE`) and `run.py` starts it again with the new
patches, in the same console. Every launch is logged to `last_run.log`.
Saves, the shader cache and `bbport.ini` go to `user/` and the repository folder (`BB_DATA_DIR`
moves them).

## What is different from Linux

| Linux | Windows |
|---|---|
| `fs:` loads rewritten to `gs:[0]`, GS base set with `arch_prctl` | GS is the TEB: the loader rewrites them to `gs:[0x1480 + 8*slot]` (a TEB TLS slot, claimed first thing in `main`). |
| One memfd, `mmap(MAP_FIXED)` views | A pagefile-backed section; the PS4 range is reserved as a placeholder that views replace (`VirtualAlloc2`/`MapViewOfFile3`, as in shadPS4). |
| SIGSEGV handler, `sigsetjmp` recovery | Vectored exception handler; `bb_setjmp`/`bb_longjmp` (`src/compat_win.c`). |
| glibc sleeps, `CLOCK_MONOTONIC` condition variables | High-resolution waitable timers; semaphore and event flag deadlines on `CLOCK_REALTIME` (winpthreads rejects monotonic condition variables). |
| `libbbgpu.so` | A static `libbbgpu.a` linked into `sacredfire.exe`, with a manifest for UTF-8 and long paths. |
| `run.sh`, GTK launcher | `run.py`; no launcher window yet (the in-game menu has the game's settings). |
| Save files replaced with `rename` | `MoveFileEx(MOVEFILE_REPLACE_EXISTING)`; all files opened in binary mode. |

Not available on Windows: the PC memory model (`BB_PC_MODEL`/`BB_GUEST_IN_PLACE`, dma-buf guest
memory), userfaultfd tracking, the watchdog/`--timeout` thread dumps, `BB_SAMPLE_THREAD`,
`BB_FREE_CHECK`, `BB_WAIT_TRACE`, `BB_COPY_PROTECT`, MangoHud, PGO, the AppImage and the
`tests/` programs. These are Linux diagnostics or Linux-only mechanisms; the game itself does not
need them.
