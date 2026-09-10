#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNoIntersection = "GEO-E001";
constexpr const char* kNotExtendable = "GEO-E002";
constexpr const char* kNotSupported = "GEO-E003";
constexpr const char* kDegenerate = "GEO-E004";
constexpr const char* kNotCoplanar = "GEO-E005";

[[nodiscard]] bool IsLine(const CurveSegment& curve)
{
    return curve.Kind() == CurveKind::Line;
}

//! 直線の無限直線どうしの最接近点。平行なら false。
[[nodiscard]] bool ClosestPointsOnLines(const Vector3& p0, const Vector3& d0,
    const Vector3& p1, const Vector3& d1, double& t0, double& t1)
{
    const double a = Dot(d0, d0);
    const double b = Dot(d0, d1);
    const double c = Dot(d1, d1);
    const Vector3 w = p0 - p1;
    const double d = Dot(d0, w);
    const double e = Dot(d1, w);
    const double denominator = a * c - b * b;
    if (std::abs(denominator) <= 1.0e-15 * std::max(1.0, a * c)) {
        return false;
    }
    t0 = (b * e - c * d) / denominator;
    t1 = (a * e - b * d) / denominator;
    return true;
}

} // namespace

// ---------------- 交差 ----------------

std::vector<EditIntersection> IntersectCurvesForEditing(const CurveSegment& a,
    const CurveSegment& b, double toleranceMm)
{
    std::vector<EditIntersection> found;

    // 直線どうしは解析的に。
    if (IsLine(a) && IsLine(b)) {
        const Vector3 p0 = a.StartPoint();
        const Vector3 d0 = a.EndPoint() - p0;
        const Vector3 p1 = b.StartPoint();
        const Vector3 d1 = b.EndPoint() - p1;
        double t0 = 0.0;
        double t1 = 0.0;
        if (!ClosestPointsOnLines(p0, d0, p1, d1, t0, t1)) {
            return found;
        }
        const Vector3 pointA = p0 + d0 * t0;
        const Vector3 pointB = p1 + d1 * t1;
        if (Distance(pointA, pointB) > toleranceMm) {
            return found; // ねじれの位置。3Dでは交わらない。
        }
        if (t0 < -1.0e-9 || t0 > 1.0 + 1.0e-9 || t1 < -1.0e-9 || t1 > 1.0 + 1.0e-9) {
            return found; // 無限直線では交わるが、線分の外。
        }
        found.push_back({std::clamp(t0, 0.0, 1.0), std::clamp(t1, 0.0, 1.0),
            (pointA + pointB) * 0.5});
        return found;
    }

    // それ以外は標本して当たりを付け、二分で詰める。
    constexpr int kSamples = 512;
    double previousDistance = Distance(a.Evaluate(0.0), b.ClosestPoint(a.Evaluate(0.0)).point);
    for (int index = 1; index <= kSamples; ++index) {
        const double t = static_cast<double>(index) / kSamples;
        const Vector3 pointA = a.Evaluate(t);
        const auto closest = b.ClosestPoint(pointA);
        const double distance = closest.distance;
        const bool crossing = (previousDistance > toleranceMm && distance <= toleranceMm);
        const bool minimum = distance <= toleranceMm;
        if (crossing || (minimum && index == kSamples)) {
            // 近傍を詰める。
            double low = std::max(0.0, t - 1.0 / kSamples);
            double high = t;
            for (int iteration = 0; iteration < 60; ++iteration) {
                const double middle = (low + high) * 0.5;
                if (b.ClosestPoint(a.Evaluate(middle)).distance <= toleranceMm) {
                    high = middle;
                } else {
                    low = middle;
                }
            }
            const double hit = (low + high) * 0.5;
            const Vector3 pointOnA = a.Evaluate(hit);
            const auto onB = b.ClosestPoint(pointOnA);
            const bool duplicate = std::any_of(found.begin(), found.end(),
                [&](const EditIntersection& existing) {
                    return Distance(existing.point, pointOnA) <= toleranceMm * 10.0;
                });
            if (!duplicate) {
                found.push_back({hit, onB.parameter, pointOnA});
            }
        }
        previousDistance = distance;
    }
    return found;
}

