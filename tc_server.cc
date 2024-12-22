#include <any>
#include <cassert>
#include <cstring>
#include <memory>

#include <cstdint>
#include <muzi/address.h>
#include <muzi/event_loop.h>
#include <muzi/inet_address.h>
#include <muzi/noncopyable.h>
#include <muzi/tcp_server.h>

#include "api_common.h"
#include "dlog.h"
#include "cache_pool.h"
#include "http_parser_wrapper.h"
#include "api_login.h"
#include "api_myfiles.h"
#include "api_register.h"
#include "api_md5.h"
#include "api_upload.h"
#include "api_sharepicture.h"
#include "api_dealfile.h"
#include "api_sharefiles.h"
#include "api_deal_sharefile.h"
#include "tc_server.h"


void TCServer::OnRead(const muzi::TcpConnectionPtr &conn, muzi::Buffer *buf, muzi::Timestamp time)
{
    LogInfo("connection = {}, socket_fd = {}", conn->GetName(), conn->GetSocketFd());

    if (buf->GetPrependableBytes() > 2048) {
        LogError("get too much data: {}", buf->PeekAllAsString());
        conn->ForceClose();
        return;
    }

    std::string in_buf = buf->RetriveAllAsString();

    LogInfo("buf_len: {}, in_buf: {}", in_buf.size(), in_buf); //将请求的数据都打印出来，方便调试分析http请求
    // 解析http数据
    auto &parser = std::any_cast<std::shared_ptr<ConnContext> &>(conn->GetContext())->parser_;

    parser.ParseHttpContent(in_buf.c_str(), in_buf.size()); // 1. 从socket接口读取数据；2.然后把数据放到buffer in_buf; 3.http解析
    if (parser.IsReadAll()) {
        string url = parser.GetUrl();
        string content = parser.GetBodyContent();
        LogInfo("url: {}", url);                     // for debug
        // 根据url处理不同的业务 
        if (strncmp(url.c_str(), "/api/reg", 8) == 0) { // 注册  url 路由。 根据根据url快速找到对应的处理函数， 能不能使用map，hash
            _HandleRegisterRequest(conn, url, content);
        } else if (strncmp(url.c_str(), "/api/login", 10) == 0) { // 登录
            _HandleLoginRequest(conn, url, content);
        } else if (strncmp(url.c_str(), "/api/myfiles", 10) == 0) { //获取我的文件数量
            _HandleMyfilesRequest(conn, url, content);
        }  else if (strncmp(url.c_str(), "/api/md5", 8) == 0) {       //
            _HandleMd5Request(conn, url, content);                     // 处理
        } else if (strncmp(url.c_str(), "/api/upload", 11) == 0) {   // 上传
            char *ct = parser.GetContentType();
            char *p = strstr(ct, "boundary=");
            string boundary = p ? p + 9 : "";
            boundary.insert(boundary.begin(), '-');
            _HandleUploadRequest(conn, content, boundary);
        } else if (strncmp(url.c_str(), "/api/dealfile", 13) == 0) {
            _HandleDealFileRequest(conn, url, content);
        } else if (strncmp(url.c_str(), "/api/sharefiles", 15) == 0) {
            _HandleShareFilesRequest(conn, url, content);
            // _HandleSharePictureRequest(conn, url, content);               
        } else if (strncmp(url.c_str(), "/api/sharepic", 13) == 0) {
            _HandleSharePictureRequest(conn, url, content);               
        } else if (strncmp(url.c_str(), "/api/dealsharefile", 18) == 0) { //
            _HandleDealShareFileRequest(conn, url, content);
        } else {
            LogError("url unknown, url= {}", url);
            conn->ForceClose();
        }
    }
}

// 账号注册处理
void TCServer::_HandleRegisterRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data) {
    string resp_json;
	int ret = ApiRegisterUser(post_data, resp_json);
	char http_body [HTTP_RESPONSE_HTML_MAX];
	uint32_t ulen = resp_json.length();
	int written = snprintf(http_body, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen,
        resp_json.c_str()); 	
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)http_body, written);
}

// 账号登陆处理 /api/login
void TCServer::_HandleLoginRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data)
{
	string resp_json;
	int ret = ApiUserLogin( post_data, resp_json);
	char http_body [HTTP_RESPONSE_HTML_MAX];
	uint32_t ulen = resp_json.length();
	int written = snprintf(http_body, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen,
        resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)http_body, written);
}

void TCServer::_HandleMyfilesRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data) {
    string resp_json;
    int ret = ApiMyfiles(url, post_data, resp_json);
    char http_body [HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    // LogInfo("json size: {}", resp_json.size());
    int written = snprintf(http_body, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)http_body, written);
}

void TCServer::_HandleMd5Request(const muzi::TcpConnectionPtr &conn, string &url, string &post_data) {
    string resp_json;
    int ret = ApiMd5(url, post_data, resp_json);
    char http_body[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(http_body, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)http_body, written);
}

/*
OnRead, buf_len=1321, conn_handle=2, POST /api/upload HTTP/1.0
Host: 127.0.0.1:8081
Connection: close
Content-Length: 722
Accept: application/json, text/plain,
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML,
like Gecko) Chrome/106.0.0.0 Safari/537.36 Edg/106.0.1370.52 Content-Type:
multipart/form-data; boundary=----WebKitFormBoundaryjWE3qXXORSg2hZiB Origin:
http://114.215.169.66 Referer: http://114.215.169.66/myFiles Accept-Encoding:
gzip, deflate Accept-Language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6
Cookie: userName=qingfuliao; token=e4252ae6e49176d51a5e87b41b6b9312

------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="file_name"

config.ini
------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="file_content_type"

application/octet-stream
------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="file_path"

/root/tmp/5/0034880075
------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="file_md5"

10f06f4707e9d108e9a9838de0f8ee33
------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="file_size"

20
------WebKitFormBoundaryjWE3qXXORSg2hZiB
Content-Disposition: form-data; name="user"

qingfuliao
------WebKitFormBoundaryjWE3qXXORSg2hZiB--
*/
//

void TCServer::_HandleUploadRequest(const muzi::TcpConnectionPtr &conn, string &post_data, const string &split) {
    string resp_json;
    int ret = ApiUpload(post_data, resp_json, split);
    char http_body[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(http_body, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)http_body, written); // 返回值暂时不做处理
}  

void TCServer::_HandleSharePictureRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data) {
    string resp_json;
    int ret = ApiSharePicture(url, post_data, resp_json);
    char szContent[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(szContent, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)szContent, written);
}

void TCServer::_HandleDealFileRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data)
{
    string resp_json;
    int ret = ApiDealfile(url, post_data, resp_json);
    char szContent[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(szContent, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)szContent, written);
}

void TCServer::_HandleShareFilesRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data)
{
    string resp_json;
    int ret = ApiSharefile(url, post_data, resp_json);
    char szContent[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(szContent, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)szContent, written);
}

 void TCServer::_HandleDealShareFileRequest(const muzi::TcpConnectionPtr &conn, string &url, string &post_data)
 {
    string resp_json;
    int ret = ApiDealsharefile(url, post_data, resp_json);
    char szContent[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = resp_json.length();
    int written = snprintf(szContent, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen, resp_json.c_str());
    if (written >= HTTP_RESPONSE_HTML_MAX)
        written = HTTP_RESPONSE_HTML_MAX - 1;
    conn->Send((void *)szContent, written);
 }
