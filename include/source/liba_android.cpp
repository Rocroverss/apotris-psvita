#ifdef ANDROID

#include "liba_android.h"
#include <SDL_system.h>
#include <algorithm>
#include <android/log.h>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <glad/gles2.h>
#include <iostream>
#include <jni.h>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>

#include "version.h"
#include <unistd.h>

#define LOG_TAG "ApotrisAndroid"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#include "def.h"

#include "scene.hpp"

#include "shader.h"

#include "nanotime.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

static bool imguiInited = false;
static bool imguiGLInited = false;
static bool imguiSDLInited = false;

static bool inBackground = false;
static bool pendingBackground = false;

static jobject vibrateService = nullptr;
static jmethodID vibrateMethodId = 0;
static jmethodID vibrateCancelMethodId = 0;

void handleAnalogInput(int axis, int value);
void handleInput();
void updateBatteryInfo();
void setLayout();

bool vibrationInit();
void vibrationStart(int durationMS);
void vibrationStop();
void vibrationDeinit();

std::unordered_set<uint32_t> currentKeys;
std::unordered_set<uint32_t> previousKeys;

std::unordered_set<uint32_t> currentlyPressed;

static std::string textInput;
static bool textInputActive = false;

void startTextInput() {
    textInput.clear();
    textInputActive = true;
    SDL_StartTextInput();
}

void stopTextInput() {
    SDL_StopTextInput();
    textInput.clear();
    textInputActive = false;
}

std::string consumeTextInput() {
    std::string result = textInput;
    textInput.clear();
    return result;
}

// Per-finger tracking for touch input: fingerId -> button key.
// Ensures a finger's button is unpressKey'd on FINGERUP even if the
// finger has moved outside the button bounds or the event is lost
// (Android ACTION_CANCEL / SDL multi-touch bugs).
std::unordered_map<SDL_FingerID, std::unordered_set<uint32_t>>
    activeFingerButtons;

std::list<SDL_Rect> rects;

// int rowStart = (288 - 288) / 2;
// int rowEnd = rowStart + 288;
int rowStart = 0;
int rowEnd = SCREEN_HEIGHT;

#define FPS_TARGET 60

double clock_timer = 0;

static uint64_t frame_start = nanotime_now();

uint32_t start_time = 0;
uint32_t frame_time = 0;

float fps = 0;

int batteryPercentage = -1;
bool charging = false;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
SDL_GameController* controller;

Layout layout;

int controllerCount = 0;

bool fullscreen = false;

bool render = true;

int screenWidth = 1280;
int screenHeight = 720;

float windowScale = 6;

float gameWindowScale = 6;

InputType lastInputType = InputType::TOUCH;

nanotime_step_data stepper;

ShaderStatus shaderStatus = ShaderStatus::NOT_INITED;

SDL_Point touchLocation = {0, 0};

bool touchEditMode = false;
bool touchEditExitRequested = false;

int moving = 0;
int resizing = -1;
int resizeCorner = -1;
int resizeFixedX = 0;
int resizeFixedY = 0;

#define DEADZONE 12000

static inline float touchScale() {
    int s = std::min(screenWidth, screenHeight);
    return s / 1080.0f;
}

static inline int scaled(int v) { return (int)std::round(v * touchScale()); }

static inline int cornerGrabRadius() { return scaled(60); }
static inline int minButtonSize() { return scaled(40); }

int KEY_A = (SDL_CONTROLLER_BUTTON_A << 16) | packKey(SDLK_RETURN);
int KEY_B = (SDL_CONTROLLER_BUTTON_B << 16) | packKey(SDLK_BACKSPACE);
int KEY_L = (SDL_CONTROLLER_BUTTON_LEFTSHOULDER << 16) | packKey(SDLK_1);
int KEY_R = (SDL_CONTROLLER_BUTTON_RIGHTSHOULDER << 16) | packKey(SDLK_2);
int KEY_UP = (SDL_CONTROLLER_BUTTON_DPAD_UP << 16) | packKey(SDLK_w);
int KEY_DOWN = (SDL_CONTROLLER_BUTTON_DPAD_DOWN << 16) | packKey(SDLK_s);
int KEY_LEFT = (SDL_CONTROLLER_BUTTON_DPAD_LEFT << 16) | packKey(SDLK_a);
int KEY_RIGHT = (SDL_CONTROLLER_BUTTON_DPAD_RIGHT << 16) | packKey(SDLK_d);
int KEY_SELECT = (SDL_CONTROLLER_BUTTON_BACK << 16) | packKey(SDLK_3);
int KEY_START = (SDL_CONTROLLER_BUTTON_START << 16) | packKey(SDLK_ESCAPE);

// The preferred storage path is saved in internal storage so on
// subsequent launches we don't need to re-probe.
static std::string storageConfigPath() {
    const char* internal = SDL_AndroidGetInternalStoragePath();
    return internal
               ? std::string(internal) + "/storage_path.txt"
               : "/data/data/com.akouzoukos.apotris/files/storage_path.txt";
}

static void saveStoragePath(const std::string& path) {
    std::string cfg = storageConfigPath();
    size_t pos = cfg.find_last_of('/');
    if (pos != std::string::npos)
        mkdir(cfg.substr(0, pos).c_str(), 0700);

    FILE* f = fopen(cfg.c_str(), "w");
    if (f) {
        fprintf(f, "%s\n", path.c_str());
        fclose(f);
    }
}

static std::string loadStoragePath() {
    FILE* f = fopen(storageConfigPath().c_str(), "r");
    if (!f)
        return "";
    char buf[512];
    std::string result;
    if (fgets(buf, sizeof(buf), f)) {
        result = buf;
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
    }
    fclose(f);
    return result;
}

// ── MANAGE_EXTERNAL_STORAGE helpers (API 30+) ─────────────────────────

static bool hasManageStoragePermission() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env)
        return false;
    jclass activityClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (!activityClass)
        return false;
    jmethodID mid = env->GetStaticMethodID(activityClass,
                                           "hasManageStoragePermission", "()Z");
    if (!mid) {
        env->DeleteLocalRef(activityClass);
        return false;
    }
    jboolean result = env->CallStaticBooleanMethod(activityClass, mid);
    env->DeleteLocalRef(activityClass);
    return result == JNI_TRUE;
}

static void requestManageStoragePermission() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) {
        LOGE("Cannot request MANAGE_EXTERNAL_STORAGE: JNIEnv is null");
        return;
    }
    jclass activityClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (!activityClass) {
        LOGE("Cannot find SDLActivity class");
        return;
    }
    jmethodID mid = env->GetStaticMethodID(
        activityClass, "requestManageStoragePermission", "()V");
    if (!mid) {
        LOGE("Cannot find requestManageStoragePermission method");
        env->DeleteLocalRef(activityClass);
        return;
    }
    env->CallStaticVoidMethod(activityClass, mid);
    env->DeleteLocalRef(activityClass);
}

// Tests whether we can actually own the marker file inside the given
// Apotris directory — creating it if fresh, or reclaiming it if
// orphaned from a previous installation.
// Returns true if the directory is usable (marker is writable).
static bool tryClaimDirectory(const std::string& apotrisDir) {
    mkdir(apotrisDir.c_str(), 0777);

    std::string assetsDir = apotrisDir + "assets";
    mkdir(assetsDir.c_str(), 0777);

    std::string marker = assetsDir + "/extracted.txt";
    struct stat st;
    bool exists = (stat(marker.c_str(), &st) == 0 && S_ISREG(st.st_mode));

    // Write placeholder so extractAssets() knows extraction is needed.
    // (extractAssets checks for APOTRIS_VERSION; a non-matching value
    //  forces it to extract the actual assets.)
    FILE* f = fopen(marker.c_str(), exists ? "a" : "w");
    if (f) {
        if (!exists)
            fprintf(f, "0\n");
        fclose(f);
        return true;
    }

    // File exists but is owned by another UID (orphaned).
    // Try to delete and recreate.
    if (exists && remove(marker.c_str()) == 0) {
        f = fopen(marker.c_str(), "w");
        if (f) {
            fprintf(f, "0\n");
            fclose(f);
            return true;
        }
    }

    return false;
}