// ---------------- 延長 ----------------

Result<CurveSegment> ExtendCurve(const CurveSegment& curve, int endpointIndex,
    double distanceMm)
{
    if (!IsFinite(distanceMm) || distanceMm <= 0.0) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "延長する長さは正の値にしてください。", {}));
    }
    if (curve.Kind() == CurveKind::Circle) {
        return Result<CurveSegment>::Failure(MakeError(kNotExtendable,
            "円は延長できません。", "閉じた形は延ばせません。"));
    }
    switch (curve.Kind()) {
    case CurveKind::Line: {
        const Vector3 start = curve.StartPoint();
        const Vector3 end = curve.EndPoint();
        const Vector3 direction = Normalized(end - start);
        // 同一直線上へ延ばす。
        return endpointIndex == 0
            ? CurveSegment::MakeLine(start - direction * distanceMm, end)
            : CurveSegment::MakeLine(start, end + direction * distanceMm);
    }
    case CurveKind::CircularArc: {
        // 同じ中心・半径・平面を保ち、掃引角だけ増やす。
        const double extraAngle = distanceMm / curve.Radius();
        const double sign = curve.SweepAngleRad() >= 0.0 ? 1.0 : -1.0;
        if (endpointIndex == 0) {
            return CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
                curve.ReferenceDirection(), curve.Radius(),
                curve.StartAngleRad() - extraAngle * sign,
                curve.SweepAngleRad() + extraAngle * sign);
        }
        return CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(), curve.StartAngleRad(),
            curve.SweepAngleRad() + extraAngle * sign);
    }
    case CurveKind::CubicBezier: {
        // 端の1階導関数を保つ多項式延長。制御点を外へ足す。
        const std::vector<Vector3>& points = curve.ControlPoints();
        const double total = curve.TotalLength(1.0e-9);
        if (!(total > 0.0)) {
            return Result<CurveSegment>::Failure(MakeError(kDegenerate,
                "長さ0のベジェは延長できません。", {}));
        }
        const double ratio = distanceMm / total;
        if (endpointIndex == 0) {
            const Vector3 direction = points[0] - points[1];
            const Vector3 newStart = points[0] + direction * ratio;
            return CurveSegment::MakeCubicBezier(
                {newStart, points[0] + (points[0] - points[1]) * (ratio * 0.5), points[1],
                    points[3]});
        }
        const Vector3 direction = points[3] - points[2];
        const Vector3 newEnd = points[3] + direction * ratio;
        return CurveSegment::MakeCubicBezier(
            {points[0], points[2], points[3] + (points[3] - points[2]) * (ratio * 0.5),
                newEnd});
    }
    case CurveKind::CubicBSpline:
        return Result<CurveSegment>::Failure(MakeError(kNotSupported,
            "3次B-splineの延長はまだ入っていません。",
            "近似の折れ線へ落として代用することはしません。"));
    case CurveKind::Circle:
        break;
    }
    return Result<CurveSegment>::Failure(MakeError(kNotExtendable, "延長できません。", {}));
}

