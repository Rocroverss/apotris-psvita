#pragma once

#include <cinttypes>

#include "audio_files.h"
#include "liba_pc.h"

#include "SDL.h"
#include "SDL_gamecontroller.h"
#include "SDL_mixer.h"
#include "glad/gles2.h"

#include <map>

extern int KEY_A;
extern int KEY_B;
extern int KEY_L;
extern int KEY_R;
extern int KEY_UP;
extern int KEY_DOWN;
extern int KEY_LEFT;
extern int KEY_RIGHT;
extern int KEY_SELECT;
extern int KEY_START;

#define KEY_FULL 0xffffffff

extern void updateWindow(uint8_t*);
extern void refreshWindowSize();
extern bool closed();

extern void windowInit();

std::string getDocumentsPath();

extern void key_poll();

extern void startTextInput();
extern void stopTextInput();
extern std::string consumeTextInput();

extern void setFullscreen(bool);
extern void shaderInit(int);
extern void shaderDeinit();

extern uint32_t key_is_down(uint32_t);
extern uint32_t key_hit(uint32_t);
extern uint32_t key_released(uint32_t);
extern uint32_t key_first();
extern uint32_t keys_raw();

extern void setKey(int&, uint32_t);
extern void unbindDuplicateKey(int&, uint32_t);

extern int splitKey(uint32_t key);

extern float windowScale;
extern int rowStart;
extern int rowEnd;

extern std::map<int, std::string> keyToString;

std::string stringFromKey(uint32_t key);

enum class InputType { KEYBOARD, CONTROLLER, TOUCH };

extern InputType lastInputType;

INLINE u16 packKey(SDL_Keycode key) {
    return (key & 0xfff) | (((key & 0xf0000000) >> 16));
}

INLINE SDL_Keycode unpackKey(u16 key) {
    return (SDL_Keycode)(key & 0xfff) | (((key & 0xf000) << 16));
}

class Button {
public:
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int key = 0;

    Button(int key, int x, int y, int w, int h) {
        this->key = key;
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
    };

    Button() {}
};

class Layout {
public:
    Button buttons[10];

    Layout(Button* buttons) {
        memcpy(this->buttons, buttons, sizeof(Button) * 10);
    }

    Layout() {}
};

extern void setGameWindowScale();

extern std::vector<Layout> getDefaultLayouts();

extern void setLayout();

extern bool touchEditMode;
extern bool touchEditExitRequested;
