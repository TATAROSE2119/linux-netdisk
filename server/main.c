#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>// For inet_ntoa
#include <sys/socket.h>
#include <netinet/in.h>
#include <sqlite3.h>// For SQLite database operations
#include <sys/stat.h> // For mkdir
#include <sys/types.h> // For ssize_t
#include <openssl/sha.h>// For SHA-256 hashing
#include <pthread.h> // For multithreading

#define PORT 9000

void * handle_client(void *arg) {
    int conn_fd = *(int *)arg;
    free(arg); // Free the allocated memory for connection file descriptor
    //1.线程内自己打开数据库
    char buffer[1024];
    ssize_t n;
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
        int ulen = ntohl(ulen_net); // Convert to host byte order
        char username[ulen + 1]; // +1 for null terminator
        read(conn_fd, username, ulen);
        username[ulen] = '\0'; // Null-terminate the string

        //1.读取文件名长度
        int name_len_net;// Network byte order for file name length
        read(conn_fd,&name_len_net, sizeof(name_len_net));
        int name_len=ntohl(name_len_net); // Convert to host byte order

        //2.读取文件名
        char filename[name_len + 1]; // +1 for null terminator
        read(conn_fd, filename, name_len);
        filename[name_len] = '\0'; // Null-terminate the string

        //拼接专属用户目录
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s", username, filename);

        //3.用文件名创建文件
        FILE *fp=fopen(filepath,"wb"); // Open file for writing
        if(fp == NULL) {
            perror("File creation error❌");
            close(conn_fd); // Close the connection
            pthread_exit(NULL); // Continue to accept next connection
        }
        while((n=read(conn_fd,buffer,sizeof(buffer)))>0){
            fwrite(buffer,sizeof(char),n,fp);
        }
        printf("File upload successfully as %s ✅\n ",filename);
        fclose(fp);
    }
    else if(cmd=='D'){// 下载文件
        //1.读取用户名长度
        int ulen_net;
        read(conn_fd, &ulen_net, sizeof(ulen_net));
        int ulen = ntohl(ulen_net); // Convert to host byte order   
        char username[ulen + 1]; // +1 for null terminator
        read(conn_fd, username, ulen);
        username[ulen] = '\0'; // Null-terminate the string

        //1.读取文件名长度
        int name_len_net;
        read(conn_fd, &name_len_net, sizeof(name_len_net));
        int name_len = ntohl(name_len_net); // Convert to host byte order

        //2.读取文件名
        char filename[name_len + 1]; // +1 for null terminator
        read(conn_fd, filename, name_len);
        filename[name_len] = '\0'; // Null-terminate the string

        //拼接专属用户目录
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "netdisk_data/%s/%s", username, filename);

        //3.打开文件
        FILE *fp = fopen(filepath, "rb"); // Open file for reading
        char flag=1; // Flag to check if file exists
        if(fp == NULL) {
            flag = 0; // File not found
            write(conn_fd, &flag, sizeof(flag)); // Send file exists flag
            perror("File not found❌");
            close(conn_fd); // Close the connection
            pthread_exit(NULL); // Continue to accept next connection
        }
        write(conn_fd, &flag, sizeof(flag)); // Send file exists flag
        while((n = fread(buffer, sizeof(char), sizeof(buffer), fp)) > 0) {
            write(conn_fd, buffer, n); // Send file data to client
        }
        printf("File download successfully as %s ✅\n", filename);
        fclose(fp);
    }

    //3.关闭连接
    sqlite3_close(db); // Close the SQLite database connection
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


    int listen_fd, conn_fd;
    struct sockaddr_in server_addr, cli_addr;// Server and client address structures

    socklen_t cli_len = sizeof(cli_addr);// Length of client address structure

    char buffer[1024];
    ssize_t n;

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