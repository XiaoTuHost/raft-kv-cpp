#include "raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include "config.h"
#include "util.h"

void Raft::electionTimeoutTicker(){
    // 循环检测是否超时
    while(true){
    
        //如果不睡眠，那么对于leader，这个函数会一直空转，浪费cpu
        // 且加入协程之后，空转会导致其他协程无法运行，对于时间敏感的AE，会导致心跳无法正常发送导致异常
        while(m_status==Leader){
            // 睡心跳时间是因为leader持续发送心跳维持leader状态
            usleep(HeartBeatTimeout);
        }
        // ns
        std::chrono::duration<signed long int,std::ratio<1,1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime{};
        {
            // 针对follower
            // 计算一个合适的睡眠时间
            // 完成后看在这段时间超时时间是否重置
            std::lock_guard<std::mutex> lock(m_mutex);
            wakeTime = now();
            suitableSleepTime = getRandomizedElectionTimeout() + m_lastRestElectionTime - wakeTime;
        }
        // 预计超时时间在未来 大于1ms
        // 休眠这段时间
        // 如果不进条件表示已经过了重置事件
        if(std::chrono::duration<double,std::milli>(suitableSleepTime).count()>1){
            auto start_ = std::chrono::steady_clock::now();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
            auto end_ = std::chrono::steady_clock::now();
            std::chrono::duration<double,std::milli> duration = end_ - start_;

            std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
            << std::endl;
            std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
            << std::endl;
        }
        // 这里表示超时时间内有没有重置时钟
        // >0表示在休眠这段时间内已经重置了事件
        // <0表示没有重置，超时选举
        if(std::chrono::duration<double,std::milli>(m_lastRestElectionTime - wakeTime).count()>0){
            continue;
        }
        doElection();
    }
}
void Raft::doElection(){

}
bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args,
    std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum){

    }
void Raft::RequestVote(const raftRpcProtoc::RequestVoteArgs *args, raftRpcProtoc::RequestVoteReply *reply){

}

void Raft::leaderHearBeatTicker(){
    while(true){
        // 在超时时间内是否收到心跳
        // 不是leader先睡一会儿
        while(m_status==Leader){
            usleep(1000*HeartBeatTimeout);
        }
        // leader不止会主动发送心跳重置心跳时间
        // 还可能因为同步日志而重置心跳时间 这段时间可能小于重置心跳时间
        static std::atomic<int32_t> atomic_count=0;
        std::chrono::duration<unsigned long int,std::ratio<1,1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime{};
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            wakeTime = now();
            suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout)+m_lastResetHeartBeatTime-wakeTime;
        }
        if(std::chrono::duration<double,std::milli>(suitableSleepTime).count()>1){
            auto start_ = std::chrono::steady_clock::now();
            usleep(std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count());
            auto end_ = std::chrono::steady_clock::now();
            std::chrono::duration<double,std::milli> duration = end_ - start_;

            // cv log
            std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
            << std::endl;
            std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
            << std::endl;
        }
        if(std::chrono::duration<double,std::milli>(m_lastResetHeartBeatTime - wakeTime).count()>0)
            // 睡眠这段时间有触发心跳、不触发心跳
            continue;
        // 执行实际的心跳
        doHeartBeat();
    }
}
void Raft::doHeartBeat(){

}
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args,
    std::shared_ptr<raftRpcProtoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums){

    }
void Raft::AppendEntries1(const raftRpcProtoc::AppendEntriesArgs *args, raftRpcProtoc::AppendEntriesReply *reply){

}
/// @brief 由上层的kvserver调用，初始化raft节点状态（节点、日志、快照），启动计时器
/// @param peers 其它raft节点
/// @param me 自己是几号节点、在处理请求时跳过自己
/// @param persister 持久化接口
/// @param applyChan 与kvsever的通信通道
void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers,
    int me,
    std::shared_ptr<Persister> persister,
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan){
    
    m_peers=peers;
    m_me=me;
    m_persister=persister;

    m_mutex.lock();
    this->applyChan=applyChan;
    m_currentTerm=0;
    m_status=Follower;
    m_commitIndex=0;
    m_lastApplied=0;
    m_logs.clear();
    for(int i=0;i<m_peers.size();++i){
        // 维护其余节点的日志状态
        // 用于同步操作
        m_matchIndex.push_back(0);
        m_nextIndex.push_back(0);
    }
    m_votedFor=-1;
    m_lastSnapshotIncludeIndex=0;
    m_lastSnapshotIncludeTerm=0;
    m_lastResetHeartBeatTime=now();
    m_lastRestElectionTime=now();

    readPersist(m_persister->ReadRaftState()); // 持久化恢复raft状态
    if(m_lastSnapshotIncludeIndex>0){
        m_lastApplied=m_lastSnapshotIncludeIndex;
    }

    // cv日志
    DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , lastSnapshotIncludeTerm {%d}", m_me,
        m_currentTerm, m_lastSnapshotIncludeIndex, m_lastSnapshotIncludeTerm);

    m_mutex.unlock();

    // 并发任务 计时器判断是否超时
    std::thread t1([this](){this->leaderHearBeatTicker();});
    // std::thread t(&Raft::leaderHearBeatTicker, this);
    std::thread t2([this](){this->electionTimeoutTicker();});
    // std::thread t2(&Raft::electionTimeOutTicker, this);
    std::thread t3(&Raft::applierTicker,this);

    t1.detach(),t2.detach(),t3.detach();

}