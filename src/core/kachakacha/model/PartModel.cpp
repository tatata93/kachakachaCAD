#include "kachakacha/model/PartModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kachakacha::model {

namespace {

constexpr int kAxisSamples = 96;   // 分割軸方向のサンプル数
constexpr int kCrossSamples = 17;  // 直交方向のサンプル数

// 入力面上の点。tは分割軸方向、sは直交方向(いずれもローカル0..1)。
[[nodiscard]] geometry::Vector3 EvaluateMid(
    const PartSource& source, PartSplitAxis axis, double t, double s)
{
    const double u = axis == PartSplitAxis::V ? s : t;
    const double v = axis == PartSplitAxis::V ? t : s;
    return source.Evaluate(u, v);
}

// 帯 [t0,t1] の「1軸曲げ近似からのずれ」を弦偏差で見積もる。
// 1軸曲げ部材は帯を横切る素線(分割軸方向の曲線)が直線になるため、
// 実曲線と弦との最大距離が近似偏差の見積もりになる。
[[nodiscard]] double EstimateChordDeviation(
    const PartSource& source, PartSplitAxis axis, double t0, double t1)
{
    double worst = 0.0;
    const int innerSamples = 9;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        const geometry::Vector3 start = EvaluateMid(source, axis, t0, s);
        const geometry::Vector3 end = EvaluateMid(source, axis, t1, s);
        const geometry::Vector3 chord = end - start;
        const double chordLengthSquared = chord.LengthSquared();
        for (int inner = 1; inner < innerSamples; ++inner) {
            const double f = static_cast<double>(inner) / innerSamples;
            const double t = t0 + (t1 - t0) * f;
            const geometry::Vector3 point = EvaluateMid(source, axis, t, s);
            geometry::Vector3 offset = point - start;
            if (chordLengthSquared > 1.0e-18) {
                const double along = Dot(offset, chord) / chordLengthSquared;
                offset = offset - chord * along;
            }
            worst = std::max(worst, offset.Length());
        }
    }
    return worst;
}

// 帯 [t0,t1] の分割軸方向の実幅(直交方向サンプルの平均)。
[[nodiscard]] double MeasureWidth(
    const PartSource& source, PartSplitAxis axis, double t0, double t1)
{
    double total = 0.0;
    const int lengthSamples = 8;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        double length = 0.0;
        geometry::Vector3 previous = EvaluateMid(source, axis, t0, s);
        for (int step = 1; step <= lengthSamples; ++step) {
            const double t = t0 + (t1 - t0) * static_cast<double>(step) / lengthSamples;
            const geometry::Vector3 point = EvaluateMid(source, axis, t, s);
            length += (point - previous).Length();
            previous = point;
        }
        total += length;
    }
    return total / kCrossSamples;
}

[[nodiscard]] ApproximatedPart MakePart(
    const PartSource& source, PartSplitAxis axis, int number, double t0, double t1)
{
    ApproximatedPart part;
    part.number = number;
    part.minimumParameter = t0;
    part.maximumParameter = t1;
    part.widthMillimeters = MeasureWidth(source, axis, t0, t1);
    part.estimatedDeviationMillimeters = EstimateChordDeviation(source, axis, t0, t1);
    if (part.estimatedDeviationMillimeters <= 1.0e-6) {
        part.classification = PlateDevelopability::Planar;
    } else {
        part.classification = PlateDevelopability::Developable;
    }
    return part;
}

} // namespace

PartApproximationResult ApproximatePlateParts(
    const PartSource& source,
    const PartApproximationOptions& options)
{
    if (!std::isfinite(options.maximumDeviationMillimeters)
        || options.maximumDeviationMillimeters <= 0.0) {
        throw std::invalid_argument("部材近似の許容偏差は正の値で指定してください。");
    }
    if (options.maximumPartCount < 1) {
        throw std::invalid_argument("部材数の上限は1以上で指定してください。");
    }
    if (!std::isfinite(options.minimumPartWidthMillimeters)
        || options.minimumPartWidthMillimeters < 0.0) {
        throw std::invalid_argument("最小部材幅は0以上で指定してください。");
    }

    const PartSplitAxis axis = options.splitAxis;
    std::vector<double> boundaries;
    boundaries.push_back(0.0);

    if (!options.automaticBoundaries) {
        std::vector<double> manual = options.manualBoundaryParameters;
        std::sort(manual.begin(), manual.end());
        for (const double parameter : manual) {
            if (!std::isfinite(parameter) || parameter <= 0.0 || parameter >= 1.0) {
                throw std::invalid_argument("手動境界のパラメータは0と1の間で指定してください。");
            }
            if (parameter <= boundaries.back() + 1.0e-9) {
                throw std::invalid_argument("手動境界のパラメータが重複しています。");
            }
            boundaries.push_back(parameter);
        }
        boundaries.push_back(1.0);
    } else {
        // 貪欲法: 偏差が許容内に収まる限り帯を伸ばす。
        const double step = 1.0 / kAxisSamples;
        double start = 0.0;
        while (start < 1.0 - 1.0e-9) {
            double end = std::min(1.0, start + step);
            double lastGood = end;
            while (end < 1.0 - 1.0e-9) {
                const double next = std::min(1.0, end + step);
                if (EstimateChordDeviation(source, axis, start, next)
                    > options.maximumDeviationMillimeters) {
                    break;
                }
                end = next;
                lastGood = next;
            }
            end = lastGood;
            // 最小幅を満たすまで伸ばす(公差超過よりも「作れない細さ」を避ける)。
            while (end < 1.0 - 1.0e-9
                && MeasureWidth(source, axis, start, end)
                    < options.minimumPartWidthMillimeters) {
                end = std::min(1.0, end + step);
            }
            if (end >= 1.0 - 1.0e-9) {
                end = 1.0;
            }
            boundaries.push_back(end);
            start = end;
        }
        // 上限部材数を超えた場合は等分割へ切り替える(部材数を優先し、偏差は結果で報告)。
        if (static_cast<int>(boundaries.size()) - 1 > options.maximumPartCount) {
            boundaries.clear();
            for (int index = 0; index <= options.maximumPartCount; ++index) {
                boundaries.push_back(
                    static_cast<double>(index) / options.maximumPartCount);
            }
        }
        // 末尾の帯が細すぎる場合は手前の帯と結合する。
        if (boundaries.size() >= 3) {
            const double t0 = boundaries[boundaries.size() - 2];
            if (MeasureWidth(source, axis, t0, 1.0)
                < options.minimumPartWidthMillimeters) {
                boundaries.erase(boundaries.end() - 2);
            }
        }
    }

    PartApproximationResult result;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
        ApproximatedPart part = MakePart(
            source, axis, static_cast<int>(index) + 1,
            boundaries[index], boundaries[index + 1]);
        result.maximumDeviationMillimeters = std::max(
            result.maximumDeviationMillimeters, part.estimatedDeviationMillimeters);
        result.parts.push_back(std::move(part));
    }
    result.reachedRequestedTolerance =
        result.maximumDeviationMillimeters <= options.maximumDeviationMillimeters + 1.0e-9;
    return result;
}

