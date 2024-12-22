#include <algorithm>

#include <cmath>
#include <sys/wait.h>

#include "dlog.h"
#include "api_upload.h"
#include "db_pool.h"

//grpc远程调用
#include <grpcpp/grpcpp.h>
#include "shorturl.pb.h"
#include "shorturl.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using shorturl_voice::ShortUrl;     //服务
using shorturl_voice::Url;
// using shorturl_voice::ShortKey;

namespace {
string s_dfs_path_client;
string s_storage_web_server_ip;
string s_storage_web_server_port;
string s_shorturl_server_address;
string s_shorturl_server_access_token;

class ShortUrlClient {
 public:
  ShortUrlClient(std::shared_ptr<Channel> channel)
      : stub_(ShortUrl::NewStub(channel)) {}

  
  int GetShortUrl(const std::string& url, const bool is_public, std::string &short_key) {
    // Data we are sending to the server.
    Url request;
    request.set_url(url);
    request.set_ispublic(is_public);
 
    // Container for the data we expect from the server.
    Url reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    string meta_key = "authorization";  // 自定义，目前和shorurl-server是写的固定key
    context.AddMetadata(meta_key, s_shorturl_server_access_token);
    // The actual RPC.
    Status status = stub_->GetShortUrl(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
        short_key = reply.url();   
        return 0;
    } else {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return -1;
    }
    return 0;
  }

 private:
  std::unique_ptr<ShortUrl::Stub> stub_;
};

/// @brief 将长链转成短链
/// @param origin_url 
/// @param short_url 
/// @return 
int originUrl2ShortUrl(const string &origin_url, string &short_url)
{
    // sksSdkFdDngKie8n05nr9jey84prEhw5u43th0yi294780yjr3h7
    ShortUrlClient client(grpc::CreateChannel(s_shorturl_server_address, 
        grpc::InsecureChannelCredentials()));

    int ret = client.GetShortUrl(origin_url, true, short_url);
    LogInfo("origin_url = {}", origin_url);
    if(ret == 0) {
        LogInfo("short_url = {}", short_url);
    }
    else {
        LogError("get short_url failed");
    }
    return ret;
}

struct UploadMsg
{
    string &file_name() { return msg_["file_name"]; }
    string &file_content_type() { return msg_["file_content_type"]; }
    string &file_path () { return msg_["file_path"]; }
    string &file_md5() { return msg_["file_md5"]; }
    string &file_size() { return msg_["file_size"]; }
    string &user() { return msg_["user"]; }

    static const int field_cnt = 6;

