#pragma once
#include "p2p.hpp"
#include <arpa/inet.h>
#include <bits/types/struct_timeval.h>
#include "../helper.hpp"
#include "../../settings.hpp"
#include <chrono>
#include <thread>
#include <unistd.h>

extern bool isMainGameP2pRunning;

void runMainGameP2pInUDP(){
  std::thread t1([&](){
    int sockFD = socket(AF_INET,SOCK_DGRAM,0);
    if (sockFD<0) return;

    sockaddr_in server;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(static_cast<unsigned int>(std::stoul(myPort)));
    server.sin_family = AF_INET;

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    int opt = 1;
    setsockopt(sockFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (setsockopt(sockFD,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))<0) return;
    // Wait briefly for the handshake socket to release myPort
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    while (bind(sockFD,(sockaddr*)&server,sizeof(server))<0) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::pair<std::string,int> nextIpData = getIpPort((nextIp.size()==0)?headIp:nextIp);
    sockaddr_in nextNode;
    nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
    nextNode.sin_port = htons(nextIpData.second);
    nextNode.sin_family = AF_INET;

    static int myLastScore = INT_MIN;
    while (isMainGameP2pRunning){
      // Send position + score every 50ms
      _scoreAccessLock.lock();
      int currentScore = scores[myID];
      _scoreAccessLock.unlock();
      _posAccessLock.lock();
      int px = peerPositions.count(myID) ? peerPositions[myID].x : 0;
      int py = peerPositions.count(myID) ? peerPositions[myID].y : 0;
      _posAccessLock.unlock();
      std::string payload = "U" + std::to_string(myID) + "|" + std::to_string(currentScore)
                          + "|" + std::to_string(px) + "|" + std::to_string(py);
      sendto(sockFD,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode));
      myLastScore = currentScore;

      std::string buffer(64,' ');
      sockaddr_in client; socklen_t cSize = sizeof(client);
      int recvSize = recvfrom(sockFD,&buffer[0],buffer.size(),0,(sockaddr*)&client,&cSize);
      if (recvSize>0 && buffer[0]=='U'){
        std::string data = buffer.substr(1, recvSize-1);
        std::vector<int> parts;
        std::stringstream ss(data);
        std::string token;
        while(std::getline(ss,token,'|')) parts.push_back(std::stoi(token));
        if (parts.size()>=4){
          int senderIdx=parts[0], newScore=parts[1], spx=parts[2], spy=parts[3];
          if (senderIdx!=myID){
            _scoreAccessLock.lock(); scores[senderIdx]=newScore; _scoreAccessLock.unlock();
            _posAccessLock.lock(); peerPositions[senderIdx]={spx,spy}; _posAccessLock.unlock();
            if (sendto(sockFD,&buffer[0],recvSize,0,(sockaddr*)&nextNode,sizeof(nextNode))<0) continue;
          }
        }
      }
    }
    close(sockFD);
  });
  t1.detach();
}
