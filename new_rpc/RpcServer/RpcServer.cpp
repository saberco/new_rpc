#include "RpcServer.h"


IRpcServer::~IRpcServer(){};
IRpcServer * IRpcServer::createRpcServer(const std::string & ip, int port, unsigned int IOWorkerNum, 
                                        unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize, 
                                        unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize)
{
    return new RpcServer(ip, port, IOWorkerNum,IOWorkerAcceptQueueMaxSize,IOWorkerCompleteQueueMaxSize, businessWorkerNum,businessWorkerQueueMaxSize);
}

void IRpcServer::releaseRpcServer(IRpcServer * pIRpcServer){
    SAFE_DELETE( pIRpcServer);
}

RpcServer::RpcServer(const std::string & ip, int port, unsigned int IOWorkerNum, unsigned int IOWorkerAcceptQueueMaxSize, 
                    unsigned int IOWorkerCompleteQueueMaxSize, unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize)
{
    m_ip = ip;
    m_port = port;
    m_pEvBase = nullptr;
    m_bStarted = false;
    m_bStopped = false;

    //创建sockpair以接收和发送结束通知
    evutil_socket_t notif_fds[2];
    int ret = evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, notif_fds);
    m_notified_fd = notif_fds[0];
    m_notify_fd = notif_fds[1];

    //创建业务处理池
    for(unsigned int i = 0;i<businessWorkerNum;++i){
        m_VecBusinessWorker.push_back(new BusinessWorker(businessWorkerQueueMaxSize));
    }

    //创建IOWorker池
    for(unsigned int i=0;i<IOWorkerNum;++i){
        //IOWorker需要sockpair进行和业务池的通信
        evutil_socket_t fds[2];
        if(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)continue;
        //设置非阻塞，仅有事件出现才进行处理
        evutil_make_socket_nonblocking(fds[0]);
        evutil_make_socket_nonblocking(fds[1]);
        m_mapIOWorker[fds[1]] = new IOWorker(fds, IOWorkerAcceptQueueMaxSize, IOWorkerCompleteQueueMaxSize, &m_VecBusinessWorker);
    }

}

RpcServer::~RpcServer(){
    stop();
}

void RpcServer::registerService(google::protobuf::Service * pService){
    if(nullptr ==  pService)return;
    //获取服务描述指针
    const google::protobuf::ServiceDescriptor* pServiceDescriptor = pService->GetDescriptor();
    if(nullptr == pServiceDescriptor)return;

    //获得，服务指针, vector<方法描述指针>
    std::pair<google::protobuf::Service*, std::vector<const google::protobuf::MethodDescriptor*>> &v = m_mapRegisteredService[pServiceDescriptor->full_name()];
    //存放服务对应的方法指针
    v.first =  pService;
    //存放对应的方法描述指针
    for(int i=0;i< pServiceDescriptor->method_count();++i){
        const google::protobuf::MethodDescriptor* pMethodDescriptor = pServiceDescriptor->method(i);
        if(nullptr == pMethodDescriptor)continue;
        v.second.emplace_back(pMethodDescriptor);
    }
}

//启动服务,不能重复启动
void RpcServer::start(){
    if(m_bStarted)return;
    m_bStarted = true;

    //创建event_base，进行连接的监听
    std::cout<<"create event_base"<<std::endl;
    event_base* pEvBase = event_base_new();
    if(nullptr == pEvBase)return;
    //绑定event_base到智能指针，自动销毁
    std::unique_ptr<event_base, std::function<void(event_base*)>> ptrEvBase(pEvBase, event_base_free);

    //创建监听的地址,注意字节序的转换
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    int ret = evutil_inet_pton(AF_INET, m_ip.c_str(), &(serverAddr.sin_addr));
    if( 0 == ret)return;
    serverAddr.sin_port = htons(m_port);
    //绑定并进行监听
    //主事件、触发事件的回调，传给回调的参数，可选选项，最大监听数量，绑定的地址，大小
    evconnlistener* pListener = evconnlistener_new_bind(pEvBase, acceptCallback, this, LEV_OPT_CLOSE_ON_FREE, 128, (sockaddr*)(&serverAddr), sizeof(serverAddr));
    if(nullptr ==  pListener)return;
    //绑定智能指针
    std::unique_ptr<evconnlistener, std::function<void(evconnlistener*)>> ptrListener(pListener, evconnlistener_free);

    //启动IOWorker池
    std::cout<<"start IOWorker pool"<<std::endl;
    for(auto it = m_mapIOWorker.begin(); it != m_mapIOWorker.end(); ++it){
        if(nullptr!=it->second){
            it->second->start();
        }
    }


    //启动业务worker池
    std::cout<<"start BusinessWorker pool"<<std::endl;
    for(auto it = m_VecBusinessWorker.begin(); it != m_VecBusinessWorker.end();++it){
        if(nullptr != *it){
            //将所注册的一切服务告知业务，让业务可以进行调用
            (*it)->setRegisteredServices(&m_mapRegisteredService);
            //启动
            (*it)->start();
        }
    }

    //创建socketpair上的可读事件，让其能接受到信号,接收到时调用回调，处理
    std::cout<<"create new event"<<std::endl;
    event* pNotifiedEv = event_new(pEvBase, m_notified_fd, EV_READ|EV_PERSIST, serverNotifiedCallback, this);
    if(nullptr == pNotifiedEv)return;
    //绑定智能指针
    std::unique_ptr<event, std::function<void(event*)>> strpNotifiedEv(pNotifiedEv, event_free);
    //注册sockpair事件
    event_add(pNotifiedEv, NULL);

    m_pEvBase = pEvBase;
    //启动循环
    std::cout<<"start loop"<<std::endl;
    event_base_dispatch(pEvBase);
}

