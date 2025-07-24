#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>// For inet_ntoa
#include <sys/socket.h>
#include <netinet/in.h>
#include <sqlite3.h>// For SQLite database operations
#include <sys/stat.h> // For mkdir and stat
#include <sys/types.h> // For ssize_t
#include <openssl/sha.h>// For SHA-256 hashing
#include <pthread.h> // For multithreading
#include <dirent.h> // For directory operations
#include <time.h> // For time functions
#include <errno.h>
#include <libgen.h> // 为了使用dirname函数
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#else
#include <endian.h>
#endif

// 确保用户目录存在
int ensure_user_dir(const char *username) {
    char user_dir[1024];
    snprintf(user_dir, sizeof(user_dir), "netdisk_data/%s", username);
    
    // 先确保 netdisk_data 目录存在
    if(access("netdisk_data", F_OK) != 0) {
        if(mkdir("netdisk_data", 0755) == -1) {
            printf("Failed to create netdisk_data directory: %s\n", strerror(errno));
            return -1;
        }
    }
    
    // 再确保用户目录存在
    if(access(user_dir, F_OK) != 0) {
        if(mkdir(user_dir, 0755) == -1) {
            printf("Failed to create user directory '%s': %s\n", user_dir, strerror(errno));
            return -1;
        }
    }
    return 0;
}

// 添加递归创建目录的辅助函数
int mkdirs(const char *path) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            if(access(tmp, F_OK) != 0) {
                if(mkdir(tmp, 0755) == -1) {
                    printf("Failed to create directory '%s': %s\n", tmp, strerror(errno));
                    return -1;
                }
            }
            *p = '/';
        }
    }
    if(access(tmp, F_OK) != 0) {
        if(mkdir(tmp, 0755) == -1) {
            printf("Failed to create directory '%s': %s\n", tmp, strerror(errno));
            return -1;
        }
    }
    return 0;
}

void sha256_string(const char *str, char *out_hex);

#define PORT 9000