std::string getDocumentsPath() {
    std::string saved = loadStoragePath();
    if (!saved.empty() && tryClaimDirectory(saved)) {
        return saved;
    }

    // Probe Apotris, Apotris (1), Apotris (2), … in Documents/.
    const char* base = "/storage/emulated/0/Documents/Apotris";

    // Try the un-numbered directory first.
    std::string candidate = std::string(base) + "/";
    if (tryClaimDirectory(candidate)) {
        saveStoragePath(candidate);
        return candidate;
    }

    // Try numbered variants.
    for (int counter = 1; counter <= 99; counter++) {
        candidate = std::string(base) + " (" + std::to_string(counter) + ")/";
        if (tryClaimDirectory(candidate)) {
            saveStoragePath(candidate);
            return candidate;
        }
    }

    // Fallback: app-private external storage.
    const char* extStorage = SDL_AndroidGetExternalStoragePath();
    if (extStorage) {
        std::string fallback = std::string(extStorage) + "/Apotris/";
        if (tryClaimDirectory(fallback)) {
            saveStoragePath(fallback);
            return fallback;
        }
    }

    return std::string(base) + "/";
}

// Returns true when the resolved path is not in the user-accessible
// Documents folder.
static bool isUsingFallbackPath(const std::string& path) {
    return path.find("/Documents/Apotris") == std::string::npos;
}

static void make_directory_recursive(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find_first_of('/', pos)) != std::string::npos) {
        std::string subpath = path.substr(0, pos);
        if (!subpath.empty()) {
            mkdir(subpath.c_str(), 0777);
        }
        pos++;
    }
    mkdir(path.c_str(), 0777);
}

bool scopedStorageError = false;

static bool assetsAreExtracted(const std::string& pathPrefix) {
    for (int i = 0; i < SFX_COUNT; i++) {
        if (access((pathPrefix + SoundEffectPaths[i]).c_str(), F_OK) != 0)
            return false;
    }

    size_t listSize;
    void* listData = SDL_LoadFile("assets/file_list.txt", &listSize);
    if (!listData)
        return false;

    std::string listStr((char*)listData, listSize);
    SDL_free(listData);
    std::stringstream ss(listStr);
    std::string line;
    while (std::getline(ss, line)) {
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        if (line.empty())
            continue;

        if (access((pathPrefix + "assets/" + line).c_str(), F_OK) != 0) {
            return false;
        }
    }

    return true;
}

void extractAssets(const std::string& pathPrefix) {
    std::string assetsDir = pathPrefix + "assets";
    mkdir(assetsDir.c_str(), 0777);

    std::string markerPath = pathPrefix + "assets/extracted.txt";
    std::string markerVersion = std::string(APOTRIS_VERSION) + ":assets-v2";

    // Check extraction manifest version
    FILE* marker = fopen(markerPath.c_str(), "r");
    bool needsUpdate = true;
    if (marker) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), marker)) {
            // Trim newline and whitespace
            std::string ver = buffer;
            ver.erase(ver.find_last_not_of(" \n\r\t") + 1);
            if (ver == markerVersion && assetsAreExtracted(pathPrefix)) {
                needsUpdate = false;
            }
        }
        fclose(marker);
    }

    if (!needsUpdate) {
        FILE* f = fopen(markerPath.c_str(), "a");
        if (f) {
            fclose(f);
        } else {
            // Can't append to the marker — probably orphaned from a
            // previous app install.  Try to delete+recreate so it's
            // owned by the current UID.
            if (remove(markerPath.c_str()) == 0) {
                f = fopen(markerPath.c_str(), "w");
                if (f) {
                    fprintf(f, "%s\n", markerVersion.c_str());
                    fclose(f);
                }
            } else {
                SDL_Log("Marker file exists but cannot be removed "
                        "or updated (likely from a previous install).");
            }
        }
        return;
    }

    SDL_Log("Extracting assets to %s (Version: %s)", assetsDir.c_str(),
            APOTRIS_VERSION);
    bool hasError = false;

    // Helper lambda to extract file
    auto extractFile = [&](const std::string& srcPath,
                           const std::string& destPath) {
        size_t size;
        void* data = SDL_LoadFile(srcPath.c_str(), &size);
        if (data) {
            size_t last_slash = destPath.find_last_of('/');
            if (last_slash != std::string::npos) {
                make_directory_recursive(destPath.substr(0, last_slash));
            }

            // Try to open for writing (overwrite)
            FILE* out = fopen(destPath.c_str(), "wb");
            if (out) {
                size_t written = fwrite(data, 1, size, out);
                if (written != size) {
                    SDL_Log("Failed to write full file: %s", destPath.c_str());
                    hasError = true;
                }
                fclose(out);
            } else {
                // fopen failed.  If the file already exists it may be
                // orphaned from a previous app installation (old Linux
                // UID).  On Android, remove() often succeeds even when
                // fopen("wb") fails, because unlinking requires only
                // write permission on the parent directory.
                if (access(destPath.c_str(), F_OK) == 0) {
                    if (remove(destPath.c_str()) == 0) {
                        // File deleted — retry creation.
                        out = fopen(destPath.c_str(), "wb");
                        if (out) {
                            size_t written = fwrite(data, 1, size, out);
                            if (written != size) {
                                SDL_Log("Failed to write full file: %s",
                                        destPath.c_str());
                                hasError = true;
                            }
                            fclose(out);
                        } else {
                            SDL_Log("Failed to create file after removal: %s",
                                    destPath.c_str());
                            scopedStorageError = true;
                        }
                    } else {
                        // File exists but we can neither overwrite nor
                        // delete it.  Skip (use whatever is on disk).
                        scopedStorageError = true;
                        SDL_Log("Warning: Cannot overwrite or delete "
                                "existing asset: %s (Permission denied?)",
                                destPath.c_str());
                    }
                } else {
                    // File doesn't exist and we can't create it. Real error.
                    SDL_Log("Failed to open file for writing: %s",
                            destPath.c_str());
                    hasError = true;
                }
            }
            SDL_free(data);
        } else {
            SDL_Log("Failed to load asset from APK: %s", srcPath.c_str());
            hasError = true;
        }
    };

    // Extract SFX
    for (int i = 0; i < SFX_COUNT; i++) {
        std::string srcPath = (std::string)SoundEffectPaths[i];
        std::string destPath = pathPrefix + srcPath;
        extractFile(srcPath, destPath);
    }

    // Extract Music
    size_t listSize;
    void* listData = SDL_LoadFile("assets/file_list.txt", &listSize);
    if (listData) {
        std::string listStr((char*)listData, listSize);
        SDL_free(listData);

        std::stringstream ss(listStr);
        std::string line;
        while (std::getline(ss, line)) {
            line.erase(line.find_last_not_of(" \n\r\t") + 1);
            if (line.empty())
                continue;

            std::string srcPath = "assets/" + line;
            std::string destPath = pathPrefix + srcPath;
            extractFile(srcPath, destPath);
        }
    } else {
        SDL_Log("Failed to load file_list.txt from APK");
        hasError = true;
    }

    // Only write the marker file if extraction was successful (ignoring
    // overwrite failures)
    if (!hasError) {
        FILE* outMarker = fopen(markerPath.c_str(), "w");
        if (outMarker) {
            fprintf(outMarker, "%s\n", markerVersion.c_str());
            fclose(outMarker);
            SDL_Log("Asset extraction completed successfully.");
        } else {
            // Marker might be orphaned from a previous install.
            // Try remove + recreate.
            if (remove(markerPath.c_str()) == 0) {
                outMarker = fopen(markerPath.c_str(), "w");
                if (outMarker) {
                    fprintf(outMarker, "%s\n", markerVersion.c_str());
                    fclose(outMarker);
                    SDL_Log("Asset extraction completed (marker "
                            "recreated after previous install).");
                } else {
                    SDL_Log("Warning: Could not write marker file: %s",
                            markerPath.c_str());
                }
            } else {
                SDL_Log("Warning: Could not write or remove marker file: %s",
                        markerPath.c_str());
            }
        }
    } else {
        SDL_Log("Asset extraction completed with errors. Marker not updated.");
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Audio Files Missing",
            "Some audio files failed to extract or are missing. The game will "
            "continue, but audio might not play correctly.",
            NULL);
    }
}

std::string getPreferencesFilePath() {
    return getDocumentsPath() + "preferences.txt";
}

static std::string getPreferencesFilePathFallback() {
    const char* ext = SDL_AndroidGetExternalStoragePath();
    return ext ? std::string(ext) + "/preferences.txt"
               : getDocumentsPath() + "preferences.txt";
}

void savePreferences() {
    std::string path = getPreferencesFilePath();
    FILE* file = fopen(path.c_str(), "w");

    // Orphaned file from a previous install?  Remove + retry.
    if (!file && access(path.c_str(), F_OK) == 0) {
        if (remove(path.c_str()) == 0) {
            file = fopen(path.c_str(), "w");
        }
    }
    // Fall back to app-private storage.
    if (!file) {
        path = getPreferencesFilePathFallback();
        file = fopen(path.c_str(), "w");
    }

    if (file) {
        fprintf(file, "%d\n", (int)lastInputType);
        fclose(file);
    }
}

