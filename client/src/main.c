/*
 * 网盘命令行客户端。
 *
 * 本文件包含交互式命令循环、Readline 自动补全、登录状态、远程文件操作和
 * 上传下载进度显示。客户端遵循“一次连接只发送一条命令”的协议：多数业务
 * 函数会临时连接服务器、完成一次请求并关闭 socket。
 */
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
#include <errno.h>
#include <sys/time.h>  // 为了使用 gettimeofday 计算速度
#include <time.h> // For localtime, strftime
#include <libgen.h> // 为了使用basename函数
#include <stdint.h> // For uint32_t, int64_t

#include "net_io.h"

/*
 * 将服务器传回的 64 位大端整数转换为主机字节序。主流平台使用系统实现，
 * 其他平台使用两个 32 位 ntohl 组合成兼容版本。
 */
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
#include <endian.h>
#elif defined(_WIN32)
#include <winsock2.h>
#define be64toh(x) _byteswap_uint64(x)
#else
// 通用实现
static inline uint64_t be64toh(uint64_t big_endian_64bits) {
    union {
        uint64_t ll;
        uint32_t l[2];
    } w, r;
    w.ll = big_endian_64bits;
    r.l[0] = ntohl(w.l[1]);
    r.l[1] = ntohl(w.l[0]);
    return r.ll;
}
#endif

/* 创建并连接一个新的服务器 socket；定义位于文件后部。 */
int connect_to_server(void);

/* 当前客户端连接目标。修改地址或端口时应与服务器配置保持一致。 */
#define PORT 9000
#define server_IP "127.0.0.1"  // 本地部署，使用localhost

/* 当前登录用户名；空字符串表示尚未登录。 */
char g_username[64]={0};

/* 客户端维护的远程逻辑工作目录，始终以 '/' 开头，不是本机目录。 */
char g_current_dir[1024] = "/";

/* Readline 补全缓存的最大条目数及单个名称容量。 */
#define MAX_FILES 1000
#define MAX_FILENAME_LEN 256

/* 从服务器目录列表缓存下来的最小条目信息。 */
typedef struct {
    char name[MAX_FILENAME_LEN];
    int is_dir;  // 1表示目录，0表示文件
} FileEntry;

/*
 * 当前目录的补全缓存。客户端为单线程交互程序，因此无需加锁；切换目录或
 * 修改远程文件后应刷新该缓存。
 */
FileEntry server_entries[MAX_FILES];
int server_entries_count = 0;

/* 将补全缓存标记为空；静态数组内容无需逐项释放。 */
void clear_server_entries() {
    server_entries_count = 0;
}

/*
 * 就地规范化客户端逻辑路径：去掉 "."，按栈语义处理 ".."，并保证结果
 * 以 '/' 开头。parts 中的每个分量由本函数分配并在重建路径后释放。
 */
void normalize_path(char *path) {
    char temp_path[1024];
    strcpy(temp_path, path);  // 创建副本避免strtok修改原字符串

    char *parts[256];  // 存储路径的各个部分
    int count = 0;

    // 分割路径
    char *token = strtok(temp_path, "/");
    while (token != NULL && count < 256) {
        if (strcmp(token, ".") == 0) {
            // 忽略 "."
        } else if (strcmp(token, "..") == 0) {
            // 返回上一级目录
            if (count > 0) count--;
        } else {
            // 复制token到新的内存位置
            parts[count] = malloc(strlen(token) + 1);
            strcpy(parts[count], token);
            count++;
        }
        token = strtok(NULL, "/");
    }

    // 重建路径
    strcpy(path, "/");
    for (int i = 0; i < count; i++) {
        if (strlen(path) > 1) {
            strcat(path, "/");
        }
        strcat(path, parts[i]);
        free(parts[i]);  // 释放内存
    }

    // 确保路径以"/"开头
    if (path[0] != '/') {
        char temp[1024];
        strcpy(temp, path);
        strcpy(path, "/");
        strcat(path, temp);
    }
}

/*
 * 发送 S 命令获取 current_dir 的一层目录内容，用结果重建自动补全缓存。
 * 协议字段依次为命令码、用户名、目录；每个响应条目包含类型和名称。
 * 网络失败时保留一个空缓存，并关闭本函数创建的 socket。
 */
