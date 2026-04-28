#pragma once
#include "raylib.h"
#include "../settings.hpp"

extern ThemeData themeData;
extern int currentScreen;

void startNewGameScreen(){
    static int choice = 0;
    std::vector<std::string> choices = {"Start New Peer Group","Join a Peer Group"};
    int screenHeight = GetScreenHeight();
    int screenWidth = GetScreenWidth();

    ClearBackground(themeData.background);

    { // Back Arrow ESC Part
        DrawText("< ESC", themeData.defaultPadding, themeData.defaultPadding,themeData.defaultSmallTextSize,themeData.text);
    }

    { // Rendering Choices
        int totalHeight = choices.size()*(themeData.defaultPadding + themeData.defaultSubTitleSize);
        int startYOffset = (screenHeight - totalHeight)>>1;
        for (int i=0; i<choices.size(); i++){
            std::string data = choices[i];
            if (choice==i) data = "> " + data + " <";
            int width = MeasureText(data.c_str(),themeData.defaultSubTitleSize);
            int xOffset = (screenWidth - width)>>1;
            DrawText(data.c_str(),xOffset,startYOffset + (themeData.defaultPadding>>1) + (themeData.defaultPadding + themeData.defaultSubTitleSize)*i,themeData.defaultSubTitleSize,(choice==i)?themeData.neonPink:themeData.text);
        }
    }

    { // Inputs
        if (IsKeyPressed(KEY_ENTER)){
            if (choice==0) currentScreen = 4;
            else if (choice==1) currentScreen = 3;
            else {
                CloseWindow();
                exit(0);
            }
        }
        if (IsKeyPressed(KEY_ESCAPE)) currentScreen = 0;
        if (((choice+1)<choices.size()) and IsKeyPressed(KEY_DOWN)) choice++;
        if (((choice-1)>=0) and IsKeyPressed(KEY_UP)) choice--;
    }
}