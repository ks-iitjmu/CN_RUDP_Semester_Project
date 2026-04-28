#pragma once
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <bits/types/struct_timeval.h>
#include <cerrno>
#include <raylib.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include "../../settings.hpp"
#include "../helper.hpp"

extern std::string headIp;
extern std::string nextIp;

bool hostShouldUpdateIds = false;
bool connectionServerStarted = false;
int  hostHandshakeSockFD = -1;  // kept so we can close it before game starts

void startLookingForPeers(){
    if (connectionServerStarted) return;
    connectionServerStarted = true;

    std::thread t1([&](){
        int sockFd = socket(AF_INET,SOCK_DGRAM,0);
        if (sockFd<0) return;

        timeval timeoutRecv;
        timeoutRecv.tv_sec = 0;
        timeoutRecv.tv_usec = 500000;

        if (setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &timeoutRecv, sizeof(timeoutRecv))<0) return;

        sockaddr_in dnsServer;
        dnsServer.sin_addr.s_addr = htonl(INADDR_ANY);
        dnsServer.sin_port = htons(0);
        dnsServer.sin_family = AF_INET;
        socklen_t sSize = sizeof(dnsServer);

        if (bind(sockFd,(sockaddr*)&dnsServer, sizeof(dnsServer))<0) return;

        if(getsockname(sockFd,(sockaddr*)&dnsServer,&sSize)<0) return;
        myPort = std::to_string(ntohs(dnsServer.sin_port));
        hostHandshakeSockFD = sockFd;  // store so stopLookingForPeer can close it
        
        sockaddr_in client; socklen_t cSize = sizeof(client);
        while (connectionServerStarted){
            std::string reqType(30,' ');
            int recvSize = recvfrom(sockFd,&reqType[0],reqType.size(),0,(sockaddr*)&client,&cSize);
            if ((recvSize<0) and !(errno==EAGAIN || errno==EWOULDBLOCK)) continue;
            else if (recvSize>=0){
                // std::cout << reqType << "| from " << inet_ntoa(client.sin_addr) << "@" << ntohs(client.sin_port) << std::endl;
                if (reqType[0]=='A'){
                    std::string portClient = reqType.substr(1,recvSize-1);
                    if (nextIp.size()==0){
                        nextIp = inet_ntoa(client.sin_addr) + (":" + portClient);
                        reqType = "S" + headIp;
                    }else{
                        reqType = "R" + nextIp;
                    }
                    sendto(sockFd,&reqType[0],reqType.size(),0,(sockaddr*)&client,cSize);
                }else if (reqType[0]=='X'){
                    int prevIdx = atoi(reqType.substr(1,reqType.find(';')-1).c_str());
                    totalPeers = prevIdx+1;

                    std::pair<std::string, int> nextIpData = getIpPort(nextIp);
                    sockaddr_in nextNode;
                    nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
                    nextNode.sin_port = htons(nextIpData.second);
                    nextNode.sin_family = AF_INET;

                    std::string payload = "Y" + std::to_string(prevIdx+1);
                    if (sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode))<0) return;
                }else if (reqType[0]=='Y'){
                    //Total Nodes Updated
                }
            }

            if ((nextIp.size()>0) and (hostShouldUpdateIds)){
                hostShouldUpdateIds = false;
                myID = 0;
                std::pair<std::string, int> nextIpData = getIpPort(nextIp);
                sockaddr_in nextNode;
                nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
                nextNode.sin_port = htons(nextIpData.second);
                nextNode.sin_family = AF_INET;

                std::string payload = "X0;";
                if (sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode))<0) return;
            }
        }

        std::pair<std::string, int> nextIpData = getIpPort(nextIp);
        sockaddr_in nextNode;
        nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
        nextNode.sin_port = htons(nextIpData.second);
        nextNode.sin_family = AF_INET;
        std::string payload = "F" + std::to_string(randomSeed);
        sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode));
        // std::cout << "Closed Socket" << std::endl;
        close(sockFd);
    });
    t1.detach();
}

void stopLookingForPeer(){
    randomSeed = time(NULL);
    SetRandomSeed(randomSeed);
    connectionServerStarted = false;
}
