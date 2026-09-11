#pragma once

//! 近似モデルの作り方(定義)から、部材と曲げ状態を作る。
//!
//! 近似の結果は文書に持たない。持つのは作り方(CreateFabricationModelDefinition)で、
//! 作るときも、開き直すときも、**同じこの関数** で作り直す。道を分けると、
//! 開いたときだけ違う部材が出来る。
//!
//! 方式は2つ。どちらも残して選べるようにする。
//!   0 = V2 方式: 面を分類し、伸ばさずに平らにできる面だけを展開する。できなければ断る。
//!   1 = V1 方式: 面を帯へ近似し直す。二重曲面も切り、どれだけずれたかを報告する。
//!
//! 曲げ状態(masterPercent / creaseProgress / bandProgress)は V1 方式の帯メッシュで
//! 形になる。V2 方式は面が既に展開可能なので、曲げ状態は「平ら」と「完成形」の2つ。
//!
//! OCCT を使わない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/fabrication/BandApproximation.h"
#include "kachakacha/fabrication/BandFold.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kachakacha::v2::app {

//! 近似の方式。定義の `method` と同じ並び。
enum class FabricationMethod {
    ClassifyFaces = 0, //!< V2 方式
    BandApproximation = 1, //!< V1 方式
};

[[nodiscard]] std::string_view FabricationMethodNameJa(FabricationMethod method) noexcept;
[[nodiscard]] FabricationMethod FabricationMethodOf(
    const domain::CreateFabricationModelDefinition& definition) noexcept;

//! 元になるもの1つぶん。画面が持っている材料をここへ詰めて渡す。
struct FabricationSource {
    base::EntityId entityId;
    std::string name;
    //! 形状ガイドの面の標本。曲がった面はこれで近似する。
    std::optional<fabrication::SurfacePatchSamples> samples;
    //! 平らな部品の「平らな1枚」の輪郭。押し出しの端の輪郭。
    std::optional<std::vector<geometry::CurveSegment>> flatBoundary;
};

//! 開口・折り線にする線。どの部材のものかは、外周と同じ平面に載っているかで決める。
struct FabricationMarkings {
    std::vector<std::vector<geometry::CurveSegment>> openings;
    std::vector<std::vector<geometry::CurveSegment>> folds;
    //! 切れ目(V1 の plate_relief_cut)。開いた線。閉じていれば FAB-M005 で断る。
    //! いまは平らな部材にだけ入る(曲がった面の切れ目は未対応)。
    std::vector<std::vector<geometry::CurveSegment>> reliefCuts;
};

//! 作った結果。文書には入れない。画面が覚えて、開いたら作り直す。
struct FabricationEvaluation {
    FabricationMethod method = FabricationMethod::ClassifyFaces;
    //! 型紙に載せる部材。両方式に共通。
    std::vector<fabrication::PatternPanel> panels;
    //! V1 方式のとき。帯の切り方と、近似メッシュ(曲げ状態の形の元)。
    std::optional<fabrication::BandApproximationResult> bands;
    std::optional<fabrication::BandMesh> bandMesh;
    double maximumDeviationMm = 0.0;
    bool reachedTolerance = true;
    std::string summaryJa;
};

//! 定義から帯近似の決め方を組み立てる。許容偏差は定義の targetMaxDeviation。
[[nodiscard]] fabrication::BandApproximationOptions BandOptionsOf(
    const domain::CreateFabricationModelDefinition& definition);

//! 作る。断るときは理由を言う。
[[nodiscard]] base::Result<FabricationEvaluation> EvaluateFabrication(
    const domain::CreateFabricationModelDefinition& definition,
    const std::vector<FabricationSource>& sources, const FabricationMarkings& markings,
    double toleranceMm);

//! 帯メッシュから型紙の部材を作る。帯1つ = 部材1枚。
//! 外周は展開した下レール→上レール(逆順)の閉じた輪。折り線は内部レール。
[[nodiscard]] std::vector<fabrication::PatternPanel> PanelsFromBandMesh(
    const std::string& baseName, const fabrication::BandMesh& mesh,
    const std::vector<double>& creaseAnglesRad);

//! 定義の曲げ状態を、帯メッシュの折り線数・帯数に合わせて解く。
//! 個別値が空なら master から。長さが合わなければ master で埋め直す(断らない。
//! 帯数は再近似で変わりうるので、古い個別値を理由に開けなくしない)。
struct ResolvedFoldState {
    std::vector<double> creaseProgress;
    std::vector<double> bandProgress;
    double masterProgress = 1.0;
};

[[nodiscard]] ResolvedFoldState ResolveFoldState(
    const domain::CreateFabricationModelDefinition& definition,
    const fabrication::BandMesh& mesh);

//! いまの曲げ状態での帯の姿勢(下レール・上レールの点列、2×帯数)。画面に出す。
//! V2 方式(帯メッシュ無し)なら空を返す。
[[nodiscard]] std::vector<std::vector<geometry::Vector3>> FoldedRailsOf(
    const domain::CreateFabricationModelDefinition& definition,
    const FabricationEvaluation& evaluation, double liftMm);

//! 接続スコープの線1本を、近似の実形状へ寄せたもの。
struct AdaptedWire {
    std::string name;
    std::vector<geometry::Vector3> points; //!< 折れ線。元の線を標本化して寄せた点列
    bool closed = false;
    int snappedPoints = 0;                 //!< 寄せた点の数。0 なら1点も載っていない
};

//! 接続スコープの線を、近似メッシュの状態 state(完成形なら mesh.world)の上へ寄せる。
//! V1 の接続スコープ(合意13)そのもの。
//!
//! 線を折れ線として標本化し、メッシュに **載っている点だけ** を寄せる。
//! 離れている点は元のまま残す。寄せる許容は「近似の偏差 + 0.35mm」(V1 と同じ値)。
//! 元の線は変えない。寄せた結果は別の線(名前に「_接続」)として返す。
[[nodiscard]] std::vector<AdaptedWire> AdaptConnectionWires(const fabrication::BandMesh& mesh,
    const std::vector<std::vector<geometry::Vector3>>& state,
    const std::vector<std::pair<std::string, std::vector<geometry::CurveSegment>>>& wires,
    double snapToleranceMm, int samplesPerCurve = 24);

//! 曲げ状態の一文。「組立 42%(折り線 2 本のうち 1 本を個別指定)」など。
[[nodiscard]] std::string FoldStateSummaryJa(
    const domain::CreateFabricationModelDefinition& definition);

} // namespace kachakacha::v2::app
