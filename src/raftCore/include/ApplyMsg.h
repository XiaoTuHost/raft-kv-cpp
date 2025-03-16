#pragma once
#ifndef __APPLYMSG__H__
#define __APPLYMSG__h__

/*

将已提交的日志条目或快照传递给上层状态机

*/

#include <string>
class ApplyMsg{
    public:
        bool CommandValid;
        std::string Command;    // 客户端命令序列化后的结果
        int CommandIndex;   // 	该命令在Raft日志中的位置索引（用于确保应用顺序性）

        bool SnapshotValid;
        std::string Snapshot;
        int SnapshotTerm;   // 快照对应最后一个日志的任期
        int SnapshotIndex;  // 快照对应最后一个日志的索引

        ApplyMsg()
        : CommandValid(false)
        , Command()
        , CommandIndex(-1)
        , SnapshotValid(false)
        , SnapshotTerm(-1)
        , SnapshotIndex(-1)
        {

        };
};

#endif