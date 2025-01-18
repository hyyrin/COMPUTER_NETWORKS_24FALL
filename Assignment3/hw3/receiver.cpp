#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <set>
#include <zlib.h>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>

#include "def.h"

using namespace std;

int sockFd, base = 1;
int turn;
int length = 0;
struct sockaddr_in recvAddr;
socklen_t recvAddrSz = sizeof(recvAddr);
segment recvSgmt {};
// sha256
unsigned int hash_len;
unsigned char hashmap[EVP_MAX_MD_SIZE];
EVP_MD_CTX *sha256;
set<int> unacked;
segment segBuffer[MAX_SEG_BUF_SIZE];

void setIP(char *dst, char *src)
{
    if (strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0 || strcmp(src, "localhost") == 0)
    {
        sscanf("127.0.0.1", "%s", dst);
    }
    else
    {
        sscanf(src, "%s", dst);
    }
    return;
}


void logEvent(segment sgmt){
    if (recvSgmt.head.checksum != crc32(0, (Bytef *)recvSgmt.data, recvSgmt.head.length)) printf("drop\tdata\t#%d\t(corrupted)\n", sgmt.head.seqNumber);
    else if(sgmt.head.fin) printf("recv\tfin\n");
    //in-order
    else if (sgmt.head.seqNumber == base) printf("recv\tdata\t#%d\t(in order)\n", sgmt.head.seqNumber);
    else if (recvSgmt.head.seqNumber > (turn + 1) * MAX_SEG_BUF_SIZE) printf("drop\tdata\t#%d\t(buffer overflow)\n", sgmt.head.seqNumber);
    else printf("recv\tdata\t#%d\t(out of order, sack-ed)\n", sgmt.head.seqNumber);
    
}

void flushBuffer(int fd){
    printf("flush\n");
    for (int i = turn * MAX_SEG_BUF_SIZE + 1; i < base; i++){
        int index = i % MAX_SEG_BUF_SIZE;
        segment &curr_seg = segBuffer[index]; 
        EVP_DigestUpdate(sha256, curr_seg.data, curr_seg.head.length);
        EVP_MD_CTX *tmp = EVP_MD_CTX_new();
        EVP_MD_CTX_copy_ex(tmp, sha256);
        EVP_DigestFinal_ex(tmp, hashmap, &hash_len);
        write(fd, curr_seg.data, curr_seg.head.length);
        length += curr_seg.head.length;
        EVP_MD_CTX_free(tmp);
    }

    const unsigned char *buf = static_cast<const unsigned char *>(hashmap);
    ostringstream reg {};
    for (int i = 0; i < hash_len; i++) reg << hex << setfill('0') << setw(2) << (unsigned int)buf[i];

    printf("sha256\t%d\t%s\n", length, reg.str().c_str());
}

