// Win32 の編集中ロックのファイル操作（Issue #62。SPEC 8.5）。
//  - 作成: CreateFileW(CREATE_NEW)。既にあれば失敗する（SMB の共有ドライブではサーバー側で排他的に決まる）。
//    OneDrive・Google Drive などの同期フォルダでは、排他的な作成が保証されないため対象外
//  - 更新: 一時ファイルへ書いて置き換えず、同じファイルの先頭へ同じ大きさを上書きする（他のマシンが読むために開いていても
//    書ける。読む側が空のファイルを見る時間をなくす）
//  - 読み込み・削除: 共有の指定（読み・書き・削除）を全部許して開く
// ロックの中身（利用者名・マシン名）はログに出さない
#pragma once

#include <string>

#include "roadmap/lock.hpp"

namespace roadmap::win {

class Win32LockStore final : public ILockStore {
 public:
  LockCreateResult tryCreate(const std::string& path, const std::string& data) override;
  Result<std::optional<std::string>> read(const std::string& path) override;
  Status update(const std::string& path, const std::string& data) override;
  Status remove(const std::string& path) override;
};

// このマシンの名前（ロックに入れる。取れなければ空）
std::string machineName();

}  // namespace roadmap::win
