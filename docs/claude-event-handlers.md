# Event handler registration in source/main.cpp

## 2026-07-14

### Notes

- `source/main.cpp` is the Calliope Mini hardware test firmware (serial banner "Calliope Mini Hardware Test"). Existing tests (`testTouchPins`, `testButtons`, `testMotor`, etc.) are polling-based, called from `main()`.
- Reference pattern for event-driven code: `source/examples/button-events/main.cpp` — registers handlers via `uBit.messageBus.listen(SOURCE, VALUE, handlerFn)`, and calls `pin.isTouched()` once up front to put a pin into touch-sense mode (required before touch events fire on `MICROBIT_ID_IO_Pn`).
- DAL in use is `yotta_modules/microbit-dal` (the actual build target resolves here; `microbit-dal_ubit` is an unused duplicate checkout, confirmed identical via `diff -rq`). It's a Calliope-patched fork (`module.json` depends on `calliope-mini/mbed-classic#...-calliope`) but the event/constant surface is stock micro:bit-dal — no BLE removal, no gesture differences.
- Relevant constants confirmed by grep in `yotta_modules/microbit-dal/inc/`:
  - Component IDs — `core/MicroBitComponent.h`: `MICROBIT_ID_BUTTON_A/B/AB`, `MICROBIT_ID_GESTURE`=13, `MICROBIT_ID_IO_P0..P17`=100-117.
  - Button values — `MicroBitButton.h`: `_EVT_DOWN/_UP/_CLICK/_LONG_CLICK/_HOLD/_DOUBLE_CLICK`.
  - Gesture values (used with `MICROBIT_ID_GESTURE`) — `drivers/MicroBitAccelerometer.h`: `MICROBIT_ACCELEROMETER_EVT_TILT_UP/_DOWN/_LEFT/_RIGHT`, `_FACE_UP/_FACE_DOWN`, `_FREEFALL`, `_3G/_6G/_8G`, `_SHAKE`.
- Build verified with `source /home/hugo/fw/YOTTAENV/bin/activate && yt build` (yotta env lives outside the repo, at `/home/hugo/fw/YOTTAENV`, not the sibling `microbit-samples-calliope/yottaENV`). Only pre-existing BLE `-Wdeprecated-copy` warnings from vendored `ble` module, unrelated to this change.

### Decisions

- **2026-07-14**: Added two new registration functions, `registerButtonHandlers()` (buttons A/B/A+B + touch P0-P3) and `registerGestureHandlers()` (accelerometer gestures: tilt, face up/down, freefall, shake, 3g/6g/8g), both called near the top of `main()` alongside the existing polling tests rather than replacing them.
  - Why: user wants event-driven coverage in addition to the existing polling-based hardware tests, not a replacement — polling tests like `testTouchPins`/`testButtons` stay as manually-invoked ad hoc checks, while the new handlers run continuously in the background for the whole session.
  - Alternatives considered: covering radio/serial/BLE events too — deferred, out of scope for this pass (user selected only buttons+touch and gestures).

## Buttons/pins firing 3x per press (2026-07-23)
- Symptom: pressing a button (or touching P0-P3) scrolled its label 3 times.
- Cause: `registerButtonHandlers()` in `source/main.cpp` listened with
  `MICROBIT_EVT_ANY`. One short press emits three events - `MICROBIT_BUTTON_EVT_DOWN`
  (1), `_UP` (2), `_CLICK` (3) (`MicroBitButton.h:41-46`) - so EVT_ANY ran the handler
  once per event. Touch pins use the same button event model, same effect.
- Fix: listen for `MICROBIT_BUTTON_EVT_CLICK` specifically -> one call per press. Other
  useful values on the same source: DOWN/UP for press/release edges, HOLD/LONG_CLICK/
  DOUBLE_CLICK for gestures.
