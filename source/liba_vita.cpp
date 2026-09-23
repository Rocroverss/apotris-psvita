#ifdef VITA

#include "liba_vita.h"
#include <fstream>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <sys/stat.h>
#include <unordered_set>

#include "def.h"
#include "scene.hpp"

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
#define PRESENTATION_DIVISOR 2

static uint64_t frame_start = nanotime_now();

float fps = 0;

bool render = true;

// Vita/PSTV native resolution.
int screenWidth = 960;
int screenHeight = 544;

// Present directly through SceDisplay. SDL's Vita renderer uploads the entire
// software frame to a GL texture and draws a scaled quad every frame. That is
// disproportionately expensive for this small, CPU-rendered 240x160 game.
// Scan out at the Vita panel's native 960x544 resolution. 816x544 is the
// largest 3:2 image that fits the panel, so it preserves Apotris's 240x160
// aspect ratio instead of stretching it to the Vita's wider 30:17 panel.
static constexpr int DISPLAY_WIDTH = 960;
static constexpr int DISPLAY_HEIGHT = 544;
static constexpr int DISPLAY_PITCH = 960;
static constexpr int CONTENT_WIDTH = 816;
static constexpr int CONTENT_HEIGHT = 544;
static constexpr int CONTENT_X = (DISPLAY_WIDTH - CONTENT_WIDTH) / 2;
static constexpr int CONTENT_Y = (DISPLAY_HEIGHT - CONTENT_HEIGHT) / 2;
static constexpr int DISPLAY_BUFFER_BYTES =
    DISPLAY_PITCH * DISPLAY_HEIGHT * (int)sizeof(uint32_t);
static constexpr int DISPLAY_MEMBLOCK_BYTES =
    (DISPLAY_BUFFER_BYTES + 0x3ffff) & ~0x3ffff;

static SceUID displayMemblocks[2] = {-1, -1};
static uint32_t* displayBuffers[2] = {nullptr, nullptr};
static int displayBufferIndex = 0;
static unsigned presentationCounter = 0;
static uint16_t horizontalSource[CONTENT_WIDTH];

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

    // SDL remains responsible only for joystick events and audio. Avoiding
    // SDL_INIT_VIDEO prevents vitaGL and its presentation shaders from being
    // initialized or used.
    SDL_Init(SDL_INIT_JOYSTICK);
    SDL_JoystickEventState(SDL_ENABLE);
    SDL_JoystickOpen(0);

    for (int i = 0; i < 2; ++i) {
        displayMemblocks[i] = sceKernelAllocMemBlock(
            "ApotrisDisplay", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
            DISPLAY_MEMBLOCK_BYTES, nullptr);
        if (displayMemblocks[i] >= 0 &&
            sceKernelGetMemBlockBase(displayMemblocks[i],
                                     (void**)&displayBuffers[i]) >= 0) {
            memset(displayBuffers[i], 0, DISPLAY_BUFFER_BYTES);
        } else {
            displayBuffers[i] = nullptr;
        }
    }

    for (int x = 0; x < CONTENT_WIDTH; ++x)
        horizontalSource[x] = (x * SCREEN_WIDTH) / CONTENT_WIDTH;

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
    uint32_t* output = displayBuffers[displayBufferIndex];
    const bool presentThisTick =
        (++presentationCounter % PRESENTATION_DIVISOR) == 0;
    if (presentThisTick && output != nullptr && render) {
        const uint32_t* source = reinterpret_cast<const uint32_t*>(framebuffer);
        int previousSourceY = -1;
        for (int y = 0; y < CONTENT_HEIGHT; ++y) {
            uint32_t* dst = output + (y + CONTENT_Y) * DISPLAY_PITCH + CONTENT_X;
            const int sourceY = (y * SCREEN_HEIGHT) / CONTENT_HEIGHT;

            // Consecutive destination rows often select the same source row;
            // copy the already-expanded row instead of expanding it again.
            if (sourceY == previousSourceY) {
                memcpy(dst - CONTENT_X, dst - CONTENT_X - DISPLAY_PITCH,
                       DISPLAY_WIDTH * 4);
                continue;
            }

            const uint32_t* src = source + sourceY * SCREEN_WIDTH;
            for (int x = 0; x < CONTENT_WIDTH; ++x)
                dst[x] = src[horizontalSource[x]];

            // Extend the menu/game background into the side areas without
            // mirroring gameplay objects or stretching the 3:2 canvas.
            const uint16_t background = gradientTable[sourceY];
            uint32_t leftBackgroundPixel =
                ((background & 0x1f) << 3) |
                (((background >> 5) & 0x1f) << 11) |
                (((background >> 10) & 0x1f) << 19);
            uint32_t rightBackgroundPixel = leftBackgroundPixel;
#ifndef MULTIBOOT
            if (dynamic_cast<MainMenuScene*>(scene) != nullptr) {
                // The main menu has its own composed background. Continue its
                // actual outermost pixels into the widescreen area instead of
                // showing the gameplay gradient there.
                leftBackgroundPixel = src[0];
                rightBackgroundPixel = src[SCREEN_WIDTH - 1];
            }
#endif
            for (int x = 0; x < CONTENT_X; ++x) {
                dst[-1 - x] = leftBackgroundPixel;
                dst[CONTENT_WIDTH + x] = rightBackgroundPixel;
            }
            previousSourceY = sourceY;
        }

        SceDisplayFrameBuf displayFrame{};
        displayFrame.size = sizeof(displayFrame);
        displayFrame.base = output;
        displayFrame.pitch = DISPLAY_PITCH;
        displayFrame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        displayFrame.width = DISPLAY_WIDTH;
        displayFrame.height = DISPLAY_HEIGHT;
        sceDisplaySetFrameBuf(&displayFrame, SCE_DISPLAY_SETBUF_NEXTFRAME);
        displayBufferIndex ^= 1;
    }

    nanotime_step(&stepper);

    uint64_t end = nanotime_now();

    float elapsedMS = (end - frame_start) / 1000000.0f;

    if (elapsedMS > 100) {
        nanotime_step_init(&stepper,
                           (uint64_t)(NANOTIME_NSEC_PER_SEC / FPS_TARGET),
                           nanotime_now_max(), nanotime_now, nanotime_sleep);
    }

    // Report the visible presentation rate. Simulation and input remain at
    // 60 Hz so a 30 FPS cap does not slow the game down.
    fps = 1000.0f / elapsedMS / PRESENTATION_DIVISOR;

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

std::map<int, std::string> keyToString = {
    {JOY_SELECT, "Select"}, {JOY_L3, "L3"},        {JOY_R3, "R3"},
    {JOY_START, "Start"},   {JOY_UP, "Up"},        {JOY_RIGHT, "Right"},
    {JOY_DOWN, "Down"},     {JOY_LEFT, "Left"},    {JOY_L, "L"},
    {JOY_R, "R"},           {JOY_TRIANGLE, "Triangle"},
    {JOY_CIRCLE, "Circle"}, {JOY_CROSS, "Cross"}, {JOY_SQUARE, "Square"},
    {JOY_L2, "L2"},         {JOY_R2, "R2"},
};

void quit() {
    sceDisplaySetFrameBuf(nullptr, SCE_DISPLAY_SETBUF_IMMEDIATE);
    for (int i = 0; i < 2; ++i) {
        if (displayMemblocks[i] >= 0)
            sceKernelFreeMemBlock(displayMemblocks[i]);
    }
    SDL_Quit();
    freeAudio();
    exit(0);
}

#endif
