#include "multiplayerClasses.h"
#include "scene.hpp"
#include <stdexcept>

int lastSeed = 0;
std::list<std::string> debugConnection;

std::string stringifyMultiplayerPacket(u16 packet) {
    std::string str;
    const u16 cmd = packet >> 13;
    const u16 param = packet & ((1 << 13) - 1);
    switch (cmd) {
    case SEND_MATCH_ATTACKS:
        str = "SEND_MATCH_ATTACKS(" + std::to_string(param) + ')';
        break;
    case SEND_MATCH_TIME:
        str = "SEND_MATCH_TIME(w=" +
              std::to_string(param >> MATCH_STAT_WINS_SHIFT) +
              ",t=" + std::to_string(param & MATCH_STAT_SECONDS_MASK) + ')';
        break;
    case UPDATE_STATE >> 13: {
        str = "UPDATE_STATE(";
        switch (param) {
        case BYE:
            str += "BYE";
            break;
        case I_WON:
            str += "I_WON";
            break;
        case HELLO:
            str += "HELLO";
            break;
        case WELCOME_BACK:
            str += "WELCOME_BACK";
            break;
        case LETS_PLAY:
            str += "LETS_PLAY";
            break;
        case GAME_OVER:
            str += "GAME_OVER";
            break;
        case I_LOST:
            str += "I_LOST";
            break;
        case MATCH_COMPLETE:
            str += "MATCH_COMPLETE";
            break;
        default:
            if (param & MATCH_TARGET) {
                str += "MATCH_TARGET(";
                str += std::to_string(param & 0xFF);
                str += ')';
            } else if (param & SEND_SEED) {
                const u16 seed = param & (SEND_SEED - 1);
                str += "SEND_SEED(";
                str += std::to_string(seed);
                str += ')';
            } else if (param & POWER_LEVEL) {
                const u16 power = param & (POWER_LEVEL - 1);
                str += "POWER_LEVEL(";
                str += std::to_string(power);
                str += ')';
            } else {
                str += std::to_string(param);
            }
            break;
        }
        str += ')';
    } break;
    case SEND_ATTACK: {
        const u16 target_id = param >> 4;
        const u16 attack_amount = param & ((1 << 4) - 1);
        str = "SEND_ATTACK(t=";
        str += std::to_string(target_id);
        str += ",a=";
        str += std::to_string(attack_amount);
        str += ')';
    } break;
    case ACK_ATTACK: {
        const u16 garbage_id = param >> 4;
        const u16 garbage_amount = param & ((1 << 4) - 1);
        str = "ACK_ATTACK(i=";
        str += std::to_string(garbage_id);
        str += ",a=";
        str += std::to_string(garbage_amount);
        str += ')';
    } break;
    case SEND_ROW:
    case SEND_ROW + 1:
    case SEND_ROW + 2: {
        // Calculate the base height based on the command
        const u16 baseHeight = (cmd - SEND_ROW) * 8;
        // Calculate the row to be updated
        const u16 height = (param >> 10) + baseHeight;
        const u16 row_bitfield = packet & 0x3ff;
        str = "SEND_ROW(h=";
        str += std::to_string(height);
        str += ",r=";
        for (size_t i = 0; i < 10; i++) {
            str += (row_bitfield & (1 << (9 - i))) ? '1' : '0';
        }
        str += ")";
    } break;
    default:
        str = "CMD?<";
        str += std::to_string(cmd);
        str += ">(";
        str += std::to_string(param);
        str += ')';
        break;
    }
    return str;
}

template <typename T> void clearQueue(std::queue<T>& q) {
    std::queue<T> empty;
    std::swap(q, empty);
}

u8 getEnemyIndex(u8 id) {
    if (id > multiplayerLink->universal->currentPlayerId())
        id -= 1;
    return id;
}

RemotePlayerId::RemotePlayerId(u8 playerId) {
    id = playerId;
    index = getEnemyIndex(playerId);
}

void MultiplayerLink::activate() {
    active = true;
    universal->activate();
}

