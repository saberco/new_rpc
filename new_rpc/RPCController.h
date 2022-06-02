#ifndef __RPCCONTROLLER_H__
#define __RPCCONTROLLER_H__

#include <string>
#include <google/protobuf/service.h>


//主要是进行错误信息的设置等

class RpcController : public google::protobuf::RpcController
{
public:
	RpcController();
	virtual void Reset();
	virtual bool Failed() const;
	virtual std::string ErrorText() const;
	virtual void StartCancel();
	virtual void SetFailed(const std::string& reason);
	virtual bool IsCanceled() const;
	virtual void NotifyOnCancel(google::protobuf::Closure* callback);

private:
	std::string m_strError;
	bool m_bFailed;
};









#endif