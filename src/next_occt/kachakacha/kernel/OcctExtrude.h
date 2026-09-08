#pragma once

//! 押し出しを実際に行う層(WP-07)。
//!
//! core (`AnalyzeExtrudeRequest`) が「押し出してよい」と判断し、
//! 「こういう形になるはず」まで出してある。ここはそれを作り、
//! 出来たものが予測どおりかを確かめる。合わなければ捨てて断る。
//!
//! これが V1 との決定的な違いである。V1 は OCCT が返した solid を
//! 無検査で採用したので、穴が多角形へ化けても、断面を通っていなくても、
//! そのまま板取りまで流れた。
//!
//! OCCT の型を外へ出さない。実体はこの層の表に置き、番号だけ返す(AT-ARC-001)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <string>
#include <vector>

namespace kachakacha::v2::kernel {

inline constexpr const char* kExtrudeBuildFailed = "KER-E001";
inline constexpr const char* kExtrudeMismatch = "KER-E002";
inline constexpr const char* kExtrudeTargetMissing = "KER-E003";
inline constexpr const char* kExtrudeBooleanTargetMissing = "KER-E004";
inline constexpr const char* kExtrudeUnsupported = "KER-E005";

//! 押し出しで出来た1つの部品。
struct ExtrudedPart {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    std::size_t faceCount = 0;
    //! 面の意味的キー。OCCT の面番号は保存しない。
    std::vector<std::string> faceKeys;
};

struct ExtrudeBuildResult {
    std::vector<ExtrudedPart> parts;
    //! 押し出し先の輪郭ワイヤー。曲線の種類を保つ。
    std::vector<std::vector<geometry::CurveSegment>> endProfileWires;
    //! 側面の境界ワイヤー。
    std::vector<std::vector<geometry::CurveSegment>> sideBoundaryWires;
    double totalVolumeMm3 = 0.0;
    std::size_t totalFaceCount = 0;
};

//! 押し出す。
//!
//! `booleanTarget` は足す/引くときの相手。NewPart のときは使わない。
[[nodiscard]] base::Result<ExtrudeBuildResult> BuildExtrude(
    const modeling::ExtrudeRequest& request,
    const modeling::ExtrudeAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance,
    modeling::KernelShapeHandle booleanTarget = {});

//! 形の体積と面の数。突き合わせ用。
[[nodiscard]] base::Result<double> ShapeVolume(modeling::KernelShapeHandle handle);

} // namespace kachakacha::v2::kernel
