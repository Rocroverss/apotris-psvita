#include "logging.h"
#include "multiplayerClasses.h"
#include "rumblePatterns.hpp"
#include "scene.hpp"
#include "sceneModes.hpp"
#include "sprites.h"
#ifdef GBA
#include "detectEmulators.h"
#ifndef MULTIBOOT
// #include "sendMultiboot.h"
#endif
#else
#include "LinkWebRTC.hpp"
#endif
#include "prng.h"
#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <atomic>
#endif
#include <cstring>
#include <utility>

#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
static constexpr int ONLINE_PLAYER_COUNT_REFRESH_FRAMES = 60 * 20;
static std::atomic<int> onlinePlayerCount{-1};
static std::atomic<unsigned int> onlinePlayerCountRequest{0};

#ifdef ANDROID
static const std::string& androidCaCertificatePath() {
    static const std::string path = []() {
        const char* internalPath = SDL_AndroidGetInternalStoragePath();
        if (!internalPath)
            return std::string{};

        const std::string destPath = std::string(internalPath) + "/cacert.pem";
        SDL_RWops* src = SDL_RWFromFile("assets/ssl/cacert.pem", "rb");
        if (!src)
            return std::string{};

        SDL_RWops* dst = SDL_RWFromFile(destPath.c_str(), "wb");
        if (!dst) {
            SDL_RWclose(src);
            return std::string{};
        }

        char buffer[4096];
        while (const size_t bytesRead =
                   SDL_RWread(src, buffer, 1, sizeof(buffer)))
            SDL_RWwrite(dst, buffer, 1, bytesRead);

        SDL_RWclose(dst);
        SDL_RWclose(src);
        return destPath;
    }();
    return path;
}
#endif

static void fetchOnlinePlayerCount() {
    onlinePlayerCount.store(-1);
    const unsigned int request = ++onlinePlayerCountRequest;
    const auto callback = [request](cpr::Response response) {
        if (response.status_code != 200)
            return;

        try {
            const auto body = nlohmann::json::parse(response.text);
            if (onlinePlayerCountRequest.load() == request &&
                body.contains("total") && body["total"].is_number_integer())
                onlinePlayerCount.store(body["total"].get<int>());
        } catch (const nlohmann::json::exception&) {
        }
    };

#ifdef ANDROID
    const std::string& caCertificatePath = androidCaCertificatePath();
    if (!caCertificatePath.empty()) {
        cpr::GetCallback(
            callback, cpr::Url{"https://api.apotris.com/v1/online-players"},
            cpr::Ssl(cpr::ssl::CaInfo{cpr::fs::path{caCertificatePath}}));
        return;
    }
#endif

    cpr::GetCallback(callback,
                     cpr::Url{"https://api.apotris.com/v1/online-players"});
}

static void drawMatchMakingText(int totalPlayers = -1) {
    clearText();
    // clearText restores a previously configured text area; this scene owns
    // layer 29, so remove it before rebuilding the matchmaking text.
    clearTilemap(29);

    const std::string str1 = "Who would you like";
    const std::string str2 = "to play with?";
    naprint(str1, (240 - getVariableWidth(str1)) / 2, 56);
    naprint(str2, (240 - getVariableWidth(str2)) / 2, 66);

    if (totalPlayers >= 0) {
        const std::string playersOnline =
            std::to_string(totalPlayers) + " PLAYERS ONLINE";
        naprintColor(playersOnline, 236 - getVariableWidth(playersOnline), 144,
                     14);
    }
}
#endif

bool lostConnection = true;

#if defined(GBA) && !defined(MULTIBOOT) && false
LinkWirelessMultiboot::Async* linkWirelessMultibootAsync =
    new LinkWirelessMultiboot::Async("Apotris");
LinkCableMultiboot::Async* linkCableMultibootAsync =
    new LinkCableMultiboot::Async();
#endif

void PlayOptionScene::init() {
    loadSpriteTiles(620, &sprite49tiles_bin, 1, 1);
    for (int i = 0; i < 5; i++) {
        proSprites[i] = &obj_buffer[10 + i];
        sprite_hide(proSprites[i]);
    }

    OptionListScene::init();

    if (elementList.empty()) {
        onStart = true;
        if (getBoard(subMode, goal) != nullptr)
            onScore = true;
    }

    startSprite.setText("START");
}

bool PlayOptionScene::control() {
    MenuKeys k = savefile->settings.menuKeys;

    if ((onScore || getMode() == BlockEngine::ZEN) && key_is_down(k.special1) &&
        key_is_down(k.special2) && key_is_down(k.special3)) {
        resetScoreboard();
        refreshText = true;
        return false;
    }

    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        if (!(onScore || onStart) || options == 0) {
            previousElement = name();
            previousSelection = selection;

            if (!path.empty())
                path.pop_back();

            changeScene(previousScene(), Transitions::FADE);
            return true;
        } else if (onStart) {
            onStart = false;
        }

        if (onScore) {
            onScore = false;
            refreshText = true;
            movingDirection = -1;
            listStart = 0;
        }
    }

    if (!(onScore || onStart)) {
        if (key_hit(k.left) && !elementList.empty()) {
            auto it = elementList.begin();
            std::advance(it, selection);

            (*it)->action(-1);

            refreshOption = true;
        }

        if (key_hit(k.right) && !elementList.empty()) {
            auto it = elementList.begin();
            std::advance(it, selection);

            (*it)->action(1);

            refreshOption = true;
        }
    }

    int prev = listStart;

    if (key_hit(k.up)) {
        movingDirection = -1;

        bool sound = true;

        if ((onScore || onStart) && !elementList.empty()) {
            onScore = false;
            onStart = false;
            refreshText = true;

            selection = options;
            auto it = elementList.end();
            do {
                selection--;
                it--;
            } while (it != elementList.begin() && !(*it)->isSelectable());

        } else {
            if (selection > 0) {
                auto it = elementList.begin();
                std::advance(it, selection);

                do {
                    if (selection > 0) {
                        selection--;
                        it--;
                    } else {
                        break;
                    }
                } while (!(*it)->isSelectable());
            }

            if (elementList.empty())
                sound = false;
        }

        if (selection == options - 1)
            listStart = max(0, selection - elementMax);
        else if (selection < listStart + 1) {
            listStart--;
            if (listStart < 0)
                listStart = 0;
        }

        if (sound)
            sfx(SFX_MENUMOVE);

        refreshText = true;
    }

    if (key_hit(k.down)) {
        if (!onScore) {
            movingDirection = 1;

            auto it = elementList.begin();
            std::advance(it, selection);

            bool found = false;
            while (selection < options - 1) {
                selection++;
                it++;
                if ((*it)->isSelectable()) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                if (getBoard(subMode, goal) != nullptr)
                    movingScore = true;
                else
                    onStart = true;
            }

            if (selection == 0)
                listStart = 0;
            else if (selection > listStart + elementMax)
                listStart++;
            refreshText = true;
            sfx(SFX_MENUMOVE);
        }
    }

    if (!onScore && (key_is_down(k.up) || key_is_down(k.down))) {
        if (dasVer < maxDas) {
            dasVer++;
        } else {
            int dir = (key_is_down(k.up) ? -1 : 1);

            if (selection + dir >= 0 && selection + dir <= options - 1 &&
                arr++ > maxArr) {
                arr = 0;

                movingDirection = dir;

                selection += dir;

                if (dir < 0) {
                    if (selection == options - 1)
                        listStart = max(0, selection - elementMax);
                    else if (selection < listStart + 1) {
                        listStart--;
                        if (listStart < 0)
                            listStart = 0;
                    }
                } else {
                    if (selection == 0)
                        listStart = 0;
                    else if (selection > listStart + elementMax)
                        listStart++;
                }

                refreshText = true;
                sfx(SFX_MENUMOVE);
            }
        }
    } else {
        dasVer = 0;
    }

    if (!(onScore || onStart) &&
        (key_is_down(k.left) || key_is_down(k.right))) {
        if (dasHor < maxDas) {
            dasHor++;
        } else {
            int dir = (key_is_down(k.left) ? -1 : 1);
            if (arr++ > maxArr) {
                arr = 0;

                auto it = elementList.begin();
                std::advance(it, selection);

                (*it)->action(dir);

                refreshOption = true;
            }
        }
    } else {
        dasHor = 0;
    }

    if (prev != listStart) {
        moving = true;
    }

    if (key_hit(k.pause) || key_hit(k.confirm)) {
        if (!onScore && getBoard(subMode, goal) != nullptr) {
            movingScore = true;
            sfx(SFX_MENUCONFIRM);
        } else if (!onStart) {
            onStart = true;
            sfx(SFX_MENUCONFIRM);
        } else {
            start();
        }
    }

    return false;
}

