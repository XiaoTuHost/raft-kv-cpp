#include "KvServer.h"

#include "rpcprovider.h"

#include "mprpcconfig.h"

// 构造函数 初始化kvsever服务
// 构建持久化
// 构建rpc服务,连接其他节点的rpc服务
// 构造raft节点
// 从快照恢复
KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port)
    : m_skipList(6)
{
    std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);

    m_me = me;
    m_maxRaftState = maxraftstate;
    m_raftNode = std::make_shared<Raft>();

    // 构建rpc服务
    // 发布本地rpc方法，监听rpc请求
    std::thread t([this,port]{
        RpcProvider provider;
        provider.NotifyService(this);
        provider.NotifyService(
            this->m_raftNode.get()
        );
        provider.Run(this->m_me,port);
    });
    t.detach();
    // 开启rpc远程调用能力，需要注意必须要保证所有节点都开启rpc接受功能之后才能开启rpc远程调用能力
    // 这里使用睡眠来保证
    std::cout << "raftServer node:" << m_me << " start to sleep to wait all ohter raftnode start!!!!" << std::endl;
    sleep(6);
    std::cout << "raftServer node:" << m_me << " wake up!!!! start to connect other raftnode" << std::endl;

    // 连接其它raft节点的rpc
    MprpcConfig config;
    config.LoadConfigFile(nodeInforFileName.c_str());
    std::vector<std::pair<std::string,short>> ipPortVec;
    // 先从节点获取数据
    for(int i=0;i<INT_MAX;++i){
        std::string node = "node"+std::to_string(i);
        std::string nodeIp = config.Load(node+"Ip");
        std::string nodePort = config.Load(node+"Port");
        // 所以节点从0开始存储
        if(nodeIp.empty())
            break;
        ipPortVec.emplace_back(nodeIp,atoi(nodePort.c_str()));
    }

    // 保存rpc客户端
    std::vector<std::shared_ptr<RaftRpcUtil>> servers;
    for(int i=0;i<INT_MAX;++i){
        if(i==m_me){
            servers.push_back(nullptr);
            continue;
        }

        std::string nodeIp = ipPortVec[i].first;
        short nodePort = ipPortVec[i].second;
        auto* rpc = new RaftRpcUtil(nodeIp,nodePort);
        servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));
        
        std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
    }
    sleep(servers.size()-m_me);
    // kvserver 关联raft 公用一个applychan来通信
    m_raftNode->init(servers,m_me,persister,applyChan);

    // TODO
    // You may need initialization code here.

    // m_skipList;
    // waitApplyCh;
    // m_lastRequestId;
    // m_lastSnapShotRaftLogIndex = 0;  
    // // todo:感覺這個函數沒什麼用，不如直接調用raft節點中的snapshot值？？？

    // 从快照恢复状态
    auto snapshot = persister->ReadSnapshot();
    if(!snapshot.empty()){
        ReadSnapshotToInstall(snapshot);
    }

    // 读取applychan命令
    std::thread t2(&KvServer::ReadRaftApplyCommandLoop,this);
    t2.join();
}
    
// TODO
// void KvServer::StartKVServer(){

// }

void KvServer::DprintfKVDB(){
    if(Debug){
        std::lock_guard<std::mutex> lock(m_mutex);
        DEFER{
            m_skipList.DisplayList();
        };
    }
}

bool KvServer::IfRequestDuplicate(std::string ClientId,int RequestId){
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_lastRequestId.find(ClientId) == m_lastRequestId.end()){
        return false;
        // TODO 不存在就创建clientId
    }
    // 每次请求到来都会先保存到本地记录客户端执行序列
    // 如果小于等于最大序列值就表面是重复请求 即dduplicate
    return RequestId <=m_lastRequestId[ClientId];
}

void KvServer::ExecuteAppendOpOnKVDB(Op op){
    m_mutex.lock();
    m_skipList.InsertAndSetElement(op.Key,op.Value);
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mutex.unlock();
    DprintfKVDB();
}

void KvServer::ExecutePutOpOnKVDB(Op op){
    m_mutex.lock();
    m_skipList.InsertAndSetElement(op.Key,op.Value);
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mutex.unlock();
    DprintfKVDB();
}

