#pragma once

#include "blockEngine.hpp"
#include "def.h"
#include <functional>
#ifdef GBA
#include "LinkUniversal.hpp"
#endif
#include <algorithm>
#include <list>
#include <map>
#include <string>
#include <tuple>
#include <utility>

extern int previousSelection;

extern void sceneSwitcher(const std::string& str);
extern const std::list<std::string> menuOptions;
extern const std::list<std::string> gameOptions;
extern const std::list<std::string> settingsOptions;
extern const std::list<std::string> controlOptions;
extern const std::list<std::string> menuControlOptions;

static const int READY = 0xC0DE;

static const int START = 0xBEEF;

extern std::string getDescription(const std::string& element);
extern std::string getDescription(const std::string& mode,
                                  const std::string& element,
                                  const std::string& option);

enum class GameElements { NONE, FIELD, HOLD, QUEUE, TIMER, LENGTH };

class Position {
public:
    s16 x = 0;
    s16 y = 0;

    Position() {}

    Position(s16 x, s16 y) {
        this->x = x;
        this->y = y;
    }
};

extern Position gameLayout[(int)GameElements::LENGTH];

class Scene {
public:
    virtual void draw() = 0;
    virtual void update() = 0;
    virtual bool control() = 0;
    virtual void init() = 0;
    virtual void deinit() = 0;
    virtual std::function<Scene*()> previousScene() = 0;

    Scene() = default;

    virtual ~Scene(){};
};

class SceneFactory {
public:
    virtual Scene* create() = 0;

    SceneFactory() = default;

    virtual ~SceneFactory(){};
};

extern Scene* scene;

class GameScene : public Scene {
    void draw();
    void update();
    bool control();
    void init();
    void deinit();

    WordSprite creditSprites[3];

    void countdown();
    void showText();
    void updateText();
    void showTimer();
    void showComboStreak();
    int pauseMenu();
    void endScreen();
    static void showModeText();
    void showStats(bool, const std::string&, const std::string&, bool) const;
    void setupCredits();
    void showCredits();
    void refreshCredits();
    static void checkSounds();
    bool reconnect();
    int handleMultiplayer(bool duringGame);
    int endScreenSetup();
    void showScoreboard();

    bool counted = false;
    bool preventLayeredCountdowns =
        false; // TODO I feel like this can be done better
    bool skipSong = false;

    int timesPaused = 0;

    std::function<Scene*()> previousScene() { return nullptr; };

    ~GameScene() { deinit(); };

    void showReadyPlayers();

public:
    static void UpdateEnemyBoard(int command, int value, u8 linkPlayerId);

    WordSprite wordSprites[MAX_WORD_SPRITES];

    void showEndScreenOptions();
};

#ifndef MULTIBOOT
class TitleScene : public Scene {
    WordSprite wordSprites[MAX_WORD_SPRITES];

    WordSprite versionText = WordSprite(0, 20, 2);
    WordSprite nameText = WordSprite(0, 23, 2 + 12);

    OBJ_ATTR* cursorSprites[2];

    int flashTimer = 0;
    const int flashMax = 48;

    int demoTimer = 0;
    const int demoMax = 60 * 2;

    bool testDraw = false;

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene() { return nullptr; };

    ~TitleScene() { deinit(); };
};
#endif

class SimpleListScene : public Scene {
public:
    int maxDas = 12;
    int dasHor = 0;
    int dasVer = 0;

    int maxArr = 3;
    int arr = 0;

    bool moving = false;
    bool movingHor = false;
    int movingTimer = 0;
    int movingDirection = 0;

    virtual std::list<std::string> getOptionList() { return menuOptions; };
    std::list<std::string> optionList;

    virtual std::string name() { return ""; };

    std::string currentOption = "";
    std::string scrollText = "";

    int scrollTextLength = 0;

    int scrollTimer = 0;
    const int scrollTimerMax = 1;
    int scrollOffset = 0;
    int scrollDelay = 0;
    int scrollDelayMax = 60;

    int endDelay = 0;
    int endDelayMax = 80;
    WordSprite* scrollingText[3];

    int options = 0;
    int selection = 0;
    WordSprite* wordSprites[MAX_WORD_SPRITES];

    OBJ_ATTR* cursorSprites[2];

    OBJ_ATTR* arrowSprites[2];

    OBJ_ATTR* scrollSideSprites[2];

    int cursorFloat = 0;

    int listStart = 0;
    const int elementMax = 4;

    int startY = 0;

    bool refreshText = true;

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene() { return nullptr; };

    virtual std::string getDescription() {
        return ::getDescription(currentOption);
    };

