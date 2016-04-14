#ifndef _CONF_H_
#define _CONF_H_
#include "zlog.h"

struct ini_s {
    short listen_port;
    int backlog;

    const char* cfg_file;

    int nworkers;
    int nepolls_per_worker;
    int nconn_per_epoll;
    int nthreads_per_epoll;
};

extern struct ini_s global_ini;
int loadconfig(const char* ini_name);

extern zlog_category_t *lg;//日志

#endif