constexpr int getDelayForBotDifficulty(int diff) {
    int delay = 0;

    float pps[] = {
        0.6, 1.5, 4, 10, 100,
    };

    delay = (int)((1.0 / pps[diff - 1]) * 60.0);

    return delay;
}

void PlayOptionScene::start() {
    bool training = false;
    int subMode = 0;
    int goal = 0;
    int bTypeHeight = 0;

#ifndef MULTIBOOT
    for (auto const& element : elementList) {
        std::string s = element->getLabel();
        int value = element->getValue();
        log(s);
        log(value);
        if (s == "Level") {
            level = value;
        } else if (s == "Type" || s == "Rules") {
            subMode = value;
        } else if (s == "Lines") {
            goal = value;
            mode = element->value;
        } else if (s == "Height") {
            bTypeHeight = value;
        } else if (s == "Finesse Training") {
            training = value;
        } else if (s == "Difficulty") {
            if (name() == "CPU Battle") {
                botThinkingSpeed = 12;
                botSleepDuration = getDelayForBotDifficulty(value);
                botStepMax = 10;
                mode = value;
                goal = 250;
            } else {
                goal = value;
                mode = element->value;
            }
        } else if (s == "Minutes") {
            goal = value;
            mode = element->value;
        }
    }
#endif

    BlockEngine::Options newOptions;

    newOptions.mode = getMode();

    if (newOptions.mode == BlockEngine::BLITZ)
        goal = 2 * FRAMES_PER_MIN;

    if (newOptions.mode == BlockEngine::CLASSIC) {
        newOptions.rotationSystem = BlockEngine::NRS;
        newOptions.randomizer = BlockEngine::RANDOM;
    } else {
        newOptions.rotationSystem = savefile->settings.rotationSystem;

        if (savefile->settings.randomizer >= 0 &&
            savefile->settings.randomizer <= 2) {
            newOptions.randomizer = savefile->settings.randomizer;
        } else {
            newOptions.randomizer = BlockEngine::BAG_7;
        }
    }

    newOptions.goal = goal;
    newOptions.level = level;
    newOptions.tuning = getTuning();
    newOptions.bigMode = savefile->settings.big;
    newOptions.subMode = subMode;
    newOptions.trainingMode = training;
    newOptions.bTypeHeight = bTypeHeight;

    MenuKeys k = savefile->settings.menuKeys;

    proMode = (key_is_down(k.special1) || key_is_down(k.special2)) ^
              (savefile->settings.pro);

    startGame(newOptions, (int)randNext());

    gameLoop();
}

