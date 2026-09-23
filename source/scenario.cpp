#ifdef PC

#include "scenario.hpp"

#include "def.h"
#include "logging.h"
#include "prng.h"
#include "scene.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

struct InputEvent {
    uint32_t frame;
    uint32_t key;
    bool pressed;
    bool controller;
};

const std::string scenarioDirectory = "tests";
std::vector<InputEvent> events;
std::unordered_set<uint32_t> playbackPressed;
Save snapshot;
std::string scenarioPath;
size_t nextEvent = 0;
uint32_t frame = 0;
bool hasSnapshot = false;
bool recording = false;
bool playing = false;
bool restartRequested = false;
bool recordingRequested = false;

std::string snapshotPath(const std::string& path) { return path + ".save"; }

std::string nextScenarioPath() {
    std::error_code error;
    std::filesystem::create_directories(scenarioDirectory, error);
    if (error) {
        log("Could not create scenario directory " + scenarioDirectory);
        return "";
    }

    for (int number = 1;; number++) {
        const std::string scenarioFile =
            scenarioDirectory + "/scenario-" + std::to_string(number) + ".scn";
        if (!std::filesystem::exists(scenarioFile) &&
            !std::filesystem::exists(snapshotPath(scenarioFile))) {
            return scenarioFile;
        }
    }
}

bool writeScenario() {
    std::ofstream output(scenarioPath);
    if (!output) {
        log("Could not write scenario " + scenarioPath);
        return false;
    }

    output << "APOTRIS_SCENARIO 2\n";
    for (const InputEvent& event : events) {
        output << event.frame << ' ' << event.key << ' '
               << (event.pressed ? "down" : "up") << ' '
               << (event.controller ? "controller" : "keyboard") << '\n';
    }

    std::ofstream saveOutput(snapshotPath(scenarioPath),
                             std::ios::binary | std::ios::trunc);
    if (!saveOutput) {
        log("Could not write scenario save " + snapshotPath(scenarioPath));
        return false;
    }
    saveOutput.write((const char*)&snapshot, sizeof(snapshot));
    if (!saveOutput) {
        log("Could not write scenario save " + snapshotPath(scenarioPath));
        return false;
    }

    log("Saved scenario " + scenarioPath + " (" +
        std::to_string(events.size()) + " input events)");
    return true;
}

} // namespace

bool scenarioLoad(const std::string& scenarioFile) {
    std::ifstream input(scenarioFile);
    std::ifstream saveInput(snapshotPath(scenarioFile), std::ios::binary);
    if (!input || !saveInput) {
        log("Could not load scenario " + scenarioFile);
        return false;
    }

    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "APOTRIS_SCENARIO" ||
        version != 2) {
        log("Invalid scenario " + scenarioFile);
        return false;
    }

    Save loadedSnapshot;
    saveInput.read((char*)&loadedSnapshot, sizeof(loadedSnapshot));
    if (!saveInput) {
        log("Invalid scenario save " + snapshotPath(scenarioFile));
        return false;
    }

    std::vector<InputEvent> loadedEvents;
    uint32_t eventFrame = 0;
    uint32_t key = 0;
    std::string state;
    std::string inputType;
    while (input >> eventFrame >> key >> state >> inputType) {
        if ((state != "down" && state != "up") ||
            (inputType != "keyboard" && inputType != "controller")) {
            log("Invalid input event in scenario " + scenarioFile);
            return false;
        }
        loadedEvents.push_back(
            {eventFrame, key, state == "down", inputType == "controller"});
    }

    if (!input.eof()) {
        log("Invalid scenario " + scenarioFile);
        return false;
    }

    if (!std::is_sorted(loadedEvents.begin(), loadedEvents.end(),
                        [](const InputEvent& a, const InputEvent& b) {
                            return a.frame < b.frame;
                        })) {
        log("Scenario events are not ordered " + scenarioFile);
        return false;
    }

    scenarioPath = scenarioFile;
    snapshot = loadedSnapshot;
    events = std::move(loadedEvents);
    hasSnapshot = true;
    recording = false;
    recordingRequested = false;
    playing = true;
    restartRequested = true;
    frame = 0;
    nextEvent = 0;
    playbackPressed.clear();
    log("Loaded scenario " + scenarioFile);
    return true;
}

bool scenarioReplayLast() {
    if (scenarioPath.empty()) {
        log("No scenario loaded");
        return false;
    }
    return scenarioLoad(scenarioPath);
}

void scenarioRequestRecording() {
    if (savefile == nullptr)
        return;

    const std::string nextPath = nextScenarioPath();
    if (nextPath.empty())
        return;

    snapshot = *savefile;
    scenarioPath = nextPath;
    events.clear();
    hasSnapshot = true;
    recording = false;
    recordingRequested = true;
    playing = false;
    restartRequested = true;
    log("Scenario recording will start at title screen");
}

void scenarioToggleRecording() {
    if (recording) {
        recording = false;
        writeScenario();
        return;
    }

    if (recordingRequested) {
        recordingRequested = false;
        restartRequested = false;
        log("Cancelled scenario recording");
        return;
    }

    scenarioRequestRecording();
}

bool scenarioConsumeRestartRequest() {
    if (!restartRequested)
        return false;
    restartRequested = false;
    return true;
}

void scenarioRestoreSave() {
    if (hasSnapshot && savefile != nullptr)
        *savefile = snapshot;
}

void scenarioRestart() {
    scenarioRestoreSave();
    randSetSeed(savefile->seed);
    frame = 0;
    nextEvent = 0;
    playbackPressed.clear();

    if (recordingRequested) {
        recordingRequested = false;
        recording = true;
        log("Recording scenario " + scenarioPath);
    }

    changeScene([]() { return new TitleScene(); });
}

void scenarioApplyInput(std::unordered_set<uint32_t>& pressedKeys) {
    if (!playing && !recording)
        return;

    if (playing) {
        while (nextEvent < events.size() && events[nextEvent].frame == frame) {
            const InputEvent& event = events[nextEvent++];
            lastInputType =
                event.controller ? InputType::CONTROLLER : InputType::KEYBOARD;
            if (event.pressed)
                playbackPressed.insert(event.key);
            else
                playbackPressed.erase(event.key);
        }
        pressedKeys = playbackPressed;

        if (nextEvent == events.size() && playbackPressed.empty()) {
            playing = false;
            log("Scenario playback complete");
        }
    }

    frame++;
}

void scenarioRecordInput(uint32_t key, bool pressed, bool controller) {
    if (!recording)
        return;

    events.push_back({frame, key, pressed, controller});
}

bool scenarioIsPlaying() { return playing; }

bool scenarioSuppressSave() { return playing; }

#endif
