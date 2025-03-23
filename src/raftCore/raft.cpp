#include "raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory>
#include "config.h"
#include "util.h"

// leader操作，follower接收
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
    std::lock_guard<std::mutex> lock(m_mutex);
    // TODO 为什么判断==Leader
    if(m_status==Leader){

    }
    // 不是leader才会进行选举
    if(m_status!=Leader){
        DPrintf("[     ticker-func-rf(%d)      ]  选举定时器到期且不是leader，开始选举 \n", m_me);
        // 设置节点状态
        m_status = Candidate;
        m_currentTerm++;
        m_votedFor = m_me;
        persist();

        // shared_ptr存储的值是线程安全的 因为引用计数操作是原子实现
        // shared_ptr本身不是线程安全的
        std::shared_ptr<int> voteNum = std::make_shared<int>(1);
        m_lastRestElectionTime = now();
        // 通过rpc发起投票
        for(int i=0;i<m_peers.size();++i){
            if(i==m_me) continue;
            int lastLogIndex = -1,lastLogTerm=-1;
            getLastLogIndexAndTerm(&lastLogIndex,&lastLogTerm);
            
            // 构造请求
            std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args = 
                std::make_shared<raftRpcProtoc::RequestVoteArgs>();
            args->set_candidateid(m_me);
            args->set_term(m_currentTerm);
            args->set_lastlogterm(lastLogTerm);
            args->set_lastlogindex(lastLogIndex);
            auto reply = std::make_shared<raftRpcProtoc::RequestVoteReply>();

            // TODO 使用匿名函数执行避免拿到锁？
            std::thread t(&Raft::sendRequestVote,this,i,args,reply,voteNum);
            t.detach();
        }
    }
}
// @return 是否操作完成
bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> args,
    std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum){
        auto start = now();
        DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 开始", m_me, m_currentTerm, getLastLogIndex());
        bool ok = m_peers[server]->RequestVote(args.get(), reply.get());
        DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 完成，耗时:{%d} ms", m_me, m_currentTerm,
                getLastLogIndex(), now() - start);
        //这个ok是网络是否正常通信的ok，而不是requestVote rpc是否投票的rpc
        if(!ok){
            // TODO 不加这个ok服务器宕机会出现问题？
            return ok;
        }
        // 响应回应
        std::lock_guard<std::mutex> lock(m_mutex);
        // 1. 先判断任期
        if(reply->term()>m_currentTerm){
            // 三变：身份、任期、投票
            m_status = Follower;
            m_currentTerm = reply->term();
            m_votedFor = -1;
            persist();
            return true;
        }else if(reply->term()<m_currentTerm){
            return true;
        }
        // 任期相同在判断日志
        myAssert(reply->term()==m_currentTerm,format("assert{reply.Term==raft.currentTerm} failed!")); 
        // 日志匹配是否拒绝
        if(!reply->votegranted()){
            return true;
        }
        // 表示认可该节点可以成为leader
        *votedNum = *votedNum+1;
        // 超过半数节点认同
        if(*votedNum>=m_peers.size()/2+1){
            *votedNum=0;
            if(m_status==Leader){
                 //如果已经是leader了，那么是就是了，不会进行下一步处理了
            myAssert(false,
                format("[func-sendRequestVote-rf{%d}]  term:{%d} 同一个term当两次领导，error", m_me, m_currentTerm));
            }
            m_status=Leader;
            DPrintf("[func-sendRequestVote rf{%d}] elect success  ,current term:{%d} ,lastLogIndex:{%d}\n", m_me, m_currentTerm,
                getLastLogIndex());
            int lastLogIndex = getLastLogIndex();
            for(int i=0;i<m_nextIndex.size();++i){
                // 有效下标从1开始
                m_nextIndex[i]=lastLogIndex+1;
                // 每次换leader match置为0
                m_matchIndex[i] = 0;
            }
            // 发起心跳宣告自己是leader
            std::thread t(&Raft::doHeartBeat,this);
            t.detach();

            persist();
        }
        return true;
    }

