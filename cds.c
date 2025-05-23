// For Raspberry Pi
#include <wiringPi.h>
#include <softTone.h>
#include <wiringPiI2C.h>
#include <syslog.h>

#include <stdio.h>

int CDS_sensor(int fd)
{
    int i, cnt;
    int a2dChannel = 0;     // analog channel AIN0, CDS sensor
    int prev, a2dVal;

    wiringPiI2CWrite(fd, 0x00 | a2dChannel);       // 0000_0000

    prev = wiringPiI2CRead(fd);     // Previously byte, garvage
    a2dVal = wiringPiI2CRead(fd);
    // syslog(LOG_INFO, "[%s] prev = %d, a2dVal = %d", __func__, prev, a2dVal);
    return a2dVal;
}
