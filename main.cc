#include "dlog.h"
#include "tc_server.h"
#include "config_file_reader.h"
#include "cache_pool.h"
#include "db_pool.h"
#include "api/api_upload.h"

#include <muzi/logger.h>

int main(int argc, const char *argv[])
{
    signal(SIGPIPE, SIG_IGN); //忽略SIGPIPE信号

    // 设置网络层logger
    muzi::gDefaultLogger.SetLogLevel(muzi::LogLevel::kError);

    // 1.read config file
    const char *config_file = nullptr;
    if (argc > 1)
        config_file = argv[1];
    else
        config_file = "tc_http_server.conf";
    
    std::cout << "Read config file: " << config_file << std::endl;

    CConfigFileReader config_reader;
    if(config_reader.ParseConf(config_file) != 0) {
        std::cout << config_file << " no exist, please check conf file" << std::endl;
        return -1;
    }

    // 2.set config
    char *log_level = config_reader.GetConfigName("log_level");   //读取日志设置级别
    if (!log_level) {
        LogError("config item missing, exit... log_level:{}", log_level);
        return -1;
    }
    DLog::SetLevel(log_level);   //设置日志打印级别

    // 短链主要是将图片链接转成短链
    char *str_enable_shorturl = config_reader.GetConfigName("enable_shorturl");
    LogInfo("enable_shorturl: {}", str_enable_shorturl);
    if(!str_enable_shorturl) {
        LogError("config item: enable_shorturl missing, exit... ");
        return -1;
    }
    
    const char *dfs_path_client = config_reader.GetConfigName("dfs_path_client"); // /etc/fdfs/client.conf
    const char *storage_web_server_ip = config_reader.GetConfigName("storage_web_server_ip"); //后续可以配置域名
    const char *storage_web_server_port = config_reader.GetConfigName("storage_web_server_port");
    uint16_t enable_shorturl = atoi(str_enable_shorturl);   //1开启短链，0不开启短链
    if (enable_shorturl)
    {
        const char *shorturl_server_address = config_reader.GetConfigName("shorturl_server_address");// 短链服务地址
        LogInfo("shorturl_server_address: {}", shorturl_server_address);
        if(!shorturl_server_address) {
            LogError("config item: shorturl_server_address missing, exit... ");
            return -1;
        }
        
        const char *shorturl_server_access_token = config_reader.GetConfigName("shorturl_server_access_token");
        LogInfo("shorturl_server_access_token: {}", shorturl_server_address);
        if(!shorturl_server_access_token) {
            LogError("config item:shorturl_server_access_token missing, exit... ");
            return -1;
        }
        // 将配置文件的参数传递给对应模块
        ApiUploadInit(dfs_path_client, storage_web_server_ip, storage_web_server_port, 
            shorturl_server_address, shorturl_server_access_token);
    } else {
        ApiUploadInit(dfs_path_client, storage_web_server_ip, storage_web_server_port, "", "");
    }
    
    // http监听地址和端口
    char *http_listen_ip = config_reader.GetConfigName("HttpListenIP");
    char *str_http_port = config_reader.GetConfigName("HttpPort"); 
    char *str_poller_num = config_reader.GetConfigName("poller_num");
    // 检测监听ip和端口是否存在
    if (!http_listen_ip || !str_http_port) {
        LogError("config item missing, exit... ip:{}, port:{}", http_listen_ip, str_http_port);
        return -1;
    }
    LogInfo("listen ip: {}, port: {}", http_listen_ip, str_http_port);
    if (!str_poller_num)
    {
        LogError("poller_num is missing.");
        return -1;
    }
    LogInfo("poller_num(include the main loop): {}", str_poller_num);
    uint16_t http_port = atoi(str_http_port);
    int poller_num = atoi(str_poller_num);
    assert(poller_num > 0);

    // 3.初始化mysql连接池，内部也会读取读取配置文件tc_http_server.conf
    CacheManager::SetConfPath(config_file); //设置配置文件路径
    CacheManager *cache_manager = CacheManager::getInstance();
    if (!cache_manager) {
        LogError("CacheManager init failed");
        return -1;
    }
    // 4.初始化redis连接池，内部也会读取读取配置文件tc_http_server.conf
    CDBManager::SetConfPath(config_file);   //设置配置文件路径
    CDBManager *db_manager = CDBManager::getInstance();
    if (!db_manager) {
        LogError("DBManager init failed");
        return -1;
    }

    // 5.启动http服务
    muzi::EventLoop loop;
    TCServer server(&loop, http_listen_ip, http_port);
    server.SetPollerNum(poller_num);
    server.Start();

    // 7.将当前进程id写入文件server.pid, 可以直接cat这个文件获取进程id
    WritePid();

    // 8.开启循环
    loop.Loop();
}
