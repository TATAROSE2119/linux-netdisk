#include "client_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// 全局变量
ClientState g_client_state = {0};
static char g_error_msg[256] = {0};

// 错误处理函数
void set_error(const char *error_msg) {
    strncpy(g_error_msg, error_msg, sizeof(g_error_msg) - 1);
    g_error_msg[sizeof(g_error_msg) - 1] = '\0';
}

const char* get_last_error(void) {
    return g_error_msg;
}

// 连接到服务器
int connect_to_server(void) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        set_error("创建socket失败");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        set_error("无效的服务器地址");
        close(socket_fd);
        return -1;
    }
    
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        set_error("连接服务器失败");
        close(socket_fd);
        return -1;
    }
    
    g_client_state.socket_fd = socket_fd;
    g_client_state.is_connected = 1;
    return socket_fd;
}

// 断开连接
void disconnect_from_server(int socket_fd) {
    if (socket_fd > 0) {
        close(socket_fd);
    }
    g_client_state.socket_fd = 0;
    g_client_state.is_connected = 0;
    memset(g_client_state.username, 0, sizeof(g_client_state.username));
}

// 用户登录
int login_user(const char *username, const char *password) {
    int socket_fd = connect_to_server();
    if (socket_fd < 0) {
        return -1;
    }
    
    // 发送登录命令
    char cmd = 'L';
    if (write(socket_fd, &cmd, sizeof(cmd)) != sizeof(cmd)) {
        set_error("发送登录命令失败");
        disconnect_from_server(socket_fd);
        return -1;
    }
    
    // 发送用户名
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(socket_fd, &ulen_net, sizeof(ulen_net));
    write(socket_fd, username, ulen);
    
    // 发送密码
    int plen = strlen(password);
    int plen_net = htonl(plen);
    write(socket_fd, &plen_net, sizeof(plen_net));
    write(socket_fd, password, plen);
    
    // 接收登录结果
    char result;
    if (read(socket_fd, &result, sizeof(result)) != sizeof(result)) {
        set_error("接收登录结果失败");
        disconnect_from_server(socket_fd);
        return -1;
    }
    
    if (result == 1) {
        strncpy(g_client_state.username, username, sizeof(g_client_state.username) - 1);
        set_error("登录成功");
        close(socket_fd); // 关闭这个连接，后续操作会重新连接
        return 0;
    } else {
        set_error("用户名或密码错误");
        disconnect_from_server(socket_fd);
        return -1;
    }
}

// 用户注册
int register_user(const char *username, const char *password) {
    int socket_fd = connect_to_server();
    if (socket_fd < 0) {
        return -1;
    }
    
    // 发送注册命令
    char cmd = 'R';
    if (write(socket_fd, &cmd, sizeof(cmd)) != sizeof(cmd)) {
        set_error("发送注册命令失败");
        disconnect_from_server(socket_fd);
        return -1;
    }
    
    // 发送用户名
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(socket_fd, &ulen_net, sizeof(ulen_net));
    write(socket_fd, username, ulen);
    
    // 发送密码
    int plen = strlen(password);
    int plen_net = htonl(plen);
    write(socket_fd, &plen_net, sizeof(plen_net));
    write(socket_fd, password, plen);
    
    // 接收注册结果
    char result;
    if (read(socket_fd, &result, sizeof(result)) != sizeof(result)) {
        set_error("接收注册结果失败");
        disconnect_from_server(socket_fd);
        return -1;
    }
    
    close(socket_fd);
    
    if (result == 1) {
        set_error("注册成功");
        return 0;
    } else {
        set_error("注册失败，用户名可能已存在");
        return -1;
    }
}

