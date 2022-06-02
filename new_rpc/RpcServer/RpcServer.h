#ifndef __RPCSERVER_H__
#define __RPCSERVER_H__

#include "RpcServerGlobal.h"
#include "IOWorker.h"
#include "BusinessWorker.h"
#include "IRpcServer.h"

//服务端要提供的功能：
//accept线程接受客户端连接
//通过IOWorker管理连接的IO事件，一个IOWorker可以管理多个IO事件
//通过IOWorker调度businessworker，处理请求，得到响应再通过IOWorker回传给客户端

//触发读事件回调此函数
void serverNotifiedCallback(evutil_socket_t fd, short event, void *pArg);


//接收到连接后触发此事件
void acceptCallback(evconnlistener* pListener, evutil_socket_t fd, struct sockaddr* pAddr, int socklen, void* pArg);

//服务器类
class RpcServer : public IRpcServer{
public:
    //构造函数,用create函数调用
    RpcServer(const std::string &ip, int port, unsigned int IOWorkerNum, 
                unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize,
                unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize);

    //析构
    ~RpcServer();

    //注册服务
    virtual void registerService(google::protobuf::Service* pService) override ;

    //启动RPCServer
    virtual void start() override ;
    
    //停止RPCServer
    virtual void stop() override ;

    //处理结束通知
    void handleEnd();

    void handleAccept(evutil_socket_t fd);

private:
    //调度一个IOWorker来接管连接
    IOWorker* schedule();

    //服务器监听的ip
    std::string m_ip;
    //服务器监听的端口
    int m_port;
    //外部注册的所有服务map<服务名称, pair<服务指针, vector<方法描述指针> > >
    std::map<std::string, std::pair<google::protobuf::Service*, std::vector<const google::protobuf::MethodDescriptor*>>> m_mapRegisteredService;
    //IOWorker池
    std::map<evutil_socket_t, IOWorker*> m_mapIOWorker;
    //业务处理池
    std::vector<BusinessWorker*> m_VecBusinessWorker;
    //event_base指针
    event_base* m_pEvBase;
    //socketpair，接收服务结束
    evutil_socket_t m_notified_fd;
    //通知服务结束
    evutil_socket_t m_notify_fd;

    //是否启动和结束的标志位
    bool m_bStarted;
    bool m_bStopped;
};





#endif