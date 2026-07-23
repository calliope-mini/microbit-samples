/*
The MIT License (MIT)

Copyright (c) 2016 British Broadcasting Corporation.
This software is provided by Lancaster University by arrangement with the BBC.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "MicroBit.h"
#include "MicroBitHeapAllocator.h"   // device_heap_size(), MICROBIT_MAXIMUM_HEAPS, MICROBIT_HEAP_BLOCK_SIZE
#include "MicroBitFiber.h"           // get_fiber_list(), Fiber

MicroBit uBit;

/*
 * Diagnostics: read the heap layout / free space and the live fiber count.
 * Useful for verifying the runtime Soft Device heap reclaim (heap 1 grows from
 * ~1KB to ~8KB on a BLE-gated-off boot) and for watching fibers pile up during
 * blocking display ops. All output goes to uBit.serial.
 */

// Number of fibers currently on the scheduler's global list (main + idle +
// any forked/blocked handler fibers). Walked with interrupts masked, since the
// scheduler can splice this list from interrupt context. PRIMASK is saved and
// restored rather than blindly re-enabled, so this is safe to call even from an
// already-critical section. The count is capped as a defensive guard against a
// corrupted (e.g. circular) list.
int countOpenFibers()
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    int count = 0;
    for (Fiber *f = get_fiber_list(); f != NULL && count < 256; f = f->next)
        count++;

    __set_PRIMASK(primask);
    return count;
}

// The allocator's internal heap table (defined in MicroBitHeapAllocator.cpp).
// Not exposed in the header, but HeapDefinition is public and the symbol has
// external linkage, so we can read it to walk block metadata directly. Unused
// slots have heap_start == NULL.
extern HeapDefinition heap[MICROBIT_MAXIMUM_HEAPS];

// Total free bytes across all heaps, computed by walking each heap's block list
// - the same scheme microbit_heap_print() uses: each block starts with a header
// word whose MICROBIT_HEAP_BLOCK_FREE bit flags a free run and whose low bits
// are the run length in blocks (header included). This is READ-ONLY: unlike a
// malloc() probe it never fails an allocation, so it can't trip the allocator's
// MICROBIT_PANIC_HEAP_FULL path (that panic, code 020, is why probing crashed).
// The figure matches the DAL's own mb_total_free (counts free block headers too).
uint32_t heapFreeBytes()
{
    uint32_t freeBlocks = 0;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (uint8_t i = 0; i < MICROBIT_MAXIMUM_HEAPS; i++)
    {
        uint32_t *block = heap[i].heap_start;
        uint32_t *end   = heap[i].heap_end;
        if (block == NULL)
            continue;

        while (block < end)
        {
            uint32_t blockSize = *block & ~MICROBIT_HEAP_BLOCK_FREE;
            if (blockSize == 0)     // corrupt/empty header - stop rather than spin forever
                break;
            if (*block & MICROBIT_HEAP_BLOCK_FREE)
                freeBlocks += blockSize;
            block += blockSize;
        }
    }

    __set_PRIMASK(primask);
    return freeBlocks * MICROBIT_HEAP_BLOCK_SIZE;
}

// One-line heap + fiber snapshot over serial. Per-heap sizes are the configured
// region capacities (0 for unused slots), so h1 reflects the Soft Device reclaim.
void readHeap()
{
    uBit.serial.printf("HEAP ram=%d", (int)microbit_ram_size());
    for (uint8_t i = 0; i < MICROBIT_MAXIMUM_HEAPS; i++)
        uBit.serial.printf(" h%d=%d", (int)i, (int)device_heap_size(i));
    uBit.serial.printf(" free=%d fibers=%d\r\n",
                       (int)heapFreeBytes(), countOpenFibers());
}

void testTouchPins()
{
    MicroBitPin* touchPins[] = { &uBit.io.P0, &uBit.io.P1, &uBit.io.P2, &uBit.io.P3 };
    const char*  pinNames[]  = { "P0", "P1", "P2", "P3" };

    uBit.display.scroll("TT");

    while (true)
    {
        for (int i = 0; i < 4; i++)
        {
            if (touchPins[i]->isTouched())
            {
                uBit.display.scroll(pinNames[i]);
            }
        }
        uBit.sleep(100);
    }
}



void toggleHeaderPins()
{
    MicroBitPin* pins[] = {
        &uBit.io.P0,  &uBit.io.P1,  &uBit.io.P2,  &uBit.io.P3,
        &uBit.io.P4,  &uBit.io.P5,  &uBit.io.P6,  &uBit.io.P7,
        &uBit.io.P8,  &uBit.io.P9,  &uBit.io.P10, &uBit.io.P11,
        &uBit.io.P12, &uBit.io.P13, &uBit.io.P14, &uBit.io.P15,
        &uBit.io.P16, &uBit.io.P17
    };
    uBit.display.disable();
    
    for (int i = 0; i < (int)(sizeof(pins) / sizeof(pins[0])); i++)
    {
        pins[i]->setDigitalValue(1);
        uBit.sleep(50);
        pins[i]->setDigitalValue(0);
        uBit.sleep(50);
    }
}

