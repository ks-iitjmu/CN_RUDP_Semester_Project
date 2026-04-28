#pragma once
#include "raylib.h"
#include "../settings.hpp"

extern ThemeData themeData;
extern int currentScreen;

void welcomeScreen(){
    std::string title = "CN Demo Game";
    std::vector<std::string> choices = {"Play","Options","Exit"};
    static int choice = 0;
    int screenHeight = GetScreenHeight();
    int screenWidth = GetScreenWidth();

    ClearBackground(themeData.background);

    { // Rendering Title
        int width = MeasureText(title.c_str(), themeData.defaultTitleSize);
        int offset = (screenWidth-width)>>1;
        DrawText(title.c_str(),offset, themeData.defaultPadding,themeData.defaultTitleSize,themeData.neonCyan);
    }

    { // Rendering Choices
        int totalHeight = choices.size()*(themeData.defaultPadding + themeData.defaultSubTitleSize);
        int startYOffset = (screenHeight - totalHeight)>>1;
        for (int i=0; i<choices.size(); i++){
            std::string data = choices[i];
            if (choice==i) data = "> " + data + " <";
            int width = MeasureText(data.c_str(),themeData.defaultSubTitleSize);
            int xOffset = (screenWidth - width)>>1;
            Color color = (choice==i)?((choice==(choices.size()-1))?themeData.neonRed:themeData.neonPink):themeData.text;
            DrawText(data.c_str(),xOffset,startYOffset + (themeData.defaultPadding>>1) + (themeData.defaultPadding + themeData.defaultSubTitleSize)*i,themeData.defaultSubTitleSize,color);
        }
    }
    

    { // Inputs
        if (IsKeyPressed(KEY_ENTER)){
            if (choice==0) currentScreen = 1;
            else if (choice==1) currentScreen = 2;
            else {
                CloseWindow();
                exit(0);
            }
        }
        if (((choice+1)<choices.size()) and IsKeyPressed(KEY_DOWN)) choice++;
        if (((choice-1)>=0) and IsKeyPressed(KEY_UP)) choice--;
    }
}