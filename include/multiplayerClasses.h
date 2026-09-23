#pragma once

#include "platform.hpp"

#ifdef GBA

#ifndef MULTIBOOT
#include "LinkCableMultiboot.hpp"
#include "LinkWirelessMultiboot.hpp"
#endif
#include "LinkUniversal.hpp"
#include "def.h"
#include "scene.hpp"
#include <memory>

#else
#include "LinkWebRTC.hpp"
#endif

#include "def.h"
#include "hsm.h"
#include "scene.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

// Higher three bits of u16 for commands
#define SEND_MATCH_ATTACKS 0 // payload: attacks sent this game
#define SEND_MATCH_TIME 1    // payload: wins (3 bits), seconds (10 bits)
#define MATCH_STAT_WINS_SHIFT 10
#define MATCH_STAT_SECONDS_MASK 0x3FF
#define UPDATE_STATE (2 << 13)
#define SEND_ATTACK 3
#define ACK_ATTACK 4
#define SEND_ROW 5

// Lower 13 bits of u16 for CHANGE_STATE commands
#define BYE 0
#define I_WON 1
#define HELLO 2
#define WELCOME_BACK 3
#define LETS_PLAY 5
#define GAME_OVER 6
#define I_LOST 7

// Match packets use bits separate from seed and power state packets.
#define MATCH_COMPLETE 0x200
#define MATCH_TARGET 0x400

// These are upper bit masks
#define SEND_SEED 0x1000
#define POWER_LEVEL 0x800

std::string stringifyMultiplayerPacket(u16 packet);

using namespace hsm;
const int boardTile = 512 - 32 * 3 - 64 - 8;

void updateRank();

extern void clearEnemyBoard();

class RemotePlayerId {
public:
    u8 id;
    u8 index;

    explicit RemotePlayerId(u8 playerId);
};

class BoardUpdate {
public:
    int row = 0;
    bool fullBoard = false;
    RemotePlayerId playerId;

    BoardUpdate(int r, RemotePlayerId playerId, bool full = false)
        : playerId(playerId) {
        row = r;
        fullBoard = full;
    }
};

extern std::vector<BoardUpdate> enemyBoardUpdates;

class Command {
public:
    int command;
    int value;
    RemotePlayerId playerId;

    Command(RemotePlayerId playerId, u16 packet) : playerId(playerId) {
        command = packet >> 13;
        value = packet & 0x1FFF;
    }

    void execute() const;
};

class Player {
public:
    u8 linkId;
    bool seeded = false;
    bool ready = false;
    bool over = false;
    bool reconnected = false;
    int wins = 0;
    int attacks = 0;
    int attackSeconds = 0;

    explicit Player(u8 lId) { linkId = lId; }

    explicit Player() { linkId = -1; }

    [[nodiscard]] int attacksPerMinute() const {
        return attackSeconds ? attacks * 60 / attackSeconds : 0;
    }

    [[nodiscard]] std::string toString() const {
        std::string result;

        result += (seeded ? "1 " : "0 ");
        result += (ready ? "1 " : "0 ");
        result += (over ? "1 " : "0 ");
        result += (reconnected ? "1 " : "0 ");

        return result;
    }
};

struct MatchResult {
    bool won = false;
    int placement = 0;   // 0 = 1st place, 1 = 2nd, etc.
    int playerCount = 2; // players in the match
};

class MultiplayerLink {
private:
    bool allOverB = false;

    bool allSeeded(u8 playerCount) {
        if (playerCount <= 1 || players.size() <= 1)
            return false;
        return std::all_of(players.begin(), players.end(),
                           [](std::pair<const unsigned char, Player> i) {
                               return i.second.seeded;
                           });
    }

    bool allOver(u8 playerCount) {
        if (playerCount <= 1 || players.size() <= 1)
            return false;
        if (playerCount > players.size())
            return allOverB;
        return std::all_of(players.begin(), players.end(),
                           [](std::pair<const unsigned char, Player> i) {
                               return i.second.over;
                           });
    }

    bool allConnected(u8 playerCount) {
        if (playerCount <= 1 || players.size() <= 1)
            return false;
        return std::all_of(players.begin(), players.end(),
                           [](std::pair<const unsigned char, Player> i) {
                               return i.second.reconnected;
                           });
    }

    void markAllOver(u8 playerCount) { allOverB = true; }

