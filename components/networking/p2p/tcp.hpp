#pragma once
#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "../helper.hpp"
#include "../../settings.hpp"

extern bool isMainGameP2pRunning;

void runMainGameP2pInTCP(){
  // ── Server Thread: listens on myPort, receives ring packets ──────────────
  std::thread tServer([&](){
    int sockFD = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFD < 0) return;

    // Allow rapid re-bind after restart
    int opt = 1;
    setsockopt(sockFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Wait for handshake socket to fully release myPort
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    sockaddr_in server{};
    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port        = htons(static_cast<unsigned int>(std::stoul(myPort)));

    while (bind(sockFD,(sockaddr*)&server,sizeof(server)) < 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (listen(sockFD, 16) < 0) { close(sockFD); return; }

    // Compute ring next-node address once
    std::pair<std::string,int> nextIpData = getIpPort((nextIp.size()==0) ? headIp : nextIp);
    sockaddr_in nextNode{};
    nextNode.sin_family      = AF_INET;
    nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
    nextNode.sin_port        = htons(nextIpData.second);

    while (true) {
      sockaddr_in client{}; socklen_t cSize = sizeof(client);
      int clientFD = accept(sockFD,(sockaddr*)&client,&cSize);
      if (clientFD < 0) continue;

      std::string buffer(128, ' ');
      int rSize = recv(clientFD, &buffer[0], buffer.size(), 0);
      close(clientFD);

      if (rSize <= 0) continue;           // FIX: don't break on closed conn
      if (buffer[0] == '-') break;        // game-over signal from local tClient

      if (buffer[0] == 'U') {
        // payload: "U{id}|{score}|{x}|{y}"
        std::string data = buffer.substr(1, rSize - 1);
        std::vector<int> parts;
        std::stringstream ss(data);
        std::string token;
        while (std::getline(ss, token, '|')) {
          if (!token.empty() && token.find_first_not_of(" \t\r\n") != std::string::npos)
            parts.push_back(std::stoi(token));
        }
        if ((int)parts.size() >= 4) {
          int senderIdx = parts[0], newScore = parts[1], px = parts[2], py = parts[3];
          if (senderIdx != myID) {
            _scoreAccessLock.lock(); scores[senderIdx] = newScore; _scoreAccessLock.unlock();
            _posAccessLock.lock();   peerPositions[senderIdx] = {px, py}; _posAccessLock.unlock();
            // Relay to next node in ring
            std::string fwd = buffer.substr(0, rSize);
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd >= 0) {
              if (connect(fd,(sockaddr*)&nextNode,sizeof(nextNode)) >= 0)
                send(fd, &fwd[0], rSize, MSG_NOSIGNAL);
              close(fd);
            }
          }
        }
      }
    }
    close(sockFD);
  });

  // ── Client Thread: pushes own position every 50ms ────────────────────────
  std::thread tClient([&](){
    std::pair<std::string,int> nextIpData = getIpPort((nextIp.size()==0) ? headIp : nextIp);
    sockaddr_in nextNode{};
    nextNode.sin_family      = AF_INET;
    nextNode.sin_addr.s_addr = inet_addr(nextIpData.first.c_str());
    nextNode.sin_port        = htons(nextIpData.second);

    while (isMainGameP2pRunning) {
      _scoreAccessLock.lock(); int currentScore = scores[myID]; _scoreAccessLock.unlock();
      _posAccessLock.lock();
      int px = peerPositions.count(myID) ? peerPositions[myID].x : 0;
      int py = peerPositions.count(myID) ? peerPositions[myID].y : 0;
      _posAccessLock.unlock();

      std::string payload = "U" + std::to_string(myID) + "|" + std::to_string(currentScore)
                          + "|" + std::to_string(px)   + "|" + std::to_string(py);
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd >= 0) {
        if (connect(fd,(sockaddr*)&nextNode,sizeof(nextNode)) >= 0)
          send(fd, &payload[0], payload.size(), MSG_NOSIGNAL);
        close(fd);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Send game-over signal to unblock server's accept()
    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = inet_addr("127.0.0.1");
    local.sin_port        = htons(static_cast<unsigned int>(std::stoul(myPort)));
    for (int tries = 0; tries < 10; tries++) {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0) continue;
      if (connect(fd,(sockaddr*)&local,sizeof(local)) >= 0) {
        std::string stop = "-";
        send(fd, &stop[0], 1, MSG_NOSIGNAL);
        close(fd);
        break;
      }
      close(fd);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  tServer.detach();
  tClient.detach();
}
