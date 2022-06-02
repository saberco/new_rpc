#ifndef __RPCCHANNEL_H__
#define __RPCCHANNEL_H__

#include "RpcClientGlobal.h"
#include "IRpcChannel.h"
#include "RpcClient.h"

//rpc channel，一个通道代表一个连接
class RpcChannel : public IRpcChannel{
public:
    //构造,需要服务器IP和端口号，以及指向客户端的指针
    RpcChannel(RpcClient* pClient, const std::string& ip, int port);
    //析构
    ~RpcChannel();

    virtual void CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done) override;
private:
    //指向客户端的指针
    RpcClient* m_pClient;
    //客户端的IOWorker
    IOWorker* m_pWorker;
    //是否发起过连接
    bool m_bConnected;
    //同步sockpair的有效性
    bool m_bSyncVaild;  
    //连接Id
    unsigned int m_connId;
    //服务器ip和端口
    std::string m_ip;
    int m_port;
    //同步调用的socketpair读端
    evutil_socket_t m_sync_read_fd;
    //同步调用的socketpair写端
    evutil_socket_t m_sync_write_fd;

};






#endif