void testMotor()
{
    // Wake motor driver
    uBit.io.MOTOR_SLEEP.setDigitalValue(1);
    uBit.sleep(10);

    // Motor A Forward
    uBit.io.MOTOR_IN1.setAnalogValue(200);
    uBit.sleep(1000);


    // Motor B Forward
    uBit.io.MOTOR_IN1.setDigitalValue(0);
    uBit.io.MOTOR_IN2.setAnalogValue(200);
    uBit.sleep(1000);

    // Brake and sleep motor driver
    uBit.io.MOTOR_IN1.setDigitalValue(0);
    uBit.io.MOTOR_IN2.setDigitalValue(0);
    uBit.io.MOTOR_SLEEP.setDigitalValue(0);
}

void testSpeaker()
{
    // Speaker on Calliope is driven via the motor driver:
    //   MOTOR_SLEEP (nSLEEP) = HIGH  → driver active
    //   MOTOR_IN1   (IN1)    = HIGH  → H-bridge half pulled high
    //   MOTOR_IN2   (IN2)    = PWM   → audio square wave
    // No setAudioPin() in the DAL; use setAnalogPeriodUs + setAnalogValue
    // to generate a 50% duty-cycle tone, matching MakeCode analogPitch.

    uBit.io.MOTOR_SLEEP.setDigitalValue(1);
    uBit.io.MOTOR_IN1.setDigitalValue(1);

    // 440 Hz = 2273 µs period
    uBit.io.MOTOR_IN2.setAnalogPeriodUs(2273);
    uBit.io.MOTOR_IN2.setAnalogValue(512); // 50% duty cycle

    uBit.sleep(500);

    // Stop PWM and put driver back to sleep
    uBit.io.MOTOR_IN2.setAnalogValue(0);
    uBit.io.MOTOR_IN1.setDigitalValue(0);
    uBit.io.MOTOR_SLEEP.setDigitalValue(0);
}

void testMicrophone()
{
    // Measure peak-to-peak amplitude over 50 ms.
    // A single sample catches one instant of the AC waveform — loud sounds
    // randomly hit 0 (negative peak) or 1023 (positive peak).
    // Peak-to-peak = max - min over many samples gives stable loudness.
    const int WINDOW_MS = 50;
    int minVal = 1023, maxVal = 0;
    uint64_t deadline = uBit.systemTime() + WINDOW_MS;
    while ((int64_t)(uBit.systemTime() - deadline) < 0)
    {
        int v = uBit.io.MIC.getAnalogValue();
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    int amplitude = maxVal - minVal; // 0 (silent) .. ~1023 (very loud)

    // Display as a vertical bar on the 5x5 grid (0-5 lit rows from bottom)
    uBit.display.enable();
    uBit.display.image.clear();
    int bars = (amplitude * 5) / 512; // scale: 512+ fills all 5 rows
    for (int row = 0; row < bars; row++)
        for (int col = 0; col < 5; col++)
            uBit.display.image.setPixelValue(col, 4 - row, 255);
}

void testRGB()
{
    // Cycle through R, G, B, W then off
    uBit.rgb.setColour(255, 0, 0, 0); uBit.rgb.on(); uBit.sleep(400);
    uBit.rgb.setColour(0, 255, 0, 0); uBit.rgb.on(); uBit.sleep(400);
    uBit.rgb.setColour(0, 0, 255, 0); uBit.rgb.on(); uBit.sleep(400);
    uBit.rgb.setColour(0, 0, 0, 255); uBit.rgb.on(); uBit.sleep(400);
    uBit.rgb.off();
}

void testAnalogPins()
{

    // P4/P5/P6 are the LED matrix columns (LEDCOL1-3). The display refresh ISR
    // drives them continuously, so getAnalogValue() faults when it tries to
    // switch them into analog-input mode. Disable the display first.
    MicroBitPin* analogpins[]  = { &uBit.io.P1, &uBit.io.P2, &uBit.io.P4,
                             &uBit.io.P5, &uBit.io.P6, &uBit.io.P16, &uBit.io.P17 };
    const int    labels[] = { 1, 2, 4, 5, 6, 16, 17 };

    uBit.display.disable();

    for (int i = 0; i < (int)(sizeof(analogpins) / sizeof(analogpins[0])); i++)
    {
        uBit.serial.send(ManagedString(labels[i]) + ": "
                         + ManagedString(analogpins[i]->getAnalogValue()) + "\r\n");
    }
    uBit.display.disable();
}


void testButtons()
{
    // Wait for button A or B press and scroll which one was pressed
    if (uBit.buttonA.isPressed())
        uBit.display.scroll("A");
    else if (uBit.buttonB.isPressed())
        uBit.display.scroll("B");
}

static void onTouchP0(MicroBitEvent)
{
    uBit.display.scroll("0");    
}
static void onTouchP1(MicroBitEvent)
{
    uBit.display.scroll("1");
}
static void onTouchP2(MicroBitEvent)
{
    uBit.display.scroll("2");
}
static void onTouchP3(MicroBitEvent)
{
   uBit.display.scroll("3");
}

static void onButtonA(MicroBitEvent)
{
   uBit.display.scroll("A");
}
static void onButtonB(MicroBitEvent)
{
   uBit.display.scroll("B");
}
static void onButtonAB(MicroBitEvent)
{
   uBit.display.scroll("AB");
}

// Register handlers for button A/B/A+B clicks and P0-P3 touch events.
void registerButtonHandlers()
{
    // Listen for CLICK only. A single press emits DOWN, UP and CLICK, so
    // MICROBIT_EVT_ANY would fire the handler three times per press.
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_BUTTON_EVT_CLICK, onButtonA);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_BUTTON_EVT_CLICK, onButtonB);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_AB, MICROBIT_BUTTON_EVT_CLICK, onButtonAB);

    uBit.messageBus.listen(MICROBIT_ID_IO_P0, MICROBIT_BUTTON_EVT_CLICK, onTouchP0);
    uBit.messageBus.listen(MICROBIT_ID_IO_P1, MICROBIT_BUTTON_EVT_CLICK, onTouchP1);
    uBit.messageBus.listen(MICROBIT_ID_IO_P2, MICROBIT_BUTTON_EVT_CLICK, onTouchP2);
    uBit.messageBus.listen(MICROBIT_ID_IO_P3, MICROBIT_BUTTON_EVT_CLICK, onTouchP3);

    // Pins only raise touch events once they've been put into touch-sense mode.
    uBit.io.P0.isTouched();
    uBit.io.P1.isTouched();
    uBit.io.P2.isTouched();
    uBit.io.P3.isTouched();
}

