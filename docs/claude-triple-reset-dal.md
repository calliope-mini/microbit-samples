# Triple-reset-to-pair on Calliope mini v1 (nRF51822 DAL)

Working notes/decision log for this task. See also the pre-existing `docs/triple-reset.md` and
`docs/session-notes.md` from an earlier session — treat those as a historical investigation log of
approaches that were tried, **not** as a description of a working solution (see below).

## 2026-07-14 — Status check on the prior session's work

Read `docs/triple-reset.md`, which documents a NOINIT-region + RTC1-tick-based design and labels it
"current, working." Two problems found:

1. The user confirmed that design **also turned out not to be reliable** in further testing, for
   reasons the notes don't capture. So the doc's final conclusion is stale/wrong, even though the
   investigation log leading up to it (documenting several confirmed-dead approaches) is accurate.
2. None of the described code changes are actually present in the working tree anymore —
   `yotta_targets/bbc-microbit-classic-gcc/ld/NRF51822.ld` and
   `yotta_modules/microbit/source/MicroBit.cpp` were both back to their pristine state. Root cause:
   `yotta_modules/` and `yotta_targets/` are both in `.gitignore`, so any edits there are silently lost
   on a fresh `yt install`/clone. The user said they'll set up git tracking for whatever files end up
   changed (microbit-dal is already tracked separately).

Decision: start the design over rather than resuscitate the RTC1 approach. Confirmed-dead approaches
carried forward from the old notes (do not re-try without new evidence):
- `NRF_POWER->GPREGRET` — cleared before the app runs. Very likely Nordic's MBR (Master Boot Record,
  shipped with every S110/S130 SoftDevice, runs before the SoftDevice/app on any DFU-capable nRF51)
  doing this as part of its own bootloader-selection logic — not something specific to this project's
  code, and not fixable from application code.
- `.noinit` attribute alone with no dedicated linker region — BSS-zero wipes it.
- Fixed RAM pointer without shrinking the linker `RAM` region — heap/stack grows over it.
- `MicroBitStorage` flash KV-store — ~50ms write, interrupted by rapid resets.
- NOINIT region + fiber-based decay timer — fiber scheduling races against slow BLE init in
  `MicroBit::init()`.
- NOINIT region + RTC1 tick decay — looked right, user reports it wasn't reliable in practice.

One thing from the old investigation that *did* hold up: a magic-number constant in a
linker-reserved RAM region (not just a `.noinit` attribute) persisted correctly across
button-triggered resets ("approach 5" in `docs/triple-reset.md`). That's the one primitive this new
design still relies on.

## New design

Drop every peripheral register (GPREGRET, RESETREAS, RTC1) as a source of truth. The only hardware
fact relied on is plain SRAM content surviving a button-triggered reset (VDD stays up, only logic
resets) — confirmed once already, being re-confirmed now (see below).

- A struct `{ uint32_t magic; uint32_t counter; }` lives in a dedicated `NOINIT` memory region carved
  out of the bottom of RAM in the linker script (not just a `.noinit` attribute — a real reserved
  `MEMORY` region, so `.data`/`.bss`/heap/stack can never be placed there).
- `magic` distinguishes "RAM lost power" (true cold boot) from "RAM survived" (any reset while VDD
  stayed up) — entirely software-defined, no dependency on RESETREAS/GPREGRET semantics.
- `counter` increments once per boot as long as `magic` is valid.
- (Not yet implemented) Decay of `counter` back to 0 should happen via `MicroBitComponent::systemTick()`
  — driven directly by the DAL's hardware system timer (TIMER1) interrupt, not a fiber and not RTC1 —
  once the app has been running continuously for ~1-2s. This mirrors codal's working
  `periodicCallback` pattern but only needs to count forward through one continuous run, so it can't
  race against slow BLE bring-up the way the old fiber-sleep did, and doesn't depend on any peripheral
  surviving reset.
- Once decay exists: if 3 boots happen before decay ever fires, `triple_reset = true`, fed into the
  existing `buttonA.isPressed() && buttonB.isPressed()` pairing-mode wait loop in
  `MicroBit::init()` (`yotta_modules/microbit/source/MicroBit.cpp:138`) — same slot the codal
  reference (`MicroBit.cpp` the user attached, for nRF52/codal devices) uses for its own
  `resetClickCount == 3` check.

