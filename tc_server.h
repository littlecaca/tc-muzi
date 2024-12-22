#pragma once

#include <cstdint>
#include <muzi/address.h>
#include <muzi/event_loop.h>
#include <muzi/inet_address.h>
#include <muzi/noncopyable.h>
#include <muzi/tcp_server.h>

#include "dlog.h"
#include "http_parser_wrapper.h"

class TCServer : muzi::noncopyable
{
public:
    TCServer(muzi::EventLoop *loop, const std::string &ip, uint16_t port)
        : server_(loop, muzi::InetAddress(ip, port), "TCServer", true)
        
    {
        server_.SetConnectionCallback(&OnConnection);
        server_.SetMessageCallback(&OnRead);
        server_.SetWriteCompleteCallback(&OnWriteComlete);
    }

    void Start()
    {
        server_.Start();
    }

    void SetPollerNum(size_t cnt)
    {
        server_.SetThreadNum(cnt - 1);
    }

    static void OnRead(const muzi::TcpConnectionPtr &conn, muzi::Buffer *buf, muzi::Timestamp time);
    
    static void OnConnection(const muzi::TcpConnectionPtr &conn)
    {
        conn->SetContext(std::make_shared<ConnContext>());
    }

    static void OnWriteComlete(const muzi::TcpConnectionPtr &conn)
    {
        LogInfo("WriteComplete: {}", conn->GetName());
        conn->ForceClose();
    }

private:
    struct ConnContext
    {
        CHttpParserWrapper parser_;
    };

    // 账号注册处理
    static void _HandleRegisterRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    // 账号登陆处理
    static void _HandleLoginRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    //获取我的文件列表
    static void _HandleMyfilesRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    //秒传
    static void _HandleMd5Request(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    //文件上传
    static void _HandleUploadRequest(const muzi::TcpConnectionPtr &conn, string &post_data, const string &split);
    //分享图片相关操作：分享、浏览、取消
    static void _HandleSharePictureRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    static void _HandleDealFileRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    static void _HandleDealShareFileRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
    static void _HandleShareFilesRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data);
private:
    muzi::TcpServer server_;
};
