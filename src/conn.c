#include <stdlib.h>  
#include <unistd.h>  
#include <pthread.h>  
#include <errno.h>  
#include <string.h>
#include "conn.h"
#include "common.h"
#include "f_epoll.h"

#define IS_ONESHOT(ptr) (((task_t*)(ptr))->events & EPOLLONESHOT)
#define SET_ONESHOT(ptr) (((task_t*)(ptr))->events |= (EPOLLONESHOT))

void DispatchConn(int connfd, int * epfds)
{
    task_t* task = (task_t*)malloc(sizeof(task_t));
    task->fd = connfd;
    task->n = 0;
    task->events = EPOLLIN|EPOLLET;
    if (global_ini.nthreads_per_epoll > 1)
        SET_ONESHOT(task);

    int epfd = epfds[connfd%global_ini.nepolls_per_worker];
    task->epfd = epfd;
    f_epoll_add(epfd, connfd, task->events, task);
}

int ReadConn(void * ptr)
{
    task_t* task = (task_t*)ptr;
    int sockfd = task->fd;

    if ( (task->n = read(sockfd, task->buffer, 100)) < 0) {  
        if (errno == ECONNRESET) {  
            CloseConn(ptr);
            return 0;
        } else {
            zlog_error(lg, "[%u]read error, fd = %d, error = %s",
                (unsigned)pthread_self(), sockfd,
                strerror(errno));

            return -1;
        }
    } else if (task->n == 0) {
        CloseConn(ptr);
        return 0;
    }
    else {
        task->buffer[task->n] = '\0';
        zlog_debug(lg, "[%u]read success (%s), fd = %d",
            (unsigned)pthread_self(), task->buffer, sockfd);

    }

    task->events |= EPOLLOUT;
    if (IS_ONESHOT(task)) //需要再次注册
        f_epoll_add(task->epfd, sockfd, task->events, ptr);
    else
        f_epoll_mod(task->epfd, sockfd, task->events, ptr);

    return task->n;
}

int WriteConn(void * ptr)
{
    task_t* task = (task_t*)ptr;
    int sockfd = task->fd;

    if (task->n > 0) {
        zlog_debug(lg, "[%u]write (%s), fd = %d",
            (unsigned)pthread_self(), task->buffer, sockfd);

        write(sockfd, task->buffer, task->n);  
        task->n = 0;
    }

    if (IS_ONESHOT(task)) //需要再次注册
        f_epoll_add(task->epfd, sockfd, task->events, ptr);

    return task->n;
}

void CloseConn(void* ptr)
{
    if (!ptr) return;

    task_t* task = (task_t*)ptr;

    zlog_debug(lg, "[%u]close, fd = %d, error = %s",
        (unsigned)pthread_self(), task->fd,
        strerror(errno));

    /**
     * the close of an fd cause it to be removed
     * from all epoll sets automatically
     * */
    close(task->fd);

    free(task);
}



