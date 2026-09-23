#pragma once

#include "platform.hpp"
#include "save.h"
#include <string>

#define SKIN_PNG_WIDTH 14
#define SKIN_PNG_HEIGHT 8
#define SKIN_PNG_COLORS 6

bool exportSkinFile(const std::string& path, const Skin& skin);
bool importSkinFile(const std::string& path, Skin& skin);
void importAllSkinFiles();
void exportAllSkinFiles();