void fetch_server_entries(const char *current_dir) {
    if (strlen(g_username) == 0) return;
    
    clear_server_entries();
    
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    // 发送列表命令
    char cmd = 'S';  // 使用正确的命令：获取目录内容
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
        // 读取类型（文件/目录）- 服务器发送4字节
        uint32_t type_net;
        if (read(sockfd, &type_net, sizeof(type_net)) <= 0) break;
        uint32_t type = ntohl(type_net);

        // 读取名称
        int name_len_net;
        if (read(sockfd, &name_len_net, sizeof(name_len_net)) <= 0) break;
        int name_len = ntohl(name_len_net);

        if (name_len <= 0 || name_len >= 256) break;
        if (read(sockfd, server_entries[i].name, name_len) <= 0) break;
        server_entries[i].name[name_len] = '\0';
        server_entries[i].is_dir = (type == 2);  // 2表示目录
        
        server_entries_count++;
    }

    close(sockfd);
}

/*
 * Readline 文件补全生成器。state 为 0 时重置遍历位置，后续调用逐个返回
 * 匹配的普通文件名。返回的 strdup 内存交给 Readline 释放。
 */
char* server_file_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (list_index < server_entries_count) {
        name = server_entries[list_index++].name;
        // 只返回文件，不返回目录
        if (!server_entries[list_index-1].is_dir && strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

/*
 * Readline 全条目补全生成器；目录名附加 '/'，让用户可以继续补全下一级。
 */
char* server_all_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (list_index < server_entries_count) {
        name = server_entries[list_index++].name;
        if (strncmp(name, text, len) == 0) {
            char* completion = malloc(strlen(name) + 2);
            strcpy(completion, name);
            // 如果是目录，添加 / 后缀
            if (server_entries[list_index-1].is_dir) {
                strcat(completion, "/");
            }
            return completion;
        }
    }
    return NULL;
}

/*
 * Readline 目录补全生成器。除服务器返回的目录外，还根据当前位置提供
 * "./" 和 "../" 两个逻辑目录候选。
 */
char* dir_generator(const char* text, int state) {
    static int list_index, len, special_state;
    char* name;

    if (!state) {
        list_index = 0;
        special_state = 0;
        len = strlen(text);
    }

    // 首先处理特殊目录
    if (special_state == 0) {
        special_state = 1;
        if ((len == 0 || strncmp("..", text, len) == 0) && strcmp(g_current_dir, "/") != 0) {
            return strdup("../");
        }
    }
    if (special_state == 1) {
        special_state = 2;
        if (len == 0 || strncmp(".", text, len) == 0) {
            return strdup("./");
        }
    }

    // 然后处理服务器目录
    while (list_index < server_entries_count) {
        name = server_entries[list_index++].name;
        // 只返回目录
        if (server_entries[list_index-1].is_dir && strncmp(name, text, len) == 0) {
            char* completion = malloc(strlen(name) + 2);
            strcpy(completion, name);
            strcat(completion, "/");
            return completion;
        }
    }

    return NULL;
}

/* NULL 结尾的顶层命令表，供 command_generator 顺序遍历。 */
char* commands[] = {
    "register", "login", "upload", "download", "list", "delete", "exit", "help",
    "mkdir", "touch", "cd", "pwd", "tree", NULL
};

/* 顶层命令名生成器；遵循 Readline generator 的 state 调用约定。 */
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

/* 统计一行中由空白字符分隔的参数数量，不修改输入字符串。 */
int count_args(const char* line) {
    int count = 0;
    int in_word = 0;

    while (*line) {
        if (!isspace(*line)) {
            if (!in_word) {
                count++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
        line++;
    }
    return count;
}

/*
 * Readline 的统一补全入口。
 * 行首补全命令；cd 只补全目录；download 只补全文件；delete 同时补全两者；
 * upload 根据参数位置在本地文件补全和远程目录补全之间切换。
 */
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

    // 确保有最新的服务器文件列表
    if (server_entries_count == 0) {
        fetch_server_entries(g_current_dir);
    }

    // 根据不同命令提供不同的补全
    if (strcmp(cmd, "cd") == 0) {
        // cd 命令只补全目录
        return rl_completion_matches(text, dir_generator);
    }
    else if (strcmp(cmd, "download") == 0) {
        // download 命令只补全文件
        return rl_completion_matches(text, server_file_generator);
    }
    else if (strcmp(cmd, "delete") == 0) {
        // delete 命令补全文件和目录
        return rl_completion_matches(text, server_all_generator);
    }
    else if (strcmp(cmd, "upload") == 0) {
        // upload 命令：upload <本地文件1> [本地文件2...] <服务器目录>
        // 判断是否是最后一个参数（目标目录）
        char* line_copy = strdup(rl_line_buffer);
        char* token = strtok(line_copy, " \t");
        int current_arg = 0;
        int is_last_arg = 1;

        // 跳过命令名
        token = strtok(NULL, " \t");
        while (token != NULL) {
            current_arg++;
            char* next_token = strtok(NULL, " \t");
            if (next_token == NULL && strstr(rl_line_buffer + start, text) != NULL) {
                // 这是最后一个参数
                is_last_arg = 1;
                break;
            } else if (next_token != NULL) {
                is_last_arg = 0;
            }
            token = next_token;
        }
        free(line_copy);

        if (is_last_arg && current_arg > 0) {
            // 最后一个参数，补全服务器目录
            return rl_completion_matches(text, dir_generator);
        } else {
            // 前面的参数，使用本地文件补全
            rl_attempted_completion_over = 0;
            return NULL;
        }
    }
    else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0 ||
             strcmp(cmd, "tree") == 0 || strcmp(cmd, "pwd") == 0) {
        // 这些命令不需要参数补全
        rl_attempted_completion_over = 1;
        return NULL;
    }
    else if (strcmp(cmd, "mkdir") == 0 || strcmp(cmd, "touch") == 0) {
        // mkdir 和 touch 不需要补全（创建新的文件/目录）
        rl_attempted_completion_over = 1;
        return NULL;
    }
    else if (strcmp(cmd, "register") == 0 || strcmp(cmd, "login") == 0) {
        // 用户名密码不需要补全
        rl_attempted_completion_over = 1;
        return NULL;
    }

    // 其他情况不进行补全
    rl_attempted_completion_over = 1;
    return NULL;
}

/*
 * 发送 R 注册命令。请求依次携带用户名和明文密码，服务器负责哈希及落库；
 * 返回 1 表示注册并创建用户目录成功。函数负责关闭自己创建的 socket。
 */
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

/*
 * 发送 L 登录命令。验证成功后更新全局用户名并刷新根目录补全缓存；
 * 验证失败不会改变当前会话状态。
 */
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
/*
 * 在同一终端行显示传输进度、百分比和速率。transferred/total 使用字节，
 * speed 使用字节每秒；调用结束后由上传/下载函数打印换行。
 */
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


/*
 * 按“4 字节网络序长度 + 字符串内容”的格式发送一个上传协议字段。
 * 使用 send_all 保证短写时继续发送，成功返回 0，失败返回 -1。
 */
static int send_upload_field(int socket_fd, const char *value) {
    size_t length = strlen(value);
    uint32_t network_length;

    if (length > UINT32_MAX) {
        errno = EMSGSIZE;
        return -1;
    }

    network_length = htonl((uint32_t)length);
    if (send_all(socket_fd, &network_length, sizeof(network_length)) != 0) {
        return -1;
    }
    return send_all(socket_fd, value, length);
}

/*
 * 上传单个本地文件到指定远程目录。
 * U 命令字段顺序：用户名、目标目录、文件名、64 位文件大小（高低各 32 位）、
 * 文件内容。只有收到服务器成功响应后才报告完成，所有退出路径都会关闭文件
 * 和 socket。
 */
void upload_file(const char *filepath, const char *target_dir) {
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    // 从完整路径中提取文件名 (basename)
    char *filepath_copy = strdup(filepath);
    if (filepath_copy == NULL) {
        perror("无法准备上传路径");
        close(sockfd);
        return;
    }
    const char *filename = basename(filepath_copy);

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        printf("❌ 错误: 无法打开本地文件 '%s'\n", filepath);
        free(filepath_copy);
        close(sockfd);
        return;
    }

    // 获取文件大小
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("无法读取上传文件大小");
        fclose(fp);
        free(filepath_copy);
        close(sockfd);
        return;
    }
    long file_size = ftell(fp);
    if (file_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        perror("无法读取上传文件大小");
        fclose(fp);
        free(filepath_copy);
        close(sockfd);
        return;
    }
    uint64_t file_size_64 = (uint64_t)file_size;
    uint64_t uploaded_size = 0;
    int progress_started = 0;

    // 发送命令和文件信息
    char cmd = 'U';
    const char *dir_to_send = target_dir ? target_dir : "";
    uint32_t size_high_net = htonl((uint32_t)(file_size_64 >> 32));
    uint32_t size_low_net = htonl((uint32_t)(file_size_64 & UINT32_MAX));

    if (send_all(sockfd, &cmd, sizeof(cmd)) != 0 ||
        send_upload_field(sockfd, g_username) != 0 ||
        send_upload_field(sockfd, dir_to_send) != 0 ||
        send_upload_field(sockfd, filename) != 0 ||
        send_all(sockfd, &size_high_net, sizeof(size_high_net)) != 0 ||
        send_all(sockfd, &size_low_net, sizeof(size_low_net)) != 0) {
        perror("上传请求发送失败");
        fclose(fp);
        free(filepath_copy);
        close(sockfd);
        return;
    }

    // 用于计算速度
    struct timeval start, now;
    gettimeofday(&start, NULL);
    
    // 上传文件内容
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (send_all(sockfd, buffer, n) != 0) {
            if (progress_started) {
                printf("\n");
            }
            perror("文件内容发送失败");
            fclose(fp);
            free(filepath_copy);
            close(sockfd);
            return;
        }
        uploaded_size += n;
        
        // 计算速度
        gettimeofday(&now, NULL);
        double time_spent = (now.tv_sec - start.tv_sec) + 
                          (now.tv_usec - start.tv_usec) / 1000000.0;
        double speed = time_spent > 0 ? uploaded_size / time_spent : 0;
        
        show_progress(filename, "上传", (long)uploaded_size, file_size, speed);
        progress_started = 1;
    }

    if (ferror(fp)) {
        if (progress_started) {
            printf("\n");
        }
        perror("读取上传文件失败");
        fclose(fp);
        free(filepath_copy);
        close(sockfd);
        return;
    }

    char response;
    if (receive_all(sockfd, &response, sizeof(response)) != 0) {
        if (progress_started) {
            printf("\n");
        }
        perror("未收到服务端上传结果");
        fclose(fp);
        free(filepath_copy);
        close(sockfd);
        return;
    }

    if (progress_started) {
        printf("\n");
    }
    if (response == 1) {
        printf("✅ 文件 '%s' 上传完成\n", filename);
    } else {
        printf("❌ 文件 '%s' 上传失败：服务端未完整接收文件\n", filename);
    }

    fclose(fp);
    free(filepath_copy);
    close(sockfd);
}

