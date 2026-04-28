#include "raylib.h"
#include <bits/stdc++.h>
#include <signal.h>
#include "./components/game.hpp"

extern ThemeData themeData;
int currentScreen = 0;

int main(){
    signal(SIGPIPE, SIG_IGN);
    SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1500, 1000, "CN Game Project Demo");
    SetTargetFPS(60);
    SetExitKey(0);

    while (!WindowShouldClose()){
        BeginDrawing();
        displayScreens();
        EndDrawing();
    }

    CloseWindow();
    
}