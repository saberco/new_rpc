#ifndef __GLOBAL_H__
#define __GLOBAL_H__

//安全删除指针
#define SAFE_DELETE(p) { if (nullptr != (p)) { delete (p); (p) = nullptr; } }

/************************************************************************
Rpc通信协议结构：head + body
1、head：长度为5字节
	（1）第1字节：数据类型，见下面DATA_TYPE的定义
	（2）第2-5字节：body的长度
2、body
	（1）若数据类型为心跳（PING、PONG），body长度为0字节
	（2）若数据类型为请求或响应，body内容见ProtocolBody.proto
************************************************************************/

// 协议头长度
#define HEAD_SIZE 5
// 定义两个别名
typedef uint32_t bodySize_t
typedef uint32_t callid_t

// 协议的枚举，头，体
enum PROTOCOL_PART{
    PROTOCOL_HEAD = 0,
    PROTOCOL_BODY = 1
};

// 协议的数据类型
enum DATA_TYPE{
    // 客户端发给服务器的请求
    DATA_TYPE_REQUEST = 0,
    // 服务端发给客户端的响应
    DATA_TYPE_RESPONSE = 1,
    // PING心跳，客户端发给服务端确认服务端在线
    DATA_TYPE_PING = 2,
    // PONG心跳，服务端发给客户端确认客户端在线
    DATA_TYPE_PONG = 3

};





#endif