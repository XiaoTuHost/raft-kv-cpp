#pragma once
#ifndef __RAFT__H__
#define __RAFT__H__

#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include "boost/serialization/serialization.hpp"
#include "boost/any.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
// raft与状态机的通信消息
#include "ApplyMsg.h"
// 持久化
#include "Persister.h"
// raft配置：超时时间等
#include "config.h"
// TODO 协程库
// 先使用线程代替
//#include "monsoon.h"

// rpc接口
#include "raftRpcUtil.h"
// 一些工具类和工具方法
#include "util.h"

// 一些变量状态
// 网络状态
constexpr int Disconnected = 0;
constexpr int Normal = 1;
// 投票状态
constexpr int Killed = 0;
constexpr int Voted = 1;
constexpr int Exprie = 2;
constexpr int Normal = 3;

// 类方法名小写开头、驼峰命名
// rpc方法是大写开头、驼峰命名
class Raft : public raftRpcProtoc::raftRpc{
    public:
        // 选举是否超时
        void electionTimeoutTicker();
        // 发起选举：递增任期，重置投票，发送 SendRequestVote RPC
        void doElection();
        // RPC调用
        bool sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args,
            std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
        void RequestVote(const raftRpcProtoc::RequestVoteArgs *args, raftRpcProtoc::RequestVoteReply *reply);
        
        // 心跳检测、日志发送
        void leaderHearBeatTicker();
        // 心跳
        void doHeartBeat();
        // RPC调用
        bool sendAppendEntries(int server, std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args,
            std::shared_ptr<raftRpcProtoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);
        void AppendEntries1(const raftRpcProtoc::AppendEntriesArgs *args, raftRpcProtoc::AppendEntriesReply *reply);



        // 客户端提交新命令到 Leader 的日志中
        void start(Op command,int* newLogIndex,int* newLogTerm,bool *isLeader);

        // 持久化
        void persist();
        void readPersist(std::string data);
        std::string persistData();

        // 快照
        void leaderSendSnapshot(int server);
        // RPC
        void InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest* args,
                        const raftRpcProtoc::InstallSnapshotResponse* reply);
        /*
        Snapshot the service says it has created a snapshot that has
        all info up to and including index. this means the
        service no longer needs the log through (and including)
        that index. Raft should now trim its log as much as possible.
        */
        void snapshot(int index,std::string snapshot);

        // TODO
        // other
        void applierTicker();
        void pushKMsgToKvServer(ApplyMsg msg);
        // 查询节点任期、身份
        void getState(int* term,bool* isLeader);
        int getRaftStateSize();

        int getLastLogIndex();
        int getLastLogTerm();
        void getLastLogIndexAndTerm(int* lastLogIndex,int* lastLogTerm);
        int getLogTermFromLogIndex(const int& logIndex);
        void getPreLogInfo(int server,int* preLogIndex,int* preLogTerm);
        
        bool condInstallSnapshot(int lastIncludeTerm,int lastIncludeIndex,std::string snapshot);
        std::vector<ApplyMsg> getApplyLogs();
        int getNewCommandIndex();
        void leaderUpdateCommitIndex();
        bool matchLog(int logIndex, int logTerm);
        // 验证给定日志是否比当前新
        bool UpToDate(int index, int term);

        // 初始化
        // @param peers 其它raft节点的rpc客户端
        // @param me    标记自身节点编号
        // @param persister 持久化接口
        // @param applyChan 消息队列、上层状态机与raft节点通信通道
        void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers,
                    int me,
                    std::shared_ptr<Persister> persister,
                    std::shared_ptr<LockQueue<ApplyMsg>> applyChan);

    public:
        // 重写基类方法,因为rpc远程调用真正调用的是这个方法
        // 序列化，反序列化等操作rpc框架都已经做完了，因此这里只需要获取值然后真正调用本地方法即可。

        // protobuf service声明的rpc服务
        // @param controller 调用信息追踪
        // @param request
        // @param response
        // @param clousuer   调用完成回调
        void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProtoc::AppendEntriesArgs *request,
                           ::raftRpcProtoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
        void InstallSnapshot(google::protobuf::RpcController *controller,
                             const ::raftRpcProtoc::InstallSnapshotRequest *request,
                             ::raftRpcProtoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
        void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProtoc::RequestVoteArgs *request,
                         ::raftRpcProtoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;
    private:
        std::mutex m_mutex;
        // 其它节点的rpc客户端
        std::vector<std::shared_ptr<RaftRpcUtil>> m_peers;
        // 持久化接口
        std::shared_ptr<Persister> m_persister;

        int m_me;
        int m_currentTerm;
        int m_votedFor;

        // 日志
        std::vector<raftRpcProtoc::LogEntry> m_logs;
        int m_commitIndex; // 已知已提交的最高日志索引
        int m_lastApplied; // 已知已经应用到状态机的最高日志索引
        std::vector<int> m_nextIndex;
        std::vector<int> m_matchIndex;
        
        // 身份
        enum Status{ Follower,Candidate,Leader};
        Status m_status;

        // 类消息队列
        // 用于向应用层传递已提交的日志条目
        std::shared_ptr<LockQueue<ApplyMsg>> applyChan;

        // 选举超时时间
        std::chrono::_V2::system_clock::time_point m_lastRestElectionTime;
        // 心跳时间
        std::chrono::_V2::system_clock::time_point m_lastResetHeartBeatTime;

        // 储存了快照中的最后一个日志的Index和Term
        int m_lastSnapshotIncludeIndex;
        int m_lastSnapshotIncludeTerm;

        // TODO
        // 可引入的协程

    /*
    BoostPersistRaftNode 是 Raft 类的内部类，用于封装需要持久化的 Raft 节点状态
    通过 Boost 序列化库实现数据存储与恢复
    其核心功能是将 Raft 的关键状态（如任期、日志、快照信息）序列化为字节流
    以便在节点重启后恢复一致性。
    */
    private:
        class BoostPersistRaftNode{
            public:
                friend class boost::serialization::access;
                // When the class Archive corresponds to an output archive, the
                // & operator is defined similar to <<.  Likewise, when the class Archive
                // is a type of input archive the & operator is defined similar to >>.
                template <class Archive>
                void serialize(Archive &ar, const unsigned int version) {
                    ar &m_currentTerm;
                    ar &m_votedFor;
                    ar &m_lastSnapshotIncludeIndex;
                    ar &m_lastSnapshotIncludeTerm;
                    ar &m_logs;
                }
                int m_currentTerm;
                int m_votedFor;
                int m_lastSnapshotIncludeIndex;
                int m_lastSnapshotIncludeTerm;
                //  TODO
                // 这里的日志应该是序列化后的日志
                // 双序列化存在性能损耗、可以考虑直接存储logentries直接序列化
                std::vector<std::string> m_logs;
                // TODO
                // 用途不明确
                std::unordered_map<std::string, int> umap;
        };
};

#endif // !__RAFT__H__