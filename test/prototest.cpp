#include "../common/ProtocolBody.pb.h"
#include <string>
#include <iostream>
#include <fstream>

// message ProtocolBodyRequest
// {
// 	optional string serviceName = 1;    //第一个参数
// 	optional uint32 methodIndex = 2;    //第二个参数
// 	optional uint32 callId = 3;         //...
// 	optional bytes content = 4;
// }

// message ProtocolBodyResponse
// {
// 	optional uint32 callId = 1;
// 	optional bytes content = 2;
// }
using namespace std;

int main(){
    ProtocolBodyRequest pr{};
    pr.set_servicename("my server");
    pr.set_methodindex(1);
    pr.set_callid(9006);
    pr.set_content("ni hao");
    string binary{};
    pr.SerializeToString(&binary);
    cout<<binary<<endl;
    //解析
    ProtocolBodyRequest pr2;
    if(!pr2.ParseFromString(binary)){
        cout<<"error\n";
    }
    cout<<pr2.content()<<endl;
    cout<<pr2.callid()<<endl;
    cout<<pr2.servicename()<<endl;
    

    return 0;
}