// 画像のデコード（添付のサムネイル・プレビュー用）。stb_image（パブリックドメイン / MIT）を使う。
// 壊れたファイル・対応していない形式・大きすぎる画像は nullopt を返し、落ちない。ファイル名・中身はログに出さない。
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace roadmap::ui {

struct Pixels {
  int w = 0, h = 0;          // 縮小後の大きさ
  int origW = 0, origH = 0;  // 元の大きさ
  std::vector<unsigned char> rgba;  // w * h * 4
};

// PNG / JPEG / GIF（先頭のコマ）/ BMP を RGBA にする。長辺が maxDim を超えれば、縦横比を保って縮小する（平均）。
// 画素数が 1億（100MP）を超えるものは読まない（メモリを使い切らないため）
std::optional<Pixels> decodeImage(const std::string& bytes, int maxDim);

}  // namespace roadmap::ui
