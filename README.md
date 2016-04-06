# epoll-essential
一个epoll set，单线程
    业务需要异步访问其他资源，不适合高cpu密度的任务，否则线程阻塞。

一个epoll set，多线程
    业务可以同步访问其他资源，如需异步访问其他资源，需要将这个访问fd加入epoll set。
    多线程访问一个epoll set，有锁的开销（系统内部的实现EPOLLONESHOT）。
    leader-follower。
    使用EPOLLONESHOT时，如果一次没有读完完整的请求包，重新加入epoll set后，下一次触发被另一个线程抢到，怎么处理这个请求包？

多个epoll set，多个线程
    可以是一对一，也可以是一对N。
    最好不要将监听socket（或同一个socket）注册给多个epoll set, 那样会有惊群问题。
    改进版leader-follower，减小锁的粒度。

多个epoll set，多个进程
    监听同一个fd时，有惊群问题，如nginx。
    惊群问题，对于短链接场景影响较大，对于长链接场景，影响相对较小。
    面向用户的，需要固定端口号的情况，处理惊群问题。
    面向rpc的，由于有服务发现服务的管理，可以不用固定端口号。

一个epoll set，多进程
    一般不这么用，可能问题较多。
