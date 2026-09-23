#pragma once

#include <cinttypes>

#include "audio_files.h"
#include "liba_pc.h"

#include "SDL.h"

#include <map>

// These indices match VitaSDK SDL2's actual `ext_button_map` in
// SDL_sysjoystick.c (src/joystick/vita/), which assigns SDL joystick
// button indices in this fixed order - NOT by walking the raw SCE_CTRL_*
// bitmask (an earlier draft of this file guessed the latter, which is why
// controls were scrambled: the SELECT/START/face-button offsets were
// wrong and the D-pad order isn't UP/RIGHT/DOWN/LEFT, it's DOWN/LEFT/UP/RIGHT).
#define JOY_TRIANGLE 0
#define JOY_CIRCLE   1
#define JOY_CROSS    2
#define JOY_SQUARE   3
#define JOY_L        4
#define JOY_R        5
#define JOY_DOWN     6
#define JOY_LEFT     7
#define JOY_UP       8
#define JOY_RIGHT    9
#define JOY_SELECT   10
#define JOY_START    11
#define JOY_L2       12
#define JOY_R2       13
#define JOY_L3       14
#define JOY_R3       15

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
