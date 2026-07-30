# Runtime RAM/flash detection, and gating BLE on 16KB devices

## Notes

- 2026-07-15: nRF51's FICR (Factory Information Configuration Registers) exposes chip
  capacity directly at runtime, no need to infer from build target:
  - RAM: `NRF_FICR->NUMRAMBLOCK * NRF_FICR->SIZERAMBLOCKS` (16384 on v1, 32768 on v2).
  - Flash: `NRF_FICR->CODESIZE * NRF_FICR->CODEPAGESIZE`.
  - `NRF_FICR` is a plain global (`nrf51.h:1248`), usable from any file that includes
    `mbed.h`/`MicroBit.h` - no DAL plumbing required.
  - `NRF_FICR->CONFIGID` has a `HWID` field that Nordic uses to identify the exact part
    variant (QFAA/QFAB/QFAC/etc.), but the vendored SDK headers here don't ship a lookup
    table for it, so it isn't used.
- `microbit_heap_size(heap_index)` (`MicroBitHeapAllocator.h`) reports the *configured*
  size of a DAL heap segment, not physical RAM and not live free/used bytes. Live
  free/used requires `MICROBIT_DBG=1` + `MICROBIT_HEAP_DBG=1`, but the DAL's own comment
  notes `MICROBIT_DBG=1` disables `uBit.serial` entirely (reroutes debug output to a
  separate USB serial channel) - not worth it for this sample since `main.cpp` uses
  `uBit.serial` elsewhere (`testAnalogPins()`).