/*
 * 从当前远程目录下载一个文件到客户端工作目录。
 * D 命令先读取存在标志和 64 位大小，再持续接收文件内容并更新进度。
 */
void download_file(const char *filename) {
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    char cmd = 'D';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 发送当前目录路径
    int dir_len = strlen(g_current_dir);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    write(sockfd, g_current_dir, dir_len);

    // 发送文件名
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

/* 把字节数转换为最多一位小数的 B/KB/MB/GB/TB 人类可读字符串。 */
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

/*
 * 使用已经连接的 sockfd 发送 S 命令，读取当前目录条目及大小、修改时间并
 * 打印表格。与多数辅助函数不同，本函数会在返回前关闭传入的 sockfd。
 */
void send_list_files(int sockfd, const char* username) {
    char cmd = 'S';  // 使用正确的命令字符
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
    if (read(sockfd, &res, sizeof(res)) <= 0) {
        printf("服务器通信错误\n");
        close(sockfd);
        return;
    }

    if(res == 0) {
        printf("目录不存在或读取失败: %s\n", g_current_dir);
        close(sockfd);
        return;
    }

    int file_count_net;
    if (read(sockfd, &file_count_net, sizeof(file_count_net)) <= 0) {
        printf("读取文件数量失败\n");
        close(sockfd);
        return;
    }
    int file_count = ntohl(file_count_net);
    
    printf("\n当前目录 (%s) 共 %d 个项目:\n", g_current_dir, file_count);
    printf("%-40s %-15s %-20s\n", "名称", "大小", "修改时间");
    printf("--------------------------------------------------------------------------------\n");

    for(int i = 0; i < file_count; i++) {
        // 读取类型（文件/目录）- 服务器发送的是4字节整数
        uint32_t type_net;
        if (read(sockfd, &type_net, sizeof(type_net)) <= 0) {
            printf("读取文件类型失败\n");
            break;
        }
        uint32_t type = ntohl(type_net);

        // 读取名称
        int name_len_net;
        if (read(sockfd, &name_len_net, sizeof(name_len_net)) <= 0) {
            printf("读取文件名长度失败\n");
            break;
        }
        int name_len = ntohl(name_len_net);
        if (name_len <= 0 || name_len >= 256) {
            printf("文件名长度异常: %d\n", name_len);
            break;
        }

        char name[256];
        if (read(sockfd, name, name_len) <= 0) {
            printf("读取文件名失败\n");
            break;
        }
        name[name_len] = '\0';

        // 读取大小
        int64_t size_net;
        if (read(sockfd, &size_net, sizeof(size_net)) <= 0) {
            printf("读取文件大小失败\n");
            break;
        }
        int64_t size = be64toh(size_net);

        // 读取修改时间
        int64_t mtime_net;
        if (read(sockfd, &mtime_net, sizeof(mtime_net)) <= 0) {
            printf("读取修改时间失败\n");
            break;
        }
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
            char display_name[300];
            snprintf(display_name, sizeof(display_name), "%s/", name);
            printf("%-40s %-15s %-20s\n", display_name, size_str, time_str);
        } else {
            printf("%-40s %-15s %-20s\n", name, size_str, time_str);
        }
    }
    printf("--------------------------------------------------------------------------------\n\n");
    close(sockfd);
}

