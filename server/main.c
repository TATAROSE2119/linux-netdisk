#define _GNU_SOURCE

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
#include <fcntl.h>
#include <libgen.h> // 为了使用dirname函数
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "thread_pool.h"
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#else
#include <endian.h>
#endif

#define PORT 9000
#define DEFAULT_WORKER_COUNT 8
#define DEFAULT_QUEUE_CAPACITY 128
#define DEFAULT_CLIENT_TIMEOUT_SECONDS 60
#define MAX_WORKER_COUNT 256
#define MAX_QUEUE_CAPACITY 65536
#define MAX_CLIENT_TIMEOUT_SECONDS 3600
#define MAX_USERNAME_LENGTH 128
#define MAX_PASSWORD_LENGTH 1024
#define MAX_FILENAME_LENGTH 255
#define MAX_PATH_LENGTH 1023
#define STORAGE_PATH_SIZE 2048

static bool is_safe_component(const char *component) {
    return component != NULL && component[0] != '\0' &&
           strcmp(component, ".") != 0 && strcmp(component, "..") != 0 &&
           strchr(component, '/') == NULL;
}

static int append_path_text(char *destination, size_t destination_size,
                            size_t *used, const char *text) {
    size_t length = strlen(text);

    if (length > destination_size - 1 - *used) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(destination + *used, text, length);
    *used += length;
    destination[*used] = '\0';
    return 0;
}

static int append_path_segment(char *destination, size_t destination_size,
                               size_t *used, const char *segment) {
    if (*used > 0 && destination[*used - 1] != '/' &&
        append_path_text(destination, destination_size, used, "/") != 0) {
        return -1;
    }
    return append_path_text(destination, destination_size, used, segment);
}

/* Convert a client path to a relative path and reject attempts to escape it. */
static int normalize_relative_path(const char *input, char *output,
                                   size_t output_size) {
    const char *cursor = input != NULL ? input : "";
    size_t used = 0;

    if (output_size == 0) {
        errno = EINVAL;
        return -1;
    }
    output[0] = '\0';

    while (*cursor != '\0') {
        const char *end;
        size_t component_length;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        end = strchr(cursor, '/');
        component_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (component_length == 1 && cursor[0] == '.') {
            cursor += component_length;
            continue;
        }
        if (component_length == 2 && cursor[0] == '.' && cursor[1] == '.') {
            errno = EPERM;
            return -1;
        }

        if (used > 0) {
            if (used >= output_size - 1) {
                errno = ENAMETOOLONG;
                return -1;
            }
            output[used++] = '/';
        }
        if (component_length > output_size - 1 - used) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(output + used, cursor, component_length);
        used += component_length;
        output[used] = '\0';
        cursor += component_length;
    }
    return 0;
}

static int build_user_path(char *destination, size_t destination_size,
                           const char *username, const char *path,
                           const char *leaf) {
    char normalized_path[MAX_PATH_LENGTH + 1];
    size_t used = 0;

    if (!is_safe_component(username) ||
        (leaf != NULL && !is_safe_component(leaf)) ||
        normalize_relative_path(path, normalized_path,
                                sizeof(normalized_path)) != 0) {
        errno = EPERM;
        return -1;
    }

    destination[0] = '\0';
    if (append_path_text(destination, destination_size, &used,
                         "netdisk_data") != 0 ||
        append_path_segment(destination, destination_size, &used,
                            username) != 0 ||
        (normalized_path[0] != '\0' &&
         append_path_segment(destination, destination_size, &used,
                             normalized_path) != 0) ||
        (leaf != NULL &&
         append_path_segment(destination, destination_size, &used,
                             leaf) != 0)) {
        return -1;
    }
    return 0;
}

static int join_paths(char *destination, size_t destination_size,
                      const char *parent, const char *child) {
    size_t used = 0;

    destination[0] = '\0';
    return append_path_text(destination, destination_size, &used, parent) == 0 &&
           append_path_segment(destination, destination_size, &used, child) == 0
               ? 0
               : -1;
}

static int ensure_directory(const char *path) {
    struct stat status;

    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST && lstat(path, &status) == 0 && S_ISDIR(status.st_mode)) {
        return 0;
    }
    return -1;
}

