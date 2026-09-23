#pragma once

#include "scene.hpp"

#ifdef ANDROID

#include "liba_android.h"

class TouchOptionScene : public Scene {
public:
    void init() override {
        canDraw = 1;
        toggleBG(3, false);
        clearSprites(128);

        // Refresh the active layout from savefile in case it was reset
        // outside this scene.
        setLayout();

        touchEditExitRequested = false;
        touchEditMode = true;
    }

    void deinit() override {
        touchEditMode = false;
        touchEditExitRequested = false;
    }

    void update() override {
        canDraw = 1;
        key_poll();
        vsync();

        if (touchEditExitRequested) {
            touchEditExitRequested = false;
            changeScene([]() { return new SettingsScene(); },
                        Transitions::FADE);
        }
    }

    void draw() override {
        // Intentionally blank: the touch-edit overlay (button outlines, ImGui
        // panel and grid) is drawn from updateWindow() in liba_android.cpp
        // while touchEditMode is true.
    }

    bool control() override { return false; }

    std::string name() { return "Touch"; }

    std::function<Scene*()> previousScene() override {
        return [] { return new SettingsScene(); };
    }

    ~TouchOptionScene() { deinit(); }
};

#endif
