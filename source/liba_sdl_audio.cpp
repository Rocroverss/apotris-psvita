#include "platform.hpp"

#if defined(PC) || defined(PORTMASTER) || defined(WEB) || defined(SWITCH) ||   \
    defined(VITA) ||                                                          \
    defined(ANDROID)

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>

#include "soloud.h"
#include "soloud_openmpt.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

#include "def.h"

SoLoud::Soloud gSoloud;
SoLoud::Queue musicQueue;

SoLoud::Wav soundEffects[SFX_COUNT];

std::vector<SoLoud::AudioSource*> musicArray;

SoLoud::handle currentSongHandle;

std::vector<std::string> songList;
std::vector<std::string> songNameList;

int currentSong = -1;
int sfxVolume = 1024;
float musicVolume = 1;
bool looping = false;

Songs songs;

bool hasSuffix(const std::string& s, const std::string& suffix) {
    return (s.size() >= suffix.size()) &&
           equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

void loadAudio(std::string pathPrefix) {

    gSoloud.init();
    log("loadAudio called with pathPrefix: " + pathPrefix);

    for (int i = 0; i < SFX_COUNT; i++) {
        std::string str = pathPrefix + (std::string)SoundEffectPaths[i];
        std::string apkPath = (std::string)SoundEffectPaths[i];

        SDL_RWops* rw = SDL_RWFromFile(str.c_str(), "rb");
        if (!rw) {
            rw = SDL_RWFromFile(apkPath.c_str(), "rb");
        }

        if (rw) {
            Sint64 size = SDL_RWsize(rw);
            if (size > 0) {
                unsigned char* buf = new unsigned char[size];
                SDL_RWread(rw, buf, 1, size);
                soundEffects[i].loadMem(buf, size, false, true);
            }
            SDL_RWclose(rw);
        } else {
            log("Warning: SFX file not found: " + str);
        }
    }

    int counter = 0;
    std::string path = pathPrefix + "assets";
    std::vector<std::string> songPaths;

    DIR* dir = opendir(path.c_str());
    if (dir) {
        dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (hasSuffix(entry->d_name, ".it") ||
                hasSuffix(entry->d_name, ".mod") ||
                hasSuffix(entry->d_name, ".s3m") ||
                hasSuffix(entry->d_name, ".S3M") ||
                hasSuffix(entry->d_name, ".mp3") ||
                hasSuffix(entry->d_name, ".ogg") ||
                hasSuffix(entry->d_name, ".flac") ||
                hasSuffix(entry->d_name, ".wav") ||
                hasSuffix(entry->d_name, ".xm")) {

                bool found = false;
                if (hasSuffix(entry->d_name, ".wav")) {
                    for (int i = 0; i < SFX_COUNT; i++) {
                        std::string str = (std::string)SoundEffectPaths[i];

                        if (hasSuffix(str, entry->d_name)) {
                            found = true;
                            break;
                        }
                    }
                }

                if (!found) {
                    songPaths.push_back(entry->d_name);
                }
            }
        }
        closedir(dir);
    } else {
        log("couldn't open dir: " + path + " (will load base assets from APK)");
    }

    // Add base songs from file_list.txt if they aren't already there
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

            if (hasSuffix(line, ".it") || hasSuffix(line, ".mod") ||
                hasSuffix(line, ".s3m") || hasSuffix(line, ".S3M") ||
                hasSuffix(line, ".mp3") || hasSuffix(line, ".ogg") ||
                hasSuffix(line, ".flac") || hasSuffix(line, ".wav") ||
                hasSuffix(line, ".xm")) {

                bool found = false;
                if (hasSuffix(line, ".wav")) {
                    for (int i = 0; i < SFX_COUNT; i++) {
                        std::string str = (std::string)SoundEffectPaths[i];

                        if (hasSuffix(str, line)) {
                            found = true;
                            break;
                        }
                    }
                }

                if (!found && std::find(songPaths.begin(), songPaths.end(),
                                        line) == songPaths.end()) {
                    songPaths.push_back(line);
                }
            }
        }
    }

    sort(songPaths.begin(), songPaths.end());

    for (auto str : songPaths) {
        songList.push_back(path + "/" + str);
        std::string asciiName = str;
        std::replace_if(
            asciiName.begin(), asciiName.end(),
            [](char c) { return c < 32 || c >= 127; }, '?');
        songNameList.push_back(std::move(asciiName));

        if (str.find("MENU_") != std::string::npos) {
            songs.menu.push_back(counter++);
        } else {
            songs.game.push_back(counter++);
        }
    }

    for (int i = 0; i < counter; i++) {
        SoLoud::result loaded = SoLoud::SO_NO_ERROR;
        std::string songPath = songList.at(i);
        std::string apkPath = "assets/" + songNameList.at(i);

        SDL_RWops* rw = SDL_RWFromFile(songPath.c_str(), "rb");
        if (!rw) {
            rw = SDL_RWFromFile(apkPath.c_str(), "rb");
        }

        if (!rw) {
            log("Warning: Music file not found or unreadable: " + songPath);
            continue;
        }

        Sint64 size = SDL_RWsize(rw);
        if (size <= 0) {
            SDL_RWclose(rw);
            continue;
        }

        unsigned char* buf = new unsigned char[size];
        SDL_RWread(rw, buf, 1, size);
        SDL_RWclose(rw);

        if (hasSuffix(songPath, ".mp3") || hasSuffix(songPath, ".ogg") ||
            hasSuffix(songPath, ".flac") || hasSuffix(songPath, ".wav")) {
            SoLoud::WavStream* wavstream = new SoLoud::WavStream;
            loaded = wavstream->loadMem(buf, size, false, true);
            musicArray.push_back((SoLoud::AudioSource*)wavstream);
        } else {
            SoLoud::Openmpt* openmpt = new SoLoud::Openmpt;
            loaded = openmpt->loadMem(buf, size, false, true);
            musicArray.push_back((SoLoud::AudioSource*)openmpt);
        }

        if (loaded != SoLoud::SO_NO_ERROR) {
            log("couldn't load: " + songList.at(i) + " " +
                std::to_string(loaded));
        }
    }
}