// 确保用户目录存在
int ensure_user_dir(const char *username) {
    char user_dir[STORAGE_PATH_SIZE];

    if (build_user_path(user_dir, sizeof(user_dir), username, NULL, NULL) != 0) {
        return -1;
    }

    if (ensure_directory("netdisk_data") != 0) {
        printf("Failed to create netdisk_data directory: %s\n", strerror(errno));
        return -1;
    }

    if (ensure_directory(user_dir) != 0) {
        printf("Failed to create user directory '%s': %s\n", user_dir, strerror(errno));
        return -1;
    }
    return 0;
}

// 添加递归创建目录的辅助函数
int mkdirs(const char *path) {
    char tmp[STORAGE_PATH_SIZE];
    char *p = NULL;
    size_t len;

    len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, len + 1);
    if (len == 0) {
        errno = EINVAL;
        return -1;
    }
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            if (ensure_directory(tmp) != 0) {
                printf("Failed to create directory '%s': %s\n", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    if (ensure_directory(tmp) != 0) {
        printf("Failed to create directory '%s': %s\n", tmp, strerror(errno));
        return -1;
    }
    return 0;
}

static int remove_tree(const char *path) {
    struct stat status;

    if (lstat(path, &status) != 0) {
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        return unlink(path);
    }

    DIR *directory = opendir(path);
    if (directory == NULL) {
        return -1;
    }

    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char child_path[STORAGE_PATH_SIZE];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (join_paths(child_path, sizeof(child_path), path, entry->d_name) != 0 ||
            remove_tree(child_path) != 0) {
            result = -1;
            break;
        }
    }
    int saved_errno = errno;
    closedir(directory);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    return rmdir(path);
}

void sha256_string(const char *str, char *out_hex);

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_read = read(fd, position, length);

        if (bytes_read == 0) {
            errno = ECONNRESET;
            return -1;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        position += bytes_read;
        length -= (size_t)bytes_read;
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_written = write(fd, position, length);

        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_written == 0) {
            errno = EPIPE;
            return -1;
        }
        position += bytes_written;
        length -= (size_t)bytes_written;
    }
    return 0;
}

static int read_u32(int fd, uint32_t *value) {
    uint32_t network_value;

    if (read_full(fd, &network_value, sizeof(network_value)) != 0) {
        return -1;
    }
    *value = ntohl(network_value);
    return 0;
}

static int read_string(int fd, char *buffer, size_t buffer_size,
                       size_t maximum_length) {
    uint32_t length;

    if (buffer_size == 0 || maximum_length >= buffer_size ||
        read_u32(fd, &length) != 0) {
        return -1;
    }
    if (length > maximum_length) {
        errno = EMSGSIZE;
        return -1;
    }
    if (read_full(fd, buffer, length) != 0) {
        return -1;
    }
    buffer[length] = '\0';
    return 0;
}

#define READ_STRING_OR_CLEANUP(fd, buffer, maximum_length)                    \
    do {                                                                      \
        if (read_string((fd), (buffer), sizeof(buffer),                       \
                        (maximum_length)) != 0) {                             \
            goto cleanup;                                                     \
        }                                                                     \
    } while (0)

#define WRITE_OR_CLEANUP(fd, buffer, length)                                  \
    do {                                                                      \
        if (write_full((fd), (buffer), (length)) != 0) {                      \
            goto cleanup;                                                     \
        }                                                                     \
    } while (0)

// 函数声明
void send_directory_tree(int conn_fd, const char* dir_path, int depth);

