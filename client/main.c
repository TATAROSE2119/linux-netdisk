#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <ctype.h> // For isspace()
#include <sys/time.h>  // 为了使用 gettimeofday 计算速度
#include <time.h> // For localtime, strftime
#include <endian.h> // For be64toh
#include <libgen.h> // 为了使用basename函数

int connect_to_server(void);

#define PORT 9000
#define server_IP "192.168.50.153"

char g_username[64]={0}; // Global variable to store username

// 添加当前工作目录
char g_current_dir[1024] = "/";

// 存储服务器文件和目录列表的全局变量
#define MAX_FILES 1000
#define MAX_FILENAME_LEN 256
typedef struct {
    char name[MAX_FILENAME_LEN];
    int is_dir;  // 1表示目录，0表示文件
} FileEntry;

FileEntry server_entries[MAX_FILES];
int server_entries_count = 0;

// 清理服务器文件列表
void clear_server_entries() {
    server_entries_count = 0;
}

// 规范化路径（处理 . 和 ..）
void normalize_path(char *path) {
    char *parts[256];  // 存储路径的各个部分
    int count = 0;
    
    // 分割路径
    char *token = strtok(path, "/");
    while (token != NULL && count < 256) {
        if (strcmp(token, ".") == 0) {
            // 忽略 "."
        } else if (strcmp(token, "..") == 0) {
            // 返回上一级目录
            if (count > 0) count--;
        } else {
            parts[count++] = token;
        }
        token = strtok(NULL, "/");
    }
    
    // 重建路径
    path[0] = '/';
    path[1] = '\0';
    for (int i = 0; i < count; i++) {
        strcat(path, parts[i]);
        if (i < count - 1) strcat(path, "/");
    }
    
    // 如果路径为空，设为根目录
    if (strlen(path) == 0) strcpy(path, "/");
}

// 获取服务器文件列表
void fetch_server_entries(const char *current_dir) {
    if (strlen(g_username) == 0) return;
    
    clear_server_entries();
    
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    // 发送列表命令
    char cmd = 'L';  // 新命令：获取目录内容
    write(sockfd, &cmd, sizeof(cmd));
    
    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 发送当前目录
    int dir_len = strlen(current_dir);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    write(sockfd, current_dir, dir_len);

    // 读取服务器响应
    char res;
    read(sockfd, &res, sizeof(res));
    if (res == 0) {
        close(sockfd);
        return;
    }

    // 读取条目数量
    int count_net;
    read(sockfd, &count_net, sizeof(count_net));
    int count = ntohl(count_net);

    // 读取每个条目
    for(int i = 0; i < count && i < MAX_FILES; i++) {
        // 读取类型（文件/目录）
        char type;
        read(sockfd, &type, sizeof(type));
        
        // 读取名称
        int name_len_net;
        read(sockfd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        
        read(sockfd, server_entries[i].name, name_len);
        server_entries[i].name[name_len] = '\0';
        server_entries[i].is_dir = (type == 2);  // 2表示目录
        
        server_entries_count++;
    }

    close(sockfd);
}

// 服务器文件名补全生成器
char* server_file_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (list_index < server_entries_count) {
        name = server_entries[list_index++].name;
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

// 目录补全生成器
char* dir_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (list_index < server_entries_count) {
        name = server_entries[list_index++].name;
        if (strncmp(name, text, len) == 0) {
            char* completion = malloc(strlen(name) + 2);  // +2 for potential '/' and null terminator
            strcpy(completion, name);
            if (server_entries[list_index-1].is_dir) {
                strcat(completion, "/");
            }
            return completion;
        }
    }
    return NULL;
}

// 命令列表
char* commands[] = {
    "register", "login", "upload", "download", "list", "delete", "exit", "help",
    "mkdir", "touch", "cd", "pwd", "tree", NULL
};

// 补全回调函数
char* command_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {// 如果状态为0
        list_index = 0;// 重置索引
        len = strlen(text);// 获取输入长度
    }

    while ((name = commands[list_index++])) {// 遍历命令列表
        if (strncmp(name, text, len) == 0) {// 比较命令和输入
            return strdup(name);// 返回补全结果  strdup 函数用于复制字符串
        }
    }
    return NULL;
}

