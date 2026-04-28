#pragma once
#include "raylib.h"
#include <bits/stdc++.h>
#include "./networking/fetchIp.hpp"

struct ThemeData {
    Color surface;
    Color background;
    Color text;
    
    Color neonBlue;
    Color neonPink;
    Color neonCyan;
    Color neonGreen;
    Color neonRed;

    int defaultTitleSize = 45;
    int defaultSubTitleSize = 38;
    int defaultTextSize = 32;
    int defaultXSmallTextSize = 20;
    int defaultSmallTextSize = 25;
    int defaultPadding = 20;
};

//Dark Theme (Tokyo Night/Storm)
ThemeData darkThemeData = {
    { 36, 40, 59, 255 }, { 26, 27, 38, 255 }, { 169, 177, 214, 255 },
    { 122, 162, 247, 255 }, { 187, 154, 247, 255 }, { 125, 207, 255, 255 },
    { 158, 206, 106, 255 }, { 247, 118, 142, 255 }
};

// Light Theme (Tokyo Day)
ThemeData lightThemeData = {
    { 225, 227, 234, 255 }, { 240, 241, 244, 255 }, { 52, 59, 88, 255 },
    { 46, 115, 160, 255 }, { 152, 90, 158, 255 }, { 0, 122, 140, 255 },
    { 72, 118, 51, 255 }, { 186, 45, 78, 255 }
};

ThemeData themeData = darkThemeData;
bool darkTheme = 1;

void changeTheme(bool toDarkTheme){
    darkTheme = toDarkTheme;
    themeData = (toDarkTheme)?darkThemeData:lightThemeData;
}

struct GameData{
    int protocol=0;
    std::vector<std::string> listAllProtocols={"TCP","UDP","Custom"};
} gameData;

std::vector<std::pair<std::string,std::string>> myIPs = getMyIPs();
std::string myPort="";
std::string headIp="",nextIp="";
bool isInConnectingState = true;
int myID = -1;
int totalPeers = 1;
unsigned int randomSeed;

int mainGameStartTime = -1;
std::vector<int> scores;
std::mutex _scoreAccessLock;

// Real-time peer positions: id -> {x, y}
struct PeerPos { int x; int y; };
std::map<int, PeerPos> peerPositions;
std::mutex _posAccessLock;

// Neon color palette per player index
static const Color PEER_COLORS[] = {
    {187,154,247,255},  // neonPink  (Player 0)
    {125,207,255,255},  // neonCyan  (Player 1)
    {158,206,106,255},  // neonGreen (Player 2)
    {122,162,247,255},  // neonBlue  (Player 3)
    {247,118,142,255},  // neonRed   (Player 4)
    {255,200,100,255},  // orange    (Player 5)
};
