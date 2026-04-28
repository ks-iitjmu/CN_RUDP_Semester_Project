#pragma once
#include "raylib.h"
#include "../settings.hpp"
#include "../networking/connection/client.hpp"
#include <string>

extern ThemeData themeData;
extern int currentScreen;

bool isIpValid(std::string &peerIp){
    int ipData[] = {-1,-1,-1,-1};
    int port = -1;
    int ptr = 0;
    for (auto &x: peerIp){
        if (x=='.') ptr++;
        else if (x==':') ptr = INT_MIN;
        else if (ptr>=0){
            if (ipData[ptr]==-1) ipData[ptr] = x-'0';
            else ipData[ptr] = (ipData[ptr]*10) + (x-'0');
        }else{
            if (port==-1) port = x-'0';
            else port = (port*10) + (x-'0');
        }

        if (ptr>3) return false;
    }
    for (int i=0; i<4; i++) if ((ipData[i]>255) or (ipData[i]<0)) return false;
    if ((port>0) and (port<=65525)) return true;
    return false;
}

void joinGroupScreen(){
    static std::string peerIp = "";
    static int connectedToRoom = false;
    int screenHeight = GetScreenHeight();
    int screenWidth = GetScreenWidth();

    ClearBackground(themeData.background);

    { // Back Arrow ESC Part
        DrawText("< ESC", themeData.defaultPadding, themeData.defaultPadding,themeData.defaultSmallTextSize,themeData.text);
    }

    { // Rendering Title
        std::string title = connectedToRoom?"Total Connected Peer(s):":"Enter IP of a PEER";
        int width = MeasureText(title.c_str(), themeData.defaultTitleSize);
        int offset = (screenWidth-width)>>1;
        DrawText(title.c_str(),offset, themeData.defaultPadding,themeData.defaultTitleSize,themeData.neonCyan);
    }

    if (!connectedToRoom){
    
        { // Rendering Underline
            int width = MeasureText("_______________", themeData.defaultTitleSize);
            int xOffset = (screenWidth-width)>>1;
            DrawText("_______________",xOffset, (screenHeight>>1)+5,themeData.defaultTitleSize,themeData.surface);
        }

        { // Rendering IP
            int width = MeasureText(peerIp.c_str(), themeData.defaultTitleSize);
            int xOffset = (screenWidth-width)>>1;
            DrawText(peerIp.c_str(),xOffset, (screenHeight>>1),themeData.defaultTitleSize,themeData.neonGreen);
        }
        
        if (!isIpValid(peerIp)){ //Rendering invalid ip
            int width = MeasureText("Enter Valid IP", themeData.defaultXSmallTextSize);
            int xOffset = (screenWidth-width)>>1;
            DrawText("Enter Valid IP",xOffset, (screenHeight>>1)+themeData.defaultPadding+10+themeData.defaultXSmallTextSize,themeData.defaultXSmallTextSize,themeData.neonRed);
        }
    }else{
        { // Rendering Peer Count
            int width = MeasureText(std::to_string(totalPeers).c_str(),themeData.defaultTitleSize);
            int yOffset = (screenHeight>>1), xOffset = ((screenWidth-width)>>1);
            DrawText(std::to_string(totalPeers).c_str(),xOffset,yOffset,themeData.defaultTitleSize,themeData.neonRed);
        }

        {// Waiting for Game to Start
            int width = MeasureText("Waiting for the Game to Start!",themeData.defaultTitleSize);
            int yOffset = (screenHeight - (themeData.defaultPadding<<2)), xOffset = ((screenWidth-width)>>1);
            DrawText("Waiting for the Game to Start!",xOffset,yOffset,themeData.defaultTitleSize,themeData.neonCyan);
        }
    }

    { // Inputs
        if (IsKeyPressed(KEY_ENTER) and isIpValid(peerIp)) {
            startJoiningPeers(peerIp);
            connectedToRoom = true;
        };
        if (IsKeyPressed(KEY_ESCAPE)) {
            currentScreen = 1;
            connectedToRoom = false;
            stopConnectionClient();
        }
        if ((peerIp.size()>0) and IsKeyPressed(KEY_BACKSPACE)) peerIp = peerIp.substr(0,peerIp.size()-1);
        int c = GetCharPressed();
        if ((peerIp.size()<20) and ((c=='.') or (c==':') or ((c>='0') and (c<='9')))) peerIp += (char)c;
    }
}
