#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include <unordered_map>
#include<fcntl.h>
#include <zlib.h>
#include <set>
#include<cmath>

#include "def.h"

using namespace std;

int state = 0;
int sockFd;
int dupACK = 0, base = 0, dataNum = 0;
int threshold = 16;
double cwnd = 1.0;
struct sockaddr_in recvAddr;
socklen_t recvAddrSz = sizeof(recvAddr);
segment *sgmt;
clock_t timer;
set<int> transmitQueue;
set<int> sendButNotAck;
set<int> haveSend;
int lastInQueue = 0;

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

void logEvent(const char *type, int seqNum, int winSize, int thresh) {
    if (strcmp(type, "send") == 0) {
        if(haveSend.find(seqNum) != haveSend.end()) printf("resnd\tdata\t#%d,\twinSize = %d\n", seqNum, winSize);
        else printf("send\tdata\t#%d,\twinSize = %d\n", seqNum, winSize);
    }
    else if (strcmp(type, "recv") == 0) {
        printf("recv\tack\t#%d,\tsack\t#%d\n", seqNum, thresh);
    }
    else if (strcmp(type, "timeout") == 0) {
        printf("time\tout,\tthreshold = %d,\twinSize = %d\n", thresh, winSize);
    }
}


void transmitNew(){
    int to_send = int(cwnd - transmitQueue.size());
    while (to_send-- > 0){
        int seqNum;
        if (sendButNotAck.size() > 0){
            std::set<int>::iterator it = sendButNotAck.begin(); //increasing order
            seqNum = *it;
            logEvent("send", seqNum, int(cwnd), 0);
            transmitQueue.insert(seqNum);
            sendButNotAck.erase(seqNum);
            int byte = sendto(sockFd, &sgmt[seqNum], sizeof(sgmt[seqNum]), 0, (struct sockaddr *)&recvAddr, sizeof(recvAddr));
        }
        else if (sendButNotAck.size() == 0){
            if (lastInQueue == dataNum) break;
            seqNum = ++lastInQueue;
            logEvent("send", seqNum, int(cwnd), 0);
            haveSend.insert(seqNum);
            transmitQueue.insert(seqNum);
            int byte = sendto(sockFd, &sgmt[seqNum], sizeof(sgmt[seqNum]), 0, (struct sockaddr *)&recvAddr, sizeof(recvAddr));
            //printf("byte is %d\n", byte);
        }
    }
}



void transmitMissing(int seqNum){
    logEvent("send", seqNum, int(cwnd), 0);
    transmitQueue.insert(seqNum);
    sendButNotAck.erase(seqNum);
    sendto(sockFd, &sgmt[seqNum], sizeof(sgmt[seqNum]), 0, (struct sockaddr *)&recvAddr, sizeof(recvAddr));
}

