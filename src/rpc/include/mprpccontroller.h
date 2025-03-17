#pragma once
#ifndef __MPRPCCONTROLLER__H__
#define __MPRPCCONTROLLER__H__

#include <google/protobuf/service.h>
#include <string>

/*

追踪rpc方法的调用状态

*/

class MprpcController : public google::protobuf::RpcController{
    public:
        MprpcController();
        void Reset();
        bool Failed() const;
        std::string ErrorText() const ;
        void SetFailed(const std::string& reason);
    
        // TODO
        // 未实现具体的功能
        // void StartCancel();
        // bool IsCanceled() const;
        // void NotifyOnCancel(google::protobuf::Closure* callback);

        // 抽象基类，继承后需要实现基类的方法
        // 否则他也是抽象基类，不能被实例化
        
        void StartCancel();
        bool IsCanceled() const ;
        void NotifyOnCancel(google::protobuf::Closure* callback);

    private:
        // 一些状态
        bool m_failed;
        std::string m_errText;
};

#endif