Result<CurveSegment> ExtendCurveToBoundary(const CurveSegment& curve, int endpointIndex,
    const CurveSegment& boundary, double toleranceMm)
{
    // 少しずつ伸ばして当たりを探すと、遠い相手のときに何万回も回る。
    // 画面が固まるので、そうしない。
    // 一度だけ十分に伸ばし、相手との交点を求め、そこまでの弧長で伸ばし直す。
    const double tolerance = toleranceMm > 0.0 ? toleranceMm : 1.0e-6;
    const Vector3 tip = endpointIndex == 0 ? curve.StartPoint() : curve.EndPoint();
    const double gap = boundary.ClosestPoint(tip).distance;
    const double curveLength = curve.TotalLength(tolerance);
    // 伸ばす長さの上限。相手までの距離と自分の長さから決める。
    const double reach =
        std::min(std::max({gap * 4.0, curveLength * 10.0, 1.0}), 1.0e4);

    const auto extended = ExtendCurve(curve, endpointIndex, reach);
    if (!extended.HasValue()) {
        return extended;   // 円や閉じたものは伸ばせない。その理由をそのまま返す
    }
    const std::vector<EditIntersection> crossings =
        IntersectCurvesForEditing(extended.Value(), boundary, tolerance);
    if (crossings.empty()) {
        return Result<CurveSegment>::Failure(MakeError(kNoIntersection,
            "延ばしても相手に届きません。",
            "交わる相手を選ぶか、距離を指定して延ばしてください。"));
    }

    // 伸ばした曲線の上で、元の端がどこにあるかを調べる。
    const double tipParameter = extended.Value().ClosestPoint(tip).parameter;
    double best = -1.0;
    for (const EditIntersection& crossing : crossings) {
        const double first = std::min(tipParameter, crossing.parameterA);
        const double second = std::max(tipParameter, crossing.parameterA);
        const double distance = extended.Value().ArcLength(first, second, tolerance);
        if (distance <= tolerance) {
            continue;   // 元の端そのもの。伸ばす必要がない
        }
        // 伸ばした側にある交点だけを採る。
        const bool forward =
            endpointIndex == 0 ? crossing.parameterA < tipParameter
                               : crossing.parameterA > tipParameter;
        if (!forward) {
            continue;
        }
        if (best < 0.0 || distance < best) {
            best = distance;
        }
    }
    if (!(best > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kNoIntersection,
            "延ばしても相手に届きません。",
            "相手は反対側にあります。反対の端を延ばしてください。"));
    }
    return ExtendCurve(curve, endpointIndex, best);
}

// ---------------- トリム ----------------

Result<CurveSegment> TrimCurve(const CurveSegment& curve, const CurveSegment& boundary,
    double clickParameter, double toleranceMm)
{
    const std::vector<EditIntersection> hits =
        IntersectCurvesForEditing(curve, boundary, toleranceMm);
    if (hits.empty()) {
        return Result<CurveSegment>::Failure(MakeError(kNoIntersection,
            "切る境界と交わっていません。",
            "交わっている線を境界に選んでください。"));
    }
    // クリック位置を挟む2つの区切りを探す。両端も区切りに含める。
    std::vector<double> cuts{0.0, 1.0};
    for (const EditIntersection& hit : hits) {
        cuts.push_back(hit.parameterA);
    }
    std::sort(cuts.begin(), cuts.end());
    std::size_t index = 0;
    while (index + 1 < cuts.size() && !(clickParameter >= cuts[index]
               && clickParameter <= cuts[index + 1])) {
        ++index;
    }
    const double removeLow = cuts[index];
    const double removeHigh = cuts[index + 1];

    // 残る側を作る。両端どちらかが丸ごと残る形にする。
    if (removeLow <= 1.0e-12) {
        const auto split = curve.Split(removeHigh);
        if (!split.HasValue()) {
            return Result<CurveSegment>::Failure(split.Diagnostics());
        }
        return Result<CurveSegment>::Success(*split.Value().second);
    }
    if (removeHigh >= 1.0 - 1.0e-12) {
        const auto split = curve.Split(removeLow);
        if (!split.HasValue()) {
            return Result<CurveSegment>::Failure(split.Diagnostics());
        }
        return Result<CurveSegment>::Success(*split.Value().first);
    }
    // 真ん中を消すと2本になる。呼び出し側が2本を扱えるまで断る。
    return Result<CurveSegment>::Failure(MakeError(kNotSupported,
        "真ん中だけを切り取ると線が2本になります。",
        "端から切るか、先に分割してください。"));
}

// ---------------- 角の加工 ----------------