void loadPreferences() {
    // Try shared path first, then fallback.
    for (int attempt = 0; attempt < 2; attempt++) {
        std::string path = (attempt == 0) ? getPreferencesFilePath()
                                          : getPreferencesFilePathFallback();
        FILE* file = fopen(path.c_str(), "r");
        if (file) {
            int val = (int)InputType::TOUCH;
            if (fscanf(file, "%d", &val) == 1) {
                if (val >= 0 && val <= 2) {
                    lastInputType = (InputType)val;
                }
            }
            fclose(file);
            return;
        }
        lastInputType = InputType::TOUCH;
    }
}

void setLastInputType(InputType type) {
    if (lastInputType != type) {
        lastInputType = type;
        savePreferences();
    }
}

void windowInit() {
    SDL_Log("windowInit: start");

    // Capture the persisted path BEFORE anything else touches it
    // (loadPreferences calls getDocumentsPath which saves a path).
    std::string previousPath = loadStoragePath();

    loadPreferences();

    // Initialize SDL
    window = nullptr;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Log("windowInit: SDL_Init VIDEO done");

    // Disables the default accelerometer "joystick" on android
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

    window = SDL_CreateWindow("Apotris Android", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, screenWidth, screenHeight,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL |
                                  SDL_WINDOW_RESIZABLE);
    SDL_Log("windowInit: SDL_CreateWindow done");

    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Log("windowInit: SDL_CreateRenderer done");

    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (SDL_NumJoysticks() > 0) {
        controller = SDL_GameControllerOpen(0);
        controllerCount = SDL_NumJoysticks();
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_ShowCursor(SDL_DISABLE);

    SDL_GetWindowSize(window, &screenWidth, &screenHeight);
    refreshWindowSize();
    SDL_Log("windowInit: refreshWindowSize done");

    SDL_AndroidRequestPermission("android.permission.READ_MEDIA_AUDIO");

    std::string basePath = getDocumentsPath();
    if (!basePath.empty() && basePath.back() != '/')
        basePath += "/";

    // Determine whether we ended up on the preferred shared path or a
    // private fallback.  On API 30+ the user may need to grant the
    // MANAGE_EXTERNAL_STORAGE permission via system Settings.
    bool usingFallback = isUsingFallbackPath(basePath);

    LOGI("Using storage path: %s  (fallback=%d)", basePath.c_str(),
         (int)usingFallback);

    extractAssets(basePath);
    loadAudio(basePath);

    // If we're using a numbered variant (e.g. Apotris (1)/), show a
    // one-time message so the user knows where their files live.
    bool isNumbered =
        (basePath.find("/Documents/Apotris (") != std::string::npos);
    if (isNumbered && previousPath != basePath && window != nullptr) {
        std::string msg = "Apotris created a new storage folder because the "
                          "previous one could not be accessed:\n\n" +
                          basePath +
                          "\n\nTo add custom music or SFX, place your files "
                          "in the 'assets' subfolder inside this directory "
                          "using a File Manager.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Storage Folder",
                                 msg.c_str(), window);
    }

    // If we're using a numbered variant (e.g. Apotris (1)/), also scan
    // the original Documents/Apotris/assets/ for user-added custom
    // music so users can still drop files in the expected location.
    {
        std::string original = "/storage/emulated/0/Documents/Apotris/assets/";
        if (basePath != "/storage/emulated/0/Documents/Apotris/" &&
            basePath != original) {
            DIR* d = opendir(original.c_str());
            if (d) {
                closedir(d);
                // The original directory exists and is readable.
                // Scan for extra music files (songList / songNameList
                // are populated by loadAudio above).
                extern std::vector<std::string> songList;
                extern std::vector<std::string> songNameList;

                DIR* dir = opendir(original.c_str());
                if (dir) {
                    dirent* entry;
                    while ((entry = readdir(dir)) != nullptr) {
                        std::string name = entry->d_name;
                        // Only consider known music extensions.
                        auto hasSuffix = [](const std::string& s,
                                            const std::string& suf) {
                            return s.size() >= suf.size() &&
                                   s.compare(s.size() - suf.size(), suf.size(),
                                             suf) == 0;
                        };
                        if (hasSuffix(name, ".it") || hasSuffix(name, ".mod") ||
                            hasSuffix(name, ".s3m") ||
                            hasSuffix(name, ".S3M") ||
                            hasSuffix(name, ".mp3") ||
                            hasSuffix(name, ".ogg") ||
                            hasSuffix(name, ".flac") ||
                            hasSuffix(name, ".xm")) {
                            // Only add if not already in songList
                            // (same file name).
                            bool already = false;
                            for (auto& s : songNameList) {
                                if (s == name) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already) {
                                int idx = (int)songList.size();
                                songList.push_back(original + name);
                                std::string asciiName = name;
                                std::replace_if(
                                    asciiName.begin(), asciiName.end(),
                                    [](char c) { return c < 32 || c >= 127; },
                                    '?');
                                songNameList.push_back(std::move(asciiName));
                                if (name.find("MENU_") != std::string::npos) {
                                    songs.menu.push_back(idx);
                                } else {
                                    songs.game.push_back(idx);
                                }
                                LOGI("Added custom music from original "
                                     "dir: %s",
                                     name.c_str());
                            }
                        }
                    }
                    closedir(dir);
                }
            }
        }
    }

    nanotime_step_init(&stepper, (uint64_t)(NANOTIME_NSEC_PER_SEC / FPS_TARGET),
                       nanotime_now_max(), nanotime_now, nanotime_sleep);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    // Scale font size for high-density screens
    io.FontGlobalScale = 4.0f;

    // Init SDL2 backend (platform)
    ImGui_ImplSDL2_InitForOpenGL(window, nullptr);
    SDL_Log("windowInit: ImGui_ImplSDL2_InitForOpenGL done");

    // Init SDLRenderer2 backend (renderer)
    ImGui_ImplSDLRenderer2_Init(renderer);
    imguiSDLInited = true;
    SDL_Log("windowInit: ImGui_ImplSDLRenderer2_Init done");

    imguiInited = true;

    vibrationInit();
    SDL_Log("windowInit: vibrationInit done");

    // ── Fallback storage message (API 29) ──────────────────────
    // On API 29 scoped storage blocks Documents/ access but
    // MANAGE_EXTERNAL_STORAGE doesn't exist yet.  Show a one-time
    // message so the user knows where files are and how to add them.
    static bool fallbackMessageShown = false;
    if (!fallbackMessageShown && !basePath.empty()) {
        std::string flagPath = basePath + "fallback_shown.flag";
        if (access(flagPath.c_str(), F_OK) == 0) {
            fallbackMessageShown = true;
        }
    }
    if (usingFallback && !isNumbered && !fallbackMessageShown &&
        SDL_GetAndroidSDKVersion() < 30 && window != nullptr) {
        fallbackMessageShown = true;
        if (!basePath.empty()) {
            std::string flagPath = basePath + "fallback_shown.flag";
            FILE* f = fopen(flagPath.c_str(), "w");
            if (f)
                fclose(f);
        }
        std::string msg =
            "Apotris is using app-private storage because Android 10 "
            "does not allow writing to the Documents folder:\n\n" +
            basePath +
            "\n\nTo add custom music or SFX, place your files in the "
            "'assets' subfolder inside the directory shown above using "
            "a File Manager.  On Android 11+, enable 'All files access' "
            "in Settings to use the Documents folder instead.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Storage Location",
                                 msg.c_str(), window);
    }

    // ── MANAGE_EXTERNAL_STORAGE prompt (API 30+) ──────────────────
    // If we couldn't write to the shared Documents/ folder and the
    // device is Android 11+, let the user know they can enable
    // \"All files access\" in system Settings to fix it.

    static bool manageStoragePromptShown = false;
    if (!manageStoragePromptShown && !hasManageStoragePermission()) {
        manageStoragePromptShown = true;
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING, "Storage Access",
            "Apotris needs \"All files access\" to extract and load "
            "assets from its Documents folder, including custom shaders.\n\n"
            "Enable \"All files access\" for Apotris in Android Settings "
            "that opens next, then restart game.\n\n"
            "You can also enable it later via Android Settings > Apps > "
            "Apotris > All files access.",
            window);
        requestManageStoragePermission();
    }

    if (scopedStorageError) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING, "Storage Warning",
            "Some audio files could not be updated. This usually happens "
            "after reinstalling the app — files from the previous "
            "installation still exist but cannot be overwritten.\n\n"
            "To fix this, use a File Manager to DELETE the folder:\n"
            "  Internal Storage / Documents / Apotris\n\n"
            "Then restart the game.\n\n"
            "WARNING: This will also delete your save data "
            "(Apotris.sav). Consider backing it up first.",
            window);
    }
}

