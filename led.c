// For Raspberry Pi
#include <wiringPi.h>
#include <softPwm.h>
#include <wiringPiI2C.h>

#define GPIO19 19

void led_onoff(int turn_on)
{
    pinMode(GPIO19, OUTPUT);
    
    if(turn_on == 1)
        digitalWrite(GPIO19, HIGH);
    else
        digitalWrite(GPIO19, LOW);
}

void led_brightness(int brightness)
{
    // pinMode(GPIO19, OUTPUT);
    softPwmCreate(GPIO19, 0, 100);
    softPwmWrite(GPIO19, brightness);
}