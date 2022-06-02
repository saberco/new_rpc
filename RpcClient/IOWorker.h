#ifndef __IOWORKER_H__
#define __IOWORKER_H__

#include "RpcClientGlobal.h"

//同服务端的IOWorker类似都负责处理IO事件，客户端的IO事件主要是发起连接，进行调用，接收回传响应

//三个回调函数，和服务端的类似
//连接描述符可读后回调此函数
void notifiedCallback(evutil_socket_t fd, short event, void *pArg);

//连接描述符的缓冲区可写后回调次函数
void readCallback(struct bufferevent*pBufEv, void *pArg);

//bufferevnet上发生读写以外的事件后回调此函数
void eventCallback(struct bufferevent*pBufEv, short event, void *pArg);

//IOWorker对象，每个对象保有一个线程，循环运行在线程中
class IOWorker{
public:
    //队列，用于放置IO任务
	SyncQueue<IOTask> m_queue;
public:
    //构造方法
    //对应的sockpair对，IO队列的长度，心跳周期
    IOWorker(evutil_socket_t *fds, unsigned int queueMaxSize, timeval heartbeatInterval);

    //析构方法
    ~IOWorker();

    //启动IOWorker
    void start();


    //处理IO任务通知，将其放在IOWorker队列中，
    void handleIOTask();

    //处理结束的通知
    void handleEnd();

    //处理buffer缓冲区的数据
    void handleRead(Conn* pConn);

    //处理buffer缓冲区连接重构事件
    void handleConnected(Conn* pConn);

    //处理buffer超时事件
    void handleTimeout(Conn* pConn);

    //发起连接
    bool connect(Conn* pConn);

    //释放连接
    void freeConn(Conn* pConn);

    //获取繁忙程度
    unsigned int getBusyLevel();

    //获取IOWorker写端
    evutil_socket_t getNotify_fd();

    //生成连接
    Conn* genConn();



private:

    //线程主方法
    void threadMain(evutil_socket_t notified_fd);

    //处理断开连接的任务
    void handleDisconnect(unsigned int connId);
    //处理调用的任务
    void handleCall(Call *pCall);
    //rpc回调
    void rpcCallback(google::protobuf::Closure *pClosure, evutil_socket_t sync_write_fd);
    //线程
    boost::thread m_thd;
    //用于收发通知的sockpair
    evutil_socket_t m_notified_fd;
    evutil_socket_t m_notify_fd;

    //唯一连接生成器
    UniqueIdGenerator<unsigned int>m_connIdGen;
    //所有连接,连接id,连接指针
    std::map<unsigned int, Conn*>m_mapConn;
    //event_base
    event_base* m_pEvBase;

    //心跳ping的周期
    timeval m_heartbeatInterval;

    //是否已经启动IOWorker
    bool m_bStarted;
    bool m_bStopped;
};


#endif