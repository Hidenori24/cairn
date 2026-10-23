// 版ごとの見本（tests/compat/）で、互換の約束（docs/COMPAT.md）を確かめる。
//   ① 今の版でそのフォルダを開ける（警告なし・開いただけでは何も書かない）
//   ② 設定だけを保存し直しても CSV のバイト列は変わらない（元に戻せば設定の JSON も同じバイト列）
//   ③ 未知のキーが消えない（置き場所ごと。今は消える場所があり、それは should_fail で残してある）
//   ④ 見本の CSV（アプリの形式に正規化済み）を取り込んで書き出すと同じバイト列（任意の CSV のバイト保持ではない。LF などは正規化される）
//   ほか: 「JIRA用」の書き出し・共有／バックアップ JSON・roadmap_prefs.json・平置きからの移行
// 見本は架空のデータ。パスは ASCII だけ（Windows でも同じに読むため）。
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../test_util.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/jira_check.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/sha256.hpp"
#include "roadmap/store.hpp"

using namespace roadmap;
namespace fsys = std::filesystem;

namespace {

// 共有／バックアップ JSON の見本の "exported"（2026-10-19T09:00:00.000Z）と同じ時刻になる時計
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0}, 1792400400000LL);

struct Sample {
  const char* ver;       // tests/compat/<ver>/
  const char* id;        // project.settings.json の id
  const char* csvRel;    // <ver>/data/ からの相対パス（'/' 区切り・ASCII）
  const char* setRel;
  bool flat;             // 以前の平置き（0.3.x まで）
  const char* unknownCol;  // CSV にある未知の列（JIRA の列など）。なければ空
  int uiScale;           // roadmap_prefs.json の uiScale
};

const Sample kSamples[] = {
    {"v0.3", "pcompat03a1", "legacy-flat.csv", "legacy-flat.settings.json", true, "", 110},
    {"v0.5", "pcompat05b2", "portal-v05/project.csv", "portal-v05/project.settings.json", false, "", 100},
    {"v0.7", "pcompat07c3", "expense-v07/project.csv", "expense-v07/project.settings.json", false, "Sprint", 125},
    {"v0.9", "pcompat09d4", "manual-v09/project.csv", "manual-v09/project.settings.json", false, "Attachment", 100},
    {"v0.9.1", "pcompat091e5", "inventory-v091/project.csv", "inventory-v091/project.settings.json", false, "Created", 100},
    {"v0.10", "pcompat010f6", "inventory-v010/project.csv", "inventory-v010/project.settings.json", false, "Created", 100},
};

const Json kMarker = Json{{"n", 1}, {"s", "x"}};  // 「今の版が知らない」キーの値

std::string readAll(const fsys::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

fsys::path sampleRoot(const Sample& s) { return fsys::path(ROADMAP_COMPAT_DIR) / s.ver; }
// 見本のファイルの元のバイト列（<ver>/data/ からの相対）
std::string original(const Sample& s, const std::string& rel) { return readAll(sampleRoot(s) / "data" / rel); }
std::string originalTop(const Sample& s, const char* name) { return readAll(sampleRoot(s) / name); }

// <ver>/data/ の全ファイルをメモリ上のデータフォルダ（"data/…"）へ写す
void loadTree(MemFileStore& fs, const Sample& s) {
  const fsys::path base = sampleRoot(s) / "data";
  REQUIRE_MESSAGE(fsys::is_directory(base), "見本のフォルダがありません: " << base.string());
  for (const auto& e : fsys::recursive_directory_iterator(base)) {
    if (!e.is_regular_file()) continue;
    fs.put("data/" + fsys::relative(e.path(), base).generic_string(), readAll(e.path()));
  }
}

std::string dataPath(const std::string& rel) { return "data/" + rel; }

// 見本を読み込んで開いた状態
struct Opened {
  MemFileStore fs;
  Workspace ws;
  int writesAtOpen = 0;
  explicit Opened(const Sample& s) : ws(fs, clk, "data") {
    loadTree(fs, s);
    writesAtOpen = fs.writeCount();
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(r->added == 1);
    CHECK(r->bad.empty());
    REQUIRE(ws.entries().size() == 1);
  }
  ProjectEntry& entry() { return ws.entries()[0]; }
};

}  // namespace

