#include "api_md5.h"
#include "dlog.h"
#include <cstdio>

enum Md5State {
    Md5Ok = 0,
    Md5Failed = 1,
    Md5TokenFaild = 4,
    Md5FileExit = 5,
};

// 解析md5信息
static int decodeMd5Json(const std::string &str_json, string &user_name,
                    string &token, string &md5, string &file_name) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LogError("parse reg json failed ");
        return -1;
    }
    // 用户名
    if (root["user"].isNull()) {
        LogError("user null");
        return -1;
    }
    user_name = root["user"].asString();

    // 密码
    if (root["token"].isNull()) {
        LogError("token null");
        return -1;
    }
    token = root["token"].asString();

    // Md5
    if (root["md5"].isNull()) {
        LogError("md5 null");
        return -1;
    }
    md5 = root["md5"].asString();

    // 文件名
    if (root["filename"].isNull()) {
        LogError("filename null");
        return -1;
    }
    file_name = root["filename"].asString();

    return 0;
}

static void encodeMd5Json(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);    
}

static void handleDealMd5(const char *user, const char *md5, const char *filename,
                   string &str_json) {
    // 获取mysql连接
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_master");
    AUTO_REL_DBCONN(db_manager, db_conn);

    // 执行事务，更新文件表文件的引用计数和用户文件表
    bool succeed = false;
    int fail_reason = 0;
    db_conn->StartTransaction();

    // 检查该用户是否拥有该记录，手动加锁解决幻读问题
    sprintf(sql_cmd, "select * from user_file_list where user "
        "= '%s' and md5 = '%s' and file_name = '%s' for update", user, md5, filename);
    LogInfo("执行: {}", sql_cmd);
    ret = CheckwhetherHaveRecord(db_conn, sql_cmd);
    if (ret == 1)
    {
        LogWarn("user: {}->  filename: {}, md5: {}已存在", user, filename, md5);
            encodeMd5Json(Md5FileExit, str_json);
        db_conn->Commit();
        return;
    }

    // 获取引用计数，手动加锁防止其它事务丢失更新
    sprintf(sql_cmd, "select count from file_info where md5 = '%s' for update", md5);
    LogInfo("执行: {}", sql_cmd);
    int file_ref_count = 0;
    ret = GetResultOneCount(db_conn, sql_cmd, file_ref_count);
    // 如果获取成功
    if (ret != -1 && ret != 1)
    {
        // 更新引用计数
        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", file_ref_count + 1, md5);
        LogInfo("执行：{}", sql_cmd);
        // 如果成功更新引用计数
        if (db_conn->ExecuteUpdate(sql_cmd, false))
        {
            // 插入用户文件列表
            // 当前时间戳
            struct timeval tv;
            struct tm *ptm;
            char time_str[128];

            // 使用函数gettimeofday()函数来得到时间。它的精度可以达到微妙
            gettimeofday(&tv, NULL);
            ptm = localtime(&tv.tv_sec); //把从1970-1-1零点零分到当前时间系统所偏移的秒数时间转换为本地时间
            // strftime()
            // 函数根据区域设置格式化本地时间/日期，函数的功能将时间格式化，或者说格式化一个时间字符串
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

            snprintf(sql_cmd, sizeof sql_cmd, "insert into user_file_list(user, md5, create_time, file_name, "
                "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
                user, md5, time_str, filename, 0, 0);
            LogInfo("执行：{}", sql_cmd);
            // 如果成功插入
            if (db_conn->ExecuteCreate(sql_cmd)) {
                succeed = true;
            }
        }
    }
    else {
        fail_reason = 1;
    }

    if (!succeed) {
        db_conn->Rollback();
        if (fail_reason == 1)
        {
            LogInfo("秒传失败");
        }
        else {
            LogError("事务执行失败");
        }
        encodeMd5Json(Md5Failed, str_json);
    }
    else {
        db_conn->Commit();
        LogInfo("秒传成功");
        encodeMd5Json(Md5Ok, str_json);
    }
}

int ApiMd5(string &url, string &post_data, string &resp_json) 
{
    // 解析json
    string user;
    string md5;
    string token;
    string filename;
    int ret = 0;

    if(decodeMd5Json(post_data, user, token, md5, filename) != 0) {
        LogError("decodeMd5Json() err");
        //封装code =  Md5Failed
        encodeMd5Json(Md5Failed, resp_json);
        return 0;
    }
    //校验token
    ret = VerifyToken(user, token); // util_cgi.h
    if (ret == 0) {
        //秒传业务的处理
        // in: user  md5 filename,  out:str_json
        handleDealMd5(user.c_str(), md5.c_str(), filename.c_str(), resp_json);
    } else {
        // 校验失败
        encodeMd5Json(Md5TokenFaild, resp_json);
    }
    return 0;
}
