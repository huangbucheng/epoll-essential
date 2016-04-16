#ifndef _NET_H_
#define _NET_H_
#include <sys/socket.h>  

int setnonblocking(int sock);
int tcplisten(int port, int backlog = 5);
void tcpstart(int listenfd, int epfd);
int tcpaccept(int listenfd, int epfd, int * epfds);

#endif