// 其余的raft节点处理vote请求
void Raft::RequestVote(const raftRpcProtoc::RequestVoteArgs *args, raftRpcProtoc::RequestVoteReply *reply){
    std::lock_guard<std::mutex> lock(m_mutex);
    DEFER{
        persist();
    };
    if(args->term()<m_currentTerm){
        reply->set_term(m_currentTerm);
        reply->set_votestate(Expire);
        reply->set_votegranted(false);
        return;
    }
    if(args->term()>m_currentTerm){
        m_status=Follower;
        m_currentTerm = args->term();
        m_votedFor = -1;
    }
    myAssert(args->term() == m_currentTerm,
    format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm，这里却不等", m_me));
    // 现在节点任期都是相同的(任期小的也已经更新到新的args的term了)
    // 还需要检查log的term和index是不是匹配的了
    int lastLogIndex = getLastLogIndex();
    // canditate-node 与 该节点对比 candidate是否更新
    if(!UpToDate(args->lastlogindex(),args->lastlogterm())){
        // 日志不匹配 拒绝投票
        // TODO
        if(args->lastlogterm()<m_currentTerm){

        }else{

        }
        reply->set_term(m_currentTerm);
        // 表示我要投给其它节点
        reply->set_votestate(Voted);
        reply->set_votegranted(false);
    }
    // 投给了其它节点
    if(m_votedFor!=-1 && m_votedFor!=args->candidateid()){
        reply->set_term(m_currentTerm);
        reply->set_votestate(Voted);
        reply->set_votegranted(false);
        return;
    }else{
        m_votedFor=args->candidateid();
        m_lastRestElectionTime = now();
        reply->set_term(m_currentTerm);
        reply->set_votestate(Normal);
        reply->set_votegranted(true);
        return;
    }
}   

// leader操作，follower接收
// 可能是正常心跳，也肯能是同步日志发送的心跳包
void Raft::leaderHearBeatTicker(){
    while(true){
        // 在超时时间内是否收到心跳
        // 不是leader先睡一会儿
        while(m_status!=Leader){
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

/*
心跳/日志同步
*/
void Raft::doHeartBeat(){
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_status==Leader){
        DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex，开始发送AE\n", m_me);
        auto appendNums = std::make_shared<int>(1);


        // TODO 
        // todo 这里肯定是要修改的，最好使用一个单独的goruntime来负责管理发送log，因为后面的log发送涉及优化之类的
        //最少要单独写一个函数来管理，而不是在这一坨
        // 将同步快照/日志、心跳分离
        for(int i=0;i<m_peers.size();++i){
            if(i==m_me) continue;
            DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
            myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));
            // 节点落后太多
            if(m_nextIndex[i]<=m_lastSnapshotIncludeIndex){
                std::thread t(Raft::leaderSendSnapshot,this,i);
                t.detach();
                continue;
            }
            // 构造心跳包
            int preLogIndex = -1;
            int preLogTerm = -1;
            getPreLogInfo(i,&preLogIndex,&preLogTerm);
            std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args = 
                std::make_shared<raftRpcProtoc::AppendEntriesArgs>();
            args->set_term(m_currentTerm);
            args->set_prevlogindex(preLogIndex);
            args->set_prevlogterm(preLogTerm);
            args->set_leaderid(m_me);
            args->set_leadercommit(m_commitIndex);
            args->clear_entries();
            // 其它节点在收到快照后有追加新的日志
            if(preLogIndex!=m_lastSnapshotIncludeIndex){
                // 如果说 leader与follower的日志匹配
                // 不会进入循环发送日志
                for(int j=getSlicesIndexFromLogIndex(preLogIndex)+1;i<m_logs.size();++i){
                    // add_entries 返回下一个插入位置
                    raftRpcProtoc::LogEntry* entry = args->add_entries(); 
                    *entry = m_logs[j];
                }
            }else{
                for(const auto&item:m_logs){
                   auto entry =  args->add_entries();
                   *entry = item;
                }
            }
            int lastLogIndex = getLastLogIndex();
            // leader对每个节点发送的日志长短不一，但是都保证从prevIndex发送直到最后
            myAssert(args->prevlogindex() + args->entries_size() == lastLogIndex,
            format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                    args->prevlogindex(), args->entries_size(), lastLogIndex));
            // 构造响应、发送请求
            auto reply = std::make_shared<raftRpcProtoc::AppendEntriesReply>();
            reply->set_appstate(Disconnected);
            std::thread t(&Raft::sendAppendEntries,this,i,args,reply,appendNums);
            t.detach();
        }
        m_lastResetHeartBeatTime = now();
    }
}


bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProtoc::AppendEntriesArgs> args,
    std::shared_ptr<raftRpcProtoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums){
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc开始 ， args->entries_size():{%d}", m_me,
        server, args->entries_size());
    //  先发送请求，再处理响应
    // shared_ptr 的 get 获取原始指针
    bool ok = m_peers[server]->AppendEntries(args.get(),reply.get());
    if (!ok) {
        DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失败", m_me, server);
        return ok;
    }
      DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", m_me, server);
      if (reply->appstate() == Disconnected) {
        return ok;
    }

    // 多线程操作
    

    if(reply->term()>m_currentTerm){
        m_status = Follower;
        m_currentTerm = reply->term();
        m_votedFor = -1;
        return ok;
    }else if(reply->term()<m_currentTerm){
        DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", m_me, server, reply->term(),
            m_me, m_currentTerm);
        return ok;
    }
    if(m_status!=Leader)
        return ok;
    
    myAssert(reply->term() == m_currentTerm,
        format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), m_currentTerm));
    
    // 日志是否匹配
    if(!reply->success()){
        // TODO -100表示什么，日志回退优化？
        if(reply->updatenextindex()!=-100){
        // TODO :待总结，就算term匹配，失败的时候nextIndex也不是照单全收的，因为如果发生rpc延迟，leader的term可能从不符合term要求
        //变得符合term要求
        //但是不能直接赋值reply.UpdateNextIndex
        DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n", m_me,
        server, reply->updatenextindex());
        }
        m_nextIndex[server] = reply->updatenextindex();
    }else{
        *appendNums = *appendNums+1;
        // 日志匹配
        // cv log
        DPrintf("---------------------------tmp------------------------- 粘贴{%d}返回true,先前*appendNums{%d}", server,
            *appendNums);
        // 1. 更新状态
        m_matchIndex[server] = std::max(m_matchIndex[server],args->prevlogindex()+args->entries_size());
        m_nextIndex[server] = m_matchIndex[server]+1; 
        int lastLogIndex = getLastLogIndex();
        // 2. 超过半数节点收到且匹配可以提交日志
        // cv log
        if(*appendNums>= m_peers.size()/2+1){

            // 防止重复提交
            *appendNums = 0;

            // 不是空的心跳包
            if (args->entries_size() > 0) {
                DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   m_currentTerm{%d}",
                        args->entries(args->entries_size() - 1).logterm(), m_currentTerm);
            }

            //  更新commitIndex
            if (args->entries_size() > 0 && args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {
                DPrintf(
                    "---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex "
                    "from{%d} to{%d}",
                    m_commitIndex, args->prevlogindex() + args->entries_size());
                
                m_commitIndex = std::max(m_commitIndex, args->prevlogindex() + args->entries_size());
            }
            myAssert(m_commitIndex <= lastLogIndex,
                       format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", m_me, lastLogIndex,
                              m_commitIndex));
        }
    }
    return ok;
}