    friend struct MultiplayerStates;

public:
    LinkUniversal* universal;
    std::map<u8, Player> players;
    StateMachine stateMachine;
    std::queue<u16> sendQueue;
#ifdef GBA
    std::queue<Packet> seedQueue;
#endif

    unsigned int playingPlayerCount = 0;
    bool playing = false;
    bool active = false;
    bool playAgain = false;
    int wins = 0;
    int losses = 0;
    int targetWins = 0; // 0 = endless / no target
    std::vector<MatchResult> matchHistory;
    bool matchComplete = false;

    u32 enemyPower[4] = {1, 1, 1, 1};

    explicit MultiplayerLink(LinkUniversal* universal) {
        this->universal = universal;
    }

    bool sync();

    void UpdateStateMachine();

    void broadcastState(u16 packet) const;
    void sendEvent(u16 packet);

    void reset() {
        playingPlayerCount = 0;
        if (targetWins == 0) {
            wins = losses = 0;
            matchHistory.clear();
        }
        playing = false;
        matchComplete = false;
        universal->sync();
        nextSeed = 0;
        purgeQueues();
        players.clear();
    }

    void resetSession() {
        wins = losses = 0;
        targetWins = 0;
        matchHistory.clear();
        matchComplete = false;
        reset();
    }

    void resetMatch() {
        wins = losses = 0;
        targetWins = 0;
        matchHistory.clear();
        matchComplete = false;
        playing = false;
        allOverB = false;
        purgeQueues();

        for (auto& player : players)
            player.second = Player(player.first);
    }

    void initPlayer(u8 i) { players[i] = Player(i); }

    void resetPlayers() {
        for (auto& i : players) {
            Player resetPlayer(i.first);
            resetPlayer.wins = i.second.wins;
            resetPlayer.attacks = i.second.attacks;
            resetPlayer.attackSeconds = i.second.attackSeconds;
            i.second = resetPlayer;
        }
    }

    void readyUp() { players[universal->currentPlayerId()].ready = true; }

    void setMatchTarget(int target) {
        if (target == 3 || target == 5 || target == 7)
            targetWins = target;
    }

    int getReadyCount() {
        int over = 0;
        for (auto& it : players)
            over += it.second.ready;
        return over;
    }

#ifndef GBA
    [[nodiscard]] std::unordered_map<u8, std::string> getPlayerNames() const {
        return universal->playerNames();
    }
#endif

    void purgeQueues();

    void deactivate(bool sendBye);

    void activate();

    [[nodiscard]] bool isTargetReached() const {
        return targetWins > 0 && wins >= targetWins;
    }

    void recordMatchResult(bool won, int placement, int attacks,
                           int gameFrames) {
        matchHistory.push_back({won, placement, (int)playingPlayerCount});

        Player& player = players[universal->currentPlayerId()];
        player.wins += won;
        player.attacks += attacks;
        player.attackSeconds += std::max(1, (gameFrames + 30) / 60);

        sendEvent(
            (u16)((SEND_MATCH_ATTACKS << 13) | std::min(attacks, 0x1FFF)));
        sendEvent((u16)((SEND_MATCH_TIME << 13) |
                        ((std::min(player.wins, 7) << MATCH_STAT_WINS_SHIFT) |
                         std::min(std::max(1, (gameFrames + 30) / 60),
                                  MATCH_STAT_SECONDS_MASK))));
    }

    void recordRemoteMatchStat(RemotePlayerId playerId, int command,
                               int value) {
        Player& player = players[playerId.id];
        if (command == SEND_MATCH_ATTACKS)
            player.attacks += value;
        else if (command == SEND_MATCH_TIME) {
            player.wins = value >> MATCH_STAT_WINS_SHIFT;
            player.attackSeconds += value & MATCH_STAT_SECONDS_MASK;
        }
    }

    [[nodiscard]] std::queue<Packet> readPackets() const {
#ifdef TRACY_ENABLE
        ZoneScoped;
#endif
        std::queue<Packet> queue;
        for (auto player : players) {
            int i = player.first;
            while (universal->canRead(i))
                queue.emplace(i, universal->read(i));
        }
#ifndef GBA
        while (std::optional<BoardUpdatePacket> boardUpdate =
                   universal->readBoardUpdate()) {
#ifdef TRACY_ENABLE
            ZoneScopedN("read_board_update");
#endif
            auto remotePlayerId = RemotePlayerId(boardUpdate->playerId);
            if (remotePlayerId.index >= 4)
                continue;
            for (size_t row = 0; row < 20; row++) {
                for (size_t col = 0; col < 10; col++) {
                    enemyBoard[remotePlayerId.index][row][col] =
                        boardUpdate->board[row][col];
                }
            }
            enemyBoardUpdates.emplace_back(0, remotePlayerId, true);
        }
#endif
#ifdef TRACY_ENABLE
        ZoneTextF("count=%u", queue.size());
#endif
        return queue;
    }

