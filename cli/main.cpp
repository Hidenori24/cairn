// rmcli: 計算・変換・検査を画面なしで実行する（SPEC 13章 P4）
// 終了コード: 成功 0 / 入力の誤り 2 / 内部エラー 1
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "roadmap/encoding.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/jira_check.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/store.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;

namespace {

constexpr int kOk = 0, kInternal = 1, kInput = 2;
bool gVerbose = false;

struct InputError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

void vlog(const std::string& s) {
  if (gVerbose) std::cerr << "[rmcli] " << s << "\n";
}

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;
  bool has(const std::string& k) const { return opt.count(k) > 0; }
  std::string get(const std::string& k, const std::string& def = "") const {
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
  }
};

// --key value / --key=value / --flag
Args parseArgs(int argc, char** argv, int from, const std::vector<std::string>& flags) {
  Args a;
  for (int i = from; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "-o") s = "--out";
    if (s == "-h") s = "--help";
    if (s.rfind("--", 0) == 0) {
      std::string k = s.substr(2), v;
      const auto eq = k.find('=');
      if (eq != std::string::npos) {
        v = k.substr(eq + 1);
        k = k.substr(0, eq);
      } else if (std::find(flags.begin(), flags.end(), k) == flags.end()) {
        if (i + 1 >= argc) throw InputError("--" + k + " に値がありません");
        v = argv[++i];
      }
      a.opt[k] = v;
    } else {
      a.pos.push_back(s);
    }
  }
  return a;
}

std::string readFileOrThrow(const std::string& path) {
  StdFileStore fs;
  auto st = fs.stat(path);
  if (!st) throw InputError("ファイルがありません: " + path);
  if (st->size > kMaxCsvBytes) throw InputError("ファイルが大きすぎます（50MB超）: " + path);
  auto r = fs.read(path);
  if (!r) throw InputError(r.error());
  return encoding::decodeText(*r).text;
}

void writeOut(const Args& a, const std::string& text) {
  const std::string out = a.get("out");
  if (out.empty() || out == "-") {
    std::cout << text;
    return;
  }
  StdFileStore fs;
  if (auto s = fs.writeAtomic(out, text); !s) throw std::runtime_error(s.error());
  vlog("書きました: " + out);
}

struct Loaded {
  csv::ImportResult imp;
  Calendar cal;
  ScheduleOptions opt;
  bool defaultHolidays = true;
};

Loaded load(const Args& a, const std::string& path) {
  Loaded L;
  auto imp = csv::importCsv(readFileOrThrow(path));
  if (!imp) throw InputError(path + ": " + imp.error());
  L.imp = std::move(imp).value();
  std::string hol = defaultHolidaysText();
  if (a.has("holidays")) {
    hol = readFileOrThrow(a.get("holidays"));
    L.defaultHolidays = false;
  }
  L.cal = Calendar::fromText(hol);
  const std::string base = a.get("base", SystemClock().todayIso());
  auto b = parseIso(base);
  if (!b) throw InputError("--base は YYYY-MM-DD で指定してください: " + base);
  L.opt.base = *b;
  L.opt.scenario = a.get("scenario", kStdScenario);
  if (a.has("level")) {
    const int cap = std::atoi(a.get("level").c_str());
    if (cap < 1) throw InputError("--level は1以上の整数です");
    L.opt.resource = {true, cap};
  }
  return L;
}

Json optDate(const OptDay& d) { return d ? Json(fmtIso(*d)) : Json(nullptr); }

// ---------------------------------------------------------------- コマンド
const char* const kHelpSchedule =
    "使い方: rmcli schedule <CSV> [--base YYYY-MM-DD] [--scenario 名前] [--level 同時件数]\n"
    "                       [--holidays 休業日ファイル] [-o 出力.json]\n"
    "  CSV を取り込んでスケジュールを計算し、golden と同じ形式の JSON を出します。\n"
    "  --base     基準日（既定は今日）\n"
    "  --scenario シナリオ名（既定「標準」）\n"
    "  --level    担当者ごとの同時作業の制限をオンにし、同時に持てる件数を指定\n"
    "  --holidays 休業日（1行に1日）。省略時は既定の33日\n";

