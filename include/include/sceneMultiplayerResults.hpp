#pragma once

#include "multiplayerClasses.h"
#include "scene.hpp"

class MultiplayerResultsScene : public Scene {
public:
    WordSprite* wordSprites[MAX_WORD_SPRITES];

    int selection = 0;
    const int optionCount = 2; // Play Again, Main Menu
    const int optionsHeight = 13;

    int cursorFloat = 0;
    OBJ_ATTR* cursorSprites[2];

    int listStart = 0;
    const int maxVisible = 4;

    // Pre-computed player standings
    std::vector<std::string> displayLines;

    void buildDisplayLines();
    void renderText();

    void draw() override;
    void update() override;
    bool control() override;
    void init() override;
    void deinit() override;

    std::function<Scene*()> previousScene() override {
        return [] { return new MainMenuScene(); };
    }

    ~MultiplayerResultsScene() { deinit(); }
};
