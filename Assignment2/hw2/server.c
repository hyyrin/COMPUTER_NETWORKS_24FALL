#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
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

int read_content(int cli_socket, char *buffer){
    int cnt = 0;
    char c = '\0';
    while (c != '\n'){
        ssize_t recv_c = recv(cli_socket, &c, 1, 0);
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

char *encode(char *ipt) {
    char *msg = (char *)malloc(3 * strlen(ipt) + 1);
    size_t length = strlen(ipt);

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
char *strnstr(char *haystack, char *needle, size_t length)
{
    if (needle[0] == '\0') return (char *)haystack;
    

    while (length > 0){
        int i = 0;
        while (i < length &&
               haystack[i] == needle[i] &&
               needle[i] != '\0') {
        
            i++;
           
        }
        length--;
        if (needle[i] == '\0')
        {
            return (char *)haystack;
        }
        
        haystack++;
        
    }

    return NULL;
}
char *decode(char *ipt){
    size_t length = strlen(ipt);
    char *msg = (char *)malloc(length + 1);
    char tmp[3];
    int j = 0;
    for (int i = 0; i < length; i++) {
        char cur = ipt[i];
        if (cur == '%'){
            strcpy(tmp, "\0");
            i++;
            tmp[0] = ipt[i];
            i++;
            tmp[1] = ipt[i];
            tmp[2] = '\0';
            msg[j] = (char)strtol(tmp, NULL, 16);
            j++;
        }
        else{
            msg[j] = cur;
            j++;
        }
    }
    msg[j] = '\0';
    return msg;

}

//type1: post file, type2: post video
void read_post_file(int cli_socket, int content_length, char *boundary, int type, char *file_name_2){
    ssize_t cnt_read = 0;
    int bound_cnt = 0;
    char file_path[buf_size], file_name[buf_size];
    char tmp[buf_size];
    FILE *fp = NULL;
    while(content_length > 0){
        strcpy(tmp, "\0");
        cnt_read = recv(cli_socket, tmp, buf_size, 0);
        content_length -= cnt_read;
        if (bound_cnt == 0){
            char *pos = strstr(tmp, boundary);
            if (pos) {
                bound_cnt++;
                pos = strstr(pos, "Content-Disposition: form-data; name=\"upfile\"; filename=\"");
                if (pos) pos += strlen("Content-Disposition: form-data; name=\"upfile\"; filename=\"");
                
                int j = 0;
                for (;;j++){
                    if (pos[j] == '"') break;
                    file_name[j] = pos[j];
                }
                file_name[j] = '\0';
                 
                strcpy(file_name_2, file_name);
                if (type == 1){
                    sprintf(file_path, "./web/files/%s", file_name);
                }
                else sprintf(file_path, "./web/tmp/%s", file_name);
                fp = fopen(file_path, "wb");
                if (fp){
                    pos = strstr(pos, "\r\n\r\n");
                    for (int i = 0; i < 4; i++) pos++;
                    //char *pos1 = strstr(pos, boundary);
                    char *pos1 = NULL;
                    pos1 = strnstr(pos, boundary, buf_size - (pos - tmp));
                    if (pos1){
                        fwrite(pos, 1, pos1 - pos - 2, fp);
                        bound_cnt++;
                    }
                    else fwrite(pos, 1, cnt_read - (pos - tmp), fp);
                }
            }
        }
        else if (bound_cnt == 1){
            char *pos1 = strnstr(tmp, boundary, buf_size);
            //char *pos1 = strstr(tmp, boundary);
            if (pos1){
                bound_cnt++;
                fwrite(tmp, 1, pos1 - tmp - 2, fp);
            }
            else fwrite(tmp, 1, cnt_read, fp);
        }
    }
    
    if (fp) fclose(fp);
    
    
}


void change_html1(int cli_socket, int class, char *body, FILE *fp, ssize_t file_size){
    char directory[buf_size];
    char original[buf_size];
    char prefix[buf_size], suffix[buf_size];
    if (class == 1) {
        strcpy(directory, "./web/files");
        strcpy(original, "<?FILE_LIST?>");
    }
    else if (class == 2) {
        strcpy(directory, "./web/videos");
        strcpy(original, "<?VIDEO_LIST?>");
    }
    char *prefix_pos = strstr(body, original);
    char *suffix_pos = prefix_pos + strlen(original);
    strncpy(prefix, body, prefix_pos - body);
    strncpy(suffix, suffix_pos, body + file_size - suffix_pos);
    prefix[prefix_pos - body] = '\0';
    send(cli_socket, prefix, strlen(prefix), MSG_NOSIGNAL);
    suffix[body + file_size - suffix_pos] = '\0';
    DIR *dir;
    struct dirent *entry;
    char str[buf_size];
    dir = opendir(directory);
    if (dir){
        while ((entry = readdir(dir))){
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){
                char *encoded = encode(entry->d_name);
                if (class == 1) sprintf(str, "<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>\n", encoded, entry->d_name);
                else sprintf(str, "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", encoded, entry->d_name);
                send(cli_socket, str, strlen(str), MSG_NOSIGNAL);
                free(encoded);
            }
        }
        closedir(dir);
    }
    send(cli_socket, suffix, strlen(suffix), MSG_NOSIGNAL);
    return;
}

void change_html2(int cli_socket, char *body, char *file_name, ssize_t file_size){
    char original[buf_size];
    char prefix[buf_size], suffix[buf_size];
    strcpy(original, "<?VIDEO_NAME?>");
    char *prefix_pos = strstr(body, original);
    char *suffix_pos = prefix_pos + strlen(original);
    strncpy(prefix, body, prefix_pos - body);
    strncpy(suffix, suffix_pos, body + file_size - suffix_pos);
    prefix[prefix_pos - body] = '\0';
    send(cli_socket, prefix, strlen(prefix), MSG_NOSIGNAL);
    suffix[body + file_size - suffix_pos] = '\0';
    send(cli_socket, file_name, strlen(file_name), MSG_NOSIGNAL);
    return;
}

void change_html3(int cli_socket, char *body, char *url, ssize_t file_size){
    char original[buf_size];
    char prefix[buf_size], suffix[buf_size];
    strcpy(original, "<?MPD_PATH?>");
    char *prefix_pos = strstr(body, original);
    char *suffix_pos = prefix_pos + strlen(original);
    strncpy(prefix, body, prefix_pos - body);
    strncpy(suffix, suffix_pos, body + file_size - suffix_pos);
    prefix[prefix_pos - body] = '\0';
    send(cli_socket, prefix, strlen(prefix), MSG_NOSIGNAL);
    suffix[body + file_size - suffix_pos] = '\0';
    send(cli_socket, url, strlen(url), MSG_NOSIGNAL);
    send(cli_socket, suffix, strlen(suffix), MSG_NOSIGNAL);
    return;
}



//type0: text/plain (no file)
void send_response0(int cli_socket, char *type, char *content, char *code, char *msg){
    char header[buf_size], tmp[buf_size];
    ssize_t content_len = 0;
    content_len += strlen(content);
    sprintf(header, "HTTP/1.1 %s %s\r\nServer: CN2024Server/1.0\r\n", code, msg);
    if (strcmp(code, "405") == 0){
        sprintf(tmp, "Allow: %s\r\nContent-Length: 0\r\n\r\n", content);
        strcat(header, tmp);
        send(cli_socket, header, strlen(header), MSG_NOSIGNAL);
        return;
    }
    else if (strcmp(code, "401") == 0)
        strcat(header, "WWW-Authenticate: Basic realm=\"B11902999\"\r\n");
    sprintf(tmp, "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", type, content_len);
    strcat(tmp, content);
    strcat(header, tmp);
    //strcat(header, tmp);
    //strcat(header, content);
    send(cli_socket, header, strlen(header), MSG_NOSIGNAL);
    return;
}
//type1: just get file
void send_response1(int cli_socket, char *type, char *path, char *code, char *msg){
    //send header
    char header[buf_size], tmp[buf_size];
    FILE *fp = NULL;
    ssize_t content_len = 0;
    fp = fopen(path, "rb");
    if (!fp){
        send_response0(cli_socket, "text/plain", "Not Found", "404", "Not Found");
        return;
    }
    fseek(fp, 0L, SEEK_END);
    ssize_t reg = (ssize_t)ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    content_len += reg;
    sprintf(header, "HTTP/1.1 %s %s\r\nServer: CN2024Server/1.0\r\n", code, msg);
    sprintf(tmp, "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", type, content_len);
    strcat(header, tmp);
    send(cli_socket, header, strlen(header), MSG_NOSIGNAL);
    //send body
    char body[buf_size];
    ssize_t cnt_read = 0, total_cnt = 0;
    while (reg > total_cnt){
        cnt_read = fread(body, 1, sizeof(body), fp);
        if (cnt_read > 0){
            send(cli_socket, body, cnt_read, MSG_NOSIGNAL);
            total_cnt += cnt_read;
        }
    }
    close(fp);
}

//type2 modify list
void send_response2(int cli_socket, char *type, char *path, char *code, char *msg, int class){
    //send header
    char header[buf_size], tmp[buf_size];
    FILE *fp = NULL;
    ssize_t content_len = 0;
    fp = fopen(path, "rb");
    if (fp == NULL){
        send_response0(cli_socket, "text/plain", "Not Found", "404", "Not Found");
        return;
    }
    fseek(fp, 0L, SEEK_END);
    ssize_t reg = (ssize_t)ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    content_len += reg;
    //class1: file
    if (class == 1){
        char directory[buf_size];
        strcpy(directory, "./web/files");
        DIR *dir;
        struct dirent *entry;
        content_len -= strlen("<?FILE_LIST?>\n");
        int constant = strlen("<tr><td><a href=\"/api/file/\"></a></td></tr>\n");
        dir = opendir(directory);
        if (dir){
            while ((entry = readdir(dir))){
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){
                    char *encoded = encode(entry->d_name);
                    content_len += constant;
                    content_len += strlen(encoded);
                    content_len += strlen(entry->d_name);
                    free(encoded);
                }
            }
            closedir(dir);
            content_len++;
        }
    }
    else if (class == 2){ //class2: video
        char directory[buf_size];
        strcpy(directory, "./web/videos");
        DIR *dir;
        struct dirent *entry;
        content_len -= strlen("<?VIDEO_LIST?>\n");
        int constant = strlen("<tr><td><a href=\"/video/\"></a></td></tr>\n");
        dir = opendir(directory);
        if (dir){
            while ((entry = readdir(dir))){
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0){
                    char *encoded = encode(entry->d_name);
                    content_len += constant;
                    content_len += strlen(encoded);
                    content_len += strlen(entry->d_name);
                    free(encoded);
                }
            }
            closedir(dir);
            content_len++;
        }
    }
    sprintf(header, "HTTP/1.1 %s %s\r\nServer: CN2024Server/1.0\r\n", code, msg);
    sprintf(tmp, "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", type, content_len);
    strcat(header, tmp);
    send(cli_socket, header, strlen(header), MSG_NOSIGNAL);
    //send body
    char body[buf_size];
    fread(body, buf_size, 1, fp);
    change_html1(cli_socket, class, body, fp, reg);
    
}

