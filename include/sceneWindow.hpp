#pragma once

#ifndef MULTIBOOT
#include "scene.hpp"

#if defined(PC) || defined(PORTMASTER) || defined(ANDROID)
#include "liba_pc.h"
#include "shader.h"
#include <cmath>
#include <sstream>
#include <string>
#endif

#ifdef ANDROID
extern int screenWidth;
extern int screenHeight;
#endif

#ifdef N3DS

class MainScreenElement : public Element {
public:
    std::string getLabel() override { return "Main Screen"; }

    std::string getCursor(std::string text) override {
        return "<" + text + ">";
    }

    void action(int dir) override {
        savefile->settings.n3dsMainScreenIsTop =
            !savefile->settings.n3dsMainScreenIsTop;
        sfx(SFX_MENUCONFIRM);
    }

    std::string getCurrentOption() override {
        if (savefile->settings.n3dsMainScreenIsTop)
            return "Top";
        else
            return "Bottom";
    }
};

class ScaleSelectElement : public Element {
    const n3ds::ScaleMode scale;

public:
    ScaleSelectElement(n3ds::ScaleMode scale) : scale{scale} {}

    std::string getLabel() override {
        switch (scale) {
        case n3ds::ScaleMode::UNSCALED:
            return "1x";
        case n3ds::ScaleMode::SCALED_LINEAR:
            return "1.5x smooth";
        case n3ds::ScaleMode::SCALED_SHARP:
            return "1.5x sharp";
        case n3ds::ScaleMode::SCALED_ULTRA:
            return "1.5x ultra";
        case n3ds::ScaleMode::NUM_ENTRIES:
        default:
            return "?";
        }
    }

    std::string getCursor(std::string text) override {
        if (savefile->settings.n3dsScaleMode == scale) {
            return ">" + text + "<";
        } else if (scale == n3ds::ScaleMode::SCALED_ULTRA &&
                   !n3ds::wideModeSupported) {
            return " " + text + " ";
        } else {
            return "[" + text + "]";
        }
    }

    bool action() override {
        if (savefile->settings.n3dsScaleMode != scale) {
            if (scale == n3ds::ScaleMode::SCALED_ULTRA &&
                !n3ds::wideModeSupported) {
                sfx(SFX_INVALID);
            } else {
                savefile->settings.n3dsScaleMode = scale;
                sfx(SFX_MENUCONFIRM);
            }
        }
        return false;
    }

    std::string getCurrentOption() override {
        if (savefile->settings.n3dsScaleMode == scale) {
            return "In use";
        } else if (scale == n3ds::ScaleMode::SCALED_ULTRA &&
                   !n3ds::wideModeSupported) {
            return "-";
        } else {
            return ">";
        }
    }
};

#else /* N3DS */

class IntegerScaleElement : public Element {
public:
    std::string getLabel() override { return "Integer Scale"; }

    std::string getCursor(std::string text) override {
        return "[" + text + "]";
    }

    void action(int dir) override {
        savefile->settings.integerScale = !savefile->settings.integerScale;
        sfx(SFX_MENUCONFIRM);

        if (savefile->settings.integerScale && savefile->settings.zoom > -1) {
            savefile->settings.zoom =
                (savefile->settings.zoom / 10) * 10; // round to 10s
        }

#if defined(PC) || defined(WEB) || defined(PORTMASTER) || defined(SWITCH) ||   \
    defined(VITA) ||                                                          \
    defined(ANDROID)
        refreshWindowSize();
#endif
    }

    std::string getCurrentOption() override {
        if (savefile->settings.integerScale)
            return "ON";
        else
            return "OFF";
    }
};

class ScaleElement : public Element {
    int* getActiveZoom() {
#ifdef ANDROID
        if (screenWidth <= screenHeight)
            return &savefile->settings.zoomPortrait;
#endif
        return &savefile->settings.zoom;
    }

public:
    const int min = -1;
    const int max = 90;
    std::string getLabel() override { return "Scale"; }

    std::string getCursor(std::string text) override {
        std::string result;
        int activeZoom = *getActiveZoom();

        if (activeZoom > min)
            result += "<";
        else
            result += " ";

        result += text;

        if (activeZoom < max)
            result += ">";

        return result;
    }