void PlayOptionScene::draw() {
    fallingBlocks();

    const int space = 1;
    const int startX = 3 * 8;
    const int endX = 30 - 4;

    int offset = (sinLut(cursorFloat) * 2) >> 12;
    FIXED scale = float2fx((1.0 - ((float)0.1 * offset)));

    int verticalOffset = 0;
    if (moving) {
        int temp =
            lerp((movingDirection) * (space + 1) * 8, 0, 64 * movingTimer);
        verticalOffset = lerp(temp, 0, 64 * movingTimer);
#ifdef N3DS
        // Scroll falling blocks to top of screen when in 1x scale:
        setLayerScroll(2, 0, -verticalOffset + 40);
#else
        setLayerScroll(2, 0, -verticalOffset);
#endif
    } else {
#ifdef N3DS
        // Scroll falling blocks to top of screen when in 1x scale:
        setLayerScroll(2, 0, 40);
#else
        setLayerScroll(2, 0, 0);
#endif
    }

    EntryBoard* board = getBoard(subMode, goal);

    if (!onScore) {
        int i = 0;       // drawing index, starts at listStart
        int counter = 0; // element index
        for (auto& option : elementList) {
            if (counter < listStart) {
                counter++;
                continue;
            }

            if (i > elementMax + 1) { // break if elements drawn reached max
                continue;
            }

            std::string opt = option->getCurrentOption();

            const int index = (i + listStart) % (elementMax + 2);

            if (refreshOption || refreshText) {
                if (counter == selection && option->isSelectable())
                    wordSprites[index]->setText(
                        option->getCursor(opt)); // draw option with text cursor
                else
                    wordSprites[index]->setText(
                        " " + opt); // draw option without text cursor
            }

            if (refreshText)
                labels[index]->setText(option->getLabel());

            const int x = (endX) * 8 - getVariableWidth(opt);
            const int y = startY + 8 * (space + 1) * i + verticalOffset;

            const int selected =
                (counter != selection || !option->isSelectable()) +
                (i == elementMax + 1 || (listStart && i == 0));

            wordSprites[index]->setPriority(0);
            wordSprites[index]->show(x, y, 15 - selected);

            labels[index]->show(startX, y, 15 - selected);

            if (counter == selection && !onStart) { // draw sprite cursor
                currentOption = opt;
                currentElement = option->getLabel();
                for (int i = 0; i < 2; i++) {
                    sprite_unhide(cursorSprites[i], 0);
                    sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5,
                                    1);
                    sprite_enable_affine(cursorSprites[i], i, true);
                    sprite_set_size(cursorSprites[i], scale, i);

                    int x = startX - ((8) + offset + 4) - 4;

                    sprite_set_pos(cursorSprites[i], x, y - 5);
                }
            }

            i++;
            counter++;
        }

        for (int j = i; j < elementMax + 2; j++) {
            int index = (j + listStart) % (elementMax + 2);

            wordSprites[index]->hide();
            labels[index]->hide();
        }

        for (int i = elementMax + 2; i < 9; i++)
            wordSprites[i]->hide();

        for (auto& proSprite : proSprites)
            sprite_hide(proSprite);
    } else {
        if (board != nullptr) {
            for (int i = 0; i < 5; i++) {
                const int y = startY + 8 * (1) * i + verticalOffset;

                char buff[12];
                posprintf(buff, "%d. %s", i + 1, board->entries[i].name);

                labels[i]->setText(buff);
                labels[i]->show(startX, y, 15);

                std::string str = "";
                if (getIfGrade() && board->entries[i].value > 0) {
                    if (name() == "Master") {
                        str += GameInfo::masterGrades[board->entries[i].grade];
                    } else if (name() == "Death") {
                        str += GameInfo::deathGrades[board->entries[i].grade];
                    }
                    str += " ";
                }

                if (!getIfTime()) {
                    str += std::to_string(board->entries[i].value);
                } else
                    str += timeToString(board->entries[i].value, false);

                const int x = (endX) * 8 - getVariableWidth(str);
                wordSprites[i]->setText(str);
                wordSprites[i]->show(x, y, 15);

                if (board->entries[i].pro) {
                    sprite_unhide(proSprites[i], 0);

                    sprite_set_attr(proSprites[i], ShapeSquare, 0, 620, 15, 0);
                    sprite_set_pos(proSprites[i], endX * 8 + 4, y);
                } else {
                    sprite_hide(proSprites[i]);
                }
            }
        }

        for (int j = 5; j < elementMax + 2; j++)
            labels[j]->hide();

        for (int j = 6; j < elementMax + 2; j++)
            wordSprites[j]->hide();

        int i = 0;
        for (auto const& option : elementList) {
            if (option->getLabel() == "Level" &&
                !(name() == "Classic" && subMode))
                continue;

            wordSprites[6 + i]->setText(option->getCurrentOption());
            wordSprites[6 + i]->show(30 * 8 - 4 - wordSprites[6 + i]->width,
                                     4 + i * 8, 14);

            i++;
        }
    }

    if (onScore || board == nullptr) {
        startSprite.show((240 - 43) / 2, 132, 15);

        if (onScore || onStart) {
            for (int i = 0; i < 2; i++) {
                sprite_unhide(cursorSprites[i], 0);
                sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 1);
                sprite_enable_affine(cursorSprites[i], i, true);
                sprite_set_size(cursorSprites[i], scale, i);

                int x =
                    240 / 2 - ((43 + 8) / 2 + offset + 4) * ((i) ? -1 : 1) - 10;

                sprite_set_pos(cursorSprites[i], x, 132 - 5);
            }
        }

        for (auto& scrollSideSprite : scrollSideSprites) {
            sprite_hide(scrollSideSprite);
        }
    } else {
        startSprite.hide();
    }

    if (listStart > 0) {
        sprite_unhide(arrowSprites[0], 0);
        sprite_set_attr(arrowSprites[0], ShapeWide, 0, 1, 14, 0);
        sprite_set_pos(arrowSprites[0], 240 / 2 - 8, startY - 11 - 16);
    } else {
        sprite_hide(arrowSprites[0]);
    }

    if (!onScore && getBoard(subMode, goal) != nullptr) {
        sprite_unhide(arrowSprites[1], 0);
        sprite_set_attr(arrowSprites[1], ShapeWide, 0, 1, 14, 0);
        sprite_enable_flip(arrowSprites[1], false, true);
        sprite_set_pos(arrowSprites[1], 240 / 2 - 8,
                       (elementMax + 2) * (1 + space) * 8 + startY - 48);
    } else {
        sprite_hide(arrowSprites[1]);
    }

    if (refreshText || refreshOption) {
        setOptions();
        std::string next =
            ::getDescription(name(), currentElement, currentOption);

        if (next.empty() || next != scrollText) {
            scrollText = next;
            clearSpriteTiles(672, 144, 1);
            if (!scrollText.empty()) {
                scrollTextLength = getVariableWidth(scrollText);

                const int width =
                    (savefile->settings.aspectRatio == 0) ? 240 : 214;

                endDelay = 0;
                scrollOffset = -max((width - scrollTextLength), 0) / 2;
                scrollDelay = 0;
                scrollingText[0]->setTextSlow(scrollText);

#if defined(TE) || defined(N3DS)
                for (int i = 0; i < 2; i++) {
                    sprite_unhide(scrollSideSprites[i], ATTR0_AFF);
                    sprite_set_pos(scrollSideSprites[i],
                                   240 / 2 +
                                       ((i * 2) - 1) *
                                           (min(scrollTextLength, width) / 2) -
                                       16 * (i == 0),
                                   152 - 4);
                }
#endif
            } else {
                scrollingText[0]->setTextSlow(scrollText);

                for (auto& scrollSideSprite : scrollSideSprites) {
                    sprite_hide(scrollSideSprite);
                }
            }
        }
    }

    showSprites(128);

    refreshText = false;
    refreshOption = false;
}

void PlayOptionScene::update() {
    if (movingScore > 0) {
        if (++movingScoreTimer >= movingScoreTimerMax) {
            if (++listStart >= options - 1) {
                movingScore = 0;
                onScore = true;
                onStart = true;
                clearSpriteTiles(672, 144, 1);
                scrollingText[0]->setText("");
                scrollText = "";
                selection = elementList.size() - 1;
            }
        }
    }

    OptionListScene::update();
}

void PlayOptionScene::setOptions() {
    for (auto const& element : elementList) {
        std::string s = element->getLabel();
        int value = element->value;
        if (s == "Level") {
            level = value;
        } else if (s == "Type" || s == "Rules") {
            subMode = value;
        } else if (s == "Lines") {
            goal = value;
        } else if (s == "Minutes") {
            goal = value;
        } else if (s == "Difficulty") {
            goal = value;
        }
    }
}

