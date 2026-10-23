// EVM（出来高管理。Issue #65）の設定: project.settings.json の "evm"。
//  - 進行中の扱い（0/100 が既定、50/50 も選べる）と、SPI の警告のしきい値
//  - 既定のときは書かない。読み込みでは壊れた値を項目ごとに読み飛ばして既定に戻し、未知のキーは消さずに書き戻す
// 集計そのものは evm.hpp。UI・Win32・ImGui に依存しない。
#pragma once

#include <nlohmann/json.hpp>

namespace roadmap {

// SPI の警告のしきい値: 既定と、設定できる範囲（0 を含まない）
inline constexpr double kEvmSpiWarnDefault = 0.9;
inline constexpr double kEvmSpiWarnMin = 0.1;
inline constexpr double kEvmSpiWarnMax = 1.0;

// 進行中の課題の出来高の数え方
enum class EvmProgress {
  ZeroHundred,  // 0/100: 完了するまで 0%、完了で 100%（既定）
  FiftyFifty    // 50/50: 着手で 50%、完了で 100%
};

struct EvmSettings {
  EvmProgress progress = EvmProgress::ZeroHundred;
  double spiWarn = kEvmSpiWarnDefault;  // SPI がこれ未満なら「!」を付ける
  nlohmann::ordered_json extra = nlohmann::ordered_json::object();  // 未知のキー（新しい版が足したもの。書き戻す）
  bool isDefault() const {
    return progress == EvmProgress::ZeroHundred && spiWarn == kEvmSpiWarnDefault && (!extra.is_object() || extra.empty());
  }
  bool operator==(const EvmSettings&) const = default;
};

// 既定で、未知のキーもなければ空のオブジェクト（呼び出し側は書かない）
nlohmann::ordered_json evmSettingsToJson(const EvmSettings& s);
// 壊れた値（型・範囲・知らない値）は読み飛ばして既定にする。オブジェクトでなければ既定
EvmSettings evmSettingsFromJson(const nlohmann::ordered_json& j);

}  // namespace roadmap
