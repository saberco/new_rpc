#ifndef __BUSINESSWORKER_H__
#define __BUSINESSWORKER_H__

#include "RpcServerGlobal.h"

//业务处理类，主要是序列化，反序列化和方法调用
//负责从缓冲区取出序列化的数据进行反序列化，而后进行方法的调用，并将调用的结果序列化放入缓冲区
class BusinessWorker{
public:
    //构造函数,设置工作队列的最大长度
    BusinessWorker(unsigned int queueMaxSize);
    //析构
    ~BusinessWorker();
    //设置注册的服务
    void setRegisteredServices(const std::map<std::string, std::pair<google::protobuf::Service*, std::vector<const google::protobuf::MethodDescriptor*> > > *pMapRegisteredService);
    //启动业务
    void start();
    //获取业务的繁忙程度，主要是靠工作队列长度体现
    unsigned int getBusyLevel();

    //业务工作队列
    SyncQueue<BusinessTask*> m_queue;

private:
    //线程主方法，由内部线程m_thd进行访问
    void threadMain();
    //包含一个线程
    boost::thread m_thd;
    //是否结束线程
    bool m_bEnded;
    //所有注册的服务指针
    const std::map<std::string, std::pair<google::protobuf::Service*, std::vector<const google::protobuf::MethodDescriptor*> > > *m_pMapRegisteredService;
};







#endif