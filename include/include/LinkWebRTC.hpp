#pragma once
#include "def.h"
#include <algorithm>
#include <array>
#include <optional>
#include <queue>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#ifdef ANDROID
#include <SDL.h>
#endif

#if !defined(SWITCH) && !defined(VITA)
#include "rtc/rtc.hpp"
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>
#endif

#ifdef N3DS
#include "n3ds/networking.hpp"

#include <3ds.h>
#endif

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

constexpr char LINK_UNIVERSAL_MAX_PLAYERS = 5;
constexpr char LINK_WIRELESS_QUEUE_SIZE = 30;
#ifndef GBA
extern void removePlayer(u8 pId);
extern void clearPlayers();
extern void initPlayer(u8 pId);
#endif

#if !defined(SWITCH) && !defined(VITA)
using nlohmann::json;
using std::shared_ptr;
using std::weak_ptr;
template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }
#endif

std::string stringifyMultiplayerPacket(u16 packet);

struct BoardUpdatePacket {
    u8 playerId;
    u16 seq;
    u8 board[20][10]; // 4-bit color values: 0=empty, 1-7=color, 8=garbage
};

class LinkUniversal {
#if !defined(SWITCH) && !defined(VITA)
    static constexpr char STUN_SERVER[] = "stun:stun.l.google.com:19302";
    static constexpr char TYPE[] = "type";
    static constexpr char ID[] = "id";
    static constexpr char NAME[] = "name";
    static constexpr char DESCRIPTION[] = "description";
    static constexpr char OFFER[] = "offer";
    static constexpr char ANSWER[] = "answer";
    static constexpr char MATCH[] = "match";
    static constexpr char CLOSE[] = "close";
    static constexpr char SEED[] = "seed";
    static constexpr char FULL[] = "full";
    static constexpr char CANDIDATE[] = "candidate";
    static constexpr char PLAYER_INFO[] = "playerInfo";
    static constexpr char MID[] = "mid";
    static constexpr char ITER[] = "iter";
    static constexpr char FROM[] = "from";

    rtc::Configuration config;
    std::map<std::string, std::queue<Packet>> readQueues;
    std::queue<BoardUpdatePacket> incomingBoardUpdates;
    bool active = false;
    shared_ptr<rtc::WebSocket> ws;
    int playerId = -1;
    std::unordered_map<std::string, u8> playerStringToId;
    std::unordered_map<u8, std::string> playerIdToString;
    std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>>
        peerConnectionMap;
    std::unordered_map<std::string, shared_ptr<rtc::DataChannel>>
        dataChannelMap;
    std::unordered_map<u8, std::string> playerIdToName;
    std::unordered_map<std::string, std::string> playerStringToName;

