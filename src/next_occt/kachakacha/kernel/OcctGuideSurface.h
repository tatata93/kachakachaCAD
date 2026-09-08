#pragma once

//! 形状ガイドを実際に作る層(WP-06)。
//!
//! ここへ来るのは、`AnalyzeGuideSurfaceRequest` を通った入力だけである。
//! 「作れるかどうか」の判断は core が済ませてあるので、
//! ここは作ることと、出来たものが本当に入力を通っているかを *測る* ことに専念する。
//!
//! 測って外れていたら、その面は捨てて断る。これが V1 の失敗の直接の対策である。
//! V1 は OCCT が返した「それらしい何か」をそのまま採用したため、
//! 断面を通っていない面が板取りまで流れていった。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ(AT-ARC-001)。
//! OCCT が投げる例外は、ここで受けて Diagnostic へ直す。外へ漏らさない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <cstddef>

namespace kachakacha::v2::kernel {

//! 面を作る層の診断コード。
inline constexpr const char* kSurfaceBuildFailed = "KER-S001";
//! 出来た面が入力の線を通っていない。geometry-contract §6 の SurfaceFitExceeded。
inline constexpr const char* kSurfaceMissesInput = "GEO-G008";
inline constexpr const char* kSurfaceSourceMissing = "KER-S003";
inline constexpr const char* kSurfaceOffsetImpossible = "KER-S004";
inline constexpr const char* kSurfaceUnsupportedMethod = "KER-S005";

//! 面を作る。作った実体はこの層の表に残り、handle で引ける。
//!
//! `sourceShape` は OffsetGuide のときだけ使う。元になる形状ガイドの handle。
//! それ以外の作り方では無視する。
[[nodiscard]] base::Result<modeling::GuideSurfaceResult> BuildGuideSurface(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance,
    modeling::KernelShapeHandle sourceShape = {});

//! 表に残した実体を捨てる。文書から消えた面を持ち続けないため。
//! 知らない handle を渡しても落ちない(false を返す)。
bool ReleaseShape(modeling::KernelShapeHandle handle);

//! その handle が表にいるか。
[[nodiscard]] bool HasShape(modeling::KernelShapeHandle handle);

//! いま表にいくつ残っているか。取りこぼしを試験で見るため。
[[nodiscard]] std::size_t CachedShapeCount();

//! 表を空にする。試験の後始末に使う。
void ClearShapeCache();

} // namespace kachakacha::v2::kernel