    friend int decodeMsg(const std::string &str_json, UploadMsg &msg, const string &split);
private:
    unordered_map<string, string> msg_;
};

void strip(string &str)
{
    auto it1 = str.begin();
    while (it1 != str.end() && (*it1 == '\r' || *it1 == '\n'))
        ++it1;
    auto it2 = str.rbegin();
    while (it2 != str.rend() && (*it2 == '\r' || *it2 == '\n'))
        ++it2;
    str = string(it1, it2.base());
}

// 解析upload消息信息
int decodeMsg(const std::string &str_json, UploadMsg &msg, const string &split) {
    static string left_flag = "name=\"";
    static string right_flag = "\"";

    auto cur = str_json.begin(), end = str_json.end();
    int parsed_cnt = 0;
    string field, value;

    cur = std::search(cur, end, split.begin(), split.end());
    while (cur != end)
    {
        cur += split.size();

        auto next = std::search(cur, end, split.begin(), split.end());
        if (next == end) break;

        cur = std::search(cur, next, left_flag.begin(), left_flag.end());
        if (cur == next) break;
        cur += left_flag.size();

        auto next2 = std::search(cur, next, right_flag.begin(), right_flag.end());
        if (next2 == next) break;

        field = string(cur, next2);
        cur = next2 + right_flag.size();
        value = string(cur, next - 1);
        strip(value);
        cur = next;
        msg.msg_[field] = value;
        
        ++parsed_cnt;
    }

    if (parsed_cnt != UploadMsg::field_cnt)
        return -1;

    return 0;
}

int encodeUploadJson(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int ApiUploadInit(const char *dfs_path_client, const char *storage_web_server_ip, const char *storage_web_server_port) {
    s_dfs_path_client = dfs_path_client;
    s_storage_web_server_ip = storage_web_server_ip;
    s_storage_web_server_port = storage_web_server_port;
    return 0;
}

/* -------------------------------------------*/
/**
 * @brief  将一个本地文件上传到 后台分布式文件系统中
 * 对应 fdfs_upload_file /etc/fdfs/client.conf  完整文件路径
 *
 * @param file_path  (in) 本地文件的路径
 * @param fileid    (out)得到上传之后的文件ID路径
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int uploadFileToFastDfs(char *file_path, char *fileid) {
    int ret = 0;

    pid_t pid;
    int fd[2];

    // 无名管道的创建
    if (pipe(fd) < 0) // fd[0] → r； fd[1] → w  获取上传后返回的信息 fileid
    {
        LogError("pipe error");
        ret = -1;
        goto END;
    }

    // 创建进程
    pid = fork();
    if (pid < 0)  // 进程创建失败
    {
        LogError("fork error");
        ret = -1;
        goto END;
    }

    if (pid == 0) { //子进程
        // 关闭读端
        close(fd[0]);
        // 将标准输出 重定向写管道
        // 当fileid产生时输出到管道fd[1]
        dup2(fd[1],  STDOUT_FILENO); // 往标准输出写的东西都会重定向到fd所指向的文件,

        // fdfs_upload_file /etc/fdfs/client.conf 123.txt
        // printf("fdfs_upload_file %s %s %s\n", fdfs_cli_conf_path, filename,
        // file_path);
        // 通过execlp执行fdfs_upload_file
        // 如果函数调用成功,进程自己的执行代码就会变成加载程序的代码,execlp()后边的代码也就不会执行了.
        execlp("fdfs_upload_file", "fdfs_upload_file",
               s_dfs_path_client.c_str(), file_path, NULL);
        // 执行正常不会跑下面的代码
        // 执行失败
        LogError("execlp fdfs_upload_file error");

        close(fd[1]);
    } else { //父进程
        //关闭写端
        close(fd[1]);

        //从管道中去读数据
        read(fd[0], fileid, TEMP_BUF_MAX_LEN); // 等待管道写入然后读取

        LogInfo("fileid1: {}", fileid);
        //去掉一个字符串两边的空白字符
        TrimSpace(fileid);

        if (strlen(fileid) == 0) {
            LogError("upload failed");
            ret = -1;
            goto END;
        }
        LogInfo("fileid2: {}", fileid);

        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);
    }

END:
    return ret;
}

/* -------------------------------------------*/
/**
 * @brief  封装文件存储在分布式系统中的完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int getFullurlByFileid(char *fileid, char *fdfs_file_url) {
    int ret = 0;

    char *p = NULL;
    char *q = NULL;
    char *k = NULL;

    char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_host_name[HOST_NAME_LEN] = {0}; // storage所在服务器ip地址

    pid_t pid;
    int fd[2];

    //无名管道的创建
    if (pipe(fd) < 0) {
        LogError("pipe error");
        ret = -1;
        goto END;
    }

    //创建进程
    pid = fork();
    if (pid < 0) //进程创建失败
    {
        LogError("fork error");
        ret = -1;
        goto END;
    }

    if (pid == 0) //子进程
    {
        //关闭读端
        close(fd[0]);

        //将标准输出 重定向 写管道
        dup2(fd[1], STDOUT_FILENO); // dup2(fd[1], 1);

        execlp("fdfs_file_info", "fdfs_file_info", s_dfs_path_client.c_str(),
               fileid, NULL);

        //执行失败
        LogError("execlp fdfs_file_info error");

        close(fd[1]);
    } else //父进程
    {
        //关闭写端
        close(fd[1]);

        //从管道中去读数据
        read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);

        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);
        // LogInfo("fdfs_file_stat_buf: {}", fdfs_file_stat_buf);
        //拼接上传文件的完整url地址--->http://host_name/group1/M00/00/00/D12313123232312.png
        p = strstr(fdfs_file_stat_buf, "source ip address: ");

        q = p + strlen("source ip address: ");
        k = strstr(q, "\n");

        strncpy(fdfs_file_host_name, q, k - q);
        fdfs_file_host_name[k - q] =
            '\0'; // 这里这个获取回来只是局域网的ip地址，在讲fastdfs原理的时候再继续讲这个问题

        LogInfo("host_name:{}, fdfs_file_host_name: {}", s_storage_web_server_ip, fdfs_file_host_name);

        // storage_web_server服务器的端口
        strcat(fdfs_file_url, "http://");
        strcat(fdfs_file_url, s_storage_web_server_ip.c_str());
        strcat(fdfs_file_url, ":");
        strcat(fdfs_file_url, s_storage_web_server_port.c_str());
        strcat(fdfs_file_url, "/");
        strcat(fdfs_file_url, fileid);

        LogInfo("fdfs_file_url:{}", fdfs_file_url);
    }

END:

    return ret;
}

int storeFileinfo(CDBConn *db_conn, const char *user,
                  const char *filename, const char *md5, long size, const char *fileid,
                  const char *fdfs_file_url) {
    int ret = 0;
    time_t now;
    char create_time[TIME_STRING_LEN];
    char suffix[SUFFIX_LEN];
    char sql_cmd[SQL_MAX_LEN] = {0};

    //得到文件后缀字符串 如果非法文件后缀,返回"null"
    GetFileSuffix(filename, suffix); // mp4, jpg, png

    // sql 语句
    /*
       -- =============================================== 文件信息表
       -- md5 文件md5
       -- file_id 文件id
       -- url 文件url
       -- size 文件大小, 以字节为单位
       -- type 文件类型： png, zip, mp4……
       -- count 文件引用计数， 默认为1， 每增加一个用户拥有此文件，此计数器+1
       */
    sprintf(sql_cmd,
            "insert into file_info (md5, file_id, url, size, type, count) "
            "values ('%s', '%s', '%s', '%ld', '%s', %d)",
            md5, fileid, fdfs_file_url, size, suffix, 1);
     LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteCreate(sql_cmd)) //执行sql语句
    {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));

    /*
       -- =============================================== 用户文件列表
       -- user 文件所属用户
       -- md5 文件md5
       -- create_time 文件创建时间
       -- file_name 文件名字
       -- shared_status 共享状态, 0为没有共享， 1为共享
       -- pv 文件下载量，默认值为0，下载一次加1
       */
    // sql语句
    sprintf(sql_cmd,
            "insert into user_file_list(user, md5, create_time, file_name, "
            "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
            user, md5, create_time, filename, 0, 0);
    LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteCreate(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

END:
    return ret;
}
}   // namespace