Wire BuildPartBoundaryWire(
    const PartSource& source,
    PartSplitAxis splitAxis,
    double parameter,
    int samples)
{
    if (samples < 2) {
        throw std::invalid_argument("部材境界のサンプル数は2以上で指定してください。");
    }
    std::vector<geometry::Vector3> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int index = 0; index <= samples; ++index) {
        const double s = static_cast<double>(index) / samples;
        points.push_back(EvaluateMid(source, splitAxis, parameter, s));
    }
    return Wire::Polyline(std::move(points));
}

} // namespace kachakacha::model

namespace kachakacha::model {

namespace {

using geometry::Vector2;
using geometry::Vector3;

[[nodiscard]] Vector3 Normalized(const Vector3& value)
{
    const double length = value.Length();
    if (length <= 1.0e-12) {
        return {0.0, 0.0, 0.0};
    }
    return value * (1.0 / length);
}

//! 既知の2点 a,b(2D)と、その2点からの距離 ra,rb で三角形の第3点を置く。
//! orientation(+1/-1)は cross(b-a, p-a) の符号。三角形の辺長を厳密に保存する。
[[nodiscard]] Vector2 PlaceThirdPoint(
    const Vector2& a, const Vector2& b, double ra, double rb, double orientation)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= 1.0e-12) {
        return {a.x + ra, a.y};
    }
    // 円の交点(標準形)。数値誤差で交わらない場合は clamp する。
    const double along = (ra * ra - rb * rb + d * d) / (2.0 * d);
    double perpendicularSquared = ra * ra - along * along;
    if (perpendicularSquared < 0.0) {
        perpendicularSquared = 0.0;
    }
    const double perpendicular = std::sqrt(perpendicularSquared);
    const double ux = dx / d;
    const double uy = dy / d;
    const double sign = orientation >= 0.0 ? 1.0 : -1.0;
    return {
        a.x + ux * along - uy * perpendicular * sign,
        a.y + uy * along + ux * perpendicular * sign,
    };
}

[[nodiscard]] double Distance3(const Vector3& a, const Vector3& b)
{
    return (b - a).Length();
}

} // namespace

