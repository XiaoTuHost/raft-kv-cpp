#pragma once
#ifndef __KVSERVER__H__ 
#define __KVSERVER__H__

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "kvServerRPC.pb.h"
#include "raft.h"
// 底层存储数据库kv-database
#include "skipList.h"

class KvServer : public raftKVRpcProtoc::kvServerRpc{   

    private:
        std::mutex m_mutex;
        int m_me;
        std::shared_ptr<Raft> m_raftNode;
        std::shared_ptr<LockQueue<ApplyMsg>> applyChan; // kvServer和raft节点的通信管道
        int m_maxRaftState; // snapshot if log grows this big

        // 快照数据的序列化缓存
        std::string m_serializedKVData;
        // TODO SkipList 存储结构
        // your code here

        // clientId -> requestId
        std::unordered_map<std::string,int> m_lastRequestId;
        // last SnapShot point , raftIndex
        int m_lastSnapshotRaftLogIndex;

    public:
        KvServer() = delete;
        KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);
    
        void StartKVServer();
    
        void DprintfKVDB();

        bool IfRequestDuplicate(std::string ClientId,int RequestId);
        // 操作接口
        void ExecuteAppendOpOnKVDB(Op op);
        void ExecutePutOpOnKVDB(Op op);
        void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);

        void Get(const raftKVRpcProtoc::GetArgs* args,raftKVRpcProtoc::GetReply *reply);
        void PutAppend(const raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply);
        
        //一直等待raft传来的applyCh
        void ReadRaftApplyCommandLoop();
        // 处理从 Raft 层收到的已提交日志条目
        void GetCommandFromRaft(ApplyMsg msg);
        // 将已提交的操作结果发送到等待队列
        bool SendMessageToWaitChan(const Op& op,int raftIndex);

        // 快照相关

        // 检查日志大小，触发快照生成
        // 检查是否需要制作快照，需要的话就向raft之下制作快照
        void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);
        // 制作快照
        std::string MakeSnapShot();
        // 处理 Raft 层发来的快照安装请求
        // Handler the SnapShot from kv.rf.applyCh
        void GetSnapshotFromRaft(ApplyMsg msg);
        // 加载快照数据到状态机
        void ReadSnapshotToInstall(std::string snapshot);

    // RPC 接口
    public:
        void PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::PutAppendArgs *request,
            ::raftKVRpcProtoc::PutAppendReply *response, ::google::protobuf::Closure *done) override;

        void Get(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::GetArgs *request,
            ::raftKVRpcProtoc::GetReply *response, ::google::protobuf::Closure *done) override;

    // 序列化
    private:
        friend class boost::serialization::access;

        template<class Archive>
        void serilalized(Archive &ar,const unsigned int version){
            ar &m_serializedKVData;
            ar &m_lastRequestId;
        } 

        // TODO 因为还可以拿到跳表机构
        std::string getSnapshotData(){
            // code here
        }

        void parseFromString(const std::string &str){
            std::stringstream ss(str);
            boost::archive::text_iarchive ia(ss);
            ia >> *this;
            // TODO 跳表结构加载序列化数据
            // code here
        }
};

#endif 