#pragma once
#ifndef __RPCPROVIDER__H__
#define __RPCPROVIDER__H__

#include <google/protobuf/descriptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>
#include <functional>
#include <string>
#include <unordered_map>
#include "google/protobuf/service.h"

// 这里是rpc服务端
// 框架提供的专门发布rpc服务的网络对象类
// TODO 现在rpc客户端变成了 长连接
// rpc服务器这边最好提供一个定时器，用以断开很久没有请求的连接。
class RpcProvider{
    public:
        // 发布rpc方法、注册到注册中心zookeeper
        void NotifyService(google::protobuf::Service *service);

        // 启动RPC服务
        void Run(int nodeIndex,short port);

        ~RpcProvider();

    private:
        muduo::net::EventLoop m_loop;
        std::shared_ptr<muduo::net::TcpServer> m_muduo_server;
        struct ServiceInfo{
            google::protobuf::Service* m_service;   // 保存服务、基类指针
            // 方法名 ---> 方法描述 --->调用具体方法
            std::unordered_map<std::string,google::protobuf::MethodDescriptor*> m_method;
        };
        // 服务名 ---> 服务信息 ---> 方法调用...
        std::unordered_map<std::string,ServiceInfo> m_serviceMap;

        // muduo库回调解耦
        // 解耦应用层和传输层
        void OnConnection(const muduo::net::TcpConnectionPtr&);
        void OnMessage(const muduo::net::TcpConnectionPtr&,muduo::net::Buffer&,muduo::Timestamp);

        // Closure Rpc方法的回调函数 完成调用后用于响应客户端
        void SendRpcResponse(const muduo::net::TcpConnectionPtr&,google::protobuf::Message*);
};

#endif