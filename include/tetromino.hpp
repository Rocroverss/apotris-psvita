#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <string>

namespace GameInfo {
extern const int colors[7][3];

extern const uint16_t tetrominos[7][4][4][4];

extern const uint16_t bars[7][4][4][4];

extern const uint16_t classic[7][4][4][4];

extern const uint16_t sdrs[7][4][4][4];

extern const uint16_t ars[7][4][4][4];

extern const int kickTwice[4][6][2];

extern const int barsKickTwice[4][8][2];

extern const int8_t kicks[2][2][4][5][2];

extern const int8_t barsKicks[2][2][4][7][2];

extern const int sdrsKicks[2][2][4][10][2];

extern const int arikaKicks[];

extern const float gravity[19];

extern const float classicGravity[30];

extern const float blitzGravity[15];

extern const int blitzLevels[15];

extern const int scoring[17][3];

extern const int classicScoring[4];

extern const int comboTable[20];

extern const uint8_t finesse[7][4][10][4];

extern const uint16_t masterDelays[9][5];

extern const float masterGravity[30][2];

extern const uint16_t deathDelays[14][5];

extern const uint8_t deathQuota[5];

extern const int sectionTimeGoal[10][2];

extern const char* masterGrades[33];

extern const char* deathGrades[14];

extern const char* secretGrades[10];

extern const uint8_t gradeTable[32][6];

extern const float masterComboMultiplayer[10][4];

extern const float masterComboMultiplayer[10][4];

extern const float creditPoints[5][2];

extern const uint16_t connectedConversion[24];

extern const uint16_t connectedFix[3][24];

extern const std::list<std::string> credits;

extern const std::map<std::string, int> base64Decode;
extern const std::map<int, std::string> base64Encode;

extern const uint8_t fumenBlocksDecode[9];
extern const uint8_t fumenBlocksEncode[9];
} // namespace GameInfo
