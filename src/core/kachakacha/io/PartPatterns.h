#pragma once

#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/model/Project.h"

#include <vector>

namespace kachakacha::io {

//! 部材近似モデルの部材(または隣接する複数部材)の型紙を作る(ADR 0019)。
//! partNumbers は1始まりの部材番号。複数指定する場合は隣接した連番であること
//! (一枚から再現できる「つながった1つの展開図」= 範囲の合併として展開する)。
[[nodiscard]] PlateFlatPattern BuildPartPattern(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const std::vector<int>& partNumbers,
    PlateFlatPatternOptions options = {});

//! 全部材の型紙を部材番号順に作る。
[[nodiscard]] std::vector<PlateFlatPattern> BuildAllPartPatterns(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    PlateFlatPatternOptions options = {});

} // namespace kachakacha::io
