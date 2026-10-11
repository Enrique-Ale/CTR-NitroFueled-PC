# Issue #2, the clobbered dword: hand-off for the next session (2026-10-11)

GitHub issue: https://github.com/T3CHNOLOG1C/SacredFire/issues/2 (the full symptom list, run
table and candidate writers). This note adds what the code reading at the end of the previous
session found, and the experiment it points to. Read the issue first, then this.

## The decisive observation from the code

`Liverpool::SubmitGfx` (gpu/shadps4/video_core/amdgpu/liverpool.cpp, around line 2070) copies
the submitted DCB and CCB into a host `std::vector` (`SubmittedCopy`) **synchronously on the
game's submitting thread**, and the decoder then decodes that copy, not guest memory
(`BB_COPY_GPU_BUFFERS=1`, the default). `ReportBadPacket` prints the address of the dword *in
that copy*: the "Bad PM4 packet at 0x32a17f28 ... buffer at 0x329f8190 (memory type -1, prot 0)"
lines name **host heap addresses** ("memory type -1" means not guest memory), not the game's
buffer. The same buffer address every frame (0x429155c0 in run 7) is the allocator handing the
copy the same block each frame.

So only two things are possible, and one check tells them apart:

1. The game's buffer already held the 1 when `SubmitGfx` ran `memcpy` (the writer acted before
   submission: the game itself, or one of the HLE'd Gnm writers that run on the game thread,
   or a port thread writing into guest memory the game is building commands in).
2. The host copy was modified after the `memcpy` (a host-side stray write: a dangling pointer
   into a freed-and-reused vector block, a label write resolving to a host address, a worker
   writing into the copy it was given).

## The experiment

Extend `ReportBadPacket` (liverpool.cpp ~line 975) so that, when the decoder meets a bad
header in a copy, it also reads the **guest** dword at the same offset (`SubmittedCopy` keeps
`guest_dcb`, `dcb_dwords`, `guest_ccb`; the copy is passed into `ProcessGraphics` as a
`shared_ptr`) and prints both: `copy=0x00000001 guest=0xc0016900` or `guest=0x00000001`. The
guest buffer may have been reused by the game by decode time, so also add a cheap header walk
right after the `memcpy` in `SubmitGfx` (type 3: advance by count+2; type 2: 1 dword; anything
else: print the offset and the dword, once per submission) and a second walk over the copy at
the start of `ProcessGraphics`. Three readings per clobber: at submit (guest), at decode (copy),
at decode (guest).

- Copy clean at submit, bad at decode: case 2, a host bug. Then set a hardware watchpoint:
  the copy block address repeats each frame, so `BB_PROBE="gdb -batch -ex 'watch *(int*)ADDR'
  ..."` or an `mprotect` of the copy block after `memcpy` (PROT_READ; the decoder only reads it;
  the fault handler names the writer) catches it within a few frames. The vector would need to
  be page-aligned for the mprotect trick (allocate with `aligned_alloc` or over-allocate).
- Bad already at submit: case 1. Then the suspects are the game and the HLE Gnm writers
  (gpu/shadps4/core/libraries/gnmdriver/gnmdriver.cpp, the functions that write packets into
  the game's buffers: the `sceGnm*` draw and state setters, `sceGnmInsert*`) and port threads
  writing guest memory (BbWriteLog covers fences, WriteData, DMA, backing writes; extend it to
  the HLE writers). A watchpoint on the guest address works the same way; the game reuses the
  buffer each frame, so the address repeats too.

## Reproduction

Cold cache with background compilation on fired in 2 of 3 unattended 4-minute runs:

```
BB_TIMEOUT=0 BB_FRAME_STATS=1 BB_CLEAR_SHADER_CACHE=1 BB_ASYNC_PIPELINES=1 bash run.sh
```

The game takes about 2 minutes to reach the intro videos; the clobbers came at 3 to 4 minutes.
With `BB_WRITE_LOG=1` the one run did not reproduce (timing). Logs of the earlier runs are in
`logs/backport-smoke*.log` and `logs/hang-repro*.log`.

## Environment reminders

- Builds run in the `archbuild` container (sudo works there); a GPU-library rebuild after a
  header change takes 20-40 minutes, `bash build.sh --test` also runs the C tests. Never run
  `build.sh` while the game is running (it replaces the mapped library).
- `kill -ALRM $(pgrep -x sacredfire)` dumps every thread's stack on a hang and exits.
- The clobber bug is independent of the backports: it was first seen on 2026-10-09 on the
  pre-backport tree. v0.2 shipped with background compilation opt-in because of it.