/*

TODO mprpc大致流程：
编写proto文件定义消息和服务接口
客户端定义存根客户端，编写请求定义通信接口
服务端继承重写同名方法，用于做本地调用和响应

服务端provider读取配置文件，发布本地方法，保存服务信息服务名、方法名、方法描述
（在protobuf的rpc中一个方法描述对应一个唯一的方法调用）
使用时客户端通过channel连接到provider
构造请求参数和响应，序列化后通过channel的callmethod发送请求到服务端
服务端通过自定义消息格式读取并反序列化读取服务、方法和 参数
根据方法描述调用服务端的callmethod执行具体的请求
通过绑定的回调函数响应执行结果
channel同步等待响应后进行处理，反序列化响应内容
其中方法中response和controller都是传出参数，不是返回值


其它raft节点处理心跳/日志同步
*/
void Raft::AppendEntries1(const raftRpcProtoc::AppendEntriesArgs *args, raftRpcProtoc::AppendEntriesReply *reply){
    // TODO why lock?
    std::lock_guard<std::mutex> lock(m_mutex);
    // 可以接收则应用正常
    reply->set_appstate(AppNormal);
    
    // 不同节点收到心跳/同步包后的不同响应动作
    // 1. 先比较任期
    if(args->term()<m_currentTerm){
        reply->set_success(false);
        reply->set_term(m_currentTerm);
        reply->set_updatenextindex(-100);   // 让领导人自己更新自己
        DPrintf("[func-AppenEntries1-rf拒绝了 因为Leader.term{%d}<rf{%d}.term{%d}]",
            reply->term(),m_me,m_currentTerm);
        return;   
    }
    // 即使异常退出也保证出作用域后持久化
    DEFER{persist();};
    if(args->term()>m_currentTerm){
        // 3
        m_status = Follower;
        m_currentTerm = args->term();
        m_votedFor = -1;

        // TODO
        // 这里可不返回，应该改成让改节点尝试接收日志
        // 如果是领导人和candidate突然转到Follower好像也不用其他操作
        // 如果本来就是Follower，那么其term变化，相当于“不言自明”的换了追随的对象，因为原来的leader的term更小，是不会再接收其消息了

    }

    myAssert(m_currentTerm == args->term(),format("assert rf.term{%d}==leader.term{%d} failed!",m_currentTerm,args->term()));
    // TODO
    // 如果发生网络分区，那么candidate可能会收到同一个term的leader的消息，要转变为Follower，为了和上面，因此直接写
    m_status = Follower;  // 这里是有必要的，因为如果candidate收到同一个term的leader的AE，需要变成follower
    m_lastRestElectionTime = now();

    // term相等
    // 2.再比较日志
    
    // 三种情况：
    // TODO a
    // a.日志更新 那么直接返回本地日志index的下一个做updateindex    可能是宕机后从快照恢复
    // b.日志更旧 leader发送的日志过于陈旧      可能能是有一个拥有陈旧日志的raft节点当选leader
    // c.leader保存的prelog处于快照log和lastlogindex之间
    // c-1 pre与本地位置匹配   从匹配位置开始写日志，覆盖lastindex之前，追加之后
    // c-2 pre位置不匹配       直接从prev位置前向纠错，找到一个开始更新的index
    
    // a
    if(args->prevlogindex() > getLastLogIndex()){
        reply->set_success(false);
        reply->set_term(m_currentTerm);
        reply->set_updatenextindex(args->prevlogindex()+1);
        return;
    }

    // b
    if(args->prevlogindex() < m_lastSnapshotIncludeIndex){
        reply->set_success(false);
        reply->set_term(m_currentTerm);
        reply->set_updatenextindex(m_lastSnapshotIncludeIndex+1);
        return;
    }

    // c
    if(matchLog(args->prevlogindex(),args->prevlogterm())){
        // c-1
        for(int i=0;i<args->entries_size();++i){
            auto log = args->entries(i);
            if(log.logindex() > getLastLogIndex()){
                m_logs.push_back(log);
            }else{
                if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
                    m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() != log.command()) {
                // TODO 为什么会出现这种情况
                //相同位置的log ，其logTerm相等，但是命令却不相同，不符合raft的前向匹配，异常了！
                myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                                 " {%d:%d}却不同！！\n",
                                 m_me, log.logindex(), log.logterm(), m_me,
                                 m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                                 log.command()));
                }
                if(m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm()!=
                    log.logterm()){
                    m_logs[getSlicesIndexFromLogIndex(log.logindex())]=log;
                }
            }
        }
        // TODO ?
        // 错误写法like：  rf.shrinkLogsToIndex(args.PrevLogIndex)
        // rf.logs = append(rf.logs, args.Entries...)
        // 因为可能会收到过期的log！！！ 因此这里是大于等于
        myAssert(
            getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
            format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
                   m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));
        if(args->leadercommit() > m_commitIndex){
            m_commitIndex = std::min(args->leadercommit(),getLastLogIndex());
        }
        // 领导者一次性发送完成所有的日志
        myAssert(getLastLogIndex() >= m_commitIndex,
        format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
               getLastLogIndex(), m_commitIndex));
        
        reply->set_success(true);
        reply->set_term(m_currentTerm);
        return;
    }else{
        // c-2
        // TODO 如何纠错的

         // TODO 优化
        // PrevLogIndex 长度合适，但是不匹配，因此往前寻找 矛盾的term的第一个元素
        // 为什么该term的日志都是矛盾的呢？也不一定都是矛盾的，只是这么优化减少rpc而已
        // ？什么时候term会矛盾呢？很多情况，比如leader接收了日志之后马上就崩溃等等

        reply->set_updatenextindex(args->prevlogindex());
        // 任期不相符 从上个任期开始
        for(int i=args->prevlogindex();i>=m_lastSnapshotIncludeIndex;--i){
            if(getLogTermFromLogIndex(i)!=getLogTermFromLogIndex(args->prevlogindex())){
                reply->set_updatenextindex(i+1);
                break;
            }
        }
        reply->set_success(false);
        reply->set_term(m_currentTerm);
        return;
    }
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