// ------------------------------------------------------------------ ① 開ける
TEST_CASE("compat: 各版のプロジェクトを今の版で開ける（警告なし・開いただけでは何も書かない）") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    const std::string csv0 = original(s, s.csvRel);
    // tests/compat は git に改行を変換させない（.gitattributes の -text）。変換されたら以降のバイト比較が意味を失う
    REQUIRE_MESSAGE(csv0.find("\r\n") != std::string::npos, "見本の CSV が CRLF ではありません（.gitattributes を確かめてください）");
    Opened o(s);
    CHECK(o.fs.writeCount() == o.writesAtOpen);  // 開いただけでは書かない
    ProjectEntry& e = o.entry();
    CHECK(e.id() == s.id);
    CHECK((e.layout == Layout::Flat) == s.flat);
    CHECK_FALSE(e.dirty);
    CHECK_FALSE(e.settingsDirty);

    auto h = hydrate(e.rec);
    REQUIRE(h.ok());
    CHECK(h->warns.empty());
    CHECK(h->data.issues.size() >= 5);
    auto t = buildTree(h->data.issues, h->data.meta);
    CHECK(t.ok());
    if (!std::string(s.unknownCol).empty()) {
      const auto& hs = h->data.meta.extraHeaders;
      CHECK_MESSAGE(std::find(hs.begin(), hs.end(), s.unknownCol) != hs.end(), "未知の列がありません: " << s.unknownCol);
    }
  }
}

TEST_CASE("compat: その版が書いたキーが、今の版で読める値になっている") {
  {  // 0.3.x: 平置き・資源制限。添付以降のキーはない
    Opened o(kSamples[0]);
    const ProjectSettings& st = o.entry().rec.settings;
    CHECK(o.entry().layout == Layout::Flat);
    CHECK(st.resource.on);
    CHECK(st.resource.cap == 2);
    CHECK(st.attachments.empty());
    CHECK(st.weekly.empty());
    CHECK(st.links.empty());
    CHECK(st.baselines.empty());
    CHECK(st.history.empty());
    CHECK(o.entry().rec.scenarios == std::vector<std::string>{"短縮案"});
  }
  {  // 0.5.x: 添付（sha256 なし）・週次レポート・先行の列
    Opened o(kSamples[1]);
    const ProjectSettings& st = o.entry().rec.settings;
    REQUIRE(st.attachments.count("3") == 1);
    REQUIRE(st.attachments.at("3").size() == 1);
    CHECK(st.attachments.at("3")[0].name == "設計メモ.pdf");
    CHECK(st.attachments.at("3")[0].sha256.empty());
    CHECK(st.weekly.snaps.size() == 1);
    CHECK_FALSE(st.weekly.notes.empty());
    CHECK(st.ui["cols"]["pred"] == true);
    CHECK_FALSE(st.ui.contains("attachThumb"));  // 0.6.0 から
    CHECK(st.links.empty());
    CHECK(st.baselines.empty());
    CHECK(st.history.empty());
  }
  {  // 0.7.x: サムネイルの大きさ・SHA-256（古い添付は sha256 なしのまま）
    Opened o(kSamples[2]);
    const ProjectSettings& st = o.entry().rec.settings;
    REQUIRE(st.attachments.count("3") == 1);
    REQUIRE(st.attachments.at("3").size() == 2);
    CHECK(isSha256Hex(st.attachments.at("3")[0].sha256));
    CHECK(st.attachments.at("3")[1].sha256.empty());
    CHECK(st.weekly.snaps.size() == 2);
    CHECK(st.ui["attachThumb"] == 96);
    CHECK(st.links.empty());
    CHECK(st.history.empty());
  }
  {  // 0.9.x: ベースライン・関連リンク・状況の件数の履歴
    Opened o(kSamples[3]);
    const ProjectSettings& st = o.entry().rec.settings;
    REQUIRE(st.baselines.list.size() == 1);
    CHECK(st.baselines.shown == "b1");
    CHECK(st.baselines.list[0].items.size() == 5);
    CHECK(st.history.points.size() == 3);
    REQUIRE(st.links.count("6") == 1);
    CHECK(st.links.at("6").size() == 2);
    REQUIRE(st.links.count("7") == 1);
    CHECK(st.links.at("7").size() == 1);
    CHECK(st.ui["cols"]["plan"] == true);
    CHECK(st.attachments.at("6")[0].name == "draft.docx");
  }
  {  // 0.9.1: weeklyAuto が増える。CSV に Created / Updated が増える
    Opened o(kSamples[4]);
    const ProjectSettings& st = o.entry().rec.settings;
    CHECK(st.links.size() == 1);
    CHECK(st.history.points.size() == 1);
    CHECK(st.weeklyAuto.snaps.size() == 1);  // 開いたときに自動で取る週の頭のスナップショット（#70）
    CHECK(st.weekly.empty());
    CHECK(st.evm.isDefault());  // evm は 0.10.0 から
  }
  {  // 0.10.0: EVM（出来高管理）の設定 "evm"（Issue #65）
    Opened o(kSamples[5]);
    const ProjectSettings& st = o.entry().rec.settings;
    CHECK(st.evm.progress == EvmProgress::FiftyFifty);
    CHECK(st.evm.spiWarn == 0.8);
    CHECK(st.weeklyAuto.snaps.size() == 1);
    CHECK(st.links.size() == 1);
    CHECK_FALSE(st.unknown.contains("evm"));  // 既知のキーとして読む（未知のキーとして持たない）
  }
}

