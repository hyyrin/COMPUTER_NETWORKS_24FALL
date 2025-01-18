#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>
#include<fcntl.h>
#include<poll.h>
#include <errno.h>
#include "utils/base64.h"

#define buf_size 4096
#define BUFFER_SIZE 4096


#define ERR_EXIT(a) \
    {               \
        perror(a);  \
        exit(1);    \
    }
int rtv;
int serverPort, sockfd;
char *serverIp, *encoded = NULL;
char boundary[] = "------WebKitFormBoundarytcHXAhuOytmBIB0B";
struct sockaddr_in addr;

void to_connect() {
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) ERR_EXIT("socket()")
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(serverIp);
    addr.sin_port = htons(serverPort);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) ERR_EXIT("connect()");
    
}

char *encode(char *ipt) {
    size_t length = strlen(ipt);
    char *msg = (char *)malloc(3 * length + 1);

    int j = 0;
    for (int i = 0; i < length; i++) {
        char cur = ipt[i];
        //if (check(cur)) msg[j++] = cur;
        if (isalnum(cur)) msg[j++] = cur;
        else if (cur == '_') msg[j++] = cur;
        else if (cur == '.') msg[j++] = cur;
        else if (cur == '~') msg[j++] = cur;
        else if (cur == '-') msg[j++] = cur;
        else{
            snprintf(&msg[j], 4, "%%%02X", (unsigned char)cur);
            for (int k = 0; k < 3; k++) j++;
        }
    }
    
    msg[j] = '\0';

    return msg;
}

int read_content(int socket, char *buffer){
    int cnt = 0;
    char c = '\0';
    while (c != '\n'){
        ssize_t recv_c = recv(socket, &c, 1, 0);
        if (recv_c == 0) break;
        else if (recv_c > 0){
            buffer[cnt] = c;
            cnt++;
        }
        else return -1;
    }
    buffer[cnt] = '\0';
    return cnt;
}