void freeAudio() {
    for (auto source : musicArray) {
        delete source;
    }

    gSoloud.deinit();
}

void songEndHandler() {
    if (musicVolume == 0)
        return;

    if (currentSong != -1 && currentSong < (int)musicArray.size() &&
        !musicQueue.isCurrentlyPlaying(*musicArray.at(currentSong))) {
        if (savefile->settings.cycleSongs == 1) { // CYCLE
            playNextSong();
        } else if (savefile->settings.cycleSongs == 2) { // SHUFFLE
            playSongRandom(currentMenu);
        }
    }
}

void sfx(int n) {
    if (n < 0 || n >= SFX_COUNT)
        return;
    soundEffects[n].setVolume((float)savefile->settings.sfxVolume / 10);
    gSoloud.play(soundEffects[n]);
}

void startSong(int song, bool loop) {
    if (song < 0 || song >= (int)musicArray.size())
        return;

    if (song == currentSong && !gSoloud.getPause(currentSongHandle) &&
        (currentSong != -1 && currentSong < (int)musicArray.size() &&
         musicQueue.isCurrentlyPlaying(*musicArray.at(currentSong)))) {
        return;
    }

    if (currentSong != -1 && currentSong < (int)musicArray.size() &&
        musicQueue.isCurrentlyPlaying(*musicArray.at(currentSong))) {
        musicQueue.stop();
        if (musicQueue.getQueueCount() > 0)
            musicQueue.skip();
    }

    musicQueue.setParams(44100, 2);
    currentSongHandle = gSoloud.play(musicQueue, musicVolume, 0, true);
    musicArray.at(song)->setLooping(loop);
    musicQueue.play(*musicArray.at(song));

    currentSong = song;
    looping = loop;

    gSoloud.setPause(currentSongHandle, false);
}

void stopSong() {
    if (currentSong == -1)
        return;

    musicQueue.stop();

    if (musicQueue.getQueueCount() > 0)
        musicQueue.skip();

    currentSong = -1;
}

void resumeSong() { gSoloud.setPause(currentSongHandle, 0); }

void setMusicVolume(int volume) {
    musicVolume = volume / (512.0 / 2);

    gSoloud.setVolume(currentSongHandle, musicVolume);
}

void setMusicTempo(int tempo) {
    gSoloud.setRelativePlaySpeed(currentSongHandle, tempo / 1024.0);
}

void sfxRate(int n, float rate) {
    if (n < 0 || n >= SFX_COUNT)
        return;
    soundEffects[n].setVolume((float)savefile->settings.sfxVolume / 10);
    int s = gSoloud.play(soundEffects[n]);
    gSoloud.setRelativePlaySpeed(s, rate);
}

void pauseSong() { gSoloud.setPause(currentSongHandle, 1); }

std::string getSongName(int song) {
    if (song < 0 || song >= (int)songNameList.size())
        return "Unknown";
    return songNameList[song];
}

#endif
