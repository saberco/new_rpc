#ifndef __IRPCCLIENT_H__
#define __IRPCCLIENT_H__


//Rpc客户端接口类，抽象类
class IRpcClient
{  
public:
    //抽象类不用提供构造
    //创建客户端实例
    //主要参数有ioworker对象熟练，一个对象工作队列大小，和心跳周期
    static IRpcClient* createRpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval);

    //销毁客户端实例
    static void releaseRpcClient(IRpcClient* pIRpcClient);

    //启动客户端
    virtual void start()=0;
    
    //结束客户端
    virtual void stop()=0;

    //析构
    virtual ~IRpcClient();
};






#endif