// ./sender <send_ip> <send_port> <agent_ip> <agent_port> <src_filepath>
int main(int argc, char *argv[])
{
    // parse arguments
    if (argc != 6)
    {
        cerr << "Usage: " << argv[0] << " <send_ip> <send_port> <agent_ip> <agent_port> <src_filepath>" << endl;
        exit(1);
    }

    int send_port, agent_port;
    char send_ip[50], agent_ip[50];

    // read argument
    setIP(send_ip, argv[1]);
    sscanf(argv[2], "%d", &send_port);

    setIP(agent_ip, argv[3]);
    sscanf(argv[4], "%d", &agent_port);

    char *filepath = argv[5];

    // make socket related stuff
    sockFd = socket(PF_INET, SOCK_DGRAM, 0);

    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(agent_port);
    recvAddr.sin_addr.s_addr = inet_addr(agent_ip);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(send_port);
    addr.sin_addr.s_addr = inet_addr(send_ip);
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));
    bind(sockFd, (struct sockaddr *)&addr, sizeof(addr));
    //change to non-blocking
    fcntl(sockFd, F_SETFL, O_NONBLOCK);

    FILE *fp = fopen(filepath, "rb");
    fseek(fp, 0L, SEEK_END);
    ssize_t file_size = (ssize_t)ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    char *data = new char[file_size];
    fread(data, 1, file_size, fp);
    fclose(fp);

    cwnd = 1.0;
    base = 0;
    threshold = 16;
    dupACK = 0;
    
    dataNum = ceil(double(file_size) / double(MAX_SEG_SIZE));
    //indexed from 1
    sgmt = new segment[dataNum + 2];
    for (int i = 1; i <= dataNum; i++){
        if (i == dataNum && (file_size % MAX_SEG_BUF_SIZE != 0)){
            int tmp = file_size % MAX_SEG_SIZE;
            memcpy(sgmt[i].data, data + (i - 1) * MAX_SEG_SIZE,  tmp);
            sgmt[i].head.seqNumber = i;
            sgmt[i].head.length = tmp;
            sgmt[i].head.fin = 0;
            sgmt[i].head.ackNumber = 0;
            sgmt[i].head.sackNumber = 0;
            sgmt[i].head.syn = 0;
            sgmt[i].head.ack = 0;
            sgmt[i].head.checksum = crc32(0, (Bytef *)sgmt[i].data,  tmp);
            continue;
        }
        memcpy(sgmt[i].data, data + MAX_SEG_SIZE * (i - 1),  MAX_SEG_SIZE);
        sgmt[i].head.seqNumber = i;
        sgmt[i].head.length = MAX_SEG_SIZE;
        sgmt[i].head.fin = 0;
        sgmt[i].head.ackNumber = 0;
        sgmt[i].head.sackNumber = 0;
        sgmt[i].head.syn = 0;
        sgmt[i].head.ack = 0;
        sgmt[i].head.checksum = crc32(0, (Bytef *)sgmt[i].data,  MAX_SEG_SIZE);
    }
    
    
    segment fin_seg {};
    
    fin_seg.head.seqNumber = dataNum + 1;
    fin_seg.head.length = 0;
    fin_seg.head.fin = 1;
    fin_seg.head.ackNumber = 0;
    fin_seg.head.sackNumber = 0;
    fin_seg.head.syn = 0;
    fin_seg.head.ack = 0;
    fin_seg.head.checksum = 0;

    

    transmitNew();
    timer = clock();
    state = 0;

    segment recvSgmt {};
    //reliable transmisstion
    while (1){
        // handling timeout
        if (clock() - timer > TIMEOUT_MILLISECONDS * CLOCKS_PER_SEC / 1000){
            threshold = max(1, int(cwnd / 2));
            cwnd = 1;
            set<int> toMove = transmitQueue;
            for (int x : toMove){
                sendButNotAck.insert(x);
                transmitQueue.erase(x);
            }
            dupACK = 0;
            logEvent("timeout", 0, int(cwnd), threshold);
            transmitMissing(base+1);
            timer = clock();
            state = 0;
        }

        if (recvfrom(sockFd, &recvSgmt, sizeof(recvSgmt), 0, (struct sockaddr *)&recvAddr, &recvAddrSz) > 0){
            logEvent("recv", recvSgmt.head.ackNumber, 0, recvSgmt.head.sackNumber);
            //duplicate ACK
            if (recvSgmt.head.ackNumber <= base){
                dupACK++;
                //remove sack
                transmitQueue.erase(recvSgmt.head.sackNumber);
                transmitNew();
                if (dupACK == 3) transmitMissing(base+1);
            }
            //new ACK
            else{
                transmitQueue.erase(recvSgmt.head.sackNumber);
                base = recvSgmt.head.ackNumber;
                if (base == dataNum){
                     dupACK = 0; //transmission done
                    break;
                }
                if (state == 0){
                    dupACK = 0;
                    cwnd++;
                    if (cwnd >= threshold) state = 1;
                }
                else if (state == 1){
                    dupACK = 0;
                    cwnd += 1.0 / int(cwnd);
                }
                transmitNew();
                timer = clock();
            }
        }
    }
    sendto(sockFd, &fin_seg, sizeof(fin_seg), 0, (struct sockaddr *)&recvAddr, sizeof(recvAddr));
    printf("send\tfin\n");
    //wait fin
    while (1){
        int byte = recvfrom(sockFd, &recvSgmt, sizeof(recvSgmt), 0, (struct sockaddr *)&recvAddr, &recvAddrSz);
        if (recvSgmt.head.fin){
            printf("recv\tfinack\n");
            break;
        }
    }
}