namespace {

//! 残す側の端。0 なら角から遠い端、1 なら始点、2 なら終点。
[[nodiscard]] Vector3 KeptEnd(const CurveSegment& line, const Vector3& corner, int keepSide)
{
    if (keepSide == 1) {
        return line.StartPoint();
    }
    if (keepSide == 2) {
        return line.EndPoint();
    }
    return Distance(line.StartPoint(), corner) > Distance(line.EndPoint(), corner)
        ? line.StartPoint()
        : line.EndPoint();
}

//! 2本の直線の無限交点。平行なら false。
[[nodiscard]] bool LineCorner(const CurveSegment& first, const CurveSegment& second,
    Vector3& corner, Vector3& firstAway, Vector3& secondAway, const CornerOptions& options)
{
    const Vector3 p0 = first.StartPoint();
    const Vector3 d0 = first.EndPoint() - p0;
    const Vector3 p1 = second.StartPoint();
    const Vector3 d1 = second.EndPoint() - p1;
    double t0 = 0.0;
    double t1 = 0.0;
    if (!ClosestPointsOnLines(p0, d0, p1, d1, t0, t1)) {
        return false;
    }
    const Vector3 a = p0 + d0 * t0;
    const Vector3 b = p1 + d1 * t1;
    if (Distance(a, b) > 1.0e-6 * std::max(1.0, d0.Length())) {
        return false; // ねじれの位置
    }
    corner = (a + b) * 0.5;
    // 「残す側」が指定されていればその端、無ければ角から遠い方の端点を残す。
    firstAway = KeptEnd(first, corner, options.firstKeepSide);
    secondAway = KeptEnd(second, corner, options.secondKeepSide);
    return true;
}

} // namespace

Result<CornerResult> ChamferLines(const CurveSegment& first, const CurveSegment& second,
    double setbackMm, double toleranceMm)
{
    return ChamferLines(first, second, setbackMm, CornerOptions{}, toleranceMm);
}

Result<CornerResult> ChamferLines(const CurveSegment& first, const CurveSegment& second,
    double setbackMm, const CornerOptions& options, double toleranceMm)
{
    (void)toleranceMm;
    if (!IsLine(first) || !IsLine(second)) {
        return Result<CornerResult>::Failure(MakeError(kNotSupported,
            "C面取りは直線どうしにだけ使えます。", {}));
    }
    // B の切戻しが 0 なら A と同じ(対称)。V1 の欄と同じ意味。
    const double secondSetbackMm = options.secondSetbackMm > 0.0 ? options.secondSetbackMm
                                                                 : setbackMm;
    if (!(setbackMm > 0.0) || !IsFinite(setbackMm) || !IsFinite(secondSetbackMm)) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "切戻し量は正の値にしてください。", {}));
    }
    Vector3 corner;
    Vector3 firstAway;
    Vector3 secondAway;
    if (!LineCorner(first, second, corner, firstAway, secondAway, options)) {
        return Result<CornerResult>::Failure(MakeError(kNoIntersection,
            "2本の直線が交わりません。",
            "平行な線やねじれの位置にある線は面取りできません。"));
    }
    const Vector3 firstDirection = Normalized(firstAway - corner);
    const Vector3 secondDirection = Normalized(secondAway - corner);
    if (Distance(firstAway, corner) < setbackMm
        || Distance(secondAway, corner) < secondSetbackMm) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "切戻し量が線の長さより大きいです。",
            "もっと小さい値にするか、線を長くしてください。残す側が角の近くの端になっていないかも見てください。"));
    }
    const Vector3 firstPoint = corner + firstDirection * setbackMm;
    const Vector3 secondPoint = corner + secondDirection * secondSetbackMm;

    const auto shortenedFirst = CurveSegment::MakeLine(firstAway, firstPoint);
    const auto chamfer = CurveSegment::MakeLine(firstPoint, secondPoint);
    const auto shortenedSecond = CurveSegment::MakeLine(secondPoint, secondAway);
    if (!shortenedFirst.HasValue() || !chamfer.HasValue() || !shortenedSecond.HasValue()) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "面取りの結果が長さ0になります。", {}));
    }
    return Result<CornerResult>::Success(
        CornerResult{shortenedFirst.Value(), chamfer.Value(), shortenedSecond.Value()});
}

Result<CornerResult> FilletLines(const CurveSegment& first, const CurveSegment& second,
    double radiusMm, double toleranceMm)
{
    return FilletLines(first, second, radiusMm, CornerOptions{}, toleranceMm);
}

