#pragma once
#include "../../settings.hpp"
#include "./tcp.hpp"
#include "./udp.hpp"
#include "./rudp.hpp"
#include <vector>

bool isMainGameP2pRunning = false;

void runMainGameP2p(){
  if (isMainGameP2pRunning) return;
  isMainGameP2pRunning = true;
  scores = std::vector<int>(totalPeers,0);

  std::cout << "Protocol Chosen:" << gameData.listAllProtocols[gameData.protocol] << "@" << gameData.protocol << std::endl;
  std::cout << "My Port: " << myPort << std::endl;
  std::cout << "HeadIp: " << headIp << std::endl;
  std::cout << "NextIp: " << nextIp << std::endl;
  std::cout << "P2p Next: " << ((nextIp.size()==0)?headIp:nextIp) << std::endl;

  switch (gameData.protocol) {
    case 0:
      runMainGameP2pInTCP();
      break;
    case 1:
      runMainGameP2pInUDP();
      break;
    default:
      runMainGameP2pInRUDP();
      break;
  }
}

void stopMainGameP2p(){
  isMainGameP2pRunning = false;
}