- Build oddity: `yotta_modules/` contains two copies of the DAL - `microbit-dal/` and
  `microbit-dal_ubit/` (the latter has its own `.git`, nested under the `microbit`
  module's dependency resolution). Confirmed via `build.ninja` that only
  `yotta_modules/microbit-dal/` is actually compiled; `microbit-dal_ubit/` is a stale/
  unused leftover from an earlier yotta dependency resolution. **Edit `microbit-dal/`,
  not `microbit-dal_ubit/`.**
- Actual BLE bring-up (`bleManager.init()`) happens in `yotta_modules/microbit/source/
  MicroBit.cpp:MicroBit::init()`, gated by `#if CONFIG_ENABLED(MICROBIT_BLE_ENABLED)`
  (compile-time). Pairing/DFU mode (`MICROBIT_BLE_PAIRING_MODE` block, higher up in the
  same function) already starts BLE unconditionally regardless of `MICROBIT_BLE_ENABLED`
  by original design (comment in `MicroBitConfig.h`: OTA programming must stay available
  regardless of the BLE-enabled setting) - left untouched.
- Yotta config → DAL macro mapping lives in `yotta_modules/microbit-dal/inc/platform/
  yotta_cfg_mappings.h` (hand-written, not generated) - e.g. yotta config path
  `microbit-dal.bluetooth.enabled` becomes `YOTTA_CFG_MICROBIT_DAL_BLUETOOTH_ENABLED`,
  which this file maps to the DAL's own `MICROBIT_BLE_ENABLED`. New yotta config knobs
  need a matching entry here.
- Building: activate `/home/hugo/fw/YOTTAENV` (venv with yotta 0.20.5), then `yt build`.
- The project's single yotta target (`bbc-microbit-classic-gcc`) hardcodes a 16KB memory
  map regardless of which chip actually runs the hex:
  - `yotta_targets/bbc-microbit-classic-gcc/ld/NRF51822.ld`: `RAM ORIGIN=0x20002000
    LENGTH=0x2000`, i.e. ends at `0x20004000` (16KB boundary). Bottom `0x20000000-
    0x20002000` reserved for the S110 SoftDevice.
  - `toolchain.cmake` unconditionally defines `-DTARGET_MCU_NRF51_16K_S110`.
  - `MicroBitConfig.h` defaults `MICROBIT_SRAM_END=0x20004000`,
    `MICROBIT_SD_LIMIT=0x20002000` to match (falls to the non-S130 `#else` branch).
  - Net effect: on a 32KB (v2) chip, physical RAM `0x20004000-0x20008000` exists on
    silicon but was never mapped by the linker or registered with the DAL heap
    allocator - dead space, until the change below.
- DAL heap allocator (`MicroBitHeapAllocator.h`/`.cpp`) supports multiple independent
  heap regions (round-robins across them in creation order once one fills). Before this
  session's change, exactly 2 were ever created in `MicroBit::init()`:
  1. `[__end__, MICROBIT_HEAP_END]` - lazily on first `malloc()`, the "normal" heap above
     the program's static `.data`/`.bss`.
  2. `[MICROBIT_SD_GATT_TABLE_START+SIZE, MICROBIT_SD_LIMIT]` (BLE enabled) or
     `[MICROBIT_SRAM_BASE, MICROBIT_SD_LIMIT]` (BLE disabled) - reclaimed SoftDevice RAM.
  `MICROBIT_MAXIMUM_HEAPS` was capped at 2, matching exactly these two.
- 2026-07-15 (follow-up, corrects the `microbit_heap_size()` note above): that note
  assumed the function worked as documented. It didn't - `MicroBitHeapAllocator.h`
  declared `uint32_t microbit_heap_size(uint8_t)`, but `MicroBitHeapAllocator.cpp`
  defined a same-logic function under the wrong name, `device_heap_size()`. Nothing else
  in the tree called `device_heap_size`, so this was silent dead code - any real call to
  `microbit_heap_size()` would link-fail. Found by actually calling it (in the heap
  stress test below) rather than just reading the header. Fixed by renaming the
  definition to match the header; see Decisions.

## Decisions

- 2026-07-15: Added `microbit_ram_size()` to `MicroBitDevice.h`/`.cpp` (returns bytes,
  via FICR) as a reusable DAL primitive, rather than inlining the FICR read at each call
  site. Used both from `source/main.cpp` (diagnostic serial output) and from the new
  BLE-gating check in `MicroBit.cpp`.
- 2026-07-15: Added a new compile flag `force_enabled_dal_16kb` under
  `yotta.config.microbit-dal.bluetooth` (mirroring pxt-microbit's existing bluetooth
  config block), mapped to internal macro `MICROBIT_BLE_FORCE_ENABLE_16KB` (default 0).
  When the compiled `microbit-dal.bluetooth.enabled: 1` is combined with a 16KB-RAM chip
  detected at runtime, BLE is now skipped in `MicroBit::init()` unless this flag is set.
  Rationale: a single hex compiled with BLE enabled may run on both 16KB (v1) and 32KB
  (v2) Calliope minis; running the full BLE stack alongside user code on 16KB is tight
  and prone to out-of-memory failures, so the safe default is to no-op BLE there unless
  a user explicitly opts back in.
  - Alternative considered and rejected: gating pairing/DFU mode too. Left alone since
    that path already bypasses `MICROBIT_BLE_ENABLED` by design, for OTA-recovery
    availability - gating it further wasn't asked for and would risk bricking recovery
    on 16KB boards.
  - Verified with `yt build` (bbc-microbit-classic-gcc target) - compiles and links
    cleanly, only pre-existing unrelated warnings from `ble`/`BLE_API`.
- 2026-07-15: Reclaimed the extra 16KB on 32KB (v2) chips as a third heap region.
  Bumped `MICROBIT_MAXIMUM_HEAPS` from 2 to 3 (`MicroBitHeapAllocator.h`), and added a
  runtime-gated `microbit_create_heap(MICROBIT_SRAM_END, MICROBIT_SRAM_END + 16*1024)`
  call in `MicroBit::init()` (`MicroBit.cpp`), guarded by `microbit_ram_size() >
  16*1024`. Left the linker script and `MICROBIT_SRAM_END`/`MICROBIT_STACK_SIZE`
  constants untouched (16KB baseline stays the safe default for `.data`/`.bss`/stack
  placement on both chip variants) - only the extra heap region is conditional, since
  it's purely additional dynamic-allocation space that's physically absent on 16KB
  chips and would otherwise fault if touched there. `ManagedString`/fiber/BLE
  allocations transparently spill into it once heaps 0-1 fill, no call-site changes
  needed elsewhere.
  - Verified with `yt build` - compiles and links cleanly.
- 2026-07-15: Added `testHeapStress()` to `source/main.cpp`, called once per main-loop
  iteration (not its own loop): grows via fixed 256-byte `malloc()` calls until one
  fails, frees them all back down, repeats, printing progress each step. Also fixed the
  `microbit_heap_size()`/`device_heap_size()` name mismatch noted above (renamed the
  `.cpp` definition to match the header) - discovered because this test was the first
  real caller of `microbit_heap_size()` in the tree, and it link-failed.
  - First attempt used a 4096-entry `static void* blocks[]` array (for a 16KB/256B
    upper bound) - overflowed `.bss` by ~10.5KB, since the pointer array itself (16KB)
    was competing for the same tiny RAM region it was meant to test. Fixed by sizing it
    to the realistic bound instead (80 entries = 16KB / 256B, the largest single heap
    region i.e. the new v2-only heap 2), not a theoretical global maximum.
  - `microbit_heap_size(i)` still only reports each heap's static configured capacity,
    not live free/used bytes (per the corrected note above) - the test tracks its own
    allocation count as the "live usage" signal, and treats a real `malloc()` failure as
    the actual OOM signal, rather than trying to derive usage from that function.
  - Verified with `yt build` - compiles and links cleanly, `.bss` at 2516/8192 bytes.

- 2026-07-15 (real hardware bug, found via on-device testing, not build/lint): flashed
  to an actual v2 board, `testHeapStress()` printed exactly 2 `GROW` lines (heap
  capacities read back correctly: h0=3440 h1=1024 h2=16384, confirming 32KB detection
  and all 3 heaps registered), then the board hung/reset. Root cause: **the new heap-2
  region (RAM blocks 2-3) was never power-enabled.** On nRF51822, RAM is split into
  power domains - blocks 0-1 via `POWER->RAMON`, blocks 2-3 (only present on 32KB parts,
  exactly the extra region this session added) via a *separate* register,
  `POWER->RAMONB`. Confirmed by comparing startup files:
  - This project's actual GCC startup code
    (`mbed-classic/.../TOOLCHAIN_GCC_ARM/startup_NRF51822.S`) only sets `RAMON` - no
    32K-specific variant exists for GCC in this tree.
  - The ARMCC 32K startup variant that DOES exist here
    (`TOOLCHAIN_ARM_STD/TARGET_MCU_NORDIC_32K/startup_nRF51822.S`) explicitly ORs the
    on-mode bits into *both* `RAMON` and `RAMONB` at reset - proving Nordic's own
    reference startup code treats this as mandatory for 32K parts, and confirming the
    GCC path here was simply never given the equivalent for this codebase's
    (unofficial, session-added) 32K support.
  - Un-powered RAM doesn't necessarily hard-fault on read/write on this part - it's
    undefined behaviour, which is consistent with a delayed crash a couple of
    allocations later rather than an immediate, obvious fault at boot.
  - Fix: in `MicroBit.cpp`, before creating heap 2, explicitly
    `NRF_POWER->RAMONB |= (ONRAM2 | ONRAM3 on-mode bits)`. Confirmed the bitfield macros
    (`POWER_RAMONB_ONRAM2_*`, `POWER_RAMONB_ONRAM3_*`) exist identically in both
    `nrf51-sdk` and `mbed-classic`'s vendored `nrf51_bitfields.h` copies, so no include
    issues regardless of which one resolves first.
  - Not yet re-verified on physical hardware after this fix (only rebuilt) - re-flash
    and re-run `testHeapStress()` before trusting heap 2 further.
  - Separately noticed while tracing allocation order (not yet fixed, latent/dormant
    only): `microbit_alloc()` in `MicroBitHeapAllocator.cpp` does `heap_count = 0;` on
    the first-ever `malloc()` call before creating its own lazy heap, which would
    silently discard any heaps already registered via explicit `microbit_create_heap()`
    calls if that first `malloc()` happened to fire before `MicroBit::init()`'s heap
    setup. It didn't fire here only because something upstream (likely
    `messageBus.listen()` in `MicroBit::init()`, called before the heap-reuse/heap-2
    code) already triggers the lazy heap first. Worth a defensive fix (skip the reset if
    `heap_count > 0`) if heap registration order in `MicroBit::init()` ever changes.