void handle_client(int conn_fd) {
    //1.线程内自己打开数据库
    //char buffer[1024];
    //ssize_t n;
    sqlite3 *db = NULL;
    
    if(sqlite3_open("netdisk.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n",
                db != NULL ? sqlite3_errmsg(db) : "out of memory");
        goto cleanup;
    }
    sqlite3_busy_timeout(db, 5000);
    //简单菜单：先收指令
    char cmd;
    if (read_full(conn_fd, &cmd, sizeof(cmd)) != 0) {
        goto cleanup;
    }

    if(cmd=='R'){//注册
        //1.读取用户名长度和用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        //2.读取密码长度和密码
        char password[MAX_PASSWORD_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, password, MAX_PASSWORD_LENGTH);

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
                res = ensure_user_dir(username) == 0;
            }else{
                res=0; // Registration failed
            }
            sqlite3_finalize(stmt); // Finalize the statement
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
        }
        goto cleanup;
    }
    else if(cmd=='L'){//登录
        //1.读取用户名和密码
        char username[MAX_USERNAME_LENGTH + 1];
        char password[MAX_PASSWORD_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);
        READ_STRING_OR_CLEANUP(conn_fd, password, MAX_PASSWORD_LENGTH);
        printf("[DEBUG] Received login request for user '%s'\n", username);


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
            sqlite3_finalize(stmt);
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
        }
        goto cleanup;
    }
    if(cmd=='U'){// 上传文件
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        char target_dir[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, target_dir, MAX_PATH_LENGTH);

        char filename[MAX_FILENAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, filename, MAX_FILENAME_LENGTH);

        uint32_t size_high_net, size_low_net;
        if (read_full(conn_fd, &size_high_net, sizeof(size_high_net)) != 0 ||
            read_full(conn_fd, &size_low_net, sizeof(size_low_net)) != 0) {
            goto cleanup;
        }

        uint32_t size_high = ntohl(size_high_net);
        uint32_t size_low = ntohl(size_low_net);
        uint64_t file_size = ((uint64_t)size_high << 32) | size_low;
        char dir_path[STORAGE_PATH_SIZE];
        char filepath[STORAGE_PATH_SIZE];
        char temporary_path[STORAGE_PATH_SIZE];
        char response = 0;

        if (build_user_path(dir_path, sizeof(dir_path), username, target_dir,
                            NULL) != 0 ||
            build_user_path(filepath, sizeof(filepath), username, target_dir,
                            filename) != 0 ||
            ensure_user_dir(username) != 0 || mkdirs(dir_path) != 0) {
            WRITE_OR_CLEANUP(conn_fd, &response, sizeof(response));
            goto cleanup;
        }

        if (join_paths(temporary_path, sizeof(temporary_path), dir_path,
                       ".netdisk-upload-XXXXXX") != 0) {
            WRITE_OR_CLEANUP(conn_fd, &response, sizeof(response));
            goto cleanup;
        }

        int temporary_fd = mkstemp(temporary_path);
        if (temporary_fd < 0) {
            WRITE_OR_CLEANUP(conn_fd, &response, sizeof(response));
            goto cleanup;
        }
        FILE *fp = fdopen(temporary_fd, "wb");
        if (fp == NULL) {
            close(temporary_fd);
            unlink(temporary_path);
            WRITE_OR_CLEANUP(conn_fd, &response, sizeof(response));
            goto cleanup;
        }

        printf("Uploading file to: %s (expected=%" PRIu64 ")\n",
               filepath, file_size);
        char buffer[4096];
        uint64_t total_received = 0;
        bool upload_ok = true;

        while (total_received < file_size) {
            uint64_t remaining = file_size - total_received;
            size_t amount = remaining < sizeof(buffer)
                                ? (size_t)remaining
                                : sizeof(buffer);
            ssize_t bytes_read = read(conn_fd, buffer, amount);

            if (bytes_read < 0 && errno == EINTR) {
                continue;
            }
            if (bytes_read <= 0 ||
                fwrite(buffer, 1, (size_t)bytes_read, fp) !=
                    (size_t)bytes_read) {
                upload_ok = false;
                break;
            }
            total_received += (uint64_t)bytes_read;
        }

        if (upload_ok && (fflush(fp) != 0 || fsync(temporary_fd) != 0)) {
            upload_ok = false;
        }
        if (fclose(fp) != 0) {
            upload_ok = false;
        }
        if (upload_ok && total_received == file_size &&
            rename(temporary_path, filepath) == 0) {
            response = 1;
        } else {
            unlink(temporary_path);
        }

        WRITE_OR_CLEANUP(conn_fd, &response, sizeof(response));

        if (response == 1) {
            printf("File upload successfully as %s ✅ (%" PRIu64 " bytes)\n",
                   filepath, total_received);
        } else {
            printf("File upload failed for %s ❌ (expected %" PRIu64
                   ", received %" PRIu64 " bytes)\n",
                   filepath, file_size, total_received);
        }
    }
    else if(cmd=='D'){// 下载文件
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取当前目录路径
        char current_dir[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, current_dir, MAX_PATH_LENGTH);

        // 读取文件名
        char filename[MAX_FILENAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, filename, MAX_FILENAME_LENGTH);

        char filepath[STORAGE_PATH_SIZE];
        if (build_user_path(filepath, sizeof(filepath), username, current_dir,
                            filename) != 0) {
            char flag = 0;
            WRITE_OR_CLEANUP(conn_fd, &flag, sizeof(flag));
            goto cleanup;
        }

        //3.打开文件
        FILE *fp = fopen(filepath, "rb");
        char flag = 1;
        if(fp == NULL) {
            flag = 0;
            WRITE_OR_CLEANUP(conn_fd, &flag, sizeof(flag));
            perror("File not found❌");
            goto cleanup;
        }
        if (write_full(conn_fd, &flag, sizeof(flag)) != 0) {
            fclose(fp);
            goto cleanup;
        }

        // 获取并发送文件大小（64位）
        fseek(fp, 0, SEEK_END);
        int64_t file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        // 分别发送高32位和低32位
        uint32_t size_high = (file_size >> 32) & 0xFFFFFFFF;
        uint32_t size_low = file_size & 0xFFFFFFFF;
        size_high = htonl(size_high);
        size_low = htonl(size_low);
        if (write_full(conn_fd, &size_high, sizeof(size_high)) != 0 ||
            write_full(conn_fd, &size_low, sizeof(size_low)) != 0) {
            fclose(fp);
            goto cleanup;
        }

        // 发送文件内容
        char buffer[4096];
        size_t n;
        while((n = fread(buffer, sizeof(char), sizeof(buffer), fp)) > 0) {
            if (write_full(conn_fd, buffer, n) != 0) {
                fclose(fp);
                goto cleanup;
            }
        }
        printf("File download successfully as %s ✅\n", filename);
        fclose(fp);
    } else if(cmd == 'F') { // 获取文件列表
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 拼接用户目录
        char userdir[STORAGE_PATH_SIZE];
        if (build_user_path(userdir, sizeof(userdir), username, NULL, NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        // 列出文件
        DIR *dir = opendir(userdir);
        if (dir == NULL) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }
        char res = 1;
        if (write_full(conn_fd, &res, sizeof(res)) != 0) {
            closedir(dir);
            goto cleanup;
        }

        struct dirent *entry;
        int file_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) file_count++;
        }
        rewinddir(dir);
        int file_count_net = htonl(file_count);
        if (write_full(conn_fd, &file_count_net, sizeof(file_count_net)) != 0) {
            closedir(dir);
            goto cleanup;
        }

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                // 获取文件信息
                char filepath[STORAGE_PATH_SIZE];
                if (join_paths(filepath, sizeof(filepath), userdir,
                               entry->d_name) != 0) {
                    continue;
                }
                struct stat file_stat;
                if (stat(filepath, &file_stat) == 0) {
                    // 发送文件名长度和文件名
                    int name_len = strlen(entry->d_name);
                    int name_len_net = htonl(name_len);
                    if (write_full(conn_fd, &name_len_net,
                                   sizeof(name_len_net)) != 0 ||
                        write_full(conn_fd, entry->d_name, name_len) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }

                    // 发送文件大小
                    int64_t size_net = htobe64(file_stat.st_size);
                    if (write_full(conn_fd, &size_net, sizeof(size_net)) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }

                    // 发送文件修改时间
                    int64_t mtime_net = htobe64(file_stat.st_mtime);
                    if (write_full(conn_fd, &mtime_net, sizeof(mtime_net)) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }
                }
            }
        }
        closedir(dir);
        goto cleanup;
    } else if(cmd == 'X') { // 删除文件或目录
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取当前目录路径
        char current_dir[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, current_dir, MAX_PATH_LENGTH);

        // 读取文件名
        char filename[MAX_FILENAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, filename, MAX_FILENAME_LENGTH);

        char res = 0;
        char filepath[STORAGE_PATH_SIZE];
        if (build_user_path(filepath, sizeof(filepath), username, current_dir,
                            filename) == 0 && remove_tree(filepath) == 0) {
            res = 1;
        }

        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
        goto cleanup;
    } else if(cmd == 'M') { // mkdir command
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取目录路径
        char dirpath[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, dirpath, MAX_PATH_LENGTH);

        // 确保用户目录存在
        if (ensure_user_dir(username) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        char full_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_path, sizeof(full_path), username, dirpath,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }
        
        printf("Attempting to create directory: %s\n", full_path);

        // 递归创建目录
        char res = (mkdirs(full_path) == 0) ? 1 : 0;
        if (!res) {
            printf("Failed to create directory '%s': %s\n", full_path, strerror(errno));
        } else {
            printf("Successfully created directory '%s'\n", full_path);
        }
        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
        
    } else if(cmd == 'T') { // touch command
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取文件路径
        char filepath[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, filepath, MAX_PATH_LENGTH);

        char full_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_path, sizeof(full_path), username, filepath,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        // 创建空文件
        FILE *fp = fopen(full_path, "a");
        char res = (fp != NULL) ? 1 : 0;
        if(fp) fclose(fp);
        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));

    } else if(cmd == 'E') { // tree command
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 构建用户根目录路径
        char root_path[STORAGE_PATH_SIZE];
        if (build_user_path(root_path, sizeof(root_path), username, NULL,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        // 首先发送成功标志
        char res = 1;
        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));

        // 遍历目录树并发送信息
        DIR *dir = opendir(root_path);
        if(dir) {
            struct dirent *entry;
            while((entry = readdir(dir)) != NULL) {
                if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;

                // 获取文件/目录信息
                char full_path[STORAGE_PATH_SIZE];
                if (join_paths(full_path, sizeof(full_path), root_path,
                               entry->d_name) != 0) {
                    continue;
                }
                struct stat st;
                if(stat(full_path, &st) == 0) {
                    // 发送项目类型（文件=1，目录=2）
                    char type = S_ISDIR(st.st_mode) ? 2 : 1;
                    if (write_full(conn_fd, &type, sizeof(type)) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }

                    // 发送名称长度和名称
                    int name_len = strlen(entry->d_name);
                    int name_len_net = htonl(name_len);
                    if (write_full(conn_fd, &name_len_net,
                                   sizeof(name_len_net)) != 0 ||
                        write_full(conn_fd, entry->d_name, name_len) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }

                    // 发送深度（这里都是第一层，为1）
                    int depth = 1;
                    int depth_net = htonl(depth);
                    if (write_full(conn_fd, &depth_net,
                                   sizeof(depth_net)) != 0) {
                        closedir(dir);
                        goto cleanup;
                    }
                }
            }
            closedir(dir);
        }
        // 发送结束标记
        char end = 0;
        WRITE_OR_CLEANUP(conn_fd, &end, sizeof(end));

    } else if(cmd == 'S') { // 获取目录列表 (List directory)
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取目标目录
        char target_dir[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, target_dir, MAX_PATH_LENGTH);

        char full_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_path, sizeof(full_path), username, target_dir,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        printf("[DEBUG] L命令 - 用户: %s, 目标目录: '%s', 完整路径: %s\n", username, target_dir, full_path);

        // 列出目录内容
        DIR *dir = opendir(full_path);
        if (dir == NULL) {
            printf("[ERROR] 无法打开目录: %s (错误: %s)\n", full_path, strerror(errno));
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }
        char res = 1;
        if (write_full(conn_fd, &res, sizeof(res)) != 0) {
            closedir(dir);
            goto cleanup;
        }

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
        if (write_full(conn_fd, &count_net, sizeof(count_net)) != 0) {
            closedir(dir);
            goto cleanup;
        }

        // 发送每个条目的信息
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // 获取文件信息
            char item_path[STORAGE_PATH_SIZE];
            if (join_paths(item_path, sizeof(item_path), full_path,
                           entry->d_name) != 0) {
                continue;
            }
            struct stat st;
            if (stat(item_path, &st) == 0) {
                // 发送类型（1=文件，2=目录）- 使用4字节整数
                uint32_t type = S_ISDIR(st.st_mode) ? 2 : 1;
                uint32_t type_net = htonl(type);
                if (write_full(conn_fd, &type_net, sizeof(type_net)) != 0) {
                    closedir(dir);
                    goto cleanup;
                }

                // 发送名称
                int name_len = strlen(entry->d_name);
                int name_len_net = htonl(name_len);
                if (write_full(conn_fd, &name_len_net,
                               sizeof(name_len_net)) != 0 ||
                    write_full(conn_fd, entry->d_name, name_len) != 0) {
                    closedir(dir);
                    goto cleanup;
                }

                // 发送大小
                int64_t size_net = htobe64(st.st_size);
                if (write_full(conn_fd, &size_net, sizeof(size_net)) != 0) {
                    closedir(dir);
                    goto cleanup;
                }

                // 发送修改时间
                int64_t mtime_net = htobe64(st.st_mtime);
                if (write_full(conn_fd, &mtime_net, sizeof(mtime_net)) != 0) {
                    closedir(dir);
                    goto cleanup;
                }

                printf("[DEBUG] 发送条目: %s, 类型: %u, 大小: %ld\n",
                       entry->d_name, type, st.st_size);
            }
        }
        closedir(dir);
    } else if(cmd == 'N') { // 重命名文件或目录 (reName)
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取旧路径
        char old_path[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, old_path, MAX_PATH_LENGTH);

        // 读取新路径
        char new_path[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, new_path, MAX_PATH_LENGTH);

        char full_old_path[STORAGE_PATH_SIZE];
        char full_new_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_old_path, sizeof(full_old_path), username,
                            old_path, NULL) != 0 ||
            build_user_path(full_new_path, sizeof(full_new_path), username,
                            new_path, NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        printf("[DEBUG] 重命名: %s -> %s\n", full_old_path, full_new_path);

        // 执行重命名
        char res = (rename(full_old_path, full_new_path) == 0) ? 1 : 0;
        if (res == 0) {
            printf("[ERROR] 重命名失败: %s\n", strerror(errno));
        } else {
            printf("重命名成功: %s -> %s\n", old_path, new_path);
        }

        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
        goto cleanup;
    } else if(cmd == 'V') { // 验证目录是否存在
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取目标目录
        char path[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, path, MAX_PATH_LENGTH);

        char full_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_path, sizeof(full_path), username, path,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        // 检查目录是否存在
        struct stat st;
        char res = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
    } else if(cmd == 'Y') { // 获取指定目录的树结构
        // 读取用户名
        char username[MAX_USERNAME_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, username, MAX_USERNAME_LENGTH);

        // 读取目录路径
        char dir_path[MAX_PATH_LENGTH + 1];
        READ_STRING_OR_CLEANUP(conn_fd, dir_path, MAX_PATH_LENGTH);

        char full_path[STORAGE_PATH_SIZE];
        if (build_user_path(full_path, sizeof(full_path), username, dir_path,
                            NULL) != 0) {
            char res = 0;
            WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));
            goto cleanup;
        }

        // 检查目录是否存在
        struct stat st;
        char res = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
        WRITE_OR_CLEANUP(conn_fd, &res, sizeof(res));

        if (res) {
            // 递归发送目录树
            send_directory_tree(conn_fd, full_path, 0);
            // 发送结束标记
            char end_marker = 0;
            WRITE_OR_CLEANUP(conn_fd, &end_marker, sizeof(end_marker));
        }
    }

