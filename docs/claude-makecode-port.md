# MakeCode -> DAL C++ port (source/main.cpp)

## Notes

- 2026-07-21: Ported a MakeCode script (7 event handlers: P0-P3 touch,
  A/B/AB buttons; each shows a character then `serial.writeValue("x", ...)`)
  to plain C++ against the DAL directly, no pxt runtime - same goal as the
  original main.cpp: tell apart calliope-mini/microbit-dal bugs from
  MakeCode/pxt C++ runtime (pxt-microbit) bugs.
- Verified every mapping against the actual pxt-microbit fork checked out at
  `/home/lop/fw/makecode/pxt-microbit` (a Calliope fork, commit
  "create heap and ram read functions for testing" - the same fork that
  added `deviceHeapSize`/`physicalRamSize`/GC-based `availableMemory` to
  `control.ts`), not guessed from memory:
  - `input.onPinTouchEvent` -> `pin->isTouched()` then
    `uBit.messageBus.listen(pinId, event, ...)` (`libs/core/input.cpp:238-245`)
    - exactly the pattern already used by hand in the original main.cpp.
  - `input.onButtonEvent` -> plain `messageBus.listen(buttonId, event, ...)`
    (`input.cpp:175-177`). `Button.AB` = `MICROBIT_ID_BUTTON_AB` (3), a real
    DAL id (`MicroBitComponent.h:34`) for the combined-press button.
  - **`basic.showString(s, interval=150)` is not always a scroll**
    (`libs/core/basic.cpp:39-51`): length-1 strings take
    `uBit.display.printChar(ch, interval*5)` - a static, *blocking* hold, not
    an animation. Only length>1 (here, only `"AB"`) uses
    `uBit.display.scroll(s, interval)`. Missed this initially by assuming
    everything scrolls; checking the real source caught it.
  - **`serial.writeValue(name, value)` wire format**: `"name:value"` (colon,
    no space - `libs/core/serial.ts:107-108`), then `writeLine()` pads the
    line with spaces out to a 32-byte boundary (`text.length + 2` for CRLF,
    mod 32) before appending `"\r\n"` (`serial.ts:37-58`,
    `writeLinePadding` defaults to 32). A naive `printf("x:%d\r\n", v)` would
    NOT match the real byte stream on the wire - reproduced exactly in
    `writeSerialValue()`.
  - **`control.availableMemory()` has no DAL equivalent.** Per this fork's
    own `control.ts:166-179`, it forces a GC pass and returns free bytes in
    pxt's *own* managed/GC heap (`gc.cpp` - a mark-sweep allocator running in
    16 KB blocks obtained via `xmalloc`, layered on top of, but distinct
    from, the DAL block allocator). A plain DAL program has no such heap.
    `availableMemory()` in the port returns `device_heap_size(0)` instead -
    a real number, but not a comparable one. Flagged prominently in the file
    header so nobody mistakes DAL heap size for the actual MakeCode metric
    when comparing serial logs side by side.
  - `registerWithDal()` also does `incr()`/`registerGCPtr()` (closure GC
    bookkeeping) and an idempotent `messageBus.ignore()` before `listen()`
    (`libs/core/codal.cpp:95-99`). Both omitted: no closures are captured
    here (each handler is a plain function, no external state), and each
    event is registered exactly once, so `ignore()`-before-`listen()` has no
    observable effect.

## OOM / fiber-explosion diagnosis (2026-07-22)

- Repro: on a simulated 16KB Calliope mini v1 (`config.json`
  `"keep_v1_heap_on_32kb": 1` + `"panic_on_heap_full": 0`), displaying a
  string while many pin/button events arrive freezes the board. Serial just
  repeats the last `x:<heap0>` (e.g. `x:3792`, padded) and then stops.