#ifndef MULTIBOOT

void ClassicScene::update() {
    if (!subMode) {
        if (elementList.back()->getLabel() == "Height") {

            Element* e = elementList.back();
            elementList.pop_back();

            delete e;
            options--;
        }
    } else {
        if (elementList.back()->getLabel() != "Height") {
            elementList.push_back(
                new IntSelectorElement("Height", {0, 1, 2, 3, 4, 5}));
            refreshText = true;
            options++;
        }
    }

    PlayOptionScene::update();
}

void ZenScene::update() {
    if (subMode == 2 || subMode == 3) {
        if (elementList.back()->getLabel() == "Lines Cleared") {

            Element* e = elementList.back();
            elementList.pop_back();

            delete e;
            options--;
        }
    } else {
        if (elementList.back()->getLabel() != "Lines Cleared") {
            elementList.push_back(new ZenLinesElement());
            refreshText = true;
            options++;
        }
    }

    PlayOptionScene::update();
}

#endif

int lastState = 0;
void MultBattleScene::init() {
#ifdef MULTIBOOT
    toggleBG(3, false);

    loadSpriteTiles(512 + 64, title1tiles_bin, 8, 4);
    loadSpriteTiles(512 + 96, title2tiles_bin, 8, 4);

    loadPalette(12 + 16, 0, title_pal_bin, title_pal_bin_size / 2);
    loadTiles(2, 102, sprite37tiles_bin, sprite37tiles_bin_size / 32);
    loadTiles(2, 105, sprite41tiles_bin, sprite41tiles_bin_size / 32);

    setSkin();
    setLightMode();

    clearSprites(128);

    // backgroundGrid
    setTiles(26, 0, 32 * 32,
             tileBuild(35 * (!savefile->settings.lightMode), false, false, 0));

    for (int i = 0; i < MAX_WORD_SPRITES; i++)
        wordSprites[i] = new WordSprite(i, 32 + i * 3, 256 + i * 12);

    for (int i = 0; i < MAX_WORD_SPRITES; i++)
        wordSprites[i]->setup(i, 64 + i * 3, 256 + i * 12, false);

    clearSpriteTiles(256, 12 * MAX_WORD_SPRITES, 1);

    enableBlend((0b000000 << 8) + (1 << 6) + (1 << 3));
#elif defined(GBA)
    multiplayerLink->universal->setProtocol(multiplayerProtocol);
    rumbleHandler = interrupt_get_handler(INTR_SERIAL);
    interrupt_set_handler(INTR_SERIAL, LINK_UNIVERSAL_ISR_SERIAL);
#endif
#ifndef MULTIBOOT
    PlayOptionScene::init();
#endif

    multiplayerLink->active = true;

    lastState = 0;

    clearSpriteTiles(672, 144, 1);
    resetSmallText();

    // Sync winTargetIndex with current multiplayerLink target (e.g. after Play Again)
    if (multiplayerLink->targetWins > 0) {
        for (int i = 0; i < 3; i++) {
            if (winTargets[i] == multiplayerLink->targetWins) {
                winTargetIndex = i;
                break;
            }
        }
    }
}

void MultBattleScene::draw() {
    fallingBlocks();
#ifdef N3DS
    // Scroll falling blocks to top of screen when in 1x scale:
    setLayerScroll(2, 0, 40);
#endif

    handleConnectionDisplay();

#ifndef MULTIBOOT
    if (choosingWinTarget)
        showWinTargetCursor();
    else
        for (int i = 0; i < 2; i++)
            sprite_hide(cursorSprites[i]);
#endif

    showSprites(128);
}

void MultBattleScene::showWinTargetSelection() {
    clearText();

    for (int i = 0; i < MAX_WORD_SPRITES; i++)
        wordSprites[i]->hide();
#ifndef MULTIBOOT
    for (int i = 0; i < 7; i++)
        labels[i]->hide();
#endif

    const std::string targets = "3   5   7";

    naprint("SET MATCH TARGET", 120 - getVariableWidth("SET MATCH TARGET") / 2,
            7 * 8);
    naprint(targets, 120 - getVariableWidth(targets) / 2, 9 * 8);
    const std::string confirmText =
        getStringFromKey(savefile->settings.menuKeys.confirm) + ": Confirm";
    naprint(confirmText, 120 - getVariableWidth(confirmText) / 2, 13 * 8);
}

void MultBattleScene::showWinTargetCursor() {
#ifndef MULTIBOOT
    const std::string targets = "3   5   7";
    const int targetX = 120 - getVariableWidth(targets) / 2;
    const int selectedOffset =
        getVariableWidth(targets.substr(0, winTargetIndex * 4));
    const int selectedWidth =
        getVariableWidth(std::to_string(winTargets[winTargetIndex]));
    const int centerX = targetX + selectedOffset + selectedWidth / 2;
    const int offset = (sinLut(cursorFloat) * 2) >> 12;
    FIXED scale = float2fx((1.0 - ((float)0.1 * offset)));

    for (int i = 0; i < 2; i++) {
        sprite_unhide(cursorSprites[i], 0);
        sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 1);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_set_size(cursorSprites[i], scale, i);

        const int x =
            centerX - ((selectedWidth + 8) / 2 + offset + 4) *
                          (i ? -1 : 1) -
            8;
        sprite_set_pos(cursorSprites[i], x, 9 * 8 - 5);
    }
#else
    // MULTIBOOT does not initialize OptionListScene cursor sprites.
#endif
}

