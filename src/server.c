#include <sys/socket.h>  
#include <sys/epoll.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  
#include <fcntl.h>  
#include <unistd.h>  
#include <stdio.h>  
#include <errno.h>  
#include <stdlib.h>  
#include <sys/types.h>  
#include <sys/wait.h>  
#include <string.h>
#include <pthread.h>
#include "iniparser.h"

using namespace std;  

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

/**
 * wait与waitpid的主要区别：
 * 1. waitpid能够等指定pid的子进程；而wait只能等第一个到达的子进程。
 * 2. waitpid通过指定WNOHANG可以不阻塞，没有可用的子进程就立即返回0;
 *    而wait要求父进程一直阻塞。
 */
void sig_chld(int signo)
{
    pid_t   pid;
    int     stat;

    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0)
        printf("child %d terminated with status %d\n", pid, stat);

    return;
}

int listenfd;
int tcplisten(int port, int backlog = 5)
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

struct ini_s {
    short listen_port;
    int backlog;

    int nworkers;
    int nepolls_per_worker;
    int nconn_per_epoll;
    int nthreads_per_epoll;
};

struct ini_s global_ini;

int loadconfig(const char* ini_name)
{
    dictionary * ini ;

    ini = iniparser_load(ini_name);
    if (ini == NULL) {
        fprintf(stderr, "cannot parse file: %s\n", ini_name);
        return -1 ;
    }
    iniparser_dump(ini, stdout);
    global_ini.listen_port = iniparser_getint(ini, "listen:port", -1);
    global_ini.backlog = iniparser_getint(ini, "listen:backlog", 1);
    global_ini.nworkers = iniparser_getint(ini, "concurrency:nworkers", 1);
    global_ini.nepolls_per_worker = iniparser_getint(ini, "concurrency:nepolls_per_worker", 1);
    global_ini.nconn_per_epoll = iniparser_getint(ini, "concurrency:nconn_per_epoll", 1);
    global_ini.nthreads_per_epoll = iniparser_getint(ini, "concurrency:nthreads_per_epoll", 1);
    iniparser_freedict(ini);
    return 0;
}

int CreateWorker(int nWorker)  
{
    /**
     * 在linux中，线程与进程最大的区别就是是否共享同一块地址空间，而且共享同一块地址空间的那一组线程将显现相同的PID号。
     * 在linux中，线程的创建和普通进程的创建类似，只不过在调用clone()的时候需要传递一些参数标志来指明需要共享的资源：
     *  clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND, 0);
     * 上面的代码产生的结果和调用fork()差不多，只是父子俩共享地址空间、文件系统资源、文件描述符和信号处理程序。
     *  换个说法就是，新建的进程和它的父进程就是流行的所谓线程。  
     *
     * 对比一下，一个普通的fork()的实现是：
     *  clone(SIGCHLD, 0);
     * 而vfork()的实现是：
     *  clone(CLONE_VFORK | CLONE_VM | SIGCHLD, 0);
     *
     * 传递给clone()的参数标志决定了新创建进程的行为方式和父子进程之间共享的资源种类。下面列举部分clone()参数标志，这些是在中定义的。
     * CLONE_FILES      父子进程共享打开的文件
     * CLONE_FS         父子进程共享文件系统信息
     * CLONE_SIGHAND    父子进程共享信号处理函数
     * CLONE_VM         父子进程共享地址空间
     * CLONE_VFORK      调用vfork()，所以父进程准备睡眠等待子进程将其唤醒
     */
    bool bIsChild = false;  
    pid_t nPid;  
    while (!bIsChild)
    {  
        if (0 < nWorker)  
        {  
            nPid = ::fork();  
            if (nPid > 0)  
            {  
                bIsChild = false;
                --nWorker;
            }  
            else if (0 == nPid)  
            {
                bIsChild = true;
                printf("create worker %d success!\n", ::getpid());
            }
            else
            {  
                printf("fork error: %s\n", ::strerror(errno));  
                return -1;  
            }  
        }  
        else {
            return 0;
        }
    }
    return 1;
}