    ~SimpleListScene() { deinit(); };
};

#ifndef MULTIBOOT
class MainMenuScene : public SimpleListScene {
public:
    void init();
    void update();
    void deinit();

    bool showingUpdateNotification = false;

    std::function<Scene*()> previousScene();
    std::list<std::string> getOptionList() {
        std::list<std::string> result = menuOptions;

#if !(defined(GBA) || defined(WEB))

        result.push_back("Quit");

#endif

        return result;
    };
    std::string name() { return ""; };
};
#endif

class ModeListScene : public SimpleListScene {
public:
    std::function<Scene*()> previousScene();

    std::list<std::string> getOptionList() {
        std::list<std::string> l = gameOptions;

#if defined(PC) || defined(WEB) || defined(PORTMASTER) || defined(SWITCH) ||   \
    defined(VITA) ||                                                          \
    defined(N3DS)
        l.remove("2P Battle");
#endif

        return l;
    };
    std::string name() { return "Play"; };
};

class Element {
public:
    int value = 0;

    virtual std::string getLabel() { return ""; };
    virtual std::string getCursor(std::string text) { return ""; };
    virtual void action(int dir) {};
    virtual bool action() { return false; };
    virtual int getValue() { return 0; };
    virtual std::string getCurrentOption() { return ""; };
    virtual bool isSelectable() { return true; };

    Element() {}

    virtual ~Element() {}
};

class OptionListScene : public Scene {
public:
    int maxDas = 12;
    int dasHor = 0;
    int dasVer = 0;

    int maxArr = 3;
    int arr = 0;

    bool moving = false;
    bool movingHor = false;
    int movingTimer = 0;
    int movingDirection = 0;

    virtual std::list<Element*> getElementList() = 0;
    std::list<Element*> elementList;

    virtual std::string name() = 0;

    std::string currentOption = "";
    std::string scrollText = "";

    int scrollTextLength = 0;

    int scrollTimer = 0;
    const int scrollTimerMax = 1;
    int scrollOffset = 0;
    int scrollDelay = 0;
    int scrollDelayMax = 60;

    int endDelay = 0;
    int endDelayMax = 80;
    WordSprite* scrollingText[3];

    int options = 0;
    int selection = 0;
    WordSprite* wordSprites[MAX_WORD_SPRITES];

    WordSprite* labels[7];

    WordSprite cursorText = WordSprite(0, 20, 2);

    OBJ_ATTR* cursorSprites[2];

    OBJ_ATTR* arrowSprites[2];

    OBJ_ATTR* scrollSideSprites[2];

    int cursorFloat = 0;

    bool refreshText = true;
    bool refreshOption = true;

    int listStart = 0;
    const int elementMax = 5;

    int startY = 0;

    void showPath();

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    virtual std::function<Scene*()> previousScene() { return nullptr; };

    virtual std::string getDescription() {
        return ::getDescription(currentOption);
    };

    ~OptionListScene() { deinit(); };
};

#ifndef GBA
class MatchMakingScene : public Scene {
public:
    int timer = 0;

    bool playRandoms = true;

    int cursorFloat = 0;
#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
    bool onlinePlayerCountShown = false;
#endif

    WordSprite wordSprites[2];

    OBJ_ATTR* cursorSprites[2];

    int lengths[2];

    int pos[2];

    std::string room;

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene() { return []{ return new MainMenuScene(); }; };
    bool hideSprites = false;
};
#endif

#ifndef MULTIBOOT
class ConfirmSaveScene : public Scene {
public:
    int timer = 0;

    bool cancel = true;

    int cursorFloat = 0;

    WordSprite wordSprites[2];

    OBJ_ATTR* cursorSprites[2];

    int lengths[2];

    int pos[2];

    int type = 0; // 0 for settings, 1 for skin editor, 2 for color editor

    void draw();
    void update();
    bool control();
    void init();
    void deinit();

    ConfirmSaveScene(int type) { this->type = type; }

    std::function<Scene*()> previousScene() { return []{ return new MainMenuScene(); }; };

    ~ConfirmSaveScene() { deinit(); };
};

class SettingsScene : public SimpleListScene {
public:
    std::function<Scene*()> previousScene() {
        if (settingsChanged())
            return []{ return new ConfirmSaveScene(0); };
        else
            return []{ return new MainMenuScene(); }; };

    std::list<std::string> getOptionList() {
        std::list<std::string> result = settingsOptions;

#ifndef GBA
        result.push_front("Video");
#else
        result.push_back("Sleep");
#endif

#ifdef ANDROID
        auto it = result.begin();
        std::advance(it, 4);
        result.insert(it, "Touch");
#endif

        return result;
    };

