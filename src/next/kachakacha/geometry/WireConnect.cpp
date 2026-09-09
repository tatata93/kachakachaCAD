#include "kachakacha/geometry/WireConnect.h"

#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace kachakacha::v2::geometry {
namespace {

using base::MakeError;
using base::Result;

[[nodiscard]] double Length(const Vector3& value) noexcept
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] Vector3 Unit(const Vector3& value) noexcept
{
    const double length = Length(value);
    if (!(length > 0.0)) {
        return Vector3{};
    }
    return Vector3{value.x / length, value.y / length, value.z / length};
}

//! 端点をその点へ動かした線。直線は端を差し替え、それ以外は動かせないので断る。
//!
//! 円弧やB-splineの端を引っぱると、途中の形まで変わってしまう。
//! 「つないだつもりが別の形になっていた」を防ぐため、ここでは断る。
[[nodiscard]] Result<CurveSegment> MoveEndpoint(const CurveSegment& curve, bool atEnd,
    const Vector3& target)
{
    if (curve.Kind() != CurveKind::Line) {
        return Result<CurveSegment>::Failure(MakeError("GEO-E011",
            "この線は端点だけを動かせません。",
            "直線以外は、端を動かすと途中の形まで変わります。"
            "先にトリムか延長で長さを合わせてください。"));
    }
    return atEnd ? CurveSegment::MakeLine(curve.StartPoint(), target)
                 : CurveSegment::MakeLine(target, curve.EndPoint());
}

//! 4通りの端の組合せのうち、いちばん近いもの。
struct NearestEnds {
    bool firstAtEnd = true;
    bool secondAtEnd = false;
    double distanceMm = 0.0;
};

[[nodiscard]] NearestEnds FindNearestEnds(const CurveSegment& first,
    const CurveSegment& second)
{
    NearestEnds best;
    double bestDistance = -1.0;
    for (const bool firstAtEnd : {false, true}) {
        for (const bool secondAtEnd : {false, true}) {
            const Vector3 a = firstAtEnd ? first.EndPoint() : first.StartPoint();
            const Vector3 b = secondAtEnd ? second.EndPoint() : second.StartPoint();
            const double distance = Length(Vector3{a.x - b.x, a.y - b.y, a.z - b.z});
            if (bestDistance < 0.0 || distance < bestDistance) {
                bestDistance = distance;
                best.firstAtEnd = firstAtEnd;
                best.secondAtEnd = secondAtEnd;
                best.distanceMm = distance;
            }
        }
    }
    return best;
}

//! つなぎ目での向き。外向き(相手へ向かう向き)にそろえて返す。
[[nodiscard]] Vector3 OutwardTangent(const CurveSegment& curve, bool atEnd)
{
    const Vector3 derivative = curve.FirstDerivative(atEnd ? 1.0 : 0.0);
    const Vector3 unit = Unit(derivative);
    return atEnd ? unit : Vector3{-unit.x, -unit.y, -unit.z};
}

//! つなぎ目での曲がり具合(曲率ベクトルの大きさ)。
[[nodiscard]] double CurvatureAt(const CurveSegment& curve, bool atEnd)
{
    const double t = atEnd ? 1.0 : 0.0;
    const Vector3 first = curve.FirstDerivative(t);
    const Vector3 second = curve.SecondDerivative(t);
    const double speed = Length(first);
    if (!(speed > 0.0)) {
        return 0.0;
    }
    const Vector3 cross{first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z, first.x * second.y - first.y * second.x};
    return Length(cross) / (speed * speed * speed);
}

} // namespace

std::string_view ConnectContinuityNameJa(ConnectContinuity value) noexcept
{
    switch (value) {
    case ConnectContinuity::Position:  return "端点一致";
    case ConnectContinuity::Tangent:   return "接線接続";
    case ConnectContinuity::Curvature: return "曲率接続";
    }
    return "不明";
}

Result<ConnectResult> ConnectCurves(const CurveSegment& first, const CurveSegment& second,
    ConnectContinuity continuity, double toleranceMm)
{
    if (!(toleranceMm > 0.0)) {
        return Result<ConnectResult>::Failure(MakeError("GEO-E012",
            "許容差が正の数ではありません。", "つなぐときの許容差です。"));
    }
    const NearestEnds ends = FindNearestEnds(first, second);
    const Vector3 a = ends.firstAtEnd ? first.EndPoint() : first.StartPoint();
    const Vector3 b = ends.secondAtEnd ? second.EndPoint() : second.StartPoint();
    const Vector3 joint{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};

    ConnectResult result{first, second, joint, ends.distanceMm * 0.5};
    if (ends.distanceMm > toleranceMm) {
        // 動かして合わせる。両方を同じだけ動かすので、どちらかだけがずれない。
        const auto movedFirst = MoveEndpoint(first, ends.firstAtEnd, joint);
        if (!movedFirst.HasValue()) {
            return Result<ConnectResult>::Failure(movedFirst.Diagnostics());
        }
        const auto movedSecond = MoveEndpoint(second, ends.secondAtEnd, joint);
        if (!movedSecond.HasValue()) {
            return Result<ConnectResult>::Failure(movedSecond.Diagnostics());
        }
        result.first = movedFirst.Value();
        result.second = movedSecond.Value();
    } else {
        result.movedMm = 0.0;
    }
    if (continuity == ConnectContinuity::Position) {
        return Result<ConnectResult>::Success(std::move(result));
    }

    // ここから先は「そろっているか」を見るだけ。種類を変えてまでそろえない。
    const Vector3 outFirst = OutwardTangent(result.first, ends.firstAtEnd);
    const Vector3 outSecond = OutwardTangent(result.second, ends.secondAtEnd);
    // つなぎ目では、1本目の外向きと2本目の外向きが正反対になっているのがそろった状態。
    const double alignment = -(outFirst.x * outSecond.x + outFirst.y * outSecond.y
        + outFirst.z * outSecond.z);
    if (alignment < 0.999) {
        const double degrees = std::acos(std::max(-1.0, std::min(1.0, alignment)))
            * 180.0 / 3.14159265358979323846;
        return Result<ConnectResult>::Failure(MakeError("GEO-E013",
            "つなぎ目で向きがそろっていません。",
            "接線接続にするには、つなぎ目で向きがそろっている必要があります。"
            "いまは " + std::to_string(degrees) + " 度ずれています。"
            "先に片方を回すか、丸め(R)でつないでください。"));
    }
    if (continuity == ConnectContinuity::Tangent) {
        return Result<ConnectResult>::Success(std::move(result));
    }
    const double curvatureFirst = CurvatureAt(result.first, ends.firstAtEnd);
    const double curvatureSecond = CurvatureAt(result.second, ends.secondAtEnd);
    const double difference = std::abs(curvatureFirst - curvatureSecond);
    if (difference > 1.0e-6) {
        return Result<ConnectResult>::Failure(MakeError("GEO-E014",
            "つなぎ目で曲がり具合がそろっていません。",
            "曲率接続にするには、つなぎ目で曲がり具合もそろっている必要があります。"
            "いまの差は " + std::to_string(difference) + " (1/mm)です。"));
    }
    return Result<ConnectResult>::Success(std::move(result));
}