cleanup:
    if (db != NULL) {
        sqlite3_close(db);
    }
}

// 递归发送目录树结构
void send_directory_tree(int conn_fd, const char* dir_path, int depth) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[STORAGE_PATH_SIZE];
        if (join_paths(full_path, sizeof(full_path), dir_path,
                       entry->d_name) != 0) {
            continue;
        }

            struct stat st;
        if (stat(full_path, &st) == 0) {
            char type = S_ISDIR(st.st_mode) ? 2 : 1; // 2=目录, 1=文件
            if (write_full(conn_fd, &type, sizeof(type)) != 0) {
                break;
            }

            // 发送文件名
            int name_len = strlen(entry->d_name);
            int name_len_net = htonl(name_len);
            if (write_full(conn_fd, &name_len_net, sizeof(name_len_net)) != 0 ||
                write_full(conn_fd, entry->d_name, name_len) != 0) {
                break;
            }

            // 发送深度
            int depth_net = htonl(depth);
            if (write_full(conn_fd, &depth_net, sizeof(depth_net)) != 0) {
                break;
            }

            // 如果是目录，递归处理
            if (S_ISDIR(st.st_mode)) {
                send_directory_tree(conn_fd, full_path, depth + 1);
            }
        }
    }
    closedir(dir);
}

