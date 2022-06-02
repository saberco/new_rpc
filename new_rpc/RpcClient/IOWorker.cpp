
#include "IOWorker.h"


IOWorker::IOWorker(evutil_socket_t *fds, unsigned int queueMaxSize, timeval heartbeatInterval){
    m_notified_fd = fds[0];
    m_notify_fd = fds[1];
    m_pEvBase = nullptr;
    m_heartbeatInterval = heartbeatInterval;
    m_bStarted = false;
    m_bStopped = false;
    m_queue.setMaxSize(queueMaxSize);
}

IOWorker::~IOWorker(){
    //通知结束循环
    char buf[1] = {NOTIFY_IOWORKER_END};
    send(m_notify_fd, buf, 1, 0);
    //等待线程结束
    m_thd.join();
    //关闭读端，防止重复接收
    evutil_closesocket(m_notified_fd);
}

//启动线程循环
void IOWorker::start(){
    if(m_bStarted)return;
    m_bStarted = true;
    //启动线程
    m_thd = std::move(boost::thread(&IOWorker::threadMain, this, m_notified_fd));
}

//线程主循环
void IOWorker::threadMain(evutil_socket_t notified_fd){
    //创建基
    event_base* pEvBase = event_base_new();
    if(nullptr ==  pEvBase){
        evutil_closesocket(notified_fd);
        return;
    }
    //将创建的event_base自动销毁
    std::unique_ptr<event_base, std::function<void(event_base*)>> ptrEvBase(pEvBase,[&notified_fd](event_base*p){
        event_base_free(p);
        evutil_closesocket(notified_fd);
    });

    //创建读事件，方便后续进行注册
    event* pNotifiedEv = event_new(pEvBase, notified_fd, EV_READ | EV_PERSIST, notifiedCallback, this);
    if(nullptr ==  pNotifiedEv){
        return;
    }

    //将事件指针绑定为只能指针
    std::unique_ptr<event, std::function<void(event*)>> ptrNotifiedEv(pNotifiedEv, event_free);

    //进行事件的注册
    if(0 != event_add(pNotifiedEv, NULL))return;
    m_pEvBase = pEvBase;
    //启动线程循环监听，当可读的文件描述符触发可读时，触发notifiedCallback回调
    event_base_dispatch(pEvBase);
}

//notifiedCallback回调
void notifiedCallback(evutil_socket_t fd, short event, void *pArg){
    if(nullptr == pArg)return;

    //通过读端读取到并进行处理调用哪一个函数进行处理
    char buf[1];
    recv(fd, buf, 1, 0);
    IOWorker* pWorker= (IOWorker*)pArg;

    if(NOTIFY_IOWORKER_IOTASK == buf[0]){
        pWorker->handleIOTask();
    }else if(NOTIFY_IOWORKER_END == buf[0]){
        pWorker->handleEnd();
    }
}

//处理IO任务
void IOWorker::handleIOTask(){
    //一次性取出所有数据
    std::list<IOTask> queue;
    m_queue.takeAll(queue);

    for(auto it = queue.begin();it!=queue.end();++it){
        if(IOTask::CONNECT == (*it).type){
            connect((Conn*)(*it).pData);
        }
        else if(IOTask::DISCONNECT == (*it).type){
            unsigned int* pConnId = (unsigned int*)(*it).pData;
            if(nullptr!=pConnId){
                handleDisconnect(*pConnId);
                delete pConnId;
            }
        }
        else if(IOTask::CALL == (*it).type){
            handleCall((Call*)(*it).pData);
        }
    }
}

//结束的连接应该进行释放
void IOWorker::handleDisconnect(unsigned int connId){
    auto it = m_mapConn.find(connId);
    if(it == m_mapConn.end())return;

    Conn* pConn = it->second;

    if(nullptr != pConn){
        freeConn(pConn);
        delete(pConn);
    }
    m_mapConn.erase(it);
}

