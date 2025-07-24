#ifndef CLIENT_NETWORK_H
#define CLIENT_NETWORK_H

#include <stdint.h>
#include <time.h>

// 服务器配置
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000

// 文件信息结构体
typedef struct {
    char name[256];
    int64_t size;
    time_t mtime;
    int is_directory;
} FileInfo;

// 客户端状态结构体
typedef struct {
    int socket_fd;
    char username[64];
    int is_connected;
} ClientState;

// 回调函数类型定义
typedef void (*ProgressCallback)(int percent, void *user_data);
typedef void (*StatusCallback)(const char *message, void *user_data);

// 基础连接函数
int connect_to_server(void);
void disconnect_from_server(int socket_fd);

// 用户认证函数
int login_user(const char *username, const char *password);
int register_user(const char *username, const char *password);

// 文件操作函数
int upload_file(const char *local_path, const char *remote_path, 
                ProgressCallback progress_cb, void *user_data);
int download_file(const char *remote_path, const char *local_path,
                  ProgressCallback progress_cb, void *user_data);
int delete_file(const char *remote_path);

// 目录操作函数
int create_directory(const char *remote_path);
int list_files(const char *remote_path, FileInfo **files, int *count);
int change_directory(const char *remote_path);

// 工具函数
void free_file_list(FileInfo *files, int count);
const char* format_file_size(int64_t size);
const char* format_time(time_t time);

// 错误处理
const char* get_last_error(void);
void set_error(const char *error_msg);

// 全局客户端状态
extern ClientState g_client_state;

#endif // CLIENT_NETWORK_H
