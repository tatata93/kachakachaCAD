#pragma once

//! 任意の組立状態を固定する(fabrication-contract.md §11、AT-FAB-011 / 014)。
//!
//! 「30%開いた状態のワイヤーと部品がほしい」に応える層。
//!
//! 大事な決まりが2つある。
//!   1. ワイヤーと部品を両方作るときは、**同じ評価の束から**作る。
//!      別々に計算すると、部品の境界とワイヤーがわずかにずれる。
//!      V1 はそれをやっていたので、型紙と部品の縁が合わなかった。
//!   2. 状態が変わっても、対応する辺の長さは変わらない。
//!      変わるのは3次元での位置と向きだけである。
//!
//! ここは OCCT を使わない。部品を作るのに要る材料(輪郭・厚み・置き方)まで
//! を用意し、実際の立体はカーネル側が作る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/Assembly.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::CurveSegment;

//! 何を固定するか(§11)。
enum class FreezeOutput {
    WiresOnly,
    PartsOnly,
    Both,
};

[[nodiscard]] std::string_view FreezeOutputNameJa(FreezeOutput value) noexcept;

//! 固定して出来るワイヤーの種類。
enum class FrozenWireKind {
    PanelBoundary,
    FoldLine,
    ReliefCut,
    Opening,
};

[[nodiscard]] std::string_view FrozenWireKindNameJa(FrozenWireKind value) noexcept;

struct FrozenWire {
    std::string sourceId;
    FrozenWireKind kind = FrozenWireKind::PanelBoundary;
    //! 3次元の曲線。種類を保つ。折れ線へ落とさない。
    std::vector<CurveSegment> segments;
    //! この線の長さ。状態が変わっても変わらないことを確かめるのに使う。
    double lengthMm = 0.0;
};

//! 部品を作るのに要る材料。立体そのものはカーネルが作る。
struct PanelSolidRequest {
    std::string panelId;
    //! 3次元へ置いた輪郭。ワイヤーと同じ束から取る。
    std::vector<geometry::Vector3> outline;
    double thicknessMm = 0.2;
    ThicknessPlacement placement = ThicknessPlacement::Centered;
};

struct FreezeBundle {
    double percent = 0.0;
    FreezeOutput output = FreezeOutput::Both;
    std::vector<FrozenWire> wires;
    std::vector<PanelSolidRequest> parts;
    std::vector<base::Diagnostic> notes;
};

//! いまの状態を固定する。
//!
//! `output` が Both のとき、ワイヤーと部品は同じ評価の束から作られる。
//! 別々に計算しない。
[[nodiscard]] base::Result<FreezeBundle> FreezeAssemblyState(
    const std::vector<AssemblyPanel>& panels, const std::vector<AssemblyFold>& folds,
    const AssemblyState& state, FreezeOutput output,
    const FabricationSettings& settings);

//! 2つの状態で、対応する辺の長さが一致しているか(AT-FAB-011)。
struct FreezeComparison {
    double maximumLengthDifferenceMm = 0.0;
    std::string worstSourceId;
    bool lengthsMatch = true;
    //! 外接箱の対角。状態が違えば違う値になるはず。
    double firstDiagonalMm = 0.0;
    double secondDiagonalMm = 0.0;
    bool shapesDiffer = false;
};

[[nodiscard]] FreezeComparison CompareFrozenStates(const FreezeBundle& first,
    const FreezeBundle& second, double toleranceMm);

//! 部品の境界とワイヤーが一致しているか(AT-FAB-014)。
//! Both で作った束にだけ意味がある。
struct BoundaryAgreement {
    double maximumDeviationMm = 0.0;
    std::string worstPanelId;
    bool agrees = true;
};

[[nodiscard]] BoundaryAgreement CheckBoundaryAgreement(const FreezeBundle& bundle,
    double toleranceMm);

} // namespace kachakacha::v2::fabrication