int cmdSchedule(const Args& a) {
  if (a.has("help") || a.pos.empty()) {
    std::cout << kHelpSchedule;
    return a.has("help") ? kOk : kInput;
  }
  Loaded L = load(a, a.pos[0]);
  auto c = computeAll(L.imp.issues, L.imp.meta, L.cal, L.opt);
  if (!c) throw InputError(c.error());
  const Tree& T = c->T;
  Json rows = Json::array();
  for (int id : T.dfs) {
    const ScheduleRow& r = c->R[id];
    const bool leaf = T.isLeaf(id);
    Json j = Json::object();
    j["id"] = T.ids[id];
    j["did"] = T.did[id];
    j["name"] = T.name[id];
    j["depth"] = T.depth[id];
    j["leaf"] = r.leaf;
    j["cat"] = catName(T.cat[id]);
    j["kind"] = leaf ? Json(T.kind[id] == Kind::Ms ? "ms" : "task") : Json(nullptr);
    j["asapStart"] = fmtIso(r.asapStart);
    j["asapFinish"] = fmtIso(r.asapFinish);
    j["alapStart"] = fmtIso(r.alapStart);
    j["alapFinish"] = fmtIso(r.alapFinish);
    j["float"] = r.floatDays;
    j["status"] = r.status;
    j["weeks"] = leaf ? Json(r.weeks) : Json(nullptr);
    j["days"] = leaf ? Json(r.days) : Json(nullptr);
    j["waited"] = r.waited;
    j["pinned"] = r.pinned;
    j["effectiveDeadline"] = leaf ? optDate(T.dl[id]) : Json(nullptr);
    j["ownDeadline"] = optDate(T.ownDl[id]);
    j["n"] = r.n;
    j["nDone"] = r.nDone;
    rows.push_back(std::move(j));
  }
  Json out = Json::object();
  out["input"] = a.pos[0];
  out["baseDate"] = fmtIso(L.opt.base);
  out["scenario"] = L.opt.scenario;
  out["levelResources"] = Json{{"on", L.opt.resource.on}, {"cap", L.opt.resource.cap}};
  out["holidays"] = L.defaultHolidays ? "DEFAULT_HOLIDAYS(別掲 holidays.txt)" : a.get("holidays");
  out["warns"] = T.warns;
  out["scenarios"] = T.scenarios;
  out["rows"] = std::move(rows);
  writeOut(a, dumpJs(out));
  return kOk;
}

const char* const kHelpImportExport =
    "使い方: rmcli import-export <CSV> [--format app|jira] [--base YYYY-MM-DD] [--scenario 名前]\n"
    "                            [--level 同時件数] [--holidays ファイル] [-o 出力.csv]\n"
    "  CSV を取り込み、アプリ用（入力した値のまま）または JIRA用（計画日・3段の階層）で書き出します。\n"
    "  出力は UTF-8（BOMなし）・改行 CRLF。\n";

int cmdImportExport(const Args& a) {
  if (a.has("help") || a.pos.empty()) {
    std::cout << kHelpImportExport;
    return a.has("help") ? kOk : kInput;
  }
  const std::string fmt = a.get("format", "app");
  if (fmt != "app" && fmt != "jira") throw InputError("--format は app か jira です");
  Loaded L = load(a, a.pos[0]);
  for (const auto& w : L.imp.warns) std::cerr << "注意: " << w << "\n";
  if (fmt == "app") {
    writeOut(a, exportAppCsv(L.imp.issues, L.imp.meta));
    return kOk;
  }
  auto c = computeAll(L.imp.issues, L.imp.meta, L.cal, L.opt);
  if (!c) throw InputError(c.error());
  writeOut(a, exportJiraCsv(L.imp.issues, L.imp.meta, &c.value()));
  return kOk;
}

const char* const kHelpCheckJira =
    "使い方: rmcli check-jira <CSV>... [--raw] [--base YYYY-MM-DD]\n"
    "  JIRA の標準CSV取り込みで問題になる点を検査します（付録E）。\n"
    "  既定では、CSV をアプリに取り込んで「JIRA用」で書き出した結果を調べます。\n"
    "  --raw  書き出し済みの JIRA用CSV をそのまま調べる\n"
    "  すべて OK なら終了コード 0、問題があれば 2。\n";

int cmdCheckJira(const Args& a) {
  if (a.has("help") || a.pos.empty()) {
    std::cout << kHelpCheckJira;
    return a.has("help") ? kOk : kInput;
  }
  bool all = true;
  for (const auto& p : a.pos) {
    std::string text;
    if (a.has("raw")) {
      text = readFileOrThrow(p);
    } else {
      Loaded L = load(a, p);
      auto c = computeAll(L.imp.issues, L.imp.meta, L.cal, L.opt);
      if (!c) throw InputError(p + ": " + c.error());
      text = exportJiraCsv(L.imp.issues, L.imp.meta, &c.value());
    }
    const auto r = checkJiraCsv(text);
    for (const auto& line : r.report(p)) std::cout << line << "\n";
    all = all && r.ok;
  }
  std::cout << (all ? "ALL OK" : "HAS ISSUES") << "\n";
  return all ? kOk : kInput;
}