    void action(int dir) override {
#if defined(PC) || defined(WEB) || defined(PORTMASTER) || defined(SWITCH) ||   \
    defined(VITA) ||                                                          \
    defined(ANDROID)
        int* activeZoomPtr = getActiveZoom();
        int activeZoom = *activeZoomPtr;
        int add = 1 + 9 * (savefile->settings.integerScale && activeZoom > -1);
        if (dir > 0) {
            if (activeZoom > min) {
                // Already in manual mode: normal increment
                if (activeZoom < max) {
                    *activeZoomPtr += add;

                    if (*activeZoomPtr > max)
                        *activeZoomPtr = max;

                    sfx(SFX_MENUMOVE);
                }
            } else {
                // Transition from AUTO to manual: start at current auto scale
                int newZoom = (int)std::round((windowScale - 1.0f) * 10.0f);
                if (newZoom < 0)
                    newZoom = 0;
                if (newZoom > max)
                    newZoom = max;
                if (savefile->settings.integerScale) {
                    // Snap to nearest multiple of 10 for integer scale
                    newZoom = ((newZoom + 5) / 10) * 10;
                    if (newZoom > max)
                        newZoom = max;
                }
                *activeZoomPtr = newZoom;
                sfx(SFX_MENUMOVE);
            }
        } else {
            if (activeZoom > min) {
                *activeZoomPtr -= add;

                if (*activeZoomPtr < min)
                    *activeZoomPtr = min;

                sfx(SFX_MENUMOVE);
            }
        }

        refreshWindowSize();
#endif
    }

    std::string getCurrentOption() override {
        int activeZoom = *getActiveZoom();
        if (activeZoom > -1) {
            return "x" + std::to_string((activeZoom / 10) + 1) + "." +
                   std::to_string(activeZoom % 10);
        } else {
            return "AUTO";
        }
    }
};

#ifdef ANDROID
class PortraitOffsetElement : public Element {
public:
    std::string getLabel() override { return "Portrait Offset"; }

    std::string getCursor(std::string text) override {
        return "[" + text + "]";
    }

    void action(int dir) override {
        savefile->settings.portraitOffset = !savefile->settings.portraitOffset;
        sfx(SFX_MENUCONFIRM);
        refreshWindowSize();
    }

    std::string getCurrentOption() override {
        if (savefile->settings.portraitOffset)
            return "ON";
        else
            return "OFF";
    }
};
#endif

#endif /* N3DS */

class FPSElement : public Element {
public:
    std::string getLabel() override { return "Show FPS"; }

    std::string getCursor(std::string text) override {
        return "[" + text + "]";
    }

    void action(int dir) override {
        savefile->settings.showFPS = !savefile->settings.showFPS;

        if (!savefile->settings.showFPS)
            clearText();

        sfx(SFX_MENUCONFIRM);
    }

    std::string getCurrentOption() override {
        if (savefile->settings.showFPS)
            return "ON";
        else
            return "OFF";
    }
};

#ifdef PC
class FullscreenElement : public Element {
public:
    std::string getLabel() override { return "Fullscreen"; }

    std::string getCursor(std::string text) override {
        return "[" + text + "]";
    }

    void action(int dir) override {
        savefile->settings.fullscreen = !savefile->settings.fullscreen;
        sfx(SFX_MENUCONFIRM);

        setFullscreen(savefile->settings.fullscreen);
    }

    std::string getCurrentOption() override {
        if (savefile->settings.fullscreen)
            return "ON";
        else
            return "OFF";
    }
};
#endif

#if defined(PC) || defined(PORTMASTER) || defined(ANDROID)
class ShaderElement : public Element {
public:
    const std::vector<std::string> shaders = findShaders();
    const int min = 0;
    const int max = shaders.size();

    std::string getLabel() override { return "Shaders"; }

    std::string getCursor(std::string text) override {
        if (savefile->settings.shaders != 0 && shaderStatus != ShaderStatus::OK)
            return "<" + text;

        std::string result;

        if (savefile->settings.shaders > min)
            result += "<";
        else
            result += " ";

        result += text;

        if (savefile->settings.shaders < max)
            result += ">";

        return result;
    }