void Raft::start(Op command,int* newLogIndex,int* newLogTerm,bool *isLeader){
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_status!=Leader){
        *newLogIndex = -1;
        *newLogTerm = -1;
        *isLeader = false;
        return;
    }

    raftRpcProtoc::LogEntry new_entry;
    new_entry.set_command(command.Operation);
    new_entry.set_logindex(getNewCommandIndex());
    new_entry.set_logterm(m_currentTerm);
    m_logs.emplace_back(new_entry);
    *newLogIndex = new_entry.logindex();
    *newLogTerm = new_entry.logterm();
    *isLeader = true;
    return;
}

/// @brief  获取已提交未应用的日志
/// @return 已提交未应用的日志队列 
std::vector<ApplyMsg> Raft::getApplyLogs(){
    std::vector<ApplyMsg> applyMsgs;
    myAssert(m_commitIndex <= getLastLogIndex(), format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
        m_me, m_commitIndex, getLastLogIndex()));
    
    while(m_lastApplied < m_commitIndex){
        // TODO 为什么先++
        m_lastApplied++;
        // TODO 断言做什么
        myAssert(m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() == m_lastApplied,
        format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogIndex{%d} != rf.lastApplied{%d} ",
               m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(), m_lastApplied));
        ApplyMsg msg;
        msg.Command = m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();
        msg.CommandIndex = m_lastApplied;
        msg.CommandValid = true;
        msg.Snapshot = false;
        applyMsgs.emplace_back(msg);
    }
    return applyMsgs;
}

void Raft::leaderSendSnapshot(int server){
    m_mutex.lock();
    raftRpcProtoc::InstallSnapshotRequest args;
    args.set_term(m_currentTerm);
    args.set_leaderid(m_me);
    args.set_lastsnapshotincludeindex(m_lastSnapshotIncludeIndex);
    args.set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);
    args.set_data(m_persister->ReadSnapshot());
    m_mutex.unlock();
    raftRpcProtoc::InstallSnapshotResponse reply;
    bool ok = m_peers[server]->InstallSnapshot(&args,&reply);
    m_mutex.lock();
    DEFER{m_mutex.unlock();};
    if(!ok){
        return;
    }    
    if(m_status!=Leader || m_currentTerm!=reply.term()){
        return;
    }
    if(reply.term()>m_currentTerm){
        // 3
        m_status=Follower;
        m_currentTerm = reply.term();
        m_votedFor=-1;
        persist();
        m_lastRestElectionTime = now();
        return;
    }
    // TODO args?
    m_matchIndex[server] = args.lastsnapshotincludeindex();
    m_nextIndex[server]=m_matchIndex[server]+1;
}

