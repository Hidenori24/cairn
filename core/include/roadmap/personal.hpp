#pragma once
#include "roadmap/model.hpp"
#include "roadmap/clock.hpp"
namespace roadmap {
// 最新値と追記履歴。画面とファイル保存が同じ形式を共有する。
Json personalFromJson(const Json& source);
std::optional<double> personalPercent(const Json& data, const std::string& id);
void recordPersonal(Json& data, const std::vector<Issue>& issues, const IClock& clock);
bool setPersonalPercent(Json& data, const std::string& id, std::optional<double> value);
}

namespace roadmap {
// 内部IDを固定した対象者。表示名を変えても報告・担当名の対応を保つ。
std::string addPersonalMember(Json& data, const std::string& name);
void syncPersonalMembers(Json& data, const std::vector<Issue>& issues);
bool renamePersonalMember(Json& data, const std::string& member, const std::string& name);
std::optional<double> plannedPercent(const BaselineItem& item, Day day, const Calendar& cal);
// -1=遅れ / 0=許容範囲または比較なし / 1=先行。実績と予定の差は百分率のポイント。
int progressTone(std::optional<double> actual, std::optional<double> planned, double margin = 10);
const Json* previousPersonalReport(const Json& data, const std::string& member, Day before);
Result<Json> makePersonalDraft(const Json& data, const std::vector<Issue>& issues, const std::string& member,
                              Day date, Day from, Day to, const Baseline* baseline, const Calendar& cal,
                              const std::string& legacyNotes = "");
// 保存した写しは変更しない。同じ報告日の再保存も版を追加する。
Result<std::string> savePersonalReport(Json& data, Json draft, const IClock& clock);
std::string personalDraftKey(const std::string& member, Day date);
}

namespace roadmap {
// 保存済みのIDだけを出力する。存在しないID・空の選択はエラー。
Result<std::string> renderPersonalReports(const Json& data, const std::vector<std::string>& ids, bool html, bool fragment = false);
}
