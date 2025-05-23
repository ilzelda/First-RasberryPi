#include <stdio.h>
#include <dlfcn.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h> // for CDS send
#include <wiringPiI2C.h> // for CDS
#include <wiringPi.h> // for delay
#include <softTone.h>

#include <pthread.h>
#include <sys/sem.h>
#include <errno.h>

#include "mythread.h"

// P 연산 (wait)
void sem_P(int semid) {
    struct sembuf op = { 0, -1, 0 };
    if (semop(semid, &op, 1) == -1) {
        syslog(LOG_INFO, "semop P 실패");
        exit(1);
    }
}

int sem_P_nonblock(int semid) {
    struct sembuf op = {
        .sem_num = 0,           // 세마포어 세트 중 인덱스 0
        .sem_op  = -1,          // 값을 1만큼 감소
        .sem_flg = IPC_NOWAIT   // 블록하지 않고 즉시 돌아오도록
    };

    if (semop(semid, &op, 1) == -1) {
        if (errno == EAGAIN) {
            // 세마포어 값이 0이라서 즉시 리턴된 경우
            syslog(LOG_INFO, "sem_P_nonblocking: 세마포어 사용 불가 (EAGAIN)");
            return -1;
        }
        // 그 외 오류
        // syslog(LOG_ERR, "sem_P_nonblocking 실패: %s", strerror(errno));
        exit(1);
    }

    return 0; // 성공적으로 세마포어를 획득한 경우
}

// V 연산 (signal)
void sem_V(int semid) {
    syslog(LOG_INFO, "semop V start");
    
    struct sembuf op = { 0, +1, 0 };
    if (semop(semid, &op, 1) == -1) {
        syslog(LOG_INFO, "semop V 실패");
        exit(1);
    }
    syslog(LOG_INFO, "semop V 성공");
    return;
}

void* led_thread(void* arg)
{
    void* handle = dlopen("libled.so", RTLD_LAZY);
    if(!handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }

    function_selector selector = *(function_selector*)arg;
    syslog(LOG_INFO, "[%s] %s : state-[%d], brightness-[%d]", __func__, selector.name, *selector.led_state, selector.brightness);

    if(strcmp(selector.name, "onoff") == 0) {
        typedef void (*led_onoff_t)(int);
        led_onoff_t led_onoff = (led_onoff_t)dlsym(handle, "led_onoff");
        
        char* error = dlerror();
        if(error) {
            syslog(LOG_ERR, "%s\n", dlerror());
            dlclose(handle);
        }
        sem_P(selector.semid_led); // lock
        *selector.led_state = !(*selector.led_state);
        led_onoff(*selector.led_state);
        sem_V(selector.semid_led); // unlock

    } else if(strcmp(selector.name, "brightness") == 0) {
        typedef void (*led_brightness_t)(int);
        led_brightness_t led_brightness = (led_brightness_t)dlsym(handle, "led_brightness");
        
        char* error = dlerror();
        if(error) {
            syslog(LOG_ERR, "%s\n", dlerror());
            dlclose(handle);
        }

        sem_P(selector.semid_led); // lock
        *selector.led_state = selector.brightness > 0 ? 1 : 0;
        led_brightness(selector.brightness);
        sem_V(selector.semid_led); // unlock

    } else {
        syslog(LOG_ERR, "cannot found function name");
        dlclose(handle);
        return NULL;
    }

    dlclose(handle);
}

void* buzzer_thread(void* arg)
{
    int oldstate;

    void* handle = dlopen("libbuzzer.so", RTLD_LAZY);
    if(!handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }

    typedef void (*musicPlay_t)(void*);
    typedef void (*muteForS_t)(void*);
    
    musicPlay_t musicPlay = (musicPlay_t)dlsym(handle, "musicPlay");
    muteForS_t muteForS = (muteForS_t)dlsym(handle, "muteForS");
    
    char* error = dlerror();
    if(error) {
        syslog(LOG_ERR, "%s\n", dlerror());
        dlclose(handle);
    }

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
    int ret = sem_P_nonblock(((music_arg*)arg)->semid_music); // lock
    if (ret < 0) {
        syslog(LOG_INFO, "[%s] semaphore busy, exiting thread with -1", __func__);
        pthread_exit((void*)(intptr_t)-1);
    }

    pthread_cleanup_push(muteForS, &( ((music_arg*)arg)->semid_music ));
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);

    syslog(LOG_INFO, "[%s] start", __func__);
    musicPlay(NULL);
    sem_V(((music_arg*)arg)->semid_music); // unlock
    syslog(LOG_INFO, "[%s] end", __func__);
    
    pthread_cleanup_pop(0); 

    dlclose(handle);
    pthread_exit((void*)(intptr_t)0);
}