// follower节点执行
/*
    针对leader请求先判断任期
    符合判断快照是否更新
    都符合则更新状态，发送给kvsever，持久化
*/
void Raft::InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest* args,
                        raftRpcProtoc::InstallSnapshotResponse* reply){
    m_mutex.lock();
    DEFER{m_mutex.unlock();};
    if(args->term() < m_currentTerm ){
        reply->set_term(m_currentTerm);
        return;
    }
    if(args->term()>m_currentTerm){
        m_currentTerm = args->term();
        m_status = Follower;
        m_votedFor = -1;
        persist();
    }
    m_status = Follower;
    m_lastRestElectionTime = now();
    if(args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex){
        return;
    }
    // 根据快照截断日志，修改commitIndex和applyIndex
    
    // 已经生成快照部分也算提交
    m_commitIndex = std::max(m_commitIndex,args->lastsnapshotincludeindex());
    m_lastApplied = std::max(m_lastApplied,args->lastsnapshotincludeindex());
    m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
    m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

    reply->set_term(m_currentTerm);
    ApplyMsg msg;
    msg.SnapshotIndex = m_lastSnapshotIncludeIndex;
    msg.SnapshotTerm = m_lastSnapshotIncludeTerm;
    msg.Snapshot = args->data();
    msg.SnapshotValid = true;

    std::thread t(&Raft::pushKMsgToKvServer,this,msg);
    t.detach();

    m_persister->Save(persistData(),args->data());
}

// TODO 如何使用
/*
由上层的kvserver调用
kvserver根据快照生成时机将kv数据和请求id序列化交给这个函数更新状态并持久化
*/
void Raft::snapshot(int index,std::string snapshot){
    std::lock_guard<std::mutex> lock(m_mutex);
    // 只能持久化已提交的部分
    // 不能小于当前快照，也不能制作未提交部分
    if (m_lastSnapshotIncludeIndex >= index || index > m_commitIndex) {
        DPrintf(
            "[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex %d as current snapshotIndex %d is larger or "
            "smaller ",
            m_me, index, m_lastSnapshotIncludeIndex);
        return;
    }
    int lastLogIndex = getLastLogIndex();

    int newLastLogIncludeIndex = index;
    int newLastLogIncldeTerm = m_logs[getSlicesIndexFromLogIndex(index)].logterm();
    std::vector<raftRpcProtoc::LogEntry> dumpEntry;
    for(int i=index +1;i<=lastLogIndex;++i){
        dumpEntry.emplace_back(m_logs[getSlicesIndexFromLogIndex(i)]);
    }
    m_lastSnapshotIncludeIndex = newLastLogIncldeTerm;
    m_lastSnapshotIncludeTerm = newLastLogIncldeTerm;
    m_logs = dumpEntry;
    // TODO  max?
    // 已经持久化的日志可以提交且已提交
    m_commitIndex = std::max(m_commitIndex,index);
    m_lastApplied = std::max(m_lastApplied,index);

    m_persister->Save(persistData(),snapshot);

    DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen {%d}", m_me, index,
        m_lastSnapshotIncludeTerm, m_logs.size());
    myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
         format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != lastLogjInde{%d}", m_logs.size(),
                m_lastSnapshotIncludeIndex, lastLogIndex)); 

}

// TODO WAHT TO DO
bool Raft::condInstallSnapshot(int lastIncludeTerm,int lastIncludeIndex,std::string snapshot){
    return true;
}

/*
定时操作
定时将本提交日志应用到上层状态机
*/
void Raft::applierTicker(){
    while(true){
        m_mutex.lock();
        if(m_status==Leader){
            DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   m_commitIndex{%d}", m_me, m_lastApplied,
                m_commitIndex);
        }
        auto applyMsgs = getApplyLogs();
        m_mutex.unlock();
        if(!applyMsgs.empty()){
            DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver把报告的applyMsgs长度为：{%d}", m_me, applyMsgs.size());
        }
        for(const auto&item:applyMsgs){
            applyChan->Push(item);
        }
        sleepNMilliseconds(ApplyInterval);
    }
}