// ------------------------------------------------------------------ ② 設定だけの保存
TEST_CASE("compat: 開いて設定だけを保存し直しても、CSV のバイト列は変わらない") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    const std::string csv0 = original(s, s.csvRel);
    const std::string set0 = original(s, s.setRel);
    Opened o(s);
    ProjectEntry& e = o.entry();
    const std::string csvPath = dataPath(s.csvRel), setPath = dataPath(s.setRel);

    ProjectSettings st = e.rec.settings;
    st.ui["nameW"] = 321;  // 設定だけの変更（履歴の記録・表示の切り替えと同じ経路）
    o.ws.updateSettings(s.id, st);
    CHECK(e.settingsDirty);
    CHECK_FALSE(e.dirty);
    const int w0 = o.fs.writeCount();
    REQUIRE(o.ws.flush().ok());
    CHECK(o.fs.writeCount() == w0 + 1);  // 設定の JSON だけ
    CHECK(o.fs.content(csvPath) == csv0);
    for (const auto& p : o.fs.paths()) CHECK_MESSAGE(p.find("バックアップ") == std::string::npos, "控えを作ってはいけません: " << p);

    // 設定は新しい値になり、ほかのキーは（順序も）元のまま
    Json before = Json::parse(set0);
    Json after = Json::parse(o.fs.content(setPath));
    CHECK(after["ui"]["nameW"] == 321);
    after["ui"]["nameW"] = before["ui"]["nameW"];
    CHECK(after == before);

    // 元の値に戻して保存し直すと、その版が書いた設定の JSON と 1 バイトも違わない
    st.ui["nameW"] = before["ui"]["nameW"];
    o.ws.updateSettings(s.id, st);
    REQUIRE(o.ws.flush().ok());
    CHECK(o.fs.content(setPath) == set0);
    CHECK(o.fs.content(csvPath) == csv0);
  }
}