void sha256_string(const char *str, char *out_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)str, strlen(str), hash);
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out_hex + (i * 2), "%02x", hash[i]);
    out_hex[64] = 0;
}


struct server_options {
    size_t worker_count;
    size_t queue_capacity;
    size_t client_timeout_seconds;
};

struct signal_wait_context {
    sigset_t signals;
    int listen_fd;
};

static atomic_bool stop_requested = ATOMIC_VAR_INIT(false);

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s [--workers N] [--queue-capacity N] "
            "[--client-timeout SECONDS]\n",
            program);
}

static int parse_size(const char *text, size_t minimum, size_t maximum,
                      size_t *result) {
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return -1;
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < minimum || value > maximum) {
        return -1;
    }

    *result = (size_t)value;
    return 0;
}

static int parse_options(int argc, char **argv, struct server_options *options) {
    static const struct option long_options[] = {
        {"workers", required_argument, NULL, 'w'},
        {"queue-capacity", required_argument, NULL, 'q'},
        {"client-timeout", required_argument, NULL, 't'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option;

    options->worker_count = DEFAULT_WORKER_COUNT;
    options->queue_capacity = DEFAULT_QUEUE_CAPACITY;
    options->client_timeout_seconds = DEFAULT_CLIENT_TIMEOUT_SECONDS;

    while ((option = getopt_long(argc, argv, "w:q:t:h", long_options, NULL)) != -1) {
        switch (option) {
        case 'w':
            if (parse_size(optarg, 1, MAX_WORKER_COUNT,
                           &options->worker_count) != 0) {
                fprintf(stderr, "invalid worker count: %s\n", optarg);
                return -1;
            }
            break;
        case 'q':
            if (parse_size(optarg, 1, MAX_QUEUE_CAPACITY,
                           &options->queue_capacity) != 0) {
                fprintf(stderr, "invalid queue capacity: %s\n", optarg);
                return -1;
            }
            break;
        case 't':
            if (parse_size(optarg, 1, MAX_CLIENT_TIMEOUT_SECONDS,
                           &options->client_timeout_seconds) != 0) {
                fprintf(stderr, "invalid client timeout: %s\n", optarg);
                return -1;
            }
            break;
        case 'h':
            print_usage(stdout, argv[0]);
            return 1;
        default:
            print_usage(stderr, argv[0]);
            return -1;
        }
    }

    if (optind != argc) {
        print_usage(stderr, argv[0]);
        return -1;
    }
    return 0;
}

static int initialize_database(void) {
    static const char *schema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT UNIQUE, "
        "password TEXT);";
    sqlite3 *db = NULL;
    char *error_message = NULL;
    int result;

    result = sqlite3_open("netdisk.db", &db);
    if (result != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n",
                db != NULL ? sqlite3_errmsg(db) : "out of memory");
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }

    sqlite3_busy_timeout(db, 5000);
    result = sqlite3_exec(db, schema, NULL, NULL, &error_message);
    if (result != SQLITE_OK) {
        fprintf(stderr, "Can't initialize database: %s\n",
                error_message != NULL ? error_message : sqlite3_errmsg(db));
        sqlite3_free(error_message);
        sqlite3_close(db);
        return -1;
    }

    sqlite3_close(db);
    return 0;
}

static int create_listener(size_t queue_capacity) {
    struct sockaddr_in server_address = {0};
    int reuse_address = 1;
    int listen_fd;
    int backlog;

#ifdef __linux__
    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (listen_fd < 0) {
        perror("socket creation error");
        return -1;
    }

#ifndef __linux__
    if (fcntl(listen_fd, F_SETFD, FD_CLOEXEC) < 0) {
        perror("failed to set close-on-exec on listener");
        close(listen_fd);
        return -1;
    }
#endif

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) < 0) {
        perror("failed to enable SO_REUSEADDR");
        close(listen_fd);
        return -1;
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);
    if (bind(listen_fd, (struct sockaddr *)&server_address,
             sizeof(server_address)) < 0) {
        perror("bind error");
        close(listen_fd);
        return -1;
    }

    backlog = queue_capacity > INT_MAX ? INT_MAX : (int)queue_capacity;
    if (listen(listen_fd, backlog) < 0) {
        perror("listen error");
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}

