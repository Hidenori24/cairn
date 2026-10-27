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
