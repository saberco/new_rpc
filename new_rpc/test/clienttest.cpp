#include <iostream>
#include <unistd.h>
#include <google/protobuf/stubs/common.h>
#include "../common/RPCController.h"
#include "Test.pb.h"
#include "../RpcClient/IRpcChannel.h"
#include "../RpcClient/IRpcClient.h"

//异步调用的回调函数
void callback(testNamespace::NumResponse *pResp, google::protobuf::RpcController *pController){
    if(nullptr == pResp)return;
    if(nullptr != pController){
        if(pController->Failed())return;
    }

    std::cout<<"async call result = " << pResp->output() << std::endl;
}

int main(){
    //创建客户端实例
    //3s的心跳
    timeval t = {3,0};
    IRpcClient* pIClient = IRpcClient::createRpcClient(3, 50 ,t);
    //启动客户端
    pIClient->start();

    //创建channel实例
    IRpcChannel* pIRpcChannel = IRpcChannel::createRpcChannel(pIClient, "127.0.0.1", 8888);
 
    //创建服务的客户端stub实例
    testNamespace::NumService::Stub numServieceStub((google::protobuf::RpcChannel*) pIRpcChannel);

    //创建请求并赋值
    testNamespace::NumRequest req;
    req.set_input1(3);
    req.set_input2(4);

    //创建响应
    testNamespace::NumResponse resp;

    //创建rpccontrol
    RpcController controller;
    std::cout<<"controller"<<std::endl;
    //done传入null为同步
    numServieceStub.minus(&controller, &req, &resp, NULL);
    std::cout<<"minus complete" <<  std::endl;
    if(controller.Failed()){
        std::cout << "sync call error: " << controller.ErrorText() << std::endl;
    }
    else{
        std::cout << "sync call result: 3 - 4 = " << resp.output() << std::endl;
    }
    sleep(1);
    //done传入回调为异步
    controller.Reset();
    numServieceStub.add(&controller, &req, &resp, google::protobuf::NewCallback(callback, &resp, (google::protobuf::RpcController *)(&controller)));
    std::cout<<"add complete" <<  std::endl;
    while(true){}
    return 0;
}