## Validation-first plan (agreed with user, given the track record of "looked right, failed on hardware")

Before wiring into the pairing-mode loop, build the smallest possible diagnostic to re-confirm RAM
retention on real hardware:

- `yotta_targets/bbc-microbit-classic-gcc/ld/NRF51822.ld`: added a `NOINIT (rwx)` memory region
  (16 bytes at `0x20002000`), shrank `RAM` to start at `0x20002010`, added a `.noinit (NOLOAD)`
  section mapped to it.
- `source/main.cpp`: added `TripleResetDiag` struct placed via
  `__attribute__((section(".noinit")))`, incremented once per boot, logged over serial along with
  `RESETREAS` (read-then-cleared) and `GPREGRET` (read-only, not relied on) for corroborating data.
  Runs at the very top of `main()`, before `uBit.init()` (so the SoftDevice isn't enabled yet and
  direct `NRF_POWER` register access is safe) and before the pre-existing (uncommitted, unrelated)
  `uBit.panic(020)` debug line, which halts execution and would otherwise swallow the diagnostic
  output.
- Build-time verification done: `arm-none-eabi-nm`/`readelf` confirm `tripleResetDiag` lands exactly
  at `0x20002000` in a `NOBITS`/`NOLOAD` `.noinit` section (not `.bss`), so it's structurally
  guaranteed to survive the BSS-zero startup loop and never get reflashed with file data.
- **Not yet verified**: actual physical retention across a real button-triggered reset on hardware.
  That's the next step — flash `build/bbc-microbit-classic-gcc/source/microbit-samples-combined.hex`,
  watch serial, and test: full power cycle (expect `COLD counter=1`), single reset-button press
  (expect `WARM counter=2`), rapid triple-press (expect counter reaching 3 within the burst).

## 2026-07-14 — First hardware test: retention fails at 0x20002000

Flashed the diagnostic and watched serial. Result: **every boot read `COLD counter=1`**, including
after several rapid consecutive reset-button presses (`RESETREAS=0x00000001` = RESETPIN only each
time, `GPREGRET=0x00` each time as expected). This is a hard, reproducible falsification of the one
assumption both this new design and the prior session's "approach 5" depended on — plain SRAM
content did **not** survive this board's button-triggered reset, at least not at the address tried.

