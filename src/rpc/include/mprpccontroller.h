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
        std::string ErrText() const ;
        void SetFailed(const std::string& reason);
    
        // TODO
        // 未实现具体的功能
        // void StartCancel();
        // bool IsCanceled() const;
        // void NotifyOnCancel(google::protobuf::Closure* callback);


    private:
        // 一些状态
        bool m_failed;
        std::string m_errText;
};

#endif