    bool allReady(u8 playerCount) {
        if (playerCount <= 1 || players.size() <= 1)
            return false;
        return std::all_of(players.begin(), players.end(),
                           [](auto i) { return i.second.ready; });
    }

#ifndef GBA
    void removePlayer(u8 pId) { players.erase(pId); }
#endif
};

/**
 * Contains the various outer and inner states that power the multiplayer state
 * machine for networking. Outer states:
 * - Deactivated
 * - Activated
 */
struct MultiplayerStates {

    /**
     * When state is entered, will disable underlying network or wireless
     * hardware automatically. Is the default state when the state machine is
     * not initialized.
     */
    struct Deactivated : StateWithOwner<MultiplayerLink> {

        void OnEnter() override {
            if (Owner().universal->isActive()) {
                Owner().universal->deactivate();
            }
        }

        Transition GetTransition() override {
            if (!Owner().active) {
                return NoTransition();
            } else {
                return SiblingTransition<Activated>();
            }
        }
    };

    /**
     * This state manages an active network or GBA wireless connection and calls
     * to the underlying polling mechanism. It awaits the presence of players
     * and will set its own inner states. PlayersPresent is the entry to the
     * inner chain.
     */
    struct Activated : StateWithOwner<MultiplayerLink> {

        void OnEnter() override {
            if (!Owner().universal->isActive()) {
                Owner().universal->activate();
            }
            Update();
        }

        void Update() override { Owner().universal->sync(); }

        // Defaults to connection initializing on first entry and when a player
        // disconnects during a live game. Otherwise, proceeds to PlayersPresent
        // and attempts to seed, etc.
        Transition GetTransition() override {
            if (!Owner().universal->isActive())
                return SiblingTransition<Deactivated>();
            if (!Owner().universal->isConnected() ||
                (Owner().playing && Owner().universal->playerCount() <
                                        Owner().playingPlayerCount)) {
                return InnerEntryTransition<ConnectionInitializing>();
            }
            return InnerEntryTransition<PlayersPresent>();
        }
    };

    /**
     * This state captures resetting and/or persisting player connection data
     * for existing or new games before connection and during reconnection.
     */
    struct ConnectionInitializing : StateWithOwner<MultiplayerLink> {

        void OnEnter() override {
#ifndef GBA
            if (!Owner().playing) {
                Owner().resetPlayers(); // TODO: GBA needs remove?
            }
#else
            Owner().players.clear();
#endif
        }

        // Waits for correct player count or any connection before marking
        // remote players as "present"
        Transition GetTransition() override {
            if (!Owner().universal->isConnected() ||
                (Owner().playing && Owner().universal->playerCount() <
                                        Owner().playingPlayerCount)) {
                return NoTransition();
            } else {
                return SiblingTransition<PlayersPresent>();
            }
        }
    };

    /**
     * Handles the unique case of 3+P where only some players are present after
     * disconnect during a live game. Also handles resetting player ready states
     * before a game has started when a disconnect occurs.
     */
    struct PlayersPresent : StateWithOwner<MultiplayerLink> {
        unsigned int seenPlayerCount = 0;

        void OnEnter() override {
            seenPlayerCount = Owner().universal->playerCount();
#ifdef GBA
            for (int i = 0; i < Owner().universal->playerCount(); i++) {
                Owner().initPlayer(i);
            }
#endif
        }

        // In MultiplayerStates::PlayersPresent
        Transition GetTransition() override {
            if (Owner().playing) {
                // A player has dropped, wait for them to come back.
                if (Owner().playingPlayerCount >
                    Owner().universal->playerCount()) {
                    return SiblingTransition<ConnectionInitializing>();
                }
                // The player is back! Go right back to the game.
                else if (Owner().playingPlayerCount <=
                         Owner().universal->playerCount()) {
                    return SiblingTransition<Playing>();
                }
            }
            // This handles player changes in the lobby before a game starts.
            else if (seenPlayerCount != Owner().universal->playerCount()) {
                return SiblingTransition<ConnectionInitializing>();
            }

            // If no player changes, proceed to seeding as normal.
            return InnerEntryTransition<PlayersSeeding>();
        }
    };