    std::mutex readQueueLock;
    std::shared_mutex multiplayerLock;

#ifdef N3DS
    std::mutex deferredSendQueueLock;
    std::queue<rtc::message_variant> deferredSendQueue;
#endif

public:
    std::string localId;
    std::string localPlayerName;
    [[nodiscard]] u8 currentPlayerId() const { return playerId; }
    static std::string randomId(size_t length) {
        static const std::string characters("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        std::string id(length, '0');
#ifdef PC
        std::random_device randomDevice;
        std::uniform_int_distribution<int> uniform(0,
                                                   int(characters.size() - 1));
        std::generate(id.begin(), id.end(),
                      [&]() { return characters.at(uniform(randomDevice)); });
#else
        std::generate(id.begin(), id.end(), [&]() {
            return characters.at((int)randNext() % (characters.length() - 1));
        });
#endif
        return id;
    }
    shared_ptr<rtc::PeerConnection>
    getOrCreatePeerConnection(const std::string& peerId,
                              const std::string& type) {
        {
            std::shared_lock<std::shared_mutex> guard{multiplayerLock};
            if (auto jt = peerConnectionMap.find(peerId);
                jt != peerConnectionMap.end())
                return jt->second;
        }

        if (type == OFFER || type == MATCH)
            return createPeerConnection(make_weak_ptr(ws), peerId);

        return nullptr;
    }

    void initWebSocketConfig() {
        localId = randomId(10);
        localPlayerName = savefile->latestName;

#ifndef WEB
        rtc::InitLogger(rtc::LogLevel::Debug);
#endif
        config.iceServers.emplace_back(STUN_SERVER);
#ifdef N3DS
        ws = std::make_shared<rtc::WebSocket>(rtc::WebSocketConfiguration{
            .caCertificatePemFile = "sdmc:/config/ssl/cacert.pem",
        });
#elif defined(ANDROID)
        std::string certPath;
        const char* internalPath = SDL_AndroidGetInternalStoragePath();
        if (internalPath) {
            std::string destPath = std::string(internalPath) + "/cacert.pem";
            SDL_RWops* src = SDL_RWFromFile("assets/ssl/cacert.pem", "rb");
            if (src) {
                SDL_RWops* dst = SDL_RWFromFile(destPath.c_str(), "wb");
                if (dst) {
                    char buf[4096];
                    int bytesRead;
                    while ((bytesRead = SDL_RWread(src, buf, 1, sizeof(buf))) >
                           0) {
                        SDL_RWwrite(dst, buf, 1, bytesRead);
                    }
                    SDL_RWclose(dst);
                    certPath = destPath; // Only set if successful
                }
                SDL_RWclose(src);
            }
        }
        ws = std::make_shared<rtc::WebSocket>(rtc::WebSocketConfiguration{
            .caCertificatePemFile =
                certPath.empty() ? std::nullopt : std::make_optional(certPath),
        });
#else
        ws = std::make_shared<rtc::WebSocket>();
#endif

        ws->onClosed([]() { std::cout << "WebSocket closed" << std::endl; });
        ws->onMessage([this, wws = make_weak_ptr(ws)](auto data) {
            if (!std::holds_alternative<std::string>(data))
                return;
            std::cout << "Got websocket message: "
                      << std::get<std::string>(data) << std::endl;
            json message = json::parse(std::get<std::string>(data));
            if (message.find(ID) == message.end() ||
                message.find(TYPE) == message.end())
                return;

            std::string type = message[TYPE];
            std::string id =
                message[ID]; // The value from the "id" field of the JSON
            std::string peerId = id; // This will be the peer ID for RTC
                                     // operations. Default to
                                     // id_field_in_message.

            // For OFFER, ANSWER, CANDIDATE, the message[ID] is localId
            // (recipient). The actual remote peer's ID is in the "from" field
            // added by the server.
            if (type == OFFER || type == ANSWER || type == CANDIDATE) {
                if (!message.contains(FROM)) {
                    std::cerr << "WebSocket " << type
                              << " message missing 'from' field. Discarding."
                              << std::endl;
                    return;
                }
                peerId = message[FROM].get<std::string>();
                if (peerId ==
                    localId) { // Should be caught by server or is an error
                    std::cerr << "WebSocket " << type
                              << " message 'from' self (" << peerId
                              << "). Discarding." << std::endl;
                    return;
                }
            }

            if (type == PLAYER_INFO) {
                const std::string playerString = message[FROM];
                const std::string playerName = message[NAME];
                playerStringToName[playerString] = playerName;

                if (auto player = playerStringToId.find(playerString);
                    player != playerStringToId.end()) {
                    playerIdToName[player->second] = playerName;
                }
            }
            if (type == SEED) {
                nextSeed = stoi(id) & ((0x1000) - 1);
                lastSeed = nextSeed;
                return;
            }
            if (type == MATCH) {
                int iter = message[ITER];
                int remotePlayerId = iter;
                if (peerId == localId) {
                    if (playerId != -1 && remotePlayerId == 0) {
                        playerIdToString.clear();
                        playerStringToId.clear();
                        playerIdToName.clear();
                    }
                    playerId = remotePlayerId;

                    playerIdToName[playerId] = localPlayerName;
                    std::cout << "We think we are player ID " << playerId
                              << std::endl;
                }
                playerIdToString[remotePlayerId] = peerId;
                playerStringToId[peerId] = remotePlayerId;
                if (auto name = playerStringToName.find(peerId);
                    name != playerStringToName.end()) {
                    playerIdToName[remotePlayerId] = name->second;
                }
                clearPlayers();
                for (auto& i : playerIdToString) {
                    initPlayer(i.first);
                }
                if (peerId == localId)
                    return;
            }

            auto pc = getOrCreatePeerConnection(peerId, type);

            if (type == CLOSE) {
                // Close the DataChannel without holding multiplayerLock to
                // avoid deadlock
                auto dc = [&]() -> std::shared_ptr<rtc::DataChannel> {
                    std::unique_lock<std::shared_mutex> guard{multiplayerLock};
                    peerConnectionMap.erase(peerId);
                    if (auto it = dataChannelMap.find(peerId);
                        it != dataChannelMap.end()) {
                        auto dc = std::move(it->second);
                        dataChannelMap.erase(it);
                        return dc;
                    }
                    return nullptr;
                }();
                if (dc)
                    dc->close();
#ifndef WEB
                if (pc)
                    pc->close();
#endif
                if (auto player = playerStringToId.find(peerId);
                    player != playerStringToId.end()) {
                    u8 pId_to_remove = player->second;
                    removePlayer(pId_to_remove);
                    playerIdToString.erase(pId_to_remove);
                    playerStringToId.erase(player);
                    playerIdToName.erase(pId_to_remove);
                }
                playerStringToName.erase(peerId);

            } else if (type == MATCH && peerId != localId) {
                std::shared_lock<std::shared_mutex> guard{multiplayerLock};
                if (!pc || dataChannelMap.count(peerId))
                    return;
                guard.unlock();
                makeOffer(pc, peerId);
            } else if (type == OFFER || type == ANSWER) {
                if (!pc)
                    return;
                pc->setRemoteDescription(
                    rtc::Description(message[DESCRIPTION], type));
            } else if (type == CANDIDATE) {
                if (!pc)
                    return;
                pc->addRemoteCandidate(
                    rtc::Candidate(message[CANDIDATE], message[MID]));
            } else if (type == FULL)
                exceptionReason = "Room is full";
        });
    }
    void activate() {
        if (active)
            return;
        active = true;
#ifdef N3DS
        NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
        aptSetHomeAllowed(false);
        aptSetSleepAllowed(false);
        n3ds::networking::start();
#endif
        initWebSocketConfig();

#if !defined(WEB) && !defined(ANDROID)
        std::promise<void> wsPromise;
        auto wsFuture = wsPromise.get_future();

        ws->onOpen([this, wws = make_weak_ptr(ws), &wsPromise]() {
            std::cout << "WebSocket connected, signaling ready" << std::endl;
            wsPromise.set_value();
            if (auto shared_ws = wws.lock()) {
                json playerInfo = {{ID, "broadcast"},
                                   {TYPE, PLAYER_INFO},
                                   {NAME, localPlayerName}};
                shared_ws->send(playerInfo.dump());
                std::cout << "Sent player info to signaling server: "
                          << playerInfo.dump() << std::endl;
            }
        });

        ws->onError([&wsPromise](const std::string& s) {
            std::cout << "WebSocket error" << std::endl;
            wsPromise.set_exception(
                std::make_exception_ptr(std::runtime_error(s)));
        });
#else
        ws->onOpen([this, wws = make_weak_ptr(ws)]() {
            std::cout << "WebSocket connected, signaling ready" << std::endl;
            if (auto shared_ws = wws.lock()) {
                json playerInfo = {{ID, "broadcast"},
                                   {TYPE, PLAYER_INFO},
                                   {NAME, localPlayerName}};
                shared_ws->send(playerInfo.dump());
                std::cout << "Sent player info to signaling server: "
                          << playerInfo.dump() << std::endl;
            }
        });

        ws->onError([](const std::string& s) {
            std::cout << "WebSocket error" << std::endl;
        });
#endif

        std::string url = "wss://api.apotris.com/ws/" + localId +
                          (!roomId.empty() ? "/" + roomId : "/match");
        url.erase(remove(url.begin(), url.end(), ' '), url.end());

        std::cout << "WebSocket URL is " << url << std::endl;
        ws->open(url);

#if !defined(WEB) && !defined(ANDROID)
        std::cout << "Waiting for signaling to be connected..." << std::endl;
        wsFuture.get();
#endif
    }

    void makeOffer(std::shared_ptr<rtc::PeerConnection> pc,
                   const std::string& remoteId) {
        // We are the offerer if we have a lower ID
        if (localId < remoteId) {
            return;
        }
        std::stringstream ss;
        ss << "apotris:" << localId << std::to_string(playerId) << "<->"
           << std::to_string(playerStringToId[remoteId]);
        std::cout << "Creating DataChannel with label \"" << ss.str() << "\""
                  << std::endl;
        auto dc = pc->createDataChannel(ss.str());
        registerDataChannelHandlers(dc, remoteId);
    }

    void parseMessage(rtc::message_variant data, const std::string& peerId) {
        // data holds either std::string or rtc::binary
        if (std::holds_alternative<std::string>(data)) {
            return;
        } else {
            auto packet = std::get<rtc::binary>(data);
            if (packet.size() == 2) {
#ifdef TRACY_ENABLE
                ZoneScopedN("recv_packet");
                ZoneTextF("from=%s", peerId.c_str());
#endif
                std::byte foo = packet[0];
                std::byte bar = packet[1];
                u16 real = (u8)foo + ((u16)bar << 8);
#ifdef TRACY_ENABLE
                ZoneTextF("0x%04x", real);
                ZoneTextF("%s", stringifyMultiplayerPacket(real).c_str());
#endif
                int realRemoteId = playerStringToId[peerId];
                std::lock_guard<std::mutex> guard{readQueueLock};
                readQueues[peerId].emplace((u8)(realRemoteId), real);
            } else if (packet.size() == 104 &&
                       ((u16)packet[0] | ((u16)packet[1] << 8)) == 0x1c00) {
                // 0x1c00 == ENCODE(SEND_ROW) + (0x1f << 10)
#ifdef TRACY_ENABLE
                ZoneScopedN("recv_packet(board)");
                ZoneTextF("from=%s", peerId.c_str());
#endif
                int realRemoteId = playerStringToId[peerId];
                const u16 seq = (u16)packet[2] | ((u16)packet[3] << 8);

#ifdef TRACY_ENABLE
                ZoneTextF("seq=%u", seq);
#endif
                std::lock_guard<std::mutex> guard{readQueueLock};
                auto& boardUpdate =
                    incomingBoardUpdates.emplace(BoardUpdatePacket{
                        .playerId = (u8)realRemoteId,
                        .seq = seq,
                    });
                for (size_t r = 0; r < 20; r++) {
                    for (size_t col = 0; col < 5; col++) {
                        u8 packed = (u8)packet[4 + r * 5 + col];
                        boardUpdate.board[r][col * 2] = packed & 0xF;
                        boardUpdate.board[r][col * 2 + 1] = (packed >> 4) & 0xF;
                    }
                }
            } else {
                fprintf(stderr, "Received unknown message of len=%d",
                        packet.size());
                for (size_t i = 0; i < packet.size(); i++) {
                    fprintf(stderr, "%02X ",
                            *((unsigned char*)packet.data() + i));
                    if (i % 16 == 15 && i + 1 != packet.size()) {
                        fputs("\n", stderr);
                    }
                }
                fputs("\n", stderr);
            }
        }
    }

    void
    registerDataChannelHandlers(const shared_ptr<rtc::DataChannel>& dataChannel,
                                const std::string& peerId) {
        dataChannel->onOpen([this, peerId]() {
            std::cout << "DataChannel from " << peerId << " opened"
                      << std::endl;
            initPlayer(playerStringToId[peerId]);
        });

        dataChannel->onClosed([this, peerId]() {
            std::cout << "DataChannel from " << peerId << " closed"
                      << std::endl;
        });

        dataChannel->onMessage(
            [this, peerId](auto data) { parseMessage(data, peerId); });

        dataChannel->onError([peerId](auto error) {
            std::cout << "Error on DataChannel to peer " << peerId
                      << ", was error:\n"
                      << error << std::endl;
        });

        std::unique_lock<std::shared_mutex> guard{multiplayerLock};
        dataChannelMap.emplace(peerId, dataChannel);
    }

    shared_ptr<rtc::PeerConnection>
    createPeerConnection(const weak_ptr<rtc::WebSocket>& wws,
                         std::string peerId) {
        auto pc = std::make_shared<rtc::PeerConnection>(config);

#ifndef WEB
        pc->onStateChange([](rtc::PeerConnection::State state) {
            std::cout << "State: " << state << std::endl;
        });

        pc->onGatheringStateChange(
            [](rtc::PeerConnection::GatheringState state) {
                std::cout << "Gathering State: " << state << std::endl;
            });
#endif

        pc->onLocalDescription(
            [wws, peerId](const rtc::Description& description) {
                json message = {{ID, peerId},
                                {TYPE, description.typeString()},
                                {DESCRIPTION, std::string(description)}};
                std::cout << "Websocket local description "
                          << to_string(message) << std::endl;
                if (auto wwss = wws.lock())
                    wwss->send(message.dump());
            });

        pc->onLocalCandidate([wws, peerId](const rtc::Candidate& candidate) {
            json message = {{ID, peerId},
                            {TYPE, "candidate"},
                            {CANDIDATE, std::string(candidate)},
                            {MID, candidate.mid()}};
            std::cout << "Websocket local candidate " << to_string(message)
                      << std::endl;
            if (auto wwss = wws.lock())
                wwss->send(message.dump());
        });

        pc->onDataChannel(
            [this, peerId](const shared_ptr<rtc::DataChannel>& dataChannel) {
                std::cout << "DataChannel from " << peerId
                          << " received with label \"" << dataChannel->label()
                          << "\"" << std::endl;
                registerDataChannelHandlers(dataChannel, peerId);
            });

        std::unique_lock<std::shared_mutex> guard{multiplayerLock};
        peerConnectionMap.emplace(peerId, pc);
        return pc;
    };
    [[nodiscard]] bool isActive() const { return active; }

    void deactivate() {
        if (!active)
            return;
        active = false;
        send(2 << 13); // BYE
#ifdef N3DS
        n3ds::networking::process_wait_completion();
        aptSetHomeAllowed(true);
        aptSetSleepAllowed(true);
        NDMU_LeaveExclusiveState();
#endif

        auto [pcs, dcs] = [&]() {
            std::unique_lock<std::shared_mutex> guard{multiplayerLock};
            return std::pair{std::move(peerConnectionMap),
                             std::move(dataChannelMap)};
        }();
        dcs.clear();
#ifndef WEB
        for (auto& it : pcs)
            it.second->close();
#endif
        pcs.clear();
        readQueues.clear();
        playerStringToId.clear();
        playerIdToString.clear();
        playerIdToName.clear();
        playerStringToName.clear();
        ws->close();
        localId = randomId(10);
        playerId = -1;
    }

    [[nodiscard]] bool sync() const { return isConnected(); }

    bool canRead(u8 pId) {
        if (pId == playerId)
            return false;
        std::string peerId = playerIdToString[pId];
        std::lock_guard<std::mutex> guard{readQueueLock};
        return !readQueues[peerId].empty();
    }

    u16 read(u8 pId) {
        std::string realRemoteId = playerIdToString[pId];
        std::lock_guard<std::mutex> guard{readQueueLock};
        auto front = readQueues[realRemoteId].front();
        readQueues[realRemoteId].pop();
        return front.packet;
    }

    std::optional<BoardUpdatePacket> readBoardUpdate() {
        std::lock_guard<std::mutex> guard{readQueueLock};
        if (incomingBoardUpdates.empty()) {
            return std::nullopt;
        }
        auto front = incomingBoardUpdates.front();
        incomingBoardUpdates.pop();
        return front;
    }

    bool send(u16 packet) {
#ifdef TRACY_ENABLE
        ZoneScopedN("link->send(u16)");
        ZoneTextF("0x%04x", packet);
        ZoneTextF("%s", stringifyMultiplayerPacket(packet).c_str());
#endif
        return send(reinterpret_cast<const std::byte*>(&packet), sizeof(u16));
    }
    template <size_t N> bool send(const std::array<std::byte, N>& packet) {
#ifdef TRACY_ENABLE
        ZoneScopedN("link->send(array)");
        ZoneTextF("len=%zu", packet.size());
#endif
        return send(packet.data(), packet.size());
    }
    bool send(const std::byte* data, size_t len) {
#ifdef TRACY_ENABLE
        ZoneScopedN("link->send(data, len)");
        ZoneTextF("len=%zu", len);
#endif
#ifdef N3DS
        {
            std::lock_guard<std::mutex> guard{deferredSendQueueLock};
            deferredSendQueue.emplace(rtc::binary{data, data + len});
        }
        n3ds::networking::process();
        return true;
#else /* N3DS */
        try {
            for (auto& it : dataChannelMap) {
#ifdef TRACY_ENABLE
                ZoneScopedN("send_to_datachannel");
                ZoneTextF("peer=%s", it.first.c_str());
#endif
                if (!it.second->isOpen()) {
                    std::cout << "DataChannel to " << it.first
                              << " appears closed!" << std::endl;
                    if (it.second->isClosed()) {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel closed");
                        ZoneColor(tracy::Color::Red);
#endif
                    } else {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel not open");
                        ZoneColor(tracy::Color::Orange);
#endif
                    }
                    return false;
                }
                if (!it.second->send(data, len)) {
#ifdef TRACY_ENABLE
                    ZoneTextF("send returned false");
                    ZoneColor(tracy::Color::Orange);
#endif
                    // The return value of `send` seems unusable. `false` may
                    // mean the message wasn't sent due to the DataChannel being
                    // closed, but it could also mean the message has been
                    // buffered... Anyway, we should not return false here.
                }
            }
            return true;
        } catch (std::exception& e) {
#ifdef TRACY_ENABLE
            ZoneTextF("send err: %s", e.what());
            ZoneColor(tracy::Color::Red);
#endif
            printf("%s", e.what());
            return true;
        }
#endif /* N3DS */
    }
    bool send(std::string msg) {
#ifdef TRACY_ENABLE
        ZoneScopedN("link->send(string)");
        ZoneTextF("%s", msg.c_str());
#endif
#ifdef N3DS
        {
            std::lock_guard<std::mutex> guard{deferredSendQueueLock};
            deferredSendQueue.emplace(std::move(msg));
        }
        n3ds::networking::process();
        return true;
#else /* N3DS */
        try {
            for (auto& it : dataChannelMap) {
#ifdef TRACY_ENABLE
                ZoneScopedN("send_to_datachannel");
                ZoneTextF("peer=%s", it.first.c_str());
#endif
                if (!it.second->isOpen()) {
                    std::cout << "DataChannel to " << it.first
                              << " appears closed!" << std::endl;
                    if (it.second->isClosed()) {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel closed");
                        ZoneColor(tracy::Color::Red);
#endif
                    } else {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel not open");
                        ZoneColor(tracy::Color::Orange);
#endif
                    }
                    return false;
                }
                if (!it.second->send(msg)) {
#ifdef TRACY_ENABLE
                    ZoneTextF("send returned false");
                    ZoneColor(tracy::Color::Orange);
#endif
                    // The return value of `send` seems unusable. `false` may
                    // mean the message wasn't sent due to the DataChannel being
                    // closed, but it could also mean the message has been
                    // buffered... Anyway, we should not return false here.
                }
            }
            return true;
        } catch (std::exception& e) {
#ifdef TRACY_ENABLE
            ZoneTextF("send err: %s", e.what());
            ZoneColor(tracy::Color::Red);
#endif
            printf("%s", e.what());
            return true;
        }
#endif /* N3DS */
    }

#ifdef N3DS
    // This is intended to be called from the 3DS networking thread.
    void sendDeferredMessages() {
        {
            std::lock_guard<std::mutex> guard{deferredSendQueueLock};
            if (deferredSendQueue.empty()) {
                return;
            }
        }

#ifdef TRACY_ENABLE
        ZoneScoped;
        ZoneTextF("count=%d", deferredSendQueue.size());
#endif

        auto dataChannels = [&]() {
            std::shared_lock<std::shared_mutex> guard{multiplayerLock};
            return std::vector(dataChannelMap.begin(), dataChannelMap.end());
        }();

        auto tryPop = [&]() -> std::optional<rtc::message_variant> {
            std::lock_guard<std::mutex> guard{deferredSendQueueLock};
            if (deferredSendQueue.empty()) {
                return std::nullopt;
            }
            rtc::message_variant msg = std::move(deferredSendQueue.front());
            deferredSendQueue.pop();
            return msg;
        };

        while (auto msg = tryPop()) {
            for (auto& [id, dc] : dataChannels) {
#ifdef TRACY_ENABLE
                ZoneScopedN("send_to_datachannel");
                ZoneTextF("peer=%s", id.c_str());
#endif
                if (!dc->isOpen()) {
                    std::cout << "DataChannel to " << id << " appears closed!"
                              << std::endl;
                    if (dc->isClosed()) {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel closed");
                        ZoneColor(tracy::Color::Red);
#endif
                    } else {
#ifdef TRACY_ENABLE
                        ZoneTextF("DataChannel not open");
                        ZoneColor(tracy::Color::Orange);
#endif
                    }
                    continue;
                }
                try {
                    dc->send(*msg);
                } catch (std::exception& e) {
#ifdef TRACY_ENABLE
                    ZoneTextF("send err: %s", e.what());
                    ZoneColor(tracy::Color::Red);
#endif
                    fprintf(stderr, "Failed to send to %s, err=%s", id.c_str(),
                            e.what());
                }
            }
        }
    }
#endif /* N3DS */

    [[nodiscard]] bool isConnected() const { return playerCount() > 1; }

    [[nodiscard]] u8 playerCount() const { return dataChannelMap.size() + 1; }

    [[nodiscard]] std::unordered_map<u8, std::string> playerNames() const {
        return playerIdToName;
    }
#else
public:
    std::string localId;
    std::string localPlayerName;
    void initWebSocketConfig() {}

    void activate() {}
    void deactivate() {}
    [[nodiscard]] bool isActive() const { return false; }
    [[nodiscard]] bool sync() const { return false; }
    bool canRead(u8 pId) { return false; }
    u16 read(u8 pId) { return 0; }
    bool send(u16 packet) { return false; }
    bool send(std::string msg) { return false; }
    template <size_t N> bool send(const std::array<std::byte, N>& msg) {
        return false;
    }
    std::optional<BoardUpdatePacket> readBoardUpdate() { return std::nullopt; }
    [[nodiscard]] bool isConnected() const { return false; }
    [[nodiscard]] u8 playerCount() const { return 0; }
    [[nodiscard]] std::unordered_map<u8, std::string> playerNames() const {
        return std::unordered_map<u8, std::string>();
    }
    [[nodiscard]] u8 currentPlayerId() const { return 0; }
#endif
};

extern LinkUniversal* linkUniversal;