Result<CornerResult> FilletLines(const CurveSegment& first, const CurveSegment& second,
    double radiusMm, const CornerOptions& options, double toleranceMm)
{
    (void)toleranceMm;
    if (!IsLine(first) || !IsLine(second)) {
        return Result<CornerResult>::Failure(MakeError(kNotSupported,
            "丸めは直線どうしにだけ使えます。", {}));
    }
    if (!(radiusMm > 0.0) || !IsFinite(radiusMm)) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "半径は正の値にしてください。", {}));
    }
    Vector3 corner;
    Vector3 firstAway;
    Vector3 secondAway;
    if (!LineCorner(first, second, corner, firstAway, secondAway, options)) {
        return Result<CornerResult>::Failure(MakeError(kNoIntersection,
            "2本の直線が交わりません。", {}));
    }
    const Vector3 firstDirection = Normalized(firstAway - corner);
    const Vector3 secondDirection = Normalized(secondAway - corner);
    const double cosine = std::clamp(Dot(firstDirection, secondDirection), -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (angle <= 1.0e-9 || std::abs(angle - kPi) <= 1.0e-9) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "2本が一直線か重なっているため、丸められません。", {}));
    }
    // 接点までの距離 = 半径 / tan(角度/2)
    const double setback = radiusMm / std::tan(angle * 0.5);
    if (Distance(firstAway, corner) < setback || Distance(secondAway, corner) < setback) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "半径が大きすぎて線に収まりません。",
            "必要な長さは " + std::to_string(setback) + " mm です。"));
    }
    const Vector3 firstPoint = corner + firstDirection * setback;
    const Vector3 secondPoint = corner + secondDirection * setback;
    const Vector3 planeNormal = Normalized(Cross(firstDirection, secondDirection));
    if (planeNormal == Vector3{}) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "2本が同じ向きのため、丸める面が決まりません。", {}));
    }
    // 中心は角の二等分線上、角から radius / sin(angle/2) の位置。
    const Vector3 bisector = Normalized(firstDirection + secondDirection);
    const Vector3 center = corner + bisector * (radiusMm / std::sin(angle * 0.5));

    // 接点から接点までの円弧を作る。向きは planeNormal で決める。
    const Vector3 reference = Normalized(firstPoint - center);
    const Vector3 binormal = Cross(planeNormal, reference);
    const Vector3 toSecond = secondPoint - center;
    const double sweep = std::atan2(Dot(toSecond, binormal), Dot(toSecond, reference));
    const auto arc = CurveSegment::MakeCircularArc(center, planeNormal, reference, radiusMm,
        0.0, sweep);
    const auto shortenedFirst = CurveSegment::MakeLine(firstAway, firstPoint);
    const auto shortenedSecond = CurveSegment::MakeLine(secondPoint, secondAway);
    if (!arc.HasValue() || !shortenedFirst.HasValue() || !shortenedSecond.HasValue()) {
        return Result<CornerResult>::Failure(MakeError(kDegenerate,
            "丸めの結果が作れません。", {}));
    }
    return Result<CornerResult>::Success(
        CornerResult{shortenedFirst.Value(), arc.Value(), shortenedSecond.Value()});
}

Result<std::pair<CurveSegment, CurveSegment>> MeetLines(const CurveSegment& first,
    const CurveSegment& second, double toleranceMm)
{
    (void)toleranceMm;
    using Pair = std::pair<CurveSegment, CurveSegment>;
    if (!IsLine(first) || !IsLine(second)) {
        return Result<Pair>::Failure(MakeError(kNotSupported,
            "「2線を交点まで」は直線どうしにだけ使えます。", {}));
    }
    Vector3 corner;
    Vector3 firstAway;
    Vector3 secondAway;
    if (!LineCorner(first, second, corner, firstAway, secondAway, CornerOptions{})) {
        return Result<Pair>::Failure(MakeError(kNoIntersection,
            "2本の直線が交わりません。", {}));
    }
    const auto a = CurveSegment::MakeLine(firstAway, corner);
    const auto b = CurveSegment::MakeLine(secondAway, corner);
    if (!a.HasValue() || !b.HasValue()) {
        return Result<Pair>::Failure(MakeError(kDegenerate,
            "交点まで詰めると長さ0になります。", {}));
    }
    return Result<Pair>::Success(Pair{a.Value(), b.Value()});
}

// ---------------- オフセット ----------------