- **Root cause = fiber/heap explosion, not fiber-pool sizing:**
  - Listeners registered via the 3-arg `listen()` get
    `EVENT_LISTENER_DEFAULT_FLAGS = MESSAGE_BUS_LISTENER_QUEUE_IF_BUSY`
    (`MicroBitConfig.h:190`); concurrency mode is `CONCURRENT_LISTENERS`
    (`:214`). Dispatch is fork-on-block (`MicroBitMessageBus.cpp:391`,
    `invoke(async_callback, l)`).
  - `basic.showString` blocks the handler fiber (`printChar`/`scroll` ->
    `fiber_sleep`). While blocked, `schedule()` FORKS a new fiber to hold the
    context (`MicroBitFiber.cpp:880-903`); each fiber's stack is malloc'd from
    the heap (`getFiberContext`/`verify_stack_size`, `:162-185`).
  - 7 listeners => up to 7 concurrently-blocked handler fibers, plus up to
    `MESSAGE_BUS_LISTENER_MAX_QUEUE_DEPTH` (=10, `:198`) queued
    `MicroBitEventQueueItem` heap allocs per listener. On the ~2-4KB v1 heap
    this OOMs; with `panic_on_heap_full=0`, `new Fiber()`/stack alloc returns
    NULL (`:178-181`) and the scheduler faults -> freeze.
- **`MICROBIT_FIBER_MAXIMUM_FIBER_POOL_SIZE 3` does NOT help** (user
  hypothesis, checked and refuted):
  - It is ALREADY 3 by default (`MicroBitConfig.h:176`), so setting it to 3 is
    a no-op.
  - It caps only the `fiberPool` = pool of DEAD/recycled fiber contexts kept
    for reuse (`MicroBitFiber.cpp:759-771`, drained in `release_fiber`), NOT
    the number of live fibers on `fiberList`. Live/blocked fibers - the thing
    that OOMs - are unbounded by it. A smaller pool actually returns memory to
    the heap slightly sooner on fiber exit.
- **Diagnostic added to main.cpp (`DIAG` gate, default 1):**
  - `countActiveFibers()` walks `get_fiber_list()` via `->next` (public API,
    `MicroBitFiber.h:130`) under a brief `__disable_irq()` critical section.
  - A `monitorFiber` (started with `create_fiber`) prints ~4x/s independently
    of the blocked handlers: `MON fibers=.. peak=.. inflight=.. peakInflight=..
    heap0=..`. When MON lines stop, that is the freeze; last `peak` = depth.
  - Each handler logs `ENTER`/`LEAVE` with the live fiber count, visualising
    fibers piling up as events stack during a blocking display op.
- **Real fixes (candidate follow-up experiments, not yet applied):** register
  listeners with `MESSAGE_BUS_LISTENER_DROP_IF_BUSY` (drop reentrant events
  instead of queue+fork), and/or move display work off the handler into one
  dedicated fiber so handlers never block. Either bounds peak fiber count.

## Empirical confirmation (2026-07-22, logfibers run)

- Diagnostic run on simulated v1 confirmed the mechanism exactly. Active-fiber
  count = 3 baseline (main + monitor + idle) + 1 per concurrently-blocked
  handler (fork-on-block). Observed: peakInflight=3 -> peakFibers=6.
  `QUEUE_IF_BUSY` seen draining sequentially (LEAVE AB immediately followed by
  ENTER AB), i.e. the queued second click, not parallel duplication.
- `heap0` stayed pinned at 3776 for the whole run including the crash -
  reconfirms `device_heap_size()` is segment SIZE, not free bytes; the fiber
  count is the only usable signal. Simulated-v1 budget = heap0 3776 + heap1
  1024 ~= 4800 bytes total.
- Crash occurred at only ~3 concurrent blocked handlers (serial dropped mid
  `heap0:3776` line -> hard fault + USB reset). Low ceiling explained by
  `verify_stack_size` (`MicroBitFiber.cpp:814-850`): each forked fiber's stack
  buffer is sized to the FULL stack depth from top-of-RAM down to the deep
  `fiber_sleep` inside the display call chain (hundreds of bytes to >1KB each).
  3-4 of those + the ManagedString allocs in each `writeSerialValue`
  (heap0:/heap1:/x: = 3 concats/handler) exhaust ~4.8KB. With
  `panic_on_heap_full=0` the failing malloc returns NULL -> NULL-deref fault.
