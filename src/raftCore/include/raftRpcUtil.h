#pragma once
#ifndef __RAFTRPCUTIL__H__
#define __RAFTRPCUTIL__H__

#include "raftRPC.pb.h"

class RaftRpcUtil{
    private:
        raftRpcProctoc::raftRpc_Stub *stub_;

    public:
        bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args,raftRpcProctoc::AppendEntriesReply *response);
        bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);
        bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response);

    
    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
};

#endif