Result<CurveSegment> OffsetCurveInPlane(const CurveSegment& curve, Vector3 planeNormal,
    double distanceMm)
{
    if (!IsFinite(distanceMm)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "オフセット距離が数値ではありません。", {}));
    }
    const Vector3 unitNormal = Normalized(planeNormal);
    if (unitNormal == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "オフセットする面の向きが決まりません。", {}));
    }
    switch (curve.Kind()) {
    case CurveKind::Line: {
        const Vector3 direction = Normalized(curve.EndPoint() - curve.StartPoint());
        const Vector3 sideways = Normalized(Cross(unitNormal, direction));
        if (sideways == Vector3{}) {
            return Result<CurveSegment>::Failure(MakeError(kNotCoplanar,
                "線が面の法線と平行です。", {}));
        }
        const Vector3 shift = sideways * distanceMm;
        return CurveSegment::MakeLine(curve.StartPoint() + shift, curve.EndPoint() + shift);
    }
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        // 同心で半径だけ変える。種類はそのまま。
        const double sign = Dot(curve.Normal(), unitNormal) >= 0.0 ? 1.0 : -1.0;
        const double radius = curve.Radius() + distanceMm * sign;
        if (!(radius > 0.0)) {
            return Result<CurveSegment>::Failure(MakeError(kDegenerate,
                "内側へ寄せすぎて半径が0以下になります。", {}));
        }
        const auto made = CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), radius, curve.StartAngleRad(),
            curve.SweepAngleRad());
        if (!made.HasValue() || curve.Kind() == CurveKind::CircularArc) {
            return made;
        }
        return CurveSegment::MakeCircle(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), radius);
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline:
        // 自由曲線の厳密オフセットは同じ次数では表せない。近似で誤魔化さない。
        return Result<CurveSegment>::Failure(MakeError(kNotSupported,
            "自由曲線のオフセットはまだ入っていません。",
            "厳密なオフセットは同じ次数の曲線では表せないため、"
            "近似で代用することはしません。"));
    }
    return Result<CurveSegment>::Failure(MakeError(kNotSupported,
        "この種類はオフセットできません。", {}));
}

// ---------------- 変換 ----------------

namespace {

[[nodiscard]] Vector3 RotateAround(const Vector3& point, const Vector3& axisPoint,
    const Vector3& axis, double angleRad)
{
    const Vector3 relative = point - axisPoint;
    const double cosine = std::cos(angleRad);
    const double sine = std::sin(angleRad);
    return axisPoint + relative * cosine + Cross(axis, relative) * sine
        + axis * (Dot(axis, relative) * (1.0 - cosine));
}

[[nodiscard]] Vector3 MirrorPoint(const Vector3& point, const Vector3& planePoint,
    const Vector3& unitNormal)
{
    return point - unitNormal * (2.0 * Dot(point - planePoint, unitNormal));
}

} // namespace

CurveSegment TranslateCurve(const CurveSegment& curve, Vector3 delta)
{
    switch (curve.Kind()) {
    case CurveKind::Line:
        return CurveSegment::MakeLine(curve.StartPoint() + delta, curve.EndPoint() + delta)
            .Value();
    case CurveKind::CircularArc:
        return CurveSegment::MakeCircularArc(curve.Center() + delta, curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(), curve.StartAngleRad(),
            curve.SweepAngleRad())
            .Value();
    case CurveKind::Circle:
        return CurveSegment::MakeCircle(curve.Center() + delta, curve.Normal(),
            curve.ReferenceDirection(), curve.Radius())
            .Value();
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points = curve.ControlPoints();
        for (Vector3& point : points) {
            point = point + delta;
        }
        return curve.Kind() == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(std::move(points)).Value()
            : CurveSegment::MakeCubicBSpline(std::move(points)).Value();
    }
    }
    return curve;
}

