#include "def.h"

#ifdef GBA
#include "LinkUniversal.hpp"
#include "sceneModes.hpp"
#endif
#include "blockEngine.hpp"
#include "logging.h"
#include "multiplayerClasses.h"
#include "rumble.h"
#include "scene.hpp"
#include "text.h"
#include <queue>
#include <string>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

using namespace BlockEngine;

int weightedRandomPlayerSelection();

u8 enemyBoard[4][20][10];
u8 attackAnimationTimers[4];

u32 ownPower = 1;

std::vector<BoardUpdate> enemyBoardUpdates;

OBJ_ATTR* enemyBoardSprite;

bool multiplayerStart = false;
int rank = 0;
int currentScanHeight = 0;

Game* botGame;
int botIncomingHeight = 0;

#define ENCODE(x) ((x) << 13)

void clearEnemyBoard() {
    memset32_fast((u32*)enemyBoard, 0, sizeof(enemyBoard) / 4);
    clearSpriteTiles(boardTile, 8, 8);

    for (auto& timer : attackAnimationTimers)
        timer = 0;

    clearSpriteTiles(boardTile, 2, 4);

    enemyBoardUpdates.clear();
}

// returns true if we should terminate the game
int GameScene::handleMultiplayer(bool duringGame) {
    if (!multiplayer)
        return 0;

#ifdef TRACY_ENABLE
    ZoneScopedC(tracy::Color::AliceBlue);
#endif

    // Update remote state and fetch incoming data
    bool playersPresent = multiplayerLink->sync();

    // Exit early for a later update if bad state (e.g. some player disconnects)
    if (!playersPresent && duringGame) {
        // If reconnect returns false, we got a connection back with all players
        if (reconnect()) {
            // Get potential new playerId
            multiplayerLink->sync();
            // Send out power and ID or lose state to map to new if changed
            u32 power = ownPower;
            if (game->lost)
                power = 0; // we lost
            multiplayerLink->broadcastState(UPDATE_STATE + POWER_LEVEL + power);
            return 0;
        } else {
            return 1;
        }
    }

    if (!duringGame && multiplayerLink->universal->playerCount() <
                           multiplayerLink->playingPlayerCount) {
        // If reconnect returns false, we got a connection back with all players

        return !reconnect();
    }

    if (!game->lost) {
        // Attacks take priority over block state
        if (!game->attackQueue.empty()) {
            // Grab next attack
            BlockEngine::Garbage atck = game->attackQueue.front();
            // Pop it out of pending attack queue
            game->attackQueue.pop_front();
            // Encode and send out attack
            u8 targetId = weightedRandomPlayerSelection();
            multiplayerLink->sendEvent(
                (u16)(ENCODE(SEND_ATTACK) + (targetId << 4) + atck.amount));
            ownPower += atck.amount;
        } else {
            if (duringGame) {
#ifdef GBA
                int rowsToSend = 1;
                u16 packet;
                bool sent;
                // Note that board updates do not use the send queue as they are
                // best-effort
                do {
                    // Rotate over the 20 rows
                    if (currentScanHeight < 19)
                        currentScanHeight++;
                    else
                        currentScanHeight = 0;

                    // Store row as each bit on or off for the presence of each
                    // block
                    u16 row_bitfield = 0;
                    for (int i = 0; i < 10; i++)
                        if (game->board[currentScanHeight + 20][i])
                            row_bitfield += 1 << i;

                    packet = (u16)(ENCODE(SEND_ROW) +
                                   ((currentScanHeight & 0x1f) << 10) +
                                   (row_bitfield & 0x3ff));
                    sent = multiplayerLink->universal->send(packet);

                } while (rowsToSend-- && sent); // avoid flooding
#else
                // Send full board updates every 16 frames instead of sending
                // two rows every frame because it is more efficient.
                if (game->timer % 16 == 0) {
                    std::array<std::byte, 104> packet;

                    constexpr u16 pkt_header = ENCODE(SEND_ROW) + (0x1f << 10);
                    packet[0] = std::byte(pkt_header & 0xff);
                    packet[1] = std::byte(pkt_header >> 8);

                    const u16 pkt_seq = game->timer / 16;
                    packet[2] = std::byte(pkt_seq & 0xff);
                    packet[3] = std::byte(pkt_seq >> 8);

                    for (size_t r = 0; r < 20; r++) {
                        bool clearing = game->lineClearArray[r + 20];
                        for (size_t col = 0; col < 5; col++) {
                            int cell0, cell1;
                            if (clearing) {
                                cell0 = cell1 = 9;
                            } else {
                                int val0 = game->board[r + 20][col * 2];
                                int val1 = game->board[r + 20][col * 2 + 1];
                                cell0 =
                                    (val0 <= 0) ? 0 : ((val0 - 1) & 0xf) + 1;
                                cell1 =
                                    (val1 <= 0) ? 0 : ((val1 - 1) & 0xf) + 1;
                            }
                            u8 packed = (cell0 & 0xF) | ((cell1 & 0xF) << 4);
                            packet[4 + r * 5 + col] = std::byte(packed);
                        }
                    }

                    multiplayerLink->universal->send(packet);
                }
#endif
            }
        }
    }

    drawEnemyBoard();

    return 0;
}

