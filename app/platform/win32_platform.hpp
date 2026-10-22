// Win32 の小道具（UTF-8 ⇔ UTF-16、ファイルダイアログ、フォルダを開く、テーマの判定）
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/app_ui.hpp"

namespace roadmap::win {

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string exeDir();
std::string localAppDataDir();  // %LOCALAPPDATA%\ProjectRoadmap
std::string envVar(const char* name);
bool systemDarkMode();
void setDarkTitleBar(HWND hwnd, bool dark);

class Win32Platform final : public ui::IPlatform {
 public:
  explicit Win32Platform(HWND hwnd) : hwnd_(hwnd) {}
  std::optional<std::pair<std::string, std::string>> openTextFile() override;
  bool saveTextFile(const std::string& suggestedName, const std::string& content) override;
  void openFolder(const std::string& path) override;
  std::optional<std::string> pickSaveFile(const std::string& suggestedName, const std::string& ext, const std::string& typeLabel) override;
  void openFile(const std::string& path) override;
  bool openLink(const std::string& target, std::string* error) override;
  ImTextureID createTexture(const unsigned char* rgba, int w, int h) override;
  void releaseTexture(ImTextureID tex) override;
  std::optional<std::string> readTextFile(const std::string& path, std::string* error) override;
  std::optional<std::string> pickFolder(const std::string& start) override;
  std::vector<std::string> pickOpenFiles(const std::string& title) override;
  std::optional<std::string> clipboardImage() override;
  bool systemDark() override { return systemDarkMode(); }
  void requestQuit() override { quit_ = true; }
  std::optional<std::string> clipboardText() override;
  bool setClipboardText(const std::string& text) override;
  bool quitRequested() const { return quit_; }
  void setHwnd(HWND h) { hwnd_ = h; }
  void setDevice(ID3D11Device* d) { device_ = d; }  // 画像のテクスチャを作る先（描画に使うデバイス）

 private:
  HWND hwnd_;
  ID3D11Device* device_ = nullptr;
  bool quit_ = false;
};

// ファイルを読み、文字コードを判定して UTF-8 にする（50MB を超えるものは読まない）
std::optional<std::string> readTextFile(const std::wstring& path, std::string* error);

}  // namespace roadmap::win
