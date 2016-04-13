#ifndef _CONF_H_
#define _CONF_H_

struct ini_s {
    short listen_port;
    int backlog;

    int nworkers;
    int nepolls_per_worker;
    int nconn_per_epoll;
    int nthreads_per_epoll;
};

extern struct ini_s global_ini;
int loadconfig(const char* ini_name);


#endif
