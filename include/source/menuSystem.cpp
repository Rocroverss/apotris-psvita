#include "def.h"
#include "logging.h"
#include "menu.h"
#include "scene.hpp"
#include "sceneAudio.hpp"
#include "sceneColorEditor.hpp"
#include "sceneControls.hpp"
#include "sceneGameplay.hpp"
#include "sceneGraphics.hpp"
#include "sceneHandling.hpp"
#include "sceneModes.hpp"
#include "sceneSaving.hpp"
#include "sceneSleep.hpp"
#include "sceneStats.hpp"
#include "sceneWindow.hpp"
#include <functional>
#ifdef ANDROID
#include "sceneTouch.hpp"
#endif
#include "skinEditor.hpp"
#include <string>

#define FADE_LENGTH 4

void fadeToBlack();
void fadeFromBlack();

static COLOR* preTransitionPalette = nullptr;

Transitions transition;
std::function<Scene*()> factory;
static bool sceneSwitch = false;
void switchScene();

void checkSceneSwitch() {
    if (!sceneSwitch)
        return;

    sceneSwitch = false;

    switchScene();
}

void changeScene(std::function<Scene*()> f) {
    changeScene(f, Transitions::INSTANT);
}

void changeScene(std::function<Scene*()> f, Transitions t) {
    factory = f;
    transition = t;

    sceneSwitch = true;
}

void switchScene() {
    gradient(0);
    vsync();
    setLightMode();

    switch (transition) {
    case Transitions::INSTANT:
        break;
    case Transitions::FADE:
        fadeToBlack();
        break;
    case Transitions::SCANLINE:
        break;
    default:
        break;
    }

    toggleRendering(false);
    if (preTransitionPalette != nullptr)
        loadPalette(0, 0, preTransitionPalette, 512);

    delete scene;
    scene = factory();
    scene->init();

    vsync();
    toggleRendering(true);

    switch (transition) {
    case Transitions::INSTANT:
        break;
    case Transitions::FADE:
        fadeFromBlack();
        break;
    case Transitions::SCANLINE:
        break;
    default:
        break;
    }
}

void fadeToBlack() {

    if (preTransitionPalette != nullptr)
        delete[] preTransitionPalette;
    preTransitionPalette = new COLOR[512];
    savePalette(preTransitionPalette);

    int color = 0;

    if (savefile->settings.lightMode)
        color = 0x5ad6;

    int timer = 0;
    while (closed() && timer++ < FADE_LENGTH) {
        vsync();
        color_fade_palette(0, 1, &preTransitionPalette[1], color, 511,
                           timer * 8);
    }
}

void fadeFromBlack() {
    int timer = FADE_LENGTH;

    if (preTransitionPalette != nullptr)
        delete[] preTransitionPalette;
    preTransitionPalette = new COLOR[512];
    savePalette(preTransitionPalette);

    int color = 0;

    if (savefile->settings.lightMode)
        color = 0x5ad6;

    color_fade_palette(0, 1, &preTransitionPalette[1], color, 511, timer * 8);

    while (closed() && timer >= 0) {
        canDraw = 1;
        vsync();
        color_fade_palette(0, 1, &preTransitionPalette[1], color, 511,
                           timer * 8);
        timer--;
    }
}

