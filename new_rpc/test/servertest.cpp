#include <boost/thread/thread.hpp>
#include "Test.pb.h"
#include "../RpcServer/IRpcServer.h"
#include "../common/RPCController.h"


//实现一个服务类

class NumServicetest : public testNamespace::NumService{
public:
    virtual void add(::google::protobuf::RpcController* controller,
                       const ::testNamespace::NumRequest* request,
                       ::testNamespace::NumResponse* response,
                       ::google::protobuf::Closure* done) override
    {
        response->set_output(request->input1() + request->input2());
    }

    virtual void minus(::google::protobuf::RpcController* controller,
                       const ::testNamespace::NumRequest* request,
                       ::testNamespace::NumResponse* response,
                       ::google::protobuf::Closure* done) override
    {
        response->set_output(request->input1() - request->input2());
    }
};


int main(){

    //创建实例
    IRpcServer* pIServer = IRpcServer::createRpcServer("127.0.0.1", 8888, 3, 50, 50, 3, 50);
    //创建服务类实例，并注册服务
    NumServicetest numService;
    pIServer->registerService(&numService);
    //启动server
    pIServer->start();

    return 0;
}
