#ifdef VITA

#include "liba_vita.h"
#include <fstream>
#include <sys/stat.h>
#include <unordered_set>

#include "def.h"

#include "nanotime.h"

// NOTE (porting): the original PS Vita / PSTV have no vibration motor, so
// rumble is a deliberate no-op here (unlike liba_switch.cpp, which drives
// real HD rumble). rumbleUpdate() still runs the pattern state machine so
// timers stay in sync with the rest of the game logic, it just never
// reaches actual hardware.
void rumbleUpdate();

void handleInput();

std::unordered_set<uint32_t> currentKeys;
std::unordered_set<uint32_t> previousKeys;

std::unordered_set<uint32_t> currentlyPressed;

int rowStart = 0;
int rowEnd = SCREEN_HEIGHT;

#define FPS_TARGET 60

static uint64_t frame_start = nanotime_now();

float fps = 0;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;

bool render = true;

// Vita/PSTV native resolution.
int screenWidth = 960;
int screenHeight = 544;

float windowScale = 4;

nanotime_step_data stepper;

// Writable save location. app0: (where the vpk's own files live) is
// read-only at runtime, so saves must go to ux0:. This directory is
// created in windowInit() if it doesn't already exist.
#define VITA_SAVE_DIR "ux0:data/Apotris"
#define VITA_SAVE_PATH "ux0:data/Apotris/Apotris.sav"

void windowInit() {
    // Make sure our save directory exists on ux0: before anything tries to
    // read/write to it.
    mkdir(VITA_SAVE_DIR, 0777);

    window = NULL;
    window = SDL_CreateWindow("Apotris", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, screenWidth,
                              screenHeight, SDL_WINDOW_SHOWN);

    renderer = SDL_CreateRenderer(window, -1, 0);

    // VitaSDK's SDL2 exposes the Vita's buttons as joystick 0. Confirm the
    // button index mapping in liba_vita.h against your VitaSDK/SDL2
    // version -- it has not been verified against real hardware yet.
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_JoystickOpen(0);

    loadAudio("");

    nanotime_step_init(&stepper, (uint64_t)(NANOTIME_NSEC_PER_SEC / FPS_TARGET),
                       nanotime_now_max(), nanotime_now, nanotime_sleep);
}

void key_poll() {}

void handleInput() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        uint32_t key = 0;

        switch (event.type) {
        case SDL_JOYBUTTONDOWN:
            key = event.jbutton.button;
            currentlyPressed.insert(key);
            break;
        case SDL_JOYBUTTONUP:
            key = event.jbutton.button;
            currentlyPressed.erase(key);
            break;
        case SDL_QUIT:
            quit();
            break;
        default:
            break;
        }
    }

    previousKeys = currentKeys;
    currentKeys.clear();

    currentKeys = currentlyPressed;
}

uint32_t key_is_down(uint32_t key) {
    if (key == KEY_FULL)
        return (!currentKeys.empty());

    return currentKeys.count(key);
}

uint32_t key_hit(uint32_t key) {
    if (key == KEY_FULL)
        return (previousKeys.empty() && !currentKeys.empty());

    bool prev = previousKeys.count(key);
    bool curr = currentKeys.count(key);

    return (!prev && curr);
}

uint32_t key_released(uint32_t key) {
    if (key == KEY_FULL)
        return (!previousKeys.empty() && currentKeys.empty());

    bool prev = previousKeys.count(key);
    bool curr = currentKeys.count(key);

    return (prev && !curr);
}

uint32_t key_first() {
    if (currentKeys.empty())
        return KEY_FULL - 1;

    for (auto key : currentKeys) {
        bool prev = previousKeys.count(key);

        if (!prev) {
            return key;
        }
    }

    return KEY_FULL - 1;
}

uint32_t keys_raw() {
    if (currentKeys.empty())
        return KEY_FULL - 1;

    uint32_t allKeys = KEY_FULL;
    for (auto key : currentKeys) {
        allKeys ^= key;
    }

    return allKeys;
}