/*
通信信道塞入消息
要么是命令
要么是快照
根据applymsg的valid判断
*/
void Raft::pushKMsgToKvServer(ApplyMsg msg){
    this->applyChan->Push(msg);
}

void Raft::getState(int* term,bool* isLeader){
    m_mutex.lock();
    DEFER{m_mutex.unlock();};
    
    *term = m_currentTerm;
    *isLeader = (m_status == Leader);
}

int Raft::getRaftStateSize(){
    return m_persister->RaftStateSize();
}

void Raft::persist(){
    // 序列化
    auto data = persistData();
    // 交由持久化接口写入持久化
    m_persister->SaveRaftState(data);
}

// 反序列化读取状态
void Raft::readPersist(std::string data){
    if(data.empty())
        return;
    std::stringstream ss(data);
    boost::archive::text_iarchive ia(ss);
    BoostPersistRaftNode boostPersistRaftNode;
    ia>>boostPersistRaftNode;
    m_currentTerm = boostPersistRaftNode.m_currentTerm;
    m_votedFor=boostPersistRaftNode.m_votedFor;
    m_lastSnapshotIncludeIndex=boostPersistRaftNode.m_lastSnapshotIncludeIndex;
    m_lastSnapshotIncludeTerm=boostPersistRaftNode.m_lastSnapshotIncludeTerm;
    m_logs.clear();
    for(const auto&log:boostPersistRaftNode.m_logs){
        raftRpcProtoc::LogEntry entry;
        entry.ParseFromString(log);
        m_logs.emplace_back(entry);
    }
}

// 序列化后持久化
std::string Raft::persistData(){
    // 持久化类
    BoostPersistRaftNode boostPersistRaftNode;
    // 将状态保存后持久化
    boostPersistRaftNode.m_currentTerm = m_currentTerm;
    boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;
    boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;
    boostPersistRaftNode.m_votedFor = m_votedFor;
    // TODO 双序列化效率？
    // vector类型存储的类 先交友protobuf序列化，再交给boost序列化
    for(const auto&item:m_logs){
        boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
    }

    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << boostPersistRaftNode;
    return ss.str();
}

int Raft::getLastLogIndex(){
    int lastLogIndex = -1;
    int _=-1;
    getLastLogIndexAndTerm(&lastLogIndex,&_);
    return lastLogIndex;
}

int Raft::getLastLogTerm(){
    int _=-1;
    int lastLogTerm=-1;
    getLastLogIndexAndTerm(&_,&lastLogTerm);
    return lastLogTerm;
}

// 获取的是逻辑索引
void Raft::getLastLogIndexAndTerm(int* lastLogIndex,int* lastLogTerm){
    if(m_logs.empty()){
        *lastLogIndex = m_lastSnapshotIncludeIndex;
        *lastLogTerm = m_lastSnapshotIncludeTerm;
        return;
    }else{
        *lastLogIndex = m_logs[m_logs.size()-1].logindex();
        *lastLogTerm = m_logs[m_logs.size()-1].logterm();
        return;
    }
}

// leader节点logindex位置的logterm与本地相同位置的是否一致
// 逻辑日志位置的日志是否相同
bool Raft::matchLog(int logIndex, int logTerm){
    myAssert(logIndex>=m_lastSnapshotIncludeIndex && logIndex<=getLastLogIndex(),
    format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&logIndex{%d}<=rf.getLastLogIndex{%d}",
        logIndex,m_lastSnapshotIncludeIndex,getLastLogIndex()));
    return logTerm == getLogTermFromLogIndex(logIndex);
}

bool Raft::UpToDate(int index, int term){
    int lastLogIndex=-1,lastLogTerm=-1;
    getLastLogIndexAndTerm(&lastLogIndex,&lastLogTerm);
    // 获取本地日志任期、状态
    // 比较任期 日期相同比较日志
    return term>lastLogTerm | (term==lastLogTerm && index>=lastLogIndex);
}