void pressKey(int key, InputType type) {
    setLastInputType(type);

    currentlyPressed.insert(key);
}

void unpressKey(int key, InputType type) {
    setLastInputType(type);

    currentlyPressed.erase(key);
}

void key_poll() {}

void saveLayout() {
    if (savefile == nullptr) {
        return;
    }

    savefile->settings.onScreenLayouts[screenWidth > screenHeight] = layout;
    saveSavefile();
}

void handleInput() {
    if (pendingBackground) {
        inBackground = true;
        pendingBackground = false;
        render = false;
    }

    SDL_Event event;

    int x = 0, y = 0, k = 0, dx = 0, dy = 0;
    int index = 0;

    bool eventAvailable = true;

    while (eventAvailable) {
        if (inBackground) {
            eventAvailable = SDL_WaitEvent(&event);
        } else {
            eventAvailable = SDL_PollEvent(&event);
        }

        if (imguiInited)
            ImGui_ImplSDL2_ProcessEvent(&event);

        if (!eventAvailable)
            break;

        int key = 0;
        auto it = currentlyPressed.begin();
        bool found = false;

        switch (event.type) {
        case SDL_TEXTINPUT:
            if (textInputActive)
                textInput += event.text.text;
            break;
        case SDL_KEYDOWN:
            key = event.key.keysym.sym;
            if (textInputActive && key == SDLK_BACKSPACE)
                textInput += '\b';
            if (textInputActive && key == SDLK_RETURN)
                textInput += '\n';
            pressKey(key, InputType::KEYBOARD);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            key = event.cbutton.button;
            pressKey(key, InputType::CONTROLLER);
            break;
        case SDL_KEYUP:
            key = event.key.keysym.sym;
            unpressKey(key, InputType::KEYBOARD);
            break;
        case SDL_CONTROLLERBUTTONUP:
            key = event.cbutton.button;
            unpressKey(key, InputType::CONTROLLER);
            break;
        case SDL_CONTROLLERAXISMOTION:
            handleAnalogInput(event.caxis.axis, event.caxis.value);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                screenWidth = event.window.data1;
                screenHeight = event.window.data2;
                refreshWindowSize();
            }
            break;
        case SDL_JOYBUTTONDOWN:
            key = event.jbutton.button;
            pressKey(key, InputType::CONTROLLER);
            break;
        case SDL_JOYBUTTONUP:
            key = event.jbutton.button;
            unpressKey(key, InputType::CONTROLLER);
            break;
        case SDL_FINGERDOWN:
            setLastInputType(InputType::TOUCH);

            x = touchLocation.x = event.tfinger.x * screenWidth;
            y = touchLocation.y = event.tfinger.y * screenHeight;

            // In edit mode, ignore touches captured by the ImGui panel so
            // tapping panel buttons doesn't drag controls beneath them.
            if (touchEditMode && imguiInited &&
                ImGui::GetIO().WantCaptureMouse) {
                break;
            }

            // Clean up stale finger tracking (lost FINGERUP / ACTION_CANCEL).
            {
                auto stale = activeFingerButtons.find(event.tfinger.fingerId);
                if (stale != activeFingerButtons.end()) {
                    for (auto key : stale->second) {
                        unpressKey(key, InputType::TOUCH);
                    }
                    activeFingerButtons.erase(stale);
                }
            }

            for (auto const& button : layout.buttons) {
                if (x > button.x - button.w / 2 &&
                    x < button.x + button.w / 2 &&
                    y > button.y - button.h / 2 &&
                    y < button.y + button.h / 2) {

                    if (!touchEditMode) {
                        pressKey(button.key, InputType::TOUCH);
                        activeFingerButtons[event.tfinger.fingerId].insert(
                            button.key);
                        if (savefile->settings.touchVibration > 0) {
                            vibrationStart(
                                16 * savefile->settings.touchVibration / 50);
                        }
                    } else {
                        moving = index;
                        resizing = -1;
                        resizeCorner = -1;

                        int g = savefile->settings.touchSnapGrid;
                        auto snapVal = [&](int v) {
                            return g > 0 ? ((v + g / 2) / g) * g : v;
                        };

                        int l = snapVal(button.x - button.w / 2);
                        int r = snapVal(button.x + button.w / 2);
                        int t = snapVal(button.y - button.h / 2);
                        int b = snapVal(button.y + button.h / 2);

                        int grab = cornerGrabRadius();

                        if (abs(x - l) < grab && abs(y - t) < grab) {
                            resizing = index;
                            resizeCorner = 0; // TL
                            resizeFixedX = r;
                            resizeFixedY = b;
                            moving = -1;
                        } else if (abs(x - r) < grab && abs(y - t) < grab) {
                            resizing = index;
                            resizeCorner = 1; // TR
                            resizeFixedX = l;
                            resizeFixedY = b;
                            moving = -1;
                        } else if (abs(x - r) < grab && abs(y - b) < grab) {
                            resizing = index;
                            resizeCorner = 2; // BR
                            resizeFixedX = l;
                            resizeFixedY = t;
                            moving = -1;
                        } else if (abs(x - l) < grab && abs(y - b) < grab) {
                            resizing = index;
                            resizeCorner = 3; // BL
                            resizeFixedX = r;
                            resizeFixedY = t;
                            moving = -1;
                        }
                    }
                }
                index++;
            }

            break;
        case SDL_FINGERMOTION:
            setLastInputType(InputType::TOUCH);

            dx = event.tfinger.dx * screenWidth;
            dy = event.tfinger.dy * screenHeight;

            x = touchLocation.x = event.tfinger.x * screenWidth;
            y = touchLocation.y = event.tfinger.y * screenHeight;

            if (!touchEditMode) {
                for (auto const& button : layout.buttons) {
                    if (!(x > button.x - button.w / 2 &&
                          x < button.x + button.w / 2 &&
                          y > button.y - button.h / 2 &&
                          y < button.y + button.h / 2) &&
                        ((x - dx) > button.x - button.w / 2 &&
                         (x - dx) < button.x + button.w / 2 &&
                         (y - dy) > button.y - button.h / 2 &&
                         (y - dy) < button.y + button.h / 2)) {

                        unpressKey(button.key, InputType::TOUCH);
                        activeFingerButtons[event.tfinger.fingerId].erase(
                            button.key);
                    } else if ((x > button.x - button.w / 2 &&
                                x < button.x + button.w / 2 &&
                                y > button.y - button.h / 2 &&
                                y < button.y + button.h / 2) &&
                               !((x - dx) > button.x - button.w / 2 &&
                                 (x - dx) < button.x + button.w / 2 &&
                                 (y - dy) > button.y - button.h / 2 &&
                                 (y - dy) < button.y + button.h / 2)) {

                        pressKey(button.key, InputType::TOUCH);
                        activeFingerButtons[event.tfinger.fingerId].insert(
                            button.key);
                        if (savefile->settings.touchVibration > 0) {
                            vibrationStart(
                                16 * savefile->settings.touchVibration / 50);
                        }
                    }
                }
            } else {

                int g = savefile->settings.touchSnapGrid;
                auto snapVal = [&](int v) {
                    return g > 0 ? ((v + g / 2) / g) * g : v;
                };

                int minSize = minButtonSize();

                if (resizing >= 0) {
                    Button& b = layout.buttons[resizing];

                    int currentX = snapVal(x);
                    int currentY = snapVal(y);

                    // Clamp minimum size by adjusting currentX/Y if too close
                    // to fixedX/Y
                    if (abs(currentX - resizeFixedX) < minSize) {
                        if (currentX < resizeFixedX)
                            currentX = resizeFixedX - minSize;
                        else
                            currentX = resizeFixedX + minSize;
                    }
                    if (abs(currentY - resizeFixedY) < minSize) {
                        if (currentY < resizeFixedY)
                            currentY = resizeFixedY - minSize;
                        else
                            currentY = resizeFixedY + minSize;
                    }

                    b.w = abs(currentX - resizeFixedX);
                    b.h = abs(currentY - resizeFixedY);
                    b.x = (resizeFixedX + currentX) / 2;
                    b.y = (resizeFixedY + currentY) / 2;
                } else if (moving >= 0) {
                    Button& b = layout.buttons[moving];
                    int tlX = x - b.w / 2;
                    int tlY = y - b.h / 2;
                    int snappedTLX = snapVal(tlX);
                    int snappedTLY = snapVal(tlY);
                    b.x = snappedTLX + b.w / 2;
                    b.y = snappedTLY + b.h / 2;
                }
            }
            break;
        case SDL_FINGERUP:
            setLastInputType(InputType::TOUCH);

            x = touchLocation.x = event.tfinger.x * screenWidth;
            y = touchLocation.y = event.tfinger.y * screenHeight;

            if (!touchEditMode) {
                for (auto const& button : layout.buttons) {
                    if (x > button.x - button.w / 2 &&
                        x < button.x + button.w / 2 &&
                        y > button.y - button.h / 2 &&
                        y < button.y + button.h / 2) {

                        unpressKey(button.key, InputType::TOUCH);
                    }
                }

                // Unpress any tracked buttons for this finger
                // (handles overlapping buttons where the finger lifted
                // outside one of them, slide-off-edge, or ACTION_CANCEL).
                auto it = activeFingerButtons.find(event.tfinger.fingerId);
                if (it != activeFingerButtons.end()) {
                    for (auto key : it->second) {
                        unpressKey(key, InputType::TOUCH);
                    }
                    activeFingerButtons.erase(it);
                }
            } else {
                moving = -1;
                resizing = -1;
                resizeCorner = -1;

                // Sync edited layout back to savefile so settingsChanged()
                // detects the change. Persistence is deferred to the standard
                // settings confirm-save flow.
                if (savefile != nullptr) {
                    savefile->settings
                        .onScreenLayouts[screenWidth > screenHeight] = layout;
                }
            }
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            if (!demo) {
                paused = true;
                pauseSong();
            }
            vibrationStop();
            // Clear all active touch state so keys don't get stuck
            // when the OS cancels touches without FINGERUP events.
            for (auto& pair : activeFingerButtons) {
                for (auto key : pair.second) {
                    unpressKey(key, InputType::TOUCH);
                }
            }
            activeFingerButtons.clear();
            break;
        case SDL_APP_DIDENTERBACKGROUND:
            pendingBackground = true;
            for (auto& pair : activeFingerButtons) {
                for (auto key : pair.second) {
                    unpressKey(key, InputType::TOUCH);
                }
            }
            activeFingerButtons.clear();
            break;
        case SDL_APP_WILLENTERFOREGROUND:
            inBackground = false;
            pendingBackground = false;
            render = true;
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            inBackground = false;
            pendingBackground = false;
            render = true;
            break;
        case SDL_APP_TERMINATING:
        case SDL_APP_LOWMEMORY:
            addGameStats();
            saveSavefile();
            if (event.type == SDL_APP_TERMINATING) {
                quit();
            }
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

int splitKey(uint32_t key) {
    if (lastInputType == InputType::KEYBOARD) {
        return unpackKey(key & 0xffff);
    } else {
        return (key & 0xffff0000) >> 16;
    }
}

static const char* getButtonLabel(int key) {
    switch (key) {
    case SDL_CONTROLLER_BUTTON_A:
        return "A";
    case SDL_CONTROLLER_BUTTON_B:
        return "B";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return "L";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return "R";
    case SDL_CONTROLLER_BUTTON_START:
        return "Start";
    case SDL_CONTROLLER_BUTTON_BACK:
        return "Select";
    default:
        return nullptr;
    }
}

uint32_t key_is_down(uint32_t key) {
    if (key == KEY_FULL)
        return (!currentKeys.empty());

    key = splitKey(key);

    return currentKeys.count(key);
}

uint32_t key_hit(uint32_t key) {
    if (key == KEY_FULL)
        return (previousKeys.empty() && !currentKeys.empty());

    key = splitKey(key);

    bool prev = previousKeys.count(key);
    bool curr = currentKeys.count(key);

    return (!prev && curr);
}

uint32_t key_released(uint32_t key) {
    if (key == KEY_FULL)
        return (!previousKeys.empty() && currentKeys.empty());

    key = splitKey(key);

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

    u32 allKeys = KEY_FULL;
    for (auto key : currentKeys) {
        allKeys ^= key;
    }

    return allKeys;
}

void updateWindow(uint8_t* framebuffer) {
    const int in_width = SCREEN_WIDTH;
    const int in_height = SCREEN_HEIGHT;

    // Create the SDL_Surface from the framebuffer
    SDL_Surface* img = SDL_CreateRGBSurfaceFrom(
        framebuffer, in_width, in_height, 32, in_width * 4, 0x0000ff, 0x00ff00,
        0xff0000, 0xff000000);

    if (img == NULL) {
        printf("Failed to create SDL_Surface: %s\n", SDL_GetError());
        SDL_Log("updateWindow: Failed to create SDL_Surface");
        return;
    }

    // Start ImGui Frame
    if (imguiInited) {
        // SDL_Log("updateWindow: ImGui inited check passed");
        if (shaderStatus == ShaderStatus::OK &&
            savefile->settings.shaders != 0) {
            // SDL_Log("updateWindow: Shaders enabled");
            if (!imguiGLInited) {
                LOGI("Switching to ImGui OpenGL3 backend");
                if (imguiSDLInited) {
                    ImGui_ImplSDLRenderer2_Shutdown();
                    imguiSDLInited = false;
                }

                makeShaderContextCurrent(window);

                const char* version = (const char*)glGetString(GL_VERSION);
                if (version == NULL) {
                    LOGE("CRITICAL: glGetString(GL_VERSION) returned NULL! "
                         "Context not current?");
                } else {
                    LOGI("Current GL Version: %s", version);
                }

                ImGui_ImplOpenGL3_Init("#version 300 es");
                imguiGLInited = true;
                LOGI("ImGui OpenGL3 initialized");
            }
            ImGui_ImplOpenGL3_NewFrame();
            // SDL_Log("updateWindow: ImGui_ImplOpenGL3_NewFrame done");
            ImGui_ImplSDL2_NewFrame();
            // SDL_Log("updateWindow: ImGui_ImplSDL2_NewFrame done (GL path)");
        } else {
            // SDL_Log("updateWindow: Shaders disabled");
            if (imguiGLInited) {
                LOGI("Switching to ImGui SDLRenderer2 backend");
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplSDLRenderer2_Init(renderer);
                imguiGLInited = false;
                imguiSDLInited = true;
            }

            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            // SDL_Log("updateWindow: ImGui SDL2/Renderer2 NewFrame done");
        }

        ImGui::NewFrame();

        // Touch layout edit panel (only in TouchOptionScene).
        if (touchEditMode) {
            float pad = scaled(20);
            float btnPadX = scaled(20);
            float btnPadY = scaled(12);
            float fontScale = std::max(1.0f, touchScale());

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(btnPadX, btnPadY));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(scaled(10), scaled(8)));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings;

            // Top-center, movable (has a title bar so the user can grab it).
            // Reset position whenever the screen orientation changes.
            static int lastOrientation = -1;
            int curOrientation = (screenWidth > screenHeight) ? 1 : 0;
            ImGuiCond posCond = (lastOrientation != curOrientation)
                                    ? ImGuiCond_Always
                                    : ImGuiCond_FirstUseEver;
            lastOrientation = curOrientation;
            ImGui::SetNextWindowPos(ImVec2(screenWidth * 0.5f, pad), posCond,
                                    ImVec2(0.5f, 0.0f));

            ImGui::Begin("Touch Layout", nullptr, flags);

            ImGui::SetWindowFontScale(fontScale);

            ImGui::TextUnformatted("Drag center: move");
            ImGui::TextUnformatted("Drag corner: resize");

            ImGui::Separator();

            if (ImGui::BeginTable("SettingsTable", 2, ImGuiTableFlags_None)) {
                ImGui::TableSetupColumn("Labels",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Controls",
                                        ImGuiTableColumnFlags_WidthFixed);

                // Snap grid stepper
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Snap");

                ImGui::TableSetColumnIndex(1);
                int g = savefile->settings.touchSnapGrid;

                if (ImGui::Button("-##snap")) {
                    if (g >= 10) {
                        savefile->settings.touchSnapGrid = g - 10;
                        sfx(SFX_MENUMOVE);
                    }
                }
                ImGui::SameLine();
                if (g == 0)
                    ImGui::TextUnformatted("Off");
                else
                    ImGui::Text("%d", g);
                ImGui::SameLine();
                if (ImGui::Button("+##snap")) {
                    if (g <= 90) {
                        savefile->settings.touchSnapGrid = g + 10;
                        sfx(SFX_MENUMOVE);
                    }
                }

                // Global scale slider
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Scale");

                ImGui::TableSetColumnIndex(1);
                static float globalScale = 1.0f;
                static Layout baseLayout;

                ImGui::PushItemWidth((float)scaled(400));
                bool changed =
                    ImGui::SliderFloat("##Scale", &globalScale, 0.8f, 1.2f);
                ImGui::PopItemWidth();

                if (ImGui::IsItemActivated()) {
                    baseLayout = layout;
                }
                if (changed) {
                    for (int i = 0; i < 10; i++) {
                        layout.buttons[i].w =
                            (int)(baseLayout.buttons[i].w * globalScale);
                        layout.buttons[i].h =
                            (int)(baseLayout.buttons[i].h * globalScale);
                    }
                }
                if (ImGui::IsItemDeactivatedAfterEdit() ||
                    ImGui::IsItemDeactivated()) {
                    globalScale = 1.0f;
                    if (savefile != nullptr) {
                        savefile->settings
                            .onScreenLayouts[screenWidth > screenHeight] =
                            layout;
                    }
                }

                // Vibration
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Vibration");

                ImGui::TableSetColumnIndex(1);
                int vibration = savefile->settings.touchVibration;

                if (ImGui::Button("-##vib")) {
                    if (vibration >= 25) {
                        savefile->settings.touchVibration = vibration - 25;
                        sfx(SFX_MENUMOVE);
                        if (savefile->settings.touchVibration > 0) {
                            vibrationStart(
                                16 * savefile->settings.touchVibration / 50);
                        }
                    }
                }
                ImGui::SameLine();
                if (savefile->settings.touchVibration == 0)
                    ImGui::TextUnformatted("Off");
                else
                    ImGui::Text("%d%%", savefile->settings.touchVibration);
                ImGui::SameLine();
                if (ImGui::Button("+##vib")) {
                    if (vibration <= 75) {
                        savefile->settings.touchVibration = vibration + 25;
                        sfx(SFX_MENUMOVE);
                        vibrationStart(16 * savefile->settings.touchVibration /
                                       50);
                    }
                }

                // Opacity
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Opacity");

                ImGui::TableSetColumnIndex(1);
                int opacity = savefile->settings.touchOpacity;
                ImGui::PushItemWidth((float)scaled(400));
                if (ImGui::SliderInt("##Opacity", &opacity, 10, 255)) {
                    savefile->settings.touchOpacity = opacity;
                }
                ImGui::PopItemWidth();

                ImGui::EndTable();
            }

            ImGui::Separator();

            float doneBtnW = ImGui::CalcTextSize("Done").x +
                             ImGui::GetStyle().FramePadding.x * 2.0f;

            if (ImGui::Button("Reset")) {
                std::vector<Layout> layouts = getDefaultLayouts();
                savefile->settings.onScreenLayouts[0] = layouts[0];
                savefile->settings.onScreenLayouts[1] = layouts[1];
                setLayout();
                sfx(SFX_MENUMOVE);
            }

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - doneBtnW -
                                 ImGui::GetStyle().WindowPadding.x);
            if (ImGui::Button("Done")) {
                touchEditExitRequested = true;
            }

            ImGui::End();
            ImGui::PopStyleVar(2);
            // Faint grid overlay
            int gridSize = savefile->settings.touchSnapGrid;
            if (gridSize > 0) {
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                ImU32 gridColor = IM_COL32(255, 255, 255, 76);
                for (int gx = 0; gx < screenWidth; gx += gridSize) {
                    bg->AddLine(ImVec2((float)gx, 0.0f),
                                ImVec2((float)gx, (float)screenHeight),
                                gridColor, 1.0f);
                }
                for (int gy = 0; gy < screenHeight; gy += gridSize) {
                    bg->AddLine(ImVec2(0.0f, (float)gy),
                                ImVec2((float)screenWidth, (float)gy),
                                gridColor, 1.0f);
                }
            }
        }

        // ImGui::Begin("Apotris Android Debug");
        // ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        // ImGui::End();

        if (lastInputType == InputType::TOUCH || touchEditMode) {
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            for (auto const& button : layout.buttons) {
                int c = savefile->settings.lightMode ? 0 : 255;
                ImU32 color =
                    IM_COL32(c, c, c, savefile->settings.touchOpacity);

                draw_list->AddRect(
                    ImVec2(button.x - button.w / 2, button.y - button.h / 2),
                    ImVec2(button.x + button.w / 2, button.y + button.h / 2),
                    color, (float)scaled(30), 0, (float)scaled(10));

                const char* label = getButtonLabel(button.key);
                if (label) {
                    ImFont* font = ImGui::GetFont();
                    float fontSize = std::min(button.w, button.h) * 0.4f;
                    ImVec2 textSize =
                        font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
                    draw_list->AddText(font, fontSize,
                                       ImVec2(button.x - textSize.x / 2,
                                              button.y - textSize.y / 2),
                                       color, label);
                } else {
                    float sz = std::min(button.w, button.h) * 0.25f;
                    float cx = (float)button.x;
                    float cy = (float)button.y;

                    if (button.key == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        draw_list->AddTriangleFilled(
                            ImVec2(cx, cy - sz), ImVec2(cx - sz, cy + sz),
                            ImVec2(cx + sz, cy + sz), color);
                    } else if (button.key == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        draw_list->AddTriangleFilled(
                            ImVec2(cx, cy + sz), ImVec2(cx - sz, cy - sz),
                            ImVec2(cx + sz, cy - sz), color);
                    } else if (button.key == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        draw_list->AddTriangleFilled(
                            ImVec2(cx - sz, cy), ImVec2(cx + sz, cy - sz),
                            ImVec2(cx + sz, cy + sz), color);
                    } else if (button.key == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                        draw_list->AddTriangleFilled(
                            ImVec2(cx + sz, cy), ImVec2(cx - sz, cy - sz),
                            ImVec2(cx - sz, cy + sz), color);
                    }
                }
            }
        }

        ImGui::Render();
    }

    if (shaderStatus != ShaderStatus::OK || savefile->settings.shaders == 0) {
        // Create the streaming texture once
        if (texture == NULL) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STREAMING, in_width,
                                        in_height);
            if (texture == NULL) {
                printf("Failed to create texture: %s\n", SDL_GetError());
                SDL_FreeSurface(img);
                return;
            }
        }

        // Lock the texture to update its pixel data
        void* pixels;
        int pitch;
        if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
            printf("Failed to lock texture: %s\n", SDL_GetError());
            SDL_FreeSurface(img);
            return;
        }

        // Copy the surface's pixel data into the streaming texture
        for (int y = 0; y < in_height; y++) {
            memcpy((uint8_t*)pixels + y * pitch, // Destination pointer
                   (uint8_t*)img->pixels + y * img->pitch, // Source pointer
                   in_width * 4                            // Row size in bytes
            );
        }

        SDL_UnlockTexture(texture);

        // Clear the renderer
        SDL_RenderClear(renderer);

        // Define source and destination rectangles
        SDL_Rect src_rect = {0, 0, in_width, in_height};
        const int w = in_width * windowScale;
        const int h = in_height * windowScale;
        SDL_Rect dest_rect = {-(w - screenWidth) / 2, -(h - screenHeight) / 2,
                              w, h};

        // Copy the updated texture to the renderer
        if (render) {
            SDL_RenderCopy(renderer, texture, &src_rect, &dest_rect);
        }

        for (auto const& rect : rects) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 0);

            // Render rect
            SDL_RenderFillRect(renderer, &rect);
        }

        rects.clear();

        if (imguiInited)
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),
                                                  renderer);

        SDL_RenderPresent(renderer);
    } else {
        if (render) {
            drawWithShaders(window, img, true, false); // Don't swap

            if (imguiInited && imguiGLInited) {
                ImDrawData* draw_data = ImGui::GetDrawData();
                if (draw_data) {
                    static int logCounter2 = 0;
                    if (logCounter2++ % 60 == 0)
                        LOGI("ImGui RenderDrawData: CmdLists=%d, TotalVtx=%d",
                             draw_data->CmdListsCount,
                             draw_data->TotalVtxCount);
                    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
                } else {
                    LOGE("ImGui DrawData is NULL");
                }
            }

            SDL_GL_SwapWindow(window);
        }
    }

    SDL_FreeSurface(img);

    // Cap FPS
    nanotime_step(&stepper);
    Uint64 end = SDL_GetPerformanceCounter();

    float elapsedMS =
        (end - frame_start) / (float)SDL_GetPerformanceFrequency() * 1000.0f;

    if (elapsedMS > 100) {
        nanotime_step_init(&stepper,
                           (uint64_t)(NANOTIME_NSEC_PER_SEC / FPS_TARGET),
                           nanotime_now_max(), nanotime_now, nanotime_sleep);
    }

    fps = 1000.0f / elapsedMS;

    frame_start = SDL_GetPerformanceCounter();

    // handle controller hotplug
    if (SDL_NumJoysticks() != controllerCount) {
        if (controllerCount == 0) {
            controller = SDL_GameControllerOpen(0);
        } else if (SDL_NumJoysticks() == 0) {
            SDL_GameControllerClose(controller);
        }

        controllerCount = SDL_NumJoysticks();
    }

    handleInput();

    updateBatteryInfo();
}