PartMeshDevelopment DevelopPartMesh(
    const PartSource& source,
    PartSplitAxis splitAxis,
    const std::vector<double>& railParameters,
    int columns)
{
    if (railParameters.size() < 2) {
        throw std::invalid_argument("展開にはレールが2本以上必要です。");
    }
    if (columns < 2) {
        throw std::invalid_argument("展開の列数は2以上で指定してください。");
    }
    for (std::size_t index = 1; index < railParameters.size(); ++index) {
        if (railParameters[index] <= railParameters[index - 1] + 1.0e-12) {
            throw std::invalid_argument("レールのパラメータは昇順で指定してください。");
        }
    }

    PartMeshDevelopment mesh;
    mesh.rows = static_cast<int>(railParameters.size());
    mesh.columns = columns + 1;

    // 近似の実形状: 各レールは実面上の曲線、レール間は直線補間(ルールド)。
    // メッシュ自体はレール点列で表す(レール間の直線は展開時の三角形が担う)。
    const auto evaluateMid = [&source, splitAxis](double t, double s) {
        const double u = splitAxis == PartSplitAxis::V ? s : t;
        const double v = splitAxis == PartSplitAxis::V ? t : s;
        return source.Evaluate(u, v);
    };
    mesh.world.resize(mesh.rows);
    for (int row = 0; row < mesh.rows; ++row) {
        mesh.world[row].reserve(mesh.columns);
        for (int column = 0; column < mesh.columns; ++column) {
            const double s = static_cast<double>(column) / columns;
            mesh.world[row].push_back(evaluateMid(railParameters[row], s));
        }
    }

    // 展開: 帯(隣り合うレール間)ごとに三角形単位の等長配置で厳密に展開する。
    // 1帯のストリップは全頂点が輪郭上にあるため、辺長を保存した展開が常に存在する。
    // 複数帯は帯ごとに展開し、共有レールの形で剛体合わせして積む(角度欠損=
    // 折り線まわりの誤差は局所に留まり、蓄積しない)。
    mesh.developed.assign(mesh.rows, std::vector<Vector2>(mesh.columns, {0.0, 0.0}));

    const auto developBand = [&mesh](int bottomRow) {
        // 帯 bottomRow → bottomRow+1 を、左端の素線を基準にクアッド行進で展開する。
        const auto& bottom3 = mesh.world[bottomRow];
        const auto& top3 = mesh.world[bottomRow + 1];
        std::vector<Vector2> bottom2(mesh.columns);
        std::vector<Vector2> top2(mesh.columns);
        bottom2[0] = {0.0, 0.0};
        top2[0] = {0.0, Distance3(bottom3[0], top3[0])};
        for (int column = 1; column < mesh.columns; ++column) {
            // 三角形 (B_{c-1}, T_{c-1}, B_c): 下辺と対角線を保存。
            bottom2[column] = PlaceThirdPoint(
                bottom2[column - 1], top2[column - 1],
                Distance3(bottom3[column - 1], bottom3[column]),
                Distance3(top3[column - 1], bottom3[column]),
                -1.0);
            // 三角形 (T_{c-1}, B_c, T_c): 上辺と素線を保存。
            top2[column] = PlaceThirdPoint(
                top2[column - 1], bottom2[column],
                Distance3(top3[column - 1], top3[column]),
                Distance3(bottom3[column], top3[column]),
                +1.0);
        }
        return std::pair<std::vector<Vector2>, std::vector<Vector2>>{
            std::move(bottom2), std::move(top2)};
    };

    // 2Dの剛体合わせ(回転+平行移動、鏡映なし)。
    const auto rigidAlign = [](const std::vector<Vector2>& from,
                               const std::vector<Vector2>& to,
                               std::vector<Vector2>& apply) {
        double fromCenterX = 0.0, fromCenterY = 0.0, toCenterX = 0.0, toCenterY = 0.0;
        const double count = static_cast<double>(from.size());
        for (std::size_t index = 0; index < from.size(); ++index) {
            fromCenterX += from[index].x;
            fromCenterY += from[index].y;
            toCenterX += to[index].x;
            toCenterY += to[index].y;
        }
        fromCenterX /= count; fromCenterY /= count;
        toCenterX /= count; toCenterY /= count;
        double dotSum = 0.0, crossSum = 0.0;
        for (std::size_t index = 0; index < from.size(); ++index) {
            const double ax = from[index].x - fromCenterX;
            const double ay = from[index].y - fromCenterY;
            const double bx = to[index].x - toCenterX;
            const double by = to[index].y - toCenterY;
            dotSum += ax * bx + ay * by;
            crossSum += ax * by - ay * bx;
        }
        const double angle = std::atan2(crossSum, dotSum);
        const double cosAngle = std::cos(angle);
        const double sinAngle = std::sin(angle);
        for (Vector2& point : apply) {
            const double px = point.x - fromCenterX;
            const double py = point.y - fromCenterY;
            point = {
                toCenterX + px * cosAngle - py * sinAngle,
                toCenterY + px * sinAngle + py * cosAngle,
            };
        }
    };

    for (int band = 0; band + 1 < mesh.rows; ++band) {
        auto [bottom2, top2] = developBand(band);
        if (band == 0) {
            mesh.developed[0] = bottom2;
            mesh.developed[1] = top2;
            continue;
        }
        // 共有レール(この帯の下辺)を、既に置かれたレールへ剛体で合わせる。
        std::vector<Vector2> alignedTop = top2;
        rigidAlign(bottom2, mesh.developed[band], alignedTop);
        mesh.developed[band + 1] = std::move(alignedTop);
    }

    // 内部レールの山谷: 前後の帯の面法線の二面角の符号。
    mesh.creaseDirections.assign(std::max(0, mesh.rows - 2), 0);
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        double accumulated = 0.0;
        for (int column = 1; column + 1 < mesh.columns; column += 4) {
            const Vector3 axis = Normalized(
                mesh.world[rail][column + 1] - mesh.world[rail][column - 1]);
            const Vector3 toPrevious = mesh.world[rail - 1][column] - mesh.world[rail][column];
            const Vector3 toNext = mesh.world[rail + 1][column] - mesh.world[rail][column];
            const Vector3 previousNormal = Normalized(Cross(axis, toPrevious));
            const Vector3 nextNormal = Normalized(Cross(toNext, axis));
            // 折れの符号: 隣り合う帯が法線側へ谷になるか山になるか。
            const double bend = Dot(Cross(previousNormal, nextNormal), axis);
            accumulated += bend;
        }
        if (accumulated > 1.0e-9) {
            mesh.creaseDirections[rail - 1] = 1;
        } else if (accumulated < -1.0e-9) {
            mesh.creaseDirections[rail - 1] = -1;
        }
    }
    return mesh;
}