/*
 * 使用已经连接的 sockfd 发送 X 删除命令并打印结果。
 * 本函数不关闭 sockfd，所有权仍属于调用者，便于外层循环批量删除多个条目。
 */
void send_delete_file(int sockfd, const char* username, const char* filename) {
    char cmd = 'X';
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, username, ulen);

    // 发送当前目录路径
    int dir_len = strlen(g_current_dir);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    write(sockfd, g_current_dir, dir_len);

    // 发送文件名
    int fname_len = strlen(filename);
    int fname_len_net = htonl(fname_len);
    write(sockfd, &fname_len_net, sizeof(fname_len_net));
    write(sockfd, filename, fname_len);

    // 读取结果
    char res;
    if (read(sockfd, &res, sizeof(res)) > 0) {
        if(res == 1) {
            printf("✅ 删除成功: %s\n", filename);
        } else {
            printf("❌ 删除失败: %s (文件/目录不存在或无权限)\n", filename);
        }
    } else {
        printf("❌ 服务器通信错误\n");
    }
}

/*
 * 发送 M 命令创建远程目录。相对路径基于 g_current_dir 展开，绝对逻辑路径
 * 会去掉开头的 '/' 后发送；函数自行建立并关闭连接。
 */
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

/* 发送 T 命令创建远程空文件；已有文件内容不变，路径规则与 send_mkdir() 相同。 */
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

