#include "IOWorker.h"


IOWorker::IOWorker(evutil_socket_t * fds, unsigned int acceptQueueMaxSize, unsigned int writeQueueMaxSize, const std::vector<BusinessWorker*>* pVecBusinessWorker){
    m_notified_fd = fds[0];
    m_notify_fd = fds[1];
    m_pVecBusinessWorker = pVecBusinessWorker;
    m_bStopped = false;
    m_bStarted = false;
    m_connNum = 0;
    m_pEvBase = nullptr;
    m_acceptQueue.setMaxSize(acceptQueueMaxSize);
    m_writeQueue.setMaxSize(writeQueueMaxSize);

}

//析构结束循环
IOWorker::~IOWorker(){
    char buf[1] = {NOTIFY_IOWORKER_END};
    //告诉IO结束循环
    send(m_notify_fd, buf, 1, 0);
    //结束线程
    m_thd.join();
    //关闭读端描述符
    evutil_closesocket(m_notified_fd);
}

//启动
void IOWorker::start(){
    //不能重复启动
    if(m_bStarted)return;
    m_bStarted = true;
    
    //启动线程
    m_thd = std::move(boost::thread(&IOWorker::threadMain, this, m_notified_fd));
}

void IOWorker::threadMain(evutil_socket_t notifiedfd){
    //创建event_base
    event_base *pEvBase = event_base_new();
    if(nullptr == pEvBase){
        evutil_closesocket(notifiedfd);
        return;
    }
    //将创建的event_base和智能指针绑定，让其自动销毁
    std::unique_ptr<event_base, std::function<void(event_base*)>> ptrEvBase(pEvBase, [&notifiedfd](event_base *p){
        event_base_free(p);
        //关闭读端
        evutil_closesocket(notifiedfd);
    });

    //创建socketpair上的读端的可读事件,接收到数据触发读事件回调函数，并将其持续化挂起
    event* pNotifiedEv = event_new(pEvBase, notifiedfd, EV_READ|EV_PERSIST, notifiedCallback, this);
    if(nullptr == pNotifiedEv)return;

    //事件和智能指针绑定
    std::unique_ptr<event, std::function<void(event*)> > ptrNotifiedEv(pNotifiedEv, event_free);

    //设置读端监听，即还未处理
    //添加入I/O多路复用并加到已注册事件链表
    if(0 != event_add(pNotifiedEv, NULL))return;

    m_pEvBase = pEvBase;
    //启动事件循环
    event_base_dispatch(pEvBase);
}

void notifiedCallback(evutil_socket_t fd, short event, void * pArg){
    if(nullptr == pArg)return;
    //通过接收到的通知类型，判断是调用哪个函数
    char buf[1];
    recv(fd, buf, 1, 0);
    IOWorker* pWorker = (IOWorker*)pArg;
    //和消息比较判断
    if (NOTIFY_IOWORKER_ACCEPT == buf[0])
		pWorker->handleAccept();
	else if (NOTIFY_IOWORKER_WRITE == buf[0])
		pWorker->handleWrite();
	else if (NOTIFY_IOWORKER_END == buf[0])
		pWorker->handleEnd();
}

void IOWorker::handleAccept(){
    //首先判断是否有基
    if(nullptr == m_pEvBase)return;
    //一次性取出所有accept的文件描述符
    std::list<evutil_socket_t> queue;
    m_acceptQueue.takeAll(queue);
    //进行遍历处理
    for( auto it = queue.begin();it!=queue.end();++it){
        //为每个接收到连接请求的文件描述符创建eventbuffer
        //BEV_OPT_CLOSE_ON_FREE释放bufferevent时关闭底层传输端口。这将关闭底层套接字，释放底层bufferevent等。
        bufferevent* pBufEv = bufferevent_socket_new(m_pEvBase, *it, BEV_OPT_CLOSE_ON_FREE);
        if(nullptr == pBufEv)continue;

        //创建连接信息
        Conn* pConn = new Conn();
        pConn->fd = *it;
        pConn->pBufEv = pBufEv;
        pConn->pWorker = this;
        //保存信息到服务端
        m_mapConn[*it] = pConn;
        //连接数加一
        ++m_connNum;

        //设置bufferevent的回调函数
        //最后一位是提供给回调函数的参数
        bufferevent_setcb(pBufEv, readCallback, NULL, eventCallback, pConn);
        //使得能够读取bufferevent上的事件
        bufferevent_enable(pBufEv,  EV_READ);

        std::cout << "IO thread " << boost::this_thread::get_id() << " begins serving connnection " << pConn->fd << "..." <<std::endl;
    }
}

