#include "def.h"
#include "sceneModes.hpp"
#include "sceneMultiplayerResults.hpp"
#include "sprites.h"
#include "text.h"
#include <algorithm>

namespace {
constexpr int resultsButtonY = 16;
constexpr int resultsButtonRight = 232;
} // namespace

void MultiplayerResultsScene::buildDisplayLines() {
    displayLines.clear();

    std::vector<std::pair<u8, Player>> standings;
    for (const auto& player : multiplayerLink->players)
        standings.push_back(player);

    std::sort(standings.begin(), standings.end(),
              [](const auto& left, const auto& right) {
                  if (left.second.wins != right.second.wins)
                      return left.second.wins > right.second.wins;
                  return left.first < right.first;
              });

#ifndef GBA
    const auto playerNames = multiplayerLink->getPlayerNames();
#endif

    for (const auto& standing : standings) {
        const u8 playerId = standing.first;
        const Player& player = standing.second;
        std::string playerName = "Player " + std::to_string(playerId + 1);
#ifndef GBA
        const auto name = playerNames.find(playerId);
        if (name != playerNames.end() &&
            name->second.find_first_not_of(' ') != std::string::npos)
            playerName = name->second;
#endif
        displayLines.push_back(
            playerName + " - " + std::to_string(player.wins) + "W - " +
            std::to_string(player.attacksPerMinute()) + "APM");
    }
}

void MultiplayerResultsScene::init() {
    reset();
    resetSmallText();
    clearText();
    clearSprites(128);

    buildDisplayLines();

    // backgroundGrid
    setTiles(26, 0, 32 * 32,
             tileBuild(35 * (!savefile->settings.lightMode), false, false, 0));
    setTiles(27, 0, 32 * 32, tileBuild(34, false, false, 0));

    for (int i = 0; i < MAX_WORD_SPRITES; i++)
        wordSprites[i] = new WordSprite(i, 64 + i * 3, 256 + i * 12);

    loadSpriteTiles(16 * 7, blockSprite, 1, 1);
    for (int i = 0; i < 2; i++) {
        cursorSprites[i] = &obj_buffer[1 + i];
        sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 1);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_hide(cursorSprites[i]);
    }

    enableBlend((0b101111 << 8) + (1 << 6) + (1 << 3));

    path.clear();
    path.emplace_back("Play");
    path.emplace_back("Multi Battle");
    path.emplace_back("Results");

    listStart = 0;
    selection = 0;
    renderText();
}

void MultiplayerResultsScene::renderText() {
    clearText();

    naprint("MATCH RESULTS", 120 - getVariableWidth("MATCH RESULTS") / 2,
            1 * 8);

    if (!multiplayerLink->matchHistory.empty()) {
        const char* outcome =
            multiplayerLink->matchHistory.back().won ? "YOU WON" : "YOU LOST";
        naprint(outcome, 120 - getVariableWidth(outcome) / 2, 3 * 8);
    }

    int visibleCount = 0;
    for (int i = listStart;
         i < (int)displayLines.size() && visibleCount < maxVisible; i++) {
        naprint(displayLines[i], 120 - getVariableWidth(displayLines[i]) / 2,
                (5 + visibleCount * 2) * 8);
        visibleCount++;
    }

    naprint("Play Again", resultsButtonRight - getVariableWidth("Play Again"),
            resultsButtonY * 8);
    naprint("Main Menu", resultsButtonRight - getVariableWidth("Main Menu"),
            (resultsButtonY + 2) * 8);
}

void MultiplayerResultsScene::draw() {
    fallingBlocks();
    toggleBG(3, true);
    showSprites(128);
}

void MultiplayerResultsScene::update() {
    canDraw = true;
    key_poll();

    cursorFloat += 6;
    if (cursorFloat >= 512)
        cursorFloat = 0;
    int offset = (sinLut(cursorFloat) * 2) >> 12;
    FIXED scale = float2fx((1.0 - ((float)0.1 * offset)));

    for (int i = 0; i < 2; i++) {
        sprite_unhide(cursorSprites[i], 0);
        sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 0);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_set_size(cursorSprites[i], scale, i);

        const char* selectedOption =
            selection == 0 ? "Play Again" : "Main Menu";
        const int buttonX =
            resultsButtonRight - getVariableWidth(selectedOption);
        const int buttonY = resultsButtonY + selection * 2;
        const int x =
            i ? resultsButtonRight + offset - 4 : buttonX - offset - 12;

        sprite_set_pos(cursorSprites[i], x, buttonY * 8 - 5);
    }

    control();
}

bool MultiplayerResultsScene::control() {
    MenuKeys k = savefile->settings.menuKeys;

    if (key_hit(k.up)) {
        if (selection > 0) {
            selection--;
            sfx(SFX_MENUMOVE);
        }
    }
    if (key_hit(k.down)) {
        if (selection < optionCount - 1) {
            selection++;
            sfx(SFX_MENUMOVE);
        }
    }

    if (key_hit(k.confirm) || key_hit(k.pause)) {
        if (selection == 0) {
            // Play Again
            sfx(SFX_MENUCONFIRM);
            multiplayerLink->resetMatch();
            multiplayerLink->playAgain = true;
#ifndef GBA
            changeScene([]() { return new MultBattleScene(roomId); },
                        Transitions::FADE);
#else
            changeScene([]() { return new MultBattleScene(); },
                        Transitions::FADE);
#endif
            return true;
        } else if (selection == 1) {
            // Main Menu
            sfx(SFX_MENUCANCEL);
            multiplayerLink->deactivate(true);
            multiplayer = false;
            path.clear();
#ifndef MULTIBOOT
            changeScene([]() { return new MainMenuScene(); },
                        Transitions::FADE);
#else
            changeScene([]() { return new MultiplayerProtocolScene(); },
                        Transitions::FADE);
#endif
            return true;
        }
    }

    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        multiplayerLink->deactivate(true);
        multiplayer = false;
        path.clear();
#ifndef MULTIBOOT
        changeScene([]() { return new MainMenuScene(); }, Transitions::FADE);
#else
        changeScene([]() { return new MultiplayerProtocolScene(); },
                    Transitions::FADE);
#endif
        return true;
    }

    return false;
}

void MultiplayerResultsScene::deinit() {
    clearSprites(128);
    showSprites(128);
    clearText();

    for (auto& wordSprite : wordSprites)
        delete wordSprite;

    for (auto& cursorSprite : cursorSprites)
        sprite_hide(cursorSprite);
}
