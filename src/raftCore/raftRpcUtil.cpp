#include "raftRpcUtil.h"

#include "mprpcchannel.h"
#include "mprpccontroller.h"

// TODO 这里发送的请求是怎么完成的
bool RaftRpcUtil::AppendEntries(raftRpcProtoc::AppendEntriesArgs *args,raftRpcProtoc::AppendEntriesReply *response){
    MprpcController controller;
    stub_->AppendEntries(&controller,args,response,nullptr);
    return !controller.Failed();
}

bool RaftRpcUtil::InstallSnapshot(raftRpcProtoc::InstallSnapshotRequest *args, raftRpcProtoc::InstallSnapshotResponse *response){
    MprpcController controller;
    stub_->InstallSnapshot(&controller,args,response,nullptr);
    return !controller.Failed();
}

bool RaftRpcUtil::RequestVote(raftRpcProtoc::RequestVoteArgs *args, raftRpcProtoc::RequestVoteReply *response){
    MprpcController controller;
    stub_->RequestVote(&controller,args,response,nullptr);
    return !controller.Failed();
}

// TODO 思考利用mprpc的流程
// 框架是如何完成请求和响应的
RaftRpcUtil::RaftRpcUtil(std::string ip, short port){
    stub_ = new raftRpcProtoc::raftRpc_Stub(new MprpcChannel(ip,port,true));
}

RaftRpcUtil::~RaftRpcUtil(){
    delete stub_;
}