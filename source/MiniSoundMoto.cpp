#include "MicroBit.h" 
#include "CalliopeRGB.h" 
#include "CalliopeSoundMotor.h" 

MicroBit uBit;
CalliopeRGB                 rgb;
CalliopeSoundMotor          soundmotor;

int main() {
    // Initialise the micro:bit runtime.
    uBit.init();
    uBit.sleep(10000);

    rgb.Set_Color(0, 0xff, 0xff, 0);
    uBit.sleep(10000);

    while (1) {
        //motors A and B on for 12 seconds, blink red and green
        soundmotor.MotorB_On(80);
        soundmotor.MotorA_On(80);

        for (uint8_t i = 0; i < 6; i++) {
            rgb.Set_Color(0, 100, 0, 0);
            uBit.sleep(1000);
            rgb.Set_Color(100, 0, 0, 0);
            uBit.sleep(1000);
        }

        //all off, short delay
        soundmotor.MotorB_Off();
        soundmotor.MotorA_Off();
        rgb.Set_Color(0, 0, 0, 0);
        uBit.sleep(2000);

        //sound on for 6 seconds in silent mode, blink blue and white
        soundmotor.Sound_Set_Silent_Mode(true);
        soundmotor.Sound_On(50);


        for (uint8_t i = 0; i < 3; i++) {
            rgb.Set_Color(0, 0, 100, 0);
            uBit.sleep(1000);
            rgb.Set_Color(100, 100, 100, 0);
            uBit.sleep(1000);
        }

        //sound on for 6 seconds without silent mode, blink blue and white
        soundmotor.Sound_Set_Silent_Mode(false);
        soundmotor.Sound_On(100);

        for (uint8_t i = 0; i < 3; i++) {
            rgb.Set_Color(0, 0, 100, 0);
            uBit.sleep(1000);
            rgb.Set_Color(100, 100, 100, 0);
            uBit.sleep(1000);
        }

        //all off, short delay
        soundmotor.Sound_Off();
        rgb.Set_Color(0, 0, 0, 0);
        uBit.sleep(2000);
    }
} 

