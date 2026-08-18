#pragma once

#include "model.hpp"

#include <string>

namespace fms {

std::string defaultSavePath();
bool saveState(const AppState& state, const std::string& path, std::string& error);
bool loadState(AppState& state, const std::string& path, std::string& error);

} // namespace fms
