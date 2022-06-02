#ifndef __IRPCCHANNEL_H__
#define __IRPCCHANNEL_H__

#include<string>
#include<google/protobuf/service.h>
#include"IRpcClient.h"

//rpcchannel的接口
class IRpcChannel
{
public:
    virtual ~IRpcChannel();

    //创建channel实例,RPCCHANNEL是为通信双方建立连接使用的，需要服务器IP和端口号，以及指向客户端的指针
    static IRpcChannel* createRpcChannel(IRpcClient* pClient,const std::string& ip, int port);

    //销毁channel实例
    static void releaseRpcChannel(IRpcChannel* pIRpcChannel);

    //调用方法,客户端进行调用，服务器进行处理
    virtual void CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done) = 0;
};










#endif