#include "kachakacha/fabrication/Unfold.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::fabrication {

using base::MakeError;
using base::Result;
using geometry::Normalized;

namespace {

constexpr const char* kBadStrip = "FAB-U001";
constexpr const char* kNotDevelopable = "FAB-U002";
constexpr const char* kWrongSurface = "FAB-U003";

[[nodiscard]] double Distance2(const Point2& a, const Point2& b)
{
    const double dx = a.u - b.u;
    const double dy = a.v - b.v;
    return std::sqrt(dx * dx + dy * dy);
}

//! 既に置いた2点 a, b から、そこへの距離が da, db になる点を作る。
//! 2つある解のうち、sign で選ぶ側を固定する。
[[nodiscard]] std::optional<Point2> PlaceByDistances(const Point2& a, const Point2& b,
    double da, double db, double sign)
{
    const double baseLength = Distance2(a, b);
    if (!(baseLength > 0.0)) {
        return std::nullopt;
    }
    const double x = (da * da - db * db + baseLength * baseLength) / (2.0 * baseLength);
    const double squared = da * da - x * x;
    const double y = squared > 0.0 ? std::sqrt(squared) : 0.0;
    const double ux = (b.u - a.u) / baseLength;
    const double uy = (b.v - a.v) / baseLength;
    // 法線方向は (-uy, ux)。
    return Point2{a.u + ux * x - uy * y * sign, a.v + uy * x + ux * y * sign};
}

} // namespace

Result<UnfoldedStrip> UnfoldStrip(const DevelopableStrip& strip, double targetMaxDeviationMm)
{
    if (!strip.Valid()) {
        return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
            "帯の形が正しくありません。",
            "母線の数が合っていないか、2本未満です。"));
    }
    for (std::size_t index = 0; index < strip.firstRail.size(); ++index) {
        if (!strip.firstRail[index].IsFinite() || !strip.secondRail[index].IsFinite()) {
            return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
                "帯に有限でない数が入っています。", std::to_string(index) + " 本目。"));
        }
    }
    const std::size_t count = strip.firstRail.size();
    UnfoldedStrip unfolded;
    unfolded.firstRail.resize(count);
    unfolded.secondRail.resize(count);

    // 最初の母線を、そのまま縦に置く。
    const double firstRuling = (strip.secondRail[0] - strip.firstRail[0]).Length();
    if (!(firstRuling > 0.0)) {
        return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
            "長さが0の母線があります。", "0 本目。"));
    }
    unfolded.firstRail[0] = Point2{0.0, 0.0};
    unfolded.secondRail[0] = Point2{0.0, firstRuling};

    for (std::size_t index = 0; index + 1 < count; ++index) {
        const Vector3& a0 = strip.firstRail[index];
        const Vector3& b0 = strip.secondRail[index];
        const Vector3& a1 = strip.firstRail[index + 1];
        const Vector3& b1 = strip.secondRail[index + 1];

        const double firstEdge = (a1 - a0).Length();     // 縁1に沿った長さ
        const double secondEdge = (b1 - b0).Length();    // 縁2に沿った長さ
        const double ruling = (b1 - a1).Length();        // 次の母線
        const double diagonal = (b0 - a1).Length();      // 対角線(こちらを保つ)
        const double otherDiagonal = (b1 - a0).Length(); // もう一方の対角線
        if (!(ruling > 0.0)) {
            return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
                "長さが0の母線があります。", std::to_string(index + 1) + " 本目。"));
        }

        // a1 を、a0 と b0 からの距離で置く。三角形 (a0, b0, a1) をそのまま倒す。
        const auto placedA = PlaceByDistances(unfolded.firstRail[index],
            unfolded.secondRail[index], firstEdge, diagonal, -1.0);
        if (!placedA.has_value()) {
            return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
                "帯を倒せません。", std::to_string(index) + " 番目の四辺形。"));
        }
        unfolded.firstRail[index + 1] = *placedA;
        // b1 を、a1 と b0 からの距離で置く。三角形 (a1, b0, b1) をそのまま倒す。
        // 基準の向きを (a1 -> b0) に取ると、1枚目と同じ側の取り方になり、
        // 四辺形が折り返らない。基準を逆に取ると帯が裏返る。
        const auto placedB = PlaceByDistances(unfolded.firstRail[index + 1],
            unfolded.secondRail[index], ruling, secondEdge, -1.0);
        if (!placedB.has_value()) {
            return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
                "帯を倒せません。", std::to_string(index) + " 番目の四辺形。"));
        }
        unfolded.secondRail[index + 1] = *placedB;

        // ここまでで、4辺と対角線1本は厳密に保たれている。
        // 残るもう1本の対角線のずれが、その四辺形の「平面から外れている量」。
        const double flatOther =
            Distance2(unfolded.secondRail[index + 1], unfolded.firstRail[index]);
        unfolded.maximumPlanarityErrorMm = std::max(unfolded.maximumPlanarityErrorMm,
            std::abs(flatOther - otherDiagonal));

        const auto relative = [](double flat, double spatial) {
            return spatial > 0.0 ? std::abs(flat - spatial) / spatial : 0.0;
        };
        unfolded.maximumLengthErrorRelative = std::max({unfolded.maximumLengthErrorRelative,
            relative(Distance2(unfolded.firstRail[index], unfolded.firstRail[index + 1]),
                firstEdge),
            relative(Distance2(unfolded.secondRail[index], unfolded.secondRail[index + 1]),
                secondEdge),
            relative(Distance2(unfolded.firstRail[index + 1], unfolded.secondRail[index + 1]),
                ruling)});
    }

    const double limit = targetMaxDeviationMm > 0.0 ? targetMaxDeviationMm : 1.0e-3;
    if (unfolded.maximumPlanarityErrorMm > limit) {
        // 展開できない面を、伸ばして平らにしない。断る。
        return Result<UnfoldedStrip>::Failure(MakeError(kNotDevelopable,
            "この面は伸ばさずに展開できません。",
            "平面から外れている量が最大 "
                + std::to_string(unfolded.maximumPlanarityErrorMm) + " mm(目標は "
                + std::to_string(limit)
                + " mm)。切れ目を入れるか、分割してください。"));
    }
    return Result<UnfoldedStrip>::Success(std::move(unfolded));
}

