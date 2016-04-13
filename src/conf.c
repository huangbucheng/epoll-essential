#include "conf.h"
#include "iniparser.h"

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