static int accept_client(int listen_fd, struct sockaddr_in *client_address,
                         socklen_t *client_address_length) {
#ifdef __linux__
    return accept4(listen_fd, (struct sockaddr *)client_address,
                   client_address_length, SOCK_CLOEXEC);
#else
    int client_fd = accept(listen_fd, (struct sockaddr *)client_address,
                           client_address_length);

    if (client_fd >= 0 && fcntl(client_fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(client_fd);
        return -1;
    }
    return client_fd;
#endif
}

static int configure_client_socket(int client_fd, size_t timeout_seconds) {
    struct timeval timeout = {
        .tv_sec = (time_t)timeout_seconds,
        .tv_usec = 0,
    };

    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
        return -1;
    }
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
        return -1;
    }
    return 0;
}

static void close_rejected_client(int client_fd) {
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
}

static void *wait_for_shutdown_signal(void *argument) {
    struct signal_wait_context *context = argument;
    int signal_number;
    int error;

#ifdef __linux__
    pthread_setname_np(pthread_self(), "netdisk-signal");
#endif

    error = sigwait(&context->signals, &signal_number);
    if (error != 0) {
        fprintf(stderr, "sigwait failed: %s\n", strerror(error));
        atomic_store(&stop_requested, true);
        shutdown(context->listen_fd, SHUT_RDWR);
        return NULL;
    }

    fprintf(stderr, "received signal %d; stopping server\n", signal_number);
    atomic_store(&stop_requested, true);
    shutdown(context->listen_fd, SHUT_RDWR);
    return NULL;
}

