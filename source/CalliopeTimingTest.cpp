#include "MicroBit.h" 
#include "CalliopeRGB.h" 

MicroBit uBit;
CalliopeRGB                 rgb;

int main() {
    uBit.init();

    rgb.Set_Color(0xff, 0x00, 0x00, 0);
    uBit.sleep(1000);
    rgb.Set_Color(0x00, 0xff, 0x00, 0);
    uBit.sleep(1000);
    rgb.Set_Color(0x00, 0x00, 0xff, 0);
    uBit.sleep(1000);
    rgb.Set_Color(0xff, 0xff, 0xff, 0);

} 