Result<CurveSegment> RotateCurve(const CurveSegment& curve, Vector3 axisPoint,
    Vector3 axisDirection, double angleRad)
{
    const Vector3 axis = Normalized(axisDirection);
    if (axis == Vector3{} || !IsFinite(angleRad)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "回転の軸か角度が正しくありません。", {}));
    }
    const auto move = [&](const Vector3& point) {
        return RotateAround(point, axisPoint, axis, angleRad);
    };
    switch (curve.Kind()) {
    case CurveKind::Line:
        return CurveSegment::MakeLine(move(curve.StartPoint()), move(curve.EndPoint()));
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const Vector3 center = move(curve.Center());
        const Vector3 normal = RotateAround(curve.Center() + curve.Normal(), curve.Center(),
                                   axis, angleRad)
            - curve.Center();
        const Vector3 reference =
            RotateAround(curve.Center() + curve.ReferenceDirection(), curve.Center(), axis,
                angleRad)
            - curve.Center();
        return curve.Kind() == CurveKind::Circle
            ? CurveSegment::MakeCircle(center, normal, reference, curve.Radius())
            : CurveSegment::MakeCircularArc(center, normal, reference, curve.Radius(),
                  curve.StartAngleRad(), curve.SweepAngleRad());
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points = curve.ControlPoints();
        for (Vector3& point : points) {
            point = move(point);
        }
        return curve.Kind() == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(std::move(points))
            : CurveSegment::MakeCubicBSpline(std::move(points));
    }
    }
    return Result<CurveSegment>::Failure(MakeError(kNotSupported, "回転できません。", {}));
}

Result<CurveSegment> MirrorCurve(const CurveSegment& curve, Vector3 planePoint,
    Vector3 planeNormal)
{
    const Vector3 unitNormal = Normalized(planeNormal);
    if (unitNormal == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "鏡映する面の向きが決まりません。", {}));
    }
    const auto move = [&](const Vector3& point) {
        return MirrorPoint(point, planePoint, unitNormal);
    };
    switch (curve.Kind()) {
    case CurveKind::Line:
        return CurveSegment::MakeLine(move(curve.StartPoint()), move(curve.EndPoint()));
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const Vector3 center = move(curve.Center());
        // 鏡映は行列式が負なので、外積の向きが1回ぶん反転する。
        // 法線を反転させておくと N'×R' が鏡映後の従法線と一致し、
        // 開始角と掃引角をそのまま使える(角度をいじると形がずれる)。
        const Vector3 normal = (move(curve.Center() + curve.Normal()) - center) * -1.0;
        const Vector3 reference = move(curve.Center() + curve.ReferenceDirection()) - center;
        return curve.Kind() == CurveKind::Circle
            ? CurveSegment::MakeCircle(center, normal, reference, curve.Radius())
            : CurveSegment::MakeCircularArc(center, normal, reference, curve.Radius(),
                  curve.StartAngleRad(), curve.SweepAngleRad());
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points = curve.ControlPoints();
        for (Vector3& point : points) {
            point = move(point);
        }
        return curve.Kind() == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(std::move(points))
            : CurveSegment::MakeCubicBSpline(std::move(points));
    }
    }
    return Result<CurveSegment>::Failure(MakeError(kNotSupported, "鏡映できません。", {}));
}

} // namespace kachakacha::v2::geometry

namespace kachakacha::v2::geometry {

base::Result<CurveSegment> ReverseCurve(const CurveSegment& curve)
{
    constexpr double kTwoPi = 6.283185307179586;
    switch (curve.Kind()) {
    case CurveKind::Line:
        return CurveSegment::MakeLine(curve.EndPoint(), curve.StartPoint());
    case CurveKind::CircularArc:
        return CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(),
            curve.StartAngleRad() + curve.SweepAngleRad(), -curve.SweepAngleRad());
    case CurveKind::Circle:
        return CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(), curve.StartAngleRad(), -kTwoPi);
    case CurveKind::CubicBezier: {
        std::vector<Vector3> points = curve.ControlPoints();
        std::reverse(points.begin(), points.end());
        return CurveSegment::MakeCubicBezier(points);
    }
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points = curve.ControlPoints();
        std::reverse(points.begin(), points.end());
        return CurveSegment::MakeCubicBSpline(points);
    }
    }
    return base::Result<CurveSegment>::Failure(base::MakeError("GEO-E010",
        "その線は向きを変えられません。", "種類が分かりません。"));
}

} // namespace kachakacha::v2::geometry
