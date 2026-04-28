#pragma once
#include "raylib.h"
#include "../settings.hpp"

extern ThemeData themeData;
extern bool darkTheme;
extern void changeTheme(bool);
extern GameData gameData;
extern int currentScreen;

void optionsScreen(){
    static int currentProperty = 0;
    std::string title = "Options";
    int screenHeight = GetScreenHeight();
    int screenWidth = GetScreenWidth();

    ClearBackground(themeData.background);

    { // Back Arrow ESC Part
        DrawText("< ESC", themeData.defaultPadding, themeData.defaultPadding,themeData.defaultSmallTextSize,themeData.text);
    }

    { // Rendering Title
        int width = MeasureText(title.c_str(), themeData.defaultTitleSize);
        int offset = (screenWidth-width)>>1;
        DrawText(title.c_str(),offset, themeData.defaultPadding,themeData.defaultTitleSize,themeData.neonCyan);
    }

    { // Rendering Protocol Part
        DrawText("Protocol",themeData.defaultPadding<<2,(themeData.defaultPadding<<2)+themeData.defaultTitleSize,themeData.defaultSubTitleSize,(currentProperty==0)?themeData.neonPink:themeData.text);

        std::string text = "<\t" + gameData.listAllProtocols[gameData.protocol] + "\t>";
        int width = MeasureText(text.c_str(),themeData.defaultSubTitleSize);
        DrawText(text.c_str(),screenWidth - width - (themeData.defaultPadding<<2),(themeData.defaultPadding<<2)+themeData.defaultTitleSize,themeData.defaultSubTitleSize,(currentProperty==0)?themeData.neonBlue:themeData.text);
    }

    { // Rendering Theme Part
        DrawText("Theme",themeData.defaultPadding<<2,(themeData.defaultPadding<<3)+themeData.defaultTitleSize,themeData.defaultSubTitleSize,(currentProperty==1)?themeData.neonPink:themeData.text);

        std::string text = (darkTheme)?"<\tDark\t>":"<\tLight\t>";
        int width = MeasureText(text.c_str(),themeData.defaultSubTitleSize);
        DrawText(text.c_str(),screenWidth - width - (themeData.defaultPadding<<2),(themeData.defaultPadding<<3)+themeData.defaultTitleSize,themeData.defaultSubTitleSize,(currentProperty==1)?themeData.neonBlue:themeData.text);
    }

    { //Inputs
        if (IsKeyPressed(KEY_ESCAPE)) currentScreen = 0;
        if ((currentProperty<1) and IsKeyPressed(KEY_DOWN)) currentProperty++;
        if ((currentProperty>0) and IsKeyPressed(KEY_UP)) currentProperty--;
        if (currentProperty==0){
            int n = gameData.listAllProtocols.size();
            if (IsKeyPressed(KEY_LEFT)) gameData.protocol = (((gameData.protocol-1)%n) + n)%n;
            if (IsKeyPressed(KEY_RIGHT)) gameData.protocol = ((gameData.protocol+1)%n);
        }else if (currentProperty==1){
            if (IsKeyPressed(KEY_LEFT) or IsKeyPressed(KEY_RIGHT)) changeTheme(!darkTheme);
        }
    }
}