/*
 * 发送 Y 命令获取当前目录的递归树。响应由若干“类型、名称、深度”记录组成，
 * 类型 0 是结束标记，depth 用于输出树形缩进。
 */
void send_tree() {
    if (strlen(g_username) == 0) {
        printf("请先登录！\n");
        return;
    }

    int sockfd = connect_to_server();
    if (sockfd < 0) {
        printf("无法连接到服务器\n");
        return;
    }

    char cmd = 'Y';  // 使用命令Y来获取指定目录的树结构
    write(sockfd, &cmd, sizeof(cmd));

    // 发送用户名
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    // 发送当前目录路径
    int dir_len = strlen(g_current_dir);
    int dir_len_net = htonl(dir_len);
    write(sockfd, &dir_len_net, sizeof(dir_len_net));
    write(sockfd, g_current_dir, dir_len);

    char res;
    if (read(sockfd, &res, sizeof(res)) <= 0) {
        printf("服务器通信错误\n");
        close(sockfd);
        return;
    }

    if (!res) {
        printf("获取目录树失败，目录可能不存在: %s\n", g_current_dir);
        close(sockfd);
        return;
    }

    printf("\n目录树 (%s):\n", g_current_dir);
    if (strcmp(g_current_dir, "/") == 0) {
        printf("📁 /\n");
    } else {
        printf("📁 %s\n", g_current_dir);
    }

    while (1) {
        char type;
        if (read(sockfd, &type, sizeof(type)) <= 0 || type == 0) break;

        int name_len_net;
        if (read(sockfd, &name_len_net, sizeof(name_len_net)) <= 0) break;
        int name_len = ntohl(name_len_net);

        char name[256];
        if (read(sockfd, name, name_len) <= 0) break;
        name[name_len] = '\0';

        int depth_net;
        if (read(sockfd, &depth_net, sizeof(depth_net)) <= 0) break;
        int depth = ntohl(depth_net);

        // 打印缩进
        for (int i = 0; i < depth; i++) {
            printf("│   ");
        }

        // 打印项目
        if (type == 1) { // 文件
            printf("├── 📄 %s\n", name);
        } else { // 目录
            printf("├── 📁 %s/\n", name);
        }
    }

    close(sockfd);
}