    std::string name() { return "Settings"; };
};

class CreditsScene : public Scene {
public:
    virtual std::list<std::string> getOptionList() { return menuOptions; };
    std::list<std::string> optionList;

    std::string name() { return "Credits"; };

    WordSprite* wordSprites[MAX_WORD_SPRITES];

    int listStart = 0;
    int maxShow = 12;

    int delay = 0;
    int delayMax = 60;

    int scrollTimer = 0;
    int scrollTimerMax = 4;
    int scrollOffset = 0;
    int space = 12;

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene() { return []{ return new MainMenuScene(); }; };

    ~CreditsScene() { deinit(); };
};

class LinksScene : public SimpleListScene {

public:
    // void init();
    std::function<Scene*()> previousScene() { return []{ return new MainMenuScene(); }; };

    std::list<std::string> getOptionList() {
        return {
            "Website",
            "Wiki",
            "Donate",
            "Discord",
        };
    };

    std::string name() { return "Links"; };
};

#endif

class MultiplayerExceptionScene : public Scene {
private:
    std::string what;
    const std::string name = "Multi error";

public:
    WordSprite* wordSprites[MAX_WORD_SPRITES];

    explicit MultiplayerExceptionScene(std::string error) {
        what = std::move(error);
    }

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene();
    ;

    ~MultiplayerExceptionScene() { deinit(); };
};
#ifndef MULTIBOOT

class QRScene : public Scene {
public:
    virtual u8* getData() = 0;

    virtual std::string name() = 0;

    virtual std::string getLink() = 0;

    OBJ_ATTR* qr;

    int qrX = 0;
    int qrY = 0;

    WordSprite* wordSprites[MAX_WORD_SPRITES];

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
    std::function<Scene*()> previousScene() { return []{ return new LinksScene(); }; };

    ~QRScene() { deinit(); };
};

#include "site_qr_bin.h"

class WebsiteLinkScene : public QRScene {
public:
    std::string name() override { return "Website"; }

    u8* getData() override { return (u8*)site_qr_bin; }

    std::string getLink() override { return "https://apotris.com"; }

    WebsiteLinkScene() {
        qrX = 29;
        qrY = 29;
    }
};

#include "wiki_qr_bin.h"

class WikiLinkScene : public QRScene {
public:
    std::string name() override { return "Wiki"; }

    u8* getData() override { return (u8*)wiki_qr_bin; }

    std::string getLink() override { return "https://apotris.com/wiki"; }

    WikiLinkScene() {
        qrX = 29;
        qrY = 29;
    }
};

#include "paypal_qr_bin.h"

class DonateLinkScene : public QRScene {
public:
    std::string name() override { return "Donate"; }

    u8* getData() override { return (u8*)paypal_qr_bin; }

    std::string getLink() override { return "https://apotris.com/donate"; }

    DonateLinkScene() {
        qrX = 29;
        qrY = 29;
    }
};

#include "discord_qr_bin.h"

class DiscordLinkScene : public QRScene {
public:
    std::string name() override { return "Discord"; }

    u8* getData() override { return (u8*)discord_qr_bin; }

    std::string getLink() override { return "https://apotris.com/discord"; }

    DiscordLinkScene() {
        qrX = 33;
        qrY = 33;
    }
};

#endif

#ifdef GBA
class MultiplayerProtocolScene : public Scene {
public:
    int timer = 0;

    bool cancel = true;

    int cursorFloat = 0;

    WordSprite wordSprites[2];

    OBJ_ATTR* cursorSprites[2];

    int lengths[2];

    int pos[2];

    void draw();
    void update();
    bool control();
    void init();
    void deinit();
#ifndef MULTIBOOT
    std::function<Scene*()> previousScene() { return []{ return new MainMenuScene(); }; };
#else
    std::function<Scene*()> previousScene() { return []{ return new MultiplayerProtocolScene(); }; };
#endif
};
#endif

class LabelElement : public Element {
    std::string label;

public:
    std::string getLabel() override { return label; }

    std::string getCurrentOption() override { return "\n"; }

    bool isSelectable() override { return false; }

    LabelElement(std::string l) { label = l; }
};

enum class Transitions { INSTANT, FADE, SCANLINE };

extern void changeScene(std::function<Scene*()> factory);
extern void changeScene(std::function<Scene*()> factory,
                        Transitions transition);
extern void checkSceneSwitch();

extern int rank;
extern u8 attackAnimationTimers[4];
const int animationMax = 15;
