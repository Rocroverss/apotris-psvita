#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

// PC developer input-tape scenarios. A scenario starts from a saved copy of
// Save at the title screen, then reproduces raw input transitions by frame.
#ifdef PC
bool scenarioLoad(const std::string& path);
bool scenarioReplayLast();
void scenarioRequestRecording();
void scenarioToggleRecording();
bool scenarioConsumeRestartRequest();
void scenarioRestoreSave();
void scenarioRestart();
void scenarioApplyInput(std::unordered_set<uint32_t>& pressedKeys);
void scenarioRecordInput(uint32_t key, bool pressed, bool controller);
bool scenarioIsPlaying();
bool scenarioSuppressSave();
#else
inline bool scenarioLoad(const std::string&) { return false; }
inline bool scenarioReplayLast() { return false; }
inline void scenarioRequestRecording() {}
inline void scenarioToggleRecording() {}
inline bool scenarioConsumeRestartRequest() { return false; }
inline void scenarioRestoreSave() {}
inline void scenarioRestart() {}
inline void scenarioApplyInput(std::unordered_set<uint32_t>&) {}
inline void scenarioRecordInput(uint32_t, bool, bool) {}
inline bool scenarioIsPlaying() { return false; }
inline bool scenarioSuppressSave() { return false; }
#endif
