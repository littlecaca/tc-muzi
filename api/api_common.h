#ifndef _API_COMMON_H_
#define _API_COMMON_H_
#include "cache_pool.h"
#include "db_pool.h"
#include "tc_common.h"
#include <string>
#include "dlog.h"
#include "json/json.h"
#include "redis_keys.h"


#define HTTP_RESPONSE_HTML_MAX 4096
#define HTTP_RESPONSE_HTML                                                     \
    "HTTP/1.1 200 OK\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"
 
using std::string;
//获取用户文件个数
int CacheSetCount(CacheConn *cache_conn, string key, int64_t count);
int CacheGetCount(CacheConn *cache_conn, string key, int64_t &count);
int CacheIncrCount(CacheConn *cache_conn, string key);
int CacheDecrCount(CacheConn *cache_conn, string key);
int DBGetUserFilesCountByUsername(CDBConn *db_conn, string user_name, int &count);
bool HasFields(const Json::Value &root, initializer_list<const char *>fields);
template <typename... Args>
std::string FormatString(const std::string &format, Args... args) {
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) +
                1; // Extra space for '\0'
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(),
                       buf.get() + size - 1); // We don't want the '\0' inside
}

//根据file id将文件从fastdfs删除
int RemoveFileFromFastDfs(const char *fileid);
int DBGetShareFilesCount(CDBConn *db_conn, int &count);

//mysql存储的部分信息加载redis缓存
int ApiInit();
#endif
