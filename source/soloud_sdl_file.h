#pragma once
#include "soloud_file.h"
#include <SDL.h>

namespace SoLoud {
    class SDLFile : public File {
    public:
        SDL_RWops* rwops;

        SDLFile(SDL_RWops* rw) : rwops(rw) {}
        virtual ~SDLFile() {
            if (rwops) {
                SDL_RWclose(rwops);
                rwops = nullptr;
            }
        }

        virtual int eof() override {
            if (!rwops) return 1;
            return (pos() >= length()) ? 1 : 0;
        }
        
        virtual unsigned int read(unsigned char *aDst, unsigned int aBytes) override {
            if (!rwops) return 0;
            return (unsigned int)SDL_RWread(rwops, aDst, 1, aBytes);
        }
        
        virtual unsigned int length() override {
            if (!rwops) return 0;
            Sint64 len = SDL_RWsize(rwops);
            return (len > 0) ? (unsigned int)len : 0;
        }
        
        virtual void seek(int aOffset) override {
            if (!rwops) return;
            SDL_RWseek(rwops, aOffset, RW_SEEK_SET);
        }
        
        virtual unsigned int pos() override {
            if (!rwops) return 0;
            Sint64 p = SDL_RWtell(rwops);
            return (p > 0) ? (unsigned int)p : 0;
        }
    };
}