//写响应
void IOWorker::handleWrite(){
    //同样，读取所有的写任务
    std::list<BusinessTask*> queue;
    m_writeQueue.takeAll(queue);
    //遍历
    for(auto it = queue.begin();it!=queue.end();++it){
        BusinessTask* pTask = *it;
        Conn* pConn = nullptr;

        //同样绑定指针，自动释放连接和销毁资源
        //lambda捕获可以实现编译时多态以及访问调用lambda表达式的调用者无法访问的变量
        std::unique_ptr<BusinessTask, std::function<void(BusinessTask*)>> ptrTask(pTask, [this, &pConn](BusinessTask* pTask){
            if(nullptr == pTask)return;
            if(nullptr != pTask->pBuf){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
            }
            SAFE_DELETE(pTask);
            checkToFreeConn(pConn);
        });

        if(nullptr == pTask)continue;
        //找连接的描述符
        auto itFind = m_mapConn.find(pTask->fd);
        pConn = itFind->second;
        if(nullptr == pConn)continue;
        //当缓冲区没有需要处理的数据或者连接不合法
        if(nullptr == pTask->pBuf || !pConn->bValid || nullptr == pConn->pBufEv){
            --(pConn->todoCount);
            continue;
        }
        //获取输出的evbuffer，指向写缓冲的数据
        evbuffer *pOutBuf = bufferevent_get_output(pConn->pBufEv);
        if(nullptr == pOutBuf){
            --(pConn->todoCount);
            continue;
        }

        //添加协议头长度5数组初始化为0
        unsigned char arr[HEAD_SIZE] = {0};
        //第1位是说明自己是响应
        arr[0] = DATA_TYPE_RESPONSE;
        bodySize_t bodyLen = evbuffer_get_length(pTask->pBuf);
        //第2到第5位是响应体的长度uint32,4个字节数据
        memcpy(arr+1, &bodyLen, HEAD_SIZE-1);
        //将协议头放入输出缓冲
        evbuffer_add(pOutBuf, arr, HEAD_SIZE);
        //然后将序列化的协议体放入输出缓冲
        evbuffer_remove_buffer(pTask->pBuf, pOutBuf, evbuffer_get_length(pTask->pBuf));
        //处理完毕
        --(pConn->todoCount);
    }
}

//处理结束
void IOWorker::handleEnd(){
    //不能重复结束
    if(m_bStopped)return;
    m_bStopped = true;
    //退出事件循环
    if(m_mapConn.empty() && nullptr != m_pEvBase){
        event_base_loopexit(m_pEvBase, NULL);
    }
}

//触发读事件后的回调
void readCallback(bufferevent * pBufEv, void * pArg){
    if(nullptr == pBufEv || nullptr == pArg){
        return;
    }
    IOWorker* pWorker = ((Conn*)pArg)->pWorker;
    if(nullptr != pWorker){
        //调用读事件处理
        pWorker->handleRead((Conn*)pArg);
    }
}

//读事件处理
void IOWorker::handleRead(Conn * pConn){
    if(nullptr == pConn)return;
    //获取输入（读）缓冲区指针
    bufferevent* pBufEv = pConn->pBufEv; 
    if(nullptr == pBufEv)return;
    //获取输入（读）缓冲区指针
    evbuffer* pInBuf = bufferevent_get_input(pBufEv);
    if(nullptr == pInBuf)return;
    //由于TCP协议是一个字节流的协议，所以必须依靠应用层协议的规格来区分协议数据的边界
    //进行读事件的处理，处理完毕break
    while(true){
        //读到请求头
        if(PROTOCOL_HEAD == pConn->inState){
            //获取输入的evbuff长度
            if(evbuffer_get_length(pInBuf) < HEAD_SIZE)break;

            //由于evbuffer可能是链式结构，要读数组必须进行线性化
            unsigned char *pArr = evbuffer_pullup(pInBuf, HEAD_SIZE);
            if(nullptr == pArr)break;
            //将2-5位转化为长度信息
            pConn->inBodySize = *((bodySize_t*)(pArr + 1));
            //通过第一位判断请求类型，是请求，还是PING
            //如果是PING，直接返回PONG
            if(DATA_TYPE_PING == pArr[0]){
                //获取输出缓冲区指针
                evbuffer* pOutBuf = bufferevent_get_output(pBufEv);
                if(nullptr != pOutBuf){
                    //创建协议头是PONG心跳的协议，体长度为0
                    unsigned char arr[HEAD_SIZE]={0};
                    arr[0] = DATA_TYPE_PONG;
                    //放入输出池中
                    evbuffer_add(pOutBuf, arr, sizeof(arr));
                    std::cout << "IO thread " << boost::this_thread::get_id() << " finishes replying PONG heartbeat." << std::endl;
                }
                //处理完成后移除读缓冲区的数据
                evbuffer_drain(pInBuf, HEAD_SIZE + pConn->inBodySize);
            }else{
                //如果是其他请求类型，则读取请求体
                evbuffer_drain(pInBuf, HEAD_SIZE);
                pConn->inState = PROTOCOL_BODY;
            }
        }else if(PROTOCOL_BODY == pConn->inState){
            //如果进入了请求体处理
            //获取长度
            if(evbuffer_get_length(pInBuf) < pConn->inBodySize)break;

            //业务来咯
            BusinessTask *pTask = new BusinessTask();
            pTask->pWorker = this;
            pTask->fd = pConn->fd;

            //创建新的缓冲区来移动evbuffer的内容
            pTask->pBuf = evbuffer_new();
            if(nullptr != pTask->pBuf){
                //调度业务处理请求，IOWorker只负责IO，业务类负责处理具体的业务
                BusinessWorker* pSelectedWorker = schedule();
                if(nullptr != pSelectedWorker){
                    //转移缓冲区到业务类
                    evbuffer_remove_buffer(pInBuf, pTask->pBuf, pConn->inBodySize);
                    //然后将任务指针放入任务队列中
                    if(pSelectedWorker->m_queue.put(pTask)){
                        ++(pConn->todoCount);
                        evbuffer_drain(pInBuf, pConn->inBodySize);
                        pConn->inState = PROTOCOL_HEAD;
                        continue;
                    }
                    //未成功删除
                    SAFE_DELETE(pTask);
                }else{
                    evbuffer_free(pTask->pBuf);
                    SAFE_DELETE(pTask);
                }
            }else{
                SAFE_DELETE(pTask);
            }
            evbuffer_drain(pInBuf, pConn->inBodySize);
			pConn->inState = PROTOCOL_HEAD;   
        }else{
            break;
        }
    }
}