//停止服务，不能重复结束
void RpcServer::stop(){
    if(m_bStopped)return;
    m_bStopped = true;

    //销毁两个池
    for(auto it = m_mapIOWorker.begin(); it!= m_mapIOWorker.end(); ++it){
        if(nullptr != it->second){
            SAFE_DELETE(it->second);
            //关闭socketpair写端
            evutil_closesocket(it->first);
        }
    }

    for(auto it = m_VecBusinessWorker.begin(); it!=m_VecBusinessWorker.end(); ++it){
        if(nullptr != *it){
            SAFE_DELETE(*it);
        }
    }

    //结束事件循环
    char buf[1] = {NOTIFY_SERVER_END};
    send(m_notify_fd, buf, 1, 0);
}

//回调
void serverNotifiedCallback(evutil_socket_t fd, short event, void * pArg){
    if(nullptr != pArg)return;
    //读取通知
    char buf[1];
    recv(fd, buf, 1, 0);

    if(NOTIFY_SERVER_END==buf[0]){
        ((RpcServer*)pArg)->handleEnd();
    }
}

//处理stop发来的结束通知
void RpcServer::handleEnd(){
    //关闭文件描述符
    evutil_closesocket(m_notified_fd);
    evutil_closesocket(m_notify_fd);
    //退出事件循环
    if(nullptr != m_pEvBase){
        event_base_loopexit(m_pEvBase, NULL);
    }
}

//接收回调
void acceptCallback(evconnlistener * pListener, evutil_socket_t fd, sockaddr * pAddr, int socklen, void * pArg){

    if(nullptr != pArg){
        ((RpcServer*)pArg)->handleAccept(fd);
    }
}

//处理accept，主要是交给IOWorker处理，主循环线程只负责监听
void RpcServer::handleAccept(evutil_socket_t fd){
    //将accept设置为非阻塞
    evutil_make_socket_nonblocking(fd);

    //调度一个IOWorker对象来处理ACCEPT
    IOWorker* pSelectedWorker = schedule();
    if(nullptr != pSelectedWorker){
        //获取sockpair写端
        evutil_socket_t selectedNotify_fd = pSelectedWorker->getNotify_fd();
        //放其如accept队列
        pSelectedWorker->m_acceptQueue.put(fd);
        //通知进行accept处理
        char buf[1] = {NOTIFY_IOWORKER_ACCEPT};
        send(selectedNotify_fd, buf, 1, 0);

    }
}

//调度一个IOWorker对象
IOWorker* RpcServer::schedule(){
    //返回值，和IOWorker选择业务对象类似，根据繁忙程度来调度
    IOWorker* pSelectedWorker = nullptr;
    unsigned int minBusyLevel;
    auto it = m_mapIOWorker.begin();
    for(;it!=m_mapIOWorker.end();++it){
        IOWorker* pWorker = it->second;
        if(nullptr != pWorker ){
            pSelectedWorker = pWorker;
            minBusyLevel = pWorker->getBusyLevel();
            break;
        }
    }

    if(nullptr == pSelectedWorker || minBusyLevel <=0){
        return pSelectedWorker;
    }

    for(++it; it!=m_mapIOWorker.end();++it){
        IOWorker* pWorker = it->second;
        if(nullptr != pWorker ){
            unsigned int busyLevel = pWorker->getBusyLevel();
            if(busyLevel < minBusyLevel){
                pSelectedWorker = pWorker;
                minBusyLevel = busyLevel;
                if(minBusyLevel <=0){
                    break;
                }
            }
        }
    }
    return pSelectedWorker;
}