namespace {

// 後方の無名名前空間で定義(同一TUの無名名前空間は同一)。
[[nodiscard]] double MeasureCreaseAngleInState(
    const std::vector<std::vector<Vector3>>& state, int row, int columns);

//! ヒンジ(共有辺 P→Q)まわりの頂点配置の分解: 辺方向位置 along、辺からの距離 height、
//! 「前三角形の反対側(=平ら)」を0とする符号付き曲げ角 bendAngle。
struct HingePlacement {
    double along = 0.0;
    double height = 0.0;
    double bendAngle = 0.0;
};

//! world 上の三角形 (P,Q,V) と前三角形の第3頂点 prev からヒンジ分解を測る。
[[nodiscard]] HingePlacement MeasureHinge(
    const Vector3& worldP,
    const Vector3& worldQ,
    const Vector3& worldV,
    const Vector3& worldPrev)
{
    HingePlacement placement;
    const Vector3 edge = worldQ - worldP;
    const double edgeLength = edge.Length();
    if (edgeLength <= 1.0e-12) {
        return placement;
    }
    const Vector3 e = edge * (1.0 / edgeLength);
    const Vector3 relativeV = worldV - worldP;
    placement.along = Dot(relativeV, e);
    const Vector3 perpendicular = relativeV - e * placement.along;
    placement.height = perpendicular.Length();
    if (placement.height <= 1.0e-12) {
        return placement;
    }
    Vector3 towardPrev = worldPrev - worldP;
    towardPrev = towardPrev - e * Dot(towardPrev, e);
    const double prevLength = towardPrev.Length();
    if (prevLength <= 1.0e-12) {
        return placement; // 前三角形が退化: 平ら扱い。
    }
    const Vector3 w = towardPrev * (1.0 / prevLength);
    const Vector3 n = Cross(e, w);
    const Vector3 direction = perpendicular * (1.0 / placement.height);
    placement.bendAngle = std::atan2(Dot(direction, n), Dot(direction, w * -1.0));
    return placement;
}

//! 再構成側: 配置済みのヒンジ(P,Q)と前三角形第3頂点から、曲げ角 angle で頂点を置く。
//! 辺長(along/height)は world から測った値そのものを使うため、三角形は厳密に剛体。
[[nodiscard]] Vector3 PlaceOnHinge(
    const Vector3& placedP,
    const Vector3& placedQ,
    const Vector3& placedPrev,
    const HingePlacement& placement,
    double angle)
{
    const Vector3 edge = placedQ - placedP;
    const double edgeLength = edge.Length();
    if (edgeLength <= 1.0e-12) {
        return placedP;
    }
    const Vector3 e = edge * (1.0 / edgeLength);
    if (placement.height <= 1.0e-12) {
        return placedP + e * placement.along;
    }
    Vector3 towardPrev = placedPrev - placedP;
    towardPrev = towardPrev - e * Dot(towardPrev, e);
    const double prevLength = towardPrev.Length();
    if (prevLength <= 1.0e-12) {
        return placedP + e * placement.along;
    }
    const Vector3 w = towardPrev * (1.0 / prevLength);
    const Vector3 n = Cross(e, w);
    const Vector3 direction = w * (-std::cos(angle)) + n * std::sin(angle);
    return placedP + e * placement.along + direction * placement.height;
}

//! 帯 band を「三角形は剛体・折り目の二面角だけ progress 倍」で等長に曲げ直す。
//! progress=1 は world と厳密一致(先頭三角形を world に固定して行進する)。
//! progress=0 は先頭三角形の平面上に完全に平らへ展開された形。
void BendBandStrip(
    const PartMeshDevelopment& mesh,
    int band,
    double progress,
    std::vector<Vector3>& bottom,
    std::vector<Vector3>& top)
{
    const auto& worldBottom = mesh.world[band];
    const auto& worldTop = mesh.world[band + 1];
    if (progress >= 1.0 - 1.0e-9) {
        bottom = worldBottom;
        top = worldTop;
        return;
    }
    const int columns = mesh.columns;
    bottom.assign(columns, Vector3{});
    top.assign(columns, Vector3{});
    bottom[0] = worldBottom[0];
    top[0] = worldTop[0];
    if (columns < 2) {
        return;
    }
    bottom[1] = worldBottom[1]; // 先頭三角形 (B0,T0,B1) はアンカーとして world のまま。
    for (int column = 1; column < columns; ++column) {
        // 三角形B: 頂点 T_c を辺 (T_{c-1}, B_c) まわりに置く(前三角形の第3頂点=B_{c-1})。
        {
            const HingePlacement placement = MeasureHinge(
                worldTop[column - 1], worldBottom[column],
                worldTop[column], worldBottom[column - 1]);
            top[column] = PlaceOnHinge(
                top[column - 1], bottom[column], bottom[column - 1],
                placement, placement.bendAngle * progress);
        }
        // 三角形A(次列): 頂点 B_{c+1} を辺 (B_c, T_c) まわりに置く(前=T_{c-1})。
        if (column + 1 < columns) {
            const HingePlacement placement = MeasureHinge(
                worldBottom[column], worldTop[column],
                worldBottom[column + 1], worldTop[column - 1]);
            bottom[column + 1] = PlaceOnHinge(
                bottom[column], top[column], top[column - 1],
                placement, placement.bendAngle * progress);
        }
    }
}

//! 3点 (origin, xRef, yRef) から正規直交フレームを作る。
struct PointFrame {
    Vector3 origin;
    Vector3 axisX{1.0, 0.0, 0.0};
    Vector3 axisY{0.0, 1.0, 0.0};
    Vector3 axisZ{0.0, 0.0, 1.0};
};

[[nodiscard]] PointFrame MakePointFrame(
    const Vector3& origin, const Vector3& xReference, const Vector3& yReference)
{
    PointFrame frame;
    frame.origin = origin;
    const Vector3 x = Normalized(xReference - origin);
    if (x.Length() > 1.0e-9) {
        frame.axisX = x;
    }
    Vector3 y = yReference - origin;
    y = y - frame.axisX * Dot(y, frame.axisX);
    const Vector3 yUnit = Normalized(y);
    if (yUnit.Length() > 1.0e-9) {
        frame.axisY = yUnit;
    } else {
        frame.axisY = Normalized(Cross(frame.axisX, Vector3{0.0, 0.0, 1.0}));
        if (frame.axisY.Length() <= 1.0e-9) {
            frame.axisY = Normalized(Cross(frame.axisX, Vector3{0.0, 1.0, 0.0}));
        }
    }
    frame.axisZ = Cross(frame.axisX, frame.axisY);
    return frame;
}

//! from フレームを to フレームへ写す剛体変換を rows の全点に適用する。
void AlignFrames(
    const PointFrame& from,
    const PointFrame& to,
    std::vector<std::vector<Vector3>*> rows)
{
    for (std::vector<Vector3>* row : rows) {
        for (Vector3& point : *row) {
            const Vector3 relative = point - from.origin;
            const double a = Dot(relative, from.axisX);
            const double b = Dot(relative, from.axisY);
            const double c = Dot(relative, from.axisZ);
            point = to.origin + to.axisX * a + to.axisY * b + to.axisZ * c;
        }
    }
}

//! 点をロドリゲスの公式で軸まわりに回転する。
[[nodiscard]] Vector3 RotatePointAboutAxis(
    const Vector3& point, const Vector3& origin, const Vector3& axis, double angle)
{
    const Vector3 relative = point - origin;
    const double c = std::cos(angle);
    const double sn = std::sin(angle);
    return origin + relative * c + Cross(axis, relative) * sn
        + axis * (Dot(axis, relative) * (1.0 - c));
}

} // namespace