const char* const kHelpGen =
    "使い方: rmcli gen [--count 件数] [--seed 整数] [-o 出力.csv]\n"
    "  テスト用の大きなCSV（階層・依存・状況・担当者・期限つき）を乱数で作ります。\n"
    "  既定は 5000件・シード 1。シードが同じなら同じ内容になります。\n";

int cmdGen(const Args& a) {
  if (a.has("help")) {
    std::cout << kHelpGen;
    return kOk;
  }
  GenOptions o;
  o.count = std::atoi(a.get("count", "5000").c_str());
  o.seed = std::strtoull(a.get("seed", "1").c_str(), nullptr, 10);
  if (o.count < 1 || o.count > 1000000) throw InputError("--count は 1〜1000000 です");
  auto g = generateProject(o);
  writeOut(a, exportAppCsv(g.issues, g.meta));
  return kOk;
}

const char* const kHelpValidate =
    "使い方: rmcli validate <CSV>... [--base YYYY-MM-DD]\n"
    "  CSV が読めるか（取り込み・ツリー構築・計算）を調べ、警告の一覧を出します。\n"
    "  読めなければ終了コード 2。\n";

int cmdValidate(const Args& a) {
  if (a.has("help") || a.pos.empty()) {
    std::cout << kHelpValidate;
    return a.has("help") ? kOk : kInput;
  }
  bool all = true;
  for (const auto& p : a.pos) {
    std::string text;
    try {
      text = readFileOrThrow(p);
    } catch (const InputError& e) {
      std::cout << p << ": NG " << e.what() << "\n";
      all = false;
      continue;
    }
    auto imp = csv::importCsv(text);
    if (!imp) {
      std::cout << p << ": NG " << imp.error() << "\n";
      all = false;
      continue;
    }
    auto t = buildTree(imp->issues, imp->meta);
    if (!t) {
      std::cout << p << ": NG " << t.error() << "\n";
      all = false;
      continue;
    }
    std::cout << p << ": OK " << imp->issues.size() << "件\n";
    for (const auto& w : imp->warns) std::cout << "   - " << w << "\n";
    for (const auto& w : t->warns) std::cout << "   - " << w << "\n";
  }
  return all ? kOk : kInput;
}

const char* const kHelp =
    "rmcli — プロジェクト管理ツール（C++版）のコマンドライン\n"
    "使い方: rmcli <コマンド> [引数] [--verbose]\n"
    "コマンド:\n"
    "  schedule       CSV からスケジュールを計算して JSON を出す\n"
    "  import-export  CSV を取り込み、アプリ用 / JIRA用 で書き出す\n"
    "  check-jira     JIRA用CSV の互換チェック\n"
    "  gen            テスト用の大きなCSVを作る\n"
    "  validate       CSV が読めるかと警告の一覧\n"
    "各コマンドの詳細は rmcli <コマンド> --help\n"
    "終了コード: 成功 0 / 入力の誤り 2 / 内部エラー 1\n";

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // 引数は UTF-16 で受け取り UTF-8 に直す（ANSI のコードページに依存しない）
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> wargs;
  std::vector<char*> wargv;
  {
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
      const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
      std::string u(static_cast<std::size_t>(std::max(len, 1)) - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w[i], -1, u.data(), len, nullptr, nullptr);
      wargs.push_back(std::move(u));
    }
    LocalFree(w);
    for (auto& a : wargs) wargv.push_back(a.data());
    argc = static_cast<int>(wargv.size());
    argv = wargv.data();
  }
#endif
  if (argc < 2) {
    std::cout << kHelp;
    return kInput;
  }
  const std::string cmd = argv[1];
  if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    std::cout << kHelp;
    return kOk;
  }
  const std::map<std::string, std::function<int(const Args&)>> cmds = {
      {"schedule", cmdSchedule}, {"import-export", cmdImportExport}, {"check-jira", cmdCheckJira},
      {"gen", cmdGen},           {"validate", cmdValidate}};
  auto it = cmds.find(cmd);
  if (it == cmds.end()) {
    std::cerr << "不明なコマンド: " << cmd << "\n" << kHelp;
    return kInput;
  }
  try {
    const Args a = parseArgs(argc, argv, 2, {"help", "raw", "verbose"});
    gVerbose = a.has("verbose");
    return it->second(a);
  } catch (const InputError& e) {
    std::cerr << "エラー: " << e.what() << "\n";
    return kInput;
  } catch (const std::exception& e) {
    std::cerr << "内部エラー: " << e.what() << "\n";
    return kInternal;
  }
}
