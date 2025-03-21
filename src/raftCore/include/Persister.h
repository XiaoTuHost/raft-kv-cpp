#pragma once
#ifndef __PERSISTER__H__
#define __PERSISTER__H__

/*

Raft节点持久化存储
持久化快照、日志、任期、投票状态

*/
#include <fstream>
#include <mutex>

// TODO state和snapshot存储 什么

class Persister{
    public:
        void Save(std::string raftstate,std::string snapshot);
        std::string ReadSnapshot();
        void SaveRaftState(const std::string& data);
        long long RaftStateSize();
        std::string ReadRaftState();
        explicit Persister(int me);
        ~Persister();
    private:
        void clearRaftState();
        void clearSnapshot();
        void clearRaftStateAndSnapshot();
    private:

        std::mutex m_mutex;
        std::string m_raftState;
        std::string m_snapshot;
        const std::string m_raftStateFileName;
        const std::string m_snapshotFileName;
        std::ofstream m_raftStateOutStream;
        std::ofstream m_snapshotOutStream;
        long long m_raftStateSize;
};

#endif