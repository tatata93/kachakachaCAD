#pragma once

#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Project.h"

#include <string>
#include <vector>

namespace kachakacha::io {

//! 部材近似モデルの「曲げ具合」状態の実体化(ADR 0019 / docs/surface-unfolding-spec.md)。
//! progress=0 は平面に置いた展開状態(型紙そのもの)、1 は折り曲げた近似完成形、
//! 中間は両者の対応点補間。各帯はどの progress でもレール間ルールドなので、
//! レールのポリラインとルールド面で正確に実体化できる。

struct PartFoldStateOptions {
    double progress = 1.0;        //!< 0=平面(展開状態) 〜 1=近似完成形
    std::vector<int> partNumbers; //!< 出力する部材番号(1始まり)。空=全部材。
    int columns = 96;             //!< レールのサンプル数(メッシュ解像度)
};

struct PartFoldStateResult {
    std::vector<std::string> railWireNames;
    std::vector<std::string> surfaceNames;
    std::vector<std::string> plateNames;
    std::vector<std::string> openingWireNames; //!< 実際に穴として付与できた開口
    std::vector<std::string> outlineWireNames; //!< 穴にできず輪郭線のみ追加した開口
};

//! 部材近似モデルの曲げ状態(レール・部材面・厚み付き板材・開口)を
//! target プロジェクトへ通常オブジェクトとして追加する。
//! target と source は同じプロジェクトでもよい(同一.kcd内への出力)。
//! 元板材の開口は各部材の面へ投影して実際の穴にする。部材境界をまたぐ
//! 開口は穴にできないため、輪郭線のみ追加して結果で報告する。
[[nodiscard]] PartFoldStateResult AddPartFoldStateModel(
    model::Project& target,
    const model::Project& source,
    const model::NamedPartModel& partModel,
    const PartFoldStateOptions& options,
    const std::string& namePrefix);

} // namespace kachakacha::io