void MultBattleScene::handleConnectionDisplay() {
    int state;
    if (multiplayerLink->players[multiplayerLink->universal->currentPlayerId()]
            .ready)
        state = 1;
    else if (multiplayerLink->stateMachine
                 .IsInState<MultiplayerStates::PlayersAwaitReady>())
        state = 2;
    else
        state = 3;

    if (choosingWinTarget) {
        for (int i = 0; i < MAX_WORD_SPRITES; i++)
            wordSprites[i]->hide();
#ifndef MULTIBOOT
        for (int i = 0; i < 7; i++)
            labels[i]->hide();
#endif
        return;
    }

    if (multiplayerLink->players[multiplayerLink->universal->currentPlayerId()]
            .ready &&
        !multiplayerLink->stateMachine.IsInState<MultiplayerStates::Lost>()) {
        wordSprites[7]->setText("Ready!");
#if !defined(NO_DIAGNOSE) && !defined(MULTIBOOT)
        if (enableBot) {
            wordSprites[7]->setText("bot Ready!");
        }
#endif
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::PlayersAwaitReady>()) {
        wordSprites[7]->setText("Not Ready");
#if !defined(NO_DIAGNOSE) && !defined(MULTIBOOT)
        if (enableBot) {
            wordSprites[7]->setText("botNot Ready");
        }
#endif
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::Playing>()) {
        wordSprites[7]->setText("Starting!");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::Lost>()) {
        wordSprites[7]->setText("Waiting...");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::Won>()) {
        wordSprites[7]->setText("Won!");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::PlayersSeeding>()) {
        wordSprites[7]->setText("Seeding...");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::PlayersPresent>()) {
        wordSprites[7]->setText("Syncing...");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else if (multiplayerLink->stateMachine
                   .IsInState<MultiplayerStates::Activated>()) {
        wordSprites[7]->setText("Reconnecting...");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    } else {
        wordSprites[7]->setText("Connecting...");
        wordSprites[7]->show(236 - wordSprites[7]->width, 160 - 12, 15);
    }

    if (multiplayerLink->universal->isConnected() && state != 3)
        showReadyPlayers();
    else
        hideReadyPlayers();

    while (debugConnection.size() > 10)
        debugConnection.pop_front();

#ifndef NO_DIAGNOSE
    int c = 0;
    for (auto const& player : multiplayerLink->players) {
        std::string txt =
            ((player.first == multiplayerLink->universal->currentPlayerId())
                 ? ">"
                 : " ") +
            std::to_string(player.first) + " " + player.second.toString();
        aprint(txt, 3, 3 + c++);
    }
#endif

    // c = 0;
    // for (auto const& str : debugConnection) {
    //     aprint(str, 3, 8 + c++);
    // }

    if (lastState == state)
        return;

    clearText();
    lastState = state;
    switch (state) {
    case 1:
    case 2:
        if (state == 1)
            naprint("                    ", 6 * 8, 15 * 8);
        else {
            const std::string confirmText =
                "Press " +
                getStringFromKey(savefile->settings.menuKeys.confirm) +
                " to ready up!";
            naprint(confirmText, 120 - getVariableWidth(confirmText) / 2,
                    15 * 8);
        }

#ifdef GBA
        naprint("Connected!", 10 * 8, 8 * 8);
        naprint("Waiting for all players...", 4 * 8, 10 * 8);
#else
        naprint("Waiting for all players...", 4 * 8, 4 * 8);
#endif

        break;
    default:
        naprint("Waiting for connection...", 5 * 8, 9 * 8);
        break;
    }
}

bool MultBattleScene::control() {
    MenuKeys k = savefile->settings.menuKeys;

    if (choosingWinTarget) {
        if (key_hit(k.cancel)) {
            choosingWinTarget = false;
            clearText();
            showPath();
            lastState = 0;
            sfx(SFX_MENUCANCEL);
        } else if (key_hit(k.left) && winTargetIndex > 0) {
            winTargetIndex--;
            showWinTargetSelection();
            sfx(SFX_MENUMOVE);
        } else if (key_hit(k.right) && winTargetIndex < 2) {
            winTargetIndex++;
            showWinTargetSelection();
            sfx(SFX_MENUMOVE);
        } else if (key_hit(k.confirm) || key_hit(k.pause)) {
            multiplayerLink->setMatchTarget(winTargets[winTargetIndex]);
            multiplayerLink->broadcastState(UPDATE_STATE + MATCH_TARGET +
                                            multiplayerLink->targetWins);
            multiplayerLink->readyUp();
            choosingWinTarget = false;
            clearText();
            showPath();
            lastState = 0;
            sfx(SFX_MENUCONFIRM);
        }
        return false;
    }

#ifndef MULTIBOOT
    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        if (!onScore || options == 0) {
            previousElement = name();
            previousSelection = selection;

            if (!path.empty())
                path.pop_back();

            changeScene(previousScene(), Transitions::FADE);

            multiplayerLink->deactivate(false);

            return true;
        } else {
            onScore = false;
            refreshText = true;
            movingDirection = -1;
            listStart = 0;
        }
    } else if (multiplayerLink
                   ->stateMachine
#else
    if (multiplayerLink
            ->stateMachine
#endif
                   .IsInState<MultiplayerStates::PlayersAwaitReady>() &&
               !multiplayerLink
                    ->players[multiplayerLink->universal->currentPlayerId()]
                    .ready &&
               (key_hit(k.confirm) || key_hit(k.pause))) {
        if (multiplayerLink->universal->currentPlayerId() == 0 &&
            multiplayerLink->targetWins == 0) {
            choosingWinTarget = true;
            showWinTargetSelection();
            sfx(SFX_MENUCONFIRM);
        } else {
            sfx(SFX_MENUCONFIRM);
            multiplayerLink->readyUp();
        }
    }

#if !defined(NO_DIAGNOSE) && !defined(MULTIBOOT)
    if (key_hit(k.special3)) {
        botThinkingSpeed = 1;
        botSleepDuration = 10;
        botStepMax = 1;
        enableBot = !enableBot;
    }
#endif

    return false;
}

void MultBattleScene::update() {
    PlayOptionScene::update();

    if (!multiplayerLink->active)
        return;

    if (multiplayerStart) {
        multiplayerStart = false;
        start();
        return;
    }

    if (multiplayerLink->sync()) {
#ifndef MULTIBOOT
        if (lostConnection) {
            if (multiplayerLink->active)
                sfx(SFX_SECRET);
            lostConnection = false;
        }
#endif
    } else {
        if (!lostConnection) {
            refreshText = true;
            clearText();
            lastState = 0;
            // TODO: Should go into commitPlayerCount
        }
        lostConnection = true;
    }
}

void MultBattleScene::start() {
    multiplayer = true;

    BlockEngine::Options newOptions;

    newOptions.mode = BlockEngine::BATTLE;
    newOptions.level = 1;
    newOptions.goal = 250;
    newOptions.tuning = getTuning();

    startGame(newOptions, nextSeed & (SEND_SEED - 1));
    nextSeed = 0;

    gameLoop();
}

void MultBattleScene::deinit() {
#ifdef GBA
    PlayOptionScene::deinit();
    multiplayerLink->deactivate(false);
    if (rumbleHandler)
        interrupt_set_handler(INTR_SERIAL, rumbleHandler);
#endif
}

#ifndef GBA
MultBattleScene::MultBattleScene() { this->matchMake = true; }

MultBattleScene::MultBattleScene(std::string room) { roomId = std::move(room); }
#else
/*
#ifndef MULTIBOOT
void SinglePakWirelessScene::pumpMultibootFeed() {
    if (lastResult == Link::AsyncMultiboot::Result::SUCCESS) {
        return;
    }
    if (multibootState == MultibootState::FAILED_INIT) {
        // Re-try in case adapter becomes live
        bool result = linkWirelessMultibootAsync->sendRom(
            Apotris_multi_gba, Apotris_multi_gba_size);
        if (result)
            multibootState = MultibootState::NOT_READY;
        return;
    }
    if (multibootState == MultibootState::NOT_READY) {
        return;
    }
    u8 progress = linkWirelessMultibootAsync->getPercentage();
    u8 playerCount = linkWirelessMultibootAsync->playerCount();
    if (lastResult == Link::AsyncMultiboot::Result::NONE &&
        (lastProgress != progress || lastPlayerCount != playerCount)) {
        clearText();
        naprint("Uploading game to " + std::to_string(playerCount - 1) +
                    " players...",
                0, 9 * 8);
        naprint("Transfer " + std::to_string(progress) + "% complete...", 0,
                11 * 8);
        refreshText = true;
        lastProgress = progress;
        lastPlayerCount = playerCount;
        return;
    }
    // Attempt to persist a result of success
    Link::AsyncMultiboot::Result result =
        linkWirelessMultibootAsync->getResult(false);
    if (result != Link::AsyncMultiboot::Result::SUCCESS) {
        // Clear result if not a success result
        linkWirelessMultibootAsync->getResult();
    }
    if (lastResult == result) {
        return;
    }
    lastResult = result;
    if (result == Link::AsyncMultiboot::Result::NONE) {
        return;
    }
    clearText();
    switch (result) {
    case Link::AsyncMultiboot::Result::INVALID_DATA:
        naprint("Invalid data", 3 * 8, 11 * 8);
        break;
    case Link::AsyncMultiboot::Result::FAILURE:
        naprint("Send failure", 3 * 8, 11 * 8);
        break;
    case Link::AsyncMultiboot::Result::INIT_FAILED:
        naprint("Init failure", 3 * 8, 11 * 8);
        break;
    default:
    case Link::AsyncMultiboot::Result::NONE:
        break;
    case Link::AsyncMultiboot::Result::SUCCESS:
        naprint("Successful!", 3 * 8, 11 * 8);
        break;
    }
}

void SinglePakWirelessScene::update() {
    PlayOptionScene::update();
    pumpMultibootFeed();
}

bool SinglePakWirelessScene::control() {
    MenuKeys k = savefile->settings.menuKeys;
    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        previousElement = name();
        previousSelection = selection;

        if (!path.empty())
            path.pop_back();

        changeScene(previousScene(), Transitions::FADE);

        return true;
    }
    if (key_hit(k.confirm)) {
        if (multibootState == MultibootState::NOT_READY &&
            linkWirelessMultibootAsync->playerCount() > 1) {
            sfx(SFX_MENUCONFIRM);
            multibootState = MultibootState::READY;
            linkWirelessMultibootAsync->markReady();
        }
    } else if (lastResult == Link::AsyncMultiboot::Result::SUCCESS) {
        sfx(SFX_MENUCONFIRM);
        multiplayerProtocol = LinkUniversal::Protocol::WIRELESS_AUTO;

        if (!path.empty())
            path.pop_back();

        changeScene([](){ return new MultBattleScene(); }, Transitions::FADE);

        return true;
    }
    return false;
}

void SinglePakWirelessScene::draw() {
    fallingBlocks();

    switch (multibootState) {
    case READY:
        break;
    case NOT_READY: {
        u8 playerCount = linkWirelessMultibootAsync->playerCount();
        if (lastPlayerCount == playerCount) {
            return;
        }
        lastPlayerCount = playerCount;
        if (playerCount > 1) {
            clearText();
            sfx(SFX_SECRET);
            naprint("Ready to send game to " + std::to_string(playerCount - 1) +
                        " players!",
                    0, 9 * 8);
            naprint("Press A to send multiboot!", 2 * 8, 10 * 8);
        } else {
            clearText();
            sfx(SFX_HOLD);
            naprint("      Waiting for others...     ", 0, 9 * 8);
        }
        break;
    }
    case NONE:
        break;
    default:
    case FAILED_INIT:
        clearText();
        naprint("No wireless adapter detected...", 2 * 8, 10 * 8);
        break;
    }
    showSprites(128);
}

void SinglePakWirelessScene::startMultibootTransfer() {
    // Begin transfer and setup interrupt
    linkWirelessMultibootAsync->reset();
    linkWirelessMultibootAsync->config.gameId = 0x0420;
    linkWirelessMultibootAsync->config.timerId = 1;
    linkWirelessMultibootAsync->config.waitForReadySignal = true;
    linkWirelessMultibootAsync->config.userName = savefile->latestName;
    interrupt_set_handler(INTR_SERIAL,
                          LINK_WIRELESS_MULTIBOOT_ASYNC_ISR_SERIAL);
    interrupt_set_handler(INTR_TIMER1, LINK_WIRELESS_MULTIBOOT_ASYNC_ISR_TIMER);
    linkVBlankHandler = LINK_WIRELESS_MULTIBOOT_ASYNC_ISR_VBLANK;
    bool result = linkWirelessMultibootAsync->sendRom(Apotris_multi_gba,
                                                      Apotris_multi_gba_size);
    if (!result)
        multibootState = MultibootState::FAILED_INIT;
}

void SinglePakWirelessScene::init() {
    PlayOptionScene::init();
    clearSpriteTiles(672, 144, 1);
    startMultibootTransfer();
}

void SinglePakWirelessScene::deinit() {
    PlayOptionScene::deinit();
    if (lastResult != Link::AsyncMultiboot::Result::SUCCESS) {
        linkWirelessMultibootAsync->reset();
    }
    vsync();
    interrupt_set_handler(INTR_SERIAL, LINK_UNIVERSAL_ISR_SERIAL);
    interrupt_set_handler(INTR_TIMER1, LINK_UNIVERSAL_ISR_TIMER);
    linkVBlankHandler = LINK_UNIVERSAL_ISR_VBLANK;
}

void SinglePakWiredScene::pumpMultibootFeed() {
    if (lastResult == Link::AsyncMultiboot::Result::SUCCESS) {
        return;
    }
    if (multibootState == MultibootState::FAILED_INIT) {
        // Re-try in case adapter becomes live
        bool result = linkCableMultibootAsync->sendRom(Apotris_multi_gba,
                                                       Apotris_multi_gba_size);
        if (result)
            multibootState = MultibootState::NOT_READY;
        return;
    }
    if (multibootState == MultibootState::NOT_READY) {
        return;
    }
    u8 progress = linkCableMultibootAsync->getPercentage();
    u8 playerCount = linkCableMultibootAsync->playerCount();
    if (lastResult == Link::AsyncMultiboot::Result::NONE &&
        (lastProgress != progress || lastPlayerCount != playerCount)) {
        clearText();
        naprint("Uploading game to " + std::to_string(playerCount - 1) +
                    " players...",
                0, 9 * 8);
        naprint("Transfer " + std::to_string(progress) + "% complete...", 0,
                11 * 8);
        refreshText = true;
        lastProgress = progress;
        lastPlayerCount = playerCount;
        return;
    }
    // Attempt to persist a result of success
    Link::AsyncMultiboot::Result result =
        linkCableMultibootAsync->getResult(false);
    if (result != Link::AsyncMultiboot::Result::SUCCESS) {
        // Clear result if not a success result
        linkCableMultibootAsync->getResult();
    }
    if (lastResult == result) {
        return;
    }
    lastResult = result;
    if (result == Link::AsyncMultiboot::Result::NONE) {
        return;
    }
    clearText();
    switch (result) {
    case Link::AsyncMultiboot::Result::INVALID_DATA:
        naprint("Invalid data", 3 * 8, 11 * 8);
        break;
    case Link::AsyncMultiboot::Result::FAILURE:
        naprint("Send failure", 3 * 8, 11 * 8);
        break;
    case Link::AsyncMultiboot::Result::INIT_FAILED:
        naprint("Init failure", 3 * 8, 11 * 8);
        break;
    default:
    case Link::AsyncMultiboot::Result::NONE:
        break;
    case Link::AsyncMultiboot::Result::SUCCESS:
        naprint("Successful!", 3 * 8, 11 * 8);
        break;
    }
}

void SinglePakWiredScene::update() {
    PlayOptionScene::update();
    pumpMultibootFeed();
}

bool SinglePakWiredScene::control() {
    MenuKeys k = savefile->settings.menuKeys;
    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        previousElement = name();
        previousSelection = selection;

        if (!path.empty())
            path.pop_back();

        changeScene(previousScene(), Transitions::FADE);

        return true;
    }
    if (key_hit(k.confirm)) {
        if (multibootState == MultibootState::NOT_READY &&
            linkCableMultibootAsync->playerCount() > 1) {
            sfx(SFX_MENUCONFIRM);
            multibootState = MultibootState::READY;
            linkCableMultibootAsync->markReady();
        }
    } else if (lastResult == Link::AsyncMultiboot::Result::SUCCESS) {
        sfx(SFX_MENUCONFIRM);
        multiplayerProtocol = LinkUniversal::Protocol::CABLE;

        if (!path.empty())
            path.pop_back();

        changeScene([](){ return new MultBattleScene(); }, Transitions::FADE);

        return true;
    }
    return false;
}

void SinglePakWiredScene::draw() {
    fallingBlocks();

    switch (multibootState) {
    case READY:
        break;
    case NOT_READY: {
        u8 playerCount = linkCableMultibootAsync->playerCount();
        if (lastPlayerCount == playerCount) {
            return;
        }
        lastPlayerCount = playerCount;
        if (playerCount > 1) {
            clearText();
            sfx(SFX_SECRET);
            naprint("Ready to send game to " + std::to_string(playerCount - 1) +
                        " players!",
                    0, 9 * 8);
            naprint("Press A to send multiboot!", 2 * 8, 10 * 8);
        } else {
            clearText();
            sfx(SFX_HOLD);
            naprint("      Waiting for others...     ", 0, 9 * 8);
        }
        break;
    }
    case NONE:
        break;
    default:
    case FAILED_INIT:
        clearText();
        naprint("No cable detected...", 2 * 8, 10 * 8);
        break;
    }
    showSprites(128);
}

void SinglePakWiredScene::startMultibootTransfer() {
    // Begin transfer and setup interrupt
    linkCableMultibootAsync->reset();
    linkCableMultibootAsync->config.waitForReadySignal = true;
    interrupt_set_handler(INTR_SERIAL, LINK_CABLE_MULTIBOOT_ASYNC_ISR_SERIAL);
    linkVBlankHandler = LINK_CABLE_MULTIBOOT_ASYNC_ISR_VBLANK;
    bool result = linkCableMultibootAsync->sendRom(Apotris_multi_gba,
                                                   Apotris_multi_gba_size);
    if (!result)
        multibootState = MultibootState::FAILED_INIT;
}

void SinglePakWiredScene::init() {
    PlayOptionScene::init();
    clearSpriteTiles(672, 144, 1);

    key_poll();
    if (key_held(KEY_L) || key_held(KEY_R)) {
        linkCableMultibootAsync->config.mode =
            LinkCableMultiboot::TransferMode::SPI;
    } else {
        linkCableMultibootAsync->config.mode =
            LinkCableMultiboot::TransferMode::MULTI_PLAY;
    }

    startMultibootTransfer();
}

void SinglePakWiredScene::deinit() {
    PlayOptionScene::deinit();
    interrupt_set_handler(INTR_SERIAL, LINK_UNIVERSAL_ISR_SERIAL);
    interrupt_set_handler(INTR_TIMER1, LINK_UNIVERSAL_ISR_TIMER);
    linkVBlankHandler = LINK_UNIVERSAL_ISR_VBLANK;
    if (lastResult != Link::AsyncMultiboot::Result::SUCCESS) {
        linkCableMultibootAsync->reset();
    }
}
#endif
*/
#endif

void PlayOptionScene::resetScoreboard() {
    vsync();

    resetSmallText();
    clearText();

    clearSprites(128);
    showSprites(128);

    int timer = 0;

    int s = 1;

    naprint("Are you sure you", 6 * 8, 6 * 8);
    naprint("want to reset the", 6 * 8, 8 * 8);
    naprint("current scoreboard?", 6 * 8, 10 * 8);

    bool reset = false;

    MenuKeys k = savefile->settings.menuKeys;

    while (closed()) {
        vsync();
        key_poll();

        fallingBlocks();
#ifdef N3DS
        // Scroll falling blocks to top of screen when in 1x scale:
        setLayerScroll(2, 0, 40);
#endif

        if (timer < 200) {
            timer++;
        } else if (timer == 200) {
            timer++;
            rumblePattern(RUMBLE_ERR_2);
        } else {
            aprint(" YES      NO ", 8, 15);

            int cursorX = 0;
            int length = 0;
            if (s == 0) {
                cursorX = 11 * 8 - 2;
                length = 3 * 8;
            } else {
                cursorX = 19 * 8 + 2;
                length = 2 * 8;
            }

            int offset = (sinLut(cursorFloat) * 2) >> 12;
            FIXED scale = float2fx((1.0 - ((float)0.1 * offset)));

            for (int i = 0; i < 2; i++) {
                sprite_unhide(cursorSprites[i], 0);
                sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 1);
                sprite_enable_affine(cursorSprites[i], i, true);
                sprite_set_size(cursorSprites[i], scale, i);

                int x = cursorX -
                        ((length + 8) / 2 + offset + 4) * ((i) ? -1 : 1) - 10;

                sprite_set_pos(cursorSprites[i], x, 15 * 8 - 5);
            }

            if (key_hit(k.confirm)) {
                clearText();
                clearSprites(128);
                sfx(SFX_MENUCONFIRM);
                if (s == 0) {
                    reset = true;
                    break;
                } else {
                    break;
                }
            }

            if (key_hit(k.cancel)) {
                clearText();
                sfx(SFX_MENUCANCEL);
                break;
            }

            if (key_hit(k.left)) {
                if (s > 0) {
                    s--;
                    sfx(SFX_MENUMOVE);
                }
            }

            if (key_hit(k.right)) {
                if (s < 1) {
                    s++;
                    sfx(SFX_MENUMOVE);
                }
            }
        }

        showSprites(128);

        cursorFloat += 6;
        if (cursorFloat >= 512)
            cursorFloat = 0;
    }

    if (reset) {
        if (getMode() != BlockEngine::ZEN) {
            EntryBoard* board = getBoard(subMode, goal);

            memset32_fast(board, 0, sizeof(EntryBoard) / 4);
        } else {
            if (subMode == 0) {
                savefile->boards.zen = 0;
                savefile->boards.zenLines = 0;
            } else if (subMode == 1) {
                savefile->boards.zenZone = 0;
                savefile->boards.zenLines = 0;
            } else {
                savefile->boards.zenTower = 0;
            }
        }

        saveSavefile();
    }

    showPath();
}

