#pragma once
#ifndef __CLERK__H__
#define __CLERK__H__

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <vector>
#include <random>
#include "raftServerRpcUtil.h"
#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"

/*

Clerk是一个外部客户端，向整个raft集群发出命令，依赖于rpc通信接口

*/

class Clerk{
    public:
        Clerk();
        // 读取配置文件，初始化 Raft 节点连接。
        void Init(std::string configFileName);

        std::string Get(std::string key);
        void Get(std::string key,std::string value);
        void Append(std::string key,std::string value);

    private:
        // TODO
        // 保存所有raft节点的fd
        
        std::string m_clientId;
        int m_requestId;
        int m_recentLeader;

        // uuid 返回随机的clientfd
        std::string uuid(){
            std::random_device rd;
            std::mt19937 gen(rd);
            std::uniform_int_distribution<> dis(0,9999);
            return std::to_string(dis(gen)) + "-" + std::to_string(time(nullptr));
        }
        
        void PutAppend(std::string key,std::string value,std::string op);

};

#endif