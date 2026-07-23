# Heap allocation stress test

## Notes

- 2026-07-16: The calliope/microbit-dal ships a custom block allocator,
  `MicroBitHeapAllocator` (`yotta_modules/microbit-dal/inc/core/MicroBitHeapAllocator.h`).
  Standard `malloc`/`free`/`new`/`delete` are overridden to route through
  `microbit_alloc`/`microbit_free`. Useful entry points:
  - `device_heap_size(uint8_t idx)` — bytes free in heap `idx` (0-based).
  - `microbit_heap_print()` — dumps the block list over serial.
  - `MICROBIT_MAXIMUM_HEAPS == 3` in this fork (extra heap for Calliope mini v2).
  - The header itself warns simplistic allocators fragment under churn — that is
    exactly what this test targets.

## Decisions

- 2026-07-16: Replaced the previous DAL repro in `source/main.cpp` with a heap
  churn stress test.
  - Uses plain `malloc`/`free` (not `microbit_alloc` directly) so the test
    exercises the same path real code / ManagedTypes / fibers hit.
  - Deterministic xorshift32 PRNG instead of the DAL RNG so a failing run is
    reproducible from a fixed seed (`0x1234abcd`).
  - Fills each block with a per-block signature byte and re-verifies on free
    (`STRESS_VERIFY_FILL`) to catch corruption from allocator bookkeeping bugs
    or overlapping blocks, not just outright allocation failure.
  - Table-of-live-blocks with swap-remove keeps churn O(1) and lets us free in
    random order to drive fragmentation.
  - Reports over serial every `STRESS_REPORT_EVERY` iterations rather than every
    iteration to avoid serial being the bottleneck; `uBit.sleep(0)` yields so
    the scheduler/idle fiber still run.
  - Alternative considered: allocating until failure then freeing everything in
    a loop. Rejected — it measures max single-shot capacity, not fragmentation
    under mixed churn, which is the documented weakness.

- 2026-07-16: **Symptom on first flash — serial showed only `start` /
  `heap=2224`, then nothing.** Root cause: `MICROBIT_PANIC_HEAP_FULL` defaults
  to `1` (`MicroBitConfig.h:494`). On the first allocation the heap cannot
  satisfy, `microbit_alloc` calls `microbit_panic(MICROBIT_OOM)`
  (`MicroBitHeapAllocator.cpp:322`), which HALTS the board with a panic code on
  the LED matrix and emits no serial. The test intentionally exhausts the
  ~2.2 KB heap, so it panicked long before the first report.
  - Fix 1: added `config.json` with `microbit-dal.panic_on_heap_full = 0`
    (yotta key `YOTTA_CFG_MICROBIT_DAL_PANIC_ON_HEAP_FULL`) so OOM returns NULL
    instead of halting.
  - Fix 2: sized tunables to the real heap (MAX_LIVE 64, MAX_BYTES 128) and
    added `STRESS_HEAP_RESERVE` (stop allocating below 256 free bytes) so the
    DAL's own runtime allocations aren't starved.
  - Fix 3: report at iteration 1 as well, so a live run is visible immediately
    and a genuine crash is distinguishable from a silent halt.

- 2026-07-16: Test now runs clean after a user-side DAL fix (~490k iterations,
  fail=0, corrupt=0, peak live ~5321 bytes). Two corrections while bumping the
  chunk size:
  - **`device_heap_size(idx)` returns a segment's FIXED size**
    (`heap_end - heap_start`, `MicroBitHeapAllocator.cpp:176-182`), NOT free
    bytes. That is why the old `free=` column was a constant 2992 — it was
    heap 0's size, never the churn. Renamed the column to `cap=` and now sum it
    across all `MICROBIT_MAXIMUM_HEAPS` segments (total capacity). There is no
    exposed free-bytes API; the real ceiling signal is `fail` rising / `peak`
    plateauing. (Peak live 5321 > heap-0 size 2992 confirms allocations spill
    into a second heap segment.)
  - **Removed the `STRESS_HEAP_RESERVE` guard** — it compared against that
    constant, so it never fired (dead code). With panic disabled a NULL return
    is safe and is the point of the test, so we let malloc hit the ceiling and
    count `fail`.
  - Raised `STRESS_MAX_BYTES` 128 -> 512 per user request. With 64 live slots
    and avg ~258 B this now demands well over capacity, so expect `fail` to
    become non-zero (fragmentation/ceiling behaviour) — that is the intended
    stress, not a regression. Corrupt must stay 0.