void onGestureEvent(MicroBitEvent e)
{
    switch (e.value)
    {
        case MICROBIT_ACCELEROMETER_EVT_TILT_UP:    uBit.serial.send("GESTURE: TILT_UP\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_TILT_DOWN:  uBit.serial.send("GESTURE: TILT_DOWN\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_TILT_LEFT:  uBit.serial.send("GESTURE: TILT_LEFT\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_TILT_RIGHT: uBit.serial.send("GESTURE: TILT_RIGHT\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_FACE_UP:    uBit.serial.send("GESTURE: FACE_UP\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_FACE_DOWN:  uBit.serial.send("GESTURE: FACE_DOWN\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_FREEFALL:   uBit.serial.send("GESTURE: FREEFALL\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_3G:         uBit.serial.send("GESTURE: 3G\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_6G:         uBit.serial.send("GESTURE: 6G\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_8G:         uBit.serial.send("GESTURE: 8G\r\n"); break;
        case MICROBIT_ACCELEROMETER_EVT_SHAKE:      uBit.serial.send("GESTURE: SHAKE\r\n"); break;
        default: break;
    }
}

// Register a handler for accelerometer gesture events (tilt, face up/down, freefall, shake, 3g/6g/8g).
void registerGestureHandlers()
{
    uBit.messageBus.listen(MICROBIT_ID_GESTURE, MICROBIT_EVT_ANY, onGestureEvent);
}
int pixel_from_g(int value)
{
    int x = 0;
    
    if (value > -750)
    x++;
    if (value > -250)
    x++;
    if (value > 250)
    x++;
    if (value > 750)
    x++;
    
    return x;
}

void testAccelerometer()
{
    // Periodically read the accelerometer x and y values, and plot a
    // scaled version of this ont the display. Call in while loop
    int x = pixel_from_g(uBit.accelerometer.getX());
    int y = pixel_from_g(uBit.accelerometer.getY());
    uBit.display.image.clear();
    uBit.display.image.setPixelValue(x, y, 255);

}

int main()
{
    // Initialise the micro:bit runtime.
    uBit.init();
    uBit.serial.send("Calliope Mini Hardware Test\r\n");

    registerButtonHandlers();
    registerGestureHandlers();

    testRGB();
    testSpeaker();
    testMotor();
    readHeap();
    
    while(1)

    {
        // Microphone Test
        // testMicrophone();

        // Touch Pin Test
        // testTouchPins();

        // Header Pin Toggle Test
        // toggleHeaderPins();
    
        // Button Test
        // testButtons();
    
        // Analog Read Test
        // testAnalogPins();

        // Heap Stress Test
        // testHeapStress();

        // Accelerometer Test
        // testAccelerometer();

        // Heap / fiber diagnostics (~1 Hz: 10 x 100ms sleep below)
        static int diagTick = 0;
        if (++diagTick >= 10)
        {
            diagTick = 0;
            readHeap();
        }

        uBit.sleep(100);
    }
}

