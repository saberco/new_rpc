#ifndef __IOWORKER_H__
#define __IOWORKER_H__

#include "RpcServerGlobal.h"
#include "BusinessWorker.h"

//这个主要负责的是IO操作，监听IO事件、进行读，写操作

//连接描述符可读后回调此函数
void notifiedCallback(evutil_socket_t fd, short event, void *pArg);

//连接描述符的缓冲区可写后回调次函数
void readCallback(struct bufferevent*pBufEv, void *pArg);

//监听到bufferevnet上发生读写以外的事件后回调此函数
void eventCallback(struct bufferevent*pBufEv, short event, void *pArg);

//IOWORKER类
class IOWorker{
public:
    //构造，传入socketpair供线程间通信，初始化accept队列和write队列的最大长度，输入业务池指针
    IOWorker(evutil_socket_t* fds, unsigned int acceptQueueMaxSize, unsigned int writeQueueMaxSize, const std::vector<BusinessWorker*> *pVecBusinessWorker);


    //析构
    ~IOWorker();

    //启动IOWorker
    void start();

    //处理Accept
    void handleAccept();

    //处理Write
    void handleWrite();

    //处理结束通知
    void handleEnd();
    //处理bufferevent缓冲区数据
    void handleRead(Conn* pConn);
    //处理除读写事件以外的事件
    void handleEvent(Conn *pConn);
    //返回繁忙程度
    unsigned int getBusyLevel();
    //获取写端socketpair描述符
    evutil_socket_t getNotify_fd();




    //accept队列
	SyncQueue<evutil_socket_t> m_acceptQueue;
	//write队列
	SyncQueue<BusinessTask *> m_writeQueue;
    
private:

    //线程的工作成员函数
    void threadMain(evutil_socket_t notifiedfd);

    //从业务池中调度一个业务来处理请求
    BusinessWorker* schedule();

    //检查释放连接
    bool checkToFreeConn(Conn*pConn);




    //保有一个线程
    boost::thread m_thd;
    //具有socketpair与Businessworker线程通信
    //读端
    evutil_socket_t m_notified_fd;
    //写端
    evutil_socket_t m_notify_fd;
    //业务池指针，每个对象保有一个，初始化完毕后不会被改变
    const std::vector<BusinessWorker*> *m_pVecBusinessWorker;
    //所有连接
    std::map<evutil_socket_t, Conn*> m_mapConn;
    //当前连接数量
    unsigned int m_connNum;
    //event_base基指针
    event_base* m_pEvBase;
    //启动和终止标志位
    bool m_bStarted;
    bool m_bStopped;
};



#endif