int checkCredentials(char *credentials) {
    FILE *file = fopen("./secret", "rb");
    char line[buf_size];
    while (fgets(line, buf_size, file)) {
        ssize_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') line[length - 1] = '\0';
        if (strcmp(line, credentials) == 0) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}


int request_secret (char *credentials){
    char request[buf_size];
    sprintf(request, "secret_%s\n", credentials);

   
    char buffer[buf_size];
   /*
    ssize_t bytes_read = read_byline(sockfd, buffer, BUFFER_SIZE);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        if (strncmp(buffer, "true", 4) == 0) {
            //printf("> ");
            //fflush(stdout);
            return 1;
        }
        return 0;
    }
    */
    
    return 0;
}
int cal_body_len(ssize_t file_size, char *file_name){
    int total = 0;
    total += 3*strlen(boundary);
    total += 22;
    total += (file_size + strlen(file_name));
    total += (strlen("Content-Disposition: form-data; name=\"upfile\"; filename=\"\"\r\n")+strlen("Content-Type: application/octet-stream\r\n")+strlen("Content-Disposition: form-data; name=\"submit\"\r\n"));
    return total;
}
void send_req(char *end_point, char *type, char *tag, ssize_t body_len, FILE *fp, char *file_name, ssize_t file_size){
    //header
    char header[buf_size], tmp[buf_size], tmp1[buf_size], tmp2[buf_size];
    strcpy(header, "\0");
    strcpy(tmp, "\0");
    strcpy(tmp1, "\0");
    strcpy(tmp2, "\0");
    sprintf(header, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: CN2024Client/1.0\r\nConnection: keep-alive\r\n", type, end_point, serverIp, serverPort);
    if (type){
         sprintf(tmp, "Content-Type: %s; boundary=%s\r\n", tag, boundary + 2);
    }
    if (encoded){
         sprintf(tmp1, "Authorization: Basic %s\r\n", encoded);
    }
    if (body_len >= 0){
        sprintf(tmp2, "Content-Length: %ld\r\n", body_len);
    }
    strcat(tmp, tmp2);
    strcat(tmp, tmp1);
    strcat(header, tmp);
    strcat(header, "\r\n");
    send(sockfd, header, strlen(header), 0);
    //send body
    if (file_size == 0) return;

    char body[buf_size];
    strcpy(body, "\0");
    strcpy(tmp, "\0");
    sprintf(tmp, "Content-Disposition: form-data; name=\"upfile\"; filename=\"%s\"", file_name);
    strcpy(tmp1, "Content-Type: application/octet-stream");
    strcpy(tmp2, "Content-Disposition: form-data; name=\"submit\"");
    strcpy(body, boundary);
    strcat(body, "\r\n");
    strcat(body, tmp);
    strcat(body, "\r\n");
    strcat(body, tmp1);
    strcat(body, "\r\n\r\n");
    send(sockfd, body, strlen(body), 0);
    strcpy(body, "\0");
    int bytes_read = 0;
    while ((bytes_read = fread(body, sizeof(char), buf_size, fp)) > 0) {
        send(sockfd, body, bytes_read, 0);
        strcpy(body, "\0");
    }
    sprintf(body, "\r\n%s\r\n%s\r\n\r\n", boundary, tmp2);
    strcat(body, "Upload\r\n");
    strcat(body, boundary);
    strcat(body, "--\r\n");
    send(sockfd, body, strlen(body), 0);
    return;
}

void rec_response(char *command, char *file_name) {
    ssize_t content_len = 0;
    int reconnect = 0;
    char buffer[buf_size], tmp[buf_size];
    char status_code[buf_size], msg[buf_size];
    
    while (read_content(sockfd, buffer) > 0) {
        if (strcmp(buffer, "\r\n") == 0) break;
        else if (sscanf(buffer, "Content-Length: %zd\r\n", &content_len) == 1) continue;
        else if (sscanf(buffer, "HTTP/1.1 %s\r\n", status_code) == 1) continue;
        else if (sscanf(buffer, "Connection: %s\r\n", tmp) == 1){
            if ((strncmp(tmp, "close", 5) == 0)){
                reconnect = 1;
            }
        }
    }
    
   
    //receive get file
    
    FILE *fp = NULL;
    if (file_name){
        char file_path[buf_size];
        strcpy(file_path, "\0");
        strcpy(file_path, "./files/");
        strcat(file_path, file_name);
        fp = fopen(file_path, "wb");
    }
    char temp[buf_size];
    ssize_t cnt_read = 0;
    while (content_len > 0){
        strcpy(temp, "\0");
        cnt_read = recv(sockfd, temp, buf_size, 0);
        content_len -= cnt_read;
        if (fp) fwrite(temp, sizeof(char), cnt_read, fp);
    }
    if (fp) fclose(fp);
        
    

    if (strcmp(status_code, "200") == 0 && rtv == 0){
        printf("> ");
        rtv++;
    }
    else if (encoded == NULL && (strncmp(command, "put", 3) == 0 || strncmp(command, "putv", 4) == 0))
        fprintf(stderr, "Command failed.\n");

    else if (strcmp(status_code, "200") == 0 && rtv > 0)
        printf("Command succeeded.\n");
    else if (strcmp(status_code, "401") == 0 || strcmp(status_code, "400") == 0) {
        fprintf(stderr, "Invalid user or wrong password.\n");
        exit(EXIT_FAILURE);
    }
    else
        fprintf(stderr, "Command failed.\n");
    if (reconnect) {
        close(sockfd);
        to_connect();
    }


}

void handle_input(){
    char ipt[buf_size];
    char buffer[buf_size];
    while(fgets(ipt, buf_size, stdin)){
        int j = 0;
        while (ipt[j] != '\n' && j < strlen(ipt)) j++;
        ipt[j] = '\0';
        char command[buf_size];
        strcpy(command, "\0");
        char file_name[buf_size];
        strcpy(file_name, "\0");
        sscanf(ipt, "%s %79[^\n]", command, file_name);
        //printf("%s %s\n", command, file_name);
        //printf("%s %d\n",file_name, strlen(file_name));
        if (strncmp(command, "put", 3) == 0){
            if (strlen(file_name) <= 0)  {
                fprintf(stderr, "Usage: %s [file]\n", command);
                //continue;
            }
            else if (strlen(file_name) > 0 && access(file_name, F_OK) != -1){
                FILE *fp = NULL;
                fp = fopen(file_name, "rb");
                if (fp == NULL) {
                    fprintf(stderr, "Command failed.\n");
                    continue;
                }
                fseek(fp, 0L, SEEK_END);
                ssize_t reg = (ssize_t)ftell(fp);
                fseek(fp, 0L, SEEK_SET);
                int body_len = cal_body_len(reg, file_name);
                send_req("/api/file", "POST", "multipart/form-data", body_len, fp, file_name, reg);
                rec_response("put", NULL);
                fclose(fp);
            }
            else if (strlen(file_name) > 0 && access(file_name, F_OK) == -1){
                 fprintf(stderr, "Command failed.\n");
                 //continue;
            }
        }
        else if (strncmp(command, "putv", 4) == 0){
            if (strlen(file_name) <= 0)  {
                fprintf(stderr, "Usage: %s [file]\n", command);
                //continue;
            }
            else if (strlen(file_name) > 0 && access(file_name, F_OK) != -1){
                FILE *fp = NULL;
                fp = fopen(file_name, "rb");
                if (fp == NULL) {
                    fprintf(stderr, "Command failed.\n");
                    continue;
                }
                fseek(fp, 0L, SEEK_END);
                ssize_t reg = (ssize_t)ftell(fp);
                fseek(fp, 0L, SEEK_SET);
                int body_len = cal_body_len(reg, file_name);
                send_req("/api/video", "POST", "multipart/form-data", body_len, fp, file_name, reg);
                rec_response("putv", NULL);
                fclose(fp);
            }
            else if (strlen(file_name) > 0 && access(file_name, F_OK) == -1){
                 fprintf(stderr, "Command failed.\n");
                 //continue;
            }
        }
        else if (strncmp(command, "get", 3) == 0){
            if (strlen(file_name) <= 0){
                fprintf(stderr, "Usage: %s [file]\n", command);
                //continue;
            }
            else{
                char *encoded = encode(file_name);
                strcpy(buffer, "/api/file/");
                strcat(buffer, encoded);
                send_req(buffer, "GET", NULL, -100, NULL, NULL, 0);
                free(encoded);
                rec_response("get", file_name);
            }
        }
        else if (strncmp(command, "quit", 4) == 0){
            printf("Bye.\n");
            return;
        }
        else fprintf(stderr, "Command Not Found.\n");
        printf("> ");
    }
}

int main(int argc, char *argv[]) {
    system("mkdir -p ./files");
    if (argc > 4 || argc < 3){
        fprintf(stderr, "Usage: %s [host] [port] [username:password]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    serverPort = atoi(argv[2]);
    char hostname[buf_size];
    strcpy(hostname, argv[1]);
    size_t encodeLength;
    rtv = 0;
   
    // change domain name to IP address
    struct hostent *host;
    if ((host = gethostbyname(hostname)) == NULL) {
        ERR_EXIT("gethostbyname()");
    }
    serverIp = inet_ntoa(*(struct in_addr *)host->h_addr_list[0]);

    setbuf(stdout, NULL);

    to_connect();
   
    if (argc == 4){
        encoded = base64_encode(argv[3], strlen(argv[3]), &encodeLength);
       
        //send_header("GET", "/upload/file", NULL, -1, NULL, NULL, 0);
        send_req("/upload/file", "GET", NULL, -100, NULL, NULL, 0);
        rec_response("get", NULL);
        
    }
    else if (argc == 3){
        printf("> ");
        rtv = 1;
    }
    handle_input();

    close(sockfd);

    return 0;
}


