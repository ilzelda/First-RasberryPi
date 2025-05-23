#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

// For Daemon
#include <syslog.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

//For Threading
#define _GNU_SOURCE  // for pthread_tryjoin_np
#include <pthread.h>

//For MultiProcess
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

//For Dynamic loading
#include <dlfcn.h>         

// For Socket
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

// For Raspberry Pi
#include <wiringPi.h>
#include <softTone.h>
#include <wiringPiI2C.h>

// custom header
#include "daemon.h"
#include "mythread.h"
#include "webserver.h"

#define MAXDATASIZE 1024
#define MAXCLIENT 10
#define RPI_IP "192.168.0.30"
#define RPI_PORT 60000

int rpi_control(int cli_sfd, int semid, int semid2, int cmd);
void sigchld_handler(int signo);
void sem_P(int semid);
void sem_V(int semid);

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int* led_state;

int main(int argc, char **argv)
{
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // 시스템 호출 자동 재시작
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // socket
    int server_sockfd, client_sockfd;
    int state, client_len;
    struct sockaddr_in clientaddr, serveraddr, myaddr;
    char response[MAXDATASIZE];

    // fork
    pid_t pid;

    // user input
    int cmd;
    const char menu[] = "\n===============================\n" \
                        "   [1] : led (on/off)\n"  \
                        "   [2] : led (brightness)\n" \
                        "   [3] : music play\n" \
                        "   [4] : CDS sensor value check\n" \
                        "   [5] : led control by CDS sensor\n" \
                        "   [6] : segment display\n" \
                        "   [7] : web server\n" \
                        "[exit] : disconnect from server\n" \
                        "===============================\n" \
                        "input command >";

    if(argc < 2) {
        printf("Usage : %s {log name}\n", argv[0]);
        return -1;
    }

    if( daemonize(argc, argv) < 0) {
        perror("Daemonize failed : ");
        return -1;
    }
    syslog(LOG_INFO, "[%s]\n=========================\nDaemonize success", __func__);


    // shared memory and semaphore
    key_t key = ftok("/tmp", 'S');  // ftok로 키 생성
    if (key == -1) { syslog(LOG_ERR, "[%s] ftok 실패", __func__); exit(1); }

    // 1) 공유메모리 생성 (크기: sizeof(int))
    int shmid = shmget(key, sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) { syslog(LOG_ERR, "[%s] shmget 실패", __func__); exit(1); }

    // 2) 공유메모리 붙이기
    led_state = shmat(shmid, NULL, 0);
    if (led_state == (void*)-1) { syslog(LOG_ERR, "[%s] shmat 실패", __func__); exit(1); }
    *led_state = 0;  // 초기화

    // 3) 세마포어 생성 (1개)
    int semid_led_sate = semget(key, 1, IPC_CREAT | 0666);
    if (semid_led_sate == -1) { syslog(LOG_ERR, "[%s] semget 실패", __func__); exit(1); }

    int semid_music_busy = semget(key, 1, IPC_CREAT | 0666);
    if (semid_music_busy == -1) { syslog(LOG_ERR, "[%s] semget 실패", __func__); exit(1); }

    // 4) 세마포어 초기값 1로 설정 (mutex)
    union semun arg_led, arg_music;
    arg_led.val = 1;
    if (semctl(semid_led_sate, 0, SETVAL, arg_led) == -1) 
    { syslog(LOG_ERR, "[%s] semctl SETVAL 실패", __func__); exit(1); }

    arg_music.val = 1;
    if (semctl(semid_music_busy, 0, SETVAL, arg_music) == -1) 
    { syslog(LOG_ERR, "[%s] semctl SETVAL 실패", __func__); exit(1); }

    syslog(LOG_INFO, "[%s] shared memory and semaphore setup success", __func__);

    // server setup
    syslog(LOG_INFO, "[%s] socket setup start", __func__);

    // internet 기반의 스트림 소켓을 만들도록 한다.
    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        syslog(LOG_ERR, "[%s] socket error : %s", __func__, strerror(errno));
        exit(0);
    }

    // set SO_REUSEADDR to allow quick restart
    int optval = 1;
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(RPI_IP);
    serveraddr.sin_port = htons(RPI_PORT);

    state = bind(server_sockfd , (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (state == -1)
    {
        syslog(LOG_ERR, "[%s] bind error : %s", __func__, strerror(errno));
        exit(0);
    }

    state = listen(server_sockfd, MAXCLIENT);
    if (state == -1)
    {
        syslog(LOG_ERR, "[%s] listen error : %s", __func__, strerror(errno));
        exit(0);
    }

    syslog(LOG_INFO, "Server setup success : %s:%d", RPI_IP, RPI_PORT);
    syslog(LOG_INFO, "waiting for client connection...");

    while(1)
    {
        memset(&clientaddr, 0, sizeof(clientaddr));
        client_len = sizeof(clientaddr);
        client_sockfd = accept(server_sockfd, (struct sockaddr *)&clientaddr, &client_len);
        if (client_sockfd == -1)
        {
            syslog(LOG_ERR, "[%s] accept error : %s", __func__, strerror(errno));
            exit(0);
        }
        syslog(LOG_INFO, "[%s] client connected : %s", __func__, inet_ntoa(clientaddr.sin_addr));
        syslog(LOG_INFO, "[%s] client port : %d", __func__, ntohs(clientaddr.sin_port));

        pid = fork();
        switch(pid)
        {
            case -1:
                syslog(LOG_ERR, "[%s] fork error : %s", __func__, strerror(errno));
                exit(0);
            case 0: // child process
            {
                syslog(LOG_INFO, "[%s] rpi control start(pid : %d)", __func__, getpid());

                while(1) 
                {
                    if(send(client_sockfd, menu, strlen(menu), 0) == -1)
                    {
                        syslog(LOG_ERR, "[%s] send error(pid : %d) : %s", __func__, getpid(), strerror(errno));
                        exit(0);
                    }

                    int n = recv(client_sockfd, &response, sizeof(response), 0);
                    if(n < 0)
                    {
                        syslog(LOG_ERR, "[%s] recv error(pid : %d)", __func__, getpid());
                        break;
                    }
                    else if(n == 0)
                    {
                        syslog(LOG_INFO, "[%s] client disconnected(pid : %d)", __func__, getpid());
                        break;
                    }

                    if (response[n-1] == '\n')
                        response[n-1] = '\0';

                    syslog(LOG_INFO, "[%s] client response(pid : %d) : [%s]", __func__, getpid(), response);

                    if(strcmp(response, "exit") == 0)
                    {
                        syslog(LOG_INFO, "[%s] client send exit(pid : %d)", __func__, getpid());
                        break;
                    }
                    
                    cmd = atoi(response);
                    syslog(LOG_INFO, "[%s] client command(pid : %d) : %d", __func__ , getpid(), cmd);
                    
                    if(cmd < 1 || cmd > 7)
                    {
                        syslog(LOG_ERR, "[%s] received invalid command(pid : %d) : %d", __func__, getpid(), cmd);
                        
                        char prompt[] = "[SERVER ALERT] invalid command\n";
                        send(client_sockfd, prompt, strlen(prompt), 0);
                        continue;
                    }
                    
                    rpi_control(client_sockfd, semid_led_sate, semid_music_busy, cmd);
                }

                syslog(LOG_INFO, "[%s] client close(pid : %d) [%s]", __func__, getpid(), inet_ntoa(clientaddr.sin_addr));
                close(client_sockfd);
                exit(0); // child process exit
                break;
            }
            default: // parent process
                continue;
        }
    }

    syslog(LOG_INFO, "[%s] Server shutdown", __func__);
    shmdt(led_state); // detach shared memory
    shmctl(shmid, IPC_RMID, NULL); // remove shared memory
    semctl(semid_led_sate, 0, IPC_RMID); // remove semaphore
    close(server_sockfd);
    closelog(); // close syslog

    return 0;
}

void sigchld_handler(int signo) {
    pid_t pid;
    int status;
    // 종료된 모든 자식 프로세스를 non-blocking으로 수거
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            syslog(LOG_INFO,"Child %d exited with status %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            syslog(LOG_INFO,"Child %d killed by signal %d\n", pid, WTERMSIG(status));
        }
        // 필요에 따라 추가 후처리 코드
    }
    // pid == 0 이면 수거할 자식 없음, <0 이면 ECHILD 등
}

