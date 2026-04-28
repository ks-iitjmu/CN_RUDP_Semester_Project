#pragma once
#include "raylib.h"
#include "../settings.hpp"
#include <bits/stdc++.h>
#include <string>


std::vector<std::pair<int,int>> getRanks(){
  static bool computationDone = false;
  static std::vector<std::pair<int,int>> rankData;

  if (computationDone) return rankData;
  computationDone = true;
  _scoreAccessLock.lock();
  for (int i=0; i<scores.size(); i++) rankData.emplace_back(std::make_pair(scores[i],i));
  _scoreAccessLock.unlock();

  std::sort(rankData.rbegin(),rankData.rend());
  return rankData;
}

void finalVerdictScreen(){
  int screenWidth = GetScreenWidth();
  int screenHeight = GetScreenHeight();
  ClearBackground(themeData.background);

  {// Rankings
    std::string title = "Final Rankings";
    int width = MeasureText(title.c_str(), themeData.defaultTitleSize);
    int xOffset = (screenWidth - width)>>1;
    DrawText(title.c_str(),xOffset,(themeData.defaultPadding<<1),themeData.defaultTitleSize,themeData.neonBlue);
  }

  std::vector<std::pair<int,int>> rankData = getRanks();
  int yOffset = ((screenHeight - (((themeData.defaultPadding<<1) + themeData.defaultSubTitleSize))*rankData.size()))>>1;
  int currentRank=0;
  for (int i=0; i<rankData.size(); i++){
    if ((i==0) or (rankData[i-1].first!=rankData[i].first)) currentRank++;
    yOffset += themeData.defaultPadding;
    std::string text = "Rank-" + std::to_string(currentRank) + ": Peer-" + std::to_string(rankData[i].second);
    int width = MeasureText(text.c_str(),themeData.defaultSubTitleSize);
    DrawText(text.c_str(),((screenWidth - width)>>1),yOffset,themeData.defaultSubTitleSize,themeData.neonPink);
    yOffset += themeData.defaultPadding;
  }
}
