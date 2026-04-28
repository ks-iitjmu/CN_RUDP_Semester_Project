#pragma once
#include "raylib.h"
#include "../settings.hpp"
#include "../networking/connection/host.hpp"
#include <string>

extern ThemeData themeData;
extern int currentScreen;

void waitingForPeerScreen(){
    int screenHeight = GetScreenHeight();
    int screenWidth = GetScreenWidth();
    int currentTicks = GetTime();

    ClearBackground(themeData.background);

    { // Back Arrow ESC Part
        DrawText("< ESC", themeData.defaultPadding, themeData.defaultPadding,themeData.defaultSmallTextSize,themeData.text);
    }

    { // Rendering Title
        std::string title = "Waiting for Peers" + std::string((currentTicks%3)+1,'.');
        int width = MeasureText(title.c_str(),themeData.defaultTitleSize);
        int xOffset = (screenWidth-width)>>1;
        DrawText(title.c_str(),xOffset,themeData.defaultPadding,themeData.defaultTitleSize,themeData.neonCyan);
    }


    int yOffset = (themeData.defaultPadding<<2) + themeData.defaultTitleSize;
    { // Rendering My Ips
        std::string title = "Listening on IP(s):";
        int width = MeasureText(title.c_str(),themeData.defaultSubTitleSize);
        int xOffset = (screenWidth-width)>>1;
        DrawText(title.c_str(),xOffset,yOffset,themeData.defaultSubTitleSize,themeData.neonGreen);

        yOffset += themeData.defaultSubTitleSize + themeData.defaultPadding;
        for (auto &ipNamePair: myIPs){ //Rendering Individual IPs
            std::string data = ipNamePair.first + ":" + myPort + " (on " + ipNamePair.second + ")";
            int width = MeasureText(data.c_str(),themeData.defaultTextSize);
            int xOffset = (screenWidth-width)>>1;
            DrawText(data.c_str(),xOffset,yOffset,themeData.defaultTextSize,themeData.text);
            yOffset += themeData.defaultTextSize + themeData.defaultPadding;
        }
    }

    yOffset += themeData.defaultPadding<<1;
    {// Rendering Connected Peers
        std::string title = "Total Peers Connected:";
        int width = MeasureText(title.c_str(),themeData.defaultSubTitleSize);
        int xOffset = (screenWidth-width)>>1;
        DrawText(title.c_str(),xOffset,yOffset,themeData.defaultSubTitleSize,themeData.neonGreen);
        yOffset += themeData.defaultPadding + themeData.defaultSubTitleSize;

        width = MeasureText(std::to_string(totalPeers).c_str(), themeData.defaultTitleSize);
        xOffset = (screenWidth-width)>>1;
        DrawText(std::to_string(totalPeers).c_str(),xOffset,yOffset,themeData.defaultTitleSize,themeData.neonRed); 
    }

    if (totalPeers>1){ // Rendering "Press Enter to Start The Game"
        int width = MeasureText("Press Enter to Start The Game", themeData.defaultSubTitleSize);
        int xOffset = ((screenWidth-width)>>1), yOffset = (screenHeight - (themeData.defaultPadding<<2));
        DrawText("Press Enter to Start The Game",xOffset, yOffset, themeData.defaultSubTitleSize,themeData.neonCyan);
    }

    { // Processing
        startLookingForPeers();
        static int lastTime = -1;
        int currentTime = GetTime();
        if (currentTime!=lastTime){
            lastTime = currentTime;
            hostShouldUpdateIds = true;
        }
    }

    { // Inputs
        if (IsKeyPressed(KEY_ENTER) and (totalPeers>1)){
            stopLookingForPeer();
            currentScreen = 5;
            mainGameStartTime = GetTime();
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            currentScreen = 1; 
            stopLookingForPeer();
        }
    //     if (((choice+1)<choices.size()) and IsKeyPressed(KEY_DOWN)) choice++;
    //     if (((choice-1)>=0) and IsKeyPressed(KEY_UP)) choice--;
    }
}
