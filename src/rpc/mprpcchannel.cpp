#include "mprpcchannel.h"
#include <string>
#include "rpcheader.pb.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include "mprpccontroller.h"
#include "util.h"

/*
所有由本地stub代理的对象的rpc调用都会通过channel到这里来
统一做rpc调用
*/
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
    google::protobuf::RpcController *controller,
    const google::protobuf::Message *request,
    google::protobuf::Message *response,
    google::protobuf::Closure *done)
{

    // TODO
    // 与rpc callee建立连接的ip:port应从注册中心获取

    // 重试建立连接
    if( -1 == m_clientFd){
        std::string errMsg;
        bool ok = newConnect(m_ip.c_str(), m_port,errMsg);
        if(!ok){
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败",m_ip.c_str(),m_port);
            controller->SetFailed(errMsg);
            return ;
        }else{
            DPrintf("[func-MprpcChannel::CallMethod]连接ip：{%s} port{%d}成功",m_ip.c_str(),m_port);
        }
    }

    // 开始构造rpc请求消息结构
    // header_size + header + args
    // header = service_name + method_name + args_size

    const google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();

    RPC::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);

    // 参数大小
    uint32_t args_size;
    std::string args_str;
    if(request->SerializeToString(&args_str)){
        args_size = static_cast<uint32_t>(args_str.size());
    }else{
        controller->SetFailed(" Serialize request failed!");
        return;
    }
    rpcHeader.set_args_size(args_size);

    // 头大小
    uint32_t header_size;
    std::string header_str;
    if(rpcHeader.SerializeToString(&header_str)){
        header_size = static_cast<uint32_t>(header_str.size());
    }else{
        controller->SetFailed(" Serialize header failed!");
        return;
    }

    std::string send_rpc_str;
    // 留下前四个byte做uint32的固定存放位置表示头大小
    send_rpc_str.insert(0,std::string((char*)header_size),4);
    send_rpc_str += header_str;
    send_rpc_str += args_str;

    // 打印调试信息
    std::cout << "============================================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str: " << header_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "============================================" << std::endl;

    // 发送rpc请求
    // 通过自定义消息结构解决粘包问题
    // 服务端先读4字节 获取头大小 读取头
    // 从头反序列化获取参数大小 读取方法参数
    while(-1 == send(m_clientFd,send_rpc_str.c_str(),send_rpc_str.size(),0)){
        std::string errtxt;
        sprintf(errtxt.data()," Send rpc error! Errno: %d",errno);
        std::cout<<"尝试重新连接，对方ip："<<m_ip<<" 对方端口"<<m_port<<std::endl;
        close(m_clientFd);
        m_clientFd=-1;
        std::string errMsg;
        bool ok = newConnect(m_ip.c_str(),m_port,errMsg);
        if(!ok){
            controller->SetFailed(errMsg);
            return;
        }
    }

    // 接收rpc响应
    // 通过固定消息大小解决粘包问题
    std::string recv_buf;
    int recv_size;
    // 固定读写大小
    int read_size = 1024;
    if( -1 == (recv_size=recv(m_clientFd,recv_buf.data(),read_size,0))){
        close(m_clientFd); m_clientFd = -1;
        std::string errtxt;
        sprintf(errtxt.data(), "  Recv error! errno: %d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    // TODO
    // 反序列化rpc调用的响应数据
    // std::string response_str(recv_buf, 0, recv_size);
    // bug：出现问题，recv_buf中遇到\0后面的数据就存不下来了，导致反序列化失败
    if(!response->ParseFromArray(recv_buf.c_str(),recv_size)){
        std::string errtxt;
        sprintf(errtxt.data()," Response parse error! Errno: %d",errno);
        controller->SetFailed(errtxt);
        return;
    }

}


// socket 编程
bool MprpcChannel::newConnect(const char *ip, uint16_t port,std::string& errMsg)
{

    int clientFd = socket(PF_INET,SOCK_STREAM,0);
    if( -1 == clientFd){
        m_clientFd = -1;
        std::string errtxt;
        sprintf(errtxt.data()," Create socket error! Errno: %d",errno);
        errMsg = std::move(errtxt);
        return false;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if(-1==connect(clientFd,(sockaddr*)&addr,sizeof(addr))){
        close(clientFd);
        m_clientFd = -1;
        std::string errtxt;
        sprintf(errtxt.data()," Connect failed! Errno: %d",errno);
        errMsg = std::move(errtxt);
        return false;
    }
    m_clientFd = clientFd;
    return true;
}

MprpcChannel::MprpcChannel(const std::string& ip, const short& port,bool connectNow)
    :m_ip(ip)
    ,m_port(port) 
    ,m_clientFd(-1)
{
    if(!connectNow)  return;

    std::string errMsg;
    bool ok = newConnect(ip.c_str(),port,errMsg);
    int try_time = 3;
    while(!ok && try_time--){
        std::cout<<" [ RpcChannel Connect Error: "<<errMsg<<" ] "<<std::endl;
        ok = newConnect(ip.c_str(),port,errMsg);
    }
}