// log的逻辑index
// 涉及到日志的index操作，都要针对逻辑index和物理index转换
// 主要是要判断逻辑index是不是在快照之后
int Raft::getLogTermFromLogIndex(const int& logIndex){
    myAssert(logIndex >= m_lastSnapshotIncludeIndex,
        format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < rf.lastSnapshotIncludeIndex{%d}", m_me,
               logIndex, m_lastSnapshotIncludeIndex));

    int lastLogIndex = getLastLogIndex();

    myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                         m_me, logIndex, lastLogIndex));

    if(logIndex==m_lastSnapshotIncludeIndex)
        return m_lastSnapshotIncludeTerm;
    else
        return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();

}

void Raft::getPreLogInfo(int server,int* preLogIndex,int* preLogTerm){
    // 和快照状态一致就从快照中拿 xxxx
    // 是否是持久化的快照索引
    if(m_nextIndex[server] == m_lastSnapshotIncludeIndex){
        *preLogIndex = m_lastSnapshotIncludeIndex;
        *preLogTerm = m_lastSnapshotIncludeTerm;
        return;
    }
    // 不一致再去日志中拿
    auto nextIndex = m_nextIndex[server];
    *preLogIndex = m_nextIndex[server]-1;
    // 转换成实际下标从log中获取
    *preLogTerm = m_logs[getSlicesIndexFromLogIndex(*preLogIndex)].logterm();
}

// 这里说明了 logindex 应该在 快照日志与实际日志大小之间
// 快照是leader节点制作后发送给follower的
// 因此快照具有一致性 但是follower节点在收到快照后可能写入了更多日志
// 这时候就从这个匹配的日志索引开始同步日志即可
int Raft::getSlicesIndexFromLogIndex(int logIndex){
    // cv log
    myAssert(logIndex > m_lastSnapshotIncludeIndex,
        format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= rf.lastSnapshotIncludeIndex{%d}", m_me,
            logIndex, m_lastSnapshotIncludeIndex));
            int lastLogIndex=  getLastLogIndex();
            myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                m_me, logIndex, lastLogIndex));
    // 说明了制作完成快照后清理日志状态 日志数组重新从0开始存储、但是日志索引持续增加
    // 转换日志索引到日志数组下标找，到实际的存储位置
    int sliceIndex = logIndex - m_lastSnapshotIncludeIndex -1;
    return sliceIndex;

}

int Raft::getNewCommandIndex(){
    auto lastLogIndex = getLastLogIndex();
    return lastLogIndex + 1;
}

// TODO WHAT TO DO
void Raft::leaderUpdateCommitIndex(){
    m_commitIndex = m_lastSnapshotIncludeIndex;
    for(int index = getLastLogIndex();index>=m_lastSnapshotIncludeIndex;--index){
        int sum = 0;
        for(int i=0;i<m_peers.size();++i){
            if(i==m_me){
                sum+=1;
                continue;
            }
            if(m_matchIndex[i]>=index){
                sum+=1;
            }
        }
        if(sum>=m_peers.size()/2+1 && getLogTermFromLogIndex(index)==m_currentTerm){
            m_commitIndex = index;
            break;
        }
    }
}

// RPC 框架调用的重写函数
// TODO WAHT TO DO
void Raft::AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProtoc::AppendEntriesArgs *request,
    ::raftRpcProtoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) {
    // 实际调用的本地方法
    AppendEntries1(request,response);
    // 回调
    done->Run();
}

void Raft::InstallSnapshot(google::protobuf::RpcController *controller,
      const ::raftRpcProtoc::InstallSnapshotRequest *request,
      ::raftRpcProtoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done){
        InstallSnapshot(request,response);
        done->Run();
}
void Raft::RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProtoc::RequestVoteArgs *request,
  ::raftRpcProtoc::RequestVoteReply *response, ::google::protobuf::Closure *done) {
    RequestVote(request,response);
    done->Run();
}