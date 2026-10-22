// GitHub の PR・Issue のリンク（SPEC 8.11。Issue #61）。#60 のリンク（links.hpp）の上に載る。
//  - 許可するのは https://github.com/<owner>/<repo>/pull/<n> と /issues/<n> だけ（末尾の /、?…、#…、/files などの下位パスは
//    許容して正規化する）。http://・www.github.com・ユーザー情報・ポート・GitHub Enterprise は対象外
//  - 状態の取得は IRemoteStatus の窓口に閉じ込める（時計・ファイルと同じ流儀。実装は app/platform の gh、テストは固定の応答）
//  - 状態は保存しない（メモリ上のみ）。閉じられたリンクだけが課題の完了の提案になる（proposeCompletion）
//  - URL・リポジトリ名・タイトルは機微なので、ログにも、理由の文言にも入れない
// UI・Win32 に依存しない。例外は投げず、失敗は戻り値で返す。
#pragma once

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/calendar.hpp"
#include "roadmap/links.hpp"
#include "roadmap/model.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

inline constexpr std::size_t kMaxGitHubNameBytes = 100;      // owner・repo の長さの上限
inline constexpr std::uint32_t kMaxGitHubNumber = 999999999;  // PR・Issue の番号（9 桁まで）
inline constexpr std::size_t kMaxGhOutputBytes = 64 * 1024;   // gh の出力として読む上限
inline constexpr int kGhTimeoutSeconds = 15;                  // gh の待ち時間の上限

enum class GitHubKind { Pull, Issue };

struct GitHubRef {
  std::string owner, repo;
  GitHubKind kind = GitHubKind::Pull;
  std::uint32_t number = 0;
  bool operator==(const GitHubRef&) const = default;
  // 正規化した URL（https://github.com/<owner>/<repo>/pull|issues/<n>）。状態の保存先の鍵・gh に渡す引数に使う
  std::string canonicalUrl() const;
};

// リンクの場所が GitHub の PR・Issue か判定して分解する。許可リストにないものは理由つきで断る（理由に入力の中身は入れない）。
// 空白・制御文字・ASCII 以外・\・パスの中の %XX・".."・host の違い・桁の違う番号などはすべて断る
Result<GitHubRef> parseGitHubUrl(std::string_view target);
// parseGitHubUrl を通る形か（classifyLink が GitHubPull / GitHubIssue を決めるのに使う）
bool isGitHubTarget(std::string_view target);
// 手で作った GitHubRef が、gh に渡してよい形か（owner・repo は英数字と - _ .、番号は 1〜9 桁）
Status validateGitHubRef(const GitHubRef& ref);

// ---- 状態 ----
enum class RemoteState {
  Open,
  Draft,             // PR だけ
  Merged,            // PR だけ
  ClosedUnmerged,    // PR: マージせずに閉じた
  ClosedCompleted,   // Issue: 完了として閉じた
  ClosedNotPlanned,  // Issue: 計画外として閉じた
  Unknown,           // 取得できた情報から判定できない（壊れた JSON・欠け・未知の値）
};
// 画面に出す名前（"open" "draft" "merged" "closed（マージなし）" "closed（完了）" "closed（計画外）" "不明"）
const char* remoteStateLabel(RemoteState s);
inline constexpr bool remoteStateClosed(RemoteState s) {
  return s == RemoteState::Merged || s == RemoteState::ClosedUnmerged || s == RemoteState::ClosedCompleted ||
         s == RemoteState::ClosedNotPlanned;
}

struct RemoteInfo {
  RemoteState state = RemoteState::Unknown;
  // 閉じた（PR は mergedAt があればマージした）日時。UTC の "YYYY-MM-DDTHH:MM:SSZ"。閉じていなければ空
  std::string closedAt;
  std::string reason;  // Unknown のときの理由（日本語。入力の中身は入れない）
  bool operator==(const RemoteInfo&) const = default;
};

// gh の JSON 出力（`gh pr view <URL> --json state,isDraft,mergedAt,closedAt` / `gh issue view <URL> --json state,stateReason,closedAt`）を読む。
// 壊れた JSON・欠け・未知の値・64KB を超える出力は、state = Unknown と理由を返す
RemoteInfo parseGhPrJson(std::string_view json);
RemoteInfo parseGhIssueJson(std::string_view json);

// "YYYY-MM-DDTHH:MM:SSZ"（gh の日時）なら true。ゼロ値（0001-01-01T00:00:00Z）は false
bool isGhTimestamp(std::string_view s);
// UTC の日時 isoUtc を、UTC からの差 utcOffsetMinutes（日本時間は +540）の暦の日にする。形式が違えば nullopt
OptDay localDayOfUtc(std::string_view isoUtc, int utcOffsetMinutes);

