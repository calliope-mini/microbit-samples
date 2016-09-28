#include "MicroBit.h" 
#include "CalliopeRGB.h" 

MicroBit uBit;
CalliopeRGB                 rgb;
MicroBitDisplay display;

int main() {
    uBit.init();

    display.scroll("RED");
    rgb.Set_Color(0xff, 0x00, 0x00, 0);
    display.scroll("GREEN");
    rgb.Set_Color(0x00, 0xff, 0x00, 0);
    display.scroll("BLUE");
    rgb.Set_Color(0x00, 0x00, 0xff, 0);
    display.scroll("OFF");
    rgb.Set_Color(0xff, 0xff, 0xff, 0);
} 