#if !defined(MULTIBOOT) && !defined(GBA)

void MatchMakingScene::init() {
#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
    fetchOnlinePlayerCount();
#endif

    clearSprites(128);
    showSprites(128);

    clearSpriteTiles(256, 12 * 2, 1);
    for (int i = 0; i < 2; i++)
        wordSprites[i].setup(i, 64 + i * 3, 256 + i * 12, false);

    clearTilemap(29);
#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
    drawMatchMakingText();
#else
    std::string str1 = "Who would you like";
    std::string str2 = "to play with?";
    naprint(str1, (240 - getVariableWidth(str1)) / 2, 56);
    naprint(str2, (240 - getVariableWidth(str2)) / 2, 66);
#endif

    wordSprites[0].setText("FRIENDS");
    wordSprites[0].show(48, 104, 14);

    wordSprites[1].setText("RANDOMS");
    wordSprites[1].show(120, 104, 14);

    // backgroundGrid
    setTiles(26, 0, 32 * 32,
             tileBuild(35 * (!savefile->settings.lightMode), false, false, 0));

    setTiles(27, 0, 32 * 32, tileBuild(34, false, false, 0));

    lengths[0] = wordSprites[0].width;
    lengths[1] = wordSprites[1].width;

    pos[0] = 48 + lengths[0] / 2;
    pos[1] = 120 + lengths[1] / 2;

    loadSpriteTiles(16 * 7, blockSprite, 1, 1);
    for (int i = 0; i < 2; i++) {
        cursorSprites[i] = &obj_buffer[1 + i];
        sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 1);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_hide(cursorSprites[i]);
    }

    enableBlend((0b101110 << 8) + (1 << 6) + (1 << 3));
}