void * handle_client(void *arg) {
    int conn_fd = *(int *)arg;
    free(arg); // Free the allocated memory for connection file descriptor
    //1.线程内自己打开数据库
    //char buffer[1024];
    //ssize_t n;
    sqlite3 *db;
    
    if(sqlite3_open("netdisk.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        close(conn_fd);
        return NULL;
    }
    //简单菜单：先收指令
    char cmd;
    read(conn_fd,&cmd, sizeof(cmd));

    if(cmd=='R'){//注册
        //1.读取用户名长度和用户名
        int ulen_net,plen_net; // Network byte order for username and password lengths
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net); // Convert to host byte order
        char username[ulen + 1]; // +1 for null terminator
        read(conn_fd, username, ulen);
        username[ulen] = '\0'; // Null-terminate the string

        //2.读取密码长度和密码
        read(conn_fd, &plen_net, sizeof(plen_net));
        int plen = ntohl(plen_net); // Convert to host byte order
        char password[plen + 1]; // +1 for null terminator
        read(conn_fd, password, plen);
        password[plen] = '\0'; // Null-terminate the string

        //3.插入数据到数据库
        sqlite3_stmt *stmt;// Prepare SQL statement
        const char *sql= "INSERT INTO users (username, password) VALUES (?, ?);";
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(stmt,1,username,-1, SQLITE_STATIC);
            char password_hash[65];
            sha256_string(password, password_hash);
            sqlite3_bind_text(stmt, 2, password_hash, -1, SQLITE_STATIC);
                            char res=1;
            if(sqlite3_step(stmt) ==SQLITE_DONE){// Execute the statement
                //注册成功 创建用户目录
                char path[256];
                snprintf(path, sizeof(path), "netdisk_data/%s", username);// Create user directory
                mkdir("netdisk_data",0755); // Create directory with permissions
                mkdir(path, 0755); // Create user directory with permissions
                res=1; // Registration successful
            }else{
                res=0; // Registration failed
            }
            write(conn_fd, &res, sizeof(res)); // Send registration result to client
            sqlite3_finalize(stmt); // Finalize the statement
        }
        close(conn_fd); // Close the connection
        pthread_exit(NULL); // Continue to accept next connection
    }
    else if(cmd=='L'){//登录
        //1.读取用户名和密码
        int ulen_net, plen_net; // Network byte order for username and password 
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net); // Convert to host byte order
        char username[ulen + 1]; // +1 for null terminator
        read(conn_fd, username, ulen);
        username[ulen] = '\0'; // Null-terminate the string

        read(conn_fd, &plen_net, sizeof(plen_net));
        int plen = ntohl(plen_net); // Convert to host byte order
        char password[plen + 1]; // +1 for null terminator
        read(conn_fd, password, plen);
        password[plen] = '\0'; // Null-terminate the string 
        printf("[DEBUG] Received username: '%s', password: '%s'\n", username, password);


        //2.查询数据库验证用户
        sqlite3_stmt *stmt;
        const char *sql = "SELECT password FROM users WHERE username = ?;";
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, username, -1,SQLITE_STATIC);
            char res = 0;
            if(sqlite3_step(stmt) == SQLITE_ROW){ // 如果存在该用户
                const char *db_password = (const char *)sqlite3_column_text(stmt, 0); // 获取数据库中的哈希密码
                char password_hash[65];
                sha256_string(password, password_hash); // 对客户端输入的明文密码哈希
                if(strcmp(db_password, password_hash) == 0){
                    res = 1;
                    printf("User %s logged in successfully ✅\n", username);
                } else {
                    printf("Login failed for user %s ❌ (password mismatch)\n", username);
                }
            } else {
                printf("Login failed for user %s ❌ (no such user)\n", username);
            }
            write(conn_fd, &res, sizeof(res)); // Send login result to client
            sqlite3_finalize(stmt);
        }
        close(conn_fd); // Close the connection
        pthread_exit(NULL); // Continue to accept next connection
    }
    if(cmd=='U'){// 上传文件
        //1.读取用户名长度
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取目标目录
        int dir_len_net;
        read(conn_fd, &dir_len_net, sizeof(dir_len_net));
        int dir_len = ntohl(dir_len_net);
        char target_dir[1024] = "";
        if (dir_len > 0) {
            read(conn_fd, target_dir, dir_len);
            target_dir[dir_len] = '\0';
        }

        //1.读取文件名长度
        int name_len_net;
        read(conn_fd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        char filename[name_len + 1];
        read(conn_fd, filename, name_len);
        filename[name_len] = '\0';

        // 确保用户目录存在
        if (ensure_user_dir(username) != 0) {
            close(conn_fd);
            pthread_exit(NULL);
        }

        // 如果指定了目标目录，确保它存在
        char dir_path[1024];
        if (strlen(target_dir) > 0) {
            if (target_dir[0] == '/') {
                snprintf(dir_path, sizeof(dir_path), "netdisk_data/%s%s", username, target_dir);
            } else {
                snprintf(dir_path, sizeof(dir_path), "netdisk_data/%s/%s", username, target_dir);
            }
            if (mkdirs(dir_path) != 0) {
                printf("Failed to create target directory '%s'\n", dir_path);
                close(conn_fd);
                pthread_exit(NULL);
            }
        }

        //拼接文件路径
        char filepath[1024];
        if (strlen(target_dir) > 0) {
            if (target_dir[0] == '/') {
                snprintf(filepath, sizeof(filepath), "netdisk_data/%s%s/%s", username, target_dir, filename);
            } else {
                snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s/%s", username, target_dir, filename);
            }
        } else {
            snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s", username, filename);
        }

        printf("Uploading file to: %s\n", filepath);

        //3.用文件名创建文件
        FILE *fp = fopen(filepath, "wb");
        if(fp == NULL) {
            perror("File creation error❌");
            close(conn_fd);
            pthread_exit(NULL);
        }

        //4.接收文件内容
        char buffer[4096];
        ssize_t n;
        while((n = read(conn_fd, buffer, sizeof(buffer))) > 0) {
            fwrite(buffer, sizeof(char), n, fp);
        }
        printf("File upload successfully as %s ✅\n", filepath);
        fclose(fp);
    }
    else if(cmd=='D'){// 下载文件
        //1.读取用户名长度
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        //1.读取文件名长度
        int name_len_net;
        read(conn_fd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);

        //2.读取文件名
        char filename[name_len + 1];
        read(conn_fd, filename, name_len);
        filename[name_len] = '\0';

        //拼接专属用户目录
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s", username, filename);

        //3.打开文件
        FILE *fp = fopen(filepath, "rb");
        char flag = 1;
        if(fp == NULL) {
            flag = 0;
            write(conn_fd, &flag, sizeof(flag));
            perror("File not found❌");
            close(conn_fd);
            pthread_exit(NULL);
        }
        write(conn_fd, &flag, sizeof(flag));

        // 获取并发送文件大小（64位）
        fseek(fp, 0, SEEK_END);
        int64_t file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        // 分别发送高32位和低32位
        uint32_t size_high = (file_size >> 32) & 0xFFFFFFFF;
        uint32_t size_low = file_size & 0xFFFFFFFF;
        size_high = htonl(size_high);
        size_low = htonl(size_low);
        write(conn_fd, &size_high, sizeof(size_high));
        write(conn_fd, &size_low, sizeof(size_low));

        // 发送文件内容
        char buffer[4096];
        size_t n;
        while((n = fread(buffer, sizeof(char), sizeof(buffer), fp)) > 0) {
            write(conn_fd, buffer, n);
        }
        printf("File download successfully as %s ✅\n", filename);
        fclose(fp);
    } else if(cmd == 'F') { // 获取文件列表
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 拼接用户目录
        char userdir[256];
        snprintf(userdir, sizeof(userdir), "netdisk_data/%s", username);

        // 列出文件
        DIR *dir = opendir(userdir);
        if (dir == NULL) {
            char res = 0;
            write(conn_fd, &res, sizeof(res));
            close(conn_fd);
            pthread_exit(NULL);
        }
        char res = 1;
        write(conn_fd, &res, sizeof(res));

        struct dirent *entry;
        int file_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) file_count++;
        }
        rewinddir(dir);
        int file_count_net = htonl(file_count);
        write(conn_fd, &file_count_net, sizeof(file_count_net));

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                // 获取文件信息
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", userdir, entry->d_name);
                struct stat file_stat;
                if (stat(filepath, &file_stat) == 0) {
                    // 发送文件名长度和文件名
                    int name_len = strlen(entry->d_name);
                    int name_len_net = htonl(name_len);
                    write(conn_fd, &name_len_net, sizeof(name_len_net));
                    write(conn_fd, entry->d_name, name_len);

                    // 发送文件大小
                    int64_t size_net = htobe64(file_stat.st_size);
                    write(conn_fd, &size_net, sizeof(size_net));

                    // 发送文件修改时间
                    int64_t mtime_net = htobe64(file_stat.st_mtime);
                    write(conn_fd, &mtime_net, sizeof(mtime_net));
                }
            }
        }
        closedir(dir);
        close(conn_fd);
        pthread_exit(NULL);
    } else if(cmd == 'X') { // 删除文件
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取文件名
        int name_len_net;
        read(conn_fd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        char filename[name_len + 1];
        read(conn_fd, filename, name_len);
        filename[name_len] = '\0';

        // 拼接文件路径
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s", username, filename);

        // 删除文件
        char res = (remove(filepath) == 0) ? 1 : 0;
        write(conn_fd, &res, sizeof(res));
        close(conn_fd);
        pthread_exit(NULL);
    } else if(cmd == 'M') { // mkdir command
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取目录路径
        int path_len_net;
        read(conn_fd, &path_len_net, sizeof(path_len_net));
        int path_len = ntohl(path_len_net);
        char dirpath[path_len + 1];
        read(conn_fd, dirpath, path_len);
        dirpath[path_len] = '\0';

        // 确保用户目录存在
        if (ensure_user_dir(username) != 0) {
            char res = 0;
            write(conn_fd, &res, sizeof(res));
            close(conn_fd);
            pthread_exit(NULL);
        }

        // 构建完整路径（去掉开头的斜杠，如果有的话）
        char full_path[1024];
        if (dirpath[0] == '/') {
            snprintf(full_path, sizeof(full_path), "netdisk_data/%s%s", username, dirpath);
        } else {
            snprintf(full_path, sizeof(full_path), "netdisk_data/%s/%s", username, dirpath);
        }
        
        printf("Attempting to create directory: %s\n", full_path);

        // 递归创建目录
        char res = (mkdirs(full_path) == 0) ? 1 : 0;
        if (!res) {
            printf("Failed to create directory '%s': %s\n", full_path, strerror(errno));
        } else {
            printf("Successfully created directory '%s'\n", full_path);
        }
        write(conn_fd, &res, sizeof(res));
        
    } else if(cmd == 'T') { // touch command
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取文件路径
        int path_len_net;
        read(conn_fd, &path_len_net, sizeof(path_len_net));
        int path_len = ntohl(path_len_net);
        char filepath[path_len + 1];
        read(conn_fd, filepath, path_len);
        filepath[path_len] = '\0';

        // 构建完整路径
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "netdisk_data/%s/%s", username, filepath);

        // 创建空文件
        FILE *fp = fopen(full_path, "a");
        char res = (fp != NULL) ? 1 : 0;
        if(fp) fclose(fp);
        write(conn_fd, &res, sizeof(res));

    } else if(cmd == 'E') { // tree command
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 构建用户根目录路径
        char root_path[1024];
        snprintf(root_path, sizeof(root_path), "netdisk_data/%s", username);

        // 首先发送成功标志
        char res = 1;
        write(conn_fd, &res, sizeof(res));

        // 遍历目录树并发送信息
        DIR *dir = opendir(root_path);
        if(dir) {
            struct dirent *entry;
            while((entry = readdir(dir)) != NULL) {
                if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;

                // 获取文件/目录信息
                char full_path[2048];
                snprintf(full_path, sizeof(full_path), "%s/%s", root_path, entry->d_name);
                struct stat st;
                if(stat(full_path, &st) == 0) {
                    // 发送项目类型（文件=1，目录=2）
                    char type = S_ISDIR(st.st_mode) ? 2 : 1;
                    write(conn_fd, &type, sizeof(type));

                    // 发送名称长度和名称
                    int name_len = strlen(entry->d_name);
                    int name_len_net = htonl(name_len);
                    write(conn_fd, &name_len_net, sizeof(name_len_net));
                    write(conn_fd, entry->d_name, name_len);

                    // 发送深度（这里都是第一层，为1）
                    int depth = 1;
                    int depth_net = htonl(depth);
                    write(conn_fd, &depth_net, sizeof(depth_net));
                }
            }
            closedir(dir);
        }
        // 发送结束标记
        char end = 0;
        write(conn_fd, &end, sizeof(end));

    } else if(cmd == 'L') { // 获取目录列表
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取目标目录
        int dir_len_net;
        read(conn_fd, &dir_len_net, sizeof(dir_len_net));
        int dir_len = ntohl(dir_len_net);
        char target_dir[1024] = "";
        if (dir_len > 0) {
            read(conn_fd, target_dir, dir_len);
            target_dir[dir_len] = '\0';
        }

        // 构建完整路径
        char full_path[1024];
        if (strcmp(target_dir, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "netdisk_data/%s", username);
        } else {
            if (target_dir[0] == '/') {
                snprintf(full_path, sizeof(full_path), "netdisk_data/%s%s", username, target_dir);
            } else {
                snprintf(full_path, sizeof(full_path), "netdisk_data/%s/%s", username, target_dir);
            }
        }

        // 列出目录内容
        DIR *dir = opendir(full_path);
        if (dir == NULL) {
            char res = 0;
            write(conn_fd, &res, sizeof(res));
            close(conn_fd);
            pthread_exit(NULL);
        }
        char res = 1;
        write(conn_fd, &res, sizeof(res));

        // 计算条目数量
        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count++;
            }
        }
        rewinddir(dir);

        // 发送条目数量
        int count_net = htonl(count);
        write(conn_fd, &count_net, sizeof(count_net));

        // 发送每个条目的信息
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // 获取文件信息
            char item_path[2048];
            snprintf(item_path, sizeof(item_path), "%s/%s", full_path, entry->d_name);
            struct stat st;
            if (stat(item_path, &st) == 0) {
                // 发送类型（1=文件，2=目录）
                char type = S_ISDIR(st.st_mode) ? 2 : 1;
                write(conn_fd, &type, sizeof(type));

                // 发送名称
                int name_len = strlen(entry->d_name);
                int name_len_net = htonl(name_len);
                write(conn_fd, &name_len_net, sizeof(name_len_net));
                write(conn_fd, entry->d_name, name_len);

                // 发送大小
                int64_t size_net = htobe64(st.st_size);
                write(conn_fd, &size_net, sizeof(size_net));

                // 发送修改时间
                int64_t mtime_net = htobe64(st.st_mtime);
                write(conn_fd, &mtime_net, sizeof(mtime_net));
            }
        }
        closedir(dir);
    } else if(cmd == 'V') { // 验证目录是否存在
        // 读取用户名
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net);
        char username[ulen + 1];
        read(conn_fd, username, ulen);
        username[ulen] = '\0';

        // 读取目标目录
        int path_len_net;
        read(conn_fd, &path_len_net, sizeof(path_len_net));
        int path_len = ntohl(path_len_net);
        char path[1024];
        read(conn_fd, path, path_len);
        path[path_len] = '\0';

        // 构建完整路径
        char full_path[1024];
        if (strcmp(path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "netdisk_data/%s", username);
        } else {
            if (path[0] == '/') {
                snprintf(full_path, sizeof(full_path), "netdisk_data/%s%s", username, path);
            } else {
                snprintf(full_path, sizeof(full_path), "netdisk_data/%s/%s", username, path);
            }
        }

        // 检查目录是否存在
        struct stat st;
        char res = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
        write(conn_fd, &res, sizeof(res));
    }

    // 关闭连接
    sqlite3_close(db);
    close(conn_fd); // Close the connection
    pthread_exit(NULL); // Exit the thread
}

