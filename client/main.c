#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9000
#define server_IP "127.0.0.1"

void upload_file(const char *filename) {
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

    FILE *fp = fopen(filename, "rb"); // Open file for reading
    if(!fp){
        perror("File open error❌");
        close(sockfd);
        return;
    }
    while((n=fread(buffer,1,sizeof(buffer),fp))>0 ){
        write(sockfd, buffer, n); // Send file data to server
    }
    fclose(fp);
    close(sockfd);
    printf("File uploaded successfully✅.\n");
}

void download_file(const char *filename) {
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

    FILE *fp = fopen(filename, "wb"); // Open file for writing
    if(!fp){
        perror("File open error❌");
        close(sockfd);
        return;
    }
    while((n=read(sockfd, buffer, sizeof(buffer)))>0){
        fwrite(buffer, sizeof(char), n, fp); // Write data to file
    }
    fclose(fp);
    close(sockfd);
    printf("File downloaded successfully✅.\n");
}

int main(int argc, char *argv[]) {
    if(argc!=3){// Check for correct number of arguments
        printf("🔔Uasage: \n %s up <local_file>\n %s down <local_file>\n", argv[0], argv[0]);
        return 1;
    }
    if(strcmp(argv[1], "up") != 0 && strcmp(argv[1], "down") != 0) {
        printf("🔔Invalid command. Use 'up' to upload or 'down' to download.\n");
        return 1;
    }
    if(strcmp(argv[1], "up") == 0){
        upload_file(argv[2]);
    }else if(strcmp(argv[1], "down") == 0){
        download_file(argv[2]);
    }
    return 0;
}