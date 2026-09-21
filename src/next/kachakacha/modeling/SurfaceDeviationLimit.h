#pragma once

//! 出来た面が、指定した線からどれだけ外れてよいか(CODEX_REVIEW_REQUIRED の答え)。
//!
//! これまでの許容は `max(modelLinearMm * 10, 1e-4)` = **0.0001mm** 固定だった。
//! 考え方(作ったものを測り、通っていなければ捨てる)は正しい。V1 は測らずに出していた。
//! しかし値が数値誤差の桁である。
//!
//! **作り方によって、通ることを約束できるかどうかが違う。**
//!
//!   通す作り方  … 平面・ルールド・ロフト・回転体。
//!                 指定した線をそのまま通る面を張る。外れたら本当に壊れている。
//!   近づける作り方 … 案内付きロフト(`MakePipeShell`)・曲線網/境界埋め(`MakeFilling`)。
//!                 **作りからして近似** である。線を通ることは保証されない。
//!
//! 近づける作り方に 0.0001mm を求めるのは、その作り方が約束していないことを
//! 求めることである。HO の前頭部で実際に測ると 0.19mm 外れて断られ、
//! 命令の一覧にはあるのに **どんな形でも作れない** ことになっていた。
//! 「あるのに使えない」は、いちばん困る形である。
//!
//! では近づける作り方は、どこまで外れてよいか。
//! **後の工程が許している大きさ**を基準にする。この面は最終物ではない。
//! 板材の曲げ近似(`fabrication/BandApproximation.h`)は、既定で
//! 面から 0.25mm 外れることを許している。その次の工程が 0.25mm 許しているのに、
//! 手前の面に 0.0001mm を求めるのは、2500 倍ちぐはぐである。
//!
//! そこで、近づける作り方の限度を 0.25mm とする。
//! **そして、外れた量は必ず言う。**黙って通さない(できないことを、できたことにしない)。

#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <string>

namespace kachakacha::v2::modeling {

//! その作り方は、指定した線を通ることを約束できるか。
enum class SurfaceFidelity {
    //! 通る。外れたら壊れている。
    Interpolating,
    //! 近づけるだけ。外れることが作りに含まれている。
    Approximating,
};

[[nodiscard]] SurfaceFidelity FidelityOf(GuideSurfaceMethod method) noexcept;

//! 入力まで見たときの約束。同じロフトでも、ガイドや中心線があれば近づける作り方になり、
//! 四辺面に内側の通る線があれば近似拘束になる。**作り方の名前だけで決めない。**
[[nodiscard]] SurfaceFidelity FidelityOf(const GuideSurfaceRequest& request) noexcept;

//! 入力まで見たときの外れてよい最大(mm)。核はこちらを使う。
[[nodiscard]] double SurfaceDeviationLimitMm(const GuideSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance) noexcept;

//! 入力まで見たときの、外れの言い方。
[[nodiscard]] std::string SurfaceDeviationNoteJa(const GuideSurfaceRequest& request,
    double deviationMm, const geometry::GeometryTolerance& tolerance);

//! 板材の曲げ近似が、既定で面から外れてよい量(mm)。
//! `fabrication/BandApproximation.h` の `maximumDeviationMm` と同じ数である。
//! 2か所に別の数を書くと、どちらが本当の基準か分からなくなる。
inline constexpr double kFabricationDeviationMm = 0.25;

//! その作り方で、外れてよい最大(mm)。これを超えたら作らせない。
[[nodiscard]] double SurfaceDeviationLimitMm(GuideSurfaceMethod method,
    const geometry::GeometryTolerance& tolerance) noexcept;

//! その外れ方を、人に言うべきか。
//! 数値誤差の桁は言わない(毎回出ると読まなくなる)。
[[nodiscard]] bool SurfaceDeviationIsWorthSaying(GuideSurfaceMethod method,
    double deviationMm, const geometry::GeometryTolerance& tolerance) noexcept;

//! 「4. 状態」と帯に出す言い方。外れを言わないときは空。
[[nodiscard]] std::string SurfaceDeviationNoteJa(GuideSurfaceMethod method,
    double deviationMm, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::modeling
