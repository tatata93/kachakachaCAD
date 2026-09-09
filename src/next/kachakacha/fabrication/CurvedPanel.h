#pragma once

//! 曲がった面を型紙にする(fabrication-contract §4、§9、AT-FAB-001/002)。
//!
//! 平らな面は PlanarPanel がそのまま型紙にする。ここは曲がった面の側である。
//!
//! 約束は1つ。**伸縮させない。**
//! 展開図の上での長さは、立体の上での長さと一致していなければならない。
//! 一致させられない面は「展開できない」と言う。**近い形へ均して成功にしない。**
//!
//! 球はここで必ず引っかかる。球は伸ばさずには平らにできない。
//! 引っかからずに通ってしまうほうが困る。切ってから合わないことに、
//! 材料を使い切ったあとで気づくことになる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/fabrication/SurfacePatch.h"

#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 伸ばさずには平らにできない。
inline constexpr const char* kCurvedPanelNotDevelopable = "FAB-P005";
//! 標本が足りない、または形になっていない。
inline constexpr const char* kCurvedPanelBadSamples = "FAB-P006";

//! 展開した結果と、その確からしさ。
struct CurvedPanelResult {
    PatternPanel panel;
    //! 立体の上と展開図の上で、長さがどれだけ違うか(相対値)。
    double lengthErrorRelative = 0.0;
    //! 平らにしたときに残るずれ(mm)。
    double distortionMm = 0.0;
};

//! 面の標本を展開して、型紙の部材にする。
//!
//! **標本は行も列も3つ以上要る。** 2つしかないと内側の点が無く、
//! 角欠損の検査が何も見ずに通ってしまう。通ったことにならない検査を
//! 通すくらいなら、確かめられないと言うほうがよい。
//!
//! `targetMaxDeviationMm` は「どこまでのずれなら許すか」である。
//! これを超えるずれが残るなら、展開できたことにしない。
[[nodiscard]] base::Result<CurvedPanelResult> BuildCurvedPanel(const std::string& panelId,
    const SurfacePatchSamples& samples, double targetMaxDeviationMm);


} // namespace kachakacha::v2::fabrication
