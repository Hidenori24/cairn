#include "roadmap/evm_settings.hpp"

#include <cmath>
#include <string>

namespace roadmap {

namespace {
using Json = nlohmann::ordered_json;
constexpr const char* kProgressKey = "progress";
constexpr const char* kSpiWarnKey = "spiWarn";
}  // namespace

Json evmSettingsToJson(const EvmSettings& s) {
  Json j = Json::object();
  if (s.progress != EvmProgress::ZeroHundred) j[kProgressKey] = "half";
  if (s.spiWarn != kEvmSpiWarnDefault) j[kSpiWarnKey] = s.spiWarn;
  if (s.extra.is_object())
    for (auto it = s.extra.begin(); it != s.extra.end(); ++it)
      if (it.key() != kProgressKey && it.key() != kSpiWarnKey) j[it.key()] = it.value();
  return j;
}

EvmSettings evmSettingsFromJson(const Json& j) {
  EvmSettings s;
  if (!j.is_object()) return s;
  if (j.contains(kProgressKey) && j[kProgressKey].is_string()) {
    const std::string v = j[kProgressKey].get<std::string>();
    if (v == "half") s.progress = EvmProgress::FiftyFifty;  // "zero" と知らない値は既定
  }
  if (j.contains(kSpiWarnKey) && j[kSpiWarnKey].is_number()) {
    const double v = j[kSpiWarnKey].get<double>();
    if (std::isfinite(v) && v >= kEvmSpiWarnMin && v <= kEvmSpiWarnMax) s.spiWarn = v;
  }
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != kProgressKey && it.key() != kSpiWarnKey) s.extra[it.key()] = it.value();
  return s;
}

}  // namespace roadmap