std::string nameInput(int, bool* = nullptr);

bool MatchMakingScene::control() {
    MenuKeys k = savefile->settings.menuKeys;

    if (key_hit(k.left) || key_hit(k.right)) {
        playRandoms = !playRandoms;
        sfx(SFX_MENUMOVE);
    }

    if (key_hit(k.confirm)) {
        sfx(SFX_MENUCONFIRM);
        if (!playRandoms) {
            clearText();
            clearSmallText();
            hideSprites = true;
#ifdef GBA
            changeScene([]() { return new MultBattleScene(); },
                        Transitions::FADE);
#else
            bool cancelled = false;
            roomId = nameInput(-1, &cancelled);
            if (cancelled) {
                changeScene([]() { return new MainMenuScene(); },
                            Transitions::FADE);
                return false;
            }

            // Keep malformed input from reaching the WebSocket connection if
            // the input loop exits without accepting a room code.
            if (roomId.find_first_not_of(' ') == std::string::npos) {
                roomId.clear();
                changeScene([]() { return new MatchMakingScene(); },
                            Transitions::FADE);
                return false;
            }

            changeScene([]() { return new MultBattleScene(roomId); },
                        Transitions::FADE);
#endif
        } else {
#ifdef GBA
            changeScene([]() { return new MultBattleScene(); },
                        Transitions::FADE);
#else
            roomId = "";
            changeScene([]() { return new MultBattleScene(); },
                        Transitions::FADE);
#endif
        }
    } else if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        changeScene([]() { return new MainMenuScene(); }, Transitions::FADE);
    }

    return false;
}

