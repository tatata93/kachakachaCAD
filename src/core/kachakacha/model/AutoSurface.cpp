#include "kachakacha/model/AutoSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace kachakacha::model {

namespace {

using geometry::Vector3;

//! 連結後の1本の線(元ワイヤ1本ならそのまま、複数なら折れ線として合成)。
struct Chain {
    Wire geometry;
    bool closed = false;
    int sourceCount = 1;
    double bridgedGap = 0.0; //!< 自動で閉じた隙間の最大値(mm)
};

[[nodiscard]] double BoundingDiagonal(const std::vector<Wire>& wires)
{
    Vector3 low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    Vector3 high{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
    for (const Wire& wire : wires) {
        for (int sample = 0; sample <= 16; ++sample) {
            const Vector3 point = wire.Evaluate(sample / 16.0);
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        }
    }
    return (high - low).Length();
}

//! ワイヤ列(向きフラグ付き)を折れ線1本へ合成する。隙間はそのまま直線になる。
[[nodiscard]] Wire JoinAsPolyline(
    const std::vector<Wire>& wires,
    const std::vector<std::pair<std::size_t, bool>>& order,
    bool close)
{
    // 複数の線を1本へ合成するときの分解能。元の線からのずれを目に見えない
    // 大きさに抑える(1本だけの鎖は合成せず元の線をそのまま使う=正確に通る)。
    constexpr int kSamplesPerWire = 128;
    std::vector<Vector3> points;
    points.reserve(order.size() * (kSamplesPerWire + 1) + 1);
    for (const auto& [index, reversed] : order) {
        const Wire& wire = wires[index];
        for (int sample = 0; sample <= kSamplesPerWire; ++sample) {
            const double t = static_cast<double>(sample) / kSamplesPerWire;
            const Vector3 point = wire.Evaluate(reversed ? 1.0 - t : t);
            if (!points.empty() && (point - points.back()).Length() <= 1.0e-9) {
                continue;
            }
            points.push_back(point);
        }
    }
    if (close && !points.empty()
        && (points.front() - points.back()).Length() > 1.0e-9) {
        points.push_back(points.front());
    }
    if (points.size() < 2) {
        throw std::invalid_argument("面にできる長さの線がありません。");
    }
    return Wire::Polyline(std::move(points));
}

//! 端点の近さで貪欲に連結した鎖を作る(向き自動・許容隙間あり)。
[[nodiscard]] std::vector<Chain> BuildChains(
    const std::vector<Wire>& wires, double tolerance)
{
    std::vector<bool> used(wires.size(), false);
    std::vector<Chain> chains;
    for (std::size_t seed = 0; seed < wires.size(); ++seed) {
        if (used[seed]) {
            continue;
        }
        used[seed] = true;
        if (wires[seed].IsClosed(1.0e-6)) {
            chains.push_back(Chain{wires[seed], true, 1, 0.0});
            continue;
        }
        std::vector<std::pair<std::size_t, bool>> order{{seed, false}};
        double bridged = 0.0;
        const auto endpointOf = [&](const std::pair<std::size_t, bool>& entry, bool tail) {
            const Wire& wire = wires[entry.first];
            const bool atEnd = tail != entry.second; // 反転していれば逆側。
            return atEnd ? wire.End() : wire.Start();
        };
        bool grew = true;
        while (grew) {
            grew = false;
            for (const bool tail : {true, false}) {
                const Vector3 endpoint = endpointOf(tail ? order.back() : order.front(), tail);
                std::size_t bestIndex = wires.size();
                bool bestReversed = false;
                double bestDistance = tolerance;
                for (std::size_t index = 0; index < wires.size(); ++index) {
                    if (used[index] || wires[index].IsClosed(1.0e-6)) {
                        continue;
                    }
                    const double toStart = (wires[index].Start() - endpoint).Length();
                    const double toEnd = (wires[index].End() - endpoint).Length();
                    if (std::min(toStart, toEnd) <= bestDistance) {
                        bestDistance = std::min(toStart, toEnd);
                        bestIndex = index;
                        // 末尾へ繋ぐなら「近い側が新しい線の始点」になる向き。
                        bestReversed = tail ? toEnd < toStart : toStart < toEnd;
                    }
                }
                if (bestIndex >= wires.size()) {
                    continue;
                }
                used[bestIndex] = true;
                bridged = std::max(bridged, bestDistance);
                if (tail) {
                    order.emplace_back(bestIndex, bestReversed);
                } else {
                    order.insert(order.begin(), {bestIndex, bestReversed});
                }
                grew = true;
            }
        }
        const Vector3 head = endpointOf(order.front(), false);
        const Vector3 tail = endpointOf(order.back(), true);
        const double closingGap = (head - tail).Length();
        const bool closed = order.size() >= 2 ? closingGap <= tolerance : false;
        Wire geometry = order.size() == 1 && !closed
            ? wires[order.front().first]
            : JoinAsPolyline(wires, order, closed);
        chains.push_back(Chain{std::move(geometry), closed,
            static_cast<int>(order.size()),
            std::max(bridged, closed ? closingGap : 0.0)});
    }
    return chains;
}

//! 断面全体の対応距離(向き判定用)。端点が対称な開断面や、
//! 端点が一致する閉断面でも正しくねじれない向きを選べる。
[[nodiscard]] double OrientationCost(
    const Wire& previous, const Wire& candidate, bool reversed)
{
    double cost = 0.0;
    for (int sample = 0; sample <= 16; ++sample) {
        const double u = static_cast<double>(sample) / 16.0;
        cost += (previous.Evaluate(u)
            - candidate.Evaluate(reversed ? 1.0 - u : u)).Length();
    }
    return cost;
}

//! 閉じた断面のシーム(始点)を target に最も近い位置へ回す(ねじれ防止)。
//! 円と閉じた折れ線は正確に作り直す。その他の形はそのまま返す(向きのみ揃う)。
[[nodiscard]] Wire ReseamClosedWire(const Wire& wire, const Vector3& target)
{
    if (wire.Kind() == WireKind::Circle) {
        const auto arc = wire.ArcData();
        const Vector3 normal = Cross(arc.uAxis, arc.vAxis);
        if (normal.LengthSquared() <= 1.0e-18) {
            return wire;
        }
        Vector3 direction = target - arc.center;
        direction = direction - normal * (Dot(direction, normal) / normal.LengthSquared());
        if (direction.LengthSquared() <= 1.0e-18) {
            return wire;
        }
        const Vector3 newUAxis = direction.Normalized();
        const Vector3 newVAxis = Cross(normal, newUAxis).Normalized();
        return Wire::Circle(arc.center, newUAxis, newVAxis, arc.radius);
    }
    if (wire.Kind() == WireKind::Polyline && wire.IsClosed(1.0e-9)) {
        const auto& points = wire.ControlPoints();
        if (points.size() < 4) {
            return wire;
        }
        // 並びは 先頭==末尾。target へ最も近い頂点を新しい始点にする。
        const std::size_t vertexCount = points.size() - 1;
        std::size_t best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index < vertexCount; ++index) {
            const double distance = (points[index] - target).Length();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        if (best == 0) {
            return wire;
        }
        std::vector<Vector3> rotated;
        rotated.reserve(points.size());
        for (std::size_t offset = 0; offset < vertexCount; ++offset) {
            rotated.push_back(points[(best + offset) % vertexCount]);
        }
        rotated.push_back(rotated.front());
        return Wire::Polyline(std::move(rotated));
    }
    return wire;
}

//! 直前の断面に向きとシームを合わせる(ねじれ防止 — オーナー指示)。
[[nodiscard]] Wire AlignSectionToPrevious(const Wire& previous, Wire section)
{
    if (OrientationCost(previous, section, true) + 1.0e-9
        < OrientationCost(previous, section, false)) {
        section = section.Reversed();
    }
    if (section.IsClosed(1.0e-6) && previous.IsClosed(1.0e-6)) {
        section = ReseamClosedWire(section, previous.Start());
        // シームを回した後にもう一度向きを確かめる。
        if (OrientationCost(previous, section, true) + 1.0e-9
            < OrientationCost(previous, section, false)) {
            section = section.Reversed();
        }
    }
    return section;
}

[[nodiscard]] Vector3 ChainCentroid(const Wire& wire)
{
    Vector3 sum{0.0, 0.0, 0.0};
    constexpr int kSamples = 16;
    for (int sample = 0; sample <= kSamples; ++sample) {
        sum = sum + wire.Evaluate(static_cast<double>(sample) / kSamples);
    }
    return sum * (1.0 / (kSamples + 1));
}

[[nodiscard]] std::string FormatMillimeters(double value)
{
    std::ostringstream stream;
    stream.precision(1);
    stream << std::fixed << value;
    return stream.str();
}

} // namespace

AutoSurfaceResult BuildAutoSurface(const std::vector<Wire>& wires)
{
    if (wires.empty()) {
        throw std::invalid_argument("面にする線を1本以上選択してください。");
    }
    const double diagonal = BoundingDiagonal(wires);
    // 「小さな隙間は自動で閉じる」— 模型スケールで1mm程度の隙間は繋がっている
    // とみなす(断面どうしは通常これより離れているため誤結合しにくい)。
    const double tolerance = std::clamp(diagonal * 0.02, 1.0, 5.0);
    std::vector<Chain> chains = BuildChains(wires, tolerance);

    double bridgedGap = 0.0;
    for (const Chain& chain : chains) {
        bridgedGap = std::max(bridgedGap, chain.bridgedGap);
    }
    const std::string bridgedNote = bridgedGap > 1.0e-6
        ? "（隙間 " + FormatMillimeters(bridgedGap) + " mm を自動で閉じました）"
        : std::string();

    // 1本の閉ループ → 平面、だめならパッチ(穴埋め)。
    // 平面判定は厳しくする: わずかでも平面から外れる輪郭を平面に押し込むと
    // 「選んだ線を通らない面」ができるため、その場合はパッチで正確に通す。
    if (chains.size() == 1 && chains.front().closed) {
        try {
            Surface planar = Surface::Planar(chains.front().geometry, 1.0e-4);
            return {std::move(planar), "閉じた輪郭から平面" + bridgedNote};
        } catch (const std::exception&) {
        }
        Surface patch = Surface::Patch(chains.front().geometry);
        return {std::move(patch), "閉じた輪郭からパッチ面(穴埋め)" + bridgedNote};
    }
    if (chains.size() == 1) {
        const Wire& geometry = chains.front().geometry;
        const double gap = (geometry.Start() - geometry.End()).Length();
        throw std::invalid_argument(
            "選んだ線は1本につながりましたが、端が閉じていません"
            "（始点と終点の距離 " + FormatMillimeters(gap) + " mm）。\n"
            "輪郭を閉じるか、向かい側になる断面の線を追加で選んでください。");
    }

    // 2本 → ルールド。向きとシーム(閉断面)は線全体の対応で揃える(ねじれ防止)。
    if (chains.size() == 2) {
        Wire first = chains[0].geometry;
        Wire second = AlignSectionToPrevious(first, chains[1].geometry);
        Surface ruled = Surface::Ruled(std::move(first), std::move(second));
        return {std::move(ruled), "2本の断面からルールド面" + bridgedNote};
    }

    // 3本以上 → 中心位置で自動整列してロフト。
    std::vector<std::size_t> indices(chains.size());
    for (std::size_t index = 0; index < chains.size(); ++index) {
        indices[index] = index;
    }
    std::vector<Vector3> centroids(chains.size());
    for (std::size_t index = 0; index < chains.size(); ++index) {
        centroids[index] = ChainCentroid(chains[index].geometry);
    }
    // 最も離れた2つの中心を軸にして、その射影で並べ替える。
    std::size_t axisA = 0;
    std::size_t axisB = 1;
    double farthest = -1.0;
    for (std::size_t a = 0; a < centroids.size(); ++a) {
        for (std::size_t b = a + 1; b < centroids.size(); ++b) {
            const double distance = (centroids[b] - centroids[a]).Length();
            if (distance > farthest) {
                farthest = distance;
                axisA = a;
                axisB = b;
            }
        }
    }
    if (farthest <= 1.0e-9) {
        throw std::invalid_argument(
            "断面どうしの位置が重なっていて並び順を決められません。");
    }
    const Vector3 axis = (centroids[axisB] - centroids[axisA]).Normalized();
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        return Dot(centroids[a] - centroids[axisA], axis)
            < Dot(centroids[b] - centroids[axisA], axis);
    });
    std::vector<Wire> sections;
    sections.reserve(indices.size());
    for (const std::size_t index : indices) {
        Wire section = chains[index].geometry;
        if (!sections.empty()) {
            section = AlignSectionToPrevious(sections.back(), std::move(section));
        }
        sections.push_back(std::move(section));
    }
    const std::size_t sectionCount = sections.size();
    Surface loft = Surface::Loft(std::move(sections));
    return {std::move(loft),
        std::to_string(sectionCount) + "本の断面を自動整列してロフト面" + bridgedNote};
}

} // namespace kachakacha::model
