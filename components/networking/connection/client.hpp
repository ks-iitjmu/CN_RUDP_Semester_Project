#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <raylib.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include "../../settings.hpp"
#include "../helper.hpp"

bool connectionClientStarted = false;
bool isClientConnectionDone = false;

void startJoiningPeers(std::string &ip){
  if (connectionClientStarted) return;
  connectionClientStarted = true;

  std::pair<std::string,int> hostIpData = getIpPort(ip);
  std::thread t1([&](std::pair<std::string,int> hostIpData){
      int sockFd = socket(AF_INET, SOCK_DGRAM,0);
      if (sockFd<0) return;

      sockaddr_in dnsServer;
      dnsServer.sin_addr.s_addr = ntohl(INADDR_ANY);
      dnsServer.sin_port = 0;
      dnsServer.sin_family = AF_INET;
      socklen_t sSize = sizeof(dnsServer);

      if (bind(sockFd,(sockaddr*)&dnsServer,sSize)<0) return;
      if (getsockname(sockFd, (sockaddr*)&dnsServer,&sSize)<0) return;
      myPort = std::to_string(ntohs(dnsServer.sin_port));

      { // Adding myself to Peers
        std::string myPortReq = "A" + myPort;
        int parentSockFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (parentSockFd<0) return;

        sockaddr_in parentDnsServer;
        parentDnsServer.sin_addr.s_addr = inet_addr(hostIpData.first.c_str());
        parentDnsServer.sin_port = htons(hostIpData.second);
        parentDnsServer.sin_family = AF_INET;
        socklen_t parentSize = sizeof(parentDnsServer);
      
        if (sendto(parentSockFd,&myPortReq[0],myPortReq.size(),0,(sockaddr*)&parentDnsServer,sizeof(parentDnsServer))<0) return;
        std::string buffer(30,' ');
        while (buffer[0]!='S'){
          int recvSize = recvfrom(parentSockFd,&buffer[0],buffer.size(),0,(sockaddr*)&parentDnsServer,&parentSize);
          if (recvSize<0) return;
          
          // std::cout << buffer << "| from " << inet_ntoa(parentDnsServer.sin_addr) << "@" << ntohs(parentDnsServer.sin_port) << std::endl;
          if (buffer[0]=='R'){
            std::pair<std::string,int> nextIpData = getIpPort(buffer.substr(1,recvSize-1));
            parentDnsServer.sin_port = htons(nextIpData.second);
            parentDnsServer.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
            if (sendto(parentSockFd,&myPortReq[0],myPortReq.size(),0,(sockaddr*)&parentDnsServer,sizeof(parentDnsServer))<0) return;
          }else if (buffer[0]=='S'){
            buffer = buffer.substr(1,recvSize-1);
            if (buffer.size()==0) headIp = inet_ntoa(parentDnsServer.sin_addr) + (":" + std::to_string(hostIpData.second));
            else headIp = buffer;
            buffer = "S";
          }
        }

        close(parentSockFd);
      }
     
      sockaddr_in client; socklen_t cSize = sizeof(client);
      while (connectionClientStarted){
          std::pair<std::string,int> nextIpData = getIpPort((nextIp.size()==0)?headIp:nextIp);
          sockaddr_in nextNode;
          nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
          nextNode.sin_port = htons(nextIpData.second);
          nextNode.sin_family = AF_INET;

          std::string reqType(30,' ');
          int recvSize = recvfrom(sockFd,&reqType[0],reqType.size(),0,(sockaddr*)&client,&cSize);
          if (recvSize<0) continue;
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
            myID = prevIdx + 1;
            std::string payload = "X" + std::to_string(prevIdx+1) + ";";
            if (sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode))<0) return;
          }else if (reqType[0]=='Y'){
            totalPeers  = atoi(reqType.substr(1,recvSize-1).c_str());
            std::string payload = "Y" + std::to_string(totalPeers);
            if (sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode))<0) return;
          }else if (reqType[0]=='F'){
            reqType = reqType.substr(1);
            randomSeed = static_cast<unsigned int>(std::stoul(reqType));
            SetRandomSeed(randomSeed);
            std::string payload = "F" + std::to_string(randomSeed);
            if (sendto(sockFd,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode))<0) return;
            connectionClientStarted = false;
            isClientConnectionDone = true;
            // Close handshake socket so myPort is freed before TCP/UDP/RUDP bind
            close(sockFd);
            currentScreen = 5;
            mainGameStartTime = GetTime();
            return;
          }
      }
      currentScreen = 5;
      mainGameStartTime = GetTime();
      close(sockFd);
  },hostIpData);
  t1.detach();  
}

void stopConnectionClient(){
  connectionClientStarted = false;
  return;
}