DevelopabilityCheck CheckDevelopability(const SurfacePatchSamples& samples)
{
    DevelopabilityCheck check;
    if (!samples.Valid() || samples.rowCount < 3 || samples.columnCount < 3) {
        return check;
    }
    // 各四辺形を2枚の三角形へ割る。割り方は全体で同じにする。
    const auto angleAt = [](const Vector3& corner, const Vector3& a, const Vector3& b) {
        const Vector3 first = a - corner;
        const Vector3 second = b - corner;
        const double lengths = first.Length() * second.Length();
        if (!(lengths > 0.0)) {
            return 0.0;
        }
        const double cosine = std::clamp(Dot(first, second) / lengths, -1.0, 1.0);
        return std::acos(cosine);
    };
    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
    double representative = 0.0;
    {
        const Vector3 a = samples.At(0, 0);
        const Vector3 b = samples.At(samples.rowCount - 1, samples.columnCount - 1);
        representative = 0.5 * (b - a).Length();
    }
    for (std::size_t row = 1; row + 1 < samples.rowCount; ++row) {
        for (std::size_t column = 1; column + 1 < samples.columnCount; ++column) {
            const Vector3 center = samples.At(row, column);
            // まわりの6枚の三角形(四辺形を (r,c)-(r+1,c)-(r+1,c+1) と
            // (r,c)-(r+1,c+1)-(r,c+1) に割ったときの近傍)。
            const Vector3 up = samples.At(row - 1, column);
            const Vector3 down = samples.At(row + 1, column);
            const Vector3 left = samples.At(row, column - 1);
            const Vector3 right = samples.At(row, column + 1);
            const Vector3 upRight = samples.At(row - 1, column + 1);
            const Vector3 downLeft = samples.At(row + 1, column - 1);
            const double sum = angleAt(center, up, upRight) + angleAt(center, upRight, right)
                + angleAt(center, right, down) + angleAt(center, down, downLeft)
                + angleAt(center, downLeft, left) + angleAt(center, left, up);
            const double defect = std::abs(kTwoPi - sum);
            if (defect > check.maximumAngleDefectRad) {
                check.maximumAngleDefectRad = defect;
                check.worstRow = row;
                check.worstColumn = column;
            }
        }
    }
    // 角欠損 theta の点のまわりを平らにすると、距離 L のところで
    // おおよそ theta * L のずれが出る。
    check.estimatedDistortionMm = check.maximumAngleDefectRad * representative;
    check.valid = true;
    return check;
}

