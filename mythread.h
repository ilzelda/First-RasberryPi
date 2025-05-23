// for led
typedef struct function_selector {
    char name[20];
    int semid_led;
    int* led_state;
    int brightness;
} function_selector;

typedef struct music_arg {
    int semid_music;
} music_arg;

typedef struct cds_arg {
    char name[20];
    int client_sd;
    int semid_led;
    int* led_state;
} cds_arg;

void* led_thread(void* arg);
void* buzzer_thread(void* arg);
void* cds_thread(void* arg);
void* segment_thread(void* arg);

void sem_P(int semid);
int sem_P_nonblock(int semid);
void sem_V(int semid);
