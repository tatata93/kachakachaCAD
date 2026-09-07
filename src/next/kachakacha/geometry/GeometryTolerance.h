#pragma once

//! 許容差の唯一の供給元。機能ごとのハードコードを禁止する(geometry-contract §1)。
//!
//! modelLinearMm を「文書の対角長から毎回計算する」と、無関係な遠方に点を1つ足しただけで
//! モデル全体の許容差が変わり、既存の接続が不正になる。
//! そのため、文書を作った時点で1度決めて保存し、変更は明示コマンドだけとする
//! (pre-implementation-fixes.md 第3節 矛盾3の決定)。

#include <algorithm>

namespace kachakacha::v2::geometry {

struct GeometryTolerance {
    double numericEpsilon = 1.0e-12;
    double modelLinearMm = 1.0e-6;
    double modelAngularRad = 1.0e-9;
    double interactiveJoinMm = 0.01;
    double displayPickPx = 8.0;
    double candidateMenuPx = 14.0;

    //! 文書の対角長から modelLinearMm を1度だけ決める。
    //! clamp(max(1e-6, diagonal * 1e-9), 1e-6, 1e-3)
    [[nodiscard]] static double ResolveModelLinearMm(double modelDiagonalMm) noexcept
    {
        if (!(modelDiagonalMm > 0.0)) {
            return 1.0e-6;
        }
        const double scaled = modelDiagonalMm * 1.0e-9;
        return std::clamp(std::max(1.0e-6, scaled), 1.0e-6, 1.0e-3);
    }

    //! 既定値。新しい文書はここから始める。
    [[nodiscard]] static GeometryTolerance Default() noexcept { return GeometryTolerance{}; }

    //! 対角長を与えて作る。作成時に1度だけ呼ぶ。
    [[nodiscard]] static GeometryTolerance ForModelDiagonal(double modelDiagonalMm) noexcept
    {
        GeometryTolerance tolerance;
        tolerance.modelLinearMm = ResolveModelLinearMm(modelDiagonalMm);
        return tolerance;
    }

    //! interactiveJoinMm はUIで 0.001〜0.1 まで変えられる。範囲外は受け付けない。
    [[nodiscard]] static bool IsValidInteractiveJoinMm(double value) noexcept
    {
        return value >= 0.001 && value <= 0.1;
    }
};

} // namespace kachakacha::v2::geometry
