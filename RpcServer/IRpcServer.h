#ifndef __IRPCSERVER_H__
#define __IRPCSERVER_H__

#include<string>
#include<google/protobuf/service.h>

//RPCserver接口，抽象类
class IRpcServer{
public:
    virtual ~IRpcServer();

    //创建RpcServer实例，主要是对各种类容进行初始化
    // ip：输入，服务器监听的ip地址
    // port：输入，服务器监听的端口号
    // IOWorkerNum：输入，IOWorker池Worker数量
    // IOWorkerAcceptQueueMaxSize：输入，IOWorker接管连接队列的最大长度
    // IOWorkerWriteQueueMaxSize：输入，IOWorker发送队列的最大长度
    // businessWorkerNum：输入，业务Worker池Worker数量
    // businessWorkerQueueMaxSize：输入，业务Worker业务任务队列的最大长度
    static IRpcServer* createRpcServer(const std::string &ip, int port, unsigned int IOWorkerNum, 
                                        unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize,
                                        unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize);

    //删除RpcServer实例
    static void releaseRpcServer(IRpcServer* pIRpcServer);

    //注册服务
    virtual void registerService(google::protobuf::Service* pService) = 0;

    //启动RPCServer
    virtual void start() = 0;
    
    //停止RPCServer
    virtual void stop() = 0;
};






#endif