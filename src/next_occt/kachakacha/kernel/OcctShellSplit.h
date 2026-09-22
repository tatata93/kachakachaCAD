#pragma once

//! 部品の形状編集: シェル(面を抜いて肉厚だけ残す)と分割(平面で 2 つに分ける)。matrix P-13。
//!
//! シェルの面は **押した面の上の点** で指す(文書にもそのまま残す)。開き直したら、同じ点を
//! 載せている面を選び直す(番号は作り直しで並びが変わりうる)。1 枚でも見つからなければ作らない。
//! 分割は平面(点と法線)で、両側をそれぞれ別の立体にする。平面が部品を通っていなければ断る。
//! 出来た形は BRepCheck と体積を確かめる。OCCT の型は外へ出さない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <vector>

namespace kachakacha::v2::kernel {

//! シェル・分割を作れなかった(厚みが大きすぎる など)。
inline constexpr const char* kShellSplitFailed = "KER-H001";
//! 指した面が部品に無い。
inline constexpr const char* kShellSplitFaceMissing = "KER-H002";
//! 出来た形が立体として壊れている・体積が変わらない。
inline constexpr const char* kShellSplitInvalid = "KER-H003";
//! カーネルが入っていない版。
inline constexpr const char* kShellSplitUnsupported = "KER-H004";
//! 分ける平面が部品を通っていない(片側に何も残らない)。
inline constexpr const char* kShellSplitNoCut = "KER-H005";

struct SolidFaceInfo {
    //! 押した点を面へ落とした点(面の上。記録と選び直しの鍵)。
    geometry::Vector3 point{};
    //! 面の縁の折れ線(下見に出す)。
    std::vector<std::vector<geometry::Vector3>> outline;
    double distanceMm = 0.0;
    //! その形の中での面の番号(同じ面かどうかを比べるためだけに使う。文書には残さない。
    //! 形が作り直されると変わりうる)。
    int faceIndex = -1;
};

struct ShellBuildResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    double previousVolumeMm3 = 0.0;
};

struct SplitBuildResult {
    //! 法線の側・反対の側。
    modeling::KernelShapeHandle positive;
    modeling::KernelShapeHandle negative;
    double positiveVolumeMm3 = 0.0;
    double negativeVolumeMm3 = 0.0;
    //! 片側がさらに離れた塊に分かれたときは 2 以上(U 字を横に切った など)。
    int positiveSolidCount = 0;
    int negativeSolidCount = 0;
};

//! 点に一番近い、立体の面。
[[nodiscard]] base::Result<SolidFaceInfo> NearestSolidFace(
    const modeling::KernelShapeHandle& solid, const geometry::Vector3& point);

//! 面の上の点で指した面の縁(下見)。見つからなければ断る。
[[nodiscard]] base::Result<SolidFaceInfo> SolidFaceAt(const modeling::KernelShapeHandle& solid,
    const geometry::Vector3& point, const geometry::GeometryTolerance& tolerance);

//! シェル: 指した面(1 枚以上)を抜き、内側へ厚みだけ残す。
[[nodiscard]] base::Result<ShellBuildResult> ShellSolid(const modeling::KernelShapeHandle& solid,
    const std::vector<geometry::Vector3>& facePoints, double thicknessMm,
    const geometry::GeometryTolerance& tolerance);

//! 分割: 平面(点と法線)で 2 つに分ける。どちらかに何も残らなければ断る。
[[nodiscard]] base::Result<SplitBuildResult> SplitSolidByPlane(
    const modeling::KernelShapeHandle& solid, const geometry::Vector3& planeOrigin,
    const geometry::Vector3& planeNormal, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
