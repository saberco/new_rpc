#ifndef __RPCSERVERGLOBAL_H__
#define __RPCSERVERGLOBAL_H__

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <boost/thread/thread.hpp>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "../common/SyncQueue.h"
#include "../common/Global.h"


enum NOTIFY_SERVER_TYPE{
    //告诉服务端停止事件循环
    NOTIFY_SERVER_END = 0
};

enum NOTIFY_IOWORKER_TYPE{
    //接听连接，先accept得到，然后通知IOWorker来接管连接
    NOTIFY_IOWORKER_ACCEPT = 0,
    //发送数据，通知IOWorker发送数据
    NOTIFY_IOWORKER_WRITE = 1,
    //告诉IOWorker结束
    NOTIFY_IOWORKER_END = 2
};

class IOWorker;

//连接信息
struct Conn{
    //是头还是体
    PROTOCOL_PART inState;
    //消息体的长度uint32_t
    bodySize_t inBodySize;

    //连接描述符
    evutil_socket_t fd;
    //连接对应的buffer事件
    //bufferevent使得对socket，socketpair和filter的读取变得方便
    bufferevent * pBufEv;
    //所述的IOWork
    IOWorker* pWorker;
    //连接上未处理的请求数量，排除PING心跳
    unsigned int todoCount;
    //连接合法性
    bool bValid;

    Conn(){
        inState = PROTOCOL_HEAD;
        inBodySize = 0;
        pBufEv = nullptr;
        pWorker = nullptr;
        todoCount = 0;
        bValid = true;
    }
};

//具体是什么任务需要处理
struct BusinessTask{
    // 发起业务的IOWorker
    IOWorker* pWorker;
    // 发起业务的连接
    evutil_socket_t fd;
    //发起连接socket的缓冲区
    evbuffer* pBuf;

    BusinessTask(){
        pWorker = nullptr;
        pBuf = nullptr;
    }
};

#endif