#include "def.h"
#include "logging.h"

int currentMenu = 0;
int currentlyPlayingSong = -1;

void playSong(int menuId, int songId) {
    int song = 0;

    std::list<int>* songList;

    if (menuId == 0) {
        songList = &songs.menu;
    } else if (menuId == 1) {
        songList = &songs.game;
    } else {
        return;
    }

    if (songId < 0 || songId >= (int)songList->size())
        return;

    auto it = songList->begin();
    std::advance(it, songId);

    song = *it;

    currentMenu = menuId;
    currentlyPlayingSong = songId;

    setMusicVolume(512 * ((float)savefile->settings.volume / 10));
    startSong(song, (savefile->settings.cycleSongs == 0));
}

static int getSongCount(int menuId) {
    if (menuId == 0)
        return (int)songs.menu.size();
    else if (menuId == 1)
        return (int)songs.game.size();
    return 0;
}

int getNextSong(int menuId, int fromSong) {
    int max = getSongCount(menuId);

    if (max <= 0)
        return -1;

    if (savefile->settings.cycleSongs == 2) {
        // Shuffle
        int index = 0;
        int attempts = 0;
        do {
            index = (int)(randNext() % max);
        } while ((getSongState(menuId, index) || index == fromSong) &&
                 attempts++ < 1000);

        if (attempts >= 1000) {
            if (!getSongState(menuId, fromSong))
                return fromSong;
            return -1;
        }
        return index;
    } else {
        // Sequential
        int index = std::max(fromSong, 0);
        int attempts = 0;
        do {
            if (++index >= max)
                index = 0;
        } while (getSongState(menuId, index) && attempts++ < 1000);

        if (attempts >= 1000)
            return -1;
        return index;
    }
}

void playSongRandom(int menuId) {
    int max = getSongCount(menuId);
    if (max <= 0)
        return;

    int index = 0;
    int count = 0;

    do {
        index = (int)(randNext() % max);
    } while (
        (getSongState(menuId, (int)index) || index == currentlyPlayingSong) &&
        count++ < 1000);

    if (count >= 1000) {
        if (!getSongState(menuId, (int)currentlyPlayingSong)) {
            playSong(menuId, (int)currentlyPlayingSong);
        }
        return;
    }

    playSong(menuId, (int)index);
}

void playNextSong() {
    int menuId = currentMenu;
    int max = getSongCount(menuId);
    if (max <= 0)
        return;

    int index = std::max(currentlyPlayingSong, 0);
    int count = 0;

    do {
        if (++index >= max)
            index = 0;
    } while (getSongState(menuId, index) && count++ < 1000);

    if (count >= 1000)
        return;

    playSong(menuId, (int)index);
}

std::string getDisplayTrackName(int songIndex) {
    if (songIndex < 0)
        return {};

    std::string name = getSongName(songIndex);

    // Strip MENU_ or GAME_ prefix
    if (name.rfind("MENU_", 0) == 0)
        name = name.substr(5);
    else if (name.rfind("GAME_", 0) == 0)
        name = name.substr(5);

    // Strip alphabetical ordering prefix (e.g. "a_" in "a_thirno")
    if (name.size() > 2 && name[1] == '_')
        name = name.substr(2);

    // Strip file extension
    auto dot = name.rfind('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);

    // Truncate to 15 characters with ellipsis
    if ((int)name.size() > 15)
        name = name.substr(0, 15) + "...";

    return name;
}

void toggleSong(int group, int index, bool state) {
    int* list = nullptr;

    if (group == 0) { // MENU
        list = savefile->settings.songDisabling.menuBits;
    } else if (group == 1) { // IN-GAME
        list = savefile->settings.songDisabling.gameBits;
    }

    if (!list)
        return;

    if (state)
        list[index / 32] |= 1 << (index % 32);
    else
        list[index / 32] &= ~(1 << (index % 32));
}

bool getSongState(int group, int index) {
    int* list = nullptr;

    if (group == 0) { // MENU
        list = savefile->settings.songDisabling.menuBits;
    } else if (group == 1) { // IN-GAME
        list = savefile->settings.songDisabling.gameBits;
    }

    if (!list)
        return true;

    return (list[index / 32] >> (index % 32)) & 1;
}

void checkSongs() {
    SongDisabling* set = &savefile->settings.songDisabling;

    if ((u8)songs.menu.size() != set->menu ||
        (u8)songs.game.size() != set->game) {
        memset32_fast(set->gameBits, 0, 4);
        memset32_fast(set->menuBits, 0, 4);

        set->menu = (u8)songs.menu.size();
        set->game = (u8)songs.game.size();
    }
}

void stopMusic() {
    currentlyPlayingSong = -1;
    stopSong();
}