void* cds_thread(void* arg)
{
    int cli_sfd;
    char selector_name[20];
    int cds_fd;
    char buffer[1024];
    int threshold = 220;

    cli_sfd = ((cds_arg*)arg)->client_sd;
    strcpy(selector_name, ((cds_arg*)arg)->name);
    syslog(LOG_INFO, "[%s] %s", __func__, selector_name);

    void* cds_handle = dlopen("libcds.so", RTLD_LAZY);
    if(!cds_handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }

    typedef int (*CDS_sensor_t)(int);
    
    CDS_sensor_t CDS_sensor = (CDS_sensor_t)dlsym(cds_handle, "CDS_sensor");
    
    char* error = dlerror();
    if(error) {
        syslog(LOG_ERR, "%s\n", dlerror());
        dlclose(cds_handle);
    }

    if((cds_fd = wiringPiI2CSetupInterface("/dev/i2c-1",0x48))<0) {
        syslog(LOG_ERR, "wiringPiI2CSetupInterface failed:\n");
    }
    syslog(LOG_INFO, "[%s] ADC/DAC(YL-40) Module testing........\n", __func__);

    if (strcmp(selector_name, "val_check") == 0) {
        int a2dVal = CDS_sensor(cds_fd);
        char prompt[] = "current CDS sensor value : ";
        char message[sizeof(prompt) + sizeof(a2dVal)];
        sprintf(message, "%s %d\n", prompt, a2dVal);
        send(cli_sfd, message, sizeof(message), 0);

    } else if (strcmp(selector_name, "led_control") == 0) {
        typedef void (*led_onoff_t)(int);
        void* led_handle = dlopen("libled.so", RTLD_LAZY);
        if (!led_handle) {
            syslog(LOG_ERR, "%s\n", dlerror());
            dlclose(cds_handle);
            return NULL;
        }

        led_onoff_t led_onoff = (led_onoff_t)dlsym(led_handle, "led_onoff");
        char* led_error = dlerror();
        if (led_error) {
            syslog(LOG_ERR, "%s\n", led_error);
            dlclose(led_handle);
            dlclose(cds_handle);
            return NULL;
        }

        int cds_value = CDS_sensor(cds_fd);
        syslog(LOG_INFO, "[%s] CDS_sensor value: %d",__func__, cds_value);

        sem_P(((cds_arg*)arg)->semid_led); // lock
        if (cds_value < threshold) { // it's bright
            *(((cds_arg*)arg)->led_state) = 0;
            led_onoff(0); 
            syslog(LOG_INFO, "[%s] LED OFF", __func__);
        } else { // it's dark
            *(((cds_arg*)arg)->led_state) = 1;
            led_onoff(1);
            syslog(LOG_INFO, "[%s] LED ON", __func__);
        }
        sem_V(((cds_arg*)arg)->semid_led); // unlock

        dlclose(led_handle);
    } else {
        syslog(LOG_ERR, "[%s] cannot found function name", __func__);
        dlclose(cds_handle);
        return NULL;
    }

    dlclose(cds_handle);
}

void* segment_thread(void* arg)
{
    int count = *(int*)arg;
    
    void* segment_handle = dlopen("libsegment.so", RTLD_LAZY);
    if(!segment_handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }

    void* buzzer_handle = dlopen("libbuzzer.so", RTLD_LAZY);
    if(!buzzer_handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }

    typedef void (*segment_control_t)(int);
    typedef void (*myalarm_t)(int);
    
    segment_control_t segment_control = (segment_control_t)dlsym(segment_handle, "segment_control");
    myalarm_t myalarm = (myalarm_t)dlsym(buzzer_handle, "myalarm");

    char* error = dlerror();
    if(error) {
        syslog(LOG_ERR, "[%s] %s\n", __func__, dlerror());
        dlclose(segment_handle);
    }

    syslog(LOG_INFO, "segment_control : %d", *(int*)arg);
    for (count; count >= 0; count--) {
        segment_control(count); // turn on segment display and delay 1 sec
        if (count == 0) {
            break;
        }
        delay(1000);
    }
    myalarm(1000); // play sound
    segment_control(-1); // turn off segment display
    
    syslog(LOG_INFO, "segment_control end");
    dlclose(segment_handle);
    dlclose(buzzer_handle);
}