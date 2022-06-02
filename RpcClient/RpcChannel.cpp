#include "RpcChannel.h"


IRpcChannel::~IRpcChannel(){}


IRpcChannel * IRpcChannel::createRpcChannel(IRpcClient * pClient, const std::string & ip, int port){
    return new ::RpcChannel((RpcClient *)pClient, ip, port);
}

void IRpcChannel::releaseRpcChannel(IRpcChannel * pIRpcChannel){
    SAFE_DELETE(pIRpcChannel);
}

RpcChannel::RpcChannel(RpcClient* pClient,const std::string& ip, int port){
    m_pClient = pClient;
    m_pWorker = nullptr;
    m_ip = ip;
    m_port = port;
    m_bConnected = false;
    m_bSyncVaild = false;

    //创建同步调用的sockpair
    evutil_socket_t fds[2];
    //一定要注意linux下只能传AF_UNIX代替AF_INET
    int ret = evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    std::cout<<ret<<std::endl;
    if(ret >= 0){
        //同步默认阻塞
        m_sync_read_fd = fds[0];
        m_sync_write_fd = fds[1];
        m_bSyncVaild = true;
    }

}

RpcChannel::~RpcChannel(){
    //首先关闭同步调用文件描述符
    if(m_bSyncVaild){
        evutil_closesocket(m_sync_read_fd);
        evutil_closesocket(m_sync_write_fd);
    }

    //其次通知IOWorker断开连接
    if(m_bConnected && nullptr != m_pWorker){
        IOTask task;
        task.type = IOTask::DISCONNECT;
        task.pData = new unsigned int(m_connId);
        m_pWorker->m_queue.put(task);
        char buf[1] = {NOTIFY_IOWORKER_IOTASK};
        send(m_pWorker->getNotify_fd(), buf, 1, 0);
    }

}

void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done)
{
    //具体调用方式
    if(nullptr != controller){
        controller->Reset();
    }
    if(nullptr == method ){
        if(nullptr != controller){
            controller->SetFailed("method == NULL");
        }
        return;
    }
    if(nullptr ==  request ){
        if(nullptr != controller){
            controller->SetFailed("request == NULL");
        }
        return;
    }
    if(nullptr ==  response ){
        if(nullptr != controller){
            controller->SetFailed("response == NULL");
        }
        return;
    }
    if(!m_bSyncVaild){
        if(nullptr != controller){
            controller->SetFailed("socketpair created failed");
        }
        return;
    }






    if(nullptr == m_pClient){
        if(nullptr != controller){
            controller->SetFailed("RpcClient object does not exist");
        }
        return;
    }
    //如果未发起连接，则发起连接
    if(!m_bConnected){
        Conn* pConn= nullptr;
        //让客户端调度IOWorker池中的对象，分配一个连接,这里pConn是传出参数
        m_pWorker = m_pClient->schedule(pConn);
        if(nullptr == m_pWorker){
            //调度失败
            if(nullptr != controller){
                controller->SetFailed("connection refused");
            }
            return;
        }
        m_connId = pConn->connId;
        //通知IOWorker对服务器发起连接
        pConn->pWorker = m_pWorker;
        //创建要连接的服务器地址
        pConn->serverAddr.sin_family = AF_INET;
        //字节序转化为网络字节序
        if(0 == evutil_inet_pton(AF_INET, m_ip.c_str(), &(pConn->serverAddr.sin_addr)))return;

        pConn->serverAddr.sin_port = htons(m_port);

        //通知IOWorker对服务器发起连接
        IOTask task;
        task.type = IOTask::CONNECT;
        task.pData = pConn;
        //放入工作队列
        m_pWorker->m_queue.put(task);
        char buf[1] = {NOTIFY_IOWORKER_IOTASK};
        send(m_pWorker->getNotify_fd(), buf, 1, 0);
        m_bConnected = true;
    }
    //执行已连接后的逻辑
    //序列化请求参数
    std::string *pStr = new std::string();
    if(!request->SerializeToString(pStr)){
        if(nullptr != controller){
            controller->SetFailed("request serialized failed");
        }
        SAFE_DELETE(pStr);
        return;
    }
    //建立调用
    Call* pCall = new Call();
    pCall->connId = m_connId;
    pCall->sync_write_fd = m_sync_write_fd;
    pCall->pStrReq = pStr;
    pCall->pRespMessage = response;
    pCall->pClosure = done;
    pCall->pController = controller;
    pCall->serviceName = method->service()->full_name();
    pCall->methodIndex = method->index();

    IOTask task;
    task.type = IOTask::CALL;
    task.pData = pCall;

    m_pWorker->m_queue.put(task);
    char buf[1] = {NOTIFY_IOWORKER_IOTASK};
    send(m_pWorker->getNotify_fd(), buf, 1,0);
    //若为同步调用，通过读阻塞的读端描述符使本线程阻塞，直到调用返回时向写端描述符写入
	//若为异步调用，直接返回，调用返回时会调用回调函数
    if (nullptr == done)
	{
		char buf[1];
		recv(m_sync_read_fd, buf, 1, 0);
	}
}