// 补全函数
char** command_completion(const char* text, int start, int end) {
    // 获取当前行的第一个词（命令）
    char* cmd_start = rl_line_buffer;
    while (*cmd_start && isspace(*cmd_start)) cmd_start++;
    
    char cmd[32] = {0};
    int i = 0;
    while (*cmd_start && !isspace(*cmd_start) && i < sizeof(cmd)-1) {
        cmd[i++] = *cmd_start++;
    }

    // 如果是行首，进行命令补全
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    
    // 如果命令是 cd、download 或 ls，进行目录/文件补全
    if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "download") == 0 || 
        strcmp(cmd, "ls") == 0 || strcmp(cmd, "tree") == 0) {
        // 当用户输入这些命令后按 Tab，获取最新的服务器文件列表
        if (server_entries_count == 0) {
            fetch_server_entries(g_current_dir);
        }
        return rl_completion_matches(text, dir_generator);
    }
    
    // 如果命令是 upload，使用本地文件补全
    if (strcmp(cmd, "upload") == 0) {
        rl_attempted_completion_over = 0;  // 允许默认文件名补全
        return NULL;
    }
    
    // 其他情况不进行补全
    rl_attempted_completion_over = 1;
    return NULL;
}

int register_user(const char *username, const char *password) {
    int sockfd;
    struct sockaddr_in server_addr;// Server address structure
    sockfd=socket(AF_INET, SOCK_STREAM, 0);// Create a socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT); // Set port number
    inet_pton(AF_INET, server_IP, &server_addr.sin_addr); // Convert IP

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed ❌");
        close(sockfd);
        return -1;
    }
    char cmd ='R'; // Command for registration
    write(sockfd, &cmd, sizeof(cmd)); // Send command to server

    //1.发送用户名长度
    int ulen = strlen(username);
    int ulen_net = htonl(ulen); // Convert to network byte order
    write(sockfd, &ulen_net, sizeof(ulen_net)); 
    write(sockfd, username, ulen); // Send username

    //2.发送密码长度
    int plen = strlen(password);
    int plen_net = htonl(plen); // Convert to network byte order
    write(sockfd, &plen_net, sizeof(plen_net));
    write(sockfd, password, plen); // Send password

    //接收服务器返回
    char res;
    read(sockfd, &res, sizeof(res)); // Read registration result from server
    close(sockfd); // Close the connection

    if(res == 1) {
        printf("Registration successful ✅\n");
        return 0; // Registration successful
    }else {
        printf("User may already exist. Registration failed ❌\n");
        return -1; // Registration failed
    }
}

int login_user(const char *username, const char *password) {
    int sockfd;
    struct sockaddr_in server_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_IP, &server_addr.sin_addr);

    connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    char cmd = 'L';
    write(sockfd, &cmd, 1);

    // 发送用户名长度和用户名
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, 4);
    write(sockfd, username, ulen);

    // 发送密码长度和密码
    int plen = strlen(password);
    int plen_net = htonl(plen);
    write(sockfd, &plen_net, 4);
    write(sockfd, password, plen);

    printf("[DEBUG] Sending username: '%s', password: '%s'\n", username, password);


    // 接收服务器返回
    char res;
    read(sockfd, &res, 1);
    close(sockfd);

    if(res == 1) {
        printf("Login successful!\n");
        strcpy(g_username, username); // 保存用户名
        return 1;
    } else {
        printf("Login failed! Wrong username or password.\n");
        return 0;
    }
}