int rpi_control(int cli_sfd, int semid_led, int semid_music, int cmd)
{
    pthread_t thread_id, cds_thread_id;
    pthread_attr_t attr, cds_attr;

    pthread_attr_init(&attr);
    pthread_attr_init(&cds_attr);
	
	if(wiringPiSetupGpio() == -1)
	{
		return -1;
	}

    if(cmd <= 2) // led
    {
        function_selector func_selector;
        func_selector.semid_led = semid_led;
        func_selector.led_state = led_state;

        if (cmd == 1) // led on/off
        {   
            func_selector.brightness = -1; // not used
            strcpy(func_selector.name, "onoff");
        }
        else if(cmd == 2) // led brightness
        {
            int brightness;
            char prompt[] = "input brightness(0~100) : ";
            char buf[10];
            int n;
            syslog(LOG_INFO, "[%s] sending prompt for brightness(pid : %d)", __func__, getpid());
            send(cli_sfd, prompt, strlen(prompt), 0);
            n = recv(cli_sfd, buf, sizeof(buf)-1, 0);
            if(n < 0)
            {
                syslog(LOG_ERR, "[%s] recv error(pid : %d)", __func__, getpid());
                return -1;
            }

            if (buf[n-1] == '\n')
                buf[n-1] = '\0';
            syslog(LOG_INFO, "[%s] received brightness(pid : %d) : %s", __func__, getpid(), buf);

            brightness = atoi(buf);
        
            strcpy(func_selector.name, "brightness");
            func_selector.brightness = brightness;
        }
        
        pthread_create(&thread_id, &attr, led_thread, &func_selector);
        pthread_join(thread_id, NULL);
        pthread_attr_destroy(&attr);
    }
    else if (cmd == 3) // music play
    {
        music_arg *msg = malloc(sizeof(*msg));
        msg->semid_music = semid_music;
        pthread_create(&thread_id, &attr, buzzer_thread, msg);

        // 프롬프트
        const char prompt[] = "s : stop music\n> ";
        send(cli_sfd, prompt, strlen(prompt), 0);

        struct pollfd pfd = { .fd = cli_sfd, .events = POLLIN };
        char buf[MAXDATASIZE];

        while (1)
        {
            // 100ms 타임아웃
            int ret = poll(&pfd, 1, 100);
            if (ret < 0)
            {
                syslog(LOG_ERR, "[%s] poll error(pid : %d): %s", __func__, getpid(), strerror(errno));
                break;
            }

            // 1) 클라이언트가 ‘s’를 보냈을 때
            if (ret > 0 && (pfd.revents & POLLIN))
            {
                int n = recv(cli_sfd, buf, sizeof(buf), 0);
                if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

                syslog(LOG_INFO, "[%s] received(pid : %d): %s", __func__, getpid(), buf);
                if (strcmp(buf, "s") == 0)
                {
                    syslog(LOG_INFO, "[%s] stopping music(pid : %d)", __func__, getpid());
                    pthread_cancel(thread_id);
                    syslog(LOG_INFO, "[%s] music thread cancelled(pid : %d)", __func__, getpid());
                    break;
                    // 아래에서 join 처리
                }
            }

            // 2) 스레드가 자연 종료되었는지 비차단식으로 확인
            {
                int state;
                int jret = pthread_tryjoin_np(thread_id, &state);
                if (jret == 0)
                {
                    syslog(LOG_INFO, "[%s] music thread finished(pid : %d)", __func__, getpid());
                    if(state == 0)
                        syslog(LOG_INFO, "[%s] music thread finished normally(pid : %d)", __func__, getpid());
                    else
                    {
                        syslog(LOG_INFO, "[%s] music thread finished with semaphore fail(pid : %d)", __func__, getpid());
                        char prompt[] = "[SERVER ALERT] music is playing... try later\n";
                        send(cli_sfd, prompt, strlen(prompt), 0);
                    }
                    goto done;    // 둘 다 빠져나가서 자원 해제
                }
                else if (jret != EBUSY)
                {
                    // ESRCH, EINVAL 등 에러
                    syslog(LOG_ERR, "[%s] pthread_tryjoin_np error(pid : %d): %d", __func__, getpid(), jret);
                    goto done;
                }
                // EBUSY면 스레드 아직 실행 중 → 계속 루프
            }
        }

        // ‘s’ 눌러서 나왔다면 여기로 와서 join
        {
            int jret = pthread_join(thread_id, NULL);
            if (jret == 0)
                syslog(LOG_INFO, "[%s] music thread joined after cancel(pid : %d)", __func__, getpid());
            else
                syslog(LOG_ERR, "[%s] pthread_join error(pid : %d): %d", __func__, getpid(), jret);
        }

    done:
        free(msg);
        pthread_attr_destroy(&attr);
    }
    else if(cmd <= 5) // cds senseor
    {
        cds_arg _cds_arg;
        _cds_arg.client_sd = cli_sfd;
        _cds_arg.semid_led = semid_led;
        _cds_arg.led_state = led_state;

        if (cmd == 4) // cds sensor
        {
            strcpy(_cds_arg.name, "val_check");
        }
        else if(cmd == 5) // led control by cds sensor
        {
            strcpy(_cds_arg.name, "led_control");
        }
 
        pthread_create(&thread_id, &attr, cds_thread, &_cds_arg);
        pthread_join(thread_id, NULL);
        pthread_attr_destroy(&attr);
    }
    else if(cmd == 6) // segment display
    {
        char prompt[] = "input num(0~9) : ";
        char buf[10];
        int n, segment_num;
        while(1)
        {
            syslog(LOG_INFO, "[%s] sending prompt for segment input(pid : %d)", __func__, getpid());
            send(cli_sfd, prompt, strlen(prompt), 0);
            n = recv(cli_sfd, buf, sizeof(buf)-1, 0);
            if(n < 0)
            {
                syslog(LOG_ERR, "[%s] recv error(pid : %d)", __func__, getpid());
                return -1;
            }

            if (buf[n-1] == '\n')
                buf[n-1] = '\0';

            segment_num = atoi(buf);
            if(segment_num >= 0 && segment_num <= 9)
                break;
        }

        pthread_create(&thread_id, &attr, segment_thread, &segment_num);
        pthread_join(thread_id, NULL);
        pthread_attr_destroy(&attr);
    }
    else if(cmd == 7)
    {
        pthread_t http_tid;
        pthread_create(&http_tid, NULL, http_server, NULL);
        pthread_detach(http_tid);
        syslog(LOG_INFO, "[%s] HTTP server thread created(pid : %d)", __func__, getpid());
    
        char prompt[256];  
        int n = snprintf(prompt, sizeof(prompt), "[URL] http://%s:%d", RPI_IP, 8080);
        if (n < 0 || n >= sizeof(prompt)) 
        {
            return 1;
        }
        send(cli_sfd, prompt, strlen(prompt), 0);
    }
    else
    {
        syslog(LOG_ERR, "[%s] received invalid command(pid : %d)", __func__, getpid());
    }
	
	return 0;
}