typedef struct task_t  
{  
    int fd;
    bool boneshot; //set EPOLLONESHOT?
    char buffer[100];
    int n;
}task_t;  

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
    if (!ptr) {
        return -1;
    }

    task_t* task = (task_t*)ptr;
    if (!task->boneshot) return 0;//不需要再次注册

    struct epoll_event ev;
    ev.data.fd = fd;
    ev.data.ptr = ptr;
    if (task->boneshot)
        ev.events = events|EPOLLONESHOT;

    if (global_ini.nthreads_per_epoll == 1) {
        task->boneshot = false;
    }

    //注册epoll事件
    epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&ev);

    return 0;
}

int f_epoll_del(int epfd, int fd)
{
    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,NULL);
    return 0;
}

void DispatchConn(int connfd, int * epfds)
{
    task_t* task = (task_t*)malloc(sizeof(task_t));
    task->fd = connfd;
    task->n = 0;
    task->boneshot = true;

    int epfd = epfds[connfd%global_ini.nepolls_per_worker];
    f_epoll_add(epfd, connfd, EPOLLIN|EPOLLOUT|EPOLLET, task);
}

void CloseConn(int epfd, int fd, void* ptr)
{
    if (!ptr) return;

    task_t* task = (task_t*)ptr;
    free(task);

    f_epoll_del(epfd, fd);
    close(fd);
}

void* ThreadRoutine(void* args);
void WorkerRoutine()
{
    if (global_ini.nepolls_per_worker < 1) global_ini.nepolls_per_worker = 1;
    if (global_ini.nthreads_per_epoll < 1) global_ini.nthreads_per_epoll = 1;

    int* epfds = (int*)malloc((global_ini.nepolls_per_worker) * sizeof(int));
    int nthreads = global_ini.nepolls_per_worker * global_ini.nthreads_per_epoll;
    pthread_t* tids;
    if (nthreads > 1)
        tids = (pthread_t*)malloc((nthreads - 1) * sizeof(pthread_t));//1 is the main thread of the worker

    /**
     * 创建一个epoll的句柄，size用来告诉内核这个监听的数目一共有多大。
     * 这个参数不同于select()中的第一个参数，给出最大监听的fd+1的值。
     */
    for (int i = 0; i < global_ini.nepolls_per_worker; ++i) {
        epfds[i] = epoll_create(global_ini.nconn_per_epoll);
    }

    /**
     * 原型：int  pthread_create（（pthread_t  *thread,  pthread_attr_t  *attr,  void  *（*start_routine）（void  *）,  void  *arg）
     * 用法：#include  <pthread.h>
     * 功能：创建线程（实际上就是确定调用该线程函数的入口点），在线程创建以后，就开始运行相关的线程函数。
     * 说明：thread：线程标识符；
     * attr：线程属性设置；
     * start_routine：线程函数的起始地址；
     * arg：传递给start_routine的参数；
     * 返回值：成功，返回0；出错，返回-1。
     */
    for (int i = 0; i < nthreads - 1; ++i) { 
        int ret;
        if((ret = pthread_create(&tids[i], NULL, ThreadRoutine, (void*)epfds)) != 0){
            fprintf(stderr, "pthread_create:%s\n", strerror(ret));
            exit(1);
        }
    }

    task_t task;
    task.fd = listenfd;
    task.boneshot = true;
    f_epoll_add(epfds[0], listenfd, EPOLLIN|EPOLLET, &task);
    ThreadRoutine((void*)epfds);
}

