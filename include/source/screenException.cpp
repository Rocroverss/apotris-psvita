#include "multiplayerClasses.h"
#include "scene.hpp"
#include "sceneModes.hpp"
#include "sprites.h"

void MultiplayerExceptionScene::init() {
    reset();
    resetSmallText();
    clearText();

    clearSprites(128);
    clearEnemyBoard();

    // backgroundGrid
    setTiles(26, 0, 32 * 32,
             tileBuild(35 * (!savefile->settings.lightMode), false, false, 0));

    setTiles(27, 0, 32 * 32, tileBuild(34, false, false, 0));

    for (int i = 0; i < MAX_WORD_SPRITES; i++)
        wordSprites[i] = new WordSprite(i, 64 + i * 5, 256 + i * 20, true);

    enableBlend((0b101111 << 8) + (1 << 6) + (1 << 3));

    path.clear();
    path.emplace_back("Play");
    path.emplace_back(name);

    std::string p;
    int i = 0;
    auto it = path.begin();
    while (it != path.end()) {
        if (i != 0)
            p += " > ";

        p += *it;

        it++;
        i++;
    }

    int width = getVariableWidth(p) >> 3;
    int count = (width) / 12 + 1;

    int wordIndex = MAX_WORD_SPRITES - count - 1;

    wordSprites[wordIndex]->setText(p);

    for (int i = 0; i < count; i++)
        wordSprites[wordIndex + i]->show(i * 12 * 8 + 4, 4, 14);

    naprint(what, 15 * 8 - getVariableWidth(what) / 2, 9 * 8);
}

void MultiplayerExceptionScene::draw() {
    fallingBlocks();
    toggleBG(3, true);
    showSprites(128);
    sprite_hide(&obj_buffer[25]);
    sprite_hide(&obj_buffer[26]);
    sprite_hide(&obj_buffer[27]);
    sprite_hide(&obj_buffer[28]);
    sprite_hide(&obj_buffer[29]);
}

bool MultiplayerExceptionScene::control() {

    MenuKeys k = savefile->settings.menuKeys;

    if (key_hit(k.cancel)) {
        sfx(SFX_MENUCANCEL);
        previousElement = name;

        if (!path.empty())
            path.pop_back();

        changeScene(previousScene(), Transitions::FADE);
    }

    return false;
}

void MultiplayerExceptionScene::update() {
    canDraw = true;
    key_poll();

    control();
}

void MultiplayerExceptionScene::deinit() {
    clearSprites(128);
    showSprites(128);

    clearText();

    for (auto& wordSprite : wordSprites)
        delete wordSprite;
}

#ifndef MULTIBOOT
std::function<Scene*()> MultiplayerExceptionScene::previousScene() { return []{ return new MainMenuScene(); }; }
#else
std::function<Scene*()> MultiplayerExceptionScene::previousScene() { return []{ return new MultBattleScene(); }; }
#endif