void eventCallback(bufferevent * pBufEv, short event, void * pArg){
    //事件1.连上2.eof，3.超时，4.其他错误
    if(nullptr == pBufEv || nullptr == pArg)return;
    IOWorker* pWorker = ((Conn*)pArg)->pWorker;
    if(nullptr != pWorker){
        //调用事件处理函数
        pWorker->handleEvent((Conn*)pArg);
    }
}

void IOWorker::handleEvent(Conn * pConn){
    if(nullptr == pConn) return;
    pConn->bValid = false;
    checkToFreeConn(pConn);
}

//从任务处理池中调度一个来处理业务
BusinessWorker * IOWorker::schedule(){
    //建一个回传
    BusinessWorker *pSelectedWorker = nullptr;
    //繁忙程度
    unsigned int minBusyLevel;

    //选出第一个不为空的
    auto it = m_pVecBusinessWorker->begin();
    for(; it!=m_pVecBusinessWorker->end();++it){
        BusinessWorker*pWorker = *it;
        if(nullptr != pWorker){
            pSelectedWorker = pWorker;
            //越不忙的业务越容易被选中
            minBusyLevel = pSelectedWorker->getBusyLevel();
            break;
        }
    }

    if(nullptr == pSelectedWorker || minBusyLevel <=0)return pSelectedWorker;

    //接着选出回传的指针
    unsigned int busyLevel;
    for(++it;it!=m_pVecBusinessWorker->end();++it){
        BusinessWorker*pWorker = *it;
        if(nullptr != pWorker){
            busyLevel = pWorker->getBusyLevel();
            if(busyLevel < minBusyLevel){
                pSelectedWorker = pWorker;
                minBusyLevel = busyLevel;
                //如果完全空闲立即选取
                if(minBusyLevel <= 0)break;
            }
        }
    }
    return pSelectedWorker;
}

bool IOWorker::checkToFreeConn(Conn * pConn){
    //这个tmp没有意义，只是为了让其销毁的时候如果满足条件则执行智能指针删除器的内容
    int tmp;
    std::unique_ptr<int, std::function<void(int*)>> ptrMonitor(&tmp, [this](int* p){
        if(m_bStopped && m_mapConn.empty() && nullptr != m_pEvBase){
            event_base_loopexit(m_pEvBase, NULL);
        }
    });

    if(nullptr == pConn){
        return false;
    }
    //如果没有尚未处理的请求，则销毁
    if(!pConn->bValid && pConn->todoCount <= 0 && nullptr != pConn->pBufEv){
        auto it = m_mapConn.find(pConn->fd);
        if(it != m_mapConn.end()){
            m_mapConn.erase(it);
        }

        bufferevent_free(pConn->pBufEv);
        SAFE_DELETE(pConn);
        return true;
    }
    return false;
}

unsigned int IOWorker::getBusyLevel(){
    return m_connNum;
}

evutil_socket_t IOWorker::getNotify_fd(){
    return m_notify_fd;
}