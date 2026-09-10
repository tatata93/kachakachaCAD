//! 帯メッシュの展開と、開口の帯ごとの切り出し(BandApproximation.h の後半)。
//! V1 の DevelopPartMesh / ClipClosedLoopIntoBands をそのまま移す。

#include "kachakacha/fabrication/BandApproximation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

//! 既知の2点 a,b(2D)と、その2点からの距離 ra,rb で三角形の第3点を置く。
//! orientation(+1/-1)は cross(b-a, p-a) の符号。三角形の辺長を厳密に保存する。
[[nodiscard]] Point2 PlaceThirdPoint(const Point2& a, const Point2& b, double ra,
    double rb, double orientation)
{
    const double dx = b.u - a.u;
    const double dy = b.v - a.v;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= 1.0e-12) {
        return {a.u + ra, a.v};
    }
    // 円の交点(標準形)。数値誤差で交わらない場合は clamp する。
    const double along = (ra * ra - rb * rb + d * d) / (2.0 * d);
    const double perpendicular = std::sqrt(std::max(0.0, ra * ra - along * along));
    const double ux = dx / d;
    const double uy = dy / d;
    const double sign = orientation >= 0.0 ? 1.0 : -1.0;
    return {a.u + ux * along - uy * perpendicular * sign,
        a.v + uy * along + ux * perpendicular * sign};
}

[[nodiscard]] double Distance3(const Vector3& a, const Vector3& b)
{
    return (b - a).Length();
}

//! 帯 bottomRow → bottomRow+1 を、左端の素線を基準にクアッド行進で展開する。
[[nodiscard]] std::pair<std::vector<Point2>, std::vector<Point2>> DevelopOneBand(
    const BandMesh& mesh, int bottomRow)
{
    const auto& bottom3 = mesh.world[static_cast<std::size_t>(bottomRow)];
    const auto& top3 = mesh.world[static_cast<std::size_t>(bottomRow) + 1];
    std::vector<Point2> bottom2(static_cast<std::size_t>(mesh.columns));
    std::vector<Point2> top2(static_cast<std::size_t>(mesh.columns));
    bottom2[0] = {0.0, 0.0};
    top2[0] = {0.0, Distance3(bottom3[0], top3[0])};
    for (std::size_t column = 1; column < static_cast<std::size_t>(mesh.columns);
         ++column) {
        // 三角形 (B_{c-1}, T_{c-1}, B_c): 下辺と対角線を保存。
        bottom2[column] = PlaceThirdPoint(bottom2[column - 1], top2[column - 1],
            Distance3(bottom3[column - 1], bottom3[column]),
            Distance3(top3[column - 1], bottom3[column]), -1.0);
        // 三角形 (T_{c-1}, B_c, T_c): 上辺と素線を保存。
        top2[column] = PlaceThirdPoint(top2[column - 1], bottom2[column],
            Distance3(top3[column - 1], top3[column]),
            Distance3(bottom3[column], top3[column]), +1.0);
    }
    return {std::move(bottom2), std::move(top2)};
}

//! 2D の剛体合わせ(回転+平行移動、鏡映なし)。from を to へ写す変換を apply へ掛ける。
void RigidAlign2(const std::vector<Point2>& from, const std::vector<Point2>& to,
    std::vector<Point2>& apply)
{
    double fromU = 0.0, fromV = 0.0, toU = 0.0, toV = 0.0;
    const double count = static_cast<double>(from.size());
    for (std::size_t index = 0; index < from.size(); ++index) {
        fromU += from[index].u;
        fromV += from[index].v;
        toU += to[index].u;
        toV += to[index].v;
    }
    fromU /= count;
    fromV /= count;
    toU /= count;
    toV /= count;
    double dotSum = 0.0, crossSum = 0.0;
    for (std::size_t index = 0; index < from.size(); ++index) {
        const double ax = from[index].u - fromU;
        const double ay = from[index].v - fromV;
        const double bx = to[index].u - toU;
        const double by = to[index].v - toV;
        dotSum += ax * bx + ay * by;
        crossSum += ax * by - ay * bx;
    }
    const double angle = std::atan2(crossSum, dotSum);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    for (Point2& point : apply) {
        const double px = point.u - fromU;
        const double py = point.v - fromV;
        point = {toU + px * c - py * s, toV + px * s + py * c};
    }
}