    /**
     * Represents the state where some but not all players are synchronized on
     * the host-chosen seed. On entry, the host rolls a seed and transmits it to
     * players. All players await seed messages to confirm synchronization
     * before proceeding, so each client will have the same random state.
     */
    struct PlayersSeeding : StateWithOwner<MultiplayerLink> {
        int broadcastSeedCounter = 0;

        void OnEnter() override {
#ifdef GBA
            if (Owner().universal->currentPlayerId())
                return;

            if (nextSeed) {
                Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
                return;
            }

            if (!nextSeed)
                nextSeed = randNext() % (SEND_SEED - 1);

            Owner().players[Owner().universal->currentPlayerId()].seeded = true;
            Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
#endif
        }

        void Update() override {
            auto process_queue = [this](std::queue<Packet>& queue) {
                while (!queue.empty()) {
                    Packet packet = queue.front();
                    queue.pop();

                    auto command =
                        Command(RemotePlayerId(packet.playerId), packet.packet);
                    if (command.command != 2)
                        continue;

                    if (command.value == BYE) {
                        u32 newPlayerCount = Owner().playingPlayerCount - 1;
#ifndef GBA
                        removePlayer(packet.playerId);
#endif
                        clearEnemyBoard();
                        Owner().universal->activate();
                        Owner().universal->sync();
                        Owner().playingPlayerCount = newPlayerCount;
                    } else if (packet.packet & SEND_SEED) {
                        // Host logic: Mark players as seeded if they have the
                        // correct seed.
                        if ((packet.packet & (SEND_SEED - 1)) == nextSeed) {
                            Owner().players[packet.playerId].seeded = true;
                        }

#ifdef GBA
                        // Client logic: Only accept a seed from the host.
                        if (Owner().universal->currentPlayerId() != 0) {
                            if (packet.playerId == 0) {
                                nextSeed = packet.packet & (SEND_SEED - 1);
                                Owner().players[packet.playerId].seeded = true;
                            }
                        }
#endif
                    }
                }
            };
            std::queue<Packet> readQueue = Owner().readPackets();
            process_queue(readQueue);

#ifdef GBA
            std::queue<Packet> seedQueue = Owner().seedQueue;
            process_queue(seedQueue);
#endif

            if (nextSeed) {
                Owner().players[Owner().universal->currentPlayerId()].seeded =
                    true;
                // Prevent flooding weak clients like 3DS
                if ((broadcastSeedCounter++) % 32 == 0) {
                    Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
                }
            }
        }

        Transition GetTransition() override {
            if (Owner().allSeeded(Owner().universal->playerCount())) {
#ifndef GBA
                // There can be a race condition in which we received SEND_SEED
                // from all peers but we haven't sent SEND_SEED to the last
                // peer, so send SEND_SEED one last time.
                Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
#endif
                return SiblingTransition<PlayersSeeded>();
            }
            return NoTransition();
        }
    };

    /**
     * When all players are seeded, we set the playingPlayerCount here, but only
     * if not already in-game. This is how the player count gets determined
     * without confusion before the ready up stage. Relies on the reset
     * operations from earlier stages in the chain.
     */
    struct PlayersSeeded : StateWithOwner<MultiplayerLink> {

        void OnEnter() override {
            Owner().playingPlayerCount = Owner().universal->playerCount();
        }

        Transition GetTransition() override {
            if (!Owner().playing) {
                return SiblingTransition<PlayersAwaitReady>();
            } else {
                return NoTransition();
            }
        }
    };

    /**
     * Players send out and await ready-up messages from others in this state,
     * gated by a confirm button in the UI.
     */
    struct PlayersAwaitReady : StateWithOwner<MultiplayerLink> {
        bool sentReady = false;
        bool multiplayerStarting = false;

