#include "api_dealfile.h"
#include "api_common.h"
#include "redis_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define ExtractField(root, field) \
    if (root[#field].isNull()) { LogInfo("{} is null.", #field); \
        encodeSharefilesJson(1, 0, resp_json); \
        return -1; } \
    else { field = root[#field].asString(); }

#define ExtractFieldAsInt(root, field) \
    if (root[#field].isNull()) { LogInfo("{} is null.", #field); \
        encodeSharefilesJson(1, 0, resp_json); \
        return -1; } \
    else { field = root[#field].asInt(); }


//获取共享文件数量
int getShareFilesCount(CDBConn *db_conn, CacheConn *cache_conn, int &count) {
    int ret = 0;
    int64_t file_count = 0;

    // 先查看用户是否存在
    string str_sql;

    // 1. 先从redis里面获取，如果数量为0则从mysql查询确定是否为0
    if (CacheGetCount(cache_conn, FILE_PUBLIC_COUNT, file_count) < 0) {
        LogWarn("CacheGetCount FILE_PUBLIC_COUNT failed");
        ret = -1;
    }

    if (file_count == 0) {
        // 从mysql加载
        if (DBGetShareFilesCount(db_conn, count) < 0) {
            LogError("DBGetShareFilesCount failed");
            return -1;
        }
        file_count = (int64_t)count;
        if (CacheSetCount(cache_conn, FILE_PUBLIC_COUNT, file_count) <
            0) // 失败了那下次继续从mysql加载
        {
            LogError("CacheSetCount FILE_PUBLIC_COUNT failed");
            return -1;
        }
        ret = 0;
    }
    count = file_count;

    return ret;
}


//获取共享文件个数
int handleGetSharefilesCount(int &count) {
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    int ret = 0;
    ret = getShareFilesCount(db_conn, cache_conn, count);

    return ret;
}

int encodeSharefilesJson(int ret, int total, string &resp_json) {
    Json::Value root;
    root["code"] = ret;
    if (ret == 0) {
        root["total"] = total; // 正常返回的时候才写入total
    }
    Json::FastWriter writer;
    resp_json = writer.write(root);
    return 0;
}


//获 取共享文件排行版
//按下载量降序127.0.0.1:80/api/sharefiles?cmd=pvdesc
void handleGetRankingFilelist(int start, int count, string &str_json) {
    /*
    a) mysql共享文件数量和redis共享文件数量对比，判断是否相等
    b) 如果不相等，清空redis数据，从mysql中导入数据到redis (mysql和redis交互)
    c) 从redis读取数据，给前端反馈相应信息
    */

    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int total = 0;
    char filename[1024] = {0};
    int sql_num;
    int redis_num;
    int score;
    int end;
    RVALUES value = NULL;
    Json::Value root;
    Json::Value files;
    int file_count = 0;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CResultSet *pCResultSet = NULL;

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    // 获取共享文件的总数量
    ret = getShareFilesCount(db_conn, cache_conn, total);
    if (ret != 0) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }
    //===1、mysql共享文件数量
    sql_num = total;

    //===2、redis共享文件数量
    redis_num = cache_conn->ZsetZcard(
        FILE_PUBLIC_ZSET); // Zcard 命令用于计算集合中元素的数量。
    if (redis_num == -1) {
        LogError("ZsetZcard  操作失败");
        ret = -1;
        goto END;
    }

    LogInfo("sql_num: {}, redis_num: {}", sql_num, redis_num);

    //===3、mysql共享文件数量和redis共享文件数量对比，判断是否相等
    if (redis_num != sql_num) // 如果数量太多会导致阻塞， redis mysql数据不一致怎么处理？
    { //===4、如果不相等，清空redis数据，重新从mysql中导入数据到redis
      //(mysql和redis交互)

        // a) 清空redis有序数据
        cache_conn->Del(FILE_PUBLIC_ZSET); // 删除集合
        cache_conn->Del(FILE_NAME_HASH); // 删除hash， 理解 这里hash和集合的关系

        // b) 从mysql中导入数据到redis
        // sql语句
        strcpy( sql_cmd, "select md5, file_name, pv from share_file_list order by pv desc");
        LogInfo("执行: {}", sql_cmd);

        pCResultSet = db_conn->ExecuteQuery(sql_cmd);
        if (!pCResultSet) {
            LogError("{} 操作失败", sql_cmd);
            ret = -1;
            goto END;
        }

        // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。
        // 当数据用完或发生错误时返回NULL.
        while (
            pCResultSet
                ->Next()) // 这里如果文件数量特别多，导致耗时严重，
                          // 可以这么去改进当
                          // mysql的记录和redis不一致的时候，开启一个后台线程去做同步
        {
            char field[1024] = {0};
            string md5 = pCResultSet->GetString("md5"); // 文件的MD5
            string file_name = pCResultSet->GetString("file_name"); // 文件名
            int pv = pCResultSet->GetInt("pv");
            sprintf(field, "%s%s", md5.c_str(),
                    file_name.c_str()); //文件标示，md5+文件名

            //增加有序集合成员
            cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, pv, field);

            //增加hash记录
            cache_conn->Hset(FILE_NAME_HASH, field, file_name);
        }
    }

    //===5、从redis读取数据，给前端反馈相应信息
    // char value[count][1024];
    value = (RVALUES)calloc(count, VALUES_ID_SIZE); //堆区请求空间
    if (value == NULL) {
        ret = -1;
        goto END;
    }

    file_count = 0;
    end = start + count - 1; //加载资源的结束位置
    //降序获取有序集合的元素   file_count获取实际返回的个数
    ret = cache_conn->ZsetZrevrange(FILE_PUBLIC_ZSET, start, end, value,
                                    file_count);
    if (ret != 0) {
        LogError("ZsetZrevrange 操作失败");
        ret = -1;
        goto END;
    }

    //遍历元素个数
    for (int i = 0; i < file_count; ++i) {
        // files[i]:
        Json::Value file;
        /*
        {
            "filename": "test.mp4",
            "pv": 0
        }
        */
        ret = cache_conn->Hget(FILE_NAME_HASH, value[i],
                               filename); // 通过 文件md5+文件名 -> 获取文件名
        if (ret != 0) {
            LogError("hget  操作失败");
            ret = -1;
            goto END;
        }
        file["filename"] = filename; // 返回文件名

        //-- pv 文件下载量
        score = cache_conn->ZsetGetScore(FILE_PUBLIC_ZSET, value[i]);
        if (score == -1) {
            LogError("ZsetGetScore  操作失败");
            ret = -1;
            goto END;
        }
        file["pv"] = score; // 返回下载量
        files[i] = file;
    }
    if (file_count > 0)
        root["files"] = files;

END:
    if (ret == 0) {
        root["code"] = 0;
        root["total"] = total;
        root["count"] = file_count;
    } else {
        root["code"] = 1;
    }
    str_json = root.toStyledString();
}

//获取共享文件列表
//获取用户文件信息 127.0.0.1:80/api/sharefiles&cmd=normal
void handleGetShareFilelist(int start, int count, string &str_json) {
    int ret = 0;
    string str_sql;
    int total = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    CResultSet *result_set = NULL;
    int file_count = 0;
    Json::Value root, files;

    ret = getShareFilesCount(db_conn, cache_conn, total); // 获取文件总数量
    if (ret < 0) {
        LogError("getShareFilesCount err");
        ret = -1;
        goto END;
    } else {
        if (total == 0) {
            ret = 0;
            goto END;
        }
    }

    // sql语句
    str_sql = FormatString(
        "select share_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
        share_file_list where file_info.md5 = share_file_list.md5 limit %d, %d",
        start, count);
    LogInfo("执行: {}", str_sql);
    result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set) {
        // 遍历所有的内容
        // 获取大小
        file_count = 0;
        while (result_set->Next()) {
            Json::Value file;
            file["user"] = result_set->GetString("user");
            file["md5"] = result_set->GetString("md5");
            file["file_name"] = result_set->GetString("file_name");
            file["share_status"] = result_set->GetInt("share_status");
            file["pv"] = result_set->GetInt("pv");
            file["create_time"] = result_set->GetString("create_time");
            file["url"] = result_set->GetString("url");
            file["size"] = result_set->GetInt("size");
            file["type"] = result_set->GetString("type");
            files[file_count] = file;
            file_count++;
        }
        if (file_count > 0)
            root["files"] = files;
        ret = 0;
        delete result_set;
    } else {
        ret = -1;
    }
END:
    if (ret == 0) {
        root["code"] = 0;
        root["total"] = total;
        root["count"] = file_count;
    } else {
        root["code"] = 1;
    }
    str_json = root.toStyledString();
}

int ApiSharefile(string &url, string &post_data, string &resp_json)\
{
char cmd[20];
    string user;
    string token;
    int start = 0; //文件起点
    int count = 0; //文件个数

    LogInfo("post_data: {}", post_data);

    //解析命令 解析url获取自定义参数
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    
    LogInfo("cmd = {}", cmd);
        Json::Value root;
    Json::Reader jsonReader;
    bool res = jsonReader.parse(post_data, root);
    if (!res)
    {
        LogInfo("parse post data failing.");
        encodeSharefilesJson(1, 0, resp_json);
        return -1;
    }
    //解析json
    ExtractFieldAsInt(root, count);
    
    
    if (strcmp(cmd, "count") == 0){ // count 获取用户文件个数
        // 解析json
        if (handleGetSharefilesCount(count) < 0){ //获取共享文件个数
            encodeSharefilesJson(1, 0, resp_json);
        } else {
            encodeSharefilesJson(0, count, resp_json);
        }
        return 0;
    } else {
        if (strcmp(cmd, "normal") == 0) {
            handleGetShareFilelist(start, count, resp_json); // 获取共享文件
        } else if (strcmp(cmd, "pvdesc") == 0) {
            handleGetRankingFilelist(start, count, resp_json); ////获取共享文件排行版
        } else {
            encodeSharefilesJson(1, 0, resp_json);
        }
    }
    return 0;
}