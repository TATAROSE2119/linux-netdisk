#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

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

void upload_file(const char *filename) {
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
    char cmd ='U'; // Command for upload
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

    //3.发送文件内容
    FILE *fp = fopen(filename, "rb"); // Open file for reading
    if(!fp){
        perror("File open error❌");
        close(sockfd);  
        return;
    }
    while((n=fread(buffer, sizeof(char), sizeof(buffer), fp))>0){
        write(sockfd, buffer, n); // Send data to server
    }
    fclose(fp);
    close(sockfd);
    printf("File uploaded successfully✅.\n");

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

int main() {
    char cmd[128];

    printf("欢迎使用 C 语言网盘客户端！输入 help 查看命令。\n");

    while (1) {
        printf("\n1. 注册\n2. 登录\n3. 上传文件\n4. 下载文件\n5. 文件列表\n6. 删除文件\n0. 退出\n选择: ");
        int choice; scanf("%d", &choice); getchar();
        if(choice == 0) break;
        else if(choice == 1) {
            char user[64], pass[64];
            scanf("%s%s", user, pass);
            register_user(user, pass);
        }
        else if(choice == 2) {
            char user[64], pass[64];
            scanf("%s%s", user, pass);
            if (login_user(user, pass)){
                strcpy(g_username, user);
            }
        }
        else if(choice == 3) {
            char filename[256];
            scanf("%s", filename);
            if (strlen(g_username) == 0)
                printf("请先登录！\n");
            else
                upload_file(filename);
        }
        else if(choice == 4) {
            char filename[256];
            scanf("%s", filename);
            if (strlen(g_username) == 0)
                printf("请先登录！\n");
            else
                download_file(filename);
        }
        else if(choice == 5) {
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
        } else if(choice == 6) {
            if(strlen(g_username) == 0) {
                printf("请先登录！\n");
            } else {
                char filename[256];
                printf("输入要删除的文件名: ");
                fgets(filename, sizeof(filename), stdin);
                filename[strcspn(filename, "\n")] = 0;
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
                send_delete_file(sockfd, g_username, filename);
                close(sockfd);
            }
        }
        // 清理输入缓冲
        int c; while ((c = getchar()) != '\n' && c != EOF);
    }
    printf("Bye!\n");
    return 0;
}