int weightedRandomPlayerSelection() {
    auto loseIndex = multiplayerLink->players;
    // Skip 2P mode
    if (multiplayerLink->playingPlayerCount == 2) {
        if (0 == multiplayerLink->universal->currentPlayerId()) {
            return 1; // choose the other player if current player is selected.
        }
        return 0;
    }

    // Calculate the total attack power
    u32 totalAttackPower = 0;
    for (int i = 0; i < multiplayerLink->playingPlayerCount; ++i) {
        if (i == multiplayerLink->universal->currentPlayerId())
            continue; // check if player is current one
        auto id = RemotePlayerId(i);
        if (loseIndex[id.id].over)
            continue;
        totalAttackPower += multiplayerLink->enemyPower[id.index];
    }

    // Pick randomly if no power
    if (!totalAttackPower) {
        u8 randomPlayer = static_cast<u8>((int)randNext() %
                                          multiplayerLink->playingPlayerCount);
        while (randomPlayer == multiplayerLink->universal->currentPlayerId() &&
               loseIndex[randomPlayer].over) {
            // get another random value if it equals the current player's id or
            // they lost
            randomPlayer = static_cast<u8>((int)randNext() %
                                           multiplayerLink->playingPlayerCount);
        }
        return randomPlayer;
    }

    // Generate a random number between 0 and totalAttackPower
    u32 r = randNext() % totalAttackPower;

    // Determine which player is selected
    u32 accumulatedPower = 0;
    for (int i = 0; i < multiplayerLink->playingPlayerCount; ++i) {
        if (i == multiplayerLink->universal->currentPlayerId())
            continue; // check if player is current one
        auto id = RemotePlayerId(i);
        if (loseIndex[id.id].over)
            continue;
        accumulatedPower += multiplayerLink->enemyPower[id.index];
        if (r < accumulatedPower) {
            return i; // Return the player ID
        }
    }

    // This point should never be reached if the enemyBoard is not empty
    // Try to find an alternative player than current one
    for (int i = 0; i < multiplayerLink->playingPlayerCount; ++i) {
        if (i != multiplayerLink->universal->currentPlayerId() &&
            !loseIndex[i].over) {
            return i;
        }
    }

    return -1; // Return an invalid player ID if no other player is found
}

void acknowledgeGarbage(BlockEngine::Game* sourceGame,
                        BlockEngine::Garbage garbage) {
    if (multiplayer) {
        multiplayerLink->sendEvent(ENCODE(ACK_ATTACK) + (garbage.id << 4) +
                                   garbage.amount);
    } else {
        if (sourceGame == botGame) {
            attackAnimationTimers[0] = animationMax * garbage.amount;
        }
    }
}

// Function to update the enemy board
void GameScene::UpdateEnemyBoard(int command, int value, u8 linkPlayerId) {
    auto remotePlayerId = RemotePlayerId(linkPlayerId);
    // Calculate the base height based on the command
    int baseHeight = (command - 5) * 8;
    // Calculate the row to be updated
    int row = (value >> 10) + baseHeight;
    // If the row is within the board's height
    if (row < 20 && remotePlayerId.index < 4) {
        // Loop through each column in the row
        for (int j = 0; j < 10; j++)
            // Update the cell in the enemy board based on the value
            enemyBoard[remotePlayerId.index][row][j] = (value >> j) & 0x1;

        enemyBoardUpdates.emplace_back(row, remotePlayerId);
    }
}

void resetMultiplayerGame() {

    clearEnemyBoard();

    for (auto& i : multiplayerLink->enemyPower)
        i = 1;

    rank = 0;
    ownPower = 1;
}

void drawEnemyBoard() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    if (!multiplayerLink->stateMachine.IsInState<MultiplayerStates::Lost>() &&
        (game->won || game->lost)) {
        return;
    }

    bool zoom = false;
    if (multiplayer) {
        if (multiplayerLink->playingPlayerCount <= 2) {
            zoom = true;
        }
    } else {
        zoom = true;
    }

    int x = 43 - 32;
    int y = 20 - 32;

    if (zoom) {
        x += 32;
        y += 32;
    }

    enemyBoardSprite = &obj_buffer[25];
    sprite_unhide(enemyBoardSprite, ATTR0_AFF_DBL);

    int boardPalette = 12;

