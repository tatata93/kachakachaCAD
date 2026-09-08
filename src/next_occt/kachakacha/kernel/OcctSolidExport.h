#pragma once

//! 部品を STEP と STL へ出す層(WP-11、AT-EXP-010 / 011)。
//!
//! 大事な約束が2つある。
//!   1. 出せない形は出さない。開いた殻、体積0、自己交差は断る。
//!   2. 断ったときに0バイトのファイルを残さない。
//!      V1 は書き始めてから失敗したので、開けない空ファイルが残った。
//!      ここでは中身を全部作ってから、原子的に置く。
//!
//! STEP と STL は同じ形から出す。別々に近似して食い違わせない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <string>
#include <vector>

namespace kachakacha::v2::kernel {

inline constexpr const char* kExportOpenShell = "EXP-010";
inline constexpr const char* kExportZeroVolume = "EXP-011";
inline constexpr const char* kExportSelfIntersecting = "EXP-012";
inline constexpr const char* kExportFailed = "EXP-013";
inline constexpr const char* kExportUnsupported = "EXP-014";

//! 出す前の検査。ここを通らない形は書き出さない。
struct SolidExportCheck {
    bool closed = false;
    bool positiveVolume = false;
    bool selfIntersectionFree = false;
    double volumeMm3 = 0.0;
    double boundingDiagonalMm = 0.0;
    [[nodiscard]] bool Ok() const noexcept
    {
        return closed && positiveVolume && selfIntersectionFree;
    }
};

[[nodiscard]] base::Result<SolidExportCheck> CheckSolidForExport(
    modeling::KernelShapeHandle handle, double toleranceMm);

//! STEP の中身を作る。ファイルには書かない。
//! 書くのは呼び出し側が io::WriteFileAtomically で行う。
[[nodiscard]] base::Result<std::string> BuildStepText(
    modeling::KernelShapeHandle handle, double toleranceMm);

//! 二進 STL の中身を作る。
[[nodiscard]] base::Result<std::string> BuildBinaryStl(
    modeling::KernelShapeHandle handle, double deflectionMm);

//! 三角形にしたときの体積と外接箱。STEP と食い違っていないかを見る。
struct MeshMeasure {
    double volumeMm3 = 0.0;
    double boundingDiagonalMm = 0.0;
    std::size_t triangleCount = 0;
};

[[nodiscard]] base::Result<MeshMeasure> MeasureMesh(modeling::KernelShapeHandle handle,
    double deflectionMm);

// ---- 選んだ部材だけを出す(AT-FAB-012) ----

//! 選んだ形をまとめて1つの出力にする。
//!
//! 10部材のうち2つを選んだら、出て来るのは2つの塊だけである。
//! 見えていない別の部材が混ざってはならない。混ざると、切り出したあとで
//! 「頼んでいない板が1枚多い」ことになり、材料も時間も無駄になる。
struct SelectedExport {
    //! 中身の塊の数。選んだ数と一致していなければならない。
    std::size_t componentCount = 0;
    double totalVolumeMm3 = 0.0;
    //! 出力の中身。
    std::string content;
};

//! 選んだ形だけを STEP へ。
[[nodiscard]] base::Result<SelectedExport> BuildStepForSelection(
    const std::vector<modeling::KernelShapeHandle>& selected, double toleranceMm);

//! 選んだ形だけを二進 STL へ。
[[nodiscard]] base::Result<SelectedExport> BuildBinaryStlForSelection(
    const std::vector<modeling::KernelShapeHandle>& selected, double deflectionMm);

//! いくつの塊が入っているかを数える。出したものを読み返して確かめるのに使う。
[[nodiscard]] base::Result<std::size_t> CountSolidComponents(
    const std::vector<modeling::KernelShapeHandle>& selected);

} // namespace kachakacha::v2::kernel
