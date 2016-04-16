#include "f_epoll.h"
#include "common.h"
#include <pthread.h>

int f_epoll_add(int epfd, int fd, int events, void* ptr)
{
    /**
     * typedef union epoll_data {  
     *     void *ptr;  
     *     int fd;  
     *     __uint32_t u32;  
     *     __uint64_t u64;  
     * } epoll_data_t;  
     *   
     * struct epoll_event {  
     *     __uint32_t events; #Epoll events
     *     epoll_data_t data; #User data variable
     * };  

     * events可以是以下几个宏的集合：
     * EPOLLIN：       触发该事件，表示对应的文件描述符上有可读数据。(包括对端SOCKET正常关闭)；
     * EPOLLOUT：      触发该事件，表示对应的文件描述符上可以写数据；
     * EPOLLPRI：      表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
     * EPOLLERR：      表示对应的文件描述符发生错误；
     * EPOLLHUP：      表示对应的文件描述符被挂断；
     * EPOLLET：       将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
     * EPOLLONESHOT：  只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里。
     */

    struct epoll_event ev;
    /**
     * ptr 和 fd只能设置一个，epoll_event是union类型
     */
    if (ptr) ev.data.ptr = ptr;
    else ev.data.fd = fd;
    ev.events = events;

    zlog_debug(lg, "[%u] epoll_ctl_add, epfd = %d, fd = %d, events = %u",
            (unsigned)pthread_self(), epfd, fd, events);
    //注册epoll事件
    if (-1 ==epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&ev))
        return -1;

    return 0;
}

int f_epoll_mod(int epfd, int fd, int events, void* ptr)
{
    struct epoll_event ev;
    if (ptr) ev.data.ptr = ptr;
    else ev.data.fd = fd;
    ev.events = events;

    zlog_debug(lg, "[%u] epoll_ctl_mod, epfd = %d, fd = %d, events = %u",
            (unsigned)pthread_self(), epfd, fd, events);

    if (-1 ==epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&ev))
        return -1;

    return 0;
}

int f_epoll_del(int epfd, int fd)
{
    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,0);
    return 0;
}
