#pragma once

#include "kachakacha/model/Plate.h"
#include "kachakacha/model/Wire.h"

#include <string>
#include <vector>

namespace kachakacha::model {

//! 部材近似モデル(docs/surface-unfolding-spec.md, ADR 0019)。
//! 完成品(の板材)を「1部材=基本1軸曲げ」の帯へ近似し直すためのレシピと計算結果。

enum class PartSplitAxis {
    U, //!< uパラメータ方向に境界を置く(帯はvに沿って並ぶ)
    V, //!< vパラメータ方向に境界を置く
};

struct PartApproximationOptions {
    PartSplitAxis splitAxis = PartSplitAxis::V;
    //! true なら公差から自動分割。false なら manualBoundaryParameters を境界に使う。
    bool automaticBoundaries = true;
    //! 1軸曲げ近似からの許容偏差(mm)。自動分割の基準。
    double maximumDeviationMillimeters = 0.25;
    //! 自動分割の上限部材数。超える場合は公差より部材数を優先する。
    int maximumPartCount = 12;
    //! 部材の最小実幅(mm)。これ未満の細い帯は作らない。
    double minimumPartWidthMillimeters = 4.0;
    //! 手動境界(分割軸パラメータ、0..1、開区間)。automaticBoundaries=false のとき使用。
    std::vector<double> manualBoundaryParameters;
};

struct ApproximatedPart {
    int number = 1;                    //!< 型紙・一覧に出す部材番号(1始まり)
    double minimumParameter = 0.0;     //!< 分割軸方向の範囲(板材ローカル 0..1)
    double maximumParameter = 1.0;
    double widthMillimeters = 0.0;     //!< 分割軸方向の実幅(平均)
    double estimatedDeviationMillimeters = 0.0; //!< 1軸曲げ近似からの推定偏差(最大)
    PlateDevelopability classification = PlateDevelopability::Developable;
};

struct PartApproximationResult {
    std::vector<ApproximatedPart> parts;
    double maximumDeviationMillimeters = 0.0; //!< 全部材の推定偏差の最大
    bool reachedRequestedTolerance = true;    //!< 全部材が許容偏差以下か
};

//! 板材(の厚み中央面)を部材へ近似分割する。
[[nodiscard]] PartApproximationResult ApproximatePlateParts(
    const Plate& plate,
    const PartApproximationOptions& options);

//! 部材境界(分割軸パラメータ=一定の線)を板厚中央面上のポリラインとして作る。
[[nodiscard]] Wire BuildPartBoundaryWire(
    const Plate& plate,
    PartSplitAxis splitAxis,
    double parameter,
    int samples = 64);

} // namespace kachakacha::model
