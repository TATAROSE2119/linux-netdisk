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

#define PORT 9000
#define server_IP "192.168.50.153"

char g_username[64]={0}; // Global variable to store username

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

void upload_file(const char *filepath) {
    int sockfd = connect_to_server();
    if (sockfd < 0) return;

    // 从完整路径中提取文件名 (basename)
    const char *filename = strrchr(filepath, '/');
    if (filename == NULL) {
        filename = filepath; // 路径中没有'/'，本身就是文件名
    } else {
        filename++; // 指向'/'后面的字符
    }

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        printf("❌ 错误: 无法打开本地文件 '%s'\n", filepath);
        close(sockfd);
        return;
    }

    char cmd = 'U';
    write(sockfd, &cmd, sizeof(cmd));

    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen);

    int name_len = strlen(filename);
    int name_len_net = htonl(name_len);
    write(sockfd, &name_len_net, sizeof(name_len_net));
    write(sockfd, filename, name_len);

    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (write(sockfd, buffer, n) < 0) {
            perror("Socket write error");
            break;
        }
    }

    fclose(fp);
    close(sockfd);
    printf("✅ 文件 '%s' 作为 '%s' 上传成功。\n", filepath, filename);
}

void download_file(const char *filename) {
    if(strlen(g_username) == 0) {
        printf("You must login first!❌.\n");
        return;
    }

    int sockfd;
    struct sockaddr_in server_addr;// Server address structure
    char buffer[1024];
    ssize_t n;

    sockfd=socket(AF_INET, SOCK_STREAM, 0);// Create a socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT); // Set port number
    inet_pton(AF_INET, server_IP, &server_addr.sin_addr); // Convert IP

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed ❌");
        close(sockfd);
        return;
    }
    char cmd ='D'; // Command for download
    write(sockfd, &cmd, sizeof(cmd)); // Send command to server

    //1.发送用户名长度
    int ulen = strlen(g_username);
    int ulen_net = htonl(ulen); // Convert to network byte order
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, g_username, ulen); // Send username

    //1.发送文件名长度
    int name_len = strlen(filename);
    int name_len_net = htonl(name_len); // Convert to network byte order
    write(sockfd, &name_len_net, sizeof(name_len_net));

    //2.发送文件名
    write(sockfd, filename, name_len);

    //3.接收文件内容
    char flag;
    if(read(sockfd, &flag, 1) != 1) { // 先接收服务器“存在标志”
        printf("Network error!\n");
        close(sockfd);
        return;
    }
    if(flag == 0) {
        printf("File not found on server❌.\n");
        close(sockfd);
        return; // 提前返回，避免创建文件
    } 
    FILE *fp = fopen(filename, "wb"); // Open file for writing
    if(fp == NULL) {
        perror("File creation error❌");
        close(sockfd);
        return;
    }
    while((n = read(sockfd, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, sizeof(char), n, fp); // Write data to file
    }
    fclose(fp);
    close(sockfd);
    printf("File downloaded successfully✅.\n");

}

void send_list_files(int sockfd, const char* username) {
    char cmd = 'F';
    write(sockfd, &cmd, sizeof(cmd));
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sockfd, &ulen_net, sizeof(ulen_net));
    write(sockfd, username, ulen);
    char res;
    read(sockfd, &res, sizeof(res));
    if(res == 0) {
        printf("[文件列表] 用户目录不存在或读取失败\n");
        return;
    }
    int file_count_net;
    read(sockfd, &file_count_net, sizeof(file_count_net));
    int file_count = ntohl(file_count_net);
    printf("[文件列表] 共 %d 个文件：\n", file_count);
    for(int i=0; i<file_count; ++i) {
        int name_len_net;
        read(sockfd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net);
        char filename[name_len+1];
        read(sockfd, filename, name_len);
        filename[name_len] = '\0';
        printf("  %d. %s\n", i+1, filename);
    }
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

// 命令列表
char* commands[] = {
    "register", "login", "upload", "download", "list", "delete", "exit", "help", NULL
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
    // 如果这是行中的第一个词，则进行命令补全
    int i = 0;
    while (i < start && isspace(rl_line_buffer[i])) {
        i++;
    }

    if (i == start) {
        return rl_completion_matches(text, command_generator);
    }

    // 否则，返回NULL，让readline使用默认的文件名补全
    return NULL;
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
    // 将rl_attempted_completion_function指向新的智能补全函数
    rl_attempted_completion_function = command_completion; 

    // main 函数中的命令处理循环保持不变，它已经可以正确传递参数了
    char* line;
    printf("欢迎使用 C 语言网盘客户端！输入 help 查看命令，按 Tab 自动补全。\n");

    while ((line = readline("Netdisk> ")) != NULL) {
        if (strlen(line) > 0) {
            add_history(line);
        }

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
            printf("  upload <local_file>     - 上传文件\n");
            printf("  download <remote_file>  - 下载文件\n");
            printf("  list                      - 查看文件列表\n");
            printf("  delete <file1> [file2..] - 删除一个或多个文件\n");
            printf("  exit                      - 退出程序\n");
        } else if (strcmp(cmd, "delete") == 0) {
            if (strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char* filename = strtok(NULL, " \t\n");
                if (!filename) {
                    printf("Usage: delete <file1> [file2] ...\n");
                }
                while (filename) {
                    int sockfd = connect_to_server();
                    if (sockfd >= 0) {
                        send_delete_file(sockfd, g_username, filename);
                        close(sockfd);
                    }
                    filename = strtok(NULL, " \t\n");
                }
            }
        } 
        else {
             // 保持原有逻辑，但用 strtok 获取参数
             char* arg1 = strtok(NULL, " \t\n");
             char* arg2 = strtok(NULL, " \t\n");
            if (strcmp(cmd, "register") == 0) {
                if (strlen(arg1) > 0 && strlen(arg2) > 0) {
                    register_user(arg1, arg2);
                } else {
                    printf("Usage: register <username> <password>\n");
                }
            }
            else if (strcmp(cmd, "login") == 0) {
                if (strlen(arg1) > 0 && strlen(arg2) > 0) {
                    
                    login_user(arg1, arg2);
                } else {
                    printf("Usage: login <username> <password>\n");
                }
            }
            else if (strcmp(cmd, "upload") == 0) {
                if (strlen(g_username) == 0) {
                    printf("请先登录！\n");
                } else {
                    if (strlen(arg1) > 0) {
                        upload_file(arg1);
                    } else {
                        printf("Usage: upload <filename>\n");
                    }
                }
            }
            else if (strcmp(cmd, "download") == 0) {
                if (strlen(g_username) == 0) {
                    printf("请先登录！\n");
                } else {
                    if (strlen(arg1) > 0) {
                        download_file(arg1);
                    } else {
                        printf("Usage: download <filename>\n");
                    }
                }
            }
            else if (strcmp(cmd, "list") == 0) {
                if(strlen(g_username) == 0) {
                    printf("请先登录！\n");
                } else {
                    int sockfd;
                    struct sockaddr_in server_addr;
                    sockfd = socket(AF_INET, SOCK_STREAM, 0);
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(PORT);
                    inet_pton(AF_INET, server_IP, &server_addr.sin_addr);
                    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        perror("Connection failed ❌");
                        close(sockfd);
                        return;
                    }
                    send_list_files(sockfd, g_username);
                    close(sockfd);
                }
            }
            else if (strcmp(cmd, "exit") == 0) {
                break;
            }
        }


        free(line);
        free(line_copy);
    }
    printf("Bye!\n");
    return 0;
}