#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>

void segment_control(int num)
{
    int i;
    int gpiopins[4] = {23, 18, 15, 14};
    int number[10][4] = {
        {0, 0, 0, 0},
        {0, 0, 0, 1},
        {0, 0, 1, 0},
        {0, 0, 1, 1},
        {0, 1, 0, 0},
        {0, 1, 0, 1},
        {0, 1, 1, 0},
        {0, 1, 1, 1},
        {1, 0, 0, 0},
        {1, 0, 0, 1},
    };
    for (i = 0; i < 4; i++)
        pinMode(gpiopins[i], OUTPUT);
    
    if (num < 0 || num > 9)
    {
        for (i = 0; i < 4; i++)
            digitalWrite(gpiopins[i], HIGH);
    }
    else
    {
        for (i = 0; i < 4; i++)
            digitalWrite(gpiopins[i], number[num][i] ? HIGH : LOW);
        delay(1000);
    }

}
