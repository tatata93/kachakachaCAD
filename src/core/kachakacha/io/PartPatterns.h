#pragma once

#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Project.h"

#include <vector>

namespace kachakacha::io {

//! 部材近似モデルの型紙(ADR 0019)。近似の実形状(角ばった帯メッシュ)を
//! 三角形単位で辺長を厳密に保存して展開するため、切り出して曲げれば
//! 近似モデルどおりに組み上がる。隣接する複数部材を指定した場合は
//! 一枚のつながった型紙になり、部材境界が折り線(山/谷)として入る。
struct PartPatternResult {
    PlateFlatPattern pattern;
    //! スライダー確認(平面↔折り曲げ)用のメッシュ。
    model::PartMeshDevelopment mesh;
};

[[nodiscard]] PartPatternResult BuildPartPatternWithPreview(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const std::vector<int>& partNumbers,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateFlatPattern BuildPartPattern(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const std::vector<int>& partNumbers,
    PlateFlatPatternOptions options = {});

//! 全部材の型紙を部材番号順に作る。
[[nodiscard]] std::vector<PartPatternResult> BuildAllPartPatternsWithPreview(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    PlateFlatPatternOptions options = {});

[[nodiscard]] std::vector<PlateFlatPattern> BuildAllPartPatterns(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    PlateFlatPatternOptions options = {});

} // namespace kachakacha::io
