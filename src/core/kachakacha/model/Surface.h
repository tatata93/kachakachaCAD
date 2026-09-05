#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <array>
#include <optional>
#include <vector>

namespace kachakacha::model {

enum class SurfaceKind {
    Planar,
    Ruled,
    Loft,
    Gordon,
    GuidedLoft,
    Patch, //!< 閉じた境界1本の穴埋め面(Coonsブレンド。おまかせ面の非平面フォールバック)
};

struct SurfaceProjection {
    geometry::Vector3 point;
    double u = 0.0;
    double v = 0.0;
    double distanceAlongDirection = 0.0;
};

struct SurfaceCurvature {
    double gaussian = 0.0;
    double mean = 0.0;
    double principalMinimum = 0.0;
    double principalMaximum = 0.0;
};

class Surface {
public:
    [[nodiscard]] static Surface Planar(Wire closedBoundary, double tolerance = 1.0e-7);
    [[nodiscard]] static Surface Ruled(Wire firstSection, Wire secondSection);
    [[nodiscard]] static Surface Loft(std::vector<Wire> sections);
    //! 断面群と外形ガイド線を両方とも厳密に通る面(Gordon曲面)。
    //! 各ガイドは全ての断面と intersectionTolerance 以内で交差していなければならない。
    //! 断面上では厳密に断面を、各ガイド上では厳密にガイドを通る。
    [[nodiscard]] static Surface Gordon(
        std::vector<Wire> sections,
        std::vector<Wire> guides,
        double intersectionTolerance = 0.01);
    [[nodiscard]] static Surface GuidedLoft(
        Wire firstGuide,
        Wire secondGuide,
        std::vector<Wire> sections,
        double connectionToleranceMillimeters = 0.05);
    //! 閉じた境界1本から穴埋め面を作る(Coonsブレンド)。平面でなくてもよい。
    //! 境界の折れが大きい4点を角として4辺に分け、辺どうしを双線形に混ぜる。
    [[nodiscard]] static Surface Patch(Wire closedBoundary);

    [[nodiscard]] SurfaceKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const Wire& FirstBoundary() const noexcept { return boundaries_.front(); }
    [[nodiscard]] const std::vector<Wire>& Boundaries() const noexcept { return boundaries_; }
    [[nodiscard]] const std::vector<Wire>& Guides() const noexcept { return guides_; }
    //! Gordon面で検出した断面×ガイド交点の最大ずれ(mm)。交点が厳密なら0。
    [[nodiscard]] double MaximumGuideGap() const noexcept { return maximumGuideGap_; }
    [[nodiscard]] const std::optional<WorkPlane>& PlanarWorkPlane() const noexcept { return planarWorkPlane_; }
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v) const;
    [[nodiscard]] geometry::Vector3 Normal(double u, double v) const;
    [[nodiscard]] SurfaceCurvature Curvature(double u, double v) const;
    [[nodiscard]] SurfaceProjection ProjectPointAlongDirection(
        geometry::Vector3 sourcePoint,
        geometry::Vector3 direction,
        double tolerance = 1.0e-7) const;
    [[nodiscard]] std::vector<SurfaceProjection> ProjectPointsAlongDirection(
        const std::vector<geometry::Vector3>& sourcePoints,
        geometry::Vector3 direction,
        double tolerance = 1.0e-7) const;
    [[nodiscard]] Wire ProjectWireAlongDirection(
        const Wire& sourceWire,
        geometry::Vector3 direction,
        int samples = 96,
        double tolerance = 1.0e-7) const;

private:
    Surface(
        SurfaceKind kind,
        std::vector<Wire> boundaries,
        std::optional<WorkPlane> planarWorkPlane,
        double minimumU,
        double minimumV,
        double maximumU,
        double maximumV,
        std::vector<Wire> guides = {},
        std::vector<double> sectionParameters = {},
        std::vector<double> firstGuideParameters = {},
        std::vector<double> secondGuideParameters = {},
        std::array<double, 4> patchCorners = {0.0, 0.25, 0.5, 0.75});

    [[nodiscard]] bool ContainsPlanarPoint(double u, double v, double tolerance) const;

    SurfaceKind kind_;
    std::vector<Wire> boundaries_;
    std::vector<Wire> guides_;
    std::vector<double> sectionParameters_;
    std::vector<double> firstGuideParameters_;
    std::vector<double> secondGuideParameters_;
    std::optional<WorkPlane> planarWorkPlane_;
    double minimumU_ = 0.0;
    double minimumV_ = 0.0;
    double maximumU_ = 0.0;
    double maximumV_ = 0.0;

    // Gordon面専用の事前計算データ。
    struct GordonGuideData {
        Wire guide;                        //!< 向きを揃えたガイド線
        double knotU = 0.0;                //!< このガイドが通る共通uパラメータ
        std::vector<double> sectionU;      //!< 断面iとの交点の断面側パラメータ u_ij
        std::vector<double> guideT;        //!< 断面iとの交点のガイド側パラメータ t_ij
    };
    std::vector<GordonGuideData> gordonGuides_;
    double maximumGuideGap_ = 0.0;

    //! パッチ面の4角の境界ワイヤ上パラメータ(周回順)。辺は境界ワイヤを
    //! そのまま評価する(サンプル折れ線ではなく元の線を必ず通る)。
    //! 辺0: 角0→角1(v=0, u昇順) / 辺1: 角1→角2(u=1, v昇順) /
    //! 辺2: 角2→角3(v=1, u降順) / 辺3: 角3→角0(u=0, v降順)。
    std::array<double, 4> patchCorners_ = {0.0, 0.25, 0.5, 0.75};

    [[nodiscard]] geometry::Vector3 EvaluateGordon(double u, double v) const;
    [[nodiscard]] geometry::Vector3 EvaluatePatch(double u, double v) const;
};

} // namespace kachakacha::model