void updateWindow(uint8_t* framebuffer) {
    const int in_width = 512;
    const int in_height = 512;

    SDL_Surface* img = SDL_CreateRGBSurfaceFrom(
        framebuffer, in_width, in_height, 32, in_width * 4, 0x0000ff, 0x00ff00,
        0xff0000, 0xff000000);

    if (img == NULL) {
        printf("Failed to create SDL_Surface: %s\n", SDL_GetError());
        return;
    }

    if (texture == NULL) {
        texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                              SDL_TEXTUREACCESS_STREAMING, in_width, in_height);
        if (texture == NULL) {
            printf("Failed to create texture: %s\n", SDL_GetError());
            SDL_FreeSurface(img);
            return;
        }
    }

    void* pixels;
    int pitch;
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
        printf("Failed to lock texture: %s\n", SDL_GetError());
        SDL_FreeSurface(img);
        return;
    }

    for (int y = 0; y < in_height; y++) {
        memcpy((uint8_t*)pixels + y * pitch, (uint8_t*)img->pixels + y * img->pitch,
               in_width * 4);
    }

    SDL_UnlockTexture(texture);
    SDL_FreeSurface(img);

    SDL_RenderClear(renderer);

    SDL_Rect src_rect = {0, 0, in_width, in_height};
    const int w = in_width * windowScale;
    const int h = in_height * windowScale;
    SDL_Rect dest_rect = {-(w - screenWidth) / 2, -(h - screenHeight) / 2, w,
                          h};

    if (render) {
        SDL_RenderCopy(renderer, texture, &src_rect, &dest_rect);
    }

    SDL_RenderPresent(renderer);

    nanotime_step(&stepper);

    uint64_t end = nanotime_now();

    float elapsedMS = (end - frame_start) / 1000000.0f;

    if (elapsedMS > 100) {
        nanotime_step_init(&stepper,
                           (uint64_t)(NANOTIME_NSEC_PER_SEC / FPS_TARGET),
                           nanotime_now_max(), nanotime_now, nanotime_sleep);
    }

    fps = 1000.0f / elapsedMS;

    frame_start = nanotime_now();

    handleInput();
}

void refreshWindowSize() {
    if (savefile != nullptr) {
        if (savefile->settings.zoom > -1) {
            windowScale = 1 + (float)savefile->settings.zoom / 10;
        } else {
            if (savefile->settings.integerScale) {
                windowScale = (int)windowScale;

                while (windowScale > 0 &&
                       screenHeight / windowScale > 160 * 2) {
                    windowScale++;
                }
                while (windowScale > 1 && screenHeight / windowScale < 160) {
                    windowScale--;
                }
            } else {
                windowScale = screenHeight / 200.0;
            }
        }
    }

    if (windowScale <= 0)
        return;

    rowStart = (SCREEN_HEIGHT - (screenHeight / windowScale)) / 2;
    rowEnd = (SCREEN_HEIGHT + (screenHeight / windowScale)) / 2;

    if (rowStart < 0)
        rowStart = 0;

    if (rowEnd > SCREEN_HEIGHT)
        rowEnd = SCREEN_HEIGHT;

    if (savefile != nullptr)
        setGradient(savefile->settings.backgroundGradient);
}

bool closed() {
    songEndHandler();
    rumbleUpdate();

    return true;
}

void toggleRendering(bool r) { render = r; }

// No vibration hardware on Vita/PSTV -- intentionally a no-op. Kept as a
// real function (rather than removed) so the rest of the shared game code
// doesn't need any additional #ifdef VITA guards around rumble calls.
void initRumble() {}

void loadSavefile() {
    std::ifstream input(VITA_SAVE_PATH, std::ios::binary | std::ios::in);

    if (savefile == nullptr)
        savefile = new Save();

    char* src = (char*)savefile;

    input.read(src, sizeof(Save));

    if (!input) {
        log("Error when trying to load save.");
        return;
    }

    input.close();
}

void saveSavefile() {
    std::ofstream output(VITA_SAVE_PATH, std::ios::binary | std::ios::out);

    char* dst = (char*)savefile;

    const int saveSize = 1 << 15;

    char temp[saveSize];

    memset32_fast(temp, 0, saveSize / 4);
    memcpy32_fast(temp, dst, sizeof(Save) / 4);

    output.write(temp, saveSize);

    if (!output) {
        log("Error when trying to write save.");
        return;
    }

    output.close();
}

void rumbleOutput(uint16_t strength) { (void)strength; }

void rumbleUpdate() { rumblePatternLoop(); }

void rumbleStop() {}

// Same caveat as liba_vita.h: verify these button-name labels against the
// real SDL joystick button indices on hardware before shipping.
std::map<int, std::string> keyToString = {
    {JOY_SELECT, "Select"}, {JOY_L3, "L3"},        {JOY_R3, "R3"},
    {JOY_START, "Start"},   {JOY_UP, "Up"},        {JOY_RIGHT, "Right"},
    {JOY_DOWN, "Down"},     {JOY_LEFT, "Left"},    {JOY_L, "L"},
    {JOY_R, "R"},           {JOY_TRIANGLE, "Triangle"},
    {JOY_CIRCLE, "Circle"}, {JOY_CROSS, "Cross"}, {JOY_SQUARE, "Square"},
};

void quit() {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    freeAudio();
    exit(0);
}

#endif