//! 内部レールの山谷: 前後の帯の面法線の二面角の符号。
void MeasureCreaseDirections(BandMesh& mesh)
{
    mesh.creaseDirections.assign(static_cast<std::size_t>(mesh.CreaseCount()), 0);
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        double accumulated = 0.0;
        const auto& previousRow = mesh.world[static_cast<std::size_t>(rail) - 1];
        const auto& row = mesh.world[static_cast<std::size_t>(rail)];
        const auto& nextRow = mesh.world[static_cast<std::size_t>(rail) + 1];
        for (std::size_t column = 1; column + 1 < static_cast<std::size_t>(mesh.columns);
             column += 4) {
            const Vector3 axis = Normalized(row[column + 1] - row[column - 1]);
            const Vector3 toPrevious = previousRow[column] - row[column];
            const Vector3 toNext = nextRow[column] - row[column];
            const Vector3 previousNormal = Normalized(Cross(axis, toPrevious));
            const Vector3 nextNormal = Normalized(Cross(toNext, axis));
            accumulated += Dot(Cross(previousNormal, nextNormal), axis);
        }
        if (accumulated > 1.0e-9) {
            mesh.creaseDirections[static_cast<std::size_t>(rail) - 1] = 1;
        } else if (accumulated < -1.0e-9) {
            mesh.creaseDirections[static_cast<std::size_t>(rail) - 1] = -1;
        }
    }
}

//! 片側の半平面で閉じた多角形を切る(Sutherland-Hodgman)。parameter も線形補間する。
void ClipAgainstLimit(std::vector<Vector3>& points, std::vector<double>& parameters,
    double limit, bool keepAbove)
{
    const std::size_t count = points.size();
    if (count < 3) {
        points.clear();
        parameters.clear();
        return;
    }
    const auto inside = [&](double parameter) {
        return keepAbove ? parameter >= limit : parameter <= limit;
    };
    std::vector<Vector3> outPoints;
    std::vector<double> outParameters;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1) % count;
        const bool insideNow = inside(parameters[index]);
        const bool insideNext = inside(parameters[next]);
        if (insideNow) {
            outPoints.push_back(points[index]);
            outParameters.push_back(parameters[index]);
        }
        if (insideNow != insideNext) {
            const double span = parameters[next] - parameters[index];
            const double ratio =
                std::abs(span) <= 1.0e-15 ? 0.0 : (limit - parameters[index]) / span;
            const double clamped = std::clamp(ratio, 0.0, 1.0);
            outPoints.push_back(points[index] + (points[next] - points[index]) * clamped);
            outParameters.push_back(limit);
        }
    }
    points = std::move(outPoints);
    parameters = std::move(outParameters);
}

//! 切り口は「帯の境目に沿った1本の直線」になり、曲がった面の上で弦になって
//! 窓の形が崩れる。長い辺を細かく割っておけば、面へ投影したときに縁に沿って曲がる。
void DensifyLoop(std::vector<Vector3>& points, std::vector<double>& parameters)
{
    const std::size_t count = points.size();
    if (count < 3) {
        return;
    }
    double perimeter = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        perimeter += (points[(index + 1) % count] - points[index]).Length();
    }
    if (perimeter <= 1.0e-9) {
        return;
    }
    const double maximumEdge = perimeter / 96.0;
    std::vector<Vector3> outPoints;
    std::vector<double> outParameters;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1) % count;
        outPoints.push_back(points[index]);
        outParameters.push_back(parameters[index]);
        const double length = (points[next] - points[index]).Length();
        if (length <= maximumEdge) {
            continue;
        }
        const int pieces = std::min(256, static_cast<int>(std::ceil(length / maximumEdge)));
        for (int step = 1; step < pieces; ++step) {
            const double ratio = static_cast<double>(step) / pieces;
            outPoints.push_back(points[index] + (points[next] - points[index]) * ratio);
            outParameters.push_back(
                parameters[index] + (parameters[next] - parameters[index]) * ratio);
        }
    }
    points = std::move(outPoints);
    parameters = std::move(outParameters);
}