Result<UnfoldedStrip> UnfoldSamples(const SurfacePatchSamples& samples,
    double targetMaxDeviationMm)
{
    if (!samples.Valid()) {
        return Result<UnfoldedStrip>::Failure(MakeError(kBadStrip,
            "面の標本が壊れています。", {}));
    }
    // 縁だけを見て展開すると、中がふくらんでいる面を通してしまう。先に中を見る。
    const DevelopabilityCheck developability = CheckDevelopability(samples);
    const double limit = targetMaxDeviationMm > 0.0 ? targetMaxDeviationMm : 1.0e-3;
    if (developability.valid && developability.estimatedDistortionMm > limit) {
        return Result<UnfoldedStrip>::Failure(MakeError(kNotDevelopable,
            "この面は伸ばさずに展開できません。",
            "平らにしたときのずれが最大 "
                + std::to_string(developability.estimatedDistortionMm) + " mm(目標は "
                + std::to_string(limit) + " mm)。切れ目を入れるか、分割してください。"));
    }
    // 行を母線と見なす。1行目と最終行を縁にした帯として扱う。
    DevelopableStrip strip;
    for (std::size_t column = 0; column < samples.columnCount; ++column) {
        strip.firstRail.push_back(samples.At(0, column));
        strip.secondRail.push_back(samples.At(samples.rowCount - 1, column));
    }
    return UnfoldStrip(strip, targetMaxDeviationMm);
}

Result<std::vector<Point2>> UnfoldOnPlane(const AnalyticSurfaceInfo& surface,
    const std::vector<Vector3>& points, double toleranceMm)
{
    if (surface.kind != AnalyticSurfaceKind::Plane) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "平面ではありません。", {}));
    }
    const Vector3 normal = Normalized(surface.axis);
    if (!(normal.Length() > 0.0)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "平面の向きが決まっていません。", {}));
    }
    Vector3 u = surface.reference - normal * Dot(surface.reference, normal);
    u = Normalized(u);
    if (!(u.Length() > 0.0)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "平面の基準方向が法線と平行です。", {}));
    }
    const Vector3 v = Cross(normal, u);
    std::vector<Point2> flat;
    flat.reserve(points.size());
    for (const Vector3& point : points) {
        const Vector3 relative = point - surface.origin;
        if (std::abs(Dot(relative, normal)) > toleranceMm) {
            return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
                "平面から外れた点があります。",
                std::to_string(std::abs(Dot(relative, normal))) + " mm。"));
        }
        flat.push_back(Point2{Dot(relative, u), Dot(relative, v)});
    }
    return Result<std::vector<Point2>>::Success(std::move(flat));
}

Result<std::vector<Point2>> UnfoldOnCylinder(const AnalyticSurfaceInfo& surface,
    const std::vector<Vector3>& points, double toleranceMm)
{
    if (surface.kind != AnalyticSurfaceKind::Cylinder) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円筒ではありません。", {}));
    }
    if (!(surface.radiusMm > 0.0)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円筒の半径が正ではありません。", {}));
    }
    const Vector3 axis = Normalized(surface.axis);
    Vector3 reference = surface.reference - axis * Dot(surface.reference, axis);
    reference = Normalized(reference);
    if (!(axis.Length() > 0.0) || !(reference.Length() > 0.0)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円筒の軸か基準方向が決まっていません。", {}));
    }
    const Vector3 other = Cross(axis, reference);

    std::vector<Point2> flat;
    flat.reserve(points.size());
    double previousAngle = 0.0;
    bool first = true;
    double turns = 0.0;
    for (const Vector3& point : points) {
        const Vector3 relative = point - surface.origin;
        const double along = Dot(relative, axis);
        const Vector3 radial = relative - axis * along;
        const double radius = radial.Length();
        if (std::abs(radius - surface.radiusMm) > toleranceMm) {
            return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
                "円筒から外れた点があります。",
                "半径 " + std::to_string(radius) + " mm、面は "
                    + std::to_string(surface.radiusMm) + " mm。"));
        }
        double angle = std::atan2(Dot(radial, other), Dot(radial, reference));
        if (!first) {
            // 一周またぐところで飛ばないように、連続になるよう繋ぐ。
            const double difference = angle + turns * 2.0 * 3.14159265358979323846
                - previousAngle;
            if (difference > 3.14159265358979323846) {
                turns -= 1.0;
            } else if (difference < -3.14159265358979323846) {
                turns += 1.0;
            }
        }
        const double continuous = angle + turns * 2.0 * 3.14159265358979323846;
        previousAngle = continuous;
        first = false;
        // 弧長 = 半径 × 角度。これが厳密展開。
        flat.push_back(Point2{surface.radiusMm * continuous, along});
    }
    return Result<std::vector<Point2>>::Success(std::move(flat));
}