void refreshWindowSize() {
    if (savefile != nullptr) {
        int activeZoom = (screenWidth <= screenHeight)
                             ? savefile->settings.zoomPortrait
                             : savefile->settings.zoom;
        if (activeZoom > -1) {
            windowScale = 1 + (float)activeZoom / 10;
        } else {
            int minAxis = std::min(screenWidth, screenHeight);
            bool portrait = screenWidth <= screenHeight;
            if (savefile->settings.integerScale) {
                windowScale = (int)windowScale;

                if (portrait) {
                    // Ensure 240px game width fits on screen
                    while (windowScale > 0 &&
                           screenWidth / (float)windowScale > 240 * 2) {
                        windowScale++;
                    }
                    while (windowScale > 1 &&
                           screenWidth / (float)windowScale < 240) {
                        windowScale--;
                    }
                } else {
                    while (windowScale > 0 && minAxis / windowScale > 160 * 2) {
                        windowScale++;
                    }
                    while (windowScale > 1 && minAxis / windowScale < 160) {
                        windowScale--;
                    }
                }
            } else {
                if (portrait) {
                    windowScale = screenWidth / 240.0;
                } else {
                    windowScale = minAxis / 200.0;
                }
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

    if (savefile != nullptr) {
        setGradient(savefile->settings.backgroundGradient);

        if (savefile->settings.shaders != 0) {
            refreshShaderResolution(screenWidth, screenHeight, windowScale);
        }

        setLayout();
        setFullscreen((screenWidth > screenHeight));
    }

    int offsetx = 0, offsety = 0;
    if (screenWidth > screenHeight) {
        offsetx = (SCREEN_WIDTH - 240) / 2;
        offsety = (SCREEN_HEIGHT - 160) / 2;
    } else {
        offsetx = (SCREEN_WIDTH - 240) * 0.5;
        if (savefile != nullptr && savefile->settings.portraitOffset) {
            offsety = (SCREEN_HEIGHT - 160) * 0.35;
        } else {
            offsety = (SCREEN_HEIGHT - 160) * 0.5;
        }
    }
    setScreenOffset(offsetx, offsety);
}

bool closed() {
    songEndHandler();

    return true;
}

void toggleRendering(bool r) { render = r; }

void initRumble() {};

std::string getSavefilePath() {
    // Always prefer the shared Documents/ path so save data survives
    // uninstall and is accessible to the user.
    return getDocumentsPath() + "Apotris.sav";
}

// App-private fallback used when the shared path is not writable.
static std::string getSavefilePathFallback() {
    const char* ext = SDL_AndroidGetExternalStoragePath();
    return ext ? std::string(ext) + "/Apotris.sav"
               : getDocumentsPath() + "Apotris.sav";
}

void loadSavefile() {
    if (savefile == nullptr)
        savefile = new Save();

    // Try the shared path first, then the app-private fallback.
    for (int attempt = 0; attempt < 2; attempt++) {
        std::string filePath =
            (attempt == 0) ? getSavefilePath() : getSavefilePathFallback();
        std::ifstream input(filePath, std::ios::binary | std::ios::in);
        char* src = (char*)savefile;
        input.read(src, sizeof(Save));
        if (input) {
            input.close();
            return;
        }
        input.close();
    }
    log("Error when trying to load save from either path.");
}

void saveSavefile() {
    std::string filePath = getSavefilePath();
    std::ofstream output(filePath, std::ios::binary | std::ios::out);

    // If opening for write failed, the file may be orphaned from a
    // previous install.  Try remove + retry (same strategy as asset
    // extraction).
    if (!output && access(filePath.c_str(), F_OK) == 0) {
        if (remove(filePath.c_str()) == 0) {
            output.open(filePath, std::ios::binary | std::ios::out);
        }
    }

    // If still no luck, fall back to app-private storage.
    if (!output) {
        scopedStorageError = true;
        filePath = getSavefilePathFallback();
        output.open(filePath, std::ios::binary | std::ios::out);
    }

    if (!output) {
        log("Error when trying to write save.");
        return;
    }

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

void quit() {
    if (imguiInited) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    vibrationDeinit();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    freeAudio();
    exit(0);
}

std::map<int, std::string> keyToString = {
    {SDL_CONTROLLER_BUTTON_A, "A"},
    {SDL_CONTROLLER_BUTTON_B, "B"},
    {SDL_CONTROLLER_BUTTON_X, "X"},
    {SDL_CONTROLLER_BUTTON_Y, "Y"},
    {SDL_CONTROLLER_BUTTON_BACK, "Select"},
    {SDL_CONTROLLER_BUTTON_START, "Start"},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, "Left"},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, "Up"},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "Right"},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, "Down"},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "LB"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RB"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, "LS"},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, "RS"},
    {(1 << 14) | (0 << 5) | (0), "Left +X"},
    {(1 << 14) | (1 << 5) | (0), "Left -X"},
    {(1 << 14) | (0 << 5) | (1), "Left +Y"},
    {(1 << 14) | (1 << 5) | (1), "Left -Y"},
    {(1 << 14) | (0 << 5) | (2), "Right +X"},
    {(1 << 14) | (1 << 5) | (2), "Right -X"},
    {(1 << 14) | (0 << 5) | (3), "Right +Y"},
    {(1 << 14) | (1 << 5) | (3), "Right -Y"},
    {(1 << 14) | (0 << 5) | (4), "LT"},
    {(1 << 14) | (0 << 5) | (5), "RT"},
    {0xffff, ""},
};