// ./receiver <recv_ip> <recv_port> <agent_ip> <agent_port> <dst_filepath>
int main(int argc, char *argv[])
{
    // parse arguments
    if (argc != 6)
    {
        cerr << "Usage: " << argv[0] << " <recv_ip> <recv_port> <agent_ip> <agent_port> <dst_filepath>" << endl;
        exit(1);
    }

    int recv_port, agent_port;
    char recv_ip[50], agent_ip[50];

    // read argument
    setIP(recv_ip, argv[1]);
    sscanf(argv[2], "%d", &recv_port);

    setIP(agent_ip, argv[3]);
    sscanf(argv[4], "%d", &agent_port);

    char *filepath = argv[5];
    
    int fd = open(filepath, O_CREAT | O_APPEND | O_RDWR );
  

    // make socket related stuff
    sockFd = socket(PF_INET, SOCK_DGRAM, 0);

    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(agent_port);
    recvAddr.sin_addr.s_addr = inet_addr(agent_ip);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(recv_port);
    addr.sin_addr.s_addr = inet_addr(recv_ip);
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));
    bind(sockFd, (struct sockaddr *)&addr, sizeof(addr));

    //init sha256
    sha256 = EVP_MD_CTX_new();
    EVP_DigestInit_ex(sha256, EVP_sha256(), NULL);

    turn = 0;
    for (int i = 0; i < MAX_SEG_BUF_SIZE; i++) unacked.insert(turn * MAX_SEG_BUF_SIZE + i + 1);
    
    int fin = 0;
   
    while (1){
        int byte = recvfrom(sockFd, &recvSgmt, sizeof(recvSgmt), 0, (struct sockaddr *)&recvAddr, &recvAddrSz);
        if (byte <= 0) continue;
        logEvent(recvSgmt);
        //handle corrupted segment using check checksum
        if (recvSgmt.head.checksum != crc32(0, (Bytef *)recvSgmt.data, recvSgmt.head.length)){
            segment sgmt{};
            sgmt.head.ack = 1;
            sgmt.head.fin = 0;
            sgmt.head.syn = 0;
            sgmt.head.checksum = 0;
            sgmt.head.length = 0;
            sgmt.head.ackNumber = base - 1;
            sgmt.head.sackNumber = base -1 ;
            sendto(sockFd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)&recvAddr, sizeof(sockaddr));
            printf("send\tack\t#%d,\tsack\t#%d\n", base - 1, base - 1);
            continue;
        }
        
        if (recvSgmt.head.fin) fin = 1;
         //handle in-order segment
        if (recvSgmt.head.seqNumber == base){
            unacked.erase(base);
            memcpy(&segBuffer[base % MAX_SEG_BUF_SIZE], &recvSgmt, sizeof(segment));
            if (unacked.size() > 0){
                std::set<int>::iterator it = unacked.begin(); //choose the smallest
                base = *it;
            }
            else if (unacked.size() == 0) base = MAX_SEG_BUF_SIZE * (turn + 1);
            
            segment sgmt{};
            sgmt.head.ack = 1;
            sgmt.head.fin = fin;
            sgmt.head.syn = 0;
            sgmt.head.checksum = 0;
            sgmt.head.length = 0;
            sgmt.head.ackNumber = base - 1;
            sgmt.head.sackNumber = recvSgmt.head.seqNumber;
            sendto(sockFd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)&recvAddr, sizeof(sockaddr));
            if (fin) printf("send\tfinack\n");
            else printf("send\tack\t#%d,\tsack\t#%d\n", base - 1, recvSgmt.head.seqNumber);

            if (fin){
                flushBuffer(fd);
                const unsigned char *buf = static_cast<const unsigned char *>(hashmap);
                ostringstream reg {};
                for (int i = 0; i < hash_len; i++) reg << hex << setfill('0') << setw(2) << (unsigned int)buf[i];
                printf("finsha\t%s\n", reg.str().c_str());
                break;
            }
            else if (unacked.size() == 0){
                flushBuffer(fd);
                turn++;
                for (int i = 0; i < MAX_SEG_BUF_SIZE; i++) unacked.insert(turn * MAX_SEG_BUF_SIZE + i + 1);
            }
        }
        //handle out-of order segment
        else{
            if (recvSgmt.head.seqNumber > (turn + 1) * MAX_SEG_BUF_SIZE){
                segment sgmt{};
                sgmt.head.ack = 1;
                sgmt.head.fin = 0;
                sgmt.head.syn = 0;
                sgmt.head.checksum = 0;
                sgmt.head.length = 0;
                sgmt.head.ackNumber = base - 1;
                sgmt.head.sackNumber = base -1 ;
                sendto(sockFd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)&recvAddr, sizeof(sockaddr));
                printf("send\tack\t#%d,\tsack\t#%d\n", base - 1, base - 1);
            }
            else if (recvSgmt.head.seqNumber <= (turn + 1) * MAX_SEG_BUF_SIZE){
                unacked.erase(recvSgmt.head.seqNumber);
                memcpy(&segBuffer[recvSgmt.head.seqNumber % MAX_SEG_BUF_SIZE], &recvSgmt, sizeof(segment));
                segment sgmt{};
                sgmt.head.ack = 1;
                sgmt.head.fin = fin;
                sgmt.head.syn = 0;
                sgmt.head.checksum = 0;
                sgmt.head.length = 0;
                sgmt.head.ackNumber = base - 1;
                sgmt.head.sackNumber = recvSgmt.head.seqNumber;
                sendto(sockFd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)&recvAddr, sizeof(sockaddr));
                if (fin) printf("send\tfinack\n");
                else printf("send\tack\t#%d,\tsack\t#%d\n", base - 1, recvSgmt.head.seqNumber);
            }
        }
    }
}
