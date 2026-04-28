#pragma once
#include <arpa/inet.h>
#include <bits/stdc++.h>

std::pair<std::string,int> getIpPort(std::string combinedIp){
  int loc = combinedIp.find(':');
  int port = atoi(combinedIp.substr(loc+1).c_str());
  return std::make_pair(combinedIp.substr(0,loc),port);
}