// 上传文件
int upload_file(const char *local_path, const char *remote_path, 
                ProgressCallback progress_cb, void *user_data) {
    if (!g_client_state.username[0]) {
        set_error("请先登录");
        return -1;
    }
    
    FILE *file = fopen(local_path, "rb");
    if (!file) {
        set_error("无法打开本地文件");
        return -1;
    }
    
    int socket_fd = connect_to_server();
    if (socket_fd < 0) {
        fclose(file);
        return -1;
    }
    
    // 发送上传命令
    char cmd = 'U';
    write(socket_fd, &cmd, sizeof(cmd));
    
    // 发送用户名
    int ulen = strlen(g_client_state.username);
    int ulen_net = htonl(ulen);
    write(socket_fd, &ulen_net, sizeof(ulen_net));
    write(socket_fd, g_client_state.username, ulen);
    
    // 发送目标目录（空字符串表示根目录）
    int dir_len = 0;
    int dir_len_net = htonl(dir_len);
    write(socket_fd, &dir_len_net, sizeof(dir_len_net));
    
    // 发送文件名
    const char *filename = strrchr(remote_path, '/');
    if (filename) {
        filename++; // 跳过斜杠
    } else {
        filename = remote_path;
    }
    
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);
    write(socket_fd, &name_len_net, sizeof(name_len_net));
    write(socket_fd, filename, name_len);
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // 发送文件内容
    char buffer[4096];
    long bytes_sent = 0;
    size_t n;
    
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (write(socket_fd, buffer, n) != (ssize_t)n) {
            set_error("发送文件数据失败");
            fclose(file);
            close(socket_fd);
            return -1;
        }
        
        bytes_sent += n;
        
        // 调用进度回调
        if (progress_cb && file_size > 0) {
            int percent = (int)((bytes_sent * 100) / file_size);
            progress_cb(percent, user_data);
        }
    }
    
    fclose(file);
    close(socket_fd);
    set_error("文件上传成功");
    return 0;
}

// 下载文件
int download_file(const char *remote_path, const char *local_path,
                  ProgressCallback progress_cb, void *user_data) {
    if (!g_client_state.username[0]) {
        set_error("请先登录");
        return -1;
    }
    
    int socket_fd = connect_to_server();
    if (socket_fd < 0) {
        return -1;
    }
    
    // 发送下载命令
    char cmd = 'D';
    write(socket_fd, &cmd, sizeof(cmd));
    
    // 发送用户名
    int ulen = strlen(g_client_state.username);
    int ulen_net = htonl(ulen);
    write(socket_fd, &ulen_net, sizeof(ulen_net));
    write(socket_fd, g_client_state.username, ulen);
    
    // 发送文件名
    const char *filename = strrchr(remote_path, '/');
    if (filename) {
        filename++; // 跳过斜杠
    } else {
        filename = remote_path;
    }
    
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);
    write(socket_fd, &name_len_net, sizeof(name_len_net));
    write(socket_fd, filename, name_len);
    
    // 接收文件存在标志
    char flag;
    if (read(socket_fd, &flag, sizeof(flag)) != sizeof(flag) || flag == 0) {
        set_error("文件不存在");
        close(socket_fd);
        return -1;
    }
    
    // 接收文件大小
    uint32_t size_high, size_low;
    read(socket_fd, &size_high, sizeof(size_high));
    read(socket_fd, &size_low, sizeof(size_low));
    int64_t file_size = ((int64_t)ntohl(size_high) << 32) | ntohl(size_low);
    
    // 创建本地文件
    FILE *file = fopen(local_path, "wb");
    if (!file) {
        set_error("无法创建本地文件");
        close(socket_fd);
        return -1;
    }
    
    // 接收文件内容
    char buffer[4096];
    int64_t bytes_received = 0;
    ssize_t n;
    
    while (bytes_received < file_size && 
           (n = read(socket_fd, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, 1, n, file);
        bytes_received += n;
        
        // 调用进度回调
        if (progress_cb && file_size > 0) {
            int percent = (int)((bytes_received * 100) / file_size);
            progress_cb(percent, user_data);
        }
    }
    
    fclose(file);
    close(socket_fd);
    set_error("文件下载成功");
    return 0;
}

// 工具函数：格式化文件大小
const char* format_file_size(int64_t size) {
    static char buffer[32];
    
    if (size < 1024) {
        snprintf(buffer, sizeof(buffer), "%ld B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
    
    return buffer;
}

// 工具函数：格式化时间
const char* format_time(time_t time) {
    static char buffer[32];
    struct tm *tm_info = localtime(&time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", tm_info);
    return buffer;
}