Result<std::vector<Point2>> UnfoldOnCone(const AnalyticSurfaceInfo& surface,
    const std::vector<Vector3>& points, double toleranceMm)
{
    if (surface.kind != AnalyticSurfaceKind::Cone) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円錐ではありません。", {}));
    }
    const double halfAngle = surface.halfAngleRad;
    if (!(halfAngle > 0.0) || !(halfAngle < 1.5707963267948966)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円錐の半頂角が正しくありません。", std::to_string(halfAngle) + " rad。"));
    }
    const Vector3 axis = Normalized(surface.axis);
    Vector3 reference = surface.reference - axis * Dot(surface.reference, axis);
    reference = Normalized(reference);
    if (!(axis.Length() > 0.0) || !(reference.Length() > 0.0)) {
        return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
            "円錐の軸か基準方向が決まっていません。", {}));
    }
    const Vector3 other = Cross(axis, reference);
    // 展開すると、頂点からの距離 s はそのまま、まわりの角度は sin(halfAngle) 倍になる。
    const double factor = std::sin(halfAngle);

    std::vector<Point2> flat;
    flat.reserve(points.size());
    double previousAngle = 0.0;
    bool first = true;
    double turns = 0.0;
    for (const Vector3& point : points) {
        const Vector3 relative = point - surface.origin;   // origin は頂点
        const double along = Dot(relative, axis);
        const Vector3 radial = relative - axis * along;
        const double radius = radial.Length();
        const double expected = std::abs(along) * std::tan(halfAngle);
        if (std::abs(radius - expected) > toleranceMm) {
            return Result<std::vector<Point2>>::Failure(MakeError(kWrongSurface,
                "円錐から外れた点があります。",
                "半径 " + std::to_string(radius) + " mm、面は "
                    + std::to_string(expected) + " mm。"));
        }
        double angle = radius > 0.0
            ? std::atan2(Dot(radial, other), Dot(radial, reference))
            : 0.0;
        if (!first) {
            const double difference = angle + turns * 2.0 * 3.14159265358979323846
                - previousAngle;
            if (difference > 3.14159265358979323846) {
                turns -= 1.0;
            } else if (difference < -3.14159265358979323846) {
                turns += 1.0;
            }
        }
        const double continuous = angle + turns * 2.0 * 3.14159265358979323846;
        previousAngle = continuous;
        first = false;
        const double slant = relative.Length();   // 頂点からの距離
        const double flatAngle = continuous * factor;
        flat.push_back(Point2{slant * std::cos(flatAngle), slant * std::sin(flatAngle)});
    }
    return Result<std::vector<Point2>>::Success(std::move(flat));
}

LengthPreservationCheck CheckLengthPreservation(const std::vector<Vector3>& spatial,
    const std::vector<Point2>& flat, double relativeTolerance)
{
    LengthPreservationCheck check;
    const std::size_t count = std::min(spatial.size(), flat.size());
    for (std::size_t index = 1; index < count; ++index) {
        const double before = (spatial[index] - spatial[index - 1]).Length();
        const double after = Distance2(flat[index], flat[index - 1]);
        if (!(before > 0.0)) {
            continue;
        }
        const double error = std::abs(after - before) / before;
        if (error > check.maximumRelativeError) {
            check.maximumRelativeError = error;
            check.worstIndex = index;
        }
    }
    check.withinTolerance = check.maximumRelativeError <= relativeTolerance;
    return check;
}

} // namespace kachakacha::v2::fabrication