    void action(int dir) override {
        if (dir > 0 && savefile->settings.shaders != 0 &&
            shaderStatus != ShaderStatus::OK) {
            sfx(SFX_MENUCANCEL);
            return;
        }

        if (dir > 0) {
            if (savefile->settings.shaders < max) {
                savefile->settings.shaders++;

                if (savefile->settings.shaders > max)
                    savefile->settings.shaders = max;

                sfx(SFX_MENUMOVE);
            }
        } else {
            if (savefile->settings.shaders > min) {
                savefile->settings.shaders--;

                if (savefile->settings.shaders < min)
                    savefile->settings.shaders = min;

                sfx(SFX_MENUMOVE);
            }
        }

        if (savefile->settings.shaders) {
            shaderInit(savefile->settings.shaders);
            refreshWindowSize();
        } else {
            shaderDeinit();
        }
    }

    std::string getCurrentOption() override {
        if (savefile->settings.shaders != 0) {
            switch (shaderStatus) {
            case ShaderStatus::NOT_INITED:
                break;
            case ShaderStatus::NO_GL:
                return "NO GL";
            case ShaderStatus::NO_SHADER_FILES:
                return "NONE";
            case ShaderStatus::SHADER_FILE_ERROR:
                return "ERROR";
            case ShaderStatus::OK:
                break;
            }
        }

        if (savefile->settings.shaders) {
            std::stringstream shaderPath;
            shaderPath << shaders.at(savefile->settings.shaders - 1);

            std::string name;

            while (getline(shaderPath, name, '/'))
                ;

            return remove_extension(name);
        } else {
            return "OFF";
        }
    }

private:
    std::string remove_extension(const std::string& filename) {
        size_t lastdot = filename.find_last_of(".");
        if (lastdot == std::string::npos)
            return filename;
        return filename.substr(0, lastdot);
    }
};
#endif

class WindowOptionScene : public OptionListScene {
public:
    std::string name() { return "Video"; };
    std::list<Element*> getElementList() {
        std::list<Element*> list;

#ifdef PC
        list.push_back(new FullscreenElement);
#endif

#ifdef N3DS
        list.push_back(new MainScreenElement());
        list.push_back(new LabelElement("Scaling mode:"));
        list.push_back(new ScaleSelectElement(n3ds::ScaleMode::UNSCALED));
        list.push_back(new ScaleSelectElement(n3ds::ScaleMode::SCALED_LINEAR));
        list.push_back(new ScaleSelectElement(n3ds::ScaleMode::SCALED_SHARP));
        list.push_back(new ScaleSelectElement(n3ds::ScaleMode::SCALED_ULTRA));
        list.push_back(new LabelElement("Diagnostics:"));
#else
        list.push_back(new IntegerScaleElement());
        list.push_back(new ScaleElement());
#ifdef ANDROID
        if (screenWidth <= screenHeight) {
            list.push_back(new PortraitOffsetElement());
        }
#endif
#endif
        list.push_back(new FPSElement());

#if defined(PC) || defined(PORTMASTER) || defined(ANDROID)
        list.push_back(new ShaderElement());
#endif

        return list;
    };

#ifdef N3DS
private:
    int previousAspectRatio = savefile->settings.aspectRatio;

public:
    void draw() {
        if (previousAspectRatio != savefile->settings.aspectRatio) {
            previousAspectRatio = savefile->settings.aspectRatio;
            // HACK: Redraw the path to make it match the aspect
            // ratio.
            showPath();
            // Clear the FPS text region:
            aprintClearArea(30 - 5 - 2, 0, 5 + 2, 1);
        }
        OptionListScene::draw();
    }
#endif

#ifdef ANDROID
private:
    int lastOrientation;

public:
    void init() override {
        OptionListScene::init();
        lastOrientation = (screenWidth <= screenHeight) ? 1 : 0;
    }

    void update() override {
        int currentOrientation = (screenWidth <= screenHeight) ? 1 : 0;
        if (currentOrientation != lastOrientation) {
            lastOrientation = currentOrientation;

            for (auto e : elementList)
                delete e;
            elementList.clear();

            elementList = getElementList();
            options = (int)elementList.size();

            if (selection >= options)
                selection = options - 1;

            refreshText = true;
            refreshOption = true;
        }

        OptionListScene::update();
    }
#endif

    std::function<Scene*()> previousScene() { return []{ return new SettingsScene(); }; };
};
#endif