void sha256_string(const char *str, char *out_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)str, strlen(str), hash);
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out_hex + (i * 2), "%02x", hash[i]);
    out_hex[64] = 0;
}


int main(){
    // Initialize SQLite database connection
    sqlite3 *db;
    int rc = sqlite3_open("netdisk.db", &db);
    if(rc){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }
    const char *sql="CREATE TABLE IF NOT EXISTS users("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "username TEXT UNIQUE, "
                    "password TEXT);";
    sqlite3_exec(db, sql, 0, 0, 0);


    int listen_fd;
    struct sockaddr_in server_addr, cli_addr;// Server and client address structures

    socklen_t cli_len = sizeof(cli_addr);// Length of client address structure

    //char buffer[1024];
    //ssize_t n;

    // Create a socket
    listen_fd=socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0) {
        perror("socket creation error❌");
        exit(EXIT_FAILURE);
    }
    //Bind port and address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address
    server_addr.sin_port = htons(PORT); // Set port number
    if(bind(listen_fd,(struct sockaddr *)&server_addr, sizeof(server_addr))<0){
        perror("bind error❌");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if(listen(listen_fd, 5) < 0) {
        perror("listen error❌");
        exit(EXIT_FAILURE);
    }
    printf("✅Server is listening on port %d\n", PORT);


    while(1){
        int *conn_fd_ptr = malloc(sizeof(int)); // Allocate memory for connection file descriptor
        *conn_fd_ptr = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if(*conn_fd_ptr < 0) {
            perror("accept error❌");
            free(conn_fd_ptr); // Free allocated memory
            continue; // Continue to accept next connection
        }
        pthread_t thread_id;
        if(pthread_create(&thread_id, NULL, handle_client, conn_fd_ptr) != 0) {
            perror("pthread_create error❌");
            free(conn_fd_ptr); // Free allocated memory
            close(*conn_fd_ptr); // Close the connection
            continue; // Continue to accept next connection
        }
        pthread_detach(thread_id); // Detach the thread to avoid memory leaks
    }
    close(listen_fd); // Close the listening socket
    sqlite3_close(db); // Close the SQLite database connection
    return 0;
}