//处理调用请求
void IOWorker::handleCall(Call * pCall){
    if(nullptr == pCall)return;

    //找到连接
    auto it = m_mapConn.find(pCall->connId);
    if(it == m_mapConn.end()){
        m_connIdGen.back(pCall->connId);
        SAFE_DELETE(pCall);
        return;
    }
    Conn* pConn = it->second;
    if(nullptr == pConn){
        m_connIdGen.back(pCall->connId);
        SAFE_DELETE(pCall);
        m_mapConn.erase(it);
        return;
    }
    //如果连接已断开或者尚未建立，则将其重新放回IO队列而不进行处理,此时一定是CALL请求
    if(!pConn->bConnected){
        IOTask task;
        task.type = IOTask::CALL;
        task.pData = pCall;
        m_queue.put(task);
        return;
    }
    //如果没有缓冲区
    if(nullptr == pConn->pBufEv){
        SAFE_DELETE(pCall);
        return;
    }
    //获得输出缓冲区指针
    evbuffer* pOutBuf = bufferevent_get_output(pConn->pBufEv);
    if(nullptr == pOutBuf){
        SAFE_DELETE(pCall);
        return;
    }
    callid_t callId;
    if(!pConn->idGen.generate(callId)){
        //设置错误信息，返回用户调用
        pCall->pController->SetFailed("call id not enough");
        rpcCallback(pCall->pClosure, pCall->sync_write_fd);
        SAFE_DELETE(pCall);
        return;
    }
    //创建调用请求
    ProtocolBodyRequest bodyReq;
    bodyReq.set_callid(callId);
    bodyReq.set_servicename(pCall->serviceName);
    bodyReq.set_methodindex(pCall->methodIndex);
    bodyReq.set_content(*pCall->pStrReq);

    //然后对请求序列化
    std::string strBuf;
    if(!bodyReq.SerializeToString(&strBuf)){
        //失败设置错误信息
        pCall->pController->SetFailed("request serialized failed");
        rpcCallback(pCall->pClosure, pCall->sync_write_fd);
        SAFE_DELETE(pCall);
        return;
    }
    //获取请求体的大小，进行请求头的搭建
    bodySize_t bodyLen = strBuf.size();
    unsigned char arr[HEAD_SIZE] = {0};
    arr[0] = DATA_TYPE_REQUEST;
    //以下这句代码要注意字节序，防止通信对端解析错误，暂未考虑字节序
    memcpy(arr + 1, &bodyLen, HEAD_SIZE - 1);

    //将请求头放入
    evbuffer_add(pOutBuf, arr, HEAD_SIZE);
    //将序列化的请求体放入
    evbuffer_add(pOutBuf, strBuf.c_str(), bodyLen);

    //请求处理完毕，将Call的请求内容删除
    SAFE_DELETE(pCall->pStrReq);
    pConn-> mapCall[callId] = pCall;
}


void IOWorker::handleEnd(){
    //不能重复结束
    if(m_bStopped)return;
    m_bStopped = true;

    //退出事件循环
    if(nullptr != m_pEvBase){
        event_base_loopexit(m_pEvBase, NULL);
    }

    //释放所有连接
    for(auto it=m_mapConn.begin(); it != m_mapConn.end(); ++it){
        unsigned int connId = it->first;
        m_connIdGen.back(connId);
        freeConn(it->second);
        SAFE_DELETE(it->second);
    }
    m_mapConn.clear();
}

void readCallback(struct bufferevent*pBufEv, void *pArg){
    if(nullptr == pArg)return;
    Conn* pConn = (Conn*)pArg;
    IOWorker* pWorker = pConn->pWorker;
    if(nullptr != pWorker){
        pWorker->handleRead(pConn);
    }
}