void send_response3(int cli_socket, char *type, char *path, char *code, char *msg, char *file_name, char *url){
    char header[buf_size], tmp[buf_size];
    FILE *fp = NULL;
    ssize_t content_len = 0;
    fp = fopen(path, "rb");
    if (fp == NULL){
        send_response0(cli_socket, "text/plain", "Not Found", "404", "Not Found");
        return;
    }
    fseek(fp, 0L, SEEK_END);
    ssize_t reg = (ssize_t)ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    content_len += reg;
    content_len -= (strlen("<?VIDEO_NAME?>") + strlen("<?MPD_PATH?>"));
    content_len += (strlen(url) + strlen(file_name));
    sprintf(header, "HTTP/1.1 %s %s\r\nServer: CN2024Server/1.0\r\n", code, msg);
    sprintf(tmp, "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", type, content_len);
    strcat(header, tmp);
    send(cli_socket, header, strlen(header), MSG_NOSIGNAL);
    
    char body[buf_size];
    fread(body, buf_size, 1, fp);
    change_html2(cli_socket, body, file_name, reg);
    change_html3(cli_socket, body, url, reg);
}


int handle_request(int cli_socket){
    char buffer[buf_size], way[buf_size], route[buf_size];
    char authen[buf_size];
    char tmp[buf_size];
    char boundary[buf_size];
    int close = 0;
    int content_length = 0;
    int authen_ok = 0;
    char file_name[buf_size], file_path[buf_size];
    
    if (read_content(cli_socket, buffer) <= 0) return 1;
    sscanf(buffer, "%s %s", way, route);
    
    while (read_content(cli_socket, buffer) > 0){
        if (strcmp(buffer, "\r\n") == 0) break;
        else if (sscanf(buffer, "Content-Type: multipart/form-data; boundary=%s\r\n", tmp) == 1) sprintf(boundary, "--%s", tmp);
        else if (sscanf(buffer, "Authorization: Basic %s\r\n", authen) == 1){
            size_t len = 0;
            char content[buf_size];
            unsigned char *decoded = base64_decode(authen, strlen(authen), &len);
            FILE *secret = fopen("./secret", "rb");
            while (fgets(content, buf_size, secret)){
                if (strlen(content) <= 0) continue;
                if (content[strlen(content) - 1] == '\n') content[strlen(content) - 1] = '\0';
                if (strncmp((char *)decoded, content, strlen(content)) == 0) {
                    authen_ok = 1;
                    break;
                }
            }
            fclose(secret);
            free(decoded);
        }
        else if (sscanf(buffer, "Connection: %s\r\n", tmp) == 1){
            if (strncmp(tmp, "close", 5) == 0) close = 1;
            continue;
        }
        else if (sscanf(buffer, "Content-Length: %ld\r\n", &content_length) == 1) continue;
        else continue;
    }
    
    int content_len = content_length;
    if (strcmp(route, "/") == 0){
        //only get
        if (strncmp(way, "GET", 3) == 0)
            send_response1(cli_socket, "text/html", "./web/index.html", "200", "OK");
        else
            send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
   
    else if (strcmp(route, "/upload/file") == 0) {
           
        //read_requestbody(cli_socket, content_len, boundary, 3, file_name);
       
        if (strncmp(way, "GET", 3) == 0 && authen_ok){
             send_response1(cli_socket, "text/html", "./web/uploadf.html", "200", "OK");
        }
        else if (strncmp(way, "GET", 3) == 0 && authen_ok == 0)
            send_response0(cli_socket, "text/plain", "Unauthorized\n", "401", "Unauthorized");
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    else if (strcmp(route, "/upload/video") == 0) {
        if (strncmp(way, "GET", 3) == 0 && authen_ok){
             send_response1(cli_socket, "text/html", "./web/uploadv.html", "200", "OK");
        }
        else if (strncmp(way, "GET", 3) == 0 && !authen_ok)
            send_response0(cli_socket, "text/plain", "Unauthorized\n", "401", "Unauthorized");
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    else if (strcmp(route, "/file/") == 0) {
        
        if (strncmp(way, "GET", 3) == 0)
            send_response2(cli_socket, "text/html", "./web/listf.rhtml", "200", "OK", 1);
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    else if (strcmp(route, "/video/") == 0) {
         if (strncmp(way, "GET", 3) == 0)
            send_response2(cli_socket, "text/html", "./web/listv.rhtml", "200", "OK", 2);
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    else if (sscanf(route, "/api/file/%s", file_name) == 1){
        if (strncmp(way, "GET", 3) == 0){
            char *decoded = decode(file_name);
            sprintf(file_path, "./web/files/%s", decoded);
            send_response1(cli_socket, "text/plain", file_path, "200", "OK");
            free(decoded);
        }
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    else if (sscanf(route, "/api/video/%s", file_name) == 1) {
        if (strncmp(way, "GET", 3) == 0){
            char file_type[buf_size];
            char *decoded = decode(file_name);
            if (strstr(decoded, ".m4v"))
                strcpy(file_type, "video/mp4");
            else if (strstr(decoded, ".mp4"))
                strcpy(file_type, "video/mp4");
            else if (strstr(decoded, ".m4a"))
                strcpy(file_type, "audio/mp4");
            else if (strstr(decoded, ".m4s"))
                strcpy(file_type, "video/iso.segment");
            else if (strstr(decoded, ".mpd"))
                strcpy(file_type, "application/dash+xml");
            sprintf(file_path, "./web/videos/%s", decoded);
            send_response1(cli_socket, file_type, file_path, "200", "OK");
        }
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    
    
    else if (sscanf(route, "/video/%s", file_name) == 1) {
        if (strncmp(way, "GET", 3) == 0){
            char url[buf_size];
            char *decoded = decode(file_name);
            char new_name[buf_size];
            int j = 0;
            for (;;j++){
                if (decoded[j] == '.') break;
                new_name[j] = decoded[j];
            }
            new_name[j] = '\0';
            char *encoded = encode(new_name);
            strcpy(url, "\"/api/video/");
            strcat(url, encoded);
            strcat(url, "/dash.mpd\"");
            send_response3(cli_socket, "text/html", "./web/player.rhtml", "200", "OK", new_name, url);
            free(encoded);
        }
        else send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
    }
    
     else if (strcmp(route, "/api/file") == 0) {
       
        if (strncmp(way, "POST", 4) == 0) {
            if (authen_ok == 1) {
                read_post_file(cli_socket, content_length, boundary, 1, file_name);
                send_response0(cli_socket, "text/plain", "File Uploaded\n", "200", "OK");
            }
            else{
                ssize_t cnt_read = 0;
                char temp[buf_size];
                while (content_length > 0){
                    strcpy(tmp, "\0");
                    cnt_read = recv(cli_socket, temp, buf_size, 0);
                    content_length -= cnt_read;
                }
                send_response0(cli_socket, "text/plain", "Unauthorized\n", "401", "Unauthorized");
            }
        }
        else{
            send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
        }
    }
    
    else if (strcmp(route, "/api/video") == 0) {
        if (strncmp(way, "POST", 4) == 0){
            if (authen_ok == 0)  send_response0(cli_socket, "text/plain", "Unauthorized\n", "401", "Unauthorized");
            else{
                char file_path[buf_size];
                char directory[buf_size];
                //read_requestbody(cli_socket, content_length, boundary, 2, file_name);
                read_post_file(cli_socket, content_length, boundary, 2, file_name);
                //printf("name is %s\n", file_name);
                strcpy(file_path, "./web/tmp/");
                strcat(file_path, file_name);
                strcpy(directory, "./web/videos/");
                strcat(directory, file_name);
                //sprintf(file_path, "./web/tmp/%s", file_name);
                //sprintf(directory, "./web/videos/%s", file_name);
                send_response0(cli_socket, "text/plain", "File Uploaded\n", "200", "OK");
                char new_dir[buf_size];
                //printf("directory is %s\n", directory);
                int j = strlen(directory) - 1;
                while (directory[j] != '.') j--;
                for (int i = 0; i < j; i++) new_dir[i] = directory[i];
                new_dir[j] = '\0';
                //printf("new dir is %s\n", new_dir);
                char ffm[buf_size];
                int pid = fork();
                if (pid == 0) {
                   int pid2 = fork();
                   if (pid2 == 0) {
                       //sprintf(ffm, "mkdir -p \"%s\"", new_dir);
                       strcpy(ffm, "mkdir -p \"");
                       strcat(ffm, new_dir);
                       strcat(ffm, "\"");
                       system(ffm);
                       sprintf(ffm, "ffmpeg -re -i \"%s\" -c:a aac -c:v libx264 \
                           -map 0 -b:v:1 6M -s:v:1 1920x1080 -profile:v:1 high \
                           -map 0 -b:v:0 144k -s:v:0 256x144 -profile:v:0 baseline \
                           -bf 1 -keyint_min 120 -g 120 -sc_threshold 0 -b_strategy 0 \
                           -ar:a:1 22050 -use_timeline 1 -use_template 1 \
                           -adaptation_sets \"id=0,streams=v id=1,streams=a\" -f dash \
                           \"%s/dash.mpd\"",
                               file_path, new_dir);
                       system(ffm);
                       exit(0);
                   }
                   else exit(0);
               }
               else wait(NULL);
            }
        }
        else{
            send_response0(cli_socket, "text/plain", "GET", "405", "Method Not Allowed");
        }
    }
     
  
    else{
        send_response0(cli_socket, "text/plain", "Not Found", "404", "Not Found");
    }
    return close;

}

int main(int argc, char *argv[]) {

    if (argc != 2){
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    system("mkdir -p ./web/files");
    system("mkdir -p ./web/tmp");
    system("mkdir -p ./web/videos");
    
    rtv = 0;

    int listenfd, connfd;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) ERR_EXIT("socket()");
   
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)  ERR_EXIT("setsockopt()");
    
    struct sockaddr_in server_addr, client_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) ERR_EXIT("bind()");

    if (listen(listenfd, 3) < 0)  ERR_EXIT("listen()");

    int client_fd[200];
    for (int i = 0; i < 200; i++) client_fd[i] = 0;

    int cnt_client = 0;
    struct pollfd fds[201];
    fds[0].events = POLLIN;
    fds[0].fd = listenfd;
    int isAuthenticated[200] = {0}; //record client authentation
    //try to connect
    while (1){
        int ready = poll(fds, cnt_client + 1, -1);
        if (fds[0].revents & POLLIN){
            int clilen = sizeof(client_addr);
            int connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t *)&clilen);
            client_fd[cnt_client] = connfd;
            fds[cnt_client + 1].fd = connfd;
            fds[cnt_client + 1].events = POLLIN;
            cnt_client++;
        }

        //receive client request
        for (int i = 0; i < cnt_client; i++){
            if (fds[i + 1].revents & POLLIN){
                int rtv = handle_request(fds[i + 1].fd);
                if (rtv){
                    close(fds[i + 1].fd);
                    //close connection
                    memmove(client_fd + i, client_fd + i + 1, (cnt_client - i - 1) * sizeof(int));
                    client_fd[cnt_client - 1] = 0;
                    for (int j = i + 1; j <= cnt_client; j++) fds[j] = fds[j + 1];
                    cnt_client--;
                }
                /*
               else if (!com && rtv){
                   if (handleHttpRequest(fds[i + 1].fd)){
               
                       close(fds[i + 1].fd);
                       memmove(clients + i, clients + i + 1, (num_clients - i - 1) * sizeof(int));
                       memset(&clients[num_clients - 1], 0, sizeof(int));
                       for (int j = i + 1; j <= num_clients; ++j) {
                           fds[j] = fds[j + 1];
                       }
                       num_clients--;
                   }
                   rtv = 0;
               }
               */
            }
        }
    }
    return 0;
}


