        void Update() override {
            std::queue<Packet> queue = Owner().readPackets();

            while (!queue.empty()) {
                Packet packet = queue.front();
                queue.pop();
                auto command =
                    Command(RemotePlayerId(packet.playerId), packet.packet);
                if (command.command != 2)
                    continue;
#ifdef GBA
                if (command.value & SEND_SEED) {
                    Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
                    continue;
                }
#endif
                if ((command.value & MATCH_TARGET) == MATCH_TARGET) {
                    if (packet.playerId == 0)
                        Owner().setMatchTarget(command.value & 0xFF);
                    continue;
                }
                if (command.value != LETS_PLAY)
                    continue;
                Owner().players[packet.playerId].ready = true;
            }

            if (!sentReady &&
                Owner().players[Owner().universal->currentPlayerId()].ready) {
                sentReady = true;
                Owner().broadcastState(UPDATE_STATE + LETS_PLAY);
#ifdef GBA
                // Help stragglers catch up (I hope I hope I hope)
                Owner().broadcastState(UPDATE_STATE + SEND_SEED + nextSeed);
#endif
            }

            if (Owner().allReady(Owner().playingPlayerCount)) {
                multiplayerStart = true;
                multiplayerStarting = true;
            }
        }

        Transition GetTransition() override {
            if (multiplayerStarting && !multiplayerStart) {
                return SiblingTransition<Playing>();
            }

            return NoTransition();
        }
    };

    /**
     * The game logic itself, which parses packets of all types into Command
     * classes that have effects when execute()'d It will transition into Won or
     * Lost states depending on flags set by game logic elsewhere in the
     * application.
     */
    struct Playing : StateWithOwner<MultiplayerLink> {
        int echoCounter = 0;

        void OnEnter() override {
            bool wasPlaying = Owner().playing;
            if (!wasPlaying) {
                Owner().playing = true;
            }
        }

        void Update() override {
            std::queue<Packet> queue = Owner().readPackets();
            while (!queue.empty()) {
                Packet packet = queue.front();
                queue.pop();

                auto command =
                    Command(RemotePlayerId(packet.playerId), packet.packet);

                if (command.command == SEND_MATCH_ATTACKS ||
                    command.command == SEND_MATCH_TIME) {
                    command.execute();
                } else if (command.command == 2) {
                    if (command.value == I_LOST)
                        Owner().players[command.playerId.id].over = true;
                    else if (command.value == I_WON) {
                        game->lost = true;
                        Owner().markAllOver(Owner().playingPlayerCount);
                    } else if (command.value == BYE) {
                        u32 newPlayerCount = Owner().playingPlayerCount - 1;
#ifndef GBA
                        removePlayer(packet.playerId);
#endif
                        clearEnemyBoard();
                        Owner().universal->activate();
                        Owner().universal->sync();
                        Owner().playingPlayerCount = newPlayerCount;
                    }
                } else
                    command.execute();
            }

            int lostCount = 0;
            for (auto i : Owner().players)
                if (i.second.over)
                    lostCount += 1;
            if (lostCount == Owner().playingPlayerCount - 1)
                game->won = true;
        }

        Transition GetTransition() override {
            if (game->lost) {
                return SiblingTransition<Lost>();
            } else if (game->won) {
                return SiblingTransition<Won>();
            }
            return NoTransition();
        }
    };

    /**
     * When a player has won, they announce it to the network and proceed to the
     * GameOver state.
     */
    struct Won : StateWithOwner<MultiplayerLink> {

        void OnEnter() override {
            Owner().broadcastState(UPDATE_STATE + I_WON);
            updateRank();
            Owner().recordMatchResult(true, rank, game->linesSent, game->timer);
            Owner().playing = false;
            Owner().resetPlayers();
            Owner().playAgain = true;
            Owner().wins++;
            if (Owner().isTargetReached()) {
                Owner().matchComplete = true;
                Owner().broadcastState(UPDATE_STATE + MATCH_COMPLETE);
            }
        }

        Transition GetTransition() override {
            return SiblingTransition<GameOver>();
        }
    };

    /**
     * When a player has lost, they continuously announce it to the network and
     * proceed to the GameOver state when hearing another player has won. This
     * state still parses remote board updates.
     */
    struct Lost : StateWithOwner<MultiplayerLink> {
        unsigned int echoCounter = 0;

        void OnEnter() override {
            Owner().broadcastState(UPDATE_STATE + I_LOST);
            updateRank();
            Owner().recordMatchResult(false, rank, game->linesSent,
                                      game->timer);
            Owner().players[Owner().universal->currentPlayerId()].over = true;
            Owner().losses++;
        }