void* ThreadRoutine(void* args)
{
    static int thread_no = 0;
    __sync_fetch_and_add(&thread_no, 1);
    printf("thread #%d(%u) of worker %d!\n", thread_no, (unsigned)pthread_self(), ::getpid());

    struct epoll_event* wait_evs = (struct epoll_event*)malloc(global_ini.nconn_per_epoll * sizeof(struct epoll_event));
    memset((void*)wait_evs, 0, global_ini.nconn_per_epoll * sizeof(struct epoll_event));

    int* epfds = (int*)args;
    int epfd = epfds[thread_no%global_ini.nepolls_per_worker];

    while(true)
    {
        //等待epoll事件的发生  
        /**
         * 等待事件的产生，类似于select()调用。
         * 参数events用来从内核得到事件的集合，maxevents告之内核这个events有多大(数组成员的个数)，这个maxevents的值不能大于创建epoll_create()时的size;
         * 参数timeout是超时时间（毫秒，0会立即返回，-1将不确定，也有说法说是永久阻塞）。
         * @return 该函数返回需要处理的事件数目，如返回0表示已超时。
         *         返回的事件集合在events数组中，数组中实际存放的成员个数是函数的返回值。返回0表示已经超时。
         */
        int nfds = epoll_wait(epfd, wait_evs, global_ini.nconn_per_epoll, 600);  
        int conns = 0;

        //处理所发生的所有事件
        for(int i = 0; i < nfds; ++i)
        {  
            printf("[%u]epoll_wait, fd = %d, event = %d\n", (unsigned)pthread_self(), wait_evs[i].data.fd, wait_evs[i].events);
            if (wait_evs[i].data.fd == listenfd)
            {
                if (thread_no != 0) {
                    printf("[%u]ERROR: listenfd wakeup in thread #%d", (unsigned)pthread_self(), thread_no);
                    continue;
                }

                socklen_t clilen;  
                struct sockaddr_in clientaddr;  

                int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clilen);  
                if (connfd < 0) {
                    printf("[%u]connfd<0, listenfd = %d, error = %s\n",
                            (unsigned)pthread_self(), listenfd, strerror(errno));  
                    continue;
                }
                else {
                    printf("[%u]new connection(%d) from %s:%d, fd = %d\n",
                            (unsigned)pthread_self(), ++conns, inet_ntoa(clientaddr.sin_addr), 
                            ntohs(clientaddr.sin_port), connfd);
                }
                setnonblocking(connfd);
                DispatchConn(connfd, epfds);
                f_epoll_add(epfd, listenfd, EPOLLIN|EPOLLET, wait_evs[i].data.ptr);
            }  
            else if (wait_evs[i].events & EPOLLIN)  
            {  
                task_t* task = (task_t*)wait_evs[i].data.ptr;
                int sockfd = task->fd;
                printf("[%u]EPOLLIN, fd = %d\n", (unsigned)pthread_self(), sockfd);

                if ( (task->n = read(sockfd, task->buffer, 100)) < 0) {  
                    if (errno == ECONNRESET) {  
                        CloseConn(epfd, sockfd, wait_evs[i].data.ptr);
                        printf("[%u]fd = %d, close, error = %s\n", (unsigned)pthread_self(), sockfd, strerror(errno));
                        continue;
                    } else  
                        printf("[%u]read error, fd = %d, error = %s\n",
                                (unsigned)pthread_self(), sockfd, strerror(errno));
                } else if (task->n == 0) {  
                    CloseConn(epfd, sockfd, wait_evs[i].data.ptr);
                    printf("[%u]fd = %d, close, error = %s\n", (unsigned)pthread_self(), sockfd, strerror(errno));
                    continue;
                }
                else {
                    task->buffer[task->n] = '\0';  
                    printf("[%u]fd = %d, read = %s\n", (unsigned)pthread_self(), sockfd, task->buffer);
                }

                f_epoll_add(epfd, sockfd, EPOLLIN|EPOLLOUT|EPOLLET, wait_evs[i].data.ptr);
            }  
            else if (wait_evs[i].events & EPOLLOUT)  
            {     
                task_t* task = (task_t*)wait_evs[i].data.ptr;
                int sockfd = task->fd;  
                printf("[%u]EPOLLOUT, fd = %d\n", (unsigned)pthread_self(), sockfd);

                if (task->n > 0) {
                    write(sockfd, task->buffer, task->n);  
                    task->n = 0;
                    printf("[%u]fd = %d, write = %s\n", (unsigned)pthread_self(), sockfd, task->buffer);
                }

                f_epoll_add(epfd, sockfd, EPOLLIN|EPOLLOUT|EPOLLET, wait_evs[i].data.ptr);
            }  
        }  
    }  

    return ((void *)0);
}

int main(int argc, char** argv)  
{
    if (argc<2) {
        fprintf(stderr, "usage: ./exe <file>\n");
        return 0;
    }
    if (loadconfig(argv[1]) < 0) {
        return 0;
    }

    signal(SIGCHLD, sig_chld);

    listenfd = tcplisten(global_ini.listen_port, global_ini.backlog);

    //fork
    int ret = CreateWorker(global_ini.nworkers);
    if (ret < 0) {
        exit(1);
    }
    else if (ret == 0) {
        //主进程
        while (true) {
            sleep(5);
            printf("==================5s===================\n");
        }
    }
    else {
        //worker 进程
        WorkerRoutine();
    }

    return 0;  
}