// 进度条显示函数
void show_progress(const char* filename, const char* type, long transferred, long total, double speed) {
    const int bar_width = 40;
    float progress = (float)transferred / total;
    int pos = bar_width * progress;
    
    // 构造进度条字符串
    printf("\r"); // 回到行首
    printf("%s %s: [", type, filename);
    
    // 显示进度条
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    
    // 显示百分比和速度
    if (speed < 1024) {
        printf("] %3.1f%% (%.2f B/s)  ", progress * 100, speed);
    } else if (speed < 1024*1024) {
        printf("] %3.1f%% (%.2f KB/s)  ", progress * 100, speed/1024);
    } else {
        printf("] %3.1f%% (%.2f MB/s)  ", progress * 100, speed/(1024*1024));
    }
    
    fflush(stdout); // 立即刷新输出
}

void upload_file(const char *filepath, const char *target_dir) {
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    // 从完整路径中提取文件名 (basename)
    char *filepath_copy = strdup(filepath);
    const char *filename = basename(filepath_copy);

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        printf("❌ 错误: 无法打开本地文件 '%s'\n", filepath);
        free(filepath_copy);
        close(sockfd);
        return;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    long uploaded_size = 0;

    // 发送命令和文件信息
    char cmd = 'U';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 发送目标目录
    const char *dir_to_send = target_dir ? target_dir : "";
    int dir_len = strlen(dir_to_send);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    if (dir_len > 0) {
        write(sockfd, dir_to_send, dir_len);
    }

    // 发送文件名
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);
    write(sockfd, &name_len_net, sizeof(name_len_net));
    write(sockfd, filename, name_len);

    // 用于计算速度
    struct timeval start, now;
    gettimeofday(&start, NULL);
    
    // 上传文件内容
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (write(sockfd, buffer, n) < 0) {
            printf("\n"); // 错误时换行
            perror("Socket write error");
            break;
        }
        uploaded_size += n;
        
        // 计算速度
        gettimeofday(&now, NULL);
        double time_spent = (now.tv_sec - start.tv_sec) + 
                          (now.tv_usec - start.tv_usec) / 1000000.0;
        double speed = uploaded_size / time_spent;
        
        show_progress(filename, "上传", uploaded_size, file_size, speed);
    }

    printf("\n"); // 只在完成时换行
    fclose(fp);
    free(filepath_copy);
    close(sockfd);
    printf("✅ 文件 '%s' 上传完成\n", filename);
}

void download_file(const char *filename) {
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    char cmd = 'D';
    write(sockfd, &cmd, sizeof(cmd));
    
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);
    write(sockfd, &name_len_net, sizeof(name_len_net));
    write(sockfd, filename, name_len);

    char flag;
    read(sockfd, &flag, sizeof(flag));
    if(flag == 0) {
        printf("❌ 文件 '%s' 不存在\n", filename);
        close(sockfd);
        return;
    }

    // 读取文件大小（64位）
    uint32_t size_high_net, size_low_net;
    read(sockfd, &size_high_net, sizeof(size_high_net));
    read(sockfd, &size_low_net, sizeof(size_low_net));
    uint32_t size_high = ntohl(size_high_net);
    uint32_t size_low = ntohl(size_low_net);
    int64_t file_size = ((int64_t)size_high << 32) | size_low;

    FILE *fp = fopen(filename, "wb");
    if(!fp) {
        perror("File creation error");
        close(sockfd);
        return;
    }

    struct timeval start, now;
    gettimeofday(&start, NULL);
    
    char buffer[4096];
    ssize_t n;
    int64_t downloaded_size = 0;

    while((n = read(sockfd, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, sizeof(char), n, fp) != n) {
            printf("\n");
            perror("File write error");
            break;
        }
        downloaded_size += n;
        
        gettimeofday(&now, NULL);
        double time_spent = (now.tv_sec - start.tv_sec) + 
                          (now.tv_usec - start.tv_usec) / 1000000.0;
        double speed = downloaded_size / time_spent;
        
        show_progress(filename, "下载", downloaded_size, file_size, speed);
    }

    printf("\n");
    fclose(fp);
    close(sockfd);
    printf("✅ 文件 '%s' 下载完成\n", filename);
}

void format_size(int64_t size, char *buf) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size_d = size;
    while (size_d >= 1024 && unit < 4) {
        size_d /= 1024;
        unit++;
    }
    sprintf(buf, "%.1f %s", size_d, units[unit]);
}