// ---- gh を起動する引数 ----
// gh に渡す引数（実行ファイル名を除く）。検証済みの URL を 1 つの引数として渡す:
//   pr view <URL> --json state,isDraft,mergedAt,closedAt / issue view <URL> --json state,stateReason,closedAt
// 手で作った GitHubRef が validateGitHubRef を通らなければ断る（シェルのメタ文字などを渡さない）
Result<std::vector<std::string>> ghArguments(const GitHubRef& ref);
// Windows の CreateProcess に渡すコマンドライン（UTF-8）。CommandLineToArgvW の規則で、空白・引用符を含む引数を引用符にくるむ。
// シェル（cmd /c）は使わない
std::string windowsCommandLine(std::string_view exe, const std::vector<std::string>& args);

// ---- 取得の窓口 ----
class IRemoteStatus {
 public:
  virtual ~IRemoteStatus() = default;
  // PR・Issue の状態を取る。時間がかかるので、呼び出し側は UI と別のスレッドで呼ぶ。
  // 失敗（gh がない・未ログイン・タイムアウトなど）は理由つきのエラー。理由に URL・リポジトリ名・タイトルを入れない
  virtual Result<RemoteInfo> fetch(const GitHubRef& ref) = 0;
  // いま実行中の取得をすぐ終わらせる（アプリの終了・窓口の差し替え）。別のスレッドから呼ばれる。
  // これから始まる取得には影響しない
  virtual void cancel() {}
};

// この環境では取得できない（gh を起動できない環境）。常に失敗する
class UnavailableRemoteStatus final : public IRemoteStatus {
 public:
  Result<RemoteInfo> fetch(const GitHubRef&) override { return Error{"この環境では GitHub の状態を確認できません"}; }
};

// テスト用の固定の応答（スレッドから呼んでも安全）。鍵は GitHubRef::canonicalUrl()
class FakeRemoteStatus final : public IRemoteStatus {
 public:
  void set(const std::string& canonicalUrl, RemoteInfo info);
  void fail(const std::string& canonicalUrl, std::string reason);
  // 応答が設定されていない URL への応答（既定は失敗）
  void setDefaultFailure(std::string reason);
  // true の間、fetch は release() か cancel() まで待つ（確認中の状態を作る）。cancel() は待っている fetch だけを中断する
  void hold(bool on);
  void release();
  Result<RemoteInfo> fetch(const GitHubRef& ref) override;
  void cancel() override;
  int calls() const;
  std::vector<std::string> calledUrls() const;
  int running() const;  // いま fetch の中にいる数

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::map<std::string, Result<RemoteInfo>> answers_;
  std::string defaultFailure_ = "応答が設定されていません";
  bool hold_ = false;
  unsigned cancelGen_ = 0;
  int calls_ = 0;
  int running_ = 0;
  std::vector<std::string> called_;
};

// ---- 完了の提案 ----
// 正規化した URL（GitHubRef::canonicalUrl）→ 取得した状態。確認していないリンクは入っていない
using RemoteStates = std::map<std::string, RemoteInfo>;

struct CompletionProposal {
  Day resolved = 0;            // Resolved に入れる日（各リンクの閉じた日のうち最も遅い日）
  std::string resolvedIso;     // 同じ日の "YYYY-MM-DD"
  int linkCount = 0;           // 数えた GitHub リンクの数
  bool hasUnmerged = false;    // マージなしで閉じられた PR を含む
  bool hasNotPlanned = false;  // 計画外で閉じた Issue を含む
  // 提案の文に添える注意（"マージなしで閉じられたものを含みます" など）。含まなければ空
  std::string caution;
};
// issue の GitHub リンクがすべて閉じているときだけ、完了の提案を返す。
//  - GitHub 以外のリンクは無視する。GitHub のリンクが 1 つもなければ提案しない
//  - 1 件でも open / draft / Unknown / 未確認があれば提案しない
//  - すでに完了（Resolved が入っている・状況が完了）の課題には提案しない
//  - 閉じた日は各リンクの closedAt を utcOffsetMinutes の暦の日にしたもの（PR は mergedAt があればそれ）の、最も遅い日
std::optional<CompletionProposal> proposeCompletion(const Issue& issue, const LinkMap& links, const RemoteStates& states,
                                                    int utcOffsetMinutes = 0);

}  // namespace roadmap
