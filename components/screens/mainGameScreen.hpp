#pragma once
#include "raylib.h"
#include <bits/stdc++.h>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include "../settings.hpp"
#include "../networking/p2p/p2p.hpp"

extern int currentScreen;
volatile int _myCurrentRank = 1;

// ─── Particle System ────────────────────────────────────────
struct Particle {
  float x, y, vx, vy, life, maxLife;
  Color color;
};
static std::vector<Particle> particles;

void spawnParticles(int x, int y, Color c) {
  for (int i = 0; i < 18; i++) {
    float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
    float spd   = (float)GetRandomValue(2, 8);
    particles.push_back({(float)x,(float)y,
      cosf(angle)*spd, sinf(angle)*spd,
      1.0f, 1.0f, c});
  }
}

void updateParticles(float dt) {
  for (auto &p : particles) {
    p.x += p.vx; p.y += p.vy;
    p.vx *= 0.92f; p.vy *= 0.92f;
    p.life -= dt * 1.5f;
    p.color.a = (unsigned char)(255 * p.life);
  }
  particles.erase(std::remove_if(particles.begin(), particles.end(),
    [](const Particle &p){ return p.life <= 0.0f; }), particles.end());
}

// ─── Neon Grid Background ───────────────────────────────────
void drawGrid(int screenWidth, int screenHeight) {
  int spacing = 50;
  Color lineColor = {40, 50, 80, 140};
  for (int x = 0; x < screenWidth; x += spacing)
    DrawLine(x, 0, x, screenHeight, lineColor);
  for (int y = 0; y < screenHeight; y += spacing)
    DrawLine(0, y, screenWidth, y, lineColor);
}

// ─── Smooth position ────────────────────────────────────────
static float floatX = -1, floatY = -1;

double distance(int x, int y, int xP, int yP){
  double xDiff = x-xP, yDiff = y-yP;
  return std::sqrt(xDiff*xDiff + yDiff*yDiff);
}