// ------------------------------------------------------------------ ③ 未知のキー
namespace {

enum class Kept { NotApplicable, Yes, No };

// 設定 JSON の kind の場所（JSON Pointer）。その版にその項目がなければ nullopt
std::optional<std::string> pointerFor(const std::string& kind, const Json& j) {
  const auto has = [&](const std::string& ptrText) { return j.contains(Json::json_pointer(ptrText)); };
  const auto firstItem = [&](const char* key) -> std::string {  // 先頭の課題の先頭の項目
    if (!j.contains(key) || !j[key].is_object() || j[key].empty()) return "";
    return std::string("/") + key + "/" + j[key].begin().key() + "/0";
  };
  std::string p;
  if (kind == "top") return std::string();
  if (kind == "ui") p = "/ui";
  else if (kind == "ui.cols") p = "/ui/cols";
  else if (kind == "res") p = "/res";
  else if (kind == "links") p = firstItem("links");
  else if (kind == "attachments") p = firstItem("attachments");
  else if (kind == "weekly") p = "/weekly";
  else if (kind == "weekly.snap") p = "/weekly/snaps/0";
  else if (kind == "weeklyAuto") p = "/weeklyAuto";
  else if (kind == "weeklyAuto.snap") p = "/weeklyAuto/snaps/0";
  else if (kind == "baselines") p = "/baselines";
  else if (kind == "baselines.item") p = "/baselines/list/0";
  else if (kind == "history") p = "/history";
  else if (kind == "history.point") p = "/history/points/0";
  else if (kind == "evm") p = "/evm";
  if (p.empty() || !has(p)) return std::nullopt;
  return p;
}

// 見本の設定 JSON の kind の場所に "futureKey" を足して開き、設定を保存し直したあとも残っているか。
// settingsOnly: 設定だけを保存する経路（#67）／そうでなければ課題の編集と同じ全体の保存
Kept futureKeyKept(const Sample& s, const std::string& kind, bool settingsOnly) {
  Json j = Json::parse(original(s, s.setRel));
  const auto p = pointerFor(kind, j);
  if (!p) return Kept::NotApplicable;
  const Json::json_pointer ptr(*p);
  j[ptr]["futureKey"] = kMarker;

  MemFileStore fs;
  loadTree(fs, s);
  fs.put(dataPath(s.setRel), j.dump(1));
  Workspace ws(fs, clk, "data");
  REQUIRE(ws.sync().ok());
  REQUIRE(ws.entries().size() == 1);
  ProjectEntry& e = ws.entries()[0];

  ProjectSettings st = e.rec.settings;
  st.ui["nameW"] = 321;
  if (settingsOnly) {
    ws.updateSettings(s.id, st);
  } else {
    ProjectRecord r = e.rec;
    r.settings = st;
    ws.update(s.id, r);
    e.dirty = true;
  }
  REQUIRE(ws.flush().ok());
  Json out = Json::parse(fs.content(dataPath(s.setRel)));
  REQUIRE(out["ui"]["nameW"] == 321);  // 書き直したこと
  if (!out.contains(ptr)) return Kept::No;
  const Json& at = out[ptr];
  return at.is_object() && at.contains("futureKey") && at["futureKey"] == kMarker ? Kept::Yes : Kept::No;
}

// kinds のそれぞれについて、全部の版・両方の保存の経路で確かめる。1 つでも消えれば false。確かめた数を n に入れる
bool allKept(const std::vector<std::string>& kinds, int& n) {
  bool ok = true;
  n = 0;
  for (const Sample& s : kSamples)
    for (const std::string& kind : kinds)
      for (const bool settingsOnly : {true, false}) {
        const Kept k = futureKeyKept(s, kind, settingsOnly);
        if (k == Kept::NotApplicable) continue;
        ++n;
        INFO("版 " << s.ver << " / 場所 " << kind << " / " << (settingsOnly ? "設定だけの保存" : "全体の保存"));
        CHECK(k == Kept::Yes);
        if (k != Kept::Yes) ok = false;
      }
  return ok;
}

}  // namespace

TEST_CASE("compat: 未知のキーが消えない（設定のトップレベル・ui・関連リンクの項目・週次レポートのスナップショット・EVM の設定）") {
  int n = 0;
  CHECK(allKept({"top", "ui", "ui.cols", "links", "weekly.snap", "weeklyAuto", "weeklyAuto.snap", "evm"}, n));
  CHECK(n >= 5 * 3);  // 見本が減ってもテストが空にならないこと
}

TEST_CASE("compat: 設定の入れ子にある未知のキーが消えない") {
  int n = 0;
  CHECK(allKept({"res", "attachments", "weekly", "baselines", "baselines.item", "history", "history.point"}, n));
  CHECK(n > 0);
}

