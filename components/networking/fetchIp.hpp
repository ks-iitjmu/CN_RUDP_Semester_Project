#pragma once
#include <ifaddrs.h>
#include<arpa/inet.h>

std::vector<std::pair<std::string,std::string>> getMyIPs(){
    struct ifaddrs *addrs, *head;

    if (getifaddrs(&addrs)==-1){
        std::cerr << "Error Occured" << std::endl;
        return {};
    }

    std::vector<std::pair<std::string,std::string>> myIPs;

    for (head = addrs; head!=nullptr; head = head->ifa_next)
        if ((head->ifa_addr!=nullptr) and (head->ifa_addr->sa_family==AF_INET))
            myIPs.emplace_back(std::make_pair(inet_ntoa(((struct sockaddr_in*)head->ifa_addr)->sin_addr),head->ifa_name));

    freeifaddrs(addrs);
    return myIPs;
}