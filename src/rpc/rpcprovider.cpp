#include "rpcprovider.h"
#include "rpcheader.pb.h"
#include "util.h"
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>
#include <fstream>
#include <string>

// 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
// 只是简单的把服务描述符和方法描述符全部保存在本地而已

// TODO 引入注册中心
void RpcProvider::NotifyService(google::protobuf::Service *service){

    const google::protobuf::ServiceDescriptor* sd = service->GetDescriptor();
    std::string service_name = sd->name();
    int method_count = sd->method_count();
    ServiceInfo serviceInfo;
    for(int i=0;i<method_count;++i){
        const google::protobuf::MethodDescriptor* md = sd->method(i);
        std::string method_name = md->name();
        serviceInfo.m_service = service;
        serviceInfo.m_method.insert({method_name,md});
    }
    m_serviceMap.insert({service_name,serviceInfo});
}

void RpcProvider::Run(int nodeIndex,short port){
    char* ipC;
    char hname[128];
    struct hostent* hent;
    gethostname(hname,sizeof(hname));
    for(int i=0;hent->h_addr_list[i];++i){
        ipC = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[i]));
    }
    std::string ip = std::string(ipC);

    // 写test>conf
    std::ofstream outfile("test.conf",std::ios::app);
    std::string node = "node"+std::to_string(nodeIndex);
    std::string node_ip = node+"Ip="+ip;
    std::string node_port = node+"Port="+std::to_string(port);

    outfile<<node_ip<<std::endl;
    outfile<<node_port<<std::endl;

    outfile.close();

    // muduo网络库
    // 开启网络服务
    muduo::net::InetAddress address(ip,port);

    // TcpServer
    m_muduo_server = std::make_shared<muduo::net::TcpServer>(&m_loop,address,"RpcProvider");

    m_muduo_server->setConnectionCallback(std::bind(&RpcProvider::OnConnection,this,std::placeholders::_1));
    m_muduo_server->setMessageCallback(std::bind(&RpcProvider::OnMessage,this,
        std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));

    m_muduo_server->setThreadNum(4);

    // rpc服务端准备启动，打印信息
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

    m_muduo_server->start();
    m_loop.loop();
}

RpcProvider::~RpcProvider(){
    std::cout << "[ func - RpcProvider::~RpcProvider() ]: ip和port信息：" << m_muduo_server->ipPort() << std::endl;
    m_loop.quit();
}

// 连接回调
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn){
    if(!conn->connected()){
        conn->shutdown();
    }
}

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn,muduo::net::Buffer* buffer,muduo::Timestamp Timestamp){

    // muduo将tcp缓冲区数据读入用户缓冲区
    std::string recv_buf = buffer->retrieveAllAsString();

    // 使用protobuf的CodedInputStream来解析数据流
    google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size());
    google::protobuf::io::CodedInputStream coded_input(&array_input);

    uint32_t header_size;
    
    coded_input.ReadVarint32(&header_size);

    std::string header_str;
    RPC::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;

    // 读取限制 避免读取过多
    google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size);
    coded_input.ReadString(&header_str,header_size);
    coded_input.PopLimit(msg_limit);

    uint32_t args_size;
    if(rpcHeader.ParseFromString(header_str)){
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }else{
        // 数据头反序列化失败
        std::cout << "rpc_header_str:" << header_str << " parse error!" << std::endl;
        return;
    }
    std::string args_str;
    bool read_args_success = coded_input.ReadString(&args_str,args_size);
    if(!read_args_success){
        // TODO
        // 错误处理
        /*  
            code here
        */
       return;
    }

      // 打印调试信息
     std::cout << "============================================" << std::endl;
     std::cout << "header_size: " << header_size << std::endl;
     std::cout << "rpc_header_str: " << header_str << std::endl;
     std::cout << "service_name: " << service_name << std::endl;
     std::cout << "method_name: " << method_name << std::endl;
     std::cout << "args_str: " << args_str << std::endl;
     std::cout << "============================================" << std::endl;

    // 获取服务名和方法名
    auto s_info = m_serviceMap.find(service_name);
    if( s_info == m_serviceMap.end()){
        // TODO

        return;
    }

    auto method_info = s_info->second.m_method.find(method_name);
    if(method_info==s_info->second.m_method.end()){
        // TODO

        return;
    }

    google::protobuf::Service* service = s_info->second.m_service;
    const google::protobuf::MethodDescriptor* method = method_info->second;

    // 生成rpc方法调用的请求request和响应response参数,由于是rpc的请求，因此请求需要通过request来序列化
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if(!request->ParseFromString(args_str)){
        // TODO

        return ;
    }

    google::protobuf::Message* response = service->GetResponsePrototype(method).New();

    // 绑定一个回调
    google::protobuf::Closure *done =
    google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr &, google::protobuf::Message *>(
        this, &RpcProvider::SendRpcResponse, conn, response);

    // 真正调用方法
    service->CallMethod(method,nullptr,request,response,done);

}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& conn,google::protobuf::Message* response){
    std::string response_str;
    if(response->SerializeToString(&response_str)){
        conn->send(response_str);
    }else{
        std::cout << "serialize response_str error!" << std::endl;
    }
}