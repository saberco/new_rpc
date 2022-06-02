#include "RpcClient.h"

IRpcClient::~IRpcClient(){}

IRpcClient* IRpcClient::createRpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval){
    return new RpcClient(IOWorkerNum, IOWorkerQueueMaxSize, heartbeatInterval);
}

void IRpcClient::releaseRpcClient(IRpcClient * pIRpcClient){
    SAFE_DELETE(pIRpcClient);
}


RpcClient::RpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval){
    m_bStarted = false;
    m_bStopped = false;

    //创建IOWorker池
    for(unsigned int i=0;i<IOWorkerNum;++i){
        evutil_socket_t fds[2];
        if(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)continue;
        //设置描述符非阻塞
        evutil_make_socket_nonblocking(fds[0]);
        evutil_make_socket_nonblocking(fds[1]);
        m_mapWorker[fds[1]] = new IOWorker(fds, IOWorkerQueueMaxSize, heartbeatInterval);
    }
}


RpcClient::~RpcClient(){
    stop();
}

void RpcClient::start(){
    if(m_bStarted)return;
    m_bStarted = true;
    //启动IOWorker池
    for(auto it = m_mapWorker.begin();it!=m_mapWorker.end();++it){
        IOWorker* pWorker = it->second;
        if(nullptr == pWorker)continue;
        pWorker->start();
    }
}

void RpcClient::stop(){
    //不能重复结束
    if(m_bStopped)return;
    m_bStopped = true;

    //销毁IOWorker池，并关闭写端，读端在IOWorker析构中进行销毁
    for(auto it = m_mapWorker.begin();it!=m_mapWorker.end();++it){
        if(nullptr != it->second){
            SAFE_DELETE(it->second);
        }
        evutil_closesocket(it->first);
    }
}

//仍然是通过繁忙程度进行调度选取
IOWorker* RpcClient::schedule(Conn* &pConn){
    IOWorker* pSelectedWorker = nullptr;
    unsigned int minBusyLevel;
    auto it = m_mapWorker.begin();

    for(;it!=m_mapWorker.end();++it){
        IOWorker* pWorker = it->second;
        if(nullptr != pWorker){
            minBusyLevel = pWorker->getBusyLevel();
            pSelectedWorker = pWorker;
            break;
        }
    }

    if(nullptr == pSelectedWorker){
        return nullptr;
    }
    if( minBusyLevel>0){
        for(++it;it!=m_mapWorker.end();++it){
            IOWorker* pWorker = it->second;
            if(nullptr != pWorker){
                unsigned int busyLevel = pWorker->getBusyLevel();
                if(busyLevel < minBusyLevel){
                    pSelectedWorker = pWorker;
                    minBusyLevel = busyLevel;
                    if(minBusyLevel <= 0)break;
                }
            }
        }
    }
    pConn = pSelectedWorker->genConn();
    return (nullptr == pConn) ? nullptr : pSelectedWorker;
}
