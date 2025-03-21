#include "Persister.h"
#include "util.h"


// TODO:会涉及反复打开文件的操作，没有考虑如果文件出现问题会怎么办？？
void Persister::Save(std::string raftstate,std::string snapshot){
    std::lock_guard<std::mutex> lock(m_mutex);
    clearRaftStateAndSnapshot();
    m_raftStateOutStream<<m_raftState;
    m_snapshotOutStream<<m_snapshot;
}

std::string Persister::ReadSnapshot(){
    std::lock_guard<std::mutex> lock(m_mutex);
    // 先关闭写文件
    if(m_snapshotOutStream.is_open())
        m_snapshotOutStream.close();
    DEFER{
        // 默认app方式
        m_snapshotOutStream.open(m_snapshotFileName);
    };
    // 以读方式打开文件
    std::fstream infile(m_snapshotFileName,std::ios::in);
    if(!infile.good())  return "";
    std::string snapshot;
    infile>>snapshot;
    infile.close();
    return snapshot;
    // 在回到写方式打开 defer延迟作用 可理解为一个函数执行完的回调操作
}

void Persister::SaveRaftState(const std::string& data){
    std::lock_guard<std::mutex> lock(m_mutex);
    clearRaftState();
    m_raftStateOutStream<<data;
    m_raftStateSize += data.size();
}

long long Persister::RaftStateSize(){
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_raftStateSize;
}

// TODO 为什么这里不用关闭后在打开文件
std::string Persister::ReadRaftState(){
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::fstream ifs(m_raftStateFileName,std::ios::in);
    if(!ifs.good()){
        return "";
    }
    std::string snapshot;
    ifs>>snapshot;
    ifs.close();
    return snapshot;

}

explicit Persister::Persister(int me)
    : m_raftStateFileName("raftStatePersist"+std::to_string(me)+".txt")
    , m_snapshotFileName("snapshotPersist"+std::to_string(me)+".txt")
    , m_raftState(0)
{
    // 检查文件状态并清空
    bool fileOpenFlag = true;
    std::fstream file(m_raftStateFileName,std::ios::out | std::ios::trunc);
    if(file.is_open()){
        file.close();
    }else{
        fileOpenFlag=false;
    }
    std::fstream file(m_snapshotFileName,std::ios::out | std::ios::trunc);
    if(file.is_open()){
        file.close();
    }else{
        fileOpenFlag=false;
    }
    if(!fileOpenFlag)
    DPrintf("[ func-Persister::Persister] file open error");
    // 绑定流
    m_raftStateOutStream.open(m_raftStateFileName);
    m_snapshotOutStream.open(m_snapshotFileName);
}

Persister::~Persister(){
    // 关闭文件
    if(m_raftStateOutStream.is_open())
        m_raftStateOutStream.close();
    
    if(m_snapshotOutStream.is_open())
        m_snapshotOutStream.close();
}

void Persister::clearRaftState(){
    m_raftStateSize=0;
    // 关闭文件流
    if(m_raftStateOutStream.is_open())
        m_raftStateOutStream.close();
    // 重新打开文件并清空
    m_raftStateOutStream.open(m_raftStateFileName,std::ios::out | std::ios::trunc);
}

void Persister::clearSnapshot(){
    if(m_snapshotOutStream.is_open())
        m_snapshotOutStream.close();
    m_snapshotOutStream.open(m_snapshotFileName,std::ios::out | std::ios::trunc);
}

void Persister::clearRaftStateAndSnapshot(){
    clearRaftState();
    clearSnapshot();
}