#ifdef GBA
    boardPalette = 15;
#endif

    sprite_set_attr(enemyBoardSprite, ShapeSquare, 3, boardTile, boardPalette,
                    1);

    sprite_enable_affine(enemyBoardSprite, 25, true);
    sprite_set_pos(enemyBoardSprite, x, y);
    sprite_set_size(enemyBoardSprite, int2fx(1) >> zoom, 25);

    const int margin = 3;

    int count = 1;
    if (multiplayer) {
        count = multiplayerLink->playingPlayerCount - 1;
    }

    for (int i = 0; i < count; i++) {
        OBJ_ATTR* boardFrame = &obj_buffer[26 + i];

        const int offsetX = (i % 2) * (10 + margin) + (25) * !zoom;
        const int offsetY = (i / 2) * (20 + margin) + (17) * !zoom;

        sprite_set_attr(boardFrame, ShapeTall, 2, boardTile + 64,
                        14 + (attackAnimationTimers[i] > 0), 2);
        sprite_unhide(boardFrame, ATTR0_AFF_DBL);
        sprite_enable_affine(boardFrame, 6, true);
        sprite_set_pos(boardFrame, x + offsetX - 2, y + offsetY - 2);
        sprite_set_size(boardFrame, int2fx(1) >> zoom, 6);

        if (attackAnimationTimers[i])
            attackAnimationTimers[i]--;
    }

    for (auto i = 0; i < multiplayerLink->playingPlayerCount; i++) {
        if (i == multiplayerLink->universal->currentPlayerId())
            continue;
        auto id = RemotePlayerId(i);
        if (multiplayerLink->players[i].over) {
            const int offsetX = (id.index % 2) * (10 + margin);
            const int offsetY = (id.index / 2) * (20 + margin);
            for (int j = 0; j < 10; j++) {
                for (int yy = 0; yy < 20; yy++) {
                    auto yyy = yy + offsetY;
                    auto color = 2 + savefile->settings.lightMode;
                    const int xx = j + offsetX;
                    setSpritePixel(boardTile, xx / 8, yyy / 8, 8, xx % 8,
                                   yyy % 8, color);
                }
            }
        }
    }

    for (auto const& update : enemyBoardUpdates) {
        const int offsetX = (update.playerId.index % 2) * (10 + margin);
        const int offsetY = (update.playerId.index / 2) * (20 + margin);

        const int startRow = update.fullBoard ? 0 : update.row;
        const int endRow = update.fullBoard ? 19 : update.row;

        for (int r = startRow; r <= endRow; r++) {
            const int yy = r + offsetY;

            for (int j = 0; j < 10; j++) {
                const int xx = j + offsetX;

                int cell = enemyBoard[update.playerId.index][r][j];
                if (cell > 0) {
#ifdef GBA
                    setSpritePixel(boardTile, xx / 8, yy / 8, 8, xx % 8, yy % 8,
                                   2 + savefile->settings.lightMode);
#else
                    int entry = (cell == 9) ? 14 : cell + 5;
                    setSpritePixel(boardTile, xx / 8, yy / 8, 8, xx % 8, yy % 8,
                                   entry);
#endif
                } else
                    setSpritePixel(boardTile, xx / 8, yy / 8, 8, xx % 8, yy % 8,
                                   0);
            }
        }
    }

    for (auto i = 0; i < multiplayerLink->playingPlayerCount; i++) {
        if (i == multiplayerLink->universal->currentPlayerId())
            continue;
        auto id = RemotePlayerId(i);
        if (!multiplayerLink->players[i].over)
            continue;

        const int offsetX = (id.index % 2) * (10 + margin);
        const int offsetY = (id.index / 2) * (20 + margin);
        const int koColor = boardPalette == 15 ? 2 : 14;
        aprintsSprite("KO", offsetX + 1, offsetY + 7, boardTile, 8, koColor,
                      koColor);
    }

    enemyBoardUpdates.clear();
}