- 2026-07-16: Run with the user's new DAL (`microbit` wrapper commit 161c87d,
  "prepare extra heap...") on 32 KB hardware: `cap=20400`, `peak=16339`,
  `fail` ~20k and climbing, `corrupt=0` over 300k+ iterations. Analysis:
  - The 32 KB extra-RAM logic lives in `yotta_modules/microbit/source/`
    `MicroBit.cpp:199-204` (NOT in microbit-dal core): gated at runtime on
    `microbit_ram_size()` (`NRF_FICR->NUMRAMBLOCK * SIZERAMBLOCKS`,
    `MicroBitDevice.cpp:85-88`), it first powers RAM blocks 2-3 via
    `NRF_POWER->RAMONB` (GCC startup only sets RAMON — unpowered upper RAM
    would surface as silent corruption), then registers
    `0x20004000..0x20008000` (16384 B) as a third heap. 16 KB chips report
    16384 from FICR, so they skip both the RAMONB write and the extra heap.
  - `cap=20400` decomposes exactly: heap0 (post-linker) 2992 + heap1 (SD GATT
    reuse, BLE enabled: 0x20001C00..0x20002000) 1024 + heap2 (extra) 16384.
    `peak=16339 > 4016` proves allocations live in the upper 16 KB, and
    `corrupt=0` proves that RAM is powered and retains data — RAMONB fix works.
  - No crash on full RAM is by design: config.json disables
    `panic_on_heap_full`, so OOM returns NULL and increments `fail`. Delete
    config.json + rebuild to test the MICROBIT_OOM panic path instead.
  - 16 KB-mini behaviour confirmed by code review only (runtime FICR gate);
    empirical confirmation needs a run on a 16 KB board — expect boot line
    `cap≈4016` (with BLE) and no corruption.
  - Serial lines in console.out are garbled (dropped chars, e.g. `eak=`,
    `fres=`) — TX buffer overrun under load, cosmetic only.

- 2026-07-21: Added a compile flag to gate the 32KB extra-heap reclaim block,
  as an escape hatch/A-B toggle:
  - `MICROBIT_KEEP_V1_HEAP_ON_32KB` (default `0`) defined in
    `yotta_modules/microbit-dal/inc/core/MicroBitConfig.h` next to
    `MICROBIT_HEAP_REUSE_SD`, following this codebase's existing convention
    (`#ifndef`/`#define` default + `CONFIG_ENABLED()` at the call site, same
    pattern as `MICROBIT_BLE_FORCE_ENABLE_16KB`).
  - Mapped in `yotta_modules/microbit-dal/inc/platform/yotta_cfg_mappings.h`
    as `YOTTA_CFG_MICROBIT_DAL_KEEP_V1_HEAP_ON_32KB` so it can be toggled via
    this project's `config.json` (`{"microbit-dal": {"keep_v1_heap_on_32kb": 1}}`)
    without editing vendored source.
  - Guard added in `yotta_modules/microbit/source/MicroBit.cpp` around the
    `microbit_ram_size() > 16*1024` block (RAMONB power-on + third heap
    registration): `#if !CONFIG_ENABLED(MICROBIT_KEEP_V1_HEAP_ON_32KB) ... #endif`.
    Set to `1` to force even 32KB devices to behave like 16KB devices (skip
    powering RAM blocks 2-3 and skip the extra heap) - useful for comparing
    behaviour with/without the reclaim fix, or as a rollback if the RAMONB
    change ever misbehaves on some board revision.
  - Default (`0`) preserves current verified behaviour (`cap=20400`,
    `corrupt=0` on the 32KB test run above) - this flag is opt-in only.
