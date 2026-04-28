/*
sending pipeline:
  queue <-- sendingData
recieving pipeline
*/
#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <sys/socket.h>
#include <thread>
#include <tuple>
#include "../../settings.hpp"
#include "../helper.hpp"
#define MAX_WINDOW_SIZE 10000

extern bool isMainGameP2pRunning;

std::vector<uint8_t> createPayload(char type, int id, int score, int px, int py, unsigned int seqId){
  std::vector<uint8_t> payload;
  payload.emplace_back(type);
  for (int i=0; i<(int)sizeof(id);    i++) {payload.emplace_back(id   &0xFF); id   >>=8;}
  for (int i=0; i<(int)sizeof(score); i++) {payload.emplace_back(score&0xFF); score>>=8;}
  for (int i=0; i<(int)sizeof(px);    i++) {payload.emplace_back(px   &0xFF); px   >>=8;}
  for (int i=0; i<(int)sizeof(py);    i++) {payload.emplace_back(py   &0xFF); py   >>=8;}
  for (int i=0; i<(int)sizeof(seqId); i++) {payload.emplace_back(seqId&0xFF); seqId>>=8;}
  return payload;
}
// returns (type, id, score, px, py, seqId)
std::tuple<char,int,int,int,int,unsigned int> loadPayload(std::vector<uint8_t> &payload){
  char type = payload[0];
  int id=0, score=0, px=0, py=0; unsigned int seqId=0;
  int off = 1;
  for (int i=0;i<(int)sizeof(id);   i++) id    |= (payload[off++]<<(i*8));
  for (int i=0;i<(int)sizeof(score);i++) score |= (payload[off++]<<(i*8));
  for (int i=0;i<(int)sizeof(px);   i++) px    |= (payload[off++]<<(i*8));
  for (int i=0;i<(int)sizeof(py);   i++) py    |= (payload[off++]<<(i*8));
  for (int i=0;i<(int)sizeof(seqId);i++) seqId |= (payload[off++]<<(i*8));
  return std::make_tuple(type,id,score,px,py,seqId);
}
 std::queue<std::tuple<char,int,int,int,int>> _goBackNQueue;  // (type,id,score,px,py)
 std::mutex _gbnqMutex;
 
void runMainGameP2pInRUDP(){
  std::thread rxThread([&](){
    int sockFD = socket(AF_INET,SOCK_DGRAM,0);
    if (sockFD<0) return;

    sockaddr_in server;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(static_cast<unsigned int>(std::stoul(myPort)));
    server.sin_family = AF_INET;

    timeval tv; tv.tv_sec=0; tv.tv_usec=50000;
    setsockopt(sockFD,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));

    int opt = 1;
    setsockopt(sockFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    unsigned int expectedSeqNum=0;
    while (bind(sockFD,(sockaddr*)&server,sizeof(server))<0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sockaddr_in client; socklen_t cSize = sizeof(client);
    while (isMainGameP2pRunning){
      std::vector<uint8_t> buffer(64);
      int rSize = recvfrom(sockFD,&buffer[0],buffer.size(),0,(sockaddr*)&client,&cSize);
      if ((rSize<0) and !(errno==EAGAIN || errno==EWOULDBLOCK)) break;
      if (rSize<0) continue;

      auto [type,id,score,px,py,seqNum] = loadPayload(buffer);
      { // ACK
        std::vector<uint8_t> ack = createPayload('A',0,0,0,0,seqNum);
        sendto(sockFD,&ack[0],ack.size(),0,(sockaddr*)&client,cSize);
      }
      if (expectedSeqNum==seqNum && type=='U'){
        expectedSeqNum = (expectedSeqNum+1)%(MAX_WINDOW_SIZE+1);
        if (id!=myID){
          _scoreAccessLock.lock(); scores[id]=score; _scoreAccessLock.unlock();
          _posAccessLock.lock(); peerPositions[id]={px,py}; _posAccessLock.unlock();
          _gbnqMutex.lock();
          _goBackNQueue.emplace(std::make_tuple('U',id,score,px,py));
          _gbnqMutex.unlock();
        }
      }
    }
    close(sockFD);
  }); 
  std::thread txThread([&](){
    int sockFD = socket(AF_INET,SOCK_DGRAM,0);
    if (sockFD<0) return;

    timeval tv; tv.tv_sec=0; tv.tv_usec=50000;
    setsockopt(sockFD,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));

    std::pair<std::string,int> nextIpData = getIpPort((nextIp.size()==0)?headIp:nextIp);
    sockaddr_in nextNode;
    nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
    nextNode.sin_port = htons(nextIpData.second);
    nextNode.sin_family = AF_INET;

    int nextSeqNum = 0;
    // window: (type, id, score, px, py, seqId)
    std::queue<std::tuple<char,int,int,int,int,unsigned int>> window;

    while (isMainGameP2pRunning){
      // Drain queue into window
      while ((int)window.size()<MAX_WINDOW_SIZE){
        _gbnqMutex.lock();
        if (_goBackNQueue.empty()){ _gbnqMutex.unlock(); break; }
        auto [txType,txId,txScore,txPx,txPy] = _goBackNQueue.front(); _goBackNQueue.pop();
        _gbnqMutex.unlock();
        auto dataToSend = std::make_tuple(txType,txId,txScore,txPx,txPy,(unsigned int)nextSeqNum);
        window.emplace(dataToSend);
        auto payload = createPayload(txType,txId,txScore,txPx,txPy,(unsigned int)nextSeqNum);
        nextSeqNum = (nextSeqNum+1)%(MAX_WINDOW_SIZE+1);
        sendto(sockFD,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode));
      }

      // Drain ACKs
      sockaddr_in client; socklen_t cSize = sizeof(client);
      while (true){
        std::vector<uint8_t> buf(64);
        int recvSize = recvfrom(sockFD,&buf[0],buf.size(),0,(sockaddr*)&client,&cSize);
        if (recvSize<0) break;
        auto [aType,aId,aScore,aPx,aPy,aSeq] = loadPayload(buf);
        if (!window.empty() && aType=='A' && aSeq==std::get<5>(window.front()))
          window.pop();
      }

      // Retransmit unACKed window
      int ws = window.size();
      while (ws--){
        auto front = window.front(); window.pop();
        auto payload = createPayload(std::get<0>(front),std::get<1>(front),std::get<2>(front),
                                     std::get<3>(front),std::get<4>(front),std::get<5>(front));
        sendto(sockFD,&payload[0],payload.size(),0,(sockaddr*)&nextNode,sizeof(nextNode));
        window.emplace(front);
      }

      // Enqueue my own position + score
      _scoreAccessLock.lock(); int curScore = scores[myID]; _scoreAccessLock.unlock();
      _posAccessLock.lock();
      int myPx = peerPositions.count(myID) ? peerPositions[myID].x : 0;
      int myPy = peerPositions.count(myID) ? peerPositions[myID].y : 0;
      _posAccessLock.unlock();
      _gbnqMutex.lock();
      _goBackNQueue.emplace(std::make_tuple('U',myID,curScore,myPx,myPy));
      _gbnqMutex.unlock();
    }
    close(sockFD);
  });

  txThread.detach();
  rxThread.detach();
}