void KvServer:: ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist){
    m_mutex.lock();
    *value="";
    *exist=false;
    // search传出参数给value赋值
    if(m_skipList.SearchElement(op.Key,*value)){
        *exist=true;
    }
    m_lastRequestId[op.ClientId]=op.RequestId;
    m_mutex.unlock();
    DprintfKVDB();
}

/*
处理来自clerk的rpc:向raft发送日志、完成后提交日志向kvdb发送op
保证clerk一定是与raft-leader通信
从args中构造op交给raft节点处理
构造waitapplychan等待raft节点提交对应索引的op
*/
void KvServer::Get(const raftKVRpcProtoc::GetArgs* args,raftKVRpcProtoc::GetReply *reply){
    // 1.构造op
    Op op;
    op.Operation = "Get";
    op.Key = args->key();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();
    // 2.获取节点状态
    int raftIndex= -1;
    int _=-1;
    bool isLeader=false;
    m_raftNode->start(op,&raftIndex,&_,&isLeader);
    // 非leader节点不受理请求
    if(isLeader){
        reply->set_err(ErrWrongLeader);
        return;
    }
    // TODO 此处为什么加锁、为什么还需要解锁 不能一直持有锁
    // std::lock_guard<std::mutex> lock(m_mutex);
    m_mutex.lock();

    // 3.构造waitapplych等待提交日志 提交到kvdb
    if(waitApplyCh.find(raftIndex)==waitApplyCh.end()){
        waitApplyCh[raftIndex] = new LockQueue<Op>;
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];
    
    m_mutex.unlock(); // TODO 对应上文为什么需要解锁，不能一直持有锁

    // 4.等待日志提交读取操作
    // TODO 理解此段代码
    Op commitOp;
    if(!chForRaftIndex->TimeoutPop(CONSENSUS_TIMEOUT,&commitOp)){
        // TODO  超时原因，raft节点可能出现问题？
        // maybe1:重复请求问题 一个rpc请求被发送了多次 clientid和requestid都是相同的 前一个请求到达已被提交 后一个到达 
        // 超时 是leader且没有其它命令到来 
        // 同样提交日志
        // 因为是get请求 提交后也可再次执行 不会影响线性一致性
        if(IfRequestDuplicate(op.ClientId,op.RequestId) && isLeader){
            std::string value{};
            bool exit = false;
            ExecuteGetOpOnKVDB(op,&value,&exit);
            if(exit){
                reply->set_err(OK);
            }else{
                reply->set_err(ErrNoKey);
            }
            reply->set_value(value);
        }else{
            reply->set_err(ErrWrongLeader);
            reply->set_value("");
        }
    }else{
        // 读取到 这里处理的是一般成功的流程
        // 可能流程：1. raft同步完成日志准备提交 创建好了waitapplych 写op kvserver读到
        //          2. raft同步完成 kvserver创建好了waitapplych raf写op kvsever读到

        // 线性一致性
        if(commitOp.ClientId == op.ClientId && commitOp.RequestId==op.RequestId){
            std::string value{};
            bool exit = false;
            ExecuteGetOpOnKVDB(commitOp,&value,&exit);
            if(exit){
                reply->set_err(OK);
            }else{
                reply->set_err(ErrNoKey);
            }
            reply->set_value(value);
        }else{
            reply->set_err(ErrWrongLeader);
            reply->set_value("");
        }
    }

    m_mutex.lock();

    // 日志提交完成后清理waitapplychan
    // 清理无用资源
    auto temp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete temp;

    m_mutex.unlock();
}

