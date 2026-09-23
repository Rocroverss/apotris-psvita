#pragma once

#include <cinttypes>

#include "audio_files.h"
#include "liba_pc.h"

#include "SDL.h"

#include <map>

// NOTE: these indices assume VitaSDK's SDL2 joystick mapping for the
// built-in Vita controls (button order as exposed by SDL_JOYBUTTONDOWN on
// the "SCE PSVITA Controller" joystick). Verify against your VitaSDK SDL2
// version on real hardware / hardware-equivalent emulator (Vita3K) before
// relying on this - mappings have shifted between VitaSDK SDL2 releases.
#define JOY_SELECT   0
#define JOY_L3       1
#define JOY_R3       2
#define JOY_START    3
#define JOY_UP       4
#define JOY_RIGHT    5
#define JOY_DOWN     6
#define JOY_LEFT     7
#define JOY_L        8
#define JOY_R        9
#define JOY_TRIANGLE 10
#define JOY_CIRCLE   11
#define JOY_CROSS    12
#define JOY_SQUARE   13

#define KEY_A JOY_CROSS
#define KEY_B JOY_CIRCLE
#define KEY_L JOY_L
#define KEY_R JOY_R
#define KEY_UP JOY_UP
#define KEY_DOWN JOY_DOWN
#define KEY_LEFT JOY_LEFT
#define KEY_RIGHT JOY_RIGHT
#define KEY_SELECT JOY_SELECT
#define KEY_START JOY_START
#define KEY_FULL 0xffffffff

extern void updateWindow(uint8_t*);
extern void refreshWindowSize();
extern bool closed();

extern void sfx(int);
extern void startSong(int, bool);
extern void stopSong();
extern void resumeSong();
extern void setMusicVolume(int);
extern void setMusicTempo(int);
extern void toggleRendering(bool);
extern void sfxRate(int, float);

extern void pauseSong();

extern void windowInit();

extern void key_poll();

extern uint32_t key_is_down(uint32_t);
extern uint32_t key_hit(uint32_t);
extern uint32_t key_released(uint32_t);
extern uint32_t key_first();
extern uint32_t keys_raw();

extern float windowScale;
extern int rowStart;
extern int rowEnd;

extern std::map<int, std::string> keyToString;