std::string stringFromKey(uint32_t key) {
    if (lastInputType == InputType::KEYBOARD) {
        key = unpackKey(key & 0xffff);

        if (key == 0xffff)
            return "";

        return SDL_GetKeyName(key);
    } else {
        return keyToString[key >> 16];
    }
}

void setKey(int& dest, uint32_t key) {
    if (lastInputType == InputType::KEYBOARD) {
        dest = (dest & 0xffff0000) | packKey(key);
    } else {
        dest = (dest & 0xffff) | (key << 16);
    }
}

void unbindDuplicateKey(int& dest, uint32_t key) {
    if (lastInputType == InputType::KEYBOARD) {
        if ((dest & 0xffff) == (key & 0xffff))
            dest = (dest & 0xffff0000) | 0xfffe;
    } else {
        if ((dest >> 16) == key)
            dest = (dest & 0xffff) | (0xfffe << 16);
    }
}

void rumbleStart(int length) {
    if (lastInputType != InputType::CONTROLLER || length <= 0)
        return;

    SDL_GameControllerRumble(controller, 0xffff, 0xffff, length * 64);
}

void rumbleStop() { SDL_GameControllerRumble(controller, 0, 0, 1); }

void setFullscreen(bool state) {
    if (fullscreen == state)
        return;
    fullscreen = state;
    if (!fullscreen) {
        SDL_SetWindowFullscreen(window, 0);
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

void shaderInit(int index) {
    if (imguiGLInited) {
        makeShaderContextCurrent(window);
        ImGui_ImplOpenGL3_Shutdown();
        imguiGLInited = false;
    }
    freeShaders();
    initShaders(window, index);
}

void shaderDeinit() { freeShaders(); }

void handleAnalogInput(int axis, int value) {
    setLastInputType(InputType::CONTROLLER);

    int key = 0;

    key |= (axis & 0xf) | (1 << 14);

    if (value > DEADZONE) {
        pressKey((key), InputType::CONTROLLER);
        unpressKey((key) | (1 << 5), InputType::CONTROLLER);
    } else if (value < -DEADZONE) {
        pressKey((key) | (1 << 5), InputType::CONTROLLER);
        unpressKey((key), InputType::CONTROLLER);
    } else {
        unpressKey((key), InputType::CONTROLLER);
        unpressKey((key) | (1 << 5), InputType::CONTROLLER);
    }
}

void setGameWindowScale() {
    windowScale = gameWindowScale;
    rowStart = (SCREEN_HEIGHT - (screenHeight / windowScale)) / 2;
    rowEnd = (SCREEN_HEIGHT + (screenHeight / windowScale)) / 2;

    if (rowStart < 0)
        rowStart = 0;

    if (rowEnd > SCREEN_HEIGHT)
        rowEnd = SCREEN_HEIGHT;

    if (savefile != nullptr) {
        setGradient(savefile->settings.backgroundGradient);

        if (savefile->settings.shaders != 0) {
            LOGI("Refreshing shader resolution...");
            refreshShaderResolution(screenWidth, screenHeight, windowScale);
            LOGI("Shader resolution refreshed.");
        }
    }
}

bool vibrationInit() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) {
        SDL_Log("Failed to get JNI environment");
        return false;
    }

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) {
        SDL_Log("Failed to get SDL Activity");
        return false;
    }

    jclass clazz = env->GetObjectClass(activity);
    if (!clazz) {
        SDL_Log("Failed to get Activity class");
        env->DeleteLocalRef(activity);
        return false;
    }

    jmethodID getSystemService_id = env->GetMethodID(
        clazz, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!getSystemService_id) {
        SDL_Log("Failed to get getSystemService method ID");
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(activity);
        return false;
    }

    jstring service_name = env->NewStringUTF("vibrator");
    jobject vibrator_service_local =
        env->CallObjectMethod(activity, getSystemService_id, service_name);
    if (!vibrator_service_local) {
        SDL_Log("Failed to get Vibrator service");
        env->DeleteLocalRef(service_name);
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(activity);
        return false;
    }

    vibrateService = env->NewGlobalRef(vibrator_service_local);
    if (!vibrateService) {
        SDL_Log("Failed to create global ref for Vibrator service");
        env->DeleteLocalRef(vibrator_service_local);
        env->DeleteLocalRef(service_name);
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(activity);
        return false;
    }

    jclass vibrator_class = env->GetObjectClass(vibrateService);
    if (!vibrator_class) {
        SDL_Log("Failed to get Vibrator class");
        env->DeleteGlobalRef(vibrateService);
        vibrateService = nullptr;
        // Clean up other local refs
        env->DeleteLocalRef(vibrator_service_local);
        env->DeleteLocalRef(service_name);
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(activity);
        return false;
    }

    // Get the method IDs for vibrate and cancel
    vibrateMethodId = env->GetMethodID(vibrator_class, "vibrate", "(J)V");
    vibrateCancelMethodId = env->GetMethodID(vibrator_class, "cancel", "()V");

    // Clean up all local references
    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(clazz);
    env->DeleteLocalRef(service_name);
    env->DeleteLocalRef(vibrator_service_local);
    env->DeleteLocalRef(vibrator_class);

    if (!vibrateMethodId || !vibrateCancelMethodId) {
        SDL_Log("Failed to get vibrate or cancel method ID");
        env->DeleteGlobalRef(vibrateService);
        vibrateService = nullptr;
        return false;
    }

    SDL_Log("Vibration service initialized successfully");
    return true;
}