int botClearTimer = 0;
#ifndef MULTIBOOT
void handleBotGame() {

#ifdef GBA
    if (botIncomingHeight++ > 19) {
        botIncomingHeight = 0;
    }
#endif

    if (!botGame->attackQueue.empty()) {
        BlockEngine::Garbage atck = botGame->attackQueue.front();
        game->addToGarbageQueue(atck.id, atck.amount);
        botGame->clearAttack(atck.id);
    }

    if (!game->attackQueue.empty()) {
        BlockEngine::Garbage atck = game->attackQueue.front();
        botGame->addToGarbageQueue(atck.id, atck.amount);
        game->clearAttack(atck.id);
    }

    const auto id = RemotePlayerId(0);

    for (int i = 0; i < 20; i++) {
        bool clearing = botGame->lineClearArray[i + 20];
        for (int j = 0; j < 10; j++) {
            u8 cellType;
            if (clearing) {
                cellType = 9;
            } else {
                int val = botGame->board[i + 20][j];
                if (val <= 0)
                    cellType = 0;
                else
                    cellType = ((val - 1) & 0xf) + 1;
            }
            enemyBoard[id.index][i][j] = cellType;
        }
    }

#ifdef GBA
    enemyBoardUpdates.emplace_back(botIncomingHeight, id);
#else
    enemyBoardUpdates.emplace_back(0, id, true);
#endif
    drawEnemyBoard();

    if (botGame->clearLock) {
        if (++botClearTimer > botGame->maxClearDelay) {
            botClearTimer = 0;
            botGame->removeClearLock();
        }
    }

    if (botGame->lost) {
        game->won = true;
    }
}
#endif

// returns true on reconnect
bool GameScene::reconnect() {
    pauseSong();
    game->liftKeys();

    resetSmallText();
    clearText();
    int prevBld = blendInfo;

    for (auto& wordSprite : wordSprites)
        wordSprite.hide();

    enableBlend((1 << 6) + (0b11101 << 9) + (1 << 3));
    setTiles(27, 0, 32 * 32, tileBuild(34, false, false, 0));
    buildBG(3, 0, 27, 0, 0, 0);
    clearTilemap(25);

    drawFrame(1);
    if (game->zoneTimer)
        frameSnow(1);

    setLayerScroll(2, 0, 0);

    // hide Sprites
    hideMinos();
    sprite_hide(&obj_buffer[23]); // hide meter
    sprite_hide(&obj_buffer[24]); // hide finesse combo counter
    sprite_hide(&obj_buffer[25]); // hide enemyBoard
    for (int i = 0; i < 3; i++)
        sprite_hide(&obj_buffer[16 + i]);

    showSprites(128);

    rumblePatternStop();

    int cursorFloat = 0;
    OBJ_ATTR* cursorSprites[2];

    loadSpriteTiles(16 * 7, blockSprite, 1, 1);
    for (int i = 0; i < 2; i++) {
        cursorSprites[i] = &obj_buffer[1 + i];
        sprite_set_attr(cursorSprites[i], ShapeSquare, 0, 7 * 16, 5, 0);
        sprite_enable_affine(cursorSprites[i], i, true);
        sprite_hide(cursorSprites[i]);
    }

    MenuKeys k = savefile->settings.menuKeys;

    naprint("Connection Lost", 8, 8);

    std::string str = "Attempting to Reconnect...";
    naprint(str, 120 - getVariableWidth(str) / 2, 80);

    int timer = 0;

    std::string exit = "Exit";

    while (!multiplayerLink->sync() && closed()) {
        vsync();
        key_poll();

        if (!multiplayerLink->active) {
            multiplayer = false;
            enableBlend(prevBld);
            playSongRandom(0);
            buildBG(3, 0, 27, 0, 1, true);
            exceptionReason = "Another player has quit the game.";
            return false;
        }

        if (timer == 120) {
            naprint(exit, 120 - getVariableWidth(exit) / 2, 144);
        }

        if (timer >= 120) {
            if (key_hit(k.confirm)) {
                multiplayer = false;
                multiplayerLink->deactivate(true);

                sfx(SFX_MENUCANCEL);
                enableBlend(prevBld);
                playSongRandom(0);
                buildBG(3, 0, 27, 0, 1, true);
#ifndef MULTIBOOT
                changeScene([]() { return new MainMenuScene(); },
                            Transitions::FADE);
#else
                changeScene([]() { return new MultBattleScene(); },
                            Transitions::FADE);
#endif
                return false;
            }

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

                int x = 240 / 2 -
                        ((getVariableWidth(exit) + 8) / 2 + offset + 4) *
                            ((i) ? -1 : 1) -
                        8;

                sprite_set_pos(cursorSprites[i], x, 144 - 5);
            }
        }

        showSprites(128);
        timer++;
    }

    clearTilemap(27);
    clearTilemap(26);
    buildBG(3, 0, 27, 0, 1, true);

    enableBlend(prevBld);

    for (auto& wordSprite : wordSprites)
        wordSprite.hide();

    for (auto& cursorSprite : cursorSprites)
        sprite_hide(cursorSprite);

    drawFrame(0);
    if (game->zoneTimer)
        frameSnow(0);

    showBackground(0);
    resetSmallText();
    clearText();
    setSmallTextArea(110, 3, 7, 9, 10);
    showText();

    if (!(game->won || game->lost)) {
        countdown();
    } else {
        endScreenSetup();
    }

    resumeSong();

    return true;
}
