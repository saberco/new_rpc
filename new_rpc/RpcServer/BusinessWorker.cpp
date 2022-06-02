#include "BusinessWorker.h"
#include "IOWorker.h"


BusinessWorker::BusinessWorker(unsigned int queueMaxSize){
    m_bEnded = false;
    m_pMapRegisteredService = nullptr;
    m_queue.setMaxSize(queueMaxSize);
    m_queue.setWait(true);
}

BusinessWorker::~BusinessWorker(){
    m_bEnded = true;
    m_queue.stop();
    m_thd.join();
}

void BusinessWorker::setRegisteredServices(const std::map<std::string,std::pair<google::protobuf::Service*,std::vector<const google::protobuf::MethodDescriptor*>>>* pMapRegisteredService){
    m_pMapRegisteredService = pMapRegisteredService;
}
//启动线程，这里应用到了boost的boost::thread的线程创建方法
//调用一般函数第一个参数为函数名，此后参数为函数参数
//调用类中函数第一个参数为类成员函数的引用，此后先传入this指针，再传入函数的参数
void BusinessWorker::start(){
    m_thd = std::move(boost::thread(&BusinessWorker::threadMain, this));
}

unsigned int BusinessWorker::getBusyLevel(){
    return m_queue.getSize();
}

//线程工作的函数
void BusinessWorker::threadMain(){
    //一次性将阻塞队列中的业务任务取出，因为阻塞队列是临界区资源，但是queue不是，queue不会出现线程同步问题，因为只有一个线程
    while (!m_bEnded){
        std::list<BusinessTask *> queue;
        m_queue.takeAll(queue);
        //然后进行遍历操作
        for(auto it = queue.begin(); it!=queue.end(); ++it){
            std::cout<<"Business thread "<<boost::this_thread::get_id()<<" begins dealing task......"<<std::endl;
            //取一个业务指针
            BusinessTask* pTask = *it;
            //绑定业务任务指针，自定义删除器，在指针析构时，自动执行通知IOWorker
            std::unique_ptr<BusinessTask, std::function<void(BusinessTask*)> > ptrMonitor(pTask, [](BusinessTask* pTask){
                if(nullptr == pTask)return;
                if(nullptr == pTask->pWorker)return;
                //将业务任务放入IOWoker的write队列，放入不成功就等待
                while(!pTask->pWorker->m_writeQueue.put(pTask)){}
                //通知相应的IOWorker,m_writeQueue有数据
                char buf[1] = {NOTIFY_IOWORKER_WRITE};
                send(pTask->pWorker->getNotify_fd(), buf, 1, 0);
            });
            //空不处理
            if(nullptr == pTask)continue;
            if(nullptr == pTask->pWorker)continue;
            //判断注册表中是否有值
            if(nullptr == m_pMapRegisteredService){
                //释放缓存,提醒io处理在智能指针中
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //获取协议的体长度
            bodySize_t len = evbuffer_get_length(pTask->pBuf);
            //反序列化传来的请求
            ProtocolBodyRequest bodyReq;
            //将缓冲区的协议体从链式数据变为顺序数据，然后通过protocol进行反序列化
            //反序列化的信息保存在bodyReq中
            if(!bodyReq.ParseFromArray(evbuffer_pullup(pTask->pBuf, len), len)){
                //如果反序列化出错
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //通过服务名找服务信息
            auto itFind = m_pMapRegisteredService->find(bodyReq.servicename());
            if(itFind == m_pMapRegisteredService->end()){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }

            //获取服务指针
            google::protobuf::Service* pService = itFind->second.first;
            //获取方法标识
            uint32_t index = bodyReq.methodindex();
            if(nullptr == pService || index >= itFind->second.second.size()){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //动态生成message，首先生成message的descriptor，再根据descriptor生成一个massage类，message类再new出一个对象
            //获取方法描述指针
            //map中vector存的就是方法描述指针
            const google::protobuf::MethodDescriptor* pMethodDescriptor = itFind->second.second[index];
            if(nullptr == pMethodDescriptor ){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //创建方法传入Message
            google::protobuf::Message *pReq = pService->GetRequestPrototype(pMethodDescriptor).New();
            if(nullptr == pReq){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //绑定为只能指针，让创建的方法Message自动销毁
            std::unique_ptr<google::protobuf::Message> ptrReq(pReq);
            //创建出参即响应
            google::protobuf::Message *pResp = pService->GetResponsePrototype(pMethodDescriptor).New();
            if(nullptr == pResp ){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            std::unique_ptr<google::protobuf::Message> ptrResp(pResp);
            //入参反序列化
            if(!pReq->ParseFromString(bodyReq.content())){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }

            //调用服务器的方法
            RpcController controller;
            //实现在rpcchannel中
            pService->CallMethod(pMethodDescriptor, &controller, pReq, pResp, NULL);

            //构造响应的协议
            ProtocolBodyResponse bodyResp;
            bodyResp.set_callid(bodyReq.callid());
            //序列化出参
            std::string content;
            if(!pResp->SerializeToString(&content)){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }
            //将序列化的出参进行设置
            bodyResp.set_content(content);
            //然后序列化整个proto响应
            std::string strRet;
            if(!bodyResp.SerializeToString(&strRet)){
                evbuffer_free(pTask->pBuf);
                pTask->pBuf = nullptr;
                continue;
            }   
            evbuffer_free(pTask->pBuf);
            //创建evbuffer，然后将序列化好的响应放入
            pTask->pBuf = evbuffer_new();
            if(nullptr==pTask->pBuf){
                continue;
            }
            //将序列化的响应放入evbuffer,供IOWorker处理
            evbuffer_add(pTask->pBuf, strRet.c_str(), strRet.size());

            std::cout<<"Business thread "<<boost::this_thread::get_id()<<" finishes handling a task"<<std::endl;
        }
    }
}
