#ifndef __RPCCLIENTGLOBAL_H__
#define __RPCCLIENTGLOBAL_H__

#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <boost/thread/thread.hpp>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include "../common/SyncQueue.h"
#include "../common/UniqueIdGenerator.h"
#include "../common/Global.h"
#include "../common/ProtocolBody.pb.h"

//向IOWorker发送的通知类型
enum NOTIFY_IOWORKER_TYPE{
    //通知IO处理任务
    NOTIFY_IOWORKER_IOTASK = 0,
    //通知IO结束循环
    NOTIFY_IOWORKER_END = 1
};

//客户端调用,传给服务器
struct Call
{
    //这里调用id和连接id都是唯一id
    //调用id
    callid_t callId;
    //连接id
    unsigned int connId;
    //同步调用的写端描述符
    evutil_socket_t sync_write_fd;
    //请求参数序列化后指针
    std::string* pStrReq;
    //响应的Message
    google::protobuf::Message* pRespMessage;
    //异步回调指针
    google::protobuf::Closure* pClosure;
    //表示调用成功与否和错误原因的指针
    google::protobuf::RpcController* pController;
    //调用服务名
    std::string serviceName;
    //调用的方法索引
    uint32_t methodIndex;

    Call(){
        pStrReq = nullptr;
        pRespMessage = nullptr;
        pClosure = nullptr;
        pController = nullptr;
    }
};

class IOWorker;
//客户端的连接
struct Conn
{
    //输入数据的状态
    PROTOCOL_PART inState;
    //输入数据的长度
    bodySize_t inBodySize;
    //连接成功标志位
    bool bConnected;
    //是否主观认为失去连接
    bool bConnetionMightLost;

    //连接ID
    unsigned int connId;
    //连接的文件描述符
    evutil_socket_t fd;
    //连接的buffer
    bufferevent* pBufEv;
    //唯一ID生成器
    UniqueIdGenerator<callid_t> idGen;
    //连接对应的所有的调用
    std::map<callid_t, Call*> mapCall;
    //连接所述的IOWorker地址
    IOWorker* pWorker;
    //服务器的地址
    sockaddr_in serverAddr;

    Conn(){
        inState = PROTOCOL_HEAD;
        inBodySize = 0;
        bConnected = false;
        bConnetionMightLost = false;
        pBufEv = nullptr;
        pWorker = nullptr;
    }
};

//IO任务
struct IOTask
{
    enum TYPE{
        //连接
        CONNECT = 0,
        //断开连接
        DISCONNECT = 1,
        //调用
        CALL = 2
    };

    //IO任务类型
    IOTask::TYPE type;
    //任务数据指针,根据任务类型做转换
    void* pData;

    IOTask(){
        pData = nullptr;
    }
};

#endif