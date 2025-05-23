#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

#define MAXDATASIZE 1024

void chatting(int sd);

int main(int argc, char *argv[])
{

    int sockfd, numbytes;
    socklen_t addr_len;
    struct hostent *he;
    struct sockaddr_in server_addr;

    sigset_t mask;
    sigfillset(&mask);                   
    sigdelset(&mask, SIGINT);           
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    if(argc != 2) {
        fprintf(stderr, "usage : client hostname \n");
        exit(1);
    }
    if((he = gethostbyname(argv[1])) == NULL) {
        perror("gethostbyname");
        exit(1);
    }
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(60000);
    server_addr.sin_addr = *((struct in_addr *)he->h_addr);
    printf("[ %s ]\n",(char*) inet_ntoa(server_addr.sin_addr));
    memset(&(server_addr.sin_zero), '\0',8);
    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr))== -1) {
        perror("connect");
        exit(1);
    }

    chatting(sockfd);

    close(sockfd);
    return 0;
}

void chatting(int sd)
{
    struct pollfd fds[2];
    fds[0].fd = 0; // stdin
    fds[0].events = POLLIN;
    fds[1].fd = sd; // socket
    fds[1].events = POLLIN;
    
    char buf[MAXDATASIZE];

    while(1)
    {
        int ret = poll(fds, 2, -1);
        if (ret < 0)
        {
            perror("poll error");
            exit(1);
        }
        
        if (fds[0].revents & POLLIN)
        {
            int n = read(0, buf, sizeof(buf));
            if (n > 0) {            
                if (buf[n-1] == '\n')
                    buf[n-1] = '\0';
                
                printf("<CLIENT> send : [%s]\n", buf);
                send(sd, buf, n, 0);

                if(strcmp(buf, "exit") == 0) 
                    break;
            }
        }
        else if (fds[1].revents & POLLIN)
        {
            int n = recv(sd, buf, sizeof(buf), 0);
            if (n > 0)
            {
                buf[n] = '\0';
                printf("%s", buf);
                fflush(stdout);
            }
        }
    }
}