void sceneSwitcher(const std::string& str) {
#ifndef MULTIBOOT
    if (str == "Play") {
        changeScene([]() { return new ModeListScene(); }, Transitions::FADE);
    } else if (str == "Settings") {
        if (previousSettings == nullptr)
            previousSettings = std::make_unique<Settings>();
        *previousSettings = savefile->settings;
        menuKeys = savefile->settings.menuKeys;

        for (int i = 0; i < MAX_CUSTOM_SKINS; i++)
            previousSkins[i] = savefile->customSkins[i];

        changeScene([]() { return new SettingsScene(); }, Transitions::FADE);
    } else if (str == "Achievements") {

    } else if (str == "Stats") {
        changeScene([]() { return new StatScene(); }, Transitions::FADE);
    } else if (str == "Links") {
        changeScene([]() { return new LinksScene(); }, Transitions::FADE);
    } else if (str == "Skin Editor") {
        setPreviousSettings(savefile->settings);
        for (int i = 0; i < MAX_CUSTOM_SKINS; i++)
            previousSkins[i] = savefile->customSkins[i];

        menuKeys = savefile->settings.menuKeys;

        changeScene([]() { return new EditorScene(); }, Transitions::FADE);
    } else if (str == "Color Editor") {
        setPreviousSettings(savefile->settings);

        if (previousPalette != nullptr)
            delete[] previousPalette;
        previousPalette = new COLOR[3 * 7 * 3];

        memcpy16(previousPalette, savefile->customPalettes, 3 * 7 * 3);

        menuKeys = savefile->settings.menuKeys;

        changeScene([]() { return new ColorSelectorScene(); },
                    Transitions::FADE);
    } else if (str == "Credits") {
        changeScene([]() { return new CreditsScene(); }, Transitions::FADE);
    } else if (str == "Graphics") {
        changeScene([]() { return new GraphicsScene(); }, Transitions::FADE);
    } else if (str == "Audio") {
        changeScene([]() { return new AudioOptionScene(); }, Transitions::FADE);
    } else if (str == "Controls") {
        changeScene([]() { return new ControlOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Handling") {
        changeScene([]() { return new HandlingOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Gameplay") {
        changeScene([]() { return new GameplayOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Saving") {
        changeScene([]() { return new SavingOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Video") {
        changeScene([]() { return new WindowOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Sleep") {
#ifdef GBA
        changeScene([]() { return new SleepOptionScene(); }, Transitions::FADE);
#endif
#ifdef ANDROID
    } else if (str == "Touch") {
        changeScene([]() { return new TouchOptionScene(); }, Transitions::FADE);
#endif
    } else if (str == "Rumble") {
        changeScene([]() { return new RumbleOptionScene(); },
                    Transitions::FADE);
    } else if (str == "Marathon") {
        changeScene([]() { return new MarathonScene(); }, Transitions::FADE);
    } else if (str == "Sprint") {
        changeScene([]() { return new SprintScene(); }, Transitions::FADE);
    } else if (str == "Dig") {
        changeScene([]() { return new DigScene(); }, Transitions::FADE);
    } else if (str == "Ultra") {
        changeScene([]() { return new UltraScene(); }, Transitions::FADE);
    } else if (str == "Blitz") {
        changeScene([]() { return new BlitzScene(); }, Transitions::FADE);
    } else if (str == "Combo") {
        changeScene([]() { return new ComboScene(); }, Transitions::FADE);
    } else if (str == "Survival") {
        changeScene([]() { return new SurvivalScene(); }, Transitions::FADE);
    } else if (str == "Classic") {
        changeScene([]() { return new ClassicScene(); }, Transitions::FADE);
    } else if (str == "Master") {
        changeScene([]() { return new MasterScene(); }, Transitions::FADE);
    } else if (str == "Death") {
        changeScene([]() { return new DeathScene(); }, Transitions::FADE);
    } else if (str == "Zen") {
        changeScene([]() { return new ZenScene(); }, Transitions::FADE);
    } else if (str == "Multi Battle") {
#ifdef GBA
        changeScene([]() { return new MultiplayerProtocolScene(); },
                    Transitions::FADE);
#ifndef MULTIBOOT
        /*
        } else if (str == "Wireless Pak") {
            changeScene([](){ return new SinglePakWirelessScene(); },
        Transitions::FADE); } else if (str == "Wired Pak") { changeScene([](){
        return new SinglePakWiredScene(); }, Transitions::FADE);
        */
#endif
#else
        changeScene([]() { return new MatchMakingScene(); }, Transitions::FADE);
#endif
    } else if (str == "CPU Battle" || str == "Battle") {
        changeScene([]() { return new CPUBattleScene(); }, Transitions::FADE);
    } else if (str == "Training") {
        changeScene([]() { return new TrainingScene(); }, Transitions::FADE);
    } else if (str == "Website") {
        changeScene([]() { return new WebsiteLinkScene(); }, Transitions::FADE);
    } else if (str == "Wiki") {
        changeScene([]() { return new WikiLinkScene(); }, Transitions::FADE);
    } else if (str == "Donate") {
        changeScene([]() { return new DonateLinkScene(); }, Transitions::FADE);
    } else if (str == "Discord") {
        changeScene([]() { return new DiscordLinkScene(); }, Transitions::FADE);
    } else if (str == "Quit") {
        quit();
    } else {
        log("invalid option: " + str);
    }
#endif
}

std::string modeToString(BlockEngine::Modes mode) {
    switch (mode) {
    case BlockEngine::NO_MODE:
        return "None";
    case BlockEngine::MARATHON:
        return "Marathon";
    case BlockEngine::SPRINT:
        return "Sprint";
    case BlockEngine::DIG:
        return "Dig";
    case BlockEngine::BATTLE:
        return "Battle";
    case BlockEngine::ULTRA:
        return "Ultra";
    case BlockEngine::BLITZ:
        return "Blitz";
    case BlockEngine::COMBO:
        return "Combo";
    case BlockEngine::SURVIVAL:
        return "Survival";
    case BlockEngine::CLASSIC:
        return "Classic";
    case BlockEngine::MASTER:
        return "Master";
    case BlockEngine::ZEN:
        return "Zen";
    case BlockEngine::DEATH:
        return "Death";
    case BlockEngine::TRAINING:
        return "Training";
    default:
        return "";
    }
}

void WordSprite::setText(const std::string& _text) {
    if (_text == text)
        return;

    text = _text;

    if (text.empty()) {
        clearSpriteTiles(startTiles, (big) ? 20 : 12, 1);
        return;
    }

    width = getVariableWidth(text);

    clearSpriteTiles(startTiles, (big) ? 20 : 12, 1);

    naprintSprite(text, startTiles);
}

void WordSprite::setTextNum(int n) {
    char buff[64];
    posprintf(buff, "%d", n);

    std::string _text = buff;
    if (_text == text)
        return;

    text = _text;

    if (text.empty()) {
        clearSpriteTiles(startTiles, (big) ? 20 : 12, 0);
        return;
    }

    width = getVariableWidth(text);

    clearSpriteTiles(startTiles, (big) ? 20 : 12, 1);

    naprintSprite(text, startTiles);
}

void WordSprite::setup(int _id, int _index, int _tiles, bool _big) {
    id = _id;
    startIndex = _index;
    startTiles = _tiles;
    big = _big;

    for (int i = 0; i < 3 * (1 + big) - big; i++) {
        sprites[i] = &obj_buffer[startIndex + i];
    }
}

void setPreviousSettings(Settings settings) {
    if (previousSettings == nullptr)
        previousSettings = std::make_unique<Settings>();

    *previousSettings = settings;
}