- Next step to make OOM unambiguous: set `panic_on_heap_full: 1` (keep
  `keep_v1_heap_on_32kb: 1`). At the freeze the board should show DAL panic
  code 20 = `MICROBIT_OOM` (`ErrorNo.h:78`) on the matrix rather than a silent
  reset. Any other code => different bug.
- Note: the instrumentation itself (per-handler ManagedString serial dumps)
  adds heap churn and makes it die marginally sooner; does not change the
  conclusion.

## The fix (2026-07-22, FIX_NONBLOCKING toggle)

- Confirmed OOM with `panic_on_heap_full: 1`: crashed forking the 5th
  concurrent blocked handler (`ENTER P1 inflight=5 fibers=7`, serial drops),
  board shows panic code 20 = `MICROBIT_OOM`. Budget ~4.8KB / ~5 stacks =>
  ~950 B per forked display-fiber stack.
- Added `FIX_NONBLOCKING` compile toggle to `source/main.cpp` (default 1) so
  the one file does both repro (0) and fix (1):
  - **Root cause is blocking work inside the event handler**, which the fork-
    on-block message bus turns into one heap-allocated fiber stack per
    concurrent event. Fix removes the blocking from handlers.
  - Handlers now call `show(text)` -> `enqueueDisplay()`: copies the string
    into a fixed 16-slot ring buffer (`DisplayRequest{char text[4]; bool
    scroll;}`) under a brief `__disable_irq()` and returns immediately. No
    blocking, no per-event heap allocation, nothing to fork. Queue-full =>
    drop + `dqDrops++` (bounded, safe failure the original lacked).
  - A single `displayConsumerFiber` (started via `create_fiber`) drains the
    queue and does the blocking `printChar`/`scroll` + `writeSerialValue`.
  - `monitorFiber` MON line gains `drops=` under the fix.
- Expected fix behaviour: active fiber count pinned flat (main + monitor +
  consumer + idle, ~3-4) regardless of event rate; `peakInflight` ~1; no
  crash; `drops` rises under a storm instead of a freeze. This is the same
  shape pxt-microbit needs: never block in the handler.
- Kept both `__disable_irq`/`__enable_irq` critical sections tiny (copy in/out
  only). Handlers run in fiber context via fork-on-block, but since the
  enqueue never blocks, async_callback returns without forking - so the FOB
  fiber is reclaimed immediately. Verified empirically that serial printf in
  diagEnter/diagLeave does NOT itself fork (original log showed forks only at
  the display call, not at the ENTER print).

## DAL-level, v1-only fix (2026-07-22)

Requirement: fix belongs in the DAL, must affect ONLY 16KB v1 (v2/v3 are fine
and must stay byte-for-byte unchanged), minimal behavioural uncertainty.

Chosen approach: **back-pressure in the message-bus idle dispatch**, gated at
runtime on TOTAL HEAP CAPACITY (not physical RAM). Rationale for this over the
alternatives:

- **Gate on heap capacity, not `microbit_ram_size()`**: the user reproduces v1
  on real 32KB hardware via `keep_v1_heap_on_32kb`, where the FICR still reports
  32768 - so a RAM-size gate would never fire in that setup (and wouldn't let
  them validate). The real constraint is a small HEAP. Total capacity (sum of
  device_heap_size over all heaps) is ~4800 on both real v1 and simulated-v1,
  ~20400 on v2/v3, so a 8192 threshold classifies both v1 cases as small-heap
  and excludes v2/v3. Fixed after boot, so classified once and cached.