std::vector<std::vector<Vector3>> BuildFoldPreview(
    const PartMeshDevelopment& mesh,
    double progress)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    if (mesh.rows < 2 || mesh.columns < 2) {
        return mesh.world;
    }
    if (t >= 1.0 - 1.0e-9) {
        return mesh.world;
    }
    if (t <= 1.0e-9) {
        // 厳密な展開平面配置(型紙そのもの)。行0中央の位置・向きへ剛体で置く。
        std::vector<std::vector<Vector3>> result(
            mesh.rows, std::vector<Vector3>(mesh.columns, Vector3{}));
        const int anchorColumn = mesh.columns / 2;
        const int nextColumn = std::min(anchorColumn + 1, mesh.columns - 1);
        const Vector3 worldAnchor = mesh.world[0][anchorColumn];
        const Vector3 worldTangent
            = Normalized(mesh.world[0][nextColumn] - mesh.world[0][anchorColumn]);
        const Vector3 worldUp = mesh.world[1][anchorColumn] - mesh.world[0][anchorColumn];
        Vector3 worldNormal = Normalized(Cross(worldTangent, worldUp));
        if (worldNormal.Length() <= 1.0e-9) {
            worldNormal = {0.0, 0.0, 1.0};
        }
        const Vector3 worldSide = Normalized(Cross(worldNormal, worldTangent));
        const Vector2 developedAnchor = mesh.developed[0][anchorColumn];
        const Vector2 developedNext = mesh.developed[0][nextColumn];
        double axisX = developedNext.x - developedAnchor.x;
        double axisY = developedNext.y - developedAnchor.y;
        const double axisLength = std::sqrt(axisX * axisX + axisY * axisY);
        if (axisLength > 1.0e-12) {
            axisX /= axisLength;
            axisY /= axisLength;
        } else {
            axisX = 1.0;
            axisY = 0.0;
        }
        for (int row = 0; row < mesh.rows; ++row) {
            for (int column = 0; column < mesh.columns; ++column) {
                const Vector2& flat = mesh.developed[row][column];
                const double dx = flat.x - developedAnchor.x;
                const double dy = flat.y - developedAnchor.y;
                const double along = dx * axisX + dy * axisY;
                const double side = -dx * axisY + dy * axisX;
                result[row][column] = worldAnchor
                    + worldTangent * along + worldSide * side;
            }
        }
        return result;
    }

    // 中間: 帯ごとに等長で曲げ(BendBandStrip)、共有レールで順に剛体接続し、
    // 帯間の折り角(world の値)も progress 倍する。帯の中は常に厳密な等長。
    std::vector<std::vector<Vector3>> result(mesh.rows);
    std::vector<Vector3> bottom;
    std::vector<Vector3> top;
    BendBandStrip(mesh, 0, t, bottom, top);
    result[0] = std::move(bottom);
    result[1] = std::move(top);
    const int anchorColumn = mesh.columns / 2;
    for (int band = 1; band + 1 < mesh.rows; ++band) {
        BendBandStrip(mesh, band, t, bottom, top);
        // 下レールを配置済みの共有レールへ剛体で合わせる。
        const std::vector<Vector3>& placed = result[band];
        const PointFrame from = MakePointFrame(
            bottom.front(), bottom.back(), bottom[anchorColumn]);
        const PointFrame to = MakePointFrame(
            placed.front(), placed.back(), placed[anchorColumn]);
        AlignFrames(from, to, {&bottom, &top});
        // 帯間の折り角も progress 倍: world の折り角 θ に対し (t-1)θ を追加回転。
        const double fullAngle
            = MeasureCreaseAngleInState(mesh.world, band, mesh.columns);
        const double delta = (t - 1.0) * fullAngle;
        if (std::abs(delta) > 1.0e-12) {
            const Vector3 chordOrigin = placed.front();
            const Vector3 chord = Normalized(placed.back() - placed.front());
            if (chord.Length() > 1.0e-9) {
                for (Vector3& point : top) {
                    point = RotatePointAboutAxis(point, chordOrigin, chord, delta);
                }
            }
        }
        result[band + 1] = std::move(top);
    }
    return result;
}

