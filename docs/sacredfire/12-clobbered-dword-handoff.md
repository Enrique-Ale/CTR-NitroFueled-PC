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

## 2026-10-11, later session: the instrumentation (branch `issue-2`)

### A pattern in the run-7 log

In run 7 (logs/backport-smoke-7-default.log) the clobbered dword is `0x13890` bytes into the
buffer, and the packet right after it is an `EVENT_WRITE_EOS` that writes the value 1 to the label
`0x10f6113890`, followed by a `WAIT_REG_MEM` for 1 at the same label. The low 16 bits of the
offset and of the label match. If the game's buffer started at `0x10f6100000` (a 1 MiB-aligned
allocation is plausible), the label *is* the clobbered header: a CPU-side label write of 1, made
by the port on the game's behalf, landing inside a submitted command buffer, before the memcpy in
`SubmitGfx` copied it. That is the "fence or label write landing in reused memory" candidate from
the issue, and it predicts case 1 (bad already at submit). The instrumentation below tests it
directly, and still covers case 2.

All four clobbers seen so far replaced a type-3 header (`c0016900`, `c0047600`) with
`00000001`; the payload dwords after it were intact.

### What the build on `issue-2` adds (always on, cheap)

New module `gpu/shim/bbport_clobber.{h,cpp}`; hooks in liverpool.cpp, bbgpu.cpp,
vk_rasterizer.cpp, buffer_cache.cpp, texture_cache.cpp.

1. **Three readings per clobber.** `SubmitGfx` walks the copy's headers right after the memcpy
   (`SubmittedCopy::bad_at_submit`); `ProcessGraphics` walks them again when the GPU thread
   starts decoding the copy (`bad_at_decode`); `ReportBadPacket` prints both, the guest dword at
   the same offset as it is now, and the time since submit:
   - `Command buffer check: submission N's dcb already holds a non-packet dword when the game
     submits it: 00000001 at +0x... (guest 0x...)` — case 1, the guest buffer was bad at submit.
   - `Command buffer check: submission N's copy changed between submit and decode start` — case
     2, a host-side write into the copy while it waited in the queue.
   - `  issue #2: copy now 00000001; guest dword at ... now ...; first non-packet dword of the
     copy at submit: none|+0x..., at decode start: none|+0x...` under every `Bad PM4 packet`.
2. **Guest write check.** Every write the port makes into guest memory on the game's behalf
   (EOP/EOS/RELEASE_MEM labels, WRITE_DATA, DMA fill/copy, buffer and image downloads) goes into a
   ring of the last 16384 writes, and is reported immediately when it lands inside one of the last
   256 submitted command buffers (`Guest write check: EOS fence of 4 bytes, value 0x1, to 0x...
   lands inside submission N's command buffer at +0x... (<where>; decoding submission D; submitted
   T ms ago; thread ...)`). `<where>` is "the buffer being decoded" (a label the game placed inside
   its own buffer: legal on the hardware, which has parsed past it), "a buffer submitted after the
   one being decoded, not decoded yet" (the clobber scenario: the game already reused the memory),
   or "an older buffer, not resubmitted since". The two checks above also list the remembered
   writes into the clobbered dword (`  port write into it: ...`).
3. **`BB_COPY_PROTECT=1`** (opt-in): the copies are allocated in whole pages and made read-only
   after the walk at submit until they are freed. A host-side write into one faults; the fault
   handler prints `Clobber trap: a write to the read-only copy of submission N at +0x... from RIP
   ...` and lets the runtime print the writer's call chain (`Host fault (signal 11) ... #2 ...`) and
   exit. Use it when the readings say case 2.

The decoder, the draw-preparation workers (they take their own copy in `DrawPreparation::Build`)
and `CheckSubmittedCopy` only read the copy, so the read-only mode has no legitimate faulter.

### Runs with the instrumentation (2026-10-11, evening; logs/issue2-run*.log)