/*
 * 创建 IPv4 TCP socket 并连接 server_IP:PORT。
 * 成功返回由调用者负责关闭的 fd；任一步失败都会关闭已创建的 socket 并
 * 返回 -1。
 */
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


/*
 * 客户端入口：安装 Readline 补全回调，循环读取并解析命令，调用对应协议
 * 函数。readline() 返回的行和为 strtok 创建的副本都在每轮结束时释放。
 */
int main(void) {
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
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                int sockfd = connect_to_server();
                if (sockfd >= 0) {
                    send_list_files(sockfd, g_username);
                    // sockfd在send_list_files中已经被关闭
                } else {
                    printf("无法连接到服务器\n");
                }
            }
        } else if (strcmp(cmd, "delete") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char* filename = strtok(NULL, " \t\n");
                if (!filename) {
                    printf("用法: delete <文件名或目录名>\n");
                } else {
                    while (filename) {
                        int sockfd = connect_to_server();
                        if (sockfd >= 0) {
                            send_delete_file(sockfd, g_username, filename);
                            close(sockfd);
                            // 更新补全缓存
                            fetch_server_entries(g_current_dir);
                        } else {
                            printf("无法连接到服务器\n");
                        }
                        filename = strtok(NULL, " \t\n");
                    }
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
                    // 无参数，切换到根目录
                    strcpy(g_current_dir, "/");
                    printf("切换到根目录: /\n");
                    fetch_server_entries(g_current_dir);
                } else {
                    char new_path[1024];

                    // 处理特殊路径
                    if (strcmp(path, "..") == 0) {
                        // 返回上级目录
                        if (strcmp(g_current_dir, "/") == 0) {
                            printf("已在根目录\n");
                            continue;
                        } else {
                            strcpy(new_path, g_current_dir);
                            char *last_slash = strrchr(new_path, '/');
                            if (last_slash && last_slash != new_path) {
                                *last_slash = '\0';
                            } else {
                                strcpy(new_path, "/");
                            }
                        }
                    } else if (strcmp(path, ".") == 0) {
                        // 当前目录，不变
                        printf("当前目录: %s\n", g_current_dir);
                        continue;
                    } else if (path[0] == '/') {
                        // 绝对路径
                        strncpy(new_path, path, sizeof(new_path)-1);
                        new_path[sizeof(new_path)-1] = '\0';
                    } else {
                        // 相对路径
                        if (strcmp(g_current_dir, "/") == 0) {
                            snprintf(new_path, sizeof(new_path), "/%s", path);
                        } else {
                            snprintf(new_path, sizeof(new_path), "%s/%s", g_current_dir, path);
                        }
                    }

                    // 规范化路径
                    normalize_path(new_path);

                    // 验证目录是否存在
                    int sockfd = connect_to_server();
                    if (sockfd >= 0) {
                        char cmd_char = 'V';  // 验证目录命令
                        write(sockfd, &cmd_char, sizeof(cmd_char));

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
                        if (read(sockfd, &res, sizeof(res)) > 0) {
                            if (res) {
                                strcpy(g_current_dir, new_path);
                                printf("切换到目录: %s\n", new_path);
                                // 更新补全缓存
                                fetch_server_entries(g_current_dir);
                            } else {
                                printf("目录不存在: %s\n", new_path);
                            }
                        } else {
                            printf("服务器通信错误\n");
                        }
                        close(sockfd);
                    } else {
                        printf("无法连接到服务器\n");
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