// ------------------------------------------------------------------ ④ CSV のバイト列
TEST_CASE("compat: 見本の CSV を取り込んで書き出すと、編集していなければ同じバイト列") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    const std::string csv0 = original(s, s.csvRel);
    auto imp = csv::importCsv(csv0);
    REQUIRE(imp.ok());
    CHECK(imp->warns.empty());
    CHECK(csv::exportCsv(imp->issues, imp->meta) == csv0);

    // 設定を通る経路（hydrate → recordOf）でも同じ
    Opened o(s);
    auto h = hydrate(o.entry().rec);
    REQUIRE(h.ok());
    CHECK(recordOf(h->data).csv == csv0);

    // 全体の保存（課題を編集したときの経路）でも、同じバイト列を書く。書く前の CSV は控えに残る
    ProjectEntry& e = o.entry();
    e.dirty = true;
    e.lastSig.clear();  // 「内容が前回と同じなら書かない」を外して、書く経路を通す
    REQUIRE(o.ws.flush().ok());
    CHECK(o.fs.content(dataPath(s.csvRel)) == csv0);
    CHECK(o.fs.content(dataPath(s.setRel)) == original(s, s.setRel));
    int backups = 0;
    for (const auto& p : o.fs.paths())
      if (p.find("バックアップ") != std::string::npos) {
        ++backups;
        CHECK(o.fs.content(p) == csv0);
      }
    CHECK(backups == 1);
  }
}

// ------------------------------------------------------------------ JIRA 互換
TEST_CASE("compat: 見本を「JIRA用」で書き出せる（未知の列を除いて check-jira の問題が出ない）") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    Opened o(s);
    auto h = hydrate(o.entry().rec);
    REQUIRE(h.ok());
    const ProjectSettings& st = h->data.settings;
    const auto base = parseIso(st.baseDate);
    REQUIRE(base.has_value());
    ScheduleOptions opt;
    opt.base = *base;
    auto c = computeAll(h->data.issues, h->data.meta, Calendar::fromText(st.holidays), opt);
    REQUIRE(c.ok());
    const std::string jira = exportJiraCsv(h->data.issues, h->data.meta, &c.value());
    CHECK(csv::importCsv(jira).ok());  // 書き出したものを、また取り込める
    const auto r = checkJiraCsv(jira);
    CHECK(r.errors.empty());
    CHECK(r.ok);  // JIRA が無視する可能性のある追加列は警告にとどめる
    if (std::string(s.unknownCol).empty()) CHECK(r.warnings.empty());
  }
}

// ------------------------------------------------------------------ 共有・バックアップ JSON
TEST_CASE("compat: 各版の共有・バックアップ JSON を読め、書き出し直すと同じバイト列") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    const std::string js = originalTop(s, "share.json");
    auto b = parseBackup(js, clk);
    REQUIRE(b.ok());
    REQUIRE(b->projects.size() == 1);
    CHECK(b->projects[0].settings.id == s.id);
    CHECK(b->projects[0].csv == original(s, s.csvRel));
    CHECK(b->templates.size() == 1);
    CHECK(exportShareJson(b->projects, b->templates, clk) == js);
  }
}

// ------------------------------------------------------------------ roadmap_prefs.json
TEST_CASE("compat: 各版の roadmap_prefs.json を読め、保存し直しても値と未知のキーが消えない") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    const std::string text = originalTop(s, "roadmap_prefs.json");
    Json j = Json::parse(text);
    j["futureKey"] = kMarker;  // 今の版が知らないトップレベルのキー
    MemFileStore fs;
    fs.put("prefs.json", j.dump(1));
    const Prefs p = loadPrefs(fs, "prefs.json");
    CHECK(p.uiScale == s.uiScale);
    CHECK(p.templates.size() == 1);
    CHECK(p.currentProject == s.id);
    REQUIRE(savePrefs(fs, "prefs.json", p));
    const Json out = Json::parse(fs.content("prefs.json"));
    CHECK(out["futureKey"] == kMarker);
    // その版が書いたキーは、すべて同じ値で残る。新しい版が足すキー（newNameFormat・autoCompleteGitHub など）は増えるので、
    // prefs はバイト列では比べない（バイト比較は設定 JSON と共有 JSON だけ）
    for (auto it = j.begin(); it != j.end(); ++it) {
      INFO("キー " << it.key());
      REQUIRE(out.contains(it.key()));
      CHECK(out[it.key()] == it.value());
    }
  }
}