void vibrationStart(int durationMS) {
    if (!vibrateService || !vibrateMethodId) {
        SDL_Log("Vibration service not initialized");
        return;
    }

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) {
        SDL_Log("Failed to get JNI environment for vibrate call");
        return;
    }

    jlong duration = durationMS;
    env->CallVoidMethod(vibrateService, vibrateMethodId, duration);
}

void vibrationStop() {
    if (!vibrateService || !vibrateCancelMethodId) {
        SDL_Log("Vibration service not initialized");
        return;
    }

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) {
        SDL_Log("Failed to get JNI environment for cancel call");
        return;
    }

    env->CallVoidMethod(vibrateService, vibrateCancelMethodId);
}

void vibrationDeinit() {
    if (vibrateService) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (env) {
            env->DeleteGlobalRef(vibrateService);
        }
        vibrateService = nullptr;
        vibrateMethodId = 0;
        vibrateCancelMethodId = 0; // Reset the cancel method ID
        SDL_Log("Vibration service cleaned up");
    }
}

std::vector<Layout> getDefaultLayouts() {
    int w, h;
    if (window) {
        SDL_GetWindowSize(window, &w, &h);
    } else {
        SDL_DisplayMode DM;
        SDL_GetCurrentDisplayMode(0, &DM);
        w = DM.w;
        h = DM.h;
    }

    int portraitWidth, portraitHeight, landscapeWidth, landscapeHeight;

    if (w > h) {
        portraitHeight = landscapeWidth = w;
        portraitWidth = landscapeHeight = h;
    } else {
        portraitHeight = landscapeWidth = h;
        portraitWidth = landscapeHeight = w;
    }

    auto snap = [&](int v) {
        int g = 10;
        return ((v + g / 2) / g) * g;
    };

    const int SPACE = snap(scaled(180));
    const int BTN_W = snap(scaled(160));
    const int BTN_H = snap(scaled(160));

    auto makeBtn = [&](int key, int cx, int cy, int w, int h) {
        int tlX = cx - w / 2;
        int tlY = cy - h / 2;
        int snappedTLX = snap(tlX);
        int snappedTLY = snap(tlY);
        return Button(key, snappedTLX + w / 2, snappedTLY + h / 2, w, h);
    };

    const int portraitActionX = portraitWidth / 2 + portraitWidth * 0.3;
    const int portraitActionY = portraitHeight * 0.70;
    const int portraitDpadX = portraitWidth / 2 - portraitWidth * 0.20;
    const int portraitDpadY = portraitHeight * 0.70;
    const int portraitMenuX = portraitWidth / 2 - portraitWidth * 0.25;
    const int portraitMenuY = portraitHeight * 0.98;

    Button portraitButtons[10] = {
        makeBtn(SDL_CONTROLLER_BUTTON_A, portraitActionX + SPACE / 2,
                portraitActionY + SPACE, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_B, portraitActionX - SPACE / 2,
                portraitActionY + SPACE, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, portraitActionX + SPACE / 2,
                portraitActionY + SPACE * 2, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                portraitActionX + SPACE / 2, portraitActionY, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_UP, portraitDpadX, portraitDpadY,
                BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_DOWN, portraitDpadX,
                portraitDpadY + SPACE * 2, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_LEFT, portraitDpadX - SPACE,
                portraitDpadY + SPACE, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, portraitDpadX + SPACE,
                portraitDpadY + SPACE, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_BACK, portraitMenuX - SPACE / 2,
                portraitMenuY - SPACE / 2, BTN_W, snap(BTN_H / 2)),
        makeBtn(SDL_CONTROLLER_BUTTON_START, portraitMenuX + SPACE / 2,
                portraitMenuY - SPACE / 2, BTN_W, snap(BTN_H / 2))};

    const int landscapeActionX = landscapeWidth / 2 + landscapeWidth * 0.35;
    const int landscapeActionY = landscapeHeight * 0.50;
    const int landscapeDpadX = landscapeWidth / 2 - landscapeWidth * 0.30;
    const int landscapeDpadY = landscapeHeight * 0.50;
    const int landscapeMenuX = landscapeWidth / 2 - landscapeWidth * 0.45;
    const int landscapeMenuY = landscapeHeight * 0.20;

    Button landscapeButtons[10] = {
        makeBtn(SDL_CONTROLLER_BUTTON_A, landscapeActionX + SPACE / 2,
                landscapeActionY + SPACE, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_B, landscapeActionX - SPACE / 2,
                landscapeActionY + SPACE, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
                landscapeActionX + SPACE / 2, landscapeActionY + SPACE * 2,
                BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                landscapeActionX + SPACE / 2, landscapeActionY, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_UP, landscapeDpadX, landscapeDpadY,
                BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_DOWN, landscapeDpadX,
                landscapeDpadY + SPACE * 2, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_LEFT, landscapeDpadX - SPACE,
                landscapeDpadY + SPACE, BTN_W, BTN_H),
        makeBtn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, landscapeDpadX + SPACE,
                landscapeDpadY + SPACE, BTN_W, BTN_H),

        makeBtn(SDL_CONTROLLER_BUTTON_BACK, landscapeMenuX,
                landscapeMenuY - SPACE / 2, BTN_W, snap(BTN_H / 2)),
        makeBtn(SDL_CONTROLLER_BUTTON_START, landscapeMenuX + SPACE,
                landscapeMenuY - SPACE / 2, BTN_W, snap(BTN_H / 2))};

    return {Layout(portraitButtons), Layout(landscapeButtons)};
}

void setLayout() {
    layout = savefile->settings.onScreenLayouts[(screenWidth > screenHeight)];
}

void updateBatteryInfo() {
    SDL_PowerState state = SDL_GetPowerInfo(NULL, &batteryPercentage);

    charging = false;
    if (state == SDL_POWERSTATE_CHARGING) {
        charging = true;
    } else if (state != SDL_POWERSTATE_ON_BATTERY) {
        batteryPercentage = -1;
    }
}

void rumbleOutput(uint16_t strength) {
    if (lastInputType != InputType::CONTROLLER)
        return;

    uint16_t sdl_strength =
        std::min(0xFFFF, strength * 0xFFFF / 8); // Cap to 0xFFFF
    SDL_GameControllerRumble(controller, sdl_strength, sdl_strength, 64);
}
#endif