namespace {

//! ベクトルから axis 成分を除いた射影(レール直交平面への射影)。
[[nodiscard]] Vector3 RejectFromAxis(const Vector3& value, const Vector3& axis)
{
    return value - axis * Dot(value, axis);
}

//! 状態 state 上のレール row の弦軸(始点と単位方向)。
struct RailChord {
    Vector3 origin;
    Vector3 direction;
    bool valid = false;
};

[[nodiscard]] RailChord MeasureRailChord(
    const std::vector<std::vector<Vector3>>& state, int row, int columns)
{
    RailChord chord;
    chord.origin = state[row][0];
    const Vector3 span = state[row][columns - 1] - state[row][0];
    const double length = span.Length();
    if (length <= 1.0e-9) {
        return chord;
    }
    chord.direction = span * (1.0 / length);
    chord.valid = true;
    return chord;
}

//! 状態 state における内部レール row の平均折り角(符号付き、0=平ら)。
//! 弦軸まわりで「前の帯の延長」から「次の帯」までの角度を列ごとに測って平均する。
[[nodiscard]] double MeasureCreaseAngleInState(
    const std::vector<std::vector<Vector3>>& state, int row, int columns)
{
    const RailChord chord = MeasureRailChord(state, row, columns);
    if (!chord.valid) {
        return 0.0;
    }
    double sinSum = 0.0;
    double cosSum = 0.0;
    int samples = 0;
    for (int column = 0; column < columns; column += 3) {
        const Vector3 toPrevious = RejectFromAxis(
            state[row - 1][column] - state[row][column], chord.direction);
        const Vector3 toNext = RejectFromAxis(
            state[row + 1][column] - state[row][column], chord.direction);
        const double previousLength = toPrevious.Length();
        const double nextLength = toNext.Length();
        if (previousLength <= 1.0e-9 || nextLength <= 1.0e-9) {
            continue;
        }
        // 平ら=次の帯が前の帯の延長(-toPrevious)方向。そこからのずれが折り角。
        const Vector3 straight = toPrevious * (-1.0 / previousLength);
        const Vector3 next = toNext * (1.0 / nextLength);
        const double sinValue = Dot(Cross(straight, next), chord.direction);
        const double cosValue = Dot(straight, next);
        sinSum += sinValue;
        cosSum += cosValue;
        ++samples;
    }
    if (samples == 0) {
        return 0.0;
    }
    return std::atan2(sinSum, cosSum);
}

} // namespace

std::vector<double> MeasureCreaseAngles(const PartMeshDevelopment& mesh)
{
    std::vector<double> angles(std::max(0, mesh.rows - 2), 0.0);
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        angles[rail - 1] = MeasureCreaseAngleInState(mesh.world, rail, mesh.columns);
    }
    return angles;
}

namespace {

//! 回転行列(行ベクトル3本)をロドリゲスの公式から作る。
[[nodiscard]] std::array<Vector3, 3> RotationRowsAboutAxis(
    const Vector3& axis, double angle)
{
    const double c = std::cos(angle);
    const double sn = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis.x;
    const double y = axis.y;
    const double z = axis.z;
    return {
        Vector3{t * x * x + c, t * x * y - sn * z, t * x * z + sn * y},
        Vector3{t * x * y + sn * z, t * y * y + c, t * y * z - sn * x},
        Vector3{t * x * z - sn * y, t * y * z + sn * x, t * z * z + c},
    };
}

//! transform の後に「(origin, axis) まわりの angle 回転」を掛けた変換を返す。
//! result(p) = Rot(transform(p)) = R*(B*p + t - origin) + origin。
[[nodiscard]] PartBandTransform ComposeRotationAfter(
    const PartBandTransform& transform,
    const Vector3& origin,
    const Vector3& axis,
    double angle)
{
    const std::array<Vector3, 3> rows = RotationRowsAboutAxis(axis, angle);
    const auto rotate = [&rows](const Vector3& value) {
        return Vector3{Dot(rows[0], value), Dot(rows[1], value), Dot(rows[2], value)};
    };
    PartBandTransform result;
    // 回転部: R*B(列ベクトルの合成)。行ベクトル表現では
    // result行i = (Bの各行を列とみなした行列に rows[i] を掛けたもの)。
    const Vector3 columnX{
        transform.rotationRowX.x, transform.rotationRowY.x, transform.rotationRowZ.x};
    const Vector3 columnY{
        transform.rotationRowX.y, transform.rotationRowY.y, transform.rotationRowZ.y};
    const Vector3 columnZ{
        transform.rotationRowX.z, transform.rotationRowY.z, transform.rotationRowZ.z};
    const Vector3 newColumnX = rotate(columnX);
    const Vector3 newColumnY = rotate(columnY);
    const Vector3 newColumnZ = rotate(columnZ);
    result.rotationRowX = {newColumnX.x, newColumnY.x, newColumnZ.x};
    result.rotationRowY = {newColumnX.y, newColumnY.y, newColumnZ.y};
    result.rotationRowZ = {newColumnX.z, newColumnY.z, newColumnZ.z};
    result.translation = rotate(transform.translation - origin) + origin;
    return result;
}

} // namespace