Reproduction command as above, `timeout -s INT 300`, unattended (the license is accepted in the
save). Build v0.2-4-g41a3e71 (PGO off, LTO on).

| run | extra | result |
|---|---|---|
| 1 | | clean 5 min; 30 FPS; 248 compiles, 0.5 s in total |
| 2 | | clean 5 min; 296 compiles, 0.5 s; 15 stalls, the longest 700 ms |
| 3 | `BB_COPY_PROTECT=1` | clean 5 min; 602 compiles; no fault, so nothing legitimate writes the copies |
| 4 | `BB_COPY_PROTECT=1`, empty `MESA_SHADER_CACHE_DIR` | clean 5 min; 693 compiles, 4.0 s (7 ms each, like the morning); 30 stalls, the longest 534 ms |
| 5 | same | clean 5 min; 248 compiles |
| 6 | `BB_COPY_PROTECT=1 BB_FREE_CHECK=1 BB_FENCE_DELAY_TEST=1` (every 200th EOP label held 80 ms) | clean 5 min; the free check saw the held fences (80 ms late) and nothing else |

The user pressed through the start screen and loaded races in some of these runs (runs 2, 3, 4 and 6
loaded Crash Cove or September Spyro), so the race-load trigger of the morning's run 7 was tried
four times without a clobber. The forced-delay runs test the gating directly: if the game could
rebuild a command buffer before its labels are written, holding labels back makes it happen; 80 ms
did not. Run 7 holds EOS labels too (`BB_EOS_DELAY_TEST`, commit 99aceb7; both switches then
took `"<ms>[,<every>]"`), run 8 holds every 300th EOP and every 2000th EOS label for 1.5 s, longer
than a plausible game-side wait timeout (the morning's clobber followed a 1633 ms stall).

What the guest write check established without a clobber:

- **The game keeps its fence labels inside the command buffers it submits.** Every EOS/EOP label
  write of value 1 (thousands a minute, `hit 149500` after five minutes) lands inside "the buffer
  being decoded": the game allocates the label from the command buffer itself (a NOP packet's
  payload, Gnm's `allocateFromCommandBuffer`). The two command buffers alternate between
  `0x10f6100000` and `0x10f6900000` (8 MiB apart), one per frame, so the game rebuilds a buffer two
  frames after submitting it. This confirms the run-7 reading: label `0x10f6113890` was the
  clobbered dword at `+0x13890` of the buffer at `0x10f6100000`.
- The labels are written by the port 20 to 33 ms after the submit (the game's frame time at 30
  FPS), up to 80 ms during stalls. The write of 1 to such a label is a write into the submitted
  command buffer, legal on the hardware (the CP has fetched past it) and harmless here while the
  game has not reused the buffer. A clobbered header is therefore a label write that arrived after
  the game rebuilt the buffer: the label's slot became a packet header in the new frame.
- In the default mode (no honest labels) the GPU thread drains the recording thread and the
  deferred fences before it signals GPU idle (`Process`, `work_retired`), and the game's next
  submission waits for that (`WaitGpuIdle`), so a label of frame F should be written before the
  game builds frame F+2 into the same buffer. Which path lets one through is what a reproduction
  with the instrumentation will show (`port write into it: EOS fence ... while decoding submission
  S`).

Why the runs stay clean now: the runs that fired on the morning of 2026-10-11 did 666 to 908
pipeline compiles costing 3.4 to 8.3 s in total (5 to 9 ms each, 119 stalls, the longest 1633 ms,
right before the clobber in run 4); the evening runs did 250 to 300 compiles at 2 ms each. The
port's cache is cleared each time (`user/cache/CUSA13795` is written and removed as expected), so
the difference is RADV's own on-disk cache (`~/.cache/mesa_shader_cache`), cold in the morning,
warm now. Run 4 points `MESA_SHADER_CACHE_DIR` at an empty directory to bring the long stalls back.
