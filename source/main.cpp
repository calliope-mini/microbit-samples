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

void testButtons()
{
    // Wait for button A or B press and scroll which one was pressed
    if (uBit.buttonA.isPressed())
        uBit.display.scroll("A");
    else if (uBit.buttonB.isPressed())
        uBit.display.scroll("B");
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

int main()
{
    // Initialise the micro:bit runtime.
    uBit.init();
    uBit.serial.send("Calliope Mini Hardware Test\r\n");

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
        testAnalogPins();
        
        // Accelerometer Test
        // Periodically read the accelerometer x and y values, and plot a 
        // scaled version of this ont the display. 
        int x = pixel_from_g(uBit.accelerometer.getX());
        int y = pixel_from_g(uBit.accelerometer.getY());
        uBit.display.image.clear();
        uBit.display.image.setPixelValue(x, y, 255);
        uBit.sleep(100);
    }
}

