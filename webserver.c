#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <syslog.h>
#include <dlfcn.h>
#include <pthread.h>

#include <wiringPi.h>
#include <softTone.h>
#include <wiringPiI2C.h>

#include "webserver.h"

#define HTTP_PORT 8080
#define WEB_ROOT "/home/veda/project/"

void *http_server(void *arg)
{
    int ssock;
    pthread_t thread;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;

    ssock = socket(AF_INET, SOCK_STREAM, 0);
    if (ssock == -1) { perror("socket()"); pthread_exit((void*)(intptr_t)-1); }

    int optval = 1;
    setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(HTTP_PORT);
    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind()"); pthread_exit((void*)(intptr_t)-1);
    }
    if (listen(ssock, 10) == -1) {
        perror("listen()"); pthread_exit((void*)(intptr_t)-1);
    }

    while (1) {
        int csock;
        char addrbuf[INET_ADDRSTRLEN];

        len   = sizeof(cliaddr);
        csock = accept(ssock, (struct sockaddr*)&cliaddr, &len);
        inet_ntop(AF_INET, &cliaddr.sin_addr, addrbuf, sizeof(addrbuf));
        // printf("Client IP : %s:%d\n", addrbuf, ntohs(cliaddr.sin_port));

        /* 스레드 분리(detach) */
        pthread_create(&thread, NULL, clnt_connection, &csock);
        pthread_detach(thread);
    }

    pthread_exit((void*)(intptr_t)-1);    
}

const char* get_mime(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if      (ext && strcmp(ext, ".html") == 0) return "text/html";
    else if (ext && strcmp(ext, ".js"  ) == 0) return "application/javascript";
    else if (ext && strcmp(ext, ".css" ) == 0) return "text/css";
    return "application/octet-stream";
}

void *clnt_connection(void *arg)
{
    int csock = *((int*)arg);
    FILE *clnt_read = fdopen(csock, "r");
    FILE *clnt_write = fdopen(dup(csock), "w");
    char line[BUFSIZ], method[16], path[BUFSIZ];
    
    /* load CDS_sensor() */
    int cds_fd;
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
        syslog(LOG_ERR, "[%s] wiringPiI2CSetupInterface failed:\n", __func__);
    }
    syslog(LOG_INFO, "[%s] ADC/DAC(YL-40) Module testing........\n", __func__);
    
    /* load puzzleJingle */
    void* buzzer_handle = dlopen("libbuzzer.so", RTLD_LAZY);
    if(!buzzer_handle) {
        syslog(LOG_ERR, "%s\n", dlerror());
        return NULL;
    }
    typedef void (*puzzleJingle_t)(void*);
    puzzleJingle_t puzzleJingle = (puzzleJingle_t)dlsym(buzzer_handle, "puzzleJingle");
    char* error2 = dlerror();
    if(error2) {
        syslog(LOG_ERR, "%s\n", dlerror());
        dlclose(buzzer_handle);
    }

    /* 요청 라인 파싱 */
    if (!fgets(line, BUFSIZ, clnt_read)) goto END;
    sscanf(line, "%15s %1023s", method, path);
    if (strcmp(method, "GET") != 0) {
        printf("Unsupported method: %s\n", method);
        sendError(clnt_write);
        goto END;
    }

    /*--- /sensor 엔드포인트 처리 ---*/
    if (strcmp(path, "/sensor") == 0) {
        int cds_value = CDS_sensor(cds_fd);

        fprintf(clnt_write,
            "HTTP/1.1 200 OK\r\n"
            "Server: SimpleC/1.0\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%d",
            (size_t)(snprintf(NULL, 0, "%d", cds_value)), cds_value
        );
        fflush(clnt_write);
        goto END;
    }

    /*--- /buzzer 엔드포인트 처리 ---*/
    if (strcmp(path, "/buzzer") == 0) {
        puzzleJingle(NULL);
        fprintf(clnt_write,
            "HTTP/1.1 200 OK\r\n"
            "Server: SimpleC/1.0\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "Buzzer played",
            (size_t)(snprintf(NULL, 0, "Buzzer played"))
        );
        fflush(clnt_write);
        goto END;
    }

    /* 루트 경로는 index.html */
    char *filename = path + 1;  // “/foo.html” -> “foo.html”
    if (filename[0] == '\0') filename = "index.html";

    /* 헤더 무시 */
    while (fgets(line, BUFSIZ, clnt_read)) {
        if (strcmp(line, "\r\n") == 0) break;
    }

    /* 실제 파일 전송 */
    const char *mime = get_mime(filename);
    sendData(clnt_write, mime, filename);

END:
    fclose(clnt_read);
    fclose(clnt_write);
    return NULL;
}

int sendData(FILE* fp, const char *ct, const char *filename)
{
    char filepath[BUFSIZ];
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT, filename);

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        sendError(fp);
        return -1;
    }

    /* 응답 헤더 */
    fprintf(fp,
        "HTTP/1.1 200 OK\r\n"
        "Server: SimpleC/1.0\r\n"
        "Content-Type: %s\r\n"
        "\r\n",
        ct
    );
    fflush(fp);

    /* 파일 바디 */
    char buf[BUFSIZ];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, fp);
    }
    close(fd);
    return 0;
}

void sendError(FILE* fp)
{
    const char *msg =
        "<html><head><title>400 Bad Request</title></head>"
        "<body><h1>400 Bad Request</h1></body></html>";
    fprintf(fp,
        "HTTP/1.1 400 Bad Request\r\n"
        "Server: SimpleC/1.0\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(msg), msg
    );
    fflush(fp);
}