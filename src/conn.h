#ifndef _CONN_H_
#define _CONN_H_

#define BUF_SZ 10
typedef struct task_t  
{  
    int fd;
    int epfd;
    int events;
    char buffer[BUF_SZ];
    char out[BUF_SZ];
    int out_sz;
}task_t;

void DispatchConn(int connfd, int * epfds);
int ReadConn(void * ptr);
int WriteConn(void * ptr);
void CloseConn(void * ptr);
void RegConn(void * ptr);



#endif
