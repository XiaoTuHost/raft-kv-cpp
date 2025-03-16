#pragma once
#ifndef __RAFTRPCUTIL__H__
#define __RAFTRPCUTIL__H__

#include "raftRPC.pb.h"

/*

不得不说，C++使用一个他人的库简直麻烦的要死
又要下载又要编译又要配置。就这些步骤烦都把人烦死了
不像Java，只需要在maven中导入一个坐标就可以愉快地使用了
也不像Go一样，只要import相应的包的github地址，一句 go mod tidy命令就解决了。
---转载

*/

class RaftRpcUtil{
    private:
        raftRpcProtoc::raftRpc_Stub *stub_;
    
    public:
        bool AppendEntries(raftRpcProtoc::AppendEntriesArgs *args,raftRpcProtoc::AppendEntriesReply *response);
        bool InstallSnapshot(raftRpcProtoc::InstallSnapshotRequest *args, raftRpcProtoc::InstallSnapshotResponse *response);
        bool RequestVote(raftRpcProtoc::RequestVoteArgs *args, raftRpcProtoc::RequestVoteReply *response);

    
    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
};

#endif