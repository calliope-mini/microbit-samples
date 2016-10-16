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

#define MICROBIT_DBG 1
#include "MicroBit.h"
#include "MicroBitSamples.h"

#ifdef MICROBIT_SAMPLE_ACCELEROMETER_DEMO

MicroBit uBit;
//MicroBitSerial serial(USBTX, USBRX);

MicroBitImage OUTPUT_ON("\
        0 1 1 1 0\n\
        1 1 1 1 1\n\
        1 1 1 1 1\n\
        1 1 1 1 1\n\
        0 1 1 1 0\n");


//
// Scales the given value that is in the -1024 to 1024 range
// int a value between 0 and 4.
//
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

int shaken = 0;

void onShake(MicroBitEvent) {
	uBit.display.print(OUTPUT_ON);
        shaken = 1;
}


int main()
{
    // Initialise the micro:bit runtime.
    uBit.init();

    //
    // Periodically read the accelerometer x and y values, and plot a 
    // scaled version of this ont the display. 
    //
    uBit.accelerometer.configure();
//     serial.baud(115200);
//     serial.send("hi joerg\n\r");

    uBit.messageBus.listen(MICROBIT_ID_GESTURE, MICROBIT_ACCELEROMETER_EVT_SHAKE, onShake);

    while(1)
    {
	if (shaken) {
		uBit.sleep(1000);
		shaken=0;
	}
	    int rx = uBit.accelerometer.getX();
	    int ry = uBit.accelerometer.getY();
	    int x = pixel_from_g(uBit.accelerometer.getX());
	    int y = pixel_from_g(uBit.accelerometer.getY());
	    //	int id = uBit.accelerometer.whoAmI();
// 	    serial.send("x=");
// 	    serial.send(rx);
// 	    serial.send("\r\n");
// 	    serial.send("y=");
// 	    serial.send(ry);
// 	    serial.send("\r\n");
        uBit.display.image.clear();
        uBit.display.image.setPixelValue(y, x, 255);
        
        uBit.sleep(100);
    }
}

#endif
