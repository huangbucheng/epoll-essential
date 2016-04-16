#include <sys/types.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>
#include <unistd.h>  
#include <fcntl.h>  
#include <pthread.h>  
#include <stdlib.h>  
#include <string.h>
#include <errno.h>  
#include "common.h"
#include "f_epoll.h"
#include "conn.h"

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
    bind(listenfd,(sockaddr *)&serveraddr, sizeof(serveraddr));  

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

    listen(listenfd, backlog);
    return listenfd;
}

void tcpstart(int listenfd, int epfd)
{
    int events = EPOLLIN|EPOLLET;
    if (global_ini.nthreads_per_epoll > 1)
        events |= EPOLLONESHOT;

    /**
     * listen fd, 不能设置event.data.ptr
     * ptr 和 fd只能设置一个，epoll_event是union类型
     */
    f_epoll_add(epfd, listenfd, events, 0);
}

int tcpaccept(int listenfd, int epfd, int * epfds)
{
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct sockaddr_in remote;  

    int conn_fd;
    while ((conn_fd = accept(listenfd,(struct sockaddr *) &remote, &addrlen)) > 0) {
        setnonblocking(conn_fd);
        DispatchConn(conn_fd, epfds);
        zlog_info(lg, "[%u] new connection from %s:%d, fd = %d",
                (unsigned)pthread_self(),
                inet_ntoa(remote.sin_addr), 
                ntohs(remote.sin_port),
                conn_fd);
    }
    if (conn_fd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
        zlog_error(lg, "[%u] accept fail, listenfd = %d, error = %s",
                (unsigned)pthread_self(), listenfd, strerror(errno));
    }

    if (global_ini.nthreads_per_epoll > 1) {
        int events = EPOLLIN|EPOLLET|EPOLLONESHOT;
        f_epoll_add(epfd, listenfd, events, 0);
    }

    return conn_fd;
}