void MatchMakingScene::draw() {
    fallingBlocks();

    int offset = (sinLut(cursorFloat) * 2) >> 12;
    FIXED scale = float2fx((1.0 - ((float)0.1 * offset)));

    const int y = 104;

    for (int i = 0; i < 2; i++) {
        sprite_unhide(cursorSprites[i], 0);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_set_size(cursorSprites[i], scale, i);

        int x = pos[playRandoms] -
                ((lengths[playRandoms] + 8) / 2 + offset + 4) * ((i) ? -1 : 1) -
                8;

        sprite_set_pos(cursorSprites[i], x, y - 5);
    }

#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
    const int totalPlayers = onlinePlayerCount.load();
    if (!onlinePlayerCountShown && totalPlayers >= 0) {
        drawMatchMakingText(totalPlayers);
        onlinePlayerCountShown = true;
    }
#endif

    if (!hideSprites) {
        wordSprites[0].show(48, 104, 14 + (playRandoms == 0));
        wordSprites[1].show(120, 104, 14 + (playRandoms == 1));
    } else {
        wordSprites[0].hide();
        wordSprites[1].hide();
    }

    showSprites(128);
}

void MatchMakingScene::update() {
    canDraw = true;
    key_poll();

    cursorFloat += 6;
    if (cursorFloat >= 512)
        cursorFloat = 0;

#if defined(PC) || defined(ANDROID) || defined(PORTMASTER)
    if (++timer >= ONLINE_PLAYER_COUNT_REFRESH_FRAMES) {
        timer = 0;
        onlinePlayerCountShown = false;
        fetchOnlinePlayerCount();
    }
#endif

    control();
}

void MatchMakingScene::deinit() {
    clearText();
    clearSmallText();
    resetSmallText();
}
#endif
