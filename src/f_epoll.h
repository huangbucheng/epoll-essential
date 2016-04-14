#ifndef _F_EPOLL_H_
#define _F_EPOLL_H_
#include <sys/epoll.h>

///epoll
int f_epoll_add(int epfd, int fd, int events, void* ptr);
int f_epoll_mod(int epfd, int fd, int events, void* ptr);
int f_epoll_del(int epfd, int fd);

#endif
