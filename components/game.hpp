#pragma once
#include "./screens/welcomeScreen.hpp"
#include "./screens/joinGroupScreen.hpp"
#include "./screens/startNewGame.hpp"
#include "./screens/optionsScreen.hpp"
#include "./screens/optionsScreen.hpp"
#include "./screens/waitingForPeersScreen.hpp"
#include "./screens/mainGameScreen.hpp"
#include "./settings.hpp"
#include "screens/finalVerdict.hpp"

extern int currentScreen;

void displayScreens(){
    switch (currentScreen){
            case 1:
                startNewGameScreen();
                break;
            case 2:
                optionsScreen();
                break;
            case 3:
                joinGroupScreen();
                break;
            case 4:
                waitingForPeerScreen();
                break;
            case 5:
                mainGameScreen();
                break;
            case 6:
                finalVerdictScreen();
                break;
            default:
                welcomeScreen();
                break;
        }
}
