#include "clerk.h"

#include "raftServerRpcUtil.h"

#include "util.h"

#include <string>
#include <vector>

Clerk::Clerk()
    : m_clientId(uuid())
    , m_requestId(0)
    , m_recentLeader(0)
{
    
}
        
/*

获取rpc配置文件
保存raft节点的rpc客户端
建立通信通道

*/
void Clerk::Init(std::string configFileName){
    // 获取所有raft节点ip和port并连接
    MprpcConfig config;
    config.LoadConfigFile(configFileName.c_str());
    std::vector<std::pair<std::string,short>> ipPortVec;
    for(int i=0;i<INT_MAX-1;++i){
        std::string node = "node" + std::to_string(i);
        std::string nodeIp = config.Load(node+"ip");
        std::string nodePort = config.Load(node+"port");
        if(nodeIp.empty())
            break;
        ipPortVec.emplace_back(nodeIp,atoi(nodePort.c_str()));
    }

    // 存储rpc客户端
    for(const auto&item:ipPortVec){
        std::string ip = item.first;
        short port = item.second;
        auto* rpc = new RaftServerRpcUtil(ip,port);

        m_servers.emplace_back(rpc);
    }
}

std::string Clerk::Get(std::string key){
    
    // 先构造参数
    m_requestId++;
    auto requestId = m_requestId;
    int server = m_recentLeader;
    raftKVRpcProtoc::GetArgs args;
    args.set_key(key);
    args.set_clientid(m_clientId);
    args.set_requestid(requestId);

    // TODO 线性一致性如何保证
    // 发送请求
    // 直至发送成功
    while(1){
        raftKVRpcProtoc::GetReply reply;
        bool ok = m_servers[server]->Get(&args,&reply);
        // 响应结果
        // err消息参考utils中的定义的字符串
        if(!ok || reply.err()==ErrWrongLeader){
            server = (server+1)%m_servers.size();
            continue;
        }else if( reply.err()==ErrNoKey){
            return "";
        }
        if(reply.err()==OK){
            m_recentLeader = server;
            return reply.value();
        }
    }
    return "";
}

void Clerk::Put(std::string key,std::string value){
    PutAppend(key,value,"Put");
}

void Clerk::Append(std::string key,std::string value){
    PutAppend(key,value,"Append");
}

void Clerk::PutAppend(std::string key,std::string value,std::string op){
    
    // 构造请求参数
    m_requestId++;
    auto server = m_recentLeader;
    raftKVRpcProtoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(m_clientId);
    args.set_requestid(m_requestId);

    while(true){
        raftKVRpcProtoc::PutAppendReply reply;
        bool ok = m_servers[server]->PutAppend(&args,&reply);
        if(!ok || reply.err()==ErrWrongLeader){
            DPrintf("【Clerk::PutAppend】原以为的leader：{%d}请求失败，向新leader{%d}重试  ，操作：{%s}", server, server + 1,
                op.c_str());
            if(!ok){
                DPrintf("Rpc调用失败，请重试");
            }
            if(reply.err()==ErrWrongLeader){
                DPrintf("{%d}不是Leader",server);
            }
            server = (server+1)%m_servers.size();
            continue;
        }
        if(reply.err()==OK){
            m_recentLeader = server;
            return;
        }
    }

}