int main(int argc, char **argv) {
    struct server_options options;
    struct thread_pool *pool = NULL;
    struct signal_wait_context signal_context;
    pthread_t signal_thread;
    sigset_t shutdown_signals;
    bool signal_thread_started = false;
    size_t rejected_connections = 0;
    int listen_fd = -1;
    int exit_status = EXIT_FAILURE;
    int result;

    result = parse_options(argc, argv, &options);
    if (result < 0) {
        return EXIT_FAILURE;
    }
    if (result > 0) {
        return EXIT_SUCCESS;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("failed to ignore SIGPIPE");
        return EXIT_FAILURE;
    }

    sigemptyset(&shutdown_signals);
    sigaddset(&shutdown_signals, SIGINT);
    sigaddset(&shutdown_signals, SIGTERM);
    result = pthread_sigmask(SIG_BLOCK, &shutdown_signals, NULL);
    if (result != 0) {
        fprintf(stderr, "failed to block shutdown signals: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    if (initialize_database() != 0) {
        return EXIT_FAILURE;
    }

    listen_fd = create_listener(options.queue_capacity);
    if (listen_fd < 0) {
        return EXIT_FAILURE;
    }

    result = thread_pool_create(&pool, options.worker_count,
                                options.queue_capacity, handle_client);
    if (result != 0) {
        fprintf(stderr, "failed to create thread pool: %s\n", strerror(result));
        goto cleanup;
    }

    signal_context.signals = shutdown_signals;
    signal_context.listen_fd = listen_fd;
    result = pthread_create(&signal_thread, NULL, wait_for_shutdown_signal,
                            &signal_context);
    if (result != 0) {
        fprintf(stderr, "failed to create signal thread: %s\n", strerror(result));
        goto cleanup;
    }
    signal_thread_started = true;

    printf("Server is listening on port %d (workers=%zu, queue=%zu, timeout=%zus)\n",
           PORT, options.worker_count, options.queue_capacity,
           options.client_timeout_seconds);
    fflush(stdout);
    exit_status = EXIT_SUCCESS;

    while (!atomic_load(&stop_requested)) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        int client_fd;

        client_fd = accept_client(listen_fd, &client_address,
                                  &client_address_length);
        if (client_fd < 0) {
            if (atomic_load(&stop_requested)) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("accept error");
            exit_status = EXIT_FAILURE;
            break;
        }

        if (configure_client_socket(client_fd,
                                    options.client_timeout_seconds) != 0) {
            perror("failed to configure client socket");
            close_rejected_client(client_fd);
            continue;
        }

        result = thread_pool_submit(pool, client_fd);
        if (result != 0) {
            close_rejected_client(client_fd);
            if (result == EAGAIN) {
                rejected_connections++;
                if (rejected_connections == 1 ||
                    rejected_connections % 100 == 0) {
                    fprintf(stderr,
                            "task queue full; rejected %zu connection(s)\n",
                            rejected_connections);
                }
            } else if (result != ECANCELED) {
                fprintf(stderr, "failed to enqueue client: %s\n",
                        strerror(result));
            }
        }
    }

cleanup:
    if (signal_thread_started) {
        if (!atomic_load(&stop_requested)) {
            result = pthread_kill(signal_thread, SIGTERM);
            if (result != 0) {
                fprintf(stderr, "failed to wake signal thread: %s\n",
                        strerror(result));
            }
        }
        pthread_join(signal_thread, NULL);
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    thread_pool_destroy(pool);

    if (rejected_connections > 0) {
        fprintf(stderr, "server rejected %zu connection(s) while the queue was full\n",
                rejected_connections);
    }
    fprintf(stderr, "server stopped\n");
    return exit_status;
}
