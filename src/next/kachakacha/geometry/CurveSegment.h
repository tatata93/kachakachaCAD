#pragma once

//! 曲線Segment。geometry-contract §2 の共通契約を、種類を保ったまま実装する。
//!
//! 大事な約束: 読み込めない種類を近似Polylineへ黙って変換しない。
//! 円弧は中心・法線・基準方向・半径・開始角・掃引角を持ち続ける。
//! B-splineは次数・制御点・ノットを持ち続ける。画面用の折れ線を正本へ戻さない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Vector3.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kachakacha::v2::geometry {

enum class CurveKind {
    Line,
    CircularArc,
    Circle,
    CubicBezier,
    CubicBSpline,
};

[[nodiscard]] constexpr std::string_view CurveKindName(CurveKind kind) noexcept
{
    switch (kind) {
    case CurveKind::Line:         return "Line";
    case CurveKind::CircularArc:  return "CircularArc";
    case CurveKind::Circle:       return "Circle";
    case CurveKind::CubicBezier:  return "CubicBezier";
    case CurveKind::CubicBSpline: return "CubicBSpline";
    }
    return "Unknown";
}

struct ClosestPointResult {
    double parameter = 0.0;
    Vector3 point{};
    double distance = 0.0;
};

class CurveSegment;

struct SplitResult {
    std::shared_ptr<CurveSegment> first;
    std::shared_ptr<CurveSegment> second;
};

//! 曲線1本。t は常に [0,1]。
class CurveSegment {
public:
    // --- 作り方。作れない入力は診断つきで断る ---
    [[nodiscard]] static base::Result<CurveSegment> MakeLine(Vector3 start, Vector3 end);

    //! 中心・法線・基準方向・半径・開始角・掃引角で持つ。掃引角0は作らない。
    [[nodiscard]] static base::Result<CurveSegment> MakeCircularArc(Vector3 center,
        Vector3 normal, Vector3 referenceDirection, double radius, double startAngleRad,
        double sweepAngleRad);

    [[nodiscard]] static base::Result<CurveSegment> MakeCircle(Vector3 center,
        Vector3 normal, Vector3 referenceDirection, double radius);

    [[nodiscard]] static base::Result<CurveSegment> MakeCubicBezier(
        std::vector<Vector3> controlPoints);

    //! 一様3次B-spline。制御点4点以上。
    [[nodiscard]] static base::Result<CurveSegment> MakeCubicBSpline(
        std::vector<Vector3> controlPoints);

    // --- 共通契約 ---
    [[nodiscard]] CurveKind Kind() const noexcept { return kind_; }

    //! 範囲外の t は黙ってclampしない。契約違反として診断を返す版も用意する。
    [[nodiscard]] Vector3 Evaluate(double t) const;
    [[nodiscard]] base::Result<Vector3> EvaluateChecked(double t) const;
    [[nodiscard]] Vector3 FirstDerivative(double t) const;
    [[nodiscard]] Vector3 SecondDerivative(double t) const;
    [[nodiscard]] double ArcLength(double t0, double t1, double tolerance) const;
    [[nodiscard]] double TotalLength(double tolerance) const
    {
        return ArcLength(0.0, 1.0, tolerance);
    }
    [[nodiscard]] Bounds3 Bounds(double tolerance) const;
    [[nodiscard]] base::Result<SplitResult> Split(double t) const;
    [[nodiscard]] ClosestPointResult ClosestPoint(const Vector3& point) const;

    [[nodiscard]] Vector3 StartPoint() const { return Evaluate(0.0); }
    [[nodiscard]] Vector3 EndPoint() const { return Evaluate(1.0); }
    [[nodiscard]] bool IsClosed(double tolerance) const;

    // --- 種類ごとの保持データ。近似で失わない ---
    [[nodiscard]] const std::vector<Vector3>& ControlPoints() const noexcept
    {
        return controlPoints_;
    }
    [[nodiscard]] Vector3 Center() const noexcept { return center_; }
    [[nodiscard]] Vector3 Normal() const noexcept { return normal_; }
    [[nodiscard]] Vector3 ReferenceDirection() const noexcept { return reference_; }
    [[nodiscard]] double Radius() const noexcept { return radius_; }
    [[nodiscard]] double StartAngleRad() const noexcept { return startAngle_; }
    [[nodiscard]] double SweepAngleRad() const noexcept { return sweepAngle_; }

private:
    CurveSegment() = default;

    [[nodiscard]] Vector3 EvaluateArc(double angleRad) const;

    CurveKind kind_ = CurveKind::Line;
    std::vector<Vector3> controlPoints_;
    Vector3 center_{};
    Vector3 normal_{0.0, 0.0, 1.0};
    Vector3 reference_{1.0, 0.0, 0.0};
    double radius_ = 0.0;
    double startAngle_ = 0.0;
    double sweepAngle_ = 0.0;
};

} // namespace kachakacha::v2::geometry
