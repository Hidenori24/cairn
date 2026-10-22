// GitHub CLI（gh）で PR・Issue の状態を取る（Issue #61。SPEC 8.11）。
//  - gh.exe を CreateProcessW で直接起動する（cmd /c などのシェルは通さない）。引数は検証済みの URL を 1 つの引数として渡す
//  - gh は PATH の絶対パスのフォルダだけから探す（カレントフォルダは見ない）
//  - 標準入力・標準エラーは NUL。標準出力は 64KB まで読み、15 秒で打ち切ってプロセスを終了する
//  - URL・リポジトリ名・タイトルは、理由にもログにも出さない
#pragma once

#include <cstdint>
#include <memory>

#include "roadmap/github_ref.hpp"

namespace roadmap::win {

std::shared_ptr<IRemoteStatus> makeGhRemoteStatus();
// その時刻（1970-01-01T00:00:00Z からの秒数）に、OS の時間帯の設定で適用される UTC からの差（分。日本時間は 540）。
// 夏時間のある時間帯は、その時刻の規則で返す（過去の閉じた日時を、いまの値で一律に換算しない）。変換できなければ 0
int utcOffsetMinutesAt(std::int64_t epochSeconds);

}  // namespace roadmap::win