// TODO 为什么putappend函数内部只需要判断幂等性、无需执行操作 
void KvServer::PutAppend(const raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply){
    Op op;
    op.Operation = args->op();
    op.Key = args->key();
    op.Value = args->value();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();

    int raftIndex = -1;
    int _=-1;   // 占位 用不到的任期传出参数
    bool isLeader = false;
    m_raftNode->start(op,&raftIndex,&_,&isLeader);

    if(isLeader){
        reply->set_err(ErrWrongLeader);
        return;
    }

    m_mutex.lock();

    if(!waitApplyCh.count(raftIndex)){
        waitApplyCh[raftIndex] = new LockQueue<Op>();
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];
    
    m_mutex.unlock();

    // 读取日志是否提交
    Op commitOp;
    if(!chForRaftIndex->TimeoutPop(CONSENSUS_TIMEOUT,&commitOp)){
        // cv的日志输出
        // 嘿嘿
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]TIMEOUT PUTAPPEND !!!! Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %s, Opreation %s Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
        if(IfRequestDuplicate(op.ClientId,op.RequestId) && isLeader){
            // 超时了,但因为是重复的请求，返回ok，实际上就算没有超时，在真正执行的时候也要判断是否重复?
            reply->set_err(OK);
        }else{
            reply->set_err(ErrWrongLeader);
        }
    }else{
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
        if(commitOp.ClientId==op.ClientId && commitOp.RequestId==op.RequestId){
            // TODO 
            //可能发生leader的变更导致日志被覆盖，因此必须检查?
            reply->set_err(OK);
        }else{
            reply->set_err(ErrWrongLeader);
        }
    }

    m_mutex.lock();

    auto temp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete temp;

    m_mutex.unlock();

}

void KvServer::ReadRaftApplyCommandLoop(){
    while(true){
        auto message = applyChan->Pop();    // 阻塞出队

        // cv 日志
        DPrintf(
            "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了下raft的消息",
            m_me);

        if(message.CommandValid){
            GetCommandFromRaft(message);
        }

        if(message.SnapshotValid){
            GetSnapshotFromRaft(message);
        }

    }   
}

/*
从raft接收已提交的日志、执行
*/
void KvServer::GetCommandFromRaft(ApplyMsg message){
    Op op;
    op.parseFromString(message.Command);

    // cv
    DPrintf(
        "[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> Index:{%d} , ClientId {%s}, RequestId {%d}, "
        "Opreation {%s}, Key :{%s}, Value :{%s}",
        m_me, message.CommandIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

    // TODO 为什么提交的非GET日志在这里执行?
    if(!IfRequestDuplicate(op.ClientId,op.RequestId)){
        if(op.Operation=="Put"){
            ExecutePutOpOnKVDB(op);
        }else if(op.Operation=="Append"){
            ExecuteAppendOpOnKVDB(op);
        }
    }

    // 是否需要制作日志
    //如果raft的log太大（大于指定的比例）就把制作快照
    if(m_maxRaftState !=-1 ){
        IfNeedToSendSnapShotCommand(message.CommandIndex,9);
    }

    // 发送已提交的日志
    SendMessageToWaitChan(op,message.CommandIndex);
}

// 回放提交的命令到waitapplych
bool KvServer::SendMessageToWaitChan(const Op& op,int raftIndex){
    std::lock_guard<std::mutex> lock(m_mutex);
    // cv 日志
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    
    // TODO 为什么不创建applychanel
    if(waitApplyCh.find(raftIndex)==waitApplyCh.end()){
        return false;
    }
    waitApplyCh[raftIndex]->Push(op);
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    return true;
}

/// @brief 是否需要制作快照
/// @param raftIndex 当前raft日志的索引
/// @param proportion 这个参数干什么？
void KvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion){
    // TODO  如何理解这里的判断
    if(m_raftNode->getRaftStateSize() > m_maxRaftState/10.0 ){
        auto snapshot = MakeSnapShot();
        m_raftNode->snapshot(raftIndex,snapshot);
    }
}

std::string KvServer::MakeSnapShot(){
    std::lock_guard<std::mutex> lock(m_mutex);
    // 快照是将kv数据和每个客户端最后一次请求id序列化后的结果
    // kv数据是序列化后的数据 --> 双序列化
    std::string snapshotData = getSnapshotData();
    return snapshotData;
}

void KvServer::GetSnapshotFromRaft(ApplyMsg msg){
    std::lock_guard<std::mutex> lock(m_mutex);

    // 是否安装日志 需要与raft节点日志比较
    if(m_raftNode->condInstallSnapshot(msg.SnapshotTerm,msg.SnapshotIndex,msg.Snapshot)){
        ReadSnapshotToInstall(msg.Snapshot);
        m_lastSnapshotRaftLogIndex = msg.SnapshotIndex;
    }

}

void KvServer::ReadSnapshotToInstall(std::string snapshot){
    if(snapshot.empty()){
        return;
    }

    // 反序列化得到序列化的kv数据 ---> 再次反序列到dumper ---> kv形式存放到dumper ---> skiplist通过kv回放数据
    parseFromString(snapshot);
    
}