bool MultiplayerLink::sync() {
    // Do universal sync
    UpdateStateMachine();

    // Beat state to other players
    if (universal->isConnected()) {

        auto limiter = LINK_WIRELESS_QUEUE_SIZE * 2;
        auto exitEarly = false;
#ifdef GBA
        if (sendQueue.size() > limiter * 3) {
            if (stateMachine.IsInState<MultiplayerStates::PlayersSeeded>() ||
                stateMachine.IsInState<MultiplayerStates::Playing>() ||
                stateMachine.IsInState<MultiplayerStates::Lost>())
                exceptionReason = "Send queue exceeded safe limit.";
            else
                clearQueue(sendQueue);
        }
        if (seedQueue.size() > limiter * 3) {
            if (stateMachine.IsInState<MultiplayerStates::PlayersSeeded>() ||
                stateMachine.IsInState<MultiplayerStates::Playing>() ||
                stateMachine.IsInState<MultiplayerStates::Lost>())
                exceptionReason = "Seed queue exceeded safe limit.";
            else
                clearQueue(seedQueue);
        }
#endif
        while (closed() && !exitEarly && !sendQueue.empty() && --limiter) {
            auto front = sendQueue.front();
            bool sent = universal->send(front);
            if (sent)
                sendQueue.pop();
            else {
#ifdef GBA
                std::string error;
                switch (universal->getLinkWireless()->getLastError(true)) {
                case LinkWireless::Error::WRONG_STATE:
                    error = "Wrong state       ";
                    break;
                case LinkWireless::Error::GAME_NAME_TOO_LONG:
                    error = "Game name too long";
                    break;
                case LinkWireless::Error::USER_NAME_TOO_LONG:
                    error = "User name too long";
                    break;
                case LinkWireless::Error::BUFFER_IS_FULL:
                case LinkWireless::Error::BUSY_TRY_AGAIN:
                    // Hopefully it'll catch up, keep trying
                    exitEarly = true;
                    break;
                case LinkWireless::Error::COMMAND_FAILED:
                    error = "Command failed    ";
                    break;
                case LinkWireless::Error::CONNECTION_FAILED:
                    error = "Connection failed ";
                    break;
                case LinkWireless::Error::SEND_DATA_FAILED:
                    error = "Send data failed  ";
                    break;
                case LinkWireless::Error::RECEIVE_DATA_FAILED:
                    error = "Recv data failed  ";
                    break;
                case LinkWireless::Error::ACKNOWLEDGE_FAILED:
                    error = "ACK failed        ";
                    break;
                case LinkWireless::Error::NONE:
                    break;
                case LinkWireless::Error::TIMEOUT:
                case LinkWireless::Error::REMOTE_TIMEOUT:
                    exitEarly = true;
                    break;
                }
                if (!error.empty()) {
#ifdef PC
                    setUpPresentException(error);
#endif
                }
#endif
            }
        }
    }

    return !stateMachine
                .IsInState<MultiplayerStates::ConnectionInitializing>() &&
           universal->playerCount() == this->playingPlayerCount;
}

/**
 * For sending events such as attack and acknowledge attack that must never be
 * dropped.
 */
void MultiplayerLink::sendEvent(u16 packet) {
    bool sent = universal->send(packet);
    if (!sent)
        sendQueue.push(packet);
}

/**
 * Does not insert members into the queue on transport send fail to prevent
 * flooding the queue. Logic managing states should handle its own re-try
 * concerns.
 */
void MultiplayerLink::broadcastState(u16 packet) const {
    universal->send(packet);
}

void MultiplayerLink::deactivate(bool sendBye) {
    active = false;
    if (sendBye) {
        universal->sync();
        vsync();
        int attempts = 5;
        bool sent;
        do {
            sent = universal->send(UPDATE_STATE + BYE);
        } while (!sent && --attempts);
    }
    // pass traffic one last time and allow vsync to flush LinkUniversal queue
    // for upstream BYE
    universal->sync();
    vsync();
    universal->deactivate();
    stateMachine.ProcessStateTransitions();
    stateMachine.Shutdown();
    purgeQueues();
    resetSession();
}

void MultiplayerLink::purgeQueues() {
    clearQueue(sendQueue);
#ifdef GBA
    clearQueue(seedQueue);
#endif
}

void MultiplayerLink::UpdateStateMachine() {
    if (!stateMachine.IsInitialized()) {
        stateMachine.SetDebugTraceLevel(hsm::TraceLevel::Diagnostic);
        stateMachine.Initialize<MultiplayerStates::Deactivated>(this);
    }
    stateMachine.ProcessStateTransitions();
    stateMachine.UpdateStates();
}

void updateRank() {
    if (game->won) {
        rank = 0;
        return;
    }
    rank = 0;
    for (auto i : multiplayerLink->players)
        if (i.second.over)
            rank++;
    rank -= 1;
}

void Command::execute() const {
    int targetId = (value >> 4) & 0xFF;
    int attackValue = value & 0xF;
    if (playerId.index >= 4)
        return;

    switch (command) {
    case SEND_MATCH_ATTACKS:
    case SEND_MATCH_TIME:
        multiplayerLink->recordRemoteMatchStat(playerId, command, value);
        break;
    case SEND_ATTACK:
        // Record this attack to judge relative multiplayer rank
        multiplayerLink->enemyPower[playerId.index] += attackValue;
        // Add the attack to the garbage queue if it's for us
        if (targetId == multiplayerLink->universal->currentPlayerId())
            game->addToGarbageQueue(targetId, value & 0xF);
        break;
    case ACK_ATTACK:
        // The player just had garbage placed on their board
        attackAnimationTimers[playerId.index] = animationMax * attackValue;
        break;
    case SEND_ROW:
    case SEND_ROW + 1:
    case SEND_ROW + 2:
        GameScene::UpdateEnemyBoard(command, value, playerId.id);
        break;
    }
}

void clearPlayers() { multiplayerLink->players.clear(); }

/**
 * Only used in non-GBA multiplayer scenarios because the GBA hardware manages
 * players for us
 */
void removePlayer(u8 pId) {
#ifndef GBA
    multiplayerLink->removePlayer(pId);
#endif
}

void initPlayer(u8 pId) { multiplayerLink->initPlayer(pId); }
