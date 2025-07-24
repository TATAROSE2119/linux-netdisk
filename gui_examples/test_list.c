#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
    
    printf("连接成功，测试L命令\n");
    
    // 发送L命令
    char cmd = 'L';
    write(sock, &cmd, sizeof(cmd));
    
    // 发送用户名
    char *username = "py";
    int ulen = strlen(username);
    int ulen_net = htonl(ulen);
    write(sock, &ulen_net, sizeof(ulen_net));
    write(sock, username, ulen);
    printf("发送用户名: %s (长度: %d)\n", username, ulen);
    
    // 发送目标目录（空字符串）
    int dir_len = 0;
    int dir_len_net = htonl(dir_len);
    write(sock, &dir_len_net, sizeof(dir_len_net));
    printf("发送目录长度: %d\n", dir_len);
    
    // 接收响应
    char result;
    if (read(sock, &result, sizeof(result)) != sizeof(result)) {
        printf("读取响应失败\n");
        close(sock);
        return 1;
    }
    
    printf("服务器响应: %d\n", result);

    if (result == 0) {
        printf("可能的原因:\n");
        printf("1. 目录不存在: netdisk_data/py\n");
        printf("2. C服务器工作目录不正确\n");
        printf("3. 权限问题\n");

        // 让我们检查当前工作目录
        printf("\n检查目录是否存在:\n");
        system("ls -la ../netdisk_data/py/ 2>/dev/null && echo '目录存在' || echo '目录不存在'");

        close(sock);
        return 1;
    }

    if (result == 1) {
        // 接收条目数量
        int count_net;
        if (read(sock, &count_net, sizeof(count_net)) != sizeof(count_net)) {
            printf("读取条目数量失败\n");
            close(sock);
            return 1;
        }
        
        int count = ntohl(count_net);
        printf("条目数量: %d\n", count);
        
        // 接收每个条目
        for (int i = 0; i < count; i++) {
            printf("处理第 %d 个条目:\n", i + 1);
            
            // 接收类型
            char type;
            if (read(sock, &type, sizeof(type)) != sizeof(type)) {
                printf("读取类型失败\n");
                break;
            }
            printf("  类型: %d (%s)\n", type, type == 2 ? "目录" : "文件");
            
            // 接收名称长度
            int name_len_net;
            if (read(sock, &name_len_net, sizeof(name_len_net)) != sizeof(name_len_net)) {
                printf("读取名称长度失败\n");
                break;
            }
            int name_len = ntohl(name_len_net);
            printf("  名称长度: %d\n", name_len);
            
            // 接收名称
            char name[name_len + 1];
            if (read(sock, name, name_len) != name_len) {
                printf("读取名称失败\n");
                break;
            }
            name[name_len] = '\0';
            printf("  名称: %s\n", name);
            
            // 接收大小
            int64_t size;
            if (read(sock, &size, sizeof(size)) != sizeof(size)) {
                printf("读取大小失败\n");
                break;
            }
            printf("  大小: %ld\n", be64toh(size));
            
            // 接收修改时间
            int64_t mtime;
            if (read(sock, &mtime, sizeof(mtime)) != sizeof(mtime)) {
                printf("读取修改时间失败\n");
                break;
            }
            printf("  修改时间: %ld\n", be64toh(mtime));
        }
    } else {
        printf("服务器返回失败\n");
    }
    
    close(sock);
    return 0;
}