- 2026-07-15 (follow-up, corrects the RAMONB entry above): re-flashed with the RAMONB
  fix, byte-for-byte identical crash - still exactly 2 `GROW` lines, same capacities.
  RAMONB was never the cause of *this* crash: heap0 alone is 3440 bytes, ~13 of these
  260-byte (256 + 4-byte header) allocations before ever spilling into heap1, let alone
  heap2 - the failure at allocation #2-3 can't have involved heap2 at all. (The RAMONB
  fix is still correct and worth keeping for whenever heap2 *does* get exercised, just
  not what was biting here.) Isolated further by temporarily commenting out the
  accelerometer/display code that ran after `testHeapStress()` in the main loop - same
  crash, same point - ruling out the accelerometer's lazy I2C init too.
  - Verified `malloc`/`free` do resolve to the DAL's `microbit_alloc`/`microbit_free`
    (checked via `arm-none-eabi-nm` on the built ELF - `malloc` and `microbit_alloc`
    share the same address, confirming the weak alias in
    `MicroBitHeapAllocator.cpp:398` took effect over newlib's), ruling out "wrong
    allocator" as well.
  - **Actual root cause: an out-of-bounds read in `microbit_malloc()`'s free-block
    merge loop**, pre-existing in the upstream DAL (not introduced this session).
    `MicroBitHeapAllocator.cpp` (was ~line 225): `next = block + blockSize; while (*next
    & MICROBIT_HEAP_BLOCK_FREE) { if (next >= heap.heap_end) break; ... }` -
    dereferences `*next` *before* checking whether `next` is in bounds. `next` lands
    exactly on `heap.heap_end` (one past the last valid word) whenever the current free
    block extends all the way to the end of its heap - which is the common case: it's
    true for the *entire heap* immediately after `microbit_create_heap()`, and recurs
    after every full free cycle. For heap0 specifically, the word immediately after
    `MICROBIT_HEAP_END` is the first word of the **stack** (`MICROBIT_HEAP_END =
    CORTEX_M0_STACK_BASE - MICROBIT_STACK_SIZE`), so this reads live stack contents. If
    that garbage word happens to have its top bit (`MICROBIT_HEAP_BLOCK_FREE =
    0x80000000`) set, the loop misinterprets stack memory as a free block, merges its
    (garbage) size into the heap's real free-block header, and re-derives `next`
    further into the stack - corrupting heap bookkeeping with whatever happened to be on
    the stack at that moment. This explains every observed symptom: happens on
    essentially any allocation against a heap whose free space reaches its end (not
    something that "gets worse with more blocks"); manifests unpredictably (2 calls one
    run, could be a different count another run) because it depends on transient stack
    contents at the time, not on our code; and was completely unaffected by the RAMONB
    fix and by removing the accelerometer code, since neither has anything to do with
    it. Never surfaced before because nothing in this tree previously exercised
    repeated alloc/free cycles against a heap that had just been fully reclaimed to one
    large block - which is exactly what a stress test does.
  - Fix: reordered to bounds-check before dereferencing -
    `while (next < heap.heap_end && (*next & MICROBIT_HEAP_BLOCK_FREE))`, dropping the
    now-redundant inner `if`/`break`. Verified with `yt build` - compiles and links
    cleanly (one pre-existing, unrelated `-Wmisleading-indentation` warning in
    `microbit_free()`, in code this change didn't touch).
  - Not yet re-verified on physical hardware - needs reflash + rerun of
    `testHeapStress()` to confirm the hang is actually gone, not just theoretically
    explained.

- 2026-07-15 (on-device verification, closes this thread): reflashed both boards with
  the merge-loop fix; full stress logs in `heapstresslog.txt`.
  - Calliope mini v1 (16KB): `h2=0 total=4464` - heap 2 correctly absent.
  - Calliope mini v2 (32KB): `h0=3440 h1=1024 h2=16384 total=20848`, grew to 65 held
    blocks (~16.9KB) - far beyond the 4.4KB possible without heap 2, so **heap 2 is
    confirmed working on real hardware**. The earlier merge-loop crash at 2 blocks is
    gone.
  - The v2 run "crashed" at 65 blocks - but with sad-face + error **020** on the LED
    matrix: that's `MICROBIT_OOM` (20), i.e. the DAL's *designed* out-of-memory
    behaviour, not a bug. `MICROBIT_PANIC_HEAP_FULL` defaults to 1
    (`MicroBitConfig.h`), so `microbit_alloc()` panics on exhaustion instead of
    returning NULL - the graceful NULL/OUT-OF-MEMORY branch in `testHeapStress()` is
    unreachable dead code under default config. 65 blocks (vs naive 79-block capacity)
    is consistent with ~10 blocks of heap0 runtime overhead (listeners, fibers, serial
    buffers, transient printf allocations).
  - Decision: keep as is (user's call) - the panic at exhaustion is itself the
    measurement, and default OOM behaviour stays untouched for downstream users.
    Alternatives considered: app-scoped `panic_heap_full: 0` yotta config (would need a
    new `yotta_cfg_mappings.h` entry), or a test-side safety cap; both rejected as
    unnecessary.
  - Minor open oddity, not chased: the host-side serial port (`/dev/ttyACM0`) dropped
    when the panic hit, even though the port is hosted by the DAPLink interface chip,
    which a target panic shouldn't affect. If it recurs outside panic scenarios, check
    `dmesg` for kernel-level USB disconnects vs. the serial monitor just giving up.

- 2026-07-23: Renamed the v1-simulation compile flag from `MICROBIT_KEEP_V1_HEAP_ON_32KB`
  to `MICROBIT_SIMULATE_MINI1_ON_MINI2` (yotta config key `microbit-dal.
  keep_v1_heap_on_32kb` -> `microbit-dal.simulate_mini1_on_mini2`, mapped in
  `yotta_cfg_mappings.h`). The new flag now controls TWO things when set to 1, decoupled
  from each other in `MicroBit::init()` (`MicroBit.cpp`):
  1. skips the v2 extra-16KB heap reclaim block (RAMONB power-up + third
     `microbit_create_heap`) - guarded by `#if !CONFIG_ENABLED(...)` around ONLY that block;
  2. drops the runtime `microbit_ram_size() > 16*1024` gate on BLE bring-up (a nested
     `#if !CONFIG_ENABLED(...)` around just the gate *condition*), so BLE is NOT
     deactivated by the simulated 16KB size - it starts unconditionally (still subject to
     `MICROBIT_BLE_ENABLED`). This is deliberate: simulate mode runs on physical v2
     silicon (32KB), which can host BLE fine; we only want the *heap* to look like v1.
  - Two bugs caught during review of the initial rename and since fixed:
    (a) name mismatch - the mapping defined `MICROBIT_SIMULATE_MINI1_ON_MINI2` but
    `MicroBit.cpp` tested `MICROBIT_DAL_SIMULATE_MINI1_ON_MINI2` (stray `DAL_`); since
    `CONFIG_ENABLED(X)` is `(X == 1)` and an undefined macro reads as 0 in `#if`, the flag
    was a silent no-op. (b) the `#endif` had been placed after `init()`'s closing `}`, so
    enabling the flag would have deleted the function's brace (build break) AND compiled
    out the whole BLE block (deactivating BLE - opposite of intent). Fixed by matching the
    name and moving the guard to wrap only the heap block / only the BLE gate condition.
  - Default now lives at `MicroBitConfig.h` as `#ifndef MICROBIT_SIMULATE_MINI1_ON_MINI2
    / #define ... 0` (was the old `KEEP_V1` default). Works because `yotta_cfg_mappings.h`
    is included at `MicroBitConfig.h:34`, before the `#ifndef` at ~141, so a set config
    value wins and an unset one falls back to 0. No references to the old
    `KEEP_V1_HEAP_ON_32KB` name remain anywhere in the tree.
  - NOT yet rebuilt/re-flashed after this rename - run `yt build` (YOTTAENV venv) with
    `simulate_mini1_on_mini2` both 0 and 1 to confirm both configs compile before trusting.

- 2026-07-23: Made the Soft Device RAM reclaim RUNTIME-aware so a BLE-gated-off boot
  actually gets that RAM back as heap (goal: maximize free heap at runtime, not just
  simulate v1's ceiling). Previously the reclaim in `MicroBit::init()` (`MicroBit.cpp`)
  was compile-time gated on `MICROBIT_BLE_ENABLED`: BLE-in reclaimed only the ~1KB slack
  above the GATT table (`[SD_GATT_TABLE_START+SIZE, SD_LIMIT)`), BLE-out reclaimed the
  whole ~8KB region (`[SRAM_BASE, SD_LIMIT)`). So a build with `bluetooth.enabled:1` that
  then skipped BLE at runtime (simulate v1 / 16KB gate) left ~7KB of SoftDevice-reserved
  RAM unused (S110 numbers: 8184 vs 1024 bytes, delta 7160).
  - Fix: compute one `bool bleWillRun` before the heap-reuse section =
    `(ble != NULL) || ((ram>16KB && !SIMULATE_MINI1_ON_MINI2) || BLE_FORCE_ENABLE_16KB)`,
    and use it for BOTH the reclaim bounds (`bleWillRun` -> 1KB, else -> 8KB) and the BLE
    start gate (`if (bleWillRun && !ble)`). No new heap region / no `MAXIMUM_HEAPS` bump -
    it only changes the bounds of the existing heap-1 `create_heap` call.
  - Why it's safe re: SoftDevice. The SoftDevice only claims its RAM while enabled
    (`ble->init()` inside `bleManager.init()`); when never enabled it never touches that
    region, so it's free heap. The one exception is pairing/DFU mode, which DOES bring up
    the SoftDevice - but that block runs EARLIER in `init()` (before the reclaim) and sets
    `ble`, and per the user it is only ever entered via A+B+reset at startup (no runtime
    re-entry without a reset, and reset re-runs init() top-down). So `ble != NULL` at the
    reclaim point reliably means "SoftDevice is up this boot" -> take the 1KB branch. The
    only unguardable hazard is user code calling `bleManager.init()` manually mid-session
    on a gated-off boot; documented in the code comment as the contract.
  - Side effect (intended, beneficial): a REAL mini v1 (16KB) with BLE compiled in but
    gated off now also reclaims the full ~8KB instead of 1KB - same safety argument. Not
    just the simulation path.
  - `BLE_FORCE_ENABLE_16KB` handled: it makes `bleWillRun` true, so a forced-on 16KB/v1
    build correctly reserves the SD region (1KB reclaim) AND starts BLE.
  - Case matrix verified by hand (start BLE / reclaim size): v1 no-force = off/8KB;
    v2 normal = on/1KB; v2 simulate = off/8KB; any force = on/1KB; pairing (ble!=NULL) =
    already-on/1KB. All consistent.
  - NOT yet built or flashed. MUST verify on real hardware before trusting: this hands the
    SoftDevice's low-RAM region to the allocator, so a heap-stress run (see heap notes)
    should now show a much larger heap-1, and a separate boot into pairing mode (A+B+reset)
    must still pair/DFU cleanly (confirms the 1KB branch is taken and SD RAM intact there).

- 2026-07-23 (corrects the 2026-07-15 name-mismatch note above): the correct/kept symbol
  is `device_heap_size(uint8_t heap_index)`, NOT `microbit_heap_size`. It is declared in
  `MicroBitHeapAllocator.h` and defined in `.cpp` under that name, and (per the user) is
  the name codal uses too - so it is intentional, not a stale mismatch. The earlier note's
  "fix" (renaming the definition to `microbit_heap_size`) is superseded: use
  `device_heap_size`. It returns each region's CONFIGURED capacity (`heap_end -
  heap_start`), 0 for indices >= heap_count, not live free bytes.
- 2026-07-23: Added heap/fiber diagnostics to `source/main.cpp` (re-adding capability the
  earlier heap-stress work had left only as a commented `// testHeapStress();`):
  - `int countOpenFibers()` - walks `get_fiber_list()` via `->next` with IRQs masked
    (saves/restores PRIMASK, not blind enable), capped at 256 as a corrupt-list guard.
  - `uint32_t largestFreeBlock()` - binary-searches `malloc()` sizes (0..64KB) and frees
    immediately; reports the largest *contiguous* allocatable block (best heap), NOT total
    free. Chosen because `device_heap_size()` is capacity-only and there is no live
    total-free accessor without `MICROBIT_DBG` (which disables `uBit.serial`).
  - `void readHeap()` - one serial line: `HEAP ram=.. h0=.. h1=.. h2=.. largestFree=..
    fibers=..`. Per-heap values are configured capacities, so h1 is the direct readout for
    verifying the Soft Device reclaim (expect ~8KB on a BLE-gated-off boot, ~1KB when BLE
    runs). Wired into the main loop at ~1Hz (diagTick % 10, loop already sleeps 100ms).
  - Not yet built/flashed.

- 2026-07-30: Renamed the yotta config key from `microbit-dal.bluetooth.force_enabled_dal_16kb`
  to `microbit-dal.bluetooth.force_enabled_16kb` (supersedes the 2026-07-15 entry above, which
  introduced the `_dal_` spelling). This completes the earlier partial rename: DAL commit
  `f27b5af` ("uniformly name force enable 16kb flag") had already renamed the C macro
  `MICROBIT_BLE_FORCE_ENABLE_16KB` -> `MICROBIT_BLE_FORCE_ENABLED_16KB` but left the
  *config key* — and therefore the
  `YOTTA_CFG_...` symbol — with the redundant `DAL_` infix.
  - `_dal_` was redundant twice over: the config block is already `microbit-dal.*`, and yotta
    prefixes the generated symbol with `YOTTA_CFG_MICROBIT_DAL_` regardless.
  - Changed: `config.json` (already renamed in the working tree by the user);
    `yotta_cfg_mappings.h` `#ifdef`/`#define` (`YOTTA_CFG_MICROBIT_DAL_BLUETOOTH_FORCE_ENABLED_DAL_16KB`
    -> `..._FORCE_ENABLED_16KB`); a stale `MICROBIT_BLE_FORCE_ENABLE_16KB` mention in a
    `MicroBit.cpp` comment. `MicroBitConfig.h` needed no change (it only knows the macro name).
  - Verified the yotta key -> symbol transformation independently of a rebuild:
    `build/bbc-microbit-classic-gcc/yotta_config.h` already emits
    `#define YOTTA_CFG_MICROBIT_DAL_BLUETOOTH_FORCE_ENABLED_16KB 0` from the renamed
    `config.json`, which is exactly the symbol the new mapping tests.
  - Failure mode to watch for if a consumer is missed: a stale `force_enabled_dal_16kb` key is
    NOT an error — yotta happily emits the old symbol, nothing `#ifdef`s it, and
    `MICROBIT_BLE_FORCE_ENABLED_16KB` silently falls back to its `MicroBitConfig.h` default of
    0. I.e. the flag becomes a silent no-op (BLE stays gated off on 16KB), not a build break.
    Same class of silent-no-op bug as the 2026-07-23 `SIMULATE_MINI1_ON_MINI2` name mismatch.
  - pxt-microbit side handled too (`libs/bluetooth/pxt.json`); see
    `pxt-microbit/docs/claude-cloud-compile-tag-mismatch.md` for the tag-pinning caveat — the
    DAL change must be committed *and tagged* in `calliope-mini/microbit-dal` before cloud
    builds see it.
  - NOT rebuilt/reflashed by this session (user is running `yt build` themselves).

- 2026-07-23 (on-device, corrects the largestFreeBlock() entry above): flashed with
  simulate_mini1_on_mini2=1. Reclaim confirmed working - serial showed
  `HEAP ram=32768 h0=3760 h1=8184 h2=0` (h1 = full ~8KB Soft Device reclaim, h2=0 as
  expected in simulate mode). BUT the board then panicked with error 020 (MICROBIT_OOM)
  mid-line, right after `h2=0` and before `largestFree=`/`fibers=` printed.
  - Root cause: `largestFreeBlock()`'s malloc binary-search. `MICROBIT_PANIC_HEAP_FULL`
    defaults to 1 (`MicroBitConfig.h:505-506`), so `microbit_malloc` calls
    `microbit_panic(MICROBIT_OOM)` on ANY failed allocation
    (`MicroBitHeapAllocator.cpp:318-320`) - it does not just return NULL. The probe's
    whole method is to malloc sizes that fail (starts at 32KB vs a ~12KB heap), so the
    first oversized malloc panicked. The args-before-call evaluation order is why the
    line cut off exactly at `h2=0` (the probe ran as a printf argument).
  - Fix (main.cpp only, DAL untouched): replaced the probe with `heapFreeBytes()`, which
    walks each heap's block metadata directly (`extern HeapDefinition heap[]` + the
    MICROBIT_HEAP_BLOCK_FREE header scheme, mirroring microbit_heap_print) and only READS
    - no allocation, so it can't trip the panic. Reports true total free (matches the
    DAL's mb_total_free). readHeap() now prints `free=` instead of `largestFree=`.
  - Lesson: on this build you cannot probe free memory with malloc - a failing malloc is
    fatal (panic 020), not a NULL return. Any heap introspection must be read-only.
  - Still to watch: the crash pre-empted the fiber-count print, so whether there is also a
    genuine fiber-explosion OOM (see claude-makecode-port.md) from the gesture/button
    handlers is not yet ruled out - re-observe now that readHeap() is non-fatal.
