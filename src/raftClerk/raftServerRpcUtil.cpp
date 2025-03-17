#include "raftServerRpcUtil.h"

RaftServerRpcUtil::RaftServerRpcUtil(std::string ip,short port){
    this->stub = new raftKVRpcProtoc::kvServerRpc_Stub(new MprpcChannel(ip,port,false));
}

RaftServerRpcUtil::~RaftServerRpcUtil(){
    delete this->stub;
}

// 由stub发送rpc请求
bool RaftServerRpcUtil::Get(raftKVRpcProtoc::GetArgs* args,raftKVRpcProtoc::GetReply* reply){
    MprpcController controller;
    this->stub->Get(&controller,args,reply,nullptr);
    return !controller.Failed();
}

bool RaftServerRpcUtil::PutAppend(raftKVRpcProtoc::PutAppendArgs* args,raftKVRpcProtoc::PutAppendReply* reply){
    MprpcController controller;
    this->stub->PutAppend(&controller,args,reply,nullptr);
    bool isFailed = controller.Failed();
    if(isFailed)
        std::cout<< controller.ErrorText()<<std::endl;
    return !isFailed;
}