#include "skinFile.hpp"

#include "def.h"
#include "sprites.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <filesystem>
#include <vector>

#ifndef GBA

INLINE uint8_t bgr555_r(uint16_t c) {
    return (u8)(((c & 0x1F) * 255 + 15) / 31);
}
INLINE uint8_t bgr555_g(uint16_t c) {
    return (u8)((((c >> 5) & 0x1F) * 255 + 15) / 31);
}
INLINE uint8_t bgr555_b(uint16_t c) {
    return (u8)((((c >> 10) & 0x1F) * 255 + 15) / 31);
}

uint16_t skinColor(int idx) {
    switch (idx) {
    case 1:
    case 2:
    case 3:
        return palette[1][BlockEngine::PIECE_I * 16 + idx];
    case 4:
        return 0x7FFF;
    case 5:
        return 0x0421;
    default:
        return 0x0000;
    }
}

uint8_t tilePixel(const TILE& tile, int row, int col) {
    return (u8)((tile.data[row] >> (col * 4)) & 0x0F);
}

void setTilePixel(TILE& tile, int row, int col, uint8_t index) {
    auto shift = (u32)(col * 4);
    tile.data[row] &= ~(0x0Fu << shift);
    tile.data[row] |= ((u32)(index & 0x0F) << shift);
}

static std::string skinFilePath(int index) {
#ifdef ANDROID
    return (std::filesystem::path(getSavefilePath()).parent_path() / "skins" /
            ("custom" + std::to_string(index) + ".png"))
        .string();
#else
    return "skins/custom" + std::to_string(index) + ".png";
#endif
}

void indexToRGBA(uint8_t idx, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if (idx == 0) {
        r = g = b = a = 0;
        return;
    }
    uint16_t c = skinColor(idx);
    r = bgr555_r(c);
    g = bgr555_g(c);
    b = bgr555_b(c);
    a = 255;
}

uint8_t rgbaToIndex(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a < 128)
        return 0;
    int best = 1;
    int bestDist = 0x7FFFFFFF;
    for (int i = 1; i < SKIN_PNG_COLORS; i++) {
        uint16_t c = skinColor(i);
        int dr = (int)(r) - (int)(bgr555_r(c));
        int dg = (int)(g) - (int)(bgr555_g(c));
        int db = (int)(b) - (int)(bgr555_b(c));
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return (u8)(best);
}

bool exportSkinFile(const std::string& path, const Skin& skin) {
    std::filesystem::path fsPath(path);
    std::error_code ec;
    std::filesystem::create_directories(fsPath.parent_path(), ec);
    if (ec) {
        log("Failed to create skin directory: " + ec.message());
        return false;
    }

    std::vector<uint8_t> rgba(SKIN_PNG_WIDTH * SKIN_PNG_HEIGHT * 4);

    auto pixel = [&](int x, int y) -> uint8_t* {
        return &rgba[(y * SKIN_PNG_WIDTH + x) * 4];
    };

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t idx = tilePixel(skin.board, y, x);
            indexToRGBA(idx, pixel(x, y)[0], pixel(x, y)[1], pixel(x, y)[2],
                        pixel(x, y)[3]);
        }
    }

    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
            uint8_t idx = tilePixel(skin.smallBoard, y, x);
            indexToRGBA(idx, pixel(8 + x, y)[0], pixel(8 + x, y)[1],
                        pixel(8 + x, y)[2], pixel(8 + x, y)[3]);
        }
    }

    for (int y = 6; y < 8; y++) {
        for (int x = 8; x < SKIN_PNG_WIDTH; x++) {
            indexToRGBA(0, pixel(x, y)[0], pixel(x, y)[1], pixel(x, y)[2],
                        pixel(x, y)[3]);
        }
    }

    std::string outputPath = fsPath.string();
    int result =
        stbi_write_png(outputPath.c_str(), SKIN_PNG_WIDTH, SKIN_PNG_HEIGHT, 4,
                       rgba.data(), SKIN_PNG_WIDTH * 4);
    if (!result) {
        log("Failed to write skin PNG file");
        return false;
    }

    return true;
}

bool importSkinFile(const std::string& path, Skin& skin) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data) {
        log("Failed to load skin PNG file");
        return false;
    }

    if (w != SKIN_PNG_WIDTH || h != SKIN_PNG_HEIGHT) {
        log("Skin PNG has wrong dimensions");
        stbi_image_free(data);
        return false;
    }

    auto pixel = [&](int x, int y) -> stbi_uc* {
        return &data[(y * SKIN_PNG_WIDTH + x) * 4];
    };

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            stbi_uc* p = pixel(x, y);
            uint8_t idx = rgbaToIndex(p[0], p[1], p[2], p[3]);
            setTilePixel(skin.board, y, x, idx);
        }
    }

    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
            stbi_uc* p = pixel(8 + x, y);
            uint8_t idx = rgbaToIndex(p[0], p[1], p[2], p[3]);
            setTilePixel(skin.smallBoard, y, x, idx);
        }
    }

    stbi_image_free(data);
    skin.changed = true;
    return true;
}

void importAllSkinFiles() {
    for (int i = 0; i < MAX_CUSTOM_SKINS; i++) {
        importSkinFile(skinFilePath(i + 1), savefile->customSkins[i]);
    }
}

void exportAllSkinFiles() {
    for (int i = 0; i < MAX_CUSTOM_SKINS; i++) {
        if (!savefile->customSkins[i].changed)
            continue;

        exportSkinFile(skinFilePath(i + 1), savefile->customSkins[i]);
    }
}

#else

bool exportSkinFile(const std::string& path, const Skin& skin) { return false; }

bool importSkinFile(const std::string& path, Skin& skin) { return false; }

void importAllSkinFiles() {}

void exportAllSkinFiles() {}

#endif
