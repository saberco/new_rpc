#ifndef __RPCCLIENT_H__
#define __RPCCLIENT_H__


#include "IOWorker.h"
#include "IRpcClient.h"

//客户端类实现，主要提供四种方法，具体IO任务在IOWorker中实现和处理
class RpcClient : public IRpcClient
{
public:
    //构造方法
    RpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval);
    //析构
    ~RpcClient();
    //启动客户端
    virtual void start() override;
    //结束客户端
    virtual void stop() override;
    //从IOWorker池中调度一个线程
    IOWorker* schedule(Conn* &pConn);

private:
    //每个IOWorker对象的写端描述符对应其对象s
    std::map<evutil_socket_t, IOWorker*> m_mapWorker;
    //是否启动
    bool m_bStarted;
    //是否结束
    bool m_bStopped;
};

#endif