void IOWorker::handleRead(Conn * pConn){
    if(nullptr == pConn)return;

    bufferevent* pBufEv = pConn->pBufEv;
    if(nullptr == pBufEv)return;

    //获取输入的指针，进行读响应
    evbuffer* pInBuf = bufferevent_get_input(pBufEv);
    if(nullptr == pInBuf)return;

    //此时收到了数据，表示主观认为没有断开连接
    pConn->bConnetionMightLost = false;

    //进行服务器响应的回传读取
    while(true){
        if(PROTOCOL_HEAD == pConn->inState){
            //获取evbuffer的长度
            if(evbuffer_get_length(pInBuf)<HEAD_SIZE)break;
            //由于evbuffer中的数据可能分散在不连续的内存块，所以若需要获取字节数组，必须调用evbuffer_pullup()进行“线性化”
			//获取协议head字节数组
            unsigned char* pArr = evbuffer_pullup(pInBuf, HEAD_SIZE);
            if(nullptr == pArr)break;
            //将2-5转化为请求体长度
            pConn->inBodySize = *((bodySize_t*)(pArr + 1));
            //判断第一位，如果是PONG，则移除协议数据
            if(DATA_TYPE_PONG == pArr[0]){
                evbuffer_drain(pInBuf, HEAD_SIZE + pConn->inBodySize);
            }//若为一般返回，则读取协议体
            else{
                evbuffer_drain(pInBuf, HEAD_SIZE);
                pConn->inState = PROTOCOL_BODY;
            }
        }else if(PROTOCOL_BODY == pConn->inState){
            if(evbuffer_get_length(pInBuf) < pConn->inBodySize)break;
            unsigned char* pArr = evbuffer_pullup(pInBuf, pConn->inBodySize);

            if(nullptr!=pArr){
                //对响应体做反序列化
                ProtocolBodyResponse bodyResp;
                if(!bodyResp.ParseFromArray(pArr,pConn->inBodySize)){
                    evbuffer_drain(pInBuf, pConn->inBodySize);
                    pConn->inState = PROTOCOL_HEAD;
                    continue;
                }
                //通过调用id找到调用指针
                auto it = pConn->mapCall.find(bodyResp.callid());
                if(it!=pConn->mapCall.end()){
                    //调用指针
                    Call* pCall = it->second;
                    if(nullptr != pCall){
                        google::protobuf::Message *pRespMessage = pCall->pRespMessage;
                        if(nullptr != pRespMessage){
                            //反序列化响应，并返回用户调用
                            if(pRespMessage->ParseFromString(bodyResp.content())){
                                rpcCallback(pCall->pClosure, pCall->sync_write_fd);
                            }
                        }
                        pConn->idGen.back(pCall->callId);
                        pConn->mapCall.erase(it);
                        SAFE_DELETE(pCall);
                    }
                }
            }
            evbuffer_drain(pInBuf, pConn->inBodySize);
            pConn->inState = PROTOCOL_HEAD;
        }
        else{
            break;
        }
    }
}


void eventCallback(struct bufferevent*pBufEv, short event, void *pArg){
    //情况：连接上、eof、超时、其他错误
    auto del = [](Conn *pConn){
        if(nullptr == pConn)return;
        if(nullptr != pConn->pBufEv){
            bufferevent_free(pConn->pBufEv);
            pConn->pBufEv = nullptr;
        }
        for(auto it = pConn->mapCall.begin(); it!=pConn->mapCall.end(); ++it){
            SAFE_DELETE(it->second);
        }
        delete pConn;
    };
    if(nullptr == pArg)return;
    Conn* pConn = (Conn*)pArg;
    IOWorker* pWorker = pConn->pWorker;
    if(nullptr == pWorker){
        del(pConn);
        return ;
    }
    //判断是否已连接
    if(0 != (event & BEV_EVENT_CONNECTED)){
        //执行已连接的逻辑
        pWorker->handleConnected(pConn);
        return;
    }
    //判断是否超时，这个超时是定时发送PING信号
    if(0 != (event & BEV_EVENT_TIMEOUT)){
        //执行超时的逻辑
        pWorker->handleTimeout(pConn);
        return;
    }
    //建立连接
    pWorker->connect(pConn);
}

//如果是已连接的IO事件
void IOWorker::handleConnected(Conn * pConn){
    if(nullptr == pConn)return;
    pConn->bConnected = true;
    //设置超时时间发送PING心跳
    if(nullptr != pConn->pBufEv && m_heartbeatInterval.tv_sec >= 0){
        bufferevent_set_timeouts(pConn->pBufEv, &m_heartbeatInterval, NULL);
    }

    //处理可能还剩余的IO事件
    handleIOTask();
}

//定时发送PING心跳
void IOWorker::handleTimeout(Conn * pConn){
    if(nullptr == pConn)return;
    bufferevent* pBufEv = pConn->pBufEv;
    if(nullptr == pBufEv)return;
    //保证是连接状态
    if(pConn->bConnected && !pConn->bConnetionMightLost){
        //获取bufferevnet指针,要发送所以是out区
        evbuffer* pOutBuf = bufferevent_get_output(pBufEv);
        if(nullptr != pOutBuf){
            //创建ping协议头
            unsigned char arr[5] = {0};
            arr[0] = DATA_TYPE_PING;
            //放进发送池
            evbuffer_add(pOutBuf, arr, sizeof(arr));
            std::cout << "IO thread " << boost::this_thread::get_id() << " finishes sending PING heartbeat." << std::endl;
        }
        //然后暂时认为连接丢失，直到服务器回传PONG
        pConn->bConnetionMightLost = true;
    }else{
        //主观丢失，重新建立连接
        connect(pConn);
    }
    //重新使能可读事件
    bufferevent_enable(pBufEv, EV_READ);
}

//发起连接
bool IOWorker::connect(Conn* pConn){
    if(nullptr == pConn)return false;
    //先释放连接再重新建立
    freeConn(pConn);

    //创建连接的buffer；
    //暂时不获取文件描述符，稍后进行通过bufferevent_setfd 或 bufferevent_socket_connect()进行设置即可
    bufferevent* pBufEv = bufferevent_socket_new(m_pEvBase, -1, BEV_OPT_CLOSE_ON_FREE);
    if(nullptr == pBufEv )return false;
    pConn->pBufEv = pBufEv;
    //设置读回调和事件回调
    bufferevent_setcb(pBufEv, readCallback, NULL, eventCallback, pConn);
    //使能读事件
    bufferevent_enable(pBufEv, EV_READ);
    //申请一个文件描述符，并且建立和服务器的连接，服务器监听到就建立连接
    if(bufferevent_socket_connect(pBufEv, (sockaddr*)(&pConn->serverAddr),sizeof(pConn->serverAddr)) < 0)return false;
    std::cout<<"success connect"<<pConn->serverAddr.sin_port<<std::endl;
    //然后获取文件描述符
    pConn ->fd =bufferevent_getfd(pBufEv);
    //设置文件描述符非阻塞
    evutil_make_socket_nonblocking(pConn ->fd);
    return true;
}

//释放一个连接
void IOWorker::freeConn(Conn * pConn){
    if(nullptr == pConn)return;
    pConn->bConnected = false;
    //释放缓冲区
    if(nullptr != pConn->pBufEv){
        bufferevent_free(pConn->pBufEv);
        pConn->pBufEv = nullptr;
    }

    //销毁连接上的所有调用
    for(auto it = pConn->mapCall.begin();it != pConn->mapCall.end(); ++it){
        callid_t callId = it->first;
        pConn->idGen.back(callId);
        SAFE_DELETE(it->second);
    }
    pConn->mapCall.clear();
}





//生成一个连接
Conn * IOWorker::genConn(){
    unsigned int connId;
    if(!m_connIdGen.generate(connId))return nullptr;
    Conn* pConn = new Conn();
    pConn->connId = connId;
    m_mapConn[connId] = pConn;
    return pConn;
}

unsigned int IOWorker::getBusyLevel(){
    return m_mapConn.size();
}

evutil_socket_t IOWorker::getNotify_fd(){
    return m_notify_fd;
}

void IOWorker::rpcCallback(google::protobuf::Closure * pClosure, evutil_socket_t sync_write_fd){
    std::cout<<"调用回调"<<std::endl;
    if (nullptr != pClosure)
		//异步回调用户函数
		pClosure->Run();
	else
	{
		//唤醒同步等待中的用户调用线程
		char buf[1];
		send(sync_write_fd, buf, 1, 0);
	}
}