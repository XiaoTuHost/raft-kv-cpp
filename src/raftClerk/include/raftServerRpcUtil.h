#pragma once
#ifndef __RAFTSERVERRPCUTIL__H__
#define __RAFTSERVERRPCUTIL__H__

#include <iostream>
#include "kvServerRPC.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"


class RaftServerRpcUtil{
    public:
        RaftServerRpcUtil(std::string ip,short port);
        ~RaftServerRpcUtil();
        
        bool Get(raftKVRpcProtoc::GetArgs* args,raftKVRpcProtoc::GetReply* reply);
        bool PutAppend(raftKVRpcProtoc::PutAppendArgs* args,raftKVRpcProtoc::PutAppendReply* reply);
    private:
        // stub是存根客户端
        // 用于代理RpcChannel中的CallMethod方法
        raftKVRpcProtoc::kvServerRpc_Stub* stub;
};

#endif