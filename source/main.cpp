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

MicroBit uBit;

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

// Continuously grows the heap (fixed-size block allocations) until malloc()
// fails, then frees everything back down and repeats, printing progress each
// step. Meant to be called once per main-loop iteration (not itself a loop).
//
// microbit_heap_size(i) only reports each heap region's configured capacity,
// not live free/used bytes (see docs/claude-ram-detection-ble-gating.md), so
// this tracks bytes it has allocated itself as the "live usage" signal, and
// reports the real malloc() failure as the actual OOM signal.
void testHeapStress()
{
    static const int BLOCK_SIZE = 256;
    static const int MAX_BLOCKS = 80; // >= 16KB (largest single heap region) / BLOCK_SIZE
    static void*     blocks[MAX_BLOCKS];
    static int       count = 0;
    static bool      growing = true;

    if (growing)
    {
        void* p = (count < MAX_BLOCKS) ? malloc(BLOCK_SIZE) : NULL;
        if (p != NULL)
        {
            blocks[count++] = p;
        }
        else
        {
            growing = false;
            uBit.serial.send("[heap-stress] OUT OF MEMORY - freeing...\r\n");
        }
    }
    else
    {
        if (count > 0)
        {
            free(blocks[--count]);
        }
        else
        {
            growing = true;
            uBit.serial.send("[heap-stress] fully freed - growing again...\r\n");
        }
    }

    uint32_t heap0 = microbit_heap_size(0);
    uint32_t heap1 = microbit_heap_size(1);
    uint32_t heap2 = microbit_heap_size(2);

    uBit.serial.printf("[heap-stress] %s held=%d blocks (%lu bytes) | heap capacity: h0=%lu h1=%lu h2=%lu total=%lu\r\n",
                        growing ? "GROW  " : "SHRINK",
                        count, (unsigned long)(count * BLOCK_SIZE),
                        (unsigned long)heap0, (unsigned long)heap1, (unsigned long)heap2,
                        (unsigned long)(heap0 + heap1 + heap2));
}

void testButtons()
{
    // Wait for button A or B press and scroll which one was pressed
    if (uBit.buttonA.isPressed())
        uBit.display.scroll("A");
    else if (uBit.buttonB.isPressed())
        uBit.display.scroll("B");
}

void onButtonEvent(MicroBitEvent e)
{
    if (e.source == MICROBIT_ID_BUTTON_A)
        uBit.serial.send("BUTTON A: ");
    else if (e.source == MICROBIT_ID_BUTTON_B)
        uBit.serial.send("BUTTON B: ");
    else if (e.source == MICROBIT_ID_BUTTON_AB)
        uBit.serial.send("BUTTON A+B: ");
    else if (e.source == MICROBIT_ID_IO_P0)
        uBit.serial.send("TOUCH P0: ");
    else if (e.source == MICROBIT_ID_IO_P1)
        uBit.serial.send("TOUCH P1: ");
    else if (e.source == MICROBIT_ID_IO_P2)
        uBit.serial.send("TOUCH P2: ");
    else if (e.source == MICROBIT_ID_IO_P3)
        uBit.serial.send("TOUCH P3: ");

    if (e.value == MICROBIT_BUTTON_EVT_DOWN)
        uBit.serial.send("DOWN\r\n");
    else if (e.value == MICROBIT_BUTTON_EVT_UP)
        uBit.serial.send("UP\r\n");
    else if (e.value == MICROBIT_BUTTON_EVT_CLICK)
        uBit.serial.send("CLICK\r\n");
    else if (e.value == MICROBIT_BUTTON_EVT_LONG_CLICK)
        uBit.serial.send("LONG_CLICK\r\n");
    else if (e.value == MICROBIT_BUTTON_EVT_HOLD)
        uBit.serial.send("HOLD\r\n");
    else if (e.value == MICROBIT_BUTTON_EVT_DOUBLE_CLICK)
        uBit.serial.send("DOUBLE_CLICK\r\n");
}

// Register handlers for button A/B/A+B clicks and P0-P3 touch events.
void registerButtonHandlers()
{
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_A, MICROBIT_EVT_ANY, onButtonEvent);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_B, MICROBIT_EVT_ANY, onButtonEvent);
    uBit.messageBus.listen(MICROBIT_ID_BUTTON_AB, MICROBIT_EVT_ANY, onButtonEvent);

    uBit.messageBus.listen(MICROBIT_ID_IO_P0, MICROBIT_EVT_ANY, onButtonEvent);
    uBit.messageBus.listen(MICROBIT_ID_IO_P1, MICROBIT_EVT_ANY, onButtonEvent);
    uBit.messageBus.listen(MICROBIT_ID_IO_P2, MICROBIT_EVT_ANY, onButtonEvent);
    uBit.messageBus.listen(MICROBIT_ID_IO_P3, MICROBIT_EVT_ANY, onButtonEvent);

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

        uBit.sleep(100);
    }
}