//! 面積がほぼ0の切れ端(境目に沿った線分だけ)を捨てる。
[[nodiscard]] bool HasArea(const std::vector<Vector3>& points)
{
    if (points.size() < 3) {
        return false;
    }
    Vector3 twiceArea{};
    for (std::size_t index = 1; index + 1 < points.size(); ++index) {
        twiceArea = twiceArea
            + Cross(points[index] - points.front(), points[index + 1] - points.front());
    }
    return twiceArea.Length() * 0.5 > 1.0e-6;
}

} // namespace

Result<BandMesh> DevelopBandMesh(const SampledSurface& source, BandSplitAxis axis,
    const std::vector<double>& railParameters, int columns)
{
    using Out = Result<BandMesh>;
    if (railParameters.size() < 2) {
        return Out::Failure(MakeError(kBandBadBoundary, "展開にはレールが2本以上要ります。", {}));
    }
    if (columns < 2) {
        return Out::Failure(MakeError(kBandBadOptions, "展開の列数は2以上で指定してください。", {}));
    }
    for (std::size_t index = 1; index < railParameters.size(); ++index) {
        if (railParameters[index] <= railParameters[index - 1] + 1.0e-12) {
            return Out::Failure(MakeError(kBandBadBoundary,
                "レールのパラメータは昇順で指定してください。", {}));
        }
    }
    BandMesh mesh;
    mesh.rows = static_cast<int>(railParameters.size());
    mesh.columns = columns + 1;
    // 近似の実形状: 各レールは実面上の曲線、レール間は直線(ルールド)。
    mesh.world.resize(static_cast<std::size_t>(mesh.rows));
    for (int row = 0; row < mesh.rows; ++row) {
        auto& points = mesh.world[static_cast<std::size_t>(row)];
        points.reserve(static_cast<std::size_t>(mesh.columns));
        for (int column = 0; column < mesh.columns; ++column) {
            const double s = static_cast<double>(column) / columns;
            points.push_back(source.EvaluateSplit(axis,
                railParameters[static_cast<std::size_t>(row)], s));
        }
    }
    // 展開: 帯ごとに三角形単位の等長配置で厳密に展開し、共有レールで剛体合わせして積む。
    // 角度欠損(折り線まわりの誤差)は局所に留まり、蓄積しない。
    mesh.developed.assign(static_cast<std::size_t>(mesh.rows),
        std::vector<Point2>(static_cast<std::size_t>(mesh.columns), Point2{}));
    for (int band = 0; band + 1 < mesh.rows; ++band) {
        auto [bottom2, top2] = DevelopOneBand(mesh, band);
        if (band == 0) {
            mesh.developed[0] = bottom2;
            mesh.developed[1] = top2;
            continue;
        }
        std::vector<Point2> alignedTop = top2;
        RigidAlign2(bottom2, mesh.developed[static_cast<std::size_t>(band)], alignedTop);
        mesh.developed[static_cast<std::size_t>(band) + 1] = std::move(alignedTop);
    }
    MeasureCreaseDirections(mesh);
    return Out::Success(std::move(mesh));
}

std::vector<BandLoopPiece> ClipLoopIntoBands(const std::vector<Vector3>& points,
    const std::vector<double>& parameters, const std::vector<double>& boundaries)
{
    if (points.size() != parameters.size() || points.size() < 3 || boundaries.size() < 2) {
        return {};
    }
    std::vector<BandLoopPiece> pieces;
    for (std::size_t band = 0; band + 1 < boundaries.size(); ++band) {
        const double low = boundaries[band];
        const double high = boundaries[band + 1];
        if (high <= low) {
            continue;
        }
        std::vector<Vector3> clippedPoints = points;
        std::vector<double> clippedParameters = parameters;
        // 最初と最後の帯は、元の範囲の外へ出た分も取り込む(端は切らない)。
        if (band > 0) {
            ClipAgainstLimit(clippedPoints, clippedParameters, low, true);
        }
        if (band + 2 < boundaries.size()) {
            ClipAgainstLimit(clippedPoints, clippedParameters, high, false);
        }
        if (!HasArea(clippedPoints)) {
            continue;
        }
        DensifyLoop(clippedPoints, clippedParameters);
        pieces.push_back(BandLoopPiece{static_cast<int>(band), std::move(clippedPoints)});
    }
    return pieces;
}

} // namespace kachakacha::v2::fabrication
