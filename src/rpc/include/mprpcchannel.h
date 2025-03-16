#pragma once
#ifndef __MPRPCCHANNEL__H__
#define __MPRPCCHANNEL__H__

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <algorithm>
#include <algorithm>  // 包含 std::generate_n() 和 std::generate() 函数的头文件
#include <functional>
#include <iostream>
#include <map>
#include <random>  // 包含 std::uniform_int_distribution 类型的头文件
#include <string>
#include <unordered_map>
#include <vector>

// RPC 通信通道

class MprpcChannel : public google::protobuf::RpcChannel{
    public:

        MprpcChannel(const std::string& ip,const short& port,bool connectNow);

        // 所有通过stub代理对象调用的rpc方法，都走到这里了
        // 统一做rpc方法调用的数据数据序列化和网络发送 那一步
        void CallMethod(const google::protobuf::MethodDescriptor *method, 
            google::protobuf::RpcController *controller,
            const google::protobuf::Message *request, 
            google::protobuf::Message *response,
            google::protobuf::Closure *done) override;

    private:
        int m_clientFd;
        const std::string ip;
        const uint16_t port;
        bool newConnect(const char* ip,short port,std::string& errMsg);
};

#endif