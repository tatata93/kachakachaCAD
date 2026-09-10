#include "kachakacha/fabrication/BandFold.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;
using Rows = std::vector<std::vector<Vector3>>;

//! ヒンジ(共有辺 P→Q)まわりの頂点配置の分解。
//! 辺方向位置 along、辺からの距離 height、
//! 「前三角形の反対側(=平ら)」を 0 とする符号付き曲げ角 bendAngle。
struct HingePlacement {
    double along = 0.0;
    double height = 0.0;
    double bendAngle = 0.0;
};

//! world 上の三角形 (P,Q,V) と前三角形の第3頂点 prev からヒンジ分解を測る。
[[nodiscard]] HingePlacement MeasureHinge(const Vector3& worldP, const Vector3& worldQ,
    const Vector3& worldV, const Vector3& worldPrev)
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

//! 配置済みのヒンジ (P,Q) と前三角形第3頂点から、曲げ角 angle で頂点を置く。
//! 辺長(along/height)は world から測った値そのものを使うので、三角形は厳密に剛体。
[[nodiscard]] Vector3 PlaceOnHinge(const Vector3& placedP, const Vector3& placedQ,
    const Vector3& placedPrev, const HingePlacement& placement, double angle)
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
void BendBandStrip(const BandMesh& mesh, int band, double progress,
    std::vector<Vector3>& bottom, std::vector<Vector3>& top)
{
    const auto& worldBottom = mesh.world[static_cast<std::size_t>(band)];
    const auto& worldTop = mesh.world[static_cast<std::size_t>(band) + 1];
    if (progress >= 1.0 - 1.0e-9) {
        bottom = worldBottom;
        top = worldTop;
        return;
    }
    const std::size_t columns = static_cast<std::size_t>(mesh.columns);
    bottom.assign(columns, Vector3{});
    top.assign(columns, Vector3{});
    bottom[0] = worldBottom[0];
    top[0] = worldTop[0];
    if (columns < 2) {
        return;
    }
    bottom[1] = worldBottom[1]; // 先頭三角形 (B0,T0,B1) はアンカーとして world のまま。
    for (std::size_t column = 1; column < columns; ++column) {
        // 三角形B: 頂点 T_c を辺 (T_{c-1}, B_c) まわりに置く(前三角形の第3頂点=B_{c-1})。
        {
            const HingePlacement placement = MeasureHinge(worldTop[column - 1],
                worldBottom[column], worldTop[column], worldBottom[column - 1]);
            top[column] = PlaceOnHinge(top[column - 1], bottom[column], bottom[column - 1],
                placement, placement.bendAngle * progress);
        }
        // 三角形A(次列): 頂点 B_{c+1} を辺 (B_c, T_c) まわりに置く(前=T_{c-1})。
        if (column + 1 < columns) {
            const HingePlacement placement = MeasureHinge(worldBottom[column],
                worldTop[column], worldBottom[column + 1], worldTop[column - 1]);
            bottom[column + 1] = PlaceOnHinge(bottom[column], top[column],
                top[column - 1], placement, placement.bendAngle * progress);
        }
    }
}

//! 3点 (origin, xRef, yRef) から正規直交フレームを作る。
struct PointFrame {
    Vector3 origin{};
    Vector3 axisX{1.0, 0.0, 0.0};
    Vector3 axisY{0.0, 1.0, 0.0};
    Vector3 axisZ{0.0, 0.0, 1.0};
};

[[nodiscard]] PointFrame MakePointFrame(const Vector3& origin, const Vector3& xReference,
    const Vector3& yReference)
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

//! from フレームを to フレームへ写す剛体変換を、rows の全点に掛ける。
void AlignFrames(const PointFrame& from, const PointFrame& to,
    std::vector<std::vector<Vector3>*> rows)
{
    for (std::vector<Vector3>* row : rows) {
        for (Vector3& point : *row) {
            const Vector3 relative = point - from.origin;
            point = to.origin + to.axisX * Dot(relative, from.axisX)
                + to.axisY * Dot(relative, from.axisY)
                + to.axisZ * Dot(relative, from.axisZ);
        }
    }
}

//! 点をロドリゲスの公式で軸まわりに回転する。
[[nodiscard]] Vector3 RotatePointAboutAxis(const Vector3& point, const Vector3& origin,
    const Vector3& axis, double angle)
{
    const Vector3 relative = point - origin;
    const double c = std::cos(angle);
    const double sn = std::sin(angle);
    return origin + relative * c + Cross(axis, relative) * sn
        + axis * (Dot(axis, relative) * (1.0 - c));
}

[[nodiscard]] Vector3 RejectFromAxis(const Vector3& value, const Vector3& axis)
{
    return value - axis * Dot(value, axis);
}

//! 状態 state 上のレール row の弦軸(始点と単位方向)。
struct RailChord {
    Vector3 origin{};
    Vector3 direction{};
    bool valid = false;
};

[[nodiscard]] RailChord MeasureRailChord(const Rows& state, int row, int columns)
{
    RailChord chord;
    const auto& rail = state[static_cast<std::size_t>(row)];
    chord.origin = rail[0];
    const Vector3 span = rail[static_cast<std::size_t>(columns) - 1] - rail[0];
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
[[nodiscard]] double MeasureCreaseAngleInState(const Rows& state, int row, int columns)
{
    const RailChord chord = MeasureRailChord(state, row, columns);
    if (!chord.valid) {
        return 0.0;
    }
    const auto& previousRow = state[static_cast<std::size_t>(row) - 1];
    const auto& rail = state[static_cast<std::size_t>(row)];
    const auto& nextRow = state[static_cast<std::size_t>(row) + 1];
    double sinSum = 0.0;
    double cosSum = 0.0;
    int samples = 0;
    for (std::size_t column = 0; column < static_cast<std::size_t>(columns); column += 3) {
        const Vector3 toPrevious =
            RejectFromAxis(previousRow[column] - rail[column], chord.direction);
        const Vector3 toNext = RejectFromAxis(nextRow[column] - rail[column], chord.direction);
        const double previousLength = toPrevious.Length();
        const double nextLength = toNext.Length();
        if (previousLength <= 1.0e-9 || nextLength <= 1.0e-9) {
            continue;
        }
        // 平ら = 次の帯が前の帯の延長(-toPrevious)方向。そこからのずれが折り角。
        const Vector3 straight = toPrevious * (-1.0 / previousLength);
        const Vector3 next = toNext * (1.0 / nextLength);
        sinSum += Dot(Cross(straight, next), chord.direction);
        cosSum += Dot(straight, next);
        ++samples;
    }
    return samples == 0 ? 0.0 : std::atan2(sinSum, cosSum);
}

//! 回転行列(行ベクトル3本)をロドリゲスの公式から作る。
[[nodiscard]] std::array<Vector3, 3> RotationRowsAboutAxis(const Vector3& axis, double angle)
{
    const double c = std::cos(angle);
    const double sn = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis.x;
    const double y = axis.y;
    const double z = axis.z;
    return {Vector3{t * x * x + c, t * x * y - sn * z, t * x * z + sn * y},
        Vector3{t * x * y + sn * z, t * y * y + c, t * y * z - sn * x},
        Vector3{t * x * z - sn * y, t * y * z + sn * x, t * z * z + c}};
}

//! transform の後に「(origin, axis) まわりの angle 回転」を掛けた変換。
[[nodiscard]] BandTransform ComposeRotationAfter(const BandTransform& transform,
    const Vector3& origin, const Vector3& axis, double angle)
{
    const std::array<Vector3, 3> rows = RotationRowsAboutAxis(axis, angle);
    const auto rotate = [&rows](const Vector3& value) {
        return Vector3{Dot(rows[0], value), Dot(rows[1], value), Dot(rows[2], value)};
    };
    const Vector3 columnX{transform.rotationRowX.x, transform.rotationRowY.x,
        transform.rotationRowZ.x};
    const Vector3 columnY{transform.rotationRowX.y, transform.rotationRowY.y,
        transform.rotationRowZ.y};
    const Vector3 columnZ{transform.rotationRowX.z, transform.rotationRowY.z,
        transform.rotationRowZ.z};
    const Vector3 newX = rotate(columnX);
    const Vector3 newY = rotate(columnY);
    const Vector3 newZ = rotate(columnZ);
    BandTransform result;
    result.rotationRowX = {newX.x, newY.x, newZ.x};
    result.rotationRowY = {newX.y, newY.y, newZ.y};
    result.rotationRowZ = {newX.z, newY.z, newZ.z};
    result.translation = rotate(transform.translation - origin) + origin;
    return result;
}

//! progress=0 のとき: 厳密な展開平面配置(型紙そのもの)を、行0中央の位置・向きへ剛体で置く。
[[nodiscard]] Rows PlaceFlatDevelopment(const BandMesh& mesh)
{
    Rows result(static_cast<std::size_t>(mesh.rows),
        std::vector<Vector3>(static_cast<std::size_t>(mesh.columns), Vector3{}));
    const std::size_t anchorColumn = static_cast<std::size_t>(mesh.columns / 2);
    const std::size_t nextColumn =
        std::min(anchorColumn + 1, static_cast<std::size_t>(mesh.columns) - 1);
    const Vector3 worldAnchor = mesh.world[0][anchorColumn];
    const Vector3 worldTangent = Normalized(mesh.world[0][nextColumn] - worldAnchor);
    const Vector3 worldUp = mesh.world[1][anchorColumn] - worldAnchor;
    Vector3 worldNormal = Normalized(Cross(worldTangent, worldUp));
    if (worldNormal.Length() <= 1.0e-9) {
        worldNormal = {0.0, 0.0, 1.0};
    }
    const Vector3 worldSide = Normalized(Cross(worldNormal, worldTangent));
    const Point2 developedAnchor = mesh.developed[0][anchorColumn];
    const Point2 developedNext = mesh.developed[0][nextColumn];
    double axisX = developedNext.u - developedAnchor.u;
    double axisY = developedNext.v - developedAnchor.v;
    const double axisLength = std::sqrt(axisX * axisX + axisY * axisY);
    if (axisLength > 1.0e-12) {
        axisX /= axisLength;
        axisY /= axisLength;
    } else {
        axisX = 1.0;
        axisY = 0.0;
    }
    for (std::size_t row = 0; row < static_cast<std::size_t>(mesh.rows); ++row) {
        for (std::size_t column = 0; column < static_cast<std::size_t>(mesh.columns);
             ++column) {
            const Point2& flat = mesh.developed[row][column];
            const double dx = flat.u - developedAnchor.u;
            const double dy = flat.v - developedAnchor.v;
            const double along = dx * axisX + dy * axisY;
            const double side = -dx * axisY + dy * axisX;
            result[row][column] = worldAnchor + worldTangent * along + worldSide * side;
        }
    }
    return result;
}

} // namespace

Rows FoldBandMesh(const BandMesh& mesh, double progress)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    if (mesh.rows < 2 || mesh.columns < 2 || t >= 1.0 - 1.0e-9) {
        return mesh.world;
    }
    if (t <= 1.0e-9) {
        return PlaceFlatDevelopment(mesh);
    }
    // 中間: 帯ごとに等長で曲げ、共有レールで順に剛体接続し、
    // 帯間の折り角(world の値)も progress 倍する。帯の中は常に厳密な等長。
    Rows result(static_cast<std::size_t>(mesh.rows));
    std::vector<Vector3> bottom;
    std::vector<Vector3> top;
    BendBandStrip(mesh, 0, t, bottom, top);
    result[0] = std::move(bottom);
    result[1] = std::move(top);
    const std::size_t anchorColumn = static_cast<std::size_t>(mesh.columns / 2);
    for (int band = 1; band + 1 < mesh.rows; ++band) {
        BendBandStrip(mesh, band, t, bottom, top);
        // 下レールを配置済みの共有レールへ剛体で合わせる。
        const std::vector<Vector3>& placed = result[static_cast<std::size_t>(band)];
        const PointFrame from = MakePointFrame(bottom.front(), bottom.back(),
            bottom[anchorColumn]);
        const PointFrame to = MakePointFrame(placed.front(), placed.back(),
            placed[anchorColumn]);
        AlignFrames(from, to, {&bottom, &top});
        // 帯間の折り角も progress 倍: world の折り角 θ に対し (t-1)θ を追加回転。
        const double fullAngle = MeasureCreaseAngleInState(mesh.world, band, mesh.columns);
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
        result[static_cast<std::size_t>(band) + 1] = std::move(top);
    }
    return result;
}

std::vector<double> MeasureCreaseAngles(const BandMesh& mesh)
{
    std::vector<double> angles(static_cast<std::size_t>(mesh.CreaseCount()), 0.0);
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        angles[static_cast<std::size_t>(rail) - 1] =
            MeasureCreaseAngleInState(mesh.world, rail, mesh.columns);
    }
    return angles;
}

Result<std::vector<BandTransform>> BuildRigidBandTransforms(const BandMesh& mesh,
    const std::vector<double>& creaseProgress)
{
    using Out = Result<std::vector<BandTransform>>;
    if (static_cast<int>(creaseProgress.size()) != mesh.CreaseCount()) {
        return Out::Failure(MakeError(kBandBadProgress,
            "折り線の進行度の数が折り線の本数と一致していません。", {}));
    }
    for (const double value : creaseProgress) {
        if (!std::isfinite(value)) {
            return Out::Failure(MakeError(kBandBadProgress,
                "折り線の進行度は有限の値で指定してください。", {}));
        }
    }
    const int bandCount = mesh.BandCount();
    std::vector<BandTransform> transforms(static_cast<std::size_t>(bandCount));
    // 帯0は固定。帯bは「帯b-1の変換」に、レールb(帯b-1との折り線)まわりの
    // 追加回転 (t-1)θ を掛けたもの。θ・弦軸は world で測り、軸は前帯の変換で写す。
    for (int band = 1; band < bandCount; ++band) {
        const int rail = band;
        const BandTransform& previous = transforms[static_cast<std::size_t>(band) - 1];
        transforms[static_cast<std::size_t>(band)] = previous;
        const double progress = creaseProgress[static_cast<std::size_t>(rail) - 1];
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
        transforms[static_cast<std::size_t>(band)] = ComposeRotationAfter(previous,
            previous.Apply(chord.origin), Normalized(previous.RotateVector(chord.direction)),
            delta);
    }
    return Out::Success(std::move(transforms));
}

Result<Rows> BuildBandFoldRails(const BandMesh& mesh,
    const std::vector<double>& creaseProgress, const std::vector<double>& bandProgress,
    double liftMm)
{
    using Out = Result<Rows>;
    const int bandCount = mesh.BandCount();
    Rows rails;
    if (bandCount == 0 || mesh.columns < 2) {
        return Out::Success(std::move(rails));
    }
    if (static_cast<int>(creaseProgress.size()) != mesh.CreaseCount()) {
        return Out::Failure(MakeError(kBandBadProgress,
            "折り線の進行度の数が折り線の本数と一致していません。", {}));
    }
    // 帯ごとの進行度(オーナー指示: 選んだ部材だけが曲がる)。不足分は最後の値。
    const auto progressOf = [&](int band) {
        if (bandProgress.empty()) {
            return 1.0;
        }
        const std::size_t index =
            std::min(static_cast<std::size_t>(band), bandProgress.size() - 1);
        return std::clamp(bandProgress[index], 0.0, 1.0);
    };
    // 帯間の折り角も帯の t で補間した剛体連鎖を使う。各帯は独立した剛体なので、
    // 帯ごとに自分の t で連鎖を評価してよい。t ごとに連鎖を作り直す。
    double cachedProgress = std::numeric_limits<double>::quiet_NaN();
    std::vector<BandTransform> transforms;
    const auto transformsFor = [&](double t) -> const std::vector<BandTransform>& {
        if (!(t == cachedProgress)) {
            std::vector<double> interpolated(creaseProgress.size(), 1.0);
            for (std::size_t index = 0; index < creaseProgress.size(); ++index) {
                interpolated[index] = 1.0 + t * (creaseProgress[index] - 1.0);
            }
            transforms = BuildRigidBandTransforms(mesh, interpolated).Value();
            cachedProgress = t;
        }
        return transforms;
    };
    // モデル重心(展開位置を外向きへ離す向きの判定に使う)。
    Vector3 centroid{};
    for (const auto& row : mesh.world) {
        for (const Vector3& point : row) {
            centroid = centroid + point;
        }
    }
    centroid = centroid * (1.0 / static_cast<double>(mesh.rows * mesh.columns));
    const std::size_t anchor = static_cast<std::size_t>(mesh.columns / 2);
    const std::size_t nextColumn =
        std::min(anchor + 1, static_cast<std::size_t>(mesh.columns) - 1);
    std::vector<Vector3> bottom;
    std::vector<Vector3> top;
    for (int band = 0; band < bandCount; ++band) {
        const double t = progressOf(band);
        const auto& worldBottom = mesh.world[static_cast<std::size_t>(band)];
        const auto& worldTop = mesh.world[static_cast<std::size_t>(band) + 1];
        // 等長の曲げ(三角形剛体+二面角×t)。t=1 で world と厳密一致。
        BendBandStrip(mesh, band, t, bottom, top);
        // 中央素線を world の中央素線へ剛体で合わせ、帯を元の位置周辺に保つ。
        AlignFrames(MakePointFrame(bottom[anchor], top[anchor], bottom[nextColumn]),
            MakePointFrame(worldBottom[anchor], worldTop[anchor], worldBottom[nextColumn]),
            {&bottom, &top});
        // 外向きの持ち上げ((1-t) で減衰)。
        const Vector3 worldTangent = Normalized(worldBottom[nextColumn] - worldBottom[anchor]);
        const Vector3 worldUp = worldTop[anchor] - worldBottom[anchor];
        Vector3 normal = Normalized(Cross(worldTangent, worldUp));
        if (normal.Length() <= 1.0e-9) {
            normal = {0.0, 0.0, 1.0};
        }
        const Vector3 bandCenter = (worldBottom[anchor] + worldTop[anchor]) * 0.5;
        if (Dot(normal, bandCenter - centroid) < 0.0) {
            normal = normal * -1.0;
        }
        const Vector3 lift = normal * (liftMm * (1.0 - t));
        const BandTransform& transform = transformsFor(t)[static_cast<std::size_t>(band)];
        for (Vector3& point : bottom) {
            point = transform.Apply(point) + lift;
        }
        for (Vector3& point : top) {
            point = transform.Apply(point) + lift;
        }
        rails.push_back(bottom);
        rails.push_back(top);
    }
    return Out::Success(std::move(rails));
}

} // namespace kachakacha::v2::fabrication
