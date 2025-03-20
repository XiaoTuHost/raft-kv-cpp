#pragma once
#ifndef __UTIL__H__
#define __UTIL__H__

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
// boost库的序列化
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <condition_variable>  // pthread_condition_t
#include <functional>
#include <iostream>
#include <mutex>  // pthread_mutex_t
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include "config.h"

// RAII延迟机制封装
// 保存一个函数 在析构时调用。也就是在出作用域后调用，函数内用于保存资源释放操作
template<typename F>
class DeferClass{
    public:
        // 万能引用： 使用模板参数T&&类模板参数时，可接收左值和右值
        // forward保证了在函数参数传递中能够正确转发参数类型
        // 根据参数类型确定触发左值还是右值，右值会触发移动语义，提高效率
        // 只有模板类型推导会触发引用折叠，完美转发用于正确处理模板编程中的万能引用的值类型
        DeferClass(F&& f) : m_fun(std::forward<F>(f)){}
        // const量都是左值
        DeferClass(const F&f) : m_fun(f){}
        ~DeferClass(){m_fun();}

        DeferClass(const DeferClass& e) = delete;
        DeferClass& operator=(const DeferClass& e) = delete;

    private:
    F m_fun;
};

#define __CONCAT(a,b) a##b
#define __DEFER__MAKER__(line) DeferClass __CONCAT(defer_placeholder,line) = [&]()

#undef DEFER
#define DEFER __DEFER__MAKER__(__LINE__)

// !end defer

// 调试输出 时间+内容
void DPrintf(const char* format, ...);

void myAssert(bool condition, std::string message = "Assertion failed!");

// TODO 
// 字符串格式化函数
template<typename... Args>
std::string format(const char* format_str,Args... args){
    int size_s = std::snprintf(nullptr,0,format_str,args...)+1;
    if(size_s<0){
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::vector<char> buf(size);
    std::snprintf(buf.data(),size,format_str,args...);
    return std::string(buf.data(),buf.data()+size-1);
}

// 获取当前时间
std::chrono::_V2::system_clock::time_point now();

// 随机超时选举时间
std::chrono::milliseconds getRandomizedElectionTimeout();
void sleepNMilliseconds(int N);

// 并发队列
template <typename T>
class LockQueue{
    public:
        // 入队
        void Push(const T& data){
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(data);
            m_condvariable.notify_one();
        }
        // 出队
        T Pop(){
            std::unique_lock<std::mutex> lock(m_mutex);
            while(m_queue.empty()){
                m_condvariable.wait(lock);
            }
            T resData = m_queue.front();
            m_queue.pop();
            return resData;
        }
        // 超时时间内是否有数据可读
        bool TimeoutPop(int timeout,T* resData){
            std::unique_lock<std::mutex> lock(m_mutex);

            // 计算超时时间点
            auto now = std::chrono::system_clock::now();
            int timeout_time = now + std::chrono::milliseconds(timeout);
            
            while(m_queue.empty()){
                // wait_until 被其它线程唤醒或是在超时时间后唤醒
                // std::cv_status::timeout 是一个枚举变量，与cv中的wait_until和wait_for配合使用
                // 用于判断是否超时
                if(m_condvariable.wait_until(lock,timeout)==std::cv_status::timeout)
                    // 超时时间内无变量返回
                    return false;
                else
                    continue;
            }
            // 通过传出参数返回值
            *resData = m_queue.front();
            m_queue.pop();
            // 超时时间内有变量返回
            return true;
        }
    private:
        std::queue<T> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_condvariable;
};

// 客户端到raft集群的命令
// 传递参数信息
// 序列化反序列化、调试方法(重载的<<)
class Op{
    public:

        // 序列化
        // 使用 Boost 的 text_oarchive 将对象序列化为字符串
        std::string asString() const {
            std::stringstream ss;
            boost::archive::text_oarchive oa(ss);
            oa << *this;
            return ss.str();
        }
        // 反序列化
        bool parseFromString(std::string str) {
            try {
                std::stringstream iss(str);
                boost::archive::text_iarchive ia(iss);
                ia >> *this;
                return true;    // TODO 异常处理
                // 添加异常处理
                // 输出控制台日志
            } catch (const boost::archive::archive_exception& e) {
                DPrintf("反序列化失败: %s", e.what());
                return false;
            }
        }

        // 输出运算符重载
        // 为了能够直接访问类的成员变量，需要将该函数声明为类的友元函数：
        // 流式输出取代字符串拼接
        // cout<<obj 输出类的成员变量

        // todo
        // 返回值左右？
        friend std::ostream& operator<<(std::ostream& os, const Op& obj) {
            return os << "Op(Operation=" << obj.Operation 
                      << ", Key=" << obj.Key 
                      << ", Value=" << obj.Value 
                      << ", ClientId=" << obj.ClientId 
                      << ", RequestId=" << obj.RequestId << ")";
        }

    private:
        // 传递参数
        std::string Operation;
        std::string Key;
        std::string Value;
        std::string ClientId;
        int RequestId;
        
        // boost序列化支持
        // 必须为 private 并声明 friend class boost::serialization::access，确保序列化逻辑的封装性。
        // 让序列化类可以访问私有成员
        friend class boost::serialization::access;
        // 序列化的函数，这一个函数完成对象的保存与恢复,这是侵入式的方法序列化，要更改类的代码
        template <class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & Operation;
            ar & Key;
            ar & Value;
            ar & ClientId;
            ar & RequestId;
        }
};

// kvserver reply err to clerk
// 
const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

// 动态获取可用端口，避免手动配置冲突。
bool isReleasePort(unsigned short usPort);
bool getReleasePort(short& port);

#endif