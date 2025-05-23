// For Raspberry Pi
#include <wiringPi.h>
#include <softTone.h>
#include <wiringPiI2C.h>

#include <syslog.h>
#include <sys/sem.h>
#include <stdlib.h>

#define SPKR 	25 	/* GPIO25 */
#define TOTAL 	32 	/* 학교종의 전체 계이름의 수 */
#define PUZZLE_TOTAL   8       /* 총 음의 개수 */
#define NOTE_DURATION  150     /* 각 음 길이(ms), 필요에 따라 수정 */

int notes[] = {     /* 학교종을 연주하기 위한 계이름 */
    391, 391, 440, 440, 391, 391, 329.63, 329.63, \
    391, 391, 329.63, 329.63, 293.66, 293.66, 293.66, 0, \
    391, 391, 440, 440, 391, 391, 329.63, 329.63, \
    391, 329.63, 293.66, 329.63, 261.63, 261.63, 261.63, 0
};

/// 음계(Hz): 솔5, 파#5, 레#5, 라4(낮은라), 솔#4(낮은솔#), 미5, 솔#5, 도6
int puzzleNotes[PUZZLE_TOTAL] = {
    783,  /* G5  = 솔 */
    740,  /* F#5 = 파# */
    622,  /* D#5 = 레# */
    440,  /* A4  = 낮은라 */
    415,  /* G#4 = 낮은솔# */
    659,  /* E5  = 미 */
    830,  /* G#5 = 솔# */
    1047  /* C6  = 도 */
};

void sem_V_For_buz(int semid) {
    syslog(LOG_INFO, "semop V start");
    
    struct sembuf op = { 0, +1, 0 };
    if (semop(semid, &op, 1) == -1) {
        syslog(LOG_INFO, "semop V 실패");
        exit(1);
    }
    syslog(LOG_INFO, "semop V 성공");
    return;
}

void musicPlay(void* arg)
{
    int i;

    softToneCreate(25); 	/* 톤 출력을 위한 GPIO 설정 */
    for (i = 0; i < TOTAL; ++i) {
        softToneWrite(SPKR, notes[i]); /* 톤 출력 : 학교종 연주 */
        delay(280); 		/* 음의 전체 길이만큼 출력되도록 대기 */
    }
}

void puzzleJingle(void* arg)
{
    int i;
    syslog(LOG_INFO, "[%s] start puzzle BGM", __func__);

    if (softToneCreate(SPKR) != 0) {
        syslog(LOG_ERR, "[%s] softToneCreate failed", __func__);
        return;
    }

    for (i = 0; i < PUZZLE_TOTAL; ++i) {
        softToneWrite(SPKR, puzzleNotes[i]);
        delay(NOTE_DURATION);
    }

    /* 마지막에 톤 멈춤 */
    softToneWrite(SPKR, 0);
    syslog(LOG_INFO, "[%s] end puzzle BGM", __func__);
}

void myalarm(int t)
{
    syslog(LOG_INFO, "[%s] start", __func__);

    softToneCreate(SPKR); 	/* 톤 출력을 위한 GPIO 설정 */
    softToneWrite(SPKR, notes[0]); /* 임의의 소리 출력 */
    delay(t); 		/* 음의 전체 길이만큼 출력되도록 대기 */
    softToneWrite(SPKR, 0); /* 톤 출력 중지 */

    syslog(LOG_INFO, "[%s] end", __func__);
    return;
}

void muteForS(void* arg)
{
    syslog(LOG_INFO, "[%s] start", __func__);

    int semid_music = *(int*)arg;

    syslog(LOG_INFO, "[%s] semid : %d", __func__, semid_music);

    softToneWrite(SPKR, 0); /* 톤 출력 중지 */
    
    syslog(LOG_INFO, "[%s] after softPwM", __func__);
    
    sem_V_For_buz(semid_music); // unlock
    
    syslog(LOG_INFO, "[%s] end", __func__);
    return;
}