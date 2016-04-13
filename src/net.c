#include <sys/types.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>
#include <unistd.h>  
#include <fcntl.h>  
#include <stdio.h>  
#include <stdlib.h>  
#include <string.h>
#include <errno.h>  
#include "conf.h"
#include "f_epoll.h"

int setnonblocking(int sock)  
{  
    int opts;  
    opts = fcntl(sock,F_GETFL);  
    if (opts < 0) {
        perror("fcntl(sock,GETFL)");  
        return -1;  
    }

    opts |= O_NONBLOCK;  
    if (fcntl(sock,F_SETFL,opts) < 0) {
        perror("fcntl(sock,SETFL,opts)");  
        return -1;  
    }
    return 0;
}

int tcplisten(int port, int backlog)
{
    struct sockaddr_in serveraddr;  
    bzero(&serveraddr, sizeof(serveraddr));  
    serveraddr.sin_family = AF_INET;  
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); //inet_addr("0.0.0.0");
    serveraddr.sin_port=htons(port);  

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);  
    if (listenfd <= 0) {
        perror("socket");  
        return -1;
    }

    // 地址重用  
    int nOptVal = 1;  
    socklen_t nOptLen = sizeof(int);  
    if (-1 == ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &nOptVal, nOptLen))  
    {
        perror("setsockopt");  
        close(listenfd);
        return -1;
    }

    //把socket设置为非阻塞方式  
    if (setnonblocking(listenfd) < 0) {
        close(listenfd);
        return -1;
    }

    bind(listenfd,(sockaddr *)&serveraddr, sizeof(serveraddr));  
    listen(listenfd, backlog);
    return listenfd;
}

typedef struct tcpserv_
{
    int fd;
    int epfd;
    int events;
} tcpserv;

void tcpstart(int listenfd, int epfd)
{
    tcpserv* srv = (tcpserv*)malloc(sizeof(tcpserv));
    srv->fd = listenfd;
    srv->epfd = epfd;
    srv->events = EPOLLIN;//|EPOLLET;
    if (global_ini.nthreads_per_epoll > 1)
        srv->events |= EPOLLONESHOT;

    f_epoll_add(epfd, listenfd, srv->events, srv);
}

int tcpaccept(int listenfd, void * ptr, struct sockaddr* clientaddr, socklen_t* clilen)
{
    int connfd = accept(listenfd, clientaddr, clilen);
    if (connfd > 0) {
        setnonblocking(connfd);
    }

    if (((tcpserv*)ptr)->events & EPOLLONESHOT)
        f_epoll_add(((tcpserv*)ptr)->epfd, listenfd,
                ((tcpserv*)ptr)->events, ptr);

    return connfd;
}

