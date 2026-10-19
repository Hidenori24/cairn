// テスト用の小道具
#pragma once

#include <fstream>
#include <sstream>
#include <string>

#include "roadmap/calendar.hpp"
#include "roadmap/encoding.hpp"

namespace roadmap::testutil {

inline std::string readBytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
inline std::string golden(const std::string& rel) { return std::string(ROADMAP_GOLDEN_DIR) + "/" + rel; }
inline std::string readText(const std::string& path) { return encoding::decodeText(readBytes(path)).text; }
inline Day D(const char* iso) { return *parseIso(iso); }

}  // namespace roadmap::testutil