        void Update() override {
            std::queue<Packet> queue = Owner().readPackets();
            while (!queue.empty()) {
                Packet packet = queue.front();
                queue.pop();
                auto command =
                    Command(RemotePlayerId(packet.playerId), packet.packet);
                if (command.command == SEND_MATCH_ATTACKS ||
                    command.command == SEND_MATCH_TIME) {
                    command.execute();
                } else if (command.command == 2) {
                    if (command.value == I_LOST) {
                        Owner().players[command.playerId.id].over = true;
                    } else if (command.value == I_WON) {
                        Owner().markAllOver(Owner().playingPlayerCount);
                    } else if (command.value == MATCH_COMPLETE) {
                        Owner().matchComplete = true;
                    } else if (command.value == BYE) {
                        u32 newPlayerCount = Owner().playingPlayerCount - 1;
#ifndef GBA
                        removePlayer(packet.playerId);
#endif
                        clearEnemyBoard();
                        Owner().universal->activate();
                        Owner().universal->sync();
                        Owner().playingPlayerCount = newPlayerCount;
                    }
#ifdef GBA
                    else if (command.value & SEND_SEED)
                        Owner().seedQueue.emplace(command.playerId.id,
                                                  (command.command << 13) +
                                                      command.value);
#endif
                } else if (command.command >= SEND_ROW) {
                    command.execute();
                }
                if (echoCounter++ % 60 == 0) {
                    Owner().broadcastState(UPDATE_STATE + I_LOST);
                }
            }
        }

        void OnExit() override {
            Owner().playing = false;
            Owner().resetPlayers();
            Owner().playAgain = true;
        }

        Transition GetTransition() override {
            if (!Owner().allOver(Owner().playingPlayerCount))
                return NoTransition();

            return SiblingTransition<GameOver>();
        }
    };

    /**
     * The GameOver state is a temporary state. It exists to announce to any
     * straggling players that are still in-game (perhaps due to dropped network
     * traffic) that the game is considered over and they must reset to re-seed.
     */
    struct GameOver : StateWithOwner<MultiplayerLink> {
        int transitionTimeout = 120; // 2 seconds
        unsigned int echoCounter = 0;

        void OnEnter() override {
            Owner().players[Owner().universal->currentPlayerId()].over = true;
            Owner().broadcastState(UPDATE_STATE + GAME_OVER);
            Owner().broadcastState(UPDATE_STATE + I_LOST);
        }

        void Update() override {
            if (transitionTimeout > 0) {
                transitionTimeout--;
            }

            std::queue<Packet> queue = Owner().readPackets();

            while (!queue.empty()) {
                Packet packet = queue.front();
                queue.pop();

                auto command =
                    Command(RemotePlayerId(packet.playerId), packet.packet);
                if (command.command == SEND_MATCH_ATTACKS ||
                    command.command == SEND_MATCH_TIME) {
                    command.execute();
                    continue;
                }
                if (command.command != 2)
                    continue;
                if (command.value == GAME_OVER) {
                    Owner().players[command.playerId.id].over = true;
                } else if (command.value == MATCH_COMPLETE) {
                    Owner().matchComplete = true;
                }
            }

            if (echoCounter++ % 10 == 0) {
                Owner().broadcastState(UPDATE_STATE + GAME_OVER);
                Owner().broadcastState(UPDATE_STATE + I_LOST);
                if (Owner().matchComplete)
                    Owner().broadcastState(UPDATE_STATE + MATCH_COMPLETE);
            }
        }

        void OnExit() override {
            Owner().broadcastState(UPDATE_STATE + GAME_OVER);
            Owner().resetPlayers();
            Owner().playing = false;
#ifndef GBA
            lastSeed += 1;
            nextSeed = lastSeed;
#endif
        }

        Transition GetTransition() override {
            if (transitionTimeout <= 0) {
                if (Owner().matchComplete || Owner().isTargetReached()) {
                    return SiblingTransition<MatchComplete>();
                }
                return SiblingTransition<PlayersSeeding>();
            }
            return NoTransition();
        }
    };

    struct MatchComplete : StateWithOwner<MultiplayerLink> {
        int holdTimer = 30;

        void OnEnter() override {
            Owner().matchComplete = true;
            Owner().playing = false;
            Owner().resetPlayers();
        }

        void Update() override {
            if (holdTimer > 0)
                holdTimer--;
        }

        Transition GetTransition() override {
            if (Owner().playAgain) {
                return SiblingTransition<PlayersSeeding>();
            }
            return NoTransition();
        }
    };
};
extern MultiplayerLink* multiplayerLink;