Investigated why. Found `MicroBitConfig.h:55`: `MICROBIT_SD_LIMIT 0x20002000` — "the end address of
memory normally reserved for Soft Device" for this board's S110 config. That is exactly the address
the diagnostic struct was placed at (`NRF51822.ld`'s `RAM`/`NOINIT` region both originated there) —
i.e. the very first byte the SoftDevice hands over to the application. Strong candidate explanation:
SoftDevice enable/bootstrap logic (`sd_softdevice_enable()` takes an `app_ram_base` argument) plausibly
touches/validates memory right at that boundary as part of its own housekeeping, stomping our struct
before the app ever reads it back.

Also noticed `startup_NRF51822.S`'s `Reset_Handler` only explicitly powers RAM banks 0–1 via `RAMON`
(mask `0x3`). Considered as a candidate but likely a red herring: the rest of the app's stack/heap/
`.bss` already lives at `0x20002000+` (blocks 2–3 in a 4×4KB layout) and clearly works throughout
execution, so those banks are evidently powered regardless.

**Decision:** relocate the reserved struct away from the SoftDevice/app RAM boundary, to the top of
RAM instead (`0x20003ff0`, just below the stack, `MICROBIT_SRAM_END = 0x20004000`), and retest before
concluding SRAM retention is impossible outright on this board. Verified via `nm`/`readelf` that the
struct now sits at `0x20003ff0` in the relocated `NOINIT` (`NOBITS`/`NOLOAD`) section. Not yet
retested on hardware.

If this *also* comes back `COLD` on every boot, the more likely conclusion is that the S110
SoftDevice/MBR bootstrap (which owns flash `0x0`–`0x18000` and whose reset vector runs first on
literally every chip reset, before our app's `Reset_Handler` ever executes) does a broader RAM
clear/init sweep as part of its own bootstrap — consistent with it also being the thing that clears
`GPREGRET`. That would mean *no* plain-RAM location is safe from it, and the next candidate worth
testing would be a raw, minimal single-word flash write (direct `NVMC` register poke to a dedicated
reserved flash page, bypassing `MicroBitStorage`'s KV/wear-leveling abstraction) — flash is
genuinely non-volatile regardless of what SoftDevice does to RAM, and a raw single-word write is
documented as microseconds-scale, unlike the ~50ms KV-store write already ruled out in the old notes.

## 2026-07-14 — Second hardware test: retention also fails at top-of-RAM

Reflashed with the struct relocated to `0x20003ff0` (top of RAM, away from the
`MICROBIT_SD_LIMIT` boundary). Result: **still `COLD counter=1` on every boot**, across several
rapid consecutive resets (`RESETREAS` varied: `0x00000000`, `0x00000005`, `0x00000001`,
`0x00000001` — consistent with the "first boot after flash looks different" pattern already noted,
then plain RESETPIN for subsequent presses). Two different addresses, same result: RAM retention
does not work on this board via a plain reserved region, regardless of address. Ruling out plain
SRAM retention as a viable primitive here (at least not without a mechanism not yet identified).

Pivoted to a raw-flash-write diagnostic (v2): `NVMC`-direct single-word writes to a dedicated
reserved flash page, incrementing a "tally" of written words per boot, erasing back to blank once
full. Implemented in `source/main.cpp` (`runTripleResetDiagnostic`, replacing the RAM version) and
reserved via a new `FLASHDIAG` region in `NRF51822.ld`.

**Near-miss caught before flashing:** first attempt reserved `FLASHDIAG` at `0x0003fc00` (the very
last 1KB page of the chip's 256KB flash). Decoding
`yotta_targets/bbc-microbit-classic-gcc/bootloader/BLE_BOOTLOADER_RESERVED.hex` (merged into
`*-combined.hex` by `toolchain.cmake`, outside the linker script's visibility) showed it is a real,
live BLE bootloader occupying flash `0x3c000`-`0x3fc20` (confirmed by decoding the record addresses,
excluding a UICR `BOOTLOADERADDR` record at `0x10001014`=`0x0003c000` which points at it) — not a
placeholder despite the "RESERVED" filename. `0x3fc00` overlaps its tail end, so the diagnostic
write would have silently corrupted live bootloader code on next flash. Relocated `FLASHDIAG` to
`0x0003bc00` (also shrank the app's own `FLASH` region to end there) — confirmed by decoding all
four relevant hex files (bootloader-reserved, softdevice, app-only, and the final combined) that
none contain any data in `0x3bc00`-`0x3bfff`. Nothing bad was written to hardware from this
near-miss; caught by static analysis before asking for another flash+test round.

Side note this also explains: the bootloader's own presence (running before the app on every reset,
and known to manipulate `GPREGRET`) is consistent with — and reinforces — the earlier finding that
`GPREGRET` reads 0 on every boot. Whether it's also responsible for the RAM-retention failures above
is still unconfirmed; not chasing that further since the flash-based approach sidesteps the question
entirely.

**Not yet tested on hardware**: the v2 flash-based diagnostic, at the corrected address.

## 2026-07-14 — Third hardware test: flash retention confirmed working

Reflashed at the corrected `0x3bc00` page. Result: counter climbed cleanly `3→4→5→6→7→8`, wrapped
back to `1` after hitting the 8-slot cap and erasing as designed. One anomalous jump (`1`→`3`,
skipping `2`) on the first couple of presses — read as a dropped/overlapped serial line from a fast
double-press (the previous boot's "Calliope Mini Hardware Test" text cut off mid-transmission by the
next reset), not a missed increment, since every other consecutive press incremented by exactly 1.

**Conclusion: raw NVMC flash writes reliably survive this board's button-triggered reset.** This is
the storage primitive to build the real feature on — RAM (in any tested location) is out.

User also noted the counter keeps climbing regardless of single vs. rapid-triple presses. Expected:
this diagnostic has no decay step yet, so nothing ever brings the count back down after a successful
boot. That's the next piece (see below) — without it there's no way to distinguish "3 presses in a
burst" from "3 resets over an afternoon."

## 2026-07-14 — Is rapid triple-press even achievable? (feasibility check before building the feature)

User raised the critical question before going further: boot takes ~1-2s (per the very first
diagnostic test: "every message takes a second to be displayed after reset"), but the whole feature
only makes sense if 3 presses can happen within that same ~1-2s window, not spread across three
full boot cycles. If something upstream of our own code eats most of that time on every single
reset, a "rapid triple press" might interrupt that upstream thing (not our counting code) on 2 of
the 3 presses, and never get counted at all.

Key distinction worth being precise about: it's much more likely to be the **bootloader**, not the
SoftDevice. The SoftDevice only gets enabled partway through `uBit.init()` — *after* our counting
code (which sits at the very top of `main()`) already ran, so SoftDevice bring-up can't be what
delays our counter. The bootloader (`0x3c000`-`0x3fc20`, confirmed real in the earlier investigation)
runs first on literally every physical reset, before our own `Reset_Handler` executes at all — if
anything is eating time before we get a chance to count a press, that's the prime suspect (crystal/
clock startup is a secondary candidate).

Added a boot-latency probe to isolate whether the observed ~1-2s is upstream of our own code
(bootloader/clock — bad, hard constraint) or downstream (serial terminal reconnect artifact — fine,
our counting code already runs fast, we just don't see confirmation of it instantly). The probe:
`uBit.display.image.setPixelValue(2, 2, 255)` as the literal first instruction in `main()`, before
even the flash-counter code. This works independent of `uBit.init()`/BLE because
`MicroBitDisplay`'s constructor already calls `system_timer_add_component()`
(`MicroBitSystemTimer.cpp:177`), which starts the system timer + LED refresh strobe during global
static initialization — i.e. before `main()` runs at all. So the center LED lighting up is a
fast, terminal-independent signal of "our own code has started running."

**Not yet tested on hardware.** Ask: reflash, then physically compare how fast the center LED lights
up after a reset-button press vs. how long the serial line takes to appear, and repeat the rapid
triple-press test while watching the LED (not just the serial log). If the LED lights up near-instantly
every time (including on rapid presses), our counting code is running fine per-press and the ~1-2s
was a terminal artifact — full steam ahead on the real feature. If the LED noticeably lags or misses
presses during a rapid burst, that's hard evidence the bootloader (or something else upstream) is
the bottleneck, and 3-rapid-presses-in-1-2s may not be reliably achievable from application code at
all — in which case the fallback is to relax the gesture (e.g. a longer window, or count "N resets
since last successful full boot" without a tight rapid-fire requirement) rather than continuing to
chase the original tight timing.

## 2026-07-14 — Boot-latency probe result: delay is upstream, not a terminal artifact

User reports the LED probe flashes "just before the serial output and program start" — i.e. LED and
serial appear together, both near the end of the ~1-2s wait, not spread apart. Conclusion: the
~1-2s delay happens **before** our own code runs at all (bootloader/clock startup, per the earlier
hypothesis), not a serial-terminal reconnect artifact. Our own code (LED then serial) is fast once
it starts; we just don't get a chance to start until ~1-2s after the physical reset.

This confirms the concerning case from the feasibility check above, but doesn't kill the feature —
it means the original framing was wrong, not that the mechanism is unworkable. The mistake: assuming
the gesture must fit inside a fixed, tight 1-2s window, modeled on codal/micro:bit v2 (near-instant
boot, no separate bootloader stage). Here, boot latency sets a **floor** on how fast a user can
physically cycle the reset button (~1-2s per press, unavoidable) — but the "was this a rapid reset
streak" threshold is a value chosen in our own app code, not dictated by hardware. Nothing requires
it to be tight.

**Reframed design:** trigger on "3 consecutive boots, none of which survived past a settle
threshold" rather than "3 presses within literally 1-2 seconds" — and size the settle threshold
generously (e.g. 4-6s), comfortably longer than one full boot cycle. A user mashing reset 3 times
~1-2s apart easily fits inside a 4-6s window. Same user-facing gesture ("press reset 3 times without
waiting for it to fully start"), just not sub-second — which was never actually required, only
assumed. This is the same flash-counter + decay/settle mechanism already validated above; the
settle duration just needs to be sized around real boot latency (~1-2s, measured) rather than an
assumed tight window.

**Not yet decided/implemented**: exact settle threshold value, and how to time it (a synchronous
`uBit.sleep()` after the existing pairing-check block in `MicroBit::init()` is viable now that we
don't need sub-second precision — no `systemTick`/ISR complexity required, unlike the earlier
RAM-decay plan).

## 2026-07-14 — User pushback: settle-window reframing not good enough, investigate speeding up boot instead

User rejected the settle-window reframing outright: a gesture that requires multi-second spacing
between presses would behave fundamentally differently from the same feature on other
micro:bit-family devices (near-instant boot, sub-second triple-click) — "not worth shipping" if it
can't match that. Correctly redirects the goal: don't design around the ~1-2s boot latency, find out
whether it can be removed.

Investigated why boot takes ~1-2s. Extracted the bootloader's raw code
(`BLE_BOOTLOADER_RESERVED.hex`, base `0x3c000`, length `0x3c20`) to a binary and ran `strings` on it —
found `DfuTarg` (confirms real DFU bootloader, as suspected), nothing more specific (no version
strings). The nRF51 SDK component sources present in this repo
(`yotta_modules/nrf51-sdk/.../bootloader_dfu/`) are the DFU *library* used by the application to
request entering the bootloader — not the bootloader's own standalone `main()`, which isn't
vendored here as source, so couldn't fully trace its internal timing logic from source.

Found a testable mechanism instead: the Master Boot Record checks `UICR->BOOTLOADERADDR` on every
single reset, before jumping anywhere. If set (it is here — `0x3c000`, written only by
`BLE_BOOTLOADER_RESERVED.hex`'s single UICR record at `0x10001014`; confirmed by scanning every hex
file merged into the build that nothing else touches UICR), the MBR routes through the bootloader
first, before our code or the SoftDevice runs — plausible root cause of the ~1-2s. `UICR` is its own
independently-erasable flash page (`NRF_NVMC->ERASEUICR`), separate from the bootloader's actual
code, which stays physically present and untouched. Erasing just that pointer should make the MBR
skip straight to the SoftDevice+app.

Implemented as a one-shot, idempotent, safely-reversible experiment in `source/main.cpp`
(`disableBootloaderRouting()`, called first thing in `main()`): if `NRF_UICR->BOOTLOADERADDR` isn't
already `0xFFFFFFFF`, erase the UICR page. The first boot after (re)flashing the bootloader-including
combined hex still routes through the bootloader (the flash tool just rewrote the pointer); every
boot after that should skip it if the hypothesis holds. Reversible at any time by reflashing the
current combined hex (which re-includes the UICR-writing record).

Hit an unrelated build break along the way: the user's own added RAM/heap diagnostic
(`microbit_heap_size()`) doesn't link — declared in `MicroBitHeapAllocator.h:96` but not actually
implemented anywhere in this DAL build (pre-existing declaration/implementation mismatch, unrelated
to this investigation). Commented out with the user's OK to unblock testing; not otherwise touched.

**Not yet tested on hardware.** Ask: reflash `build/bbc-microbit-classic-gcc/source/microbit-samples-combined.hex`
once (this pass still goes through the bootloader as usual, then clears the UICR pointer at the
end), then reset again and observe whether boot becomes noticeably faster (LED/serial appearing
much sooner). If it does, the bootloader was indeed the bottleneck and removing it from normal boots
makes the tight triple-reset gesture (matching other micro:bit-family devices) achievable — at the
cost of losing OTA BLE DFU capability, worth confirming isn't actually relied upon before making this
permanent (e.g. by dropping the bootloader merge from `toolchain.cmake` going forward). If boot
latency is unchanged, the bootloader wasn't the (sole) cause and the ~1-2s has some other source
worth chasing down before concluding the feature isn't shippable.

## Next steps (not yet done)

- Physically test the diagnostic on hardware, report back what the serial log shows.
- If retention holds up: add the `systemTick()`-based decay component.
- Wire `triple_reset` into `MicroBit::init()`'s pairing-mode loop.
- Set up git tracking for the changed files under `yotta_targets/`/`yotta_modules/microbit/` (user's
  action item — these dirs are gitignored).
