#include <netinet/in.h>  
#include <arpa/inet.h>  
#include <stdio.h>  
#include <errno.h>  
#include <stdlib.h>  
#include <sys/wait.h>  
#include <string.h>
#include <pthread.h>
#include <unistd.h>  
#include "conn.h"
#include "f_epoll.h"
#include "net.h"
#include "common.h"

int listenfd;

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
        zlog_warn(lg, "child %d terminated with status %d", pid, stat);

    return;
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
                zlog_info(lg, "create worker %d success!", ::getpid());
            }
            else
            {  
                zlog_error(lg, "fork error: %s", ::strerror(errno));  
                return -1;  
            }  
        }  
        else {
            return 0;
        }
    }
    return 1;
}

void* ThreadRoutine(void* args);
void WorkerRoutine()
{
    if (global_ini.nepolls_per_worker < 1) global_ini.nepolls_per_worker = 1;
    if (global_ini.nthreads_per_epoll < 1) global_ini.nthreads_per_epoll = 1;

    int* epfds = (int*)malloc((global_ini.nepolls_per_worker) * sizeof(int));
    int nthreads = global_ini.nepolls_per_worker * global_ini.nthreads_per_epoll;
    pthread_t* tids = 0;
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
            zlog_info(lg, "pthread_create:%s", strerror(ret));
            exit(1);
        }
    }

    zlog_info(lg, "listenfd = %d, epfd = %d",
            listenfd, epfds[0]);
    zlog_info(lg, "EPOLLIN = %d, EPOLLOUT = %d, EPOLLET = %u, EPOLLONESHOT = %u",
            EPOLLIN, EPOLLOUT, EPOLLET, EPOLLONESHOT);

    ///start accepting in main thread
    tcpstart(listenfd, epfds[0]);
    ThreadRoutine((void*)epfds);
}

void* ThreadRoutine(void* args)
{
    static int thread_no = 0;
    __sync_fetch_and_add(&thread_no, 1);

    static const int MAX_EVENTS = 3;
    struct epoll_event wait_evs[MAX_EVENTS];

    int* epfds = (int*)args;
    int epfd = epfds[thread_no%global_ini.nepolls_per_worker];
    zlog_info(lg, "thread #%d(%u) of worker %d, epfd = %d!",
            thread_no, (unsigned)pthread_self(), ::getpid(),
            epfd);

    while (1)
    {
        //等待epoll事件的发生  
        /**
         * 等待事件的产生，类似于select()调用。
         * 参数events用来从内核得到事件的集合，maxevents告之内核这个events有多大(数组成员的个数)，这个maxevents的值不能大于创建epoll_create()时的size;
         * 参数timeout是超时时间（毫秒，0会立即返回，-1将不确定，也有说法说是永久阻塞）。
         * @return 该函数返回需要处理的事件数目，如返回0表示已超时。
         *         返回的事件集合在events数组中，数组中实际存放的成员个数是函数的返回值。返回0表示已经超时。
         */
        int nfds = epoll_wait(epfd, wait_evs, MAX_EVENTS, -1);  

        //处理所发生的所有事件
        for(int i = 0; i < nfds; ++i)
        {
            if (wait_evs[i].data.fd == listenfd)
            {
                int connfd = tcpaccept(listenfd, epfd);
                if (connfd < 0) {
                    continue;
                }

                DispatchConn(connfd, epfds);
            }
            else if ((wait_evs[i].events & EPOLLERR) ||
                    (wait_evs[i].events & EPOLLHUP))
            {
                /**
                 * An error has occured on this fd
                 */
                CloseConn(wait_evs[i].data.ptr);
            }
            else if (wait_evs[i].events & EPOLLIN)  
            {
                ReadConn(wait_evs[i].data.ptr);
            }
            else if (wait_evs[i].events & EPOLLOUT)  
            {
                WriteConn(wait_evs[i].data.ptr);
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
    int ret = zlog_init(global_ini.cfg_file);
    if (ret) {
        fprintf(stderr, "init log failed\n");
        return 0;
    }

    lg = zlog_get_category("default");
    if (!lg) {
        printf("get default log catagory fail\n");
        zlog_fini();
        return 0;
    }

    signal(SIGCHLD, sig_chld);

    listenfd = tcplisten(global_ini.listen_port, global_ini.backlog);

    //fork
    ret = CreateWorker(global_ini.nworkers);
    if (ret < 0) {
        exit(1);
    }
    else if (ret == 0) {
        //主进程
        while (true) {
            sleep(60);
            zlog_info(lg, "=================1 min===================");
        }
    }
    else {
        //worker 进程
        WorkerRoutine();
    }

    zlog_fini();
    close(listenfd);
    return 0;  
}