TEST_CASE("compat: 未知のキー: roadmap_prefs.json の入れ子の中") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    Json j = Json::parse(originalTop(s, "roadmap_prefs.json"));
    j["window"]["futureKey"] = kMarker;
    j["templates"][0]["futureKey"] = kMarker;
    const bool hasInherit = j.contains("inherit") && j["inherit"].is_object();  // 0.4.0 から
    if (hasInherit) j["inherit"]["futureKey"] = kMarker;
    MemFileStore fs;
    fs.put("prefs.json", j.dump(1));
    REQUIRE(savePrefs(fs, "prefs.json", loadPrefs(fs, "prefs.json")));
    const Json out = Json::parse(fs.content("prefs.json"));
    CHECK(out["window"].contains("futureKey"));
    CHECK(out["templates"][0].contains("futureKey"));
    if (hasInherit) CHECK(out["inherit"].contains("futureKey"));
  }
}

// ------------------------------------------------------------------ 以前の平置き
TEST_CASE("compat: 平置きのデータを、フォルダ構成へ移してもバイト列が変わらない") {
  const Sample& s = kSamples[0];
  REQUIRE(s.flat);
  Opened o(s);
  CHECK(o.ws.legacyCount() == 1);
  // 平置きのまま設定だけを保存しても、CSV は平置きの場所に元のまま・控えは作らない
  ProjectSettings st = o.entry().rec.settings;
  st.ui["nameW"] = 321;
  o.ws.updateSettings(s.id, st);
  REQUIRE(o.ws.flush().ok());
  CHECK(o.fs.content("data/legacy-flat.csv") == original(s, s.csvRel));
  st.ui["nameW"] = 300;
  o.ws.updateSettings(s.id, st);
  REQUIRE(o.ws.flush().ok());
  CHECK(o.fs.content("data/legacy-flat.settings.json") == original(s, s.setRel));

  auto r = o.ws.migrateLegacy();
  REQUIRE(r.ok());
  CHECK(r->moved == 1);
  CHECK(r->failed.empty());
  CHECK_FALSE(r->backupDir.empty());
  CHECK(o.fs.content("data/legacy-flat/project.csv") == original(s, s.csvRel));
  CHECK(o.fs.content("data/legacy-flat/project.settings.json") == original(s, s.setRel));
  CHECK_FALSE(o.fs.exists("data/legacy-flat.csv"));
}

// 共有・バックアップ JSON の枠のキー（v・csv）は設定に混ざらず、以前に混ざった値も次の保存で落ちる。
TEST_CASE("compat: 共有 JSON から取り込んで保存した設定に、csv や v が混ざらない") {
  for (const Sample& s : kSamples) {
    CAPTURE(s.ver);
    auto b = parseBackup(originalTop(s, "share.json"), clk);
    REQUIRE(b.ok());
    MemFileStore fs;
    Workspace ws(fs, clk, "data");
    ws.add(b->projects[0]);
    REQUIRE(ws.flush().ok());
    const Json out = Json::parse(fs.content("data/" + ws.entries()[0].stem + "/project.settings.json"));
    CHECK_FALSE(out.contains("csv"));
    CHECK_FALSE(out.contains("v"));
  }
  // 以前の版で混ざった設定ファイルを開いて保存した場合も、CSV 全文の写しを除去する。
  const Sample& s = kSamples[0];
  Json contaminated = Json::parse(original(s, s.setRel));
  contaminated["v"] = 3;
  contaminated["csv"] = original(s, s.csvRel);
  MemFileStore fs;
  loadTree(fs, s);
  fs.put(dataPath(s.setRel), contaminated.dump(1));
  Workspace ws(fs, clk, "data");
  REQUIRE(ws.sync().ok());
  REQUIRE(ws.entries().size() == 1);
  ProjectSettings settings = ws.entries()[0].rec.settings;
  settings.ui["nameW"] = 321;
  ws.updateSettings(s.id, settings);
  REQUIRE(ws.flush().ok());
  const Json out = Json::parse(fs.content(dataPath(s.setRel)));
  CHECK_FALSE(out.contains("csv"));
  CHECK_FALSE(out.contains("v"));
}