std::vector<PartBandTransform> BuildRigidBandTransforms(
    const PartMeshDevelopment& mesh,
    const std::vector<double>& creaseProgress)
{
    if (static_cast<int>(creaseProgress.size()) != std::max(0, mesh.rows - 2)) {
        throw std::invalid_argument("折り線の進行度の数が折り線の本数と一致していません。");
    }
    for (const double value : creaseProgress) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("折り線の進行度は有限の値で指定してください。");
        }
    }
    const int bandCount = std::max(0, mesh.rows - 1);
    std::vector<PartBandTransform> transforms(
        static_cast<std::size_t>(bandCount));
    // 帯0は固定。帯bは「帯b-1の変換」に、レールb(帯b-1との折り線)まわりの
    // 追加回転 (t-1)θ を掛けたもの。θ・弦軸は world で測り、軸は前帯の変換で写す。
    for (int band = 1; band < bandCount; ++band) {
        const int rail = band;
        const PartBandTransform& previous = transforms[band - 1];
        transforms[band] = previous;
        const double progress = creaseProgress[rail - 1];
        if (std::abs(progress - 1.0) <= 1.0e-12) {
            continue;
        }
        const RailChord chord = MeasureRailChord(mesh.world, rail, mesh.columns);
        if (!chord.valid) {
            continue;
        }
        const double fullAngle = MeasureCreaseAngleInState(mesh.world, rail, mesh.columns);
        const double delta = (progress - 1.0) * fullAngle;
        if (std::abs(delta) <= 1.0e-12) {
            continue;
        }
        transforms[band] = ComposeRotationAfter(
            previous,
            previous.Apply(chord.origin),
            Normalized(previous.RotateVector(chord.direction)),
            delta);
    }
    return transforms;
}

std::vector<std::vector<Vector3>> BuildBandFoldAnimationRails(
    const PartMeshDevelopment& mesh,
    const std::vector<double>& creaseProgress,
    double assemblyProgress,
    double liftDistanceMillimeters)
{
    return BuildBandFoldAnimationRails(
        mesh, creaseProgress, std::vector<double>{assemblyProgress},
        liftDistanceMillimeters);
}

