#ifndef _CONN_H_
#define _CONN_H_

typedef struct task_t  
{  
    int fd;
    int epfd;
    int events;
    char buffer[100];
    int n;
}task_t;

void DispatchConn(int connfd, int * epfds);
int ReadConn(void * ptr);
int WriteConn(void * ptr);
void CloseConn(void* ptr);



#endif