Result<std::vector<CurveSegment>> SplitCurveAtIntersections(const CurveSegment& curve,
    const std::vector<CurveSegment>& others, double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (others.empty()) {
        return Out::Failure(MakeError("GEO-E015", "切る相手がありません。",
            "分割するには、交わる相手の線も選んでください。"));
    }
    std::vector<double> cuts;
    for (const CurveSegment& other : others) {
        for (const EditIntersection& hit :
            IntersectCurvesForEditing(curve, other, toleranceMm)) {
            // 端では切らない。切っても同じ形が2本できるだけである。
            if (hit.parameterA > 1.0e-9 && hit.parameterA < 1.0 - 1.0e-9) {
                cuts.push_back(hit.parameterA);
            }
        }
    }
    if (cuts.empty()) {
        return Out::Failure(MakeError("GEO-E015", "切る相手がありません。",
            "選んだ線どうしは交わっていません。"));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
                   [](double a, double b) { return std::abs(a - b) < 1.0e-9; }),
        cuts.end());

    // 端から順に切っていく。切るたびに残りのパラメータを詰め直す。
    std::vector<CurveSegment> pieces;
    CurveSegment remaining = curve;
    double consumed = 0.0;
    for (const double cut : cuts) {
        const double local = (cut - consumed) / (1.0 - consumed);
        if (!(local > 1.0e-9) || !(local < 1.0 - 1.0e-9)) {
            continue;
        }
        const auto split = remaining.Split(local);
        if (!split.HasValue()) {
            return Out::Failure(split.Diagnostics());
        }
        pieces.push_back(*split.Value().first);
        remaining = *split.Value().second;
        consumed = cut;
    }
    pieces.push_back(remaining);
    return Out::Success(std::move(pieces));
}

Result<std::vector<CurveSegment>> JoinCurves(const std::vector<CurveSegment>& curves,
    double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (curves.size() < 2) {
        return Out::Failure(MakeError("GEO-E016", "つなぐ線が足りません。",
            "結合するには2本以上選んでください。"));
    }
    std::vector<ChainInput> inputs;
    inputs.reserve(curves.size());
    for (std::size_t index = 0; index < curves.size(); ++index) {
        // 連結解析は「同じ線を二度入れた」をIDで見分ける。
        // 全部ゼロのIDにすると、別の線どうしが同じものに見えてしまう。
        // 並び順から作った目印を付ける。文書のIDとは別物で、ここでしか使わない。
        std::array<std::uint8_t, 16> bytes{};
        bytes[15] = static_cast<std::uint8_t>((index + 1) & 0xFF);
        bytes[14] = static_cast<std::uint8_t>(((index + 1) >> 8) & 0xFF);
        inputs.push_back(ChainInput{base::EntityId{},
            base::SegmentId(base::Uuid(bytes)), curves[index]});
    }
    GeometryTolerance tolerance;
    tolerance.interactiveJoinMm = toleranceMm;
    const auto analysis = AnalyzeChain(inputs, tolerance);
    if (!analysis.HasValue()) {
        // つながっていない・枝分かれしている、をそのまま伝える。
        return Out::Failure(analysis.Diagnostics());
    }
    std::vector<CurveSegment> ordered;
    ordered.reserve(curves.size());
    for (std::size_t index = 0; index < analysis.Value().order.segments.size(); ++index) {
        const auto& oriented = analysis.Value().order.segments[index];
        // AnalyzeChain は入力の並びを保つので、添字で引ける。
        const CurveSegment& segment = curves[index];
        if (!oriented.reversed) {
            ordered.push_back(segment);
            continue;
        }
        const auto flipped = ReverseCurve(segment);
        if (!flipped.HasValue()) {
            return Out::Failure(flipped.Diagnostics());
        }
        ordered.push_back(flipped.Value());
    }
    return Out::Success(std::move(ordered));
}

} // namespace kachakacha::v2::geometry