std::vector<std::vector<Vector3>> BuildBandFoldAnimationRails(
    const PartMeshDevelopment& mesh,
    const std::vector<double>& creaseProgress,
    const std::vector<double>& bandAssemblyProgress,
    double liftDistanceMillimeters)
{
    const int bandCount = std::max(0, mesh.rows - 1);
    std::vector<std::vector<Vector3>> rails;
    rails.reserve(static_cast<std::size_t>(bandCount) * 2);
    if (bandCount == 0 || mesh.columns < 2) {
        return rails;
    }
    // 帯ごとの進行度(オーナー指示: 選んだ部材だけが曲がる)。不足分は最後の値。
    const auto bandProgress = [&](int band) {
        if (bandAssemblyProgress.empty()) {
            return 1.0;
        }
        const std::size_t index = std::min(
            static_cast<std::size_t>(band), bandAssemblyProgress.size() - 1);
        return std::clamp(bandAssemblyProgress[index], 0.0, 1.0);
    };
    // 帯間の折り角(可動折り線の個別値)も帯の t で補間した剛体連鎖を使う。
    // 各帯は独立した剛体なので、帯ごとに自分の t で連鎖を評価してよい
    // (全帯が同じ t なら従来と完全に一致する)。t ごとに連鎖を作り直す。
    double cachedProgress = std::numeric_limits<double>::quiet_NaN();
    std::vector<PartBandTransform> transforms;
    const auto transformsFor = [&](double t) -> const std::vector<PartBandTransform>& {
        if (!(t == cachedProgress)) {
            std::vector<double> interpolated(creaseProgress.size(), 1.0);
            for (std::size_t index = 0; index < creaseProgress.size(); ++index) {
                interpolated[index] = 1.0 + t * (creaseProgress[index] - 1.0);
            }
            transforms = BuildRigidBandTransforms(mesh, interpolated);
            cachedProgress = t;
        }
        return transforms;
    };

    // モデル重心(展開位置を外向きへ離す向きの判定に使う)。
    Vector3 centroid{0.0, 0.0, 0.0};
    for (int row = 0; row < mesh.rows; ++row) {
        for (int column = 0; column < mesh.columns; ++column) {
            centroid = centroid + mesh.world[row][column];
        }
    }
    centroid = centroid * (1.0 / (mesh.rows * mesh.columns));

    const int anchorColumn = mesh.columns / 2;
    std::vector<Vector3> bottom;
    std::vector<Vector3> top;
    for (int band = 0; band < bandCount; ++band) {
        const double t = bandProgress(band);
        // 等長の曲げ(三角形剛体+二面角×t)。t=1 で world と厳密一致。
        BendBandStrip(mesh, band, t, bottom, top);
        // 中央素線を world の中央素線へ剛体で合わせ、帯を元の位置周辺に保つ。
        const PointFrame from = MakePointFrame(
            bottom[anchorColumn], top[anchorColumn],
            bottom[std::min(anchorColumn + 1, mesh.columns - 1)]);
        const PointFrame to = MakePointFrame(
            mesh.world[band][anchorColumn], mesh.world[band + 1][anchorColumn],
            mesh.world[band][std::min(anchorColumn + 1, mesh.columns - 1)]);
        AlignFrames(from, to, {&bottom, &top});
        // 外向きの持ち上げ((1-t) で減衰)。
        const Vector3 worldTangent = Normalized(
            mesh.world[band][std::min(anchorColumn + 1, mesh.columns - 1)]
            - mesh.world[band][anchorColumn]);
        const Vector3 worldUp
            = mesh.world[band + 1][anchorColumn] - mesh.world[band][anchorColumn];
        Vector3 normal = Normalized(Cross(worldTangent, worldUp));
        if (normal.Length() <= 1.0e-9) {
            normal = {0.0, 0.0, 1.0};
        }
        const Vector3 bandCenter = (mesh.world[band][anchorColumn]
            + mesh.world[band + 1][anchorColumn]) * 0.5;
        if (Dot(normal, bandCenter - centroid) < 0.0) {
            normal = normal * -1.0;
        }
        const Vector3 lift = normal * (liftDistanceMillimeters * (1.0 - t));
        const PartBandTransform& transform
            = transformsFor(t)[static_cast<std::size_t>(band)];
        for (Vector3& point : bottom) {
            point = transform.Apply(point) + lift;
        }
        for (Vector3& point : top) {
            point = transform.Apply(point) + lift;
        }
        rails.push_back(bottom);
        rails.push_back(top);
    }
    return rails;
}

PartMeshMappedPoint MapPointToPartMeshState(
    const PartMeshDevelopment& mesh,
    const std::vector<std::vector<Vector3>>& state,
    const Vector3& point)
{
    if (mesh.rows < 2 || mesh.columns < 2
        || static_cast<int>(state.size()) != mesh.rows) {
        throw std::invalid_argument("近似メッシュと状態の位相が一致していません。");
    }
    PartMeshMappedPoint best;
    best.distanceMillimeters = std::numeric_limits<double>::max();
    // 三角形への最近点(重心座標)。標準的な閉形式(Ericson)。
    const auto consider = [&](int band,
                              const Vector3& a3, const Vector3& b3, const Vector3& c3,
                              const Vector3& aT, const Vector3& bT, const Vector3& cT) {
        const Vector3 ab = b3 - a3;
        const Vector3 ac = c3 - a3;
        const Vector3 ap = point - a3;
        const double d1 = Dot(ab, ap);
        const double d2 = Dot(ac, ap);
        double v = 0.0;
        double w = 0.0;
        do {
            if (d1 <= 0.0 && d2 <= 0.0) {
                break;
            }
            const Vector3 bp = point - b3;
            const double d3 = Dot(ab, bp);
            const double d4 = Dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3) {
                v = 1.0;
                break;
            }
            const Vector3 cp = point - c3;
            const double d5 = Dot(ab, cp);
            const double d6 = Dot(ac, cp);
            if (d6 >= 0.0 && d5 <= d6) {
                w = 1.0;
                break;
            }
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                v = d1 / (d1 - d3);
                break;
            }
            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                w = d2 / (d2 - d6);
                break;
            }
            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
                const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                v = 1.0 - t;
                w = t;
                break;
            }
            const double denominator = va + vb + vc;
            if (std::abs(denominator) > 1.0e-18) {
                v = vb / denominator;
                w = vc / denominator;
            }
        } while (false);
        const Vector3 closest = a3 + ab * v + ac * w;
        const double distance = (point - closest).Length();
        if (distance < best.distanceMillimeters) {
            best.distanceMillimeters = distance;
            best.band = band;
            best.point = aT + (bT - aT) * v + (cT - aT) * w;
        }
    };
    for (int band = 0; band + 1 < mesh.rows; ++band) {
        const auto& bottom3 = mesh.world[band];
        const auto& top3 = mesh.world[band + 1];
        const auto& bottomT = state[band];
        const auto& topT = state[band + 1];
        for (int column = 0; column + 1 < mesh.columns; ++column) {
            consider(band,
                bottom3[column], top3[column], bottom3[column + 1],
                bottomT[column], topT[column], bottomT[column + 1]);
            consider(band,
                top3[column], bottom3[column + 1], top3[column + 1],
                topT[column], bottomT[column + 1], topT[column + 1]);
        }
    }
    return best;
}

} // namespace kachakacha::model