int ApiUpload(string &post_data, string &str_json, const string &split)
{
    LogInfo("boundary:{}", split);
    // 将临时文件上传到fastdfs，调用fastdfs客户端
    char suffix[SUFFIX_LEN] = {0};
    char new_file_path[128] = {0};
    char fileid[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_url[FILE_URL_LEN] = {0};
    long long_file_size = 0;
    string short_url;

    // 获取数据库连接
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);

    // 解析post_data
    UploadMsg msg;
    int ret = decodeMsg(post_data, msg, split);
    if (ret != 0)
    {
        LogError("post_data解析失败");
        goto END;
    }
    else {
        LogInfo("file_name: {}, file_path: {}, file_md5: {}, file_size: {}, user: {}", 
            msg.file_name(), msg.file_path(), msg.file_md5(), msg.file_size(), msg.user());
    }

    GetFileSuffix(msg.file_name().c_str(), suffix);
    strcat(new_file_path, msg.file_path().c_str()); // /root/tmp/1/0045118901
    strcat(new_file_path, ".");  // /root/tmp/1/0045118901.
    strcat(new_file_path, suffix); // /root/tmp/1/0045118901.txt
    // 重命名 修改文件名  fastdfs 他需要带后缀的文件
    ret = rename(msg.file_path().c_str(), new_file_path); /// /root/tmp/1/0045118901 ->  /root/tmp/1/0045118901.txt
    if (ret < 0) {
        LogError("rename {} to {} failed", msg.file_path(), new_file_path);
        goto END;
    }

    //===============> 将该文件存入fastDFS中,并得到文件的file_id <============
    LogInfo("uploadFileToFastDfs, file_name:{}, file_path:{}, new_file_path:{}", msg.file_name(), msg.file_path(), new_file_path);
    if (uploadFileToFastDfs(new_file_path, fileid) < 0) {
        LogError("uploadFileToFastDfs failed, unlink: {}", new_file_path);
        ret = unlink(new_file_path);
        if (ret != 0) {
            LogError("unlink: {} failed", new_file_path); // 删除失败则需要有个监控重新清除过期的临时文件，比如过期两天的都删除
        }
        // ret = -1;
        goto END;
    }
    //================> 删除本地临时存放的上传文件 <===============
    LogInfo("unlink: {}", new_file_path);
    ret = unlink(new_file_path);
    if (ret != 0) {
        LogWarn("unlink: {} failed", new_file_path); // 删除失败则需要有个监控重新清除过期的临时文件，比如过期两天的都删除
    }
    //================> 得到文件所存放storage的host_name <=================
    // 拼接出完整的http地址
    LogInfo("getFullurlByFileid, fileid: {}", fileid);
    if (getFullurlByFileid(fileid, fdfs_file_url) < 0) {
        LogError("getFullurlByFileid failed ");
        // ret = -1;
        goto END;
    }

    // 如果需要使用短链服务，当短链不开启时程序自动将s_shorturl_server_address设置为empty
    if(!s_shorturl_server_address.empty()) {
        ret = originUrl2ShortUrl(fdfs_file_url, short_url);
        if(ret != 0) {
            short_url = fdfs_file_url; // 如果调用失败则保持原来的url
            LogWarn("originUrl2ShortUrl failed, no use short url");
        } else {
            LogInfo("originUrl2ShortUrl ok");
        }
    } else {
        short_url = fdfs_file_url;
        LogWarn("s_shorturl_server_address null, no use short_url");
    }
    LogInfo("storeFileinfo, origin url: {} -> short url: {}", fdfs_file_url, short_url);

    //===============> 将该文件的FastDFS相关信息存入mysql中 <======
    // 把文件写入file_info
    if (storeFileinfo(db_conn, msg.user().c_str(), msg.file_name().c_str(),
        msg.file_md5().c_str(), long_file_size, fileid, short_url.c_str()) < 0) {
        LogError("storeFileinfo failed ");
        // ret = -1;
        // 严谨而言，这里需要删除 已经上传的文件
        goto END;
    }

    encodeUploadJson(0, str_json);
    return 0;

END:
    encodeUploadJson(1, str_json);
    return 0;
}

int ApiUploadInit(const char *dfs_path_client, const char *storage_web_server_ip, const char *storage_web_server_port,
    const char *shorturl_server_address, const char *access_token) {
    s_dfs_path_client = dfs_path_client;
    s_storage_web_server_ip = storage_web_server_ip;
    s_storage_web_server_port = storage_web_server_port;
    s_shorturl_server_address = shorturl_server_address;
    s_shorturl_server_access_token = access_token;
    return 0;
}