void send_list_files(int sockfd, const char* username) {
    char cmd = 'L';
    write(sockfd, &cmd, sizeof(cmd));
    
    // 发送用户名
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, username, ulen);

    // 发送当前目录
    int dir_len = strlen(g_current_dir);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    write(sockfd, g_current_dir, dir_len);

    char res;
    read(sockfd, &res, sizeof(res));
    if(res == 0) {
        printf("[文件列表] 目录不存在或读取失败\n");
        return;
    }

    int file_count_net;
    read(sockfd, &file_count_net, sizeof(file_count_net));
    int file_count = ntohl(file_count_net);
    
    printf("\n当前目录 (%s) 共 %d 个项目:\n", g_current_dir, file_count);
    printf("%-40s %-15s %-20s\n", "名称", "大小", "修改时间");
    printf("--------------------------------------------------------------------------------\n");

    for(int i = 0; i < file_count; i++) {
        // 读取类型（文件/目录）
        char type;
        read(sockfd, &type, sizeof(type));

        // 读取名称
        int name_len_net;
        read(sockfd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        char name[256];
        read(sockfd, name, name_len);
        name[name_len] = '\0';

        // 读取大小
        int64_t size_net;
        read(sockfd, &size_net, sizeof(size_net));
        int64_t size = be64toh(size_net);

        // 读取修改时间
        int64_t mtime_net;
        read(sockfd, &mtime_net, sizeof(mtime_net));
        time_t mtime = be64toh(mtime_net);

        // 格式化大小
        char size_str[32];
        if (type == 2) { // 目录
            strcpy(size_str, "<DIR>");
        } else {
            format_size(size, size_str);
        }

        // 格式化时间
        char time_str[64];
        struct tm *tm_info = localtime(&mtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        // 打印项目信息（目录添加/后缀）
        if (type == 2) {
            printf("%-40s %-15s %-20s\n", strcat(name, "/"), size_str, time_str);
        } else {
            printf("%-40s %-15s %-20s\n", name, size_str, time_str);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");
}

void send_delete_file(int sockfd, const char* username, const char* filename) {
    char cmd = 'X';
    write(sockfd, &cmd, sizeof(cmd));
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, username, ulen);
    int fname_len = strlen(filename);
    int fname_len_net = htonl(fname_len);
    write(sockfd, &fname_len_net, sizeof(fname_len_net));
    write(sockfd, filename, fname_len);
    char res;
    read(sockfd, &res, sizeof(res));
    if(res == 1) printf("[删除] 文件 '%s' 删除成功！\n", filename);
    else printf("[删除] 文件 '%s' 删除失败！\n", filename);
}

void send_mkdir(const char* path) {
    if (strlen(g_username) == 0) {
        printf("请先登录！\n");
        return;
    }

    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    char cmd = 'M';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 构建完整路径
    char full_path[1024];
    if (path[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", path + 1);
    } else {
        if (strcmp(g_current_dir, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", path);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", g_current_dir + 1, path);
        }
    }

    // 发送路径
    int path_len = strlen(full_path);
    int path_len_net = htonl(path_len);
    write(sockfd, &path_len_net, sizeof(path_len_net));
    write(sockfd, full_path, path_len);

    char res;
    read(sockfd, &res, sizeof(res));
    if (res) {
        printf("目录创建成功 ✅\n");
    } else {
        printf("目录创建失败 ❌\n");
    }
    close(sockfd);
}

void send_touch(const char* path) {
    if (strlen(g_username) == 0) {
        printf("请先登录！\n");
        return;
    }

    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    char cmd = 'T';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 构建完整路径
    char full_path[1024];
    if (path[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", path + 1);
    } else {
        if (strcmp(g_current_dir, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", path);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", g_current_dir + 1, path);
        }
    }

    // 发送路径
    int path_len = strlen(full_path);
    int path_len_net = htonl(path_len);
    write(sockfd, &path_len_net, sizeof(path_len_net));
    write(sockfd, full_path, path_len);

    char res;
    read(sockfd, &res, sizeof(res));
    if (res) {
        printf("文件创建成功 ✅\n");
    } else {
        printf("文件创建失败 ❌\n");
    }
    close(sockfd);
}

void send_tree() {
    if (strlen(g_username) == 0) {
        printf("请先登录！\n");
        return;
    }

    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    char cmd = 'E';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    char res;
    read(sockfd, &res, sizeof(res));
    if (!res) {
        printf("获取目录树失败 ❌\n");
        close(sockfd);
        return;
    }

    printf("\n%s 的目录树:\n", g_username);
    printf(".\n");

    while (1) {
        char type;
        if (read(sockfd, &type, sizeof(type)) <= 0 || type == 0) break;

        int name_len_net;
        read(sockfd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        
        char name[256];
        read(sockfd, name, name_len);
        name[name_len] = '\0';

        int depth_net;
        read(sockfd, &depth_net, sizeof(depth_net));
        int depth = ntohl(depth_net);

        // 打印缩进
        for (int i = 0; i < depth; i++) {
            printf("│   ");
        }
        
        // 打印项目
        if (type == 1) { // 文件
            printf("├── %s\n", name);
        } else { // 目录
            printf("├── %s/\n", name);
        }
    }

    close(sockfd);
}

int connect_to_server() {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed ❌");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported ❌");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed ❌");
        close(sockfd);
        return -1;
    }
    return sockfd;
}


int main() {
    rl_attempted_completion_function = command_completion;
    char prompt[1024];
    char* line;

    printf("欢迎使用 C 语言网盘客户端！输入 help 查看命令，按 Tab 自动补全。\n");

    while (1) {
        // 更新提示符以显示当前用户和目录
        if (strlen(g_username) > 0) {
            snprintf(prompt, sizeof(prompt), "Netdisk[%s]%s> ", g_username, g_current_dir);
        } else {
            snprintf(prompt, sizeof(prompt), "Netdisk> ");
        }

        line = readline(prompt);
        if (!line) break;

        if (strlen(line) > 0) add_history(line);

        char* line_copy = strdup(line);
        char* cmd = strtok(line_copy, " \t\n");

        if (!cmd) {
            free(line);
            free(line_copy);
            continue;
        }

        if (strcmp(cmd, "help") == 0) {
            printf("命令列表:\n");
            printf("  register <user> <pass>  - 注册新用户\n");
            printf("  login <user> <pass>     - 登录账户\n");
            printf("  upload <file1> [file2...] <dir> - 上传多个文件到指定目录\n");
            printf("  download <file>         - 下载文件\n");
            printf("  list                    - 查看文件列表\n");
            printf("  delete <file>           - 删除文件\n");
            printf("  mkdir <dir>             - 创建目录\n");
            printf("  touch <file>            - 创建空文件\n");
            printf("  cd <dir>                - 切换目录\n");
            printf("  pwd                     - 显示当前目录\n");
            printf("  tree                    - 显示目录树\n");
            printf("  exit                    - 退出程序\n");
        } else if (strcmp(cmd, "register") == 0) {
            char* user = strtok(NULL, " \t\n");
            char* pass = strtok(NULL, " \t\n");
            if (user && pass) register_user(user, pass);
            else printf("Usage: register <user> <pass>\n");
        } else if (strcmp(cmd, "login") == 0) {
            char* user = strtok(NULL, " \t\n");
            char* pass = strtok(NULL, " \t\n");
            if (user && pass) login_user(user, pass);
            else printf("Usage: login <user> <pass>\n");
        } else if (strcmp(cmd, "upload") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                // 收集所有参数
                char *args[100];  // 最多支持100个参数
                int arg_count = 0;
                char *arg;
                
                while ((arg = strtok(NULL, " \t\n")) != NULL && arg_count < 100) {
                    args[arg_count++] = arg;
                }

                if (arg_count < 2) {
                    printf("Usage: upload <file1> [file2...] <dir>\n");
                    printf("Example: upload file1.txt file2.txt mydir\n");
                } else {
                    // 最后一个参数是目标目录
                    char *target_dir = args[arg_count - 1];
                    
                    // 上传除最后一个参数外的所有文件
                    for (int i = 0; i < arg_count - 1; i++) {
                        printf("\n正在上传第 %d/%d 个文件: %s\n", i + 1, arg_count - 1, args[i]);
                        upload_file(args[i], target_dir);
                    }
                }
            }
        } else if (strcmp(cmd, "download") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char* filename = strtok(NULL, " \t\n");
                if (!filename) printf("Usage: download <file1> [file2..]\n");
                while (filename) {
                    download_file(filename);
                    filename = strtok(NULL, " \t\n");
                }
            }
        } else if (strcmp(cmd, "list") == 0) {
            if (strlen(g_username) == 0) printf("请先登录！\n");
            else {
                int sockfd = connect_to_server();
                if (sockfd >= 0) {
                    send_list_files(sockfd, g_username);
                    close(sockfd);
                }
            }
        } else if (strcmp(cmd, "delete") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char* filename = strtok(NULL, " \t\n");
                if (!filename) printf("Usage: delete <file1> [file2..]\n");
                while (filename) {
                    int sockfd = connect_to_server();
                    if (sockfd >= 0) {
                        send_delete_file(sockfd, g_username, filename);
                        close(sockfd);
                    }
                    filename = strtok(NULL, " \t\n");
                }
            }
        } else if (strcmp(cmd, "mkdir") == 0) {
            char* path = strtok(NULL, " \t\n");
            if (path) send_mkdir(path);
            else printf("Usage: mkdir <dir>\n");
        } else if (strcmp(cmd, "touch") == 0) {
            char* path = strtok(NULL, " \t\n");
            if (path) send_touch(path);
            else printf("Usage: touch <file>\n");
        } else if (strcmp(cmd, "cd") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char* path = strtok(NULL, " \t\n");
                if (!path) {
                    strcpy(g_current_dir, "/");
                } else {
                    char new_path[1024];
                    if (path[0] == '/') {
                        strncpy(new_path, path, sizeof(new_path)-1);
                    } else {
                        if (strcmp(g_current_dir, "/") == 0) {
                            snprintf(new_path, sizeof(new_path), "/%s", path);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s", g_current_dir, path);
                        }
                    }
                    normalize_path(new_path);
                    
                    // 验证目录是否存在
                    int sockfd = connect_to_server();
                    if (sockfd >= 0) {
                        char cmd = 'V';  // 新命令：验证目录
                        write(sockfd, &cmd, sizeof(cmd));
                        
                        // 发送用户名
                        int ulen = strlen(g_username);
                        int ulen_net = htonl(ulen);
                        write(sockfd, &ulen_net, sizeof(ulen_net));
                        write(sockfd, g_username, ulen);
                        
                        // 发送路径
                        int path_len = strlen(new_path);
                        int path_len_net = htonl(path_len);
                        write(sockfd, &path_len_net, sizeof(path_len_net));
                        write(sockfd, new_path, path_len);
                        
                        char res;
                        read(sockfd, &res, sizeof(res));
                        close(sockfd);
                        
                        if (res) {
                            strcpy(g_current_dir, new_path);
                            // 更新补全缓存
                            fetch_server_entries(g_current_dir);
                        } else {
                            printf("目录不存在: %s\n", new_path);
                        }
                    }
                }
            }
        } else if (strcmp(cmd, "pwd") == 0) {
            printf("%s\n", g_current_dir);
        } else if (strcmp(cmd, "tree") == 0) {
            send_tree();
        } else if (strcmp(cmd, "exit") == 0) {
            free(line);
            free(line_copy);
            break;
        } else {
            printf("未知命令: '%s'. 输入 'help' 查看帮助。\n", cmd);
        }

        free(line);
        free(line_copy);
    }
    clear_server_entries();
    printf("Bye!\n");
    return 0;
}