void mainGameScreen(){
  int screenWidth  = GetScreenWidth();
  int screenHeight = GetScreenHeight();
  float dt = GetFrameTime();

  static int x=(screenWidth>>1), y=(screenHeight>>1);
  static int xPoint=-1, yPoint=-1;
  static int shownPoints = 0;

  // Initialize float position
  if (floatX < 0) { floatX = (float)x; floatY = (float)y; }

  if ((xPoint==-1) or (yPoint==-1)){
    xPoint = GetRandomValue(themeData.defaultPadding, screenWidth  - themeData.defaultPadding);
    yPoint = GetRandomValue(themeData.defaultTitleSize+(themeData.defaultPadding<<1), screenHeight - themeData.defaultPadding);
    shownPoints++;
  }

  // ─── Background ─────────────────────────────────────────
  ClearBackground(themeData.background);
  drawGrid(screenWidth, screenHeight);

  // ─── Title ──────────────────────────────────────────────
  {
    std::string title = "Collect Most Coins to Win!";
    int width = MeasureText(title.c_str(), themeData.defaultTitleSize);
    DrawText(title.c_str(),(screenWidth-width)>>1, themeData.defaultPadding, themeData.defaultTitleSize, themeData.neonBlue);
  }

  // ─── Timer & Rank ───────────────────────────────────────
  int currentTime = 20 - (GetTime() - mainGameStartTime);
  {
    std::string timeStr = "Time: " + std::to_string(std::max(0,currentTime)) + "s";
    std::string rankStr = "Rank: " + std::to_string(_myCurrentRank);
    int tw = MeasureText(timeStr.c_str(), themeData.defaultTitleSize);
    DrawText(timeStr.c_str(), screenWidth-tw-themeData.defaultPadding, themeData.defaultPadding,
             themeData.defaultTitleSize, (currentTime>3)?themeData.neonCyan:themeData.neonRed);
    DrawText(rankStr.c_str(), themeData.defaultPadding, themeData.defaultPadding,
             themeData.defaultTitleSize, themeData.neonPink);
  }

  // ─── Live Leaderboard (right side) ──────────────────────
  {
    int lbX = screenWidth - 200, lbY = themeData.defaultTitleSize + (themeData.defaultPadding<<1);
    DrawText("Leaderboard", lbX, lbY, themeData.defaultSmallTextSize, themeData.neonCyan);
    lbY += themeData.defaultSmallTextSize + 6;

    // Build sorted snapshot
    std::vector<std::pair<int,int>> lb; // (score, id)
    _scoreAccessLock.lock();
    for (int i=0; i<(int)scores.size(); i++) lb.push_back({scores[i],i});
    _scoreAccessLock.unlock();
    std::sort(lb.rbegin(), lb.rend());

    for (int i=0; i<(int)lb.size(); i++){
      int pid = lb[i].second;
      Color c = PEER_COLORS[pid % 6];
      std::string entry = (pid==myID ? "YOU" : "P"+std::to_string(pid))
                        + "  " + std::to_string(lb[i].first);
      DrawRectangle(lbX-4, lbY-2, 180, themeData.defaultXSmallTextSize+4, {c.r,c.g,c.b,30});
      DrawText(entry.c_str(), lbX, lbY, themeData.defaultXSmallTextSize, c);
      lbY += themeData.defaultXSmallTextSize + 8;
    }
  }

  // ─── Coin (glowing yellow circle) ──────────────────────
  {
    // outer glow
    DrawCircle(xPoint, yPoint, themeData.defaultSmallTextSize, {255,200,0,40});
    DrawCircle(xPoint, yPoint, (themeData.defaultSmallTextSize>>1)+2, {255,220,50,80});
    DrawCircle(xPoint, yPoint, themeData.defaultSmallTextSize>>1, {255,220,0,255});
    DrawText("$", xPoint-5, yPoint-(themeData.defaultXSmallTextSize>>1),
             themeData.defaultXSmallTextSize, {30,20,0,255});
  }

  // ─── Other players ──────────────────────────────────────
  {
    _posAccessLock.lock();
    auto posCopy = peerPositions;
    _posAccessLock.unlock();
    for (auto &[pid, pos] : posCopy){
      if (pid == myID) continue;
      Color c = PEER_COLORS[pid % 6];
      // shadow
      DrawCircle(pos.x+3, pos.y+3, themeData.defaultTextSize, {0,0,0,80});
      // glow
      DrawCircle(pos.x, pos.y, themeData.defaultTextSize+4, {c.r,c.g,c.b,40});
      DrawCircle(pos.x, pos.y, themeData.defaultTextSize, c);
      // label
      std::string label = "P" + std::to_string(pid);
      int lw = MeasureText(label.c_str(), themeData.defaultXSmallTextSize);
      DrawText(label.c_str(), pos.x-(lw>>1), pos.y-themeData.defaultTextSize-themeData.defaultXSmallTextSize-2,
               themeData.defaultXSmallTextSize, c);
    }
  }

  // ─── My Player ──────────────────────────────────────────
  {
    Color myC = PEER_COLORS[myID % 6];
    // smooth interpolation
    floatX += ((float)x - floatX) * 0.25f;
    floatY += ((float)y - floatY) * 0.25f;
    int rx = (int)floatX, ry = (int)floatY;
    // shadow + glow
    DrawCircle(rx+3, ry+3, themeData.defaultTextSize, {0,0,0,80});
    DrawCircle(rx, ry, themeData.defaultTextSize+6, {myC.r,myC.g,myC.b,50});
    DrawCircle(rx, ry, themeData.defaultTextSize, myC);
    DrawText("YOU", rx-14, ry-themeData.defaultTextSize-16, themeData.defaultXSmallTextSize, myC);
    // publish my position
    _posAccessLock.lock(); peerPositions[myID] = {rx, ry}; _posAccessLock.unlock();
  }

  // ─── Particles ──────────────────────────────────────────
  updateParticles(dt);
  for (auto &p : particles){
    DrawCircleV({p.x,p.y}, 4.0f*p.life, p.color);
  }

  // ─── Processing ─────────────────────────────────────────
  {
    runMainGameP2p();
    if (currentTime<=0){
      currentScreen = 6;
      stopMainGameP2p();
      particles.clear();
      return;
    }

    static int rankUpdatorStarted = false;
    if (!rankUpdatorStarted){
      rankUpdatorStarted = true;
      std::thread rankThread([&](){
        while(currentScreen==5){
          int betterThanMe = 0;
          _scoreAccessLock.lock();
          for (auto &s: scores) if (s>scores[myID]) betterThanMe++;
          _scoreAccessLock.unlock();
          _myCurrentRank = betterThanMe+1;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      });
      rankThread.detach();
    }
  }

  // ─── Inputs ─────────────────────────────────────────────
  {
    int speed = 10;
    bool moved = false;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) { y = std::max(themeData.defaultTitleSize+(themeData.defaultPadding<<1), y-speed); moved=true; }
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) { y = std::min(screenHeight-themeData.defaultPadding, y+speed); moved=true; }
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) { x = std::max(themeData.defaultPadding, x-speed); moved=true; }
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) { x = std::min(screenWidth-200-themeData.defaultPadding, x+speed); moved=true; }

    (void)moved;  // suppress unused warning

    // Coin collection check
    if (distance(x,y,xPoint,yPoint) <= (themeData.defaultTextSize+(themeData.defaultSmallTextSize>>1))){
      Color myC = PEER_COLORS[myID%6];
      spawnParticles(xPoint, yPoint, {255,215,0,255});
      spawnParticles(xPoint, yPoint, myC);
      xPoint = GetRandomValue(themeData.defaultPadding, screenWidth-200-themeData.defaultPadding);
      yPoint = GetRandomValue(themeData.defaultTitleSize+(themeData.defaultPadding<<1), screenHeight-themeData.defaultPadding);
      _scoreAccessLock.lock(); scores[myID]++; _scoreAccessLock.unlock();
      shownPoints++;
    }
  }
}