- Fork-on-block is the single mechanism turning "blocking handler" into "heap-
  allocated fiber stack". The one place all such fibers originate for user
  events is `MicroBitMessageBus::idleTick()` -> `process()` -> `invoke(
  async_callback,...)` (non-urgent handlers only; urgent/NONBLOCKING run inline
  and never fork). So idleTick is the correct single choke point.
- Capping fibers inside the scheduler (`handle_fob`) was rejected: declining a
  fork there leaves the FOB flag set and `schedule()` (`MicroBitFiber.cpp:880`)
  then derefs a NULL `forkedFiber` -> crash. Clearing FOB instead would block
  the idle fiber, which fork-on-block exists specifically to avoid. Too invasive
  / too risky.
- Gating on FREE HEAP rather than a fiber-count magic number: self-tuning, makes
  no assumption about per-stack size or baseline fiber count, and directly
  targets the exhausted resource. Required adding `device_heap_free()`.
- Safe against shifting the OOM into the event queue: that queue is already
  bounded at `MESSAGE_BUS_LISTENER_MAX_QUEUE_DEPTH` (=10), excess dropped at
  `queueEvent` (`MicroBitMessageBus.cpp:174`). So fibers AND queue are bounded.

Changes (all in yotta_modules/microbit-dal, the fork):
- `source/core/MicroBitHeapAllocator.cpp` + `inc/core/MicroBitHeapAllocator.h`:
  new `device_heap_free()` - walks each heap's block list, sums free-block
  payload (total free, not largest-contiguous). Also independently useful (it's
  the free-bytes API whose absence caused the earlier `cap=`/`heap0=` constant
  confusion).
- `inc/core/MicroBitConfig.h`: `MICROBIT_MESSAGE_BUS_LIMIT_FIBERS_16K` (default
  1), `MICROBIT_MESSAGE_BUS_SMALL_HEAP_THRESHOLD` (default 8192 bytes), and
  `MICROBIT_MESSAGE_BUS_FIBER_HEADROOM_16K` (default 1536 bytes).
- `inc/platform/yotta_cfg_mappings.h`: yotta mappings so limit + headroom are
  tunable from config.json (`microbit-dal.message_bus_limit_fibers_16k`,
  `...fiber_headroom_16k`).
- `source/drivers/MicroBitMessageBus.cpp`: at the top of `idleTick()`, cache
  once whether total heap capacity <= threshold (isSmallHeap); if small-heap
  AND `device_heap_free() < HEADROOM`, return without delivering (event stays
  queued). On v2/v3 `isSmallHeap` is 0 -> reduces to one int test, `device_heap_
  free()` never called -> identical path to before.

Behaviour:
- v1: under an event burst, handlers run less concurrently (back-pressured);
  events wait in the bounded queue; no OOM panic. Strictly better than the
  current crash.
- v2/v3: unchanged (runtime-gated out; one cached compare).
- Tunables: raise HEADROOM if a handler transiently allocates a lot; lower to
  allow more concurrency; set LIMIT_FIBERS_16K=0 to disable entirely.

Validation note: `source/main.cpp` `FIX_NONBLOCKING` set back to 0 so the repro
exercises ONLY the DAL fix (both fixes on would mask each other). Expected: MON
`fibers` stays bounded, no crash, `heap0`/free never approaches 0. Not yet
built/flashed here (no yotta toolchain in this env) - needs on-device confirm,
ideally on a real 16KB board as well as the simulated one.

## Decisions

- 2026-07-21: Kept the script's own apparent bug verbatim - the P3 touch
  handler calls `basic.showString("1", 150)` (shows "1", not "3") in the
  source MakeCode script, and the port's `onTouchP3()` does the same. Not
  "fixed", since the whole point is a faithful repro; if this turns out to
  matter it should be visible in the port too.
- 2026-07-21: `source/main.cpp` is this project's single rotating repro
  slot (established across this session) - the prior heap-churn stress test
  was preserved as `source/main_ramtest.cpp` before this port replaced
  `main.cpp`.
