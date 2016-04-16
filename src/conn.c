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
    task->out_sz = 0;
    task->events = EPOLLIN|EPOLLET;
    if (global_ini.nthreads_per_epoll > 1)
        SET_ONESHOT(task);

    int epfd = epfds[connfd%global_ini.nepolls_per_worker];
    task->epfd = epfd;
    f_epoll_add(epfd, connfd, task->events, task);
}

int ReadConn(void * ptr)
{
    if (!ptr) return -1;

    task_t* task = (task_t*)ptr;
    int sockfd = task->fd;

    int offset = 0, nread = 0;
    while ((nread = read(sockfd, task->buffer + offset, BUF_SZ-offset)) > 0) {
        offset += nread;
    }
    if (nread == -1 && errno != EAGAIN) {
        if (errno == ECONNRESET) {  
            CloseConn(ptr);
            return 0;
        } else {
            //丢弃已读数据
            zlog_error(lg, "[%u] read error, fd = %d, error = %s",
                    (unsigned)pthread_self(), sockfd,
                    strerror(errno));

            return -1;
        }
    }
    else if (offset > 0) {
        memcpy(task->out, task->buffer, offset);
        task->out_sz = offset;
        zlog_debug(lg, "[%u] read success (%d), fd = %d",
                (unsigned)pthread_self(), offset, sockfd);
    }
    else if (offset == 0) {
        CloseConn(ptr);
        return 0;
    }

    task->events |= EPOLLOUT;

    return offset;
}

int WriteConn(void * ptr)
{
    if (!ptr) return -1;

    task_t* task = (task_t*)ptr;
    int sockfd = task->fd;

    int offset = 0;
    while (task->out_sz > offset) {
        int nwrite = write(sockfd, task->out + offset,
                task->out_sz - offset);
        if (nwrite < task->out_sz - offset) {
            if (nwrite == -1 && errno != EAGAIN) {
                zlog_error(lg, "[%u] write fail, fd = %d, error = %s",
                        (unsigned)pthread_self(), sockfd,
                        strerror(errno));
                CloseConn(ptr);
                return 0;
            }
            break;
        }
        offset += nwrite;
    }

    if (offset > 0) {
        zlog_debug(lg, "[%u] write success (%d), fd = %d",
                (unsigned)pthread_self(), offset, sockfd);
    }
    task->out_sz = 0;

    return offset;
}

void CloseConn(void* ptr)
{
    if (!ptr) return;

    task_t* task = (task_t*)ptr;

    zlog_debug(lg, "[%u] close, fd = %d, error = %s",
            (unsigned)pthread_self(), task->fd,
            strerror(errno));

    /**
     * the close of an fd cause it to be removed
     * from all epoll sets automatically
     * */
    close(task->fd);

    free(task);
}

void RegConn(void * ptr)
{
    if (!ptr) return;
    task_t* task = (task_t*)ptr;
    int sockfd = task->fd;

    if (IS_ONESHOT(task)) //需要再次注册
        f_epoll_add(task->epfd, sockfd, task->events, ptr);
    else
        f_epoll_mod(task->epfd, sockfd, task->events, ptr);
}

