#include "CadViewport.h"

#include "kachakacha/io/NumericExpression.h"
#include "kachakacha/model/WireOperations.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineF>
#include <QFontMetrics>
#include <QCursor>
#include <QLabel>
#include <QLineEdit>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using kachakacha::geometry::Vector3;
using kachakacha::geometry::AlmostEqual;
using kachakacha::model::NamedWire;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::ExtendWireToBoundary;
using kachakacha::model::TrimWireAtBoundaries;

namespace {

constexpr double kPlaneHalfSize = 12.0;

enum class ViewCubeFace {
    None,
    Top,
    Bottom,
    Front,
    Back,
    Right,
    Left,
    Home,
    Selection,
    Edge,
    AdjacentLeft,
    AdjacentRight,
    AdjacentUp,
    AdjacentDown,
    CornerPositivePositivePositive,
    CornerPositivePositiveNegative,
    CornerPositiveNegativePositive,
    CornerPositiveNegativeNegative,
    CornerNegativePositivePositive,
    CornerNegativePositiveNegative,
    CornerNegativeNegativePositive,
    CornerNegativeNegativeNegative,
    RollLeft,
    RollRight,
    WorldXNegative,
    WorldXPositive,
    WorldYNegative,
    WorldYPositive,
    WorldZNegative,
    WorldZPositive,
    RelativeXNegative,
    RelativeXPositive,
    RelativeYNegative,
    RelativeYPositive,
};

struct ViewCubeFaceGeometry {
    ViewCubeFace face = ViewCubeFace::None;
    QPolygonF polygon;
    Vector3 direction;
    double depth = 0.0;
    QColor color;
    QString label;
};

struct ViewCubeCornerGeometry {
    ViewCubeFace face = ViewCubeFace::None;
    QRectF area;
    Vector3 direction;
    double depth = 0.0;
};

//! キューブの辺(2面の中間ビュー)。ヒット領域は投影した辺の中点に置く。
struct ViewCubeEdgeGeometry {
    QRectF area;
    Vector3 direction;
    QPointF lineStart;
    QPointF lineEnd;
    double depth = 0.0;
};

//! モデル軸まわりの回転リング。キューブと同じ投影で描くので視点に追従する。
struct ViewCubeRingGeometry {
    int axis = 0; //!< 0=X 1=Y 2=Z
    QPolygonF polyline;
    QColor color;
    QRectF plusArea;      //!< クリックで+方向(カメラ+回転)の矢じり
    QPointF plusTip;
    QPointF plusTangent;  //!< 矢じりの向き(単位ベクトル)
    ViewCubeFace plusFace = ViewCubeFace::None;
    QRectF minusArea;
    QPointF minusTip;
    QPointF minusTangent;
    ViewCubeFace minusFace = ViewCubeFace::None;
};

//! 画面基準(位置固定)の回転矢印の半分。クリックした側の方向へ回す。
struct ViewCubeScreenArrowGeometry {
    QRectF area;
    ViewCubeFace face = ViewCubeFace::None;
};

struct ViewCubeGeometry {
    std::vector<ViewCubeFaceGeometry> faces;
    std::vector<ViewCubeCornerGeometry> corners;
    std::vector<ViewCubeEdgeGeometry> edges;
    std::array<ViewCubeRingGeometry, 3> rings;
    std::vector<ViewCubeScreenArrowGeometry> screenArrows; //!< ヨー左右+ピッチ上下
    std::vector<std::pair<QRectF, ViewCubeFace>> adjacentArrows; //!< 面に正対中のみ
    QRectF home;
    QRectF selection;
    QRectF rollLeft;
    QRectF rollRight;
    QPointF cubeCenter;
    QRectF bounds;
};

//! ヒット結果。Edge のときは direction が正対方向を運ぶ。
struct ViewCubeHit {
    ViewCubeFace face = ViewCubeFace::None;
    Vector3 direction;
};

Vector3 RotateVectorAroundAxis(Vector3 value, Vector3 axis, double angleRadians)
{
    axis = axis.Normalized();
    const double cosine = std::cos(angleRadians);
    const double sine = std::sin(angleRadians);
    return value * cosine
        + Cross(axis, value) * sine
        + axis * Dot(axis, value) * (1.0 - cosine);
}

constexpr double kViewCubeCenterFromRight = 84.0;
constexpr double kViewCubeCenterY = 74.0;

ViewCubeGeometry MakeViewCubeGeometry(
    int viewportWidth,
    const std::array<Vector3, 3>& viewBasis)
{
    // Use the model's camera basis so the navigator always reports the exact current attitude.
    ViewCubeGeometry geometry;
    const double centerX = viewportWidth - kViewCubeCenterFromRight;
    const double centerY = kViewCubeCenterY;
    const double cubeScale = 22.0;
    const auto project = [&](Vector3 point) {
        return QPointF{
            centerX + Dot(point, viewBasis[1]) * cubeScale,
            centerY - Dot(point, viewBasis[2]) * cubeScale,
        };
    };

    struct FaceDefinition {
        ViewCubeFace face;
        Vector3 direction;
        std::array<Vector3, 4> corners;
        QColor color;
        QString label;
    };
    const std::array faceDefinitions = {
        FaceDefinition{ViewCubeFace::Top, {0.0, 0.0, 1.0},
            {{{-1.0, -1.0, 1.0}, {1.0, -1.0, 1.0}, {1.0, 1.0, 1.0}, {-1.0, 1.0, 1.0}}},
            QColor("#e8edef"), QStringLiteral("上")},
        FaceDefinition{ViewCubeFace::Bottom, {0.0, 0.0, -1.0},
            {{{-1.0, 1.0, -1.0}, {1.0, 1.0, -1.0}, {1.0, -1.0, -1.0}, {-1.0, -1.0, -1.0}}},
            QColor("#e4e8eb"), QStringLiteral("下")},
        FaceDefinition{ViewCubeFace::Front, {0.0, -1.0, 0.0},
            {{{-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0}, {1.0, -1.0, 1.0}, {-1.0, -1.0, 1.0}}},
            QColor("#d6e6e7"), QStringLiteral("前")},
        FaceDefinition{ViewCubeFace::Back, {0.0, 1.0, 0.0},
            {{{1.0, 1.0, -1.0}, {-1.0, 1.0, -1.0}, {-1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}}},
            QColor("#e0e8e9"), QStringLiteral("後")},
        FaceDefinition{ViewCubeFace::Right, {1.0, 0.0, 0.0},
            {{{1.0, -1.0, -1.0}, {1.0, 1.0, -1.0}, {1.0, 1.0, 1.0}, {1.0, -1.0, 1.0}}},
            QColor("#dfe4e8"), QStringLiteral("右")},
        FaceDefinition{ViewCubeFace::Left, {-1.0, 0.0, 0.0},
            {{{-1.0, 1.0, -1.0}, {-1.0, -1.0, -1.0}, {-1.0, -1.0, 1.0}, {-1.0, 1.0, 1.0}}},
            QColor("#e3e6e9"), QStringLiteral("左")},
    };
    for (const FaceDefinition& definition : faceDefinitions) {
        const double depth = Dot(definition.direction, viewBasis[0]);
        if (depth <= 1.0e-6) {
            continue;
        }
        QPolygonF polygon;
        for (const Vector3& corner : definition.corners) {
            polygon << project(corner);
        }
        geometry.faces.push_back({
            definition.face, std::move(polygon), definition.direction,
            depth, definition.color, definition.label,
        });
    }
    std::sort(geometry.faces.begin(), geometry.faces.end(), [](const auto& first, const auto& second) {
        return first.depth < second.depth;
    });

    struct CornerDefinition {
        ViewCubeFace face;
        Vector3 direction;
    };
    const std::array cornerDefinitions = {
        CornerDefinition{ViewCubeFace::CornerPositivePositivePositive, {1.0, 1.0, 1.0}},
        CornerDefinition{ViewCubeFace::CornerPositivePositiveNegative, {1.0, 1.0, -1.0}},
        CornerDefinition{ViewCubeFace::CornerPositiveNegativePositive, {1.0, -1.0, 1.0}},
        CornerDefinition{ViewCubeFace::CornerPositiveNegativeNegative, {1.0, -1.0, -1.0}},
        CornerDefinition{ViewCubeFace::CornerNegativePositivePositive, {-1.0, 1.0, 1.0}},
        CornerDefinition{ViewCubeFace::CornerNegativePositiveNegative, {-1.0, 1.0, -1.0}},
        CornerDefinition{ViewCubeFace::CornerNegativeNegativePositive, {-1.0, -1.0, 1.0}},
        CornerDefinition{ViewCubeFace::CornerNegativeNegativeNegative, {-1.0, -1.0, -1.0}},
    };
    for (const CornerDefinition& definition : cornerDefinitions) {
        const double depth = Dot(definition.direction, viewBasis[0]);
        if (depth <= 1.0e-6) {
            continue;
        }
        const QPointF center = project(definition.direction);
        geometry.corners.push_back({
            definition.face,
            QRectF(center.x() - 7.0, center.y() - 7.0, 14.0, 14.0),
            definition.direction.Normalized(),
            depth,
        });
    }
    std::sort(geometry.corners.begin(), geometry.corners.end(), [](const auto& first, const auto& second) {
        return first.depth < second.depth;
    });

    // 辺(2面の中間ビュー)。中点にヒット領域、ハイライト用に辺の両端も持つ。
    const std::array<Vector3, 3> axisUnits = {
        Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, Vector3{0.0, 0.0, 1.0}};
    for (int firstAxis = 0; firstAxis < 3; ++firstAxis) {
        for (int secondAxis = firstAxis + 1; secondAxis < 3; ++secondAxis) {
            const int freeAxis = 3 - firstAxis - secondAxis;
            for (double firstSign : {-1.0, 1.0}) {
                for (double secondSign : {-1.0, 1.0}) {
                    const Vector3 middle =
                        axisUnits[firstAxis] * firstSign + axisUnits[secondAxis] * secondSign;
                    const double depth = Dot(middle.Normalized(), viewBasis[0]);
                    if (depth <= 0.05) {
                        continue;
                    }
                    const QPointF center = project(middle);
                    geometry.edges.push_back({
                        QRectF(center.x() - 6.0, center.y() - 6.0, 12.0, 12.0),
                        middle.Normalized(),
                        project(middle - axisUnits[freeAxis]),
                        project(middle + axisUnits[freeAxis]),
                        depth,
                    });
                }
            }
        }
    }
    std::sort(geometry.edges.begin(), geometry.edges.end(), [](const auto& first, const auto& second) {
        return first.depth < second.depth;
    });

    // モデル軸まわりの回転リング(キューブと同じ投影=視点に追従する矢印)。
    const std::array<QColor, 3> axisColors = {
        QColor("#b9363e"), QColor("#2d7f49"), QColor("#326bb4")};
    const std::array<ViewCubeFace, 3> ringNegative = {
        ViewCubeFace::WorldXNegative, ViewCubeFace::WorldYNegative, ViewCubeFace::WorldZNegative};
    const std::array<ViewCubeFace, 3> ringPositive = {
        ViewCubeFace::WorldXPositive, ViewCubeFace::WorldYPositive, ViewCubeFace::WorldZPositive};
    constexpr int kRingSamples = 64;
    constexpr double kRingRadius = 1.95;
    for (int axis = 0; axis < 3; ++axis) {
        ViewCubeRingGeometry ring;
        ring.axis = axis;
        ring.color = axisColors[axis];
        const Vector3 planeU = axisUnits[(axis + 1) % 3];
        const Vector3 planeV = axisUnits[(axis + 2) % 3];
        int farthestIndex = 0;
        double farthestDistance = -1.0;
        for (int sample = 0; sample < kRingSamples; ++sample) {
            const double angle = 2.0 * std::numbers::pi * sample / kRingSamples;
            const QPointF point = project(
                planeU * (kRingRadius * std::cos(angle)) + planeV * (kRingRadius * std::sin(angle)));
            ring.polyline << point;
            const double distance = std::hypot(point.x() - centerX, point.y() - centerY);
            if (distance > farthestDistance) {
                farthestDistance = distance;
                farthestIndex = sample;
            }
        }
        const auto ringPoint = [&](int index) {
            return ring.polyline[((index % kRingSamples) + kRingSamples) % kRingSamples];
        };
        const auto tangentAt = [&](int index) {
            const QPointF delta = ringPoint(index + 1) - ringPoint(index - 1);
            const double length = std::hypot(delta.x(), delta.y());
            return length > 1.0e-9 ? delta / length : QPointF(1.0, 0.0);
        };
        // +θ向きの矢じり=見た目が+θへ回る=カメラは-回転(Negative)。反対側の矢じりが+回転。
        const int oppositeIndex = (farthestIndex + kRingSamples / 2) % kRingSamples;
        ring.minusTip = ringPoint(farthestIndex);
        ring.minusTangent = tangentAt(farthestIndex);
        ring.minusFace = ringNegative[axis];
        ring.minusArea = QRectF(ring.minusTip.x() - 9.0, ring.minusTip.y() - 9.0, 18.0, 18.0);
        ring.plusTip = ringPoint(oppositeIndex);
        ring.plusTangent = -tangentAt(oppositeIndex);
        ring.plusFace = ringPositive[axis];
        ring.plusArea = QRectF(ring.plusTip.x() - 9.0, ring.plusTip.y() - 9.0, 18.0, 18.0);
        geometry.rings[axis] = std::move(ring);
    }

    // 画面基準の回転矢印(位置固定)。下=左右回し、右=上下回し、上=ロール。
    geometry.screenArrows = {
        {QRectF(centerX - 48.0, centerY + 52.0, 38.0, 22.0), ViewCubeFace::RelativeYPositive},
        {QRectF(centerX + 10.0, centerY + 52.0, 38.0, 22.0), ViewCubeFace::RelativeYNegative},
        {QRectF(centerX + 52.0, centerY - 46.0, 22.0, 38.0), ViewCubeFace::RelativeXPositive},
        {QRectF(centerX + 52.0, centerY + 8.0, 22.0, 38.0), ViewCubeFace::RelativeXNegative},
    };
    geometry.rollLeft = QRectF(centerX - 33.0, 2.0, 22.0, 20.0);
    geometry.rollRight = QRectF(centerX + 11.0, 2.0, 22.0, 20.0);
    geometry.home = QRectF(centerX - 94.0, 2.0, 26.0, 24.0);

    // 面に正対しているときだけ、隣の面へ90°回る三角矢印を出す(行き止まり防止)。
    for (const ViewCubeFaceGeometry& face : geometry.faces) {
        if (face.depth > 0.9995) {
            geometry.adjacentArrows = {
                {QRectF(centerX - 38.0, centerY - 9.0, 14.0, 18.0), ViewCubeFace::AdjacentLeft},
                {QRectF(centerX + 24.0, centerY - 9.0, 14.0, 18.0), ViewCubeFace::AdjacentRight},
                {QRectF(centerX - 9.0, centerY - 38.0, 18.0, 14.0), ViewCubeFace::AdjacentUp},
                {QRectF(centerX - 9.0, centerY + 24.0, 18.0, 14.0), ViewCubeFace::AdjacentDown},
            };
            break;
        }
    }

    geometry.selection = QRectF(centerX - 50.0, centerY + 80.0, 100.0, 26.0);
    geometry.cubeCenter = QPointF(centerX, centerY);
    geometry.bounds = QRectF(centerX - 96.0, 0.0, 176.0, centerY + 110.0);
    return geometry;
}

ViewCubeHit HitViewCube(const ViewCubeGeometry& cube, QPointF position)
{
    if (cube.home.contains(position)) {
        return {ViewCubeFace::Home, {}};
    }
    if (cube.rollLeft.contains(position)) {
        return {ViewCubeFace::RollLeft, {}};
    }
    if (cube.rollRight.contains(position)) {
        return {ViewCubeFace::RollRight, {}};
    }
    // リングの矢じりは隣接面三角より先に判定する。面正対時、直線に潰れたリングの
    // 矢じりが三角の外側帯と重なるため(重なった場合は矢じりが勝つ)。
    for (const ViewCubeRingGeometry& ring : cube.rings) {
        if (ring.plusArea.contains(position)) {
            return {ring.plusFace, {}};
        }
        if (ring.minusArea.contains(position)) {
            return {ring.minusFace, {}};
        }
    }
    for (const auto& [area, face] : cube.adjacentArrows) {
        if (area.contains(position)) {
            return {face, {}};
        }
    }
    for (const ViewCubeScreenArrowGeometry& arrow : cube.screenArrows) {
        if (arrow.area.contains(position)) {
            return {arrow.face, {}};
        }
    }
    for (auto corner = cube.corners.rbegin(); corner != cube.corners.rend(); ++corner) {
        if (corner->area.contains(position)) {
            return {corner->face, corner->direction};
        }
    }
    for (auto edge = cube.edges.rbegin(); edge != cube.edges.rend(); ++edge) {
        if (edge->area.contains(position)) {
            return {ViewCubeFace::Edge, edge->direction};
        }
    }
    for (auto face = cube.faces.rbegin(); face != cube.faces.rend(); ++face) {
        if (face->polygon.containsPoint(position, Qt::OddEvenFill)) {
            return {face->face, face->direction};
        }
    }
    for (const ViewCubeRingGeometry& ring : cube.rings) {
        double nearestDistance = 1.0e18;
        for (int index = 0; index < ring.polyline.size(); ++index) {
            const QPointF a = ring.polyline[index];
            const QPointF b = ring.polyline[(index + 1) % ring.polyline.size()];
            const QPointF delta = b - a;
            const double lengthSquared = QPointF::dotProduct(delta, delta);
            double t = 0.0;
            if (lengthSquared > 1.0e-12) {
                t = std::clamp(
                    QPointF::dotProduct(position - a, delta) / lengthSquared, 0.0, 1.0);
            }
            nearestDistance =
                std::min(nearestDistance, QLineF(position, a + delta * t).length());
        }
        if (nearestDistance <= 7.0) {
            // 矢じりに近い側の方向へ回す。
            const double toMinus = QLineF(position, ring.minusTip).length();
            const double toPlus = QLineF(position, ring.plusTip).length();
            return {toMinus <= toPlus ? ring.minusFace : ring.plusFace, {}};
        }
    }
    if (cube.selection.contains(position)) {
        return {ViewCubeFace::Selection, {}};
    }
    return {ViewCubeFace::None, {}};
}

bool CanDragViewCube(ViewCubeFace face)
{
    return face == ViewCubeFace::Top
        || face == ViewCubeFace::Bottom
        || face == ViewCubeFace::Front
        || face == ViewCubeFace::Back
        || face == ViewCubeFace::Right
        || face == ViewCubeFace::Left
        || face == ViewCubeFace::Edge
        || (face >= ViewCubeFace::CornerPositivePositivePositive
            && face <= ViewCubeFace::CornerNegativeNegativeNegative);
}

std::optional<Vector3> ViewCubeDirection(ViewCubeFace face)
{
    switch (face) {
    case ViewCubeFace::Top: return Vector3{0.0, 0.0, 1.0};
    case ViewCubeFace::Bottom: return Vector3{0.0, 0.0, -1.0};
    case ViewCubeFace::Front: return Vector3{0.0, -1.0, 0.0};
    case ViewCubeFace::Back: return Vector3{0.0, 1.0, 0.0};
    case ViewCubeFace::Right: return Vector3{1.0, 0.0, 0.0};
    case ViewCubeFace::Left: return Vector3{-1.0, 0.0, 0.0};
    case ViewCubeFace::CornerPositivePositivePositive: return Vector3{1.0, 1.0, 1.0}.Normalized();
    case ViewCubeFace::CornerPositivePositiveNegative: return Vector3{1.0, 1.0, -1.0}.Normalized();
    case ViewCubeFace::CornerPositiveNegativePositive: return Vector3{1.0, -1.0, 1.0}.Normalized();
    case ViewCubeFace::CornerPositiveNegativeNegative: return Vector3{1.0, -1.0, -1.0}.Normalized();
    case ViewCubeFace::CornerNegativePositivePositive: return Vector3{-1.0, 1.0, 1.0}.Normalized();
    case ViewCubeFace::CornerNegativePositiveNegative: return Vector3{-1.0, 1.0, -1.0}.Normalized();
    case ViewCubeFace::CornerNegativeNegativePositive: return Vector3{-1.0, -1.0, 1.0}.Normalized();
    case ViewCubeFace::CornerNegativeNegativeNegative: return Vector3{-1.0, -1.0, -1.0}.Normalized();
    default: return std::nullopt;
    }
}

struct ViewCubeRotationCommand {
    bool relative = false;
    ViewRotationAxis axis = ViewRotationAxis::X;
    double direction = 1.0;
};

std::optional<ViewCubeRotationCommand> ViewCubeRotation(ViewCubeFace face)
{
    switch (face) {
    case ViewCubeFace::WorldXNegative: return ViewCubeRotationCommand{false, ViewRotationAxis::X, -1.0};
    case ViewCubeFace::WorldXPositive: return ViewCubeRotationCommand{false, ViewRotationAxis::X, 1.0};
    case ViewCubeFace::WorldYNegative: return ViewCubeRotationCommand{false, ViewRotationAxis::Y, -1.0};
    case ViewCubeFace::WorldYPositive: return ViewCubeRotationCommand{false, ViewRotationAxis::Y, 1.0};
    case ViewCubeFace::WorldZNegative: return ViewCubeRotationCommand{false, ViewRotationAxis::Z, -1.0};
    case ViewCubeFace::WorldZPositive: return ViewCubeRotationCommand{false, ViewRotationAxis::Z, 1.0};
    case ViewCubeFace::RelativeXNegative: return ViewCubeRotationCommand{true, ViewRotationAxis::X, -1.0};
    case ViewCubeFace::RelativeXPositive: return ViewCubeRotationCommand{true, ViewRotationAxis::X, 1.0};
    case ViewCubeFace::RelativeYNegative: return ViewCubeRotationCommand{true, ViewRotationAxis::Y, -1.0};
    case ViewCubeFace::RelativeYPositive: return ViewCubeRotationCommand{true, ViewRotationAxis::Y, 1.0};
    default: return std::nullopt;
    }
}

//! 回転行列(列 = 前・右・上)と単位クォータニオンの相互変換。ビュー遷移の補間用。
struct ViewQuaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

ViewQuaternion BasisToQuaternion(const std::array<Vector3, 3>& basis)
{
    // m[row][column]、列0=前, 列1=右, 列2=上。
    const double m00 = basis[0].x, m01 = basis[1].x, m02 = basis[2].x;
    const double m10 = basis[0].y, m11 = basis[1].y, m12 = basis[2].y;
    const double m20 = basis[0].z, m21 = basis[1].z, m22 = basis[2].z;
    ViewQuaternion q;
    const double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q.w = (m21 - m12) / s;
        q.x = 0.25 * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25 * s;
        q.z = (m12 + m21) / s;
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25 * s;
    }
    return q;
}

std::array<Vector3, 3> QuaternionToBasis(const ViewQuaternion& q)
{
    const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const Vector3 forward{1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz), 2.0 * (xz - wy)};
    const Vector3 right{2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz + wx)};
    const Vector3 up{2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy)};
    return {forward.Normalized(), right.Normalized(), up.Normalized()};
}

ViewQuaternion SlerpQuaternion(ViewQuaternion from, const ViewQuaternion& to, double t)
{
    double dot = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;
    if (dot < 0.0) {
        from.w = -from.w;
        from.x = -from.x;
        from.y = -from.y;
        from.z = -from.z;
        dot = -dot;
    }
    double weightFrom = 1.0 - t;
    double weightTo = t;
    if (dot < 0.9995) {
        const double theta = std::acos(std::clamp(dot, -1.0, 1.0));
        const double sine = std::sin(theta);
        if (sine > 1.0e-9) {
            weightFrom = std::sin((1.0 - t) * theta) / sine;
            weightTo = std::sin(t * theta) / sine;
        }
    }
    ViewQuaternion result{
        from.w * weightFrom + to.w * weightTo,
        from.x * weightFrom + to.x * weightTo,
        from.y * weightFrom + to.y * weightTo,
        from.z * weightFrom + to.z * weightTo,
    };
    const double length =
        std::sqrt(result.w * result.w + result.x * result.x + result.y * result.y + result.z * result.z);
    if (length > 1.0e-12) {
        result.w /= length;
        result.x /= length;
        result.y /= length;
        result.z /= length;
    }
    return result;
}

std::array<Vector3, 3> IsometricViewBasis()
{
    const double yaw = 0.75;
    const double pitch = 0.48;
    const Vector3 forward{
        std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch)};
    const Vector3 right = Vector3{-forward.y, forward.x, 0.0}.Normalized();
    return {forward, right, Cross(forward, right).Normalized()};
}

QString RotationAxisName(ViewRotationAxis axis)
{
    switch (axis) {
    case ViewRotationAxis::X: return QStringLiteral("X");
    case ViewRotationAxis::Y: return QStringLiteral("Y");
    case ViewRotationAxis::Z: return QStringLiteral("Z");
    }
    return {};
}

double DistanceToSegment(QPointF point, QPointF a, QPointF b)
{
    const QPointF segment = b - a;
    const double lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared < 1.0e-9) {
        return QLineF(point, a).length();
    }

    const double t = std::clamp(QPointF::dotProduct(point - a, segment) / lengthSquared, 0.0, 1.0);
    return QLineF(point, a + segment * t).length();
}

std::optional<double> EvaluateDimensionExpression(QString expression)
{
    expression.replace(QChar(0x00d7), QLatin1Char('*'));
    expression.replace(QChar(0x00f7), QLatin1Char('/'));
    expression.replace(QChar(0x03c0), QStringLiteral("pi"));
    const QByteArray utf8 = expression.trimmed().toUtf8();
    return kachakacha::io::EvaluateNumericExpression(
        std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

QString DimensionText(double value)
{
    return QString::number(value, 'f', 3);
}

} // namespace

CadViewport::CadViewport(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    dynamicDimensionEditor_ = new QFrame(this);
    dynamicDimensionEditor_->setObjectName(QStringLiteral("dynamicDimensionEditor"));
    auto* layout = new QHBoxLayout(dynamicDimensionEditor_);
    layout->setContentsMargins(7, 5, 7, 5);
    layout->setSpacing(5);
    dynamicPrimaryLabel_ = new QLabel(dynamicDimensionEditor_);
    dynamicPrimaryField_ = new QLineEdit(dynamicDimensionEditor_);
    dynamicSecondaryLabel_ = new QLabel(dynamicDimensionEditor_);
    dynamicSecondaryField_ = new QLineEdit(dynamicDimensionEditor_);
    for (QLineEdit* field : {dynamicPrimaryField_, dynamicSecondaryField_}) {
        field->setMaxLength(64);
        field->setFixedWidth(92);
        field->setAlignment(Qt::AlignRight);
        field->setPlaceholderText(QStringLiteral("(180/2)*3"));
        field->setToolTip(QStringLiteral("数値または + - * / ( ) を使った計算式。Enterで確定"));
        field->installEventFilter(this);
    }
    layout->addWidget(dynamicPrimaryLabel_);
    layout->addWidget(dynamicPrimaryField_);
    layout->addWidget(dynamicSecondaryLabel_);
    layout->addWidget(dynamicSecondaryField_);
    dynamicDimensionEditor_->setStyleSheet(QStringLiteral(
        "QFrame#dynamicDimensionEditor {"
        " background: #ffffff; border: 1px solid #82939b; border-radius: 4px; }"
        "QFrame#dynamicDimensionEditor QLabel {"
        " color: #26343b; border: none; font-weight: 600; }"
        "QFrame#dynamicDimensionEditor QLineEdit {"
        " min-height: 24px; padding: 1px 5px; color: #17252b;"
        " background: #f7fbfb; border: 1px solid #6d858d; border-radius: 2px; }"
        "QFrame#dynamicDimensionEditor QLineEdit:focus {"
        " background: #ffffff; border: 2px solid #087780; }"
        "QFrame#dynamicDimensionEditor QLineEdit[expressionError=\"true\"] {"
        " background: #fff0f1; border: 2px solid #a02c3a; }"));
    dynamicDimensionEditor_->hide();
}

void CadViewport::SetProject(const kachakacha::model::Project* project, bool fitView)
{
    project_ = project;
    selection_ = {};
    selections_.clear();
    reference_ = {};
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();
    hoveredControlPoint_.reset();
    CancelControlPointDrag();
    coincidencePicks_.clear();
    measurementPicks_.clear();
    measurementOverlayFirst_.reset();
    measurementOverlaySecond_.reset();
    measurementOverlayThird_.reset();
    measurementOverlayText_.clear();
    measurementOverlayComponentTexts_.clear();
    referenceDimensionOverlays_.clear();
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
    if (fitView) {
        FitAll();
    } else {
        update();
    }
}

void CadViewport::SetSelection(CadSelection selection)
{
    if (selection.kind == CadSelectionKind::None) {
        SetSelections({});
    } else {
        SetSelections({selection});
    }
}

void CadViewport::SetSelections(std::vector<CadSelection> selections)
{
    selections_ = std::move(selections);
    selection_ = selections_.empty() ? CadSelection{} : selections_.back();
    update();
}

void CadViewport::SetDisplayMode(
    ViewportDisplayMode mode,
    std::vector<CadSelection> isolatedSelections)
{
    displayMode_ = mode;
    isolatedSelections_ = mode == ViewportDisplayMode::IsolatedSelection
        ? std::move(isolatedSelections)
        : std::vector<CadSelection>{};
    ClearHover();
    update();
}

void CadViewport::SetReference(CadSelection reference)
{
    reference_ = reference;
    update();
}

void CadViewport::SetSelectionChangedCallback(std::function<void(const std::vector<CadSelection>&)> callback)
{
    selectionChanged_ = std::move(callback);
}

void CadViewport::SetPointCreatedCallback(std::function<void(Vector3)> callback)
{
    pointCreated_ = std::move(callback);
}

void CadViewport::SetLineCreatedCallback(std::function<void(Vector3, Vector3)> callback)
{
    lineCreated_ = std::move(callback);
}

void CadViewport::SetPolylineCreatedCallback(std::function<void(const std::vector<Vector3>&)> callback)
{
    polylineCreated_ = std::move(callback);
}

void CadViewport::SetRectangleCreatedCallback(std::function<void(const std::array<Vector3, 4>&)> callback)
{
    rectangleCreated_ = std::move(callback);
}

void CadViewport::SetCircleCreatedCallback(std::function<void(Vector3, double)> callback)
{
    circleCreated_ = std::move(callback);
}

void CadViewport::SetArcCreatedCallback(std::function<void(Vector3, Vector3, Vector3)> callback)
{
    arcCreated_ = std::move(callback);
}

void CadViewport::SetArcWireCreatedCallback(std::function<void(const Wire&)> callback)
{
    arcWireCreated_ = std::move(callback);
}

void CadViewport::SetBezierCreatedCallback(std::function<void(const std::array<Vector3, 4>&)> callback)
{
    bezierCreated_ = std::move(callback);
}

void CadViewport::SetSplineCreatedCallback(std::function<void(const std::vector<Vector3>&)> callback)
{
    splineCreated_ = std::move(callback);
}

void CadViewport::SetWireControlPointMovedCallback(
    std::function<void(int, const Wire&)> callback)
{
    wireControlPointMoved_ = std::move(callback);
}

void CadViewport::SetTranslationRequestedCallback(std::function<void(Vector3, bool)> callback)
{
    translationRequested_ = std::move(callback);
}

void CadViewport::SetMirrorRequestedCallback(std::function<void(Vector3, Vector3, Vector3)> callback)
{
    mirrorRequested_ = std::move(callback);
}

void CadViewport::SetRotationRequestedCallback(std::function<void(Vector3, Vector3, double)> callback)
{
    rotationRequested_ = std::move(callback);
}

void CadViewport::SetSplitRequestedCallback(std::function<void(int, double)> callback)
{
    splitRequested_ = std::move(callback);
}

void CadViewport::SetTrimRequestedCallback(std::function<void(int, double)> callback)
{
    trimRequested_ = std::move(callback);
}

void CadViewport::SetExtendRequestedCallback(std::function<void(int, double)> callback)
{
    extendRequested_ = std::move(callback);
}

void CadViewport::SetToolExitRequestedCallback(std::function<void()> callback)
{
    toolExitRequested_ = std::move(callback);
}

void CadViewport::SetEscapeRequestedCallback(std::function<void()> callback)
{
    escapeRequested_ = std::move(callback);
}

void CadViewport::SetLineBetweenPickedCallback(
    std::function<void(Vector3, Vector3)> callback)
{
    lineBetweenPicked_ = std::move(callback);
}

void CadViewport::SetCoincidenceRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    coincidenceRequested_ = std::move(callback);
}

void CadViewport::SetTangentRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    tangentRequested_ = std::move(callback);
}

void CadViewport::SetCurvatureRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    curvatureRequested_ = std::move(callback);
}

void CadViewport::SetMeasurementChangedCallback(
    std::function<void(const std::vector<MeasurementPick>&)> callback)
{
    measurementChanged_ = std::move(callback);
}

void CadViewport::SetDrawingStateChangedCallback(std::function<void(ViewportTool, std::size_t)> callback)
{
    drawingStateChanged_ = std::move(callback);
    NotifyDrawingState();
}

void CadViewport::SetActiveWorkPlane(std::optional<kachakacha::model::WorkPlane> plane)
{
    activePlane_ = std::move(plane);
    CancelDrawing();
    update();
}

void CadViewport::SetTool(ViewportTool tool)
{
    if (tool_ != tool) {
        coincidencePicks_.clear();
        if (dynamicPrimaryField_ != nullptr) {
            dynamicPrimaryField_->clear();
            dynamicSecondaryField_->clear();
        }
    }
    tool_ = tool;
    gridOriginDragSource_.reset();
    gridOriginDragTarget_.reset();
    lineBetweenFirstPoint_.reset();
    lineBetweenHoverPoint_.reset();
    CancelControlPointDrag();
    CancelDrawing();
    setCursor(hoveredSelection_.kind == CadSelectionKind::Wire
            && (tool_ == ViewportTool::Select || tool_ == ViewportTool::Measure
                || tool_ == ViewportTool::SplitWire || tool_ == ViewportTool::TrimWire
                || tool_ == ViewportTool::ExtendWire || tool_ == ViewportTool::Coincident
                || tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature)
        ? Qt::PointingHandCursor
        : tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void CadViewport::SetSnapEnabled(bool enabled)
{
    snapEnabled_ = enabled;
    if (!enabled) {
        drawingSnapHover_.reset();
    }
    update();
}

void CadViewport::RestoreViewFraming(Vector3 target, double pixelsPerMillimeter)
{
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)
        || !std::isfinite(pixelsPerMillimeter) || pixelsPerMillimeter <= 0.0) {
        return;
    }
    target_ = target;
    pixelsPerMillimeter_ = pixelsPerMillimeter;
    update();
}

void CadViewport::SetSnapStep(double stepMillimeters)
{
    if (std::isfinite(stepMillimeters) && stepMillimeters > 1.0e-6) {
        snapStep_ = stepMillimeters;
        update();
    }
}

void CadViewport::SetGridPointsVisible(bool visible)
{
    gridPointsVisible_ = visible;
    update();
}

void CadViewport::SetGridSubdivision(int subdivision)
{
    if (subdivision != 1 && subdivision != 2 && subdivision != 3 && subdivision != 4) {
        return;
    }
    gridSubdivision_ = subdivision;
    update();
}

void CadViewport::SetGridOrigin(double u, double v)
{
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return;
    }
    gridOriginU_ = u;
    gridOriginV_ = v;
    update();
}

void CadViewport::SetGridOriginChangedCallback(std::function<void(double, double)> callback)
{
    gridOriginChanged_ = std::move(callback);
}

void CadViewport::SetArcDrawingMode(ArcDrawingMode mode)
{
    if (arcDrawingMode_ == mode) {
        return;
    }
    arcDrawingMode_ = mode;
    CancelDrawing();
}

void CadViewport::SetConfiguredArc(
    double radiusMillimeters,
    double tangentAngleDegrees,
    double sweepAngleDegrees,
    bool bulgeLeft)
{
    if (!std::isfinite(radiusMillimeters) || radiusMillimeters <= 1.0e-9
        || !std::isfinite(tangentAngleDegrees) || !std::isfinite(sweepAngleDegrees)) {
        return;
    }
    configuredArcRadius_ = radiusMillimeters;
    configuredArcTangentAngleDegrees_ = tangentAngleDegrees;
    configuredArcSweepAngleDegrees_ = sweepAngleDegrees;
    configuredArcBulgeLeft_ = bulgeLeft;
    update();
}

bool CadViewport::CommitConfiguredArc()
{
    if (tool_ != ViewportTool::DrawArc || !activePlane_.has_value()
        || arcDrawingMode_ == ArcDrawingMode::ThreePoints || !arcWireCreated_) {
        return false;
    }

    Wire arc = arcDrawingMode_ == ArcDrawingMode::EndpointsRadius
        ? (drawingPoints_.size() == 2
            ? Wire::CircularArcFromEndpointsRadius(
                drawingPoints_[0], drawingPoints_[1], activePlane_->Normal(),
                configuredArcRadius_, configuredArcBulgeLeft_)
            : throw std::invalid_argument("Endpoint-radius arc needs its start and end points."))
        : (drawingPoints_.size() == 1
            ? Wire::CircularArcFromStartTangent(
                drawingPoints_.front(),
                activePlane_->UAxis() * std::cos(
                    configuredArcTangentAngleDegrees_ * std::numbers::pi / 180.0)
                    + activePlane_->VAxis() * std::sin(
                        configuredArcTangentAngleDegrees_ * std::numbers::pi / 180.0),
                activePlane_->Normal(),
                configuredArcRadius_,
                configuredArcSweepAngleDegrees_ * std::numbers::pi / 180.0)
            : throw std::invalid_argument("Start-tangent arc needs its start point."));
    arcWireCreated_(arc);
    drawingPoints_.clear();
    drawingSnapHover_.reset();
    NotifyDrawingState();
    UpdateDynamicDimensionEditor();
    update();
    return true;
}

void CadViewport::SetWireAppearance(const QColor& color, double width, Qt::PenStyle style)
{
    if (color.isValid()) {
        wireColor_ = color;
    }
    if (std::isfinite(width)) {
        wireWidth_ = std::clamp(width, 0.25, 12.0);
    }
    wireStyle_ = style;
    update();
}

void CadViewport::SetConstructionWireAppearance(
    const QColor& color,
    double width,
    Qt::PenStyle style)
{
    if (color.isValid()) {
        constructionWireColor_ = color;
    }
    if (std::isfinite(width)) {
        constructionWireWidth_ = std::clamp(width, 0.25, 12.0);
    }
    constructionWireStyle_ = style;
    update();
}

void CadViewport::SetSurfaceAppearance(
    const QColor& fillColor,
    int opacityPercent,
    const QColor& edgeColor,
    double edgeWidth,
    Qt::PenStyle edgeStyle)
{
    if (fillColor.isValid()) {
        surfaceFillColor_ = fillColor;
    }
    surfaceOpacityPercent_ = std::clamp(opacityPercent, 0, 100);
    if (edgeColor.isValid()) {
        surfaceEdgeColor_ = edgeColor;
    }
    if (std::isfinite(edgeWidth)) {
        surfaceEdgeWidth_ = std::clamp(edgeWidth, 0.25, 12.0);
    }
    surfaceEdgeStyle_ = edgeStyle;
    update();
}

void CadViewport::SetSurfaceDiagnosticMode(SurfaceDiagnosticMode mode)
{
    surfaceDiagnosticMode_ = mode;
    update();
}

void CadViewport::SetPlateAppearance(
    const QColor& fillColor,
    int opacityPercent,
    const QColor& edgeColor,
    double edgeWidth,
    Qt::PenStyle edgeStyle)
{
    if (fillColor.isValid()) {
        plateFillColor_ = fillColor;
    }
    plateOpacityPercent_ = std::clamp(opacityPercent, 0, 100);
    if (edgeColor.isValid()) {
        plateEdgeColor_ = edgeColor;
    }
    if (std::isfinite(edgeWidth)) {
        plateEdgeWidth_ = std::clamp(edgeWidth, 0.25, 12.0);
    }
    plateEdgeStyle_ = edgeStyle;
    update();
}

void CadViewport::SetBackgroundColor(const QColor& color)
{
    if (color.isValid()) {
        backgroundColor_ = color;
        update();
    }
}

void CadViewport::SetGridColors(const QColor& majorColor, const QColor& minorColor)
{
    if (majorColor.isValid()) {
        majorGridColor_ = majorColor;
    }
    if (minorColor.isValid()) {
        minorGridColor_ = minorColor;
    }
    update();
}

void CadViewport::SetPlateSplitPreview(
    std::optional<kachakacha::model::PlateSplitAxis> axis,
    double parameter)
{
    plateSplitPreviewAxis_ = axis;
    plateSplitPreviewParameter_ = std::clamp(parameter, 0.0, 1.0);
    update();
}

void CadViewport::SetPlateAssemblyGuidePreview(
    std::optional<int> plateIndex,
    std::vector<std::vector<Vector3>> foldLines,
    std::vector<std::vector<Vector3>> reliefCuts)
{
    plateAssemblyGuideIndex_ = plateIndex;
    plateAssemblyFoldLines_ = std::move(foldLines);
    plateAssemblyReliefCuts_ = std::move(reliefCuts);
    update();
}

void CadViewport::SetPlateAssemblyApproximationPreview(
    std::optional<int> plateIndex,
    std::vector<std::array<Vector3, 3>> panels,
    std::vector<int> pieceIndices,
    std::vector<double> deviations,
    double maximumDeviationMillimeters,
    bool smoothPaper)
{
    plateAssemblyApproximationIndex_ = plateIndex;
    plateAssemblyApproximationPanels_ = std::move(panels);
    plateAssemblyApproximationPieceIndices_ = std::move(pieceIndices);
    plateAssemblyApproximationDeviations_ = std::move(deviations);
    plateAssemblyApproximationMaximumDeviationMillimeters_
        = std::max(0.0, maximumDeviationMillimeters);
    plateAssemblyApproximationSmoothPaper_ = smoothPaper;
    update();
}

void CadViewport::SetPartFoldPreview(
    std::vector<std::vector<Vector3>> rails,
    std::vector<int> creaseDirections)
{
    partFoldPreviewRails_ = std::move(rails);
    partFoldPreviewCreases_ = std::move(creaseDirections);
    update();
}

void CadViewport::SetWireOffsetPreview(std::vector<Wire> wires)
{
    wireOffsetPreviews_ = std::move(wires);
    update();
}

void CadViewport::SetMeasurementMode(MeasurementMode mode)
{
    measurementMode_ = mode;
    ClearMeasurement();
}

void CadViewport::ClearMeasurement()
{
    measurementPicks_.clear();
    measurementOverlayFirst_.reset();
    measurementOverlaySecond_.reset();
    measurementOverlayThird_.reset();
    measurementOverlayText_.clear();
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
    update();
}

void CadViewport::ClearCoincidencePicks()
{
    coincidencePicks_.clear();
    NotifyDrawingState();
    update();
}

void CadViewport::SetMeasurementOverlay(
    std::optional<Vector3> firstPoint,
    std::optional<Vector3> secondPoint,
    QString text,
    QStringList componentTexts)
{
    measurementOverlayFirst_ = firstPoint;
    measurementOverlaySecond_ = secondPoint;
    measurementOverlayThird_.reset();
    measurementOverlayText_ = std::move(text);
    measurementOverlayComponentTexts_ = std::move(componentTexts);
    update();
}

void CadViewport::SetMeasurementAngleOverlay(
    Vector3 vertex,
    Vector3 firstPoint,
    Vector3 secondPoint,
    QString text)
{
    measurementOverlayFirst_ = vertex;
    measurementOverlaySecond_ = firstPoint;
    measurementOverlayThird_ = secondPoint;
    measurementOverlayText_ = std::move(text);
    measurementOverlayComponentTexts_.clear();
    update();
}

void CadViewport::SetReferenceDimensionOverlays(std::vector<ReferenceDimensionOverlay> overlays)
{
    referenceDimensionOverlays_ = std::move(overlays);
    update();
}

void CadViewport::AlignToActiveWorkPlane()
{
    if (!activePlane_.has_value()) {
        return;
    }
    AlignToWorkPlane(*activePlane_);
}

void CadViewport::AlignToWorkPlane(const kachakacha::model::WorkPlane& plane)
{
    StopViewAnimation();
    target_ = plane.Origin();
    alignedViewBasis_ = std::array<Vector3, 3>{plane.Normal(), plane.UAxis(), plane.VAxis()};
    rollRadians_ = 0.0;
    update();
}

bool CadViewport::AlignToSelection()
{
    if (project_ == nullptr || selection_.kind == CadSelectionKind::None) {
        return false;
    }
    if (selection_.kind == CadSelectionKind::Point
        && selection_.index >= 0
        && selection_.index < static_cast<int>(project_->Points().size())) {
        target_ = project_->Points()[selection_.index].point;
        update();
        return true;
    }

    std::vector<Vector3> points;
    Vector3 origin;
    Vector3 normal;
    Vector3 uAxisHint;
    const Vector3 previousViewDirection = ViewDirection();

    const auto sampleWire = [&](const Wire& wire) {
        const int samples = wire.Kind() == WireKind::Line ? 1 : 64;
        for (int sample = 0; sample <= samples; ++sample) {
            points.push_back(wire.Evaluate(static_cast<double>(sample) / samples));
        }
    };
    const auto sampleSurface = [&](const kachakacha::model::Surface& surface) {
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                points.push_back(surface.Evaluate(
                    static_cast<double>(uIndex) / 24.0,
                    static_cast<double>(vIndex) / 8.0));
            }
        }
    };
    const auto surfaceUAxis = [](const kachakacha::model::Surface& surface, double u, double v) {
        const double before = std::max(0.0, u - 0.01);
        const double after = std::min(1.0, u + 0.01);
        Vector3 tangent = surface.Evaluate(after, v) - surface.Evaluate(before, v);
        if (tangent.LengthSquared() <= 1.0e-18) {
            const double vBefore = std::max(0.0, v - 0.01);
            const double vAfter = std::min(1.0, v + 0.01);
            tangent = surface.Evaluate(u, vAfter) - surface.Evaluate(u, vBefore);
        }
        return tangent;
    };

    try {
        if (selection_.kind == CadSelectionKind::WorkPlane
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->WorkPlanes().size())) {
            const auto& plane = project_->WorkPlanes()[selection_.index].plane;
            origin = plane.Origin();
            normal = plane.Normal();
            uAxisHint = plane.UAxis();
            points = {
                origin + plane.UAxis() * kPlaneHalfSize + plane.VAxis() * kPlaneHalfSize,
                origin + plane.UAxis() * kPlaneHalfSize - plane.VAxis() * kPlaneHalfSize,
                origin - plane.UAxis() * kPlaneHalfSize + plane.VAxis() * kPlaneHalfSize,
                origin - plane.UAxis() * kPlaneHalfSize - plane.VAxis() * kPlaneHalfSize,
            };
        } else if (selection_.kind == CadSelectionKind::Surface
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Surfaces().size())) {
            const auto& surface = project_->Surfaces()[selection_.index].surface;
            origin = surface.Evaluate(0.5, 0.5);
            normal = surface.Normal(0.5, 0.5);
            uAxisHint = surfaceUAxis(surface, 0.5, 0.5);
            sampleSurface(surface);
        } else if (selection_.kind == CadSelectionKind::Plate
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Plates().size())) {
            const auto& plate = project_->Plates()[selection_.index].plate;
            const double sourceU = plate.SourceU(0.5);
            const double sourceV = plate.SourceV(0.5);
            origin = plate.Evaluate(0.5, 0.5, 0.5);
            normal = plate.SourceSurface().Normal(sourceU, sourceV);
            uAxisHint = surfaceUAxis(plate.SourceSurface(), sourceU, sourceV);
            for (int uIndex = 0; uIndex <= 24; ++uIndex) {
                for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                    const double u = static_cast<double>(uIndex) / 24.0;
                    const double v = static_cast<double>(vIndex) / 8.0;
                    points.push_back(plate.Evaluate(u, v, 0.0));
                    points.push_back(plate.Evaluate(u, v, 1.0));
                }
            }
        } else if (selection_.kind == CadSelectionKind::Body
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Bodies().size())) {
            const auto& body = project_->Bodies()[selection_.index].body;
            const double sourceU = body.SourceU(0.5);
            const double sourceV = body.SourceV(0.5);
            origin = body.Evaluate(0.5, 0.5, 0.5);
            normal = body.SourceSurface().Normal(sourceU, sourceV);
            uAxisHint = surfaceUAxis(body.SourceSurface(), sourceU, sourceV);
            for (int uIndex = 0; uIndex <= 24; ++uIndex) {
                for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                    const double u = static_cast<double>(uIndex) / 24.0;
                    const double v = static_cast<double>(vIndex) / 8.0;
                    points.push_back(body.Evaluate(u, v, 0.0));
                    points.push_back(body.Evaluate(u, v, 1.0));
                }
            }
        } else if (selection_.kind == CadSelectionKind::Wire
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Wires().size())) {
            const auto& namedWire = project_->Wires()[selection_.index];
            const Wire& wire = namedWire.wire;
            sampleWire(wire);
            for (const Vector3& point : points) {
                origin = origin + point;
            }
            origin = origin / static_cast<double>(points.size());

            bool usedSourcePlane = false;
            if (namedWire.metadata.sourcePlaneName.has_value()) {
                const auto sourcePlane = project_->FindWorkPlane(*namedWire.metadata.sourcePlaneName);
                if (sourcePlane.has_value()) {
                    const bool liesOnPlane = std::all_of(points.begin(), points.end(), [&](Vector3 point) {
                        return std::abs(sourcePlane->Project(point).w) <= 1.0e-6;
                    });
                    if (liesOnPlane) {
                        normal = sourcePlane->Normal();
                        uAxisHint = sourcePlane->UAxis();
                        usedSourcePlane = true;
                    }
                }
            }
            if (!usedSourcePlane && namedWire.projection.has_value()) {
                const auto targetSurface = project_->FindSurface(namedWire.projection->targetSurfaceName);
                if (targetSurface.has_value()) {
                    normal = targetSurface->Normal(0.5, 0.5);
                    uAxisHint = surfaceUAxis(*targetSurface, 0.5, 0.5);
                    usedSourcePlane = true;
                }
            }
            if (!usedSourcePlane
                && (wire.Kind() == WireKind::Circle || wire.Kind() == WireKind::CircularArc)) {
                const auto arc = wire.ArcData();
                normal = Cross(arc.uAxis, arc.vAxis);
                uAxisHint = arc.uAxis;
                usedSourcePlane = true;
            }
            if (!usedSourcePlane) {
                double bestCrossLengthSquared = 0.0;
                for (std::size_t first = 0; first < points.size(); ++first) {
                    for (std::size_t second = first + 1; second < points.size(); ++second) {
                        const Vector3 candidate = Cross(points[first] - origin, points[second] - origin);
                        if (candidate.LengthSquared() > bestCrossLengthSquared) {
                            bestCrossLengthSquared = candidate.LengthSquared();
                            normal = candidate;
                        }
                    }
                }
                uAxisHint = wire.End() - wire.Start();
                if (uAxisHint.LengthSquared() <= 1.0e-18 && points.size() > 1) {
                    uAxisHint = points[1] - points[0];
                }
                if (bestCrossLengthSquared <= 1.0e-18) {
                    const Vector3 lineDirection = uAxisHint.Normalized();
                    normal = previousViewDirection
                        - lineDirection * Dot(previousViewDirection, lineDirection);
                    if (normal.LengthSquared() <= 1.0e-18) {
                        const Vector3 fallback = std::abs(lineDirection.z) < 0.9
                            ? Vector3{0.0, 0.0, 1.0}
                            : Vector3{0.0, 1.0, 0.0};
                        normal = fallback - lineDirection * Dot(fallback, lineDirection);
                    }
                }
            }
        } else {
            return false;
        }

        if (points.empty() || normal.LengthSquared() <= 1.0e-18
            || uAxisHint.LengthSquared() <= 1.0e-18) {
            return false;
        }
        normal = normal.Normalized();
        if (Dot(normal, previousViewDirection) < 0.0) {
            normal = -normal;
        }
        AlignToWorkPlane(kachakacha::model::WorkPlane::FromPointNormal(origin, normal, uAxisHint));

        const auto basis = CurrentViewBasis();
        Vector3 minimum{
            Dot(points.front(), basis[0]),
            Dot(points.front(), basis[1]),
            Dot(points.front(), basis[2])};
        Vector3 maximum = minimum;
        for (const Vector3& point : points) {
            const Vector3 coordinates{
                Dot(point, basis[0]),
                Dot(point, basis[1]),
                Dot(point, basis[2])};
            minimum.x = std::min(minimum.x, coordinates.x);
            minimum.y = std::min(minimum.y, coordinates.y);
            minimum.z = std::min(minimum.z, coordinates.z);
            maximum.x = std::max(maximum.x, coordinates.x);
            maximum.y = std::max(maximum.y, coordinates.y);
            maximum.z = std::max(maximum.z, coordinates.z);
        }
        const Vector3 middle = (minimum + maximum) * 0.5;
        target_ = basis[0] * middle.x + basis[1] * middle.y + basis[2] * middle.z;
        const double span = std::max({maximum.y - minimum.y, maximum.z - minimum.z, 10.0});
        const double available = std::max(160, std::min(width() - 150, height() - 80));
        pixelsPerMillimeter_ = std::clamp(available / (span * 1.25), 1.0, 80.0);
        update();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CadViewport::StopViewAnimation()
{
    if (viewAnimationTimer_ != nullptr && viewAnimationTimer_->isActive()) {
        viewAnimationTimer_->stop();
        viewAnimationProgress_ = 1.0;
    }
}

void CadViewport::SetIsometricView()
{
    StopViewAnimation();
    alignedViewBasis_.reset();
    yawRadians_ = 0.75;
    pitchRadians_ = 0.48;
    rollRadians_ = 0.0;
    update();
}

void CadViewport::SetCornerView(Vector3 direction)
{
    StopViewAnimation();
    direction = direction.Normalized();
    alignedViewBasis_.reset();
    yawRadians_ = std::atan2(direction.y, direction.x);
    pitchRadians_ = std::asin(std::clamp(direction.z, -1.0, 1.0));
    rollRadians_ = 0.0;
    update();
}

void CadViewport::SetDirectionView(Vector3 direction)
{
    if (direction.LengthSquared() <= 1.0e-12) {
        return;
    }
    direction = direction.Normalized();
    const auto current = CurrentViewBasis();
    Vector3 up{0.0, 0.0, 0.0};
    const bool faceView = std::abs(std::abs(direction.x) - 1.0) < 1.0e-9
        || std::abs(std::abs(direction.y) - 1.0) < 1.0e-9
        || std::abs(std::abs(direction.z) - 1.0) < 1.0e-9;
    if (faceView) {
        // 面ビュー: 面に垂直な4方向のうち、現在のアップに最も近いものを選ぶ
        // (上面図にしても直前の「どちらが前か」が保たれる)。
        const std::array<Vector3, 6> candidates = {
            Vector3{1.0, 0.0, 0.0}, Vector3{-1.0, 0.0, 0.0},
            Vector3{0.0, 1.0, 0.0}, Vector3{0.0, -1.0, 0.0},
            Vector3{0.0, 0.0, 1.0}, Vector3{0.0, 0.0, -1.0}};
        double bestScore = -2.0;
        for (const Vector3& candidate : candidates) {
            if (std::abs(Dot(candidate, direction)) > 1.0e-6) {
                continue;
            }
            const double score = Dot(candidate, current[2]);
            if (score > bestScore) {
                bestScore = score;
                up = candidate;
            }
        }
    } else {
        up = Vector3{0.0, 0.0, 1.0} - direction * direction.z;
        if (up.LengthSquared() <= 1.0e-9) {
            up = current[2] - direction * Dot(current[2], direction);
        }
    }
    if (up.LengthSquared() <= 1.0e-9) {
        SetCornerView(direction);
        return;
    }
    up = up.Normalized();
    const Vector3 right = Cross(up, direction).Normalized();
    AnimateViewTo({direction, right, Cross(direction, right).Normalized()});
}

void CadViewport::AnimateViewTo(const std::array<Vector3, 3>& targetBasis)
{
    const auto current = CurrentViewBasis();
    double difference = 0.0;
    for (int index = 0; index < 3; ++index) {
        difference += (targetBasis[index] - current[index]).Length();
    }
    if (!viewTransitionsEnabled_ || difference < 1.0e-6) {
        StopViewAnimation();
        alignedViewBasis_ = targetBasis;
        rollRadians_ = 0.0;
        update();
        return;
    }
    if (viewAnimationTimer_ == nullptr) {
        viewAnimationTimer_ = new QTimer(this);
        viewAnimationTimer_->setInterval(16);
        QObject::connect(viewAnimationTimer_, &QTimer::timeout, this, [this] {
            viewAnimationProgress_ = std::min(1.0, viewAnimationProgress_ + 16.0 / 180.0);
            const double t = viewAnimationProgress_;
            const double eased = t * t * (3.0 - 2.0 * t);
            const ViewQuaternion interpolated = SlerpQuaternion(
                BasisToQuaternion(viewAnimationStart_),
                BasisToQuaternion(viewAnimationTarget_), eased);
            alignedViewBasis_ = QuaternionToBasis(interpolated);
            rollRadians_ = 0.0;
            if (viewAnimationProgress_ >= 1.0) {
                alignedViewBasis_ = viewAnimationTarget_;
                viewAnimationTimer_->stop();
            }
            update();
        });
    }
    viewAnimationStart_ = current;
    viewAnimationTarget_ = targetBasis;
    viewAnimationProgress_ = 0.0;
    rollRadians_ = 0.0;
    alignedViewBasis_ = current;
    viewAnimationTimer_->start();
}

void CadViewport::RotateViewAroundWorldAxis(ViewRotationAxis axis, double angleRadians)
{
    if (!std::isfinite(angleRadians)) {
        return;
    }
    StopViewAnimation();
    const auto basis = CurrentViewBasis();
    const Vector3 rotationAxis = axis == ViewRotationAxis::X ? Vector3{1.0, 0.0, 0.0}
        : axis == ViewRotationAxis::Y ? Vector3{0.0, 1.0, 0.0}
                                      : Vector3{0.0, 0.0, 1.0};
    alignedViewBasis_ = std::array<Vector3, 3>{
        RotateVectorAroundAxis(basis[0], rotationAxis, angleRadians).Normalized(),
        RotateVectorAroundAxis(basis[1], rotationAxis, angleRadians).Normalized(),
        RotateVectorAroundAxis(basis[2], rotationAxis, angleRadians).Normalized(),
    };
    rollRadians_ = 0.0;
    update();
}

void CadViewport::RotateViewAroundRelativeAxis(ViewRotationAxis axis, double angleRadians)
{
    if (!std::isfinite(angleRadians)) {
        return;
    }
    StopViewAnimation();
    const auto basis = CurrentViewBasis();
    const Vector3 rotationAxis = axis == ViewRotationAxis::X ? basis[1]
        : axis == ViewRotationAxis::Y ? basis[2]
                                      : basis[0];
    alignedViewBasis_ = std::array<Vector3, 3>{
        RotateVectorAroundAxis(basis[0], rotationAxis, angleRadians).Normalized(),
        RotateVectorAroundAxis(basis[1], rotationAxis, angleRadians).Normalized(),
        RotateVectorAroundAxis(basis[2], rotationAxis, angleRadians).Normalized(),
    };
    rollRadians_ = 0.0;
    update();
}

void CadViewport::RotateViewYaw(double angleRadians)
{
    RotateViewAroundWorldAxis(ViewRotationAxis::Z, angleRadians);
}

void CadViewport::RollView(double angleRadians)
{
    RotateViewAroundRelativeAxis(ViewRotationAxis::Z, angleRadians);
}

Vector3 CadViewport::ViewDirection() const
{
    return CurrentViewBasis()[0];
}

Vector3 CadViewport::ViewRightDirection() const
{
    return CurrentViewBasis()[1];
}

Vector3 CadViewport::ViewUpDirection() const
{
    return CurrentViewBasis()[2];
}

void CadViewport::FinishDrawing()
{
    if (tool_ == ViewportTool::DrawPolyline && drawingPoints_.size() >= 2) {
        const std::vector<Vector3> points = drawingPoints_;
        drawingPoints_.clear();
        if (polylineCreated_) {
            polylineCreated_(points);
        }
    } else if (tool_ == ViewportTool::DrawSpline && drawingPoints_.size() >= 4) {
        const std::vector<Vector3> points = drawingPoints_;
        drawingPoints_.clear();
        if (splineCreated_) {
            splineCreated_(points);
        }
    } else {
        drawingPoints_.clear();
    }
    hoverDrawingPoint_.reset();
    drawingSnapHover_.reset();
    dynamicDimensionEditor_->hide();
    NotifyDrawingState();
    update();
}

void CadViewport::CancelDrawing()
{
    drawingPoints_.clear();
    hoverDrawingPoint_.reset();
    drawingSnapHover_.reset();
    splitPreviewParameter_.reset();
    directLineEditPreviewWire_.reset();
    directLineEditPreviewIntersection_.reset();
    if (dynamicDimensionEditor_ != nullptr) {
        dynamicDimensionEditor_->hide();
    }
    NotifyDrawingState();
    update();
}

DrawingMeasurements CadViewport::CurrentDrawingMeasurements() const
{
    DrawingMeasurements measurements;
    if (!activePlane_.has_value() || drawingPoints_.empty() || !hoverDrawingPoint_.has_value()) {
        return measurements;
    }

    const bool usesLastPoint = tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline
        || (tool_ == ViewportTool::DrawArc && arcDrawingMode_ == ArcDrawingMode::ThreePoints)
        || tool_ == ViewportTool::DrawBezier;
    const Vector3 anchor = usesLastPoint
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    const auto hover = activePlane_->Project(*hoverDrawingPoint_);
    const double deltaU = hover.u - start.u;
    const double deltaV = hover.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline
        || (tool_ == ViewportTool::DrawArc && arcDrawingMode_ == ArcDrawingMode::ThreePoints)
        || tool_ == ViewportTool::DrawBezier) {
        measurements.lengthMillimeters = std::hypot(deltaU, deltaV);
        measurements.angleDegrees = std::atan2(deltaV, deltaU) * 180.0 / std::numbers::pi;
        measurements.available = measurements.lengthMillimeters > 1.0e-9;
    } else if (tool_ == ViewportTool::DrawRectangle) {
        measurements.widthMillimeters = std::abs(deltaU);
        measurements.heightMillimeters = std::abs(deltaV);
        measurements.available = measurements.widthMillimeters > 1.0e-9
            && measurements.heightMillimeters > 1.0e-9;
    } else if (tool_ == ViewportTool::DrawCircle) {
        measurements.radiusMillimeters = std::hypot(deltaU, deltaV);
        measurements.available = measurements.radiusMillimeters > 1.0e-9;
    }
    return measurements;
}

bool CadViewport::HasDynamicDimensions() const noexcept
{
    return tool_ == ViewportTool::DrawLine
        || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawRectangle
        || tool_ == ViewportTool::DrawCircle
        || (tool_ == ViewportTool::DrawArc && arcDrawingMode_ == ArcDrawingMode::ThreePoints)
        || tool_ == ViewportTool::DrawBezier
        || tool_ == ViewportTool::DrawSpline;
}

void CadViewport::UpdateDynamicDimensionEditor()
{
    if (dynamicDimensionEditor_ == nullptr || !HasDynamicDimensions()
        || !activePlane_.has_value() || drawingPoints_.empty()) {
        if (dynamicDimensionEditor_ != nullptr) {
            dynamicDimensionEditor_->hide();
        }
        return;
    }

    const bool rectangle = tool_ == ViewportTool::DrawRectangle;
    const bool circle = tool_ == ViewportTool::DrawCircle;
    const bool line = tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline;
    dynamicPrimaryLabel_->setText(
        rectangle ? QStringLiteral("幅 mm")
        : circle ? QStringLiteral("半径 mm")
        : line ? QStringLiteral("長さ mm")
               : QStringLiteral("距離 mm"));
    dynamicSecondaryLabel_->setText(rectangle ? QStringLiteral("高さ mm") : QStringLiteral("角度 °"));
    dynamicSecondaryLabel_->setVisible(!circle);
    dynamicSecondaryField_->setVisible(!circle);

    const DrawingMeasurements measurements = CurrentDrawingMeasurements();
    if (measurements.available) {
        if (!dynamicPrimaryField_->hasFocus()) {
            dynamicPrimaryField_->setText(DimensionText(
                rectangle ? measurements.widthMillimeters
                : circle ? measurements.radiusMillimeters
                         : measurements.lengthMillimeters));
        }
        if (!circle && !dynamicSecondaryField_->hasFocus()) {
            dynamicSecondaryField_->setText(DimensionText(
                rectangle ? measurements.heightMillimeters : measurements.angleDegrees));
        }
    } else {
        if (!dynamicPrimaryField_->hasFocus() && dynamicPrimaryField_->text().isEmpty()) {
            dynamicPrimaryField_->setText(circle ? QStringLiteral("5") : QStringLiteral("10"));
        }
        if (!circle && !dynamicSecondaryField_->hasFocus() && dynamicSecondaryField_->text().isEmpty()) {
            dynamicSecondaryField_->setText(rectangle ? QStringLiteral("10") : QStringLiteral("0"));
        }
    }

    dynamicDimensionEditor_->adjustSize();
    PositionDynamicDimensionEditor();
    dynamicDimensionEditor_->show();
    dynamicDimensionEditor_->raise();
}

void CadViewport::PositionDynamicDimensionEditor()
{
    if (dynamicDimensionEditor_ == nullptr || drawingPoints_.empty()) {
        return;
    }
    QPoint anchor = hoverScreenPosition_;
    if (!rect().contains(anchor)) {
        anchor = ProjectPoint(drawingPoints_.back()).toPoint();
    }
    int x = anchor.x() + 16;
    int y = anchor.y() + 18;
    if (x + dynamicDimensionEditor_->width() > width() - 8) {
        x = anchor.x() - dynamicDimensionEditor_->width() - 16;
    }
    if (y + dynamicDimensionEditor_->height() > height() - 8) {
        y = anchor.y() - dynamicDimensionEditor_->height() - 16;
    }
    dynamicDimensionEditor_->move(
        std::clamp(x, 8, std::max(8, width() - dynamicDimensionEditor_->width() - 8)),
        std::clamp(y, 8, std::max(8, height() - dynamicDimensionEditor_->height() - 8)));
}

void CadViewport::BeginDynamicDimensionInput(const QString& initialText, bool secondary)
{
    UpdateDynamicDimensionEditor();
    if (!dynamicDimensionEditor_->isVisible()) {
        return;
    }
    QLineEdit* field = secondary && dynamicSecondaryField_->isVisible()
        ? dynamicSecondaryField_
        : dynamicPrimaryField_;
    SetDynamicDimensionFieldError(field, false);
    field->setFocus(Qt::ShortcutFocusReason);
    field->selectAll();
    if (!initialText.isEmpty()) {
        field->setText(initialText);
        field->setCursorPosition(field->text().size());
    }
}

void CadViewport::SetDynamicDimensionFieldError(QLineEdit* field, bool error)
{
    if (field == nullptr || field->property("expressionError").toBool() == error) {
        return;
    }
    field->setProperty("expressionError", error);
    field->style()->unpolish(field);
    field->style()->polish(field);
}

bool CadViewport::ValidateDynamicDimensionField(QLineEdit* field, bool positiveOnly)
{
    const std::optional<double> value = field != nullptr
        ? EvaluateDimensionExpression(field->text())
        : std::nullopt;
    const bool valid = value.has_value() && std::isfinite(*value)
        && (!positiveOnly || *value > 1.0e-9);
    SetDynamicDimensionFieldError(field, !valid);
    if (!valid && field != nullptr) {
        QToolTip::showText(
            field->mapToGlobal(QPoint(0, field->height() + 3)),
            positiveOnly
                ? QStringLiteral("0より大きい数値または計算式を入力してください")
                : QStringLiteral("計算式を確認してください"),
            field);
    }
    return valid;
}

bool CadViewport::CommitDynamicDimensionInput()
{
    const bool circle = tool_ == ViewportTool::DrawCircle;
    const bool rectangle = tool_ == ViewportTool::DrawRectangle;
    if (!ValidateDynamicDimensionField(dynamicPrimaryField_, true)
        || (!circle && !ValidateDynamicDimensionField(dynamicSecondaryField_, rectangle))) {
        return false;
    }

    const double primary = *EvaluateDimensionExpression(dynamicPrimaryField_->text());
    const double secondary = circle
        ? 0.0
        : *EvaluateDimensionExpression(dynamicSecondaryField_->text());
    if (!CommitDrawingDimensions(primary, secondary)) {
        return false;
    }

    dynamicPrimaryField_->clearFocus();
    dynamicSecondaryField_->clearFocus();
    setFocus(Qt::OtherFocusReason);
    UpdateDynamicDimensionEditor();
    return true;
}

bool CadViewport::CommitDrawingDimensions(double primaryMillimeters, double secondaryValue)
{
    if (!activePlane_.has_value() || drawingPoints_.empty()
        || !std::isfinite(primaryMillimeters) || primaryMillimeters <= 1.0e-9
        || !std::isfinite(secondaryValue)) {
        return false;
    }

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline
        || (tool_ == ViewportTool::DrawArc && arcDrawingMode_ == ArcDrawingMode::ThreePoints)
        || tool_ == ViewportTool::DrawBezier) {
        const bool usesLastPoint = tool_ != ViewportTool::DrawLine;
        const Vector3 anchor = usesLastPoint
            ? drawingPoints_.back()
            : drawingPoints_.front();
        const auto coordinates = activePlane_->Project(anchor);
        const double angleRadians = secondaryValue * std::numbers::pi / 180.0;
        CommitDrawingPoint(activePlane_->ToWorld(
            coordinates.u + primaryMillimeters * std::cos(angleRadians),
            coordinates.v + primaryMillimeters * std::sin(angleRadians)));
        return true;
    }

    if (tool_ == ViewportTool::DrawRectangle && secondaryValue > 1.0e-9) {
        const auto start = activePlane_->Project(drawingPoints_.front());
        double directionU = 1.0;
        double directionV = 1.0;
        if (hoverDrawingPoint_.has_value()) {
            const auto hover = activePlane_->Project(*hoverDrawingPoint_);
            directionU = hover.u < start.u ? -1.0 : 1.0;
            directionV = hover.v < start.v ? -1.0 : 1.0;
        }
        CommitDrawingPoint(activePlane_->ToWorld(
            start.u + directionU * primaryMillimeters,
            start.v + directionV * secondaryValue));
        return true;
    }

    if (tool_ == ViewportTool::DrawCircle) {
        const auto center = activePlane_->Project(drawingPoints_.front());
        CommitDrawingPoint(activePlane_->ToWorld(center.u + primaryMillimeters, center.v));
        return true;
    }
    return false;
}

bool CadViewport::IsSelected(CadSelectionKind kind, int index) const
{
    return std::any_of(selections_.begin(), selections_.end(), [&](const CadSelection& selection) {
        return selection.kind == kind && selection.index == index;
    });
}

bool CadViewport::HiddenBySet(CadSelectionKind kind, int index) const
{
    if (project_ == nullptr || project_->ObjectSets().empty() || index < 0) {
        return false;
    }
    using kachakacha::model::ObjectSetState;
    using kachakacha::model::ProjectObjectKind;
    const std::string* name = nullptr;
    ProjectObjectKind objectKind = ProjectObjectKind::Wire;
    switch (kind) {
    case CadSelectionKind::WorkPlane:
        if (index >= static_cast<int>(project_->WorkPlanes().size())) return false;
        name = &project_->WorkPlanes()[index].name;
        objectKind = ProjectObjectKind::WorkPlane;
        break;
    case CadSelectionKind::Point:
        if (index >= static_cast<int>(project_->Points().size())) return false;
        name = &project_->Points()[index].name;
        objectKind = ProjectObjectKind::Point;
        break;
    case CadSelectionKind::Wire:
        if (index >= static_cast<int>(project_->Wires().size())) return false;
        name = &project_->Wires()[index].name;
        objectKind = ProjectObjectKind::Wire;
        break;
    case CadSelectionKind::Surface:
        if (index >= static_cast<int>(project_->Surfaces().size())) return false;
        name = &project_->Surfaces()[index].name;
        objectKind = ProjectObjectKind::Surface;
        break;
    case CadSelectionKind::Plate:
        if (index >= static_cast<int>(project_->Plates().size())) return false;
        name = &project_->Plates()[index].name;
        objectKind = ProjectObjectKind::Plate;
        break;
    case CadSelectionKind::Body:
        if (index >= static_cast<int>(project_->Bodies().size())) return false;
        name = &project_->Bodies()[index].name;
        objectKind = ProjectObjectKind::Body;
        break;
    default:
        return false;
    }
    return project_->ObjectStateInSets(objectKind, *name) == ObjectSetState::Hidden;
}

bool CadViewport::ShouldDisplay(
    CadSelectionKind kind,
    int index,
    bool projectVisible) const
{
    if (displayMode_ == ViewportDisplayMode::IsolatedSelection) {
        return std::any_of(
            isolatedSelections_.begin(), isolatedSelections_.end(),
            [&](const CadSelection& selection) {
                return selection.kind == kind && selection.index == index;
            });
    }
    if (!projectVisible) {
        return false;
    }
    if (HiddenBySet(kind, index)) {
        return false;
    }
    if (displayMode_ == ViewportDisplayMode::Design) {
        return true;
    }
    if (displayMode_ == ViewportDisplayMode::FinishedModel) {
        return kind == CadSelectionKind::Plate || kind == CadSelectionKind::Body;
    }
    return false;
}

std::array<Vector3, 3> CadViewport::CurrentViewBasis() const
{
    std::array<Vector3, 3> basis;
    if (alignedViewBasis_.has_value()) {
        basis = *alignedViewBasis_;
    } else {
        const Vector3 viewDirection = {
            std::cos(pitchRadians_) * std::cos(yawRadians_),
            std::cos(pitchRadians_) * std::sin(yawRadians_),
            std::sin(pitchRadians_),
        };
        Vector3 right{-viewDirection.y, viewDirection.x, 0.0};
        if (right.LengthSquared() <= 1.0e-12) {
            right = {1.0, 0.0, 0.0};
        } else {
            right = right.Normalized();
        }
        basis = {viewDirection, right, Cross(viewDirection, right).Normalized()};
    }
    if (std::abs(rollRadians_) <= 1.0e-12) {
        return basis;
    }
    const Vector3 right = basis[1] * std::cos(rollRadians_)
        + basis[2] * std::sin(rollRadians_);
    const Vector3 up = basis[2] * std::cos(rollRadians_)
        - basis[1] * std::sin(rollRadians_);
    return {basis[0], right, up};
}

void CadViewport::OrbitViewByPixels(double horizontalPixels, double verticalPixels)
{
    if (!std::isfinite(horizontalPixels) || !std::isfinite(verticalPixels)) {
        return;
    }
    StopViewAnimation();

    auto basis = CurrentViewBasis();
    const double horizontalAngle = -horizontalPixels * 0.008;
    const double verticalAngle = verticalPixels * 0.008;

    basis[0] = RotateVectorAroundAxis(basis[0], basis[2], horizontalAngle).Normalized();
    basis[1] = RotateVectorAroundAxis(basis[1], basis[2], horizontalAngle).Normalized();
    basis[0] = RotateVectorAroundAxis(basis[0], basis[1], verticalAngle).Normalized();
    basis[2] = RotateVectorAroundAxis(basis[2], basis[1], verticalAngle).Normalized();

    basis[1] = (basis[1] - basis[0] * Dot(basis[1], basis[0])).Normalized();
    basis[2] = Cross(basis[0], basis[1]).Normalized();
    alignedViewBasis_ = basis;
    rollRadians_ = 0.0;
}

QPointF CadViewport::ProjectPoint(Vector3 point) const
{
    const auto basis = CurrentViewBasis();
    const Vector3& right = basis[1];
    const Vector3& up = basis[2];
    const Vector3 relative = point - target_;
    return {
        width() * 0.5 + Dot(relative, right) * pixelsPerMillimeter_,
        height() * 0.5 - Dot(relative, up) * pixelsPerMillimeter_,
    };
}

void CadViewport::FitAll()
{
    if (project_ == nullptr) {
        return;
    }

    Vector3 minimum{0.0, 0.0, 0.0};
    Vector3 maximum{0.0, 0.0, 0.0};
    bool hasPoint = false;
    auto include = [&](Vector3 point) {
        if (!hasPoint) {
            minimum = point;
            maximum = point;
            hasPoint = true;
            return;
        }
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    };

    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const auto& wire = project_->Wires()[index];
        if (!ShouldDisplay(CadSelectionKind::Wire, index, wire.visible)) {
            continue;
        }
        for (int sample = 0; sample <= 32; ++sample) {
            include(wire.wire.Evaluate(static_cast<double>(sample) / 32.0));
        }
    }
    for (int index = 0; index < static_cast<int>(project_->Surfaces().size()); ++index) {
        const auto& surface = project_->Surfaces()[index];
        if (!ShouldDisplay(CadSelectionKind::Surface, index, surface.visible)) {
            continue;
        }
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                include(surface.surface.Evaluate(
                    static_cast<double>(uIndex) / 24.0,
                    static_cast<double>(vIndex) / 8.0));
            }
        }
    }
    for (int index = 0; index < static_cast<int>(project_->Plates().size()); ++index) {
        const auto& plate = project_->Plates()[index];
        if (!ShouldDisplay(CadSelectionKind::Plate, index, plate.visible)) {
            continue;
        }
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                const double u = static_cast<double>(uIndex) / 24.0;
                const double v = static_cast<double>(vIndex) / 8.0;
                include(plate.plate.Evaluate(u, v, 0.0));
                include(plate.plate.Evaluate(u, v, 1.0));
            }
        }
    }
    for (int index = 0; index < static_cast<int>(project_->Bodies().size()); ++index) {
        const auto& body = project_->Bodies()[index];
        if (!ShouldDisplay(CadSelectionKind::Body, index, body.visible)) {
            continue;
        }
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                const double u = static_cast<double>(uIndex) / 24.0;
                const double v = static_cast<double>(vIndex) / 8.0;
                include(body.body.Evaluate(u, v, 0.0));
                include(body.body.Evaluate(u, v, 1.0));
            }
        }
    }
    for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
        const auto& plane = project_->WorkPlanes()[index];
        if (ShouldDisplay(CadSelectionKind::WorkPlane, index, plane.visible)) {
            include(plane.plane.Origin());
        }
    }
    for (int index = 0; index < static_cast<int>(project_->Points().size()); ++index) {
        const auto& point = project_->Points()[index];
        if (ShouldDisplay(CadSelectionKind::Point, index, point.visible)) {
            include(point.point);
        }
    }

    if (!hasPoint) {
        target_ = {};
        pixelsPerMillimeter_ = 14.0;
    } else {
        target_ = (minimum + maximum) * 0.5;
        const double span = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 20.0});
        const double available = std::max(200, std::min(width(), height()));
        pixelsPerMillimeter_ = std::clamp(available / (span * 1.45), 1.0, 80.0);
    }
    update();
}

std::optional<Vector3> CadViewport::PointOnPlane(
    QPointF position,
    const kachakacha::model::WorkPlane& plane) const
{
    const auto basis = CurrentViewBasis();
    const Vector3& viewDirection = basis[0];
    const Vector3& right = basis[1];
    const Vector3& up = basis[2];
    const double screenX = (position.x() - width() * 0.5) / pixelsPerMillimeter_;
    const double screenY = (height() * 0.5 - position.y()) / pixelsPerMillimeter_;
    const Vector3 rayPoint = target_ + right * screenX + up * screenY;
    const double denominator = Dot(viewDirection, plane.Normal());
    if (std::abs(denominator) <= 1.0e-9) {
        return std::nullopt;
    }
    const double distance = Dot(plane.Origin() - rayPoint, plane.Normal()) / denominator;
    return rayPoint + viewDirection * distance;
}

std::optional<Vector3> CadViewport::PointOnActivePlane(QPointF position) const
{
    return activePlane_.has_value()
        ? PointOnPlane(position, *activePlane_)
        : std::nullopt;
}

std::optional<WireControlPointPick> CadViewport::NearestEditableControlPoint(
    QPointF position,
    double maximumDistance) const
{
    if (project_ == nullptr || tool_ != ViewportTool::Select
        || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return std::nullopt;
    }

    double bestDistance = maximumDistance;
    std::optional<WireControlPointPick> best;
    for (const CadSelection& selected : selections_) {
        if (selected.kind != CadSelectionKind::Wire || selected.index < 0
            || selected.index >= static_cast<int>(project_->Wires().size())) {
            continue;
        }
        const NamedWire& namedWire = project_->Wires()[selected.index];
        if (!ShouldDisplay(CadSelectionKind::Wire, selected.index, namedWire.visible)
            || namedWire.projection.has_value()) {
            continue;
        }
        const auto& points = namedWire.wire.ControlPoints();
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (namedWire.wire.Kind() == WireKind::Circle && index > 1) {
                continue;
            }
            if (namedWire.wire.Kind() == WireKind::Circle && index == 1
                && namedWire.metadata.curveConstraints.radiusMillimeters.has_value()) {
                continue;
            }
            const double distance = QLineF(position, ProjectPoint(points[index])).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = WireControlPointPick{selected.index, index};
            }
        }
    }
    return best;
}

std::optional<double> CadViewport::NearestWireParameter(
    int wireIndex,
    QPointF position,
    double maximumDistance,
    bool allowEndpoints) const
{
    if (project_ == nullptr || wireIndex < 0 || wireIndex >= static_cast<int>(project_->Wires().size())
        || !ShouldDisplay(
            CadSelectionKind::Wire, wireIndex, project_->Wires()[wireIndex].visible)) {
        return std::nullopt;
    }
    const Wire& wire = project_->Wires()[wireIndex].wire;
    const int samples = wire.Kind() == WireKind::Line ? 1 : 256;
    double bestDistance = maximumDistance;
    double bestParameter = 0.0;
    QPointF previous = ProjectPoint(wire.Evaluate(0.0));
    for (int sample = 0; sample < samples; ++sample) {
        const QPointF current = ProjectPoint(wire.Evaluate(static_cast<double>(sample + 1) / samples));
        const QPointF segment = current - previous;
        const double lengthSquared = QPointF::dotProduct(segment, segment);
        const double local = lengthSquared <= 1.0e-12
            ? 0.0
            : std::clamp(QPointF::dotProduct(position - previous, segment) / lengthSquared, 0.0, 1.0);
        const double distance = QLineF(position, previous + segment * local).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            bestParameter = (static_cast<double>(sample) + local) / samples;
        }
        previous = current;
    }
    if (bestDistance >= maximumDistance
        || (!allowEndpoints && (bestParameter <= 1.0e-6 || bestParameter >= 1.0 - 1.0e-6))) {
        return std::nullopt;
    }
    return bestParameter;
}

std::optional<Vector3> CadViewport::FindConnectablePoint(
    QPointF position, double maximumDistance) const
{
    if (project_ == nullptr || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return std::nullopt;
    }
    double bestDistance = maximumDistance;
    std::optional<Vector3> best;
    bool bestIsIntersection = false;
    const auto consider = [&](Vector3 candidate, bool isIntersection) {
        const double distance = QLineF(position, ProjectPoint(candidate)).length();
        // ほぼ同距離なら交点を優先する(端点と交点が重なりがちなため)。
        if (distance < bestDistance - 1.0e-9
            || (isIntersection && !bestIsIntersection && distance <= bestDistance + 2.0)) {
            if (distance <= maximumDistance) {
                bestDistance = std::min(bestDistance, distance);
                best = candidate;
                bestIsIntersection = isIntersection;
            }
        }
    };
    for (int index = 0; index < static_cast<int>(project_->Points().size()); ++index) {
        const auto& point = project_->Points()[index];
        if (!ShouldDisplay(CadSelectionKind::Point, index, point.visible)) {
            continue;
        }
        consider(point.point, false);
    }
    std::vector<int> nearbyWires;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const NamedWire& namedWire = project_->Wires()[index];
        if (!ShouldDisplay(CadSelectionKind::Wire, index, namedWire.visible)) {
            continue;
        }
        if (!namedWire.wire.IsClosed()) {
            consider(namedWire.wire.Start(), false);
            consider(namedWire.wire.End(), false);
        }
        if (nearbyWires.size() < 6
            && NearestWireParameter(index, position, maximumDistance, true).has_value()) {
            nearbyWires.push_back(index);
        }
    }
    for (std::size_t first = 0; first < nearbyWires.size(); ++first) {
        for (std::size_t second = first + 1; second < nearbyWires.size(); ++second) {
            try {
                const auto intersections = kachakacha::model::IntersectWires(
                    project_->Wires()[nearbyWires[first]].wire,
                    project_->Wires()[nearbyWires[second]].wire);
                for (const Vector3& intersection : intersections) {
                    consider(intersection, true);
                }
            } catch (const std::exception&) {
                // 交点計算に失敗した組は候補に含めない。
            }
        }
    }
    return best;
}

std::optional<WireEndpointPick> CadViewport::NearestWireEndpoint(
    QPointF position,
    double maximumDistance) const
{
    if (project_ == nullptr || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return std::nullopt;
    }
    double bestDistance = maximumDistance;
    std::optional<WireEndpointPick> best;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const NamedWire& namedWire = project_->Wires()[index];
        if (!ShouldDisplay(CadSelectionKind::Wire, index, namedWire.visible)
            || namedWire.projection.has_value() || namedWire.wire.IsClosed()) {
            continue;
        }
        if (tool_ == ViewportTool::Tangent && !coincidencePicks_.empty()
            && namedWire.wire.Kind() != WireKind::CubicBezier
            && namedWire.wire.Kind() != WireKind::CubicBSpline
            && namedWire.wire.Kind() != WireKind::CircularArc) {
            continue;
        }
        if (tool_ == ViewportTool::Curvature && !coincidencePicks_.empty()
            && namedWire.wire.Kind() != WireKind::CubicBezier) {
            continue;
        }
        for (const auto endpoint : {kachakacha::model::WireEndpoint::Start, kachakacha::model::WireEndpoint::End}) {
            const Vector3 point = endpoint == kachakacha::model::WireEndpoint::Start
                ? namedWire.wire.Start()
                : namedWire.wire.End();
            const double distance = QLineF(position, ProjectPoint(point)).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = WireEndpointPick{index, endpoint, point};
            }
        }
    }
    return best;
}

void CadViewport::CommitMeasurementPick(QPointF position)
{
    if (project_ == nullptr) {
        return;
    }

    MeasurementPick pick;
    const CadSelection hit = HitTest(position);
    if (measurementMode_ == MeasurementMode::Elements) {
        if (hit.kind == CadSelectionKind::Wire) {
            const auto parameter = NearestWireParameter(hit.index, position, 14.0, true);
            if (!parameter.has_value()) {
                return;
            }
            pick = {
                MeasurementPickKind::Wire,
                hit.index,
                project_->Wires()[hit.index].wire.Evaluate(*parameter),
                *parameter,
            };
        } else if (hit.kind == CadSelectionKind::WorkPlane) {
            pick = {
                MeasurementPickKind::WorkPlane,
                hit.index,
                project_->WorkPlanes()[hit.index].plane.Origin(),
                0.0,
            };
        } else if (hit.kind == CadSelectionKind::Point
            && hit.index >= 0 && hit.index < static_cast<int>(project_->Points().size())) {
            pick = {
                MeasurementPickKind::Point,
                hit.index,
                project_->Points()[hit.index].point,
                0.0,
            };
        } else {
            const auto point = PointOnActivePlane(position);
            if (!point.has_value()) {
                return;
            }
            pick = {MeasurementPickKind::Point, -1, SnapPoint(*point, position), 0.0};
        }
    } else {
        if (hit.kind == CadSelectionKind::Point
            && hit.index >= 0 && hit.index < static_cast<int>(project_->Points().size())) {
            pick = {
                MeasurementPickKind::Point,
                hit.index,
                project_->Points()[hit.index].point,
                0.0,
            };
        } else if (hit.kind == CadSelectionKind::Wire) {
            const auto parameter = NearestWireParameter(hit.index, position, 14.0, true);
            if (parameter.has_value()) {
                pick = {
                    MeasurementPickKind::Point,
                    hit.index,
                    project_->Wires()[hit.index].wire.Evaluate(*parameter),
                    *parameter,
                };
            } else {
                return;
            }
        } else {
            const auto point = PointOnActivePlane(position);
            if (!point.has_value()) {
                return;
            }
            pick = {MeasurementPickKind::Point, -1, SnapPoint(*point, position), 0.0};
        }
    }

    const std::size_t requiredPicks = measurementMode_ == MeasurementMode::ThreePointsAngle
        ? 3 : 2;
    if (measurementPicks_.size() >= requiredPicks
        || (measurementMode_ == MeasurementMode::Elements
            && measurementPicks_.size() == 1
            && measurementPicks_.front().kind == pick.kind
            && measurementPicks_.front().index == pick.index)) {
        measurementPicks_.clear();
        measurementOverlayFirst_.reset();
        measurementOverlaySecond_.reset();
        measurementOverlayThird_.reset();
        measurementOverlayText_.clear();
    }
    measurementPicks_.push_back(pick);
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
    update();
}

void CadViewport::CommitCoincidencePick(QPointF position)
{
    const std::optional<WireEndpointPick> pick = NearestWireEndpoint(position, 14.0);
    if (!pick.has_value()) {
        return;
    }
    if (!coincidencePicks_.empty()
        && coincidencePicks_.front().wireIndex == pick->wireIndex) {
        return;
    }
    coincidencePicks_.push_back(*pick);
    if (coincidencePicks_.size() == 2) {
        const WireEndpointPick anchor = coincidencePicks_[0];
        const WireEndpointPick follower = coincidencePicks_[1];
        coincidencePicks_.clear();
        if (tool_ == ViewportTool::Curvature && curvatureRequested_) {
            curvatureRequested_(anchor, follower);
        } else if (tool_ == ViewportTool::Tangent && tangentRequested_) {
            tangentRequested_(anchor, follower);
        } else if (coincidenceRequested_) {
            coincidenceRequested_(anchor, follower);
        }
    }
    NotifyDrawingState();
    update();
}

std::optional<DrawingSnapCandidate> CadViewport::FindDrawingSnap(
    Vector3 point,
    QPointF screenPosition,
    bool nearbyStructuralOnly) const
{
    if (!activePlane_.has_value() || !snapEnabled_) {
        return std::nullopt;
    }

    std::optional<DrawingSnapCandidate> best;
    const auto priority = [](DrawingSnapKind kind) {
        switch (kind) {
        case DrawingSnapKind::Intersection: return 6;
        case DrawingSnapKind::Point: return 5;
        case DrawingSnapKind::Endpoint: return 4;
        case DrawingSnapKind::ProjectedPoint: return 3;
        case DrawingSnapKind::Extension: return 2;
        case DrawingSnapKind::Grid: return 1;
        case DrawingSnapKind::None: return 0;
        }
        return 0;
    };
    // スナップの階級: 平面上の実在ジオメトリ(2) > 投影・延長の推測(1) > グリッド(0)。
    // 推測スナップは実在候補が届かない時だけ効かせ、従来の挙動を変えない。
    const auto snapClass = [](DrawingSnapKind kind) {
        switch (kind) {
        case DrawingSnapKind::Intersection:
        case DrawingSnapKind::Point:
        case DrawingSnapKind::Endpoint:
            return 2;
        case DrawingSnapKind::ProjectedPoint:
        case DrawingSnapKind::Extension:
            return 1;
        case DrawingSnapKind::Grid:
        case DrawingSnapKind::None:
            return 0;
        }
        return 0;
    };
    const auto consider = [&](DrawingSnapKind kind, Vector3 candidate, double maximumDistance,
                              std::optional<Vector3> guideAnchor = std::nullopt) {
        const double distance = QLineF(screenPosition, ProjectPoint(candidate)).length();
        if (distance > maximumDistance) {
            return;
        }
        const int candidateClass = snapClass(kind);
        const int bestClass = best.has_value() ? snapClass(best->kind) : -1;
        if (!best.has_value()
            || candidateClass > bestClass
            || (candidateClass == bestClass && distance < best->distancePixels - 0.15)
            || (candidateClass == bestClass
                && std::abs(distance - best->distancePixels) <= 0.15
                && priority(kind) > priority(best->kind))) {
            best = DrawingSnapCandidate{kind, candidate, distance, guideAnchor};
        }
    };

    const double pointRadius = nearbyStructuralOnly ? 18.0 : 12.0;
    const double intersectionRadius = nearbyStructuralOnly ? 19.0 : 14.0;
    const double segmentSearchRadius = nearbyStructuralOnly ? 22.0 : 16.0;

    if (project_ != nullptr) {
        for (int pointIndex = 0; pointIndex < static_cast<int>(project_->Points().size()); ++pointIndex) {
            const auto& namedPoint = project_->Points()[pointIndex];
            if (!ShouldDisplay(CadSelectionKind::Point, pointIndex, namedPoint.visible)) {
                continue;
            }
            const auto planeCoordinates = activePlane_->Project(namedPoint.point);
            if (std::abs(planeCoordinates.w) <= 1.0e-6) {
                consider(DrawingSnapKind::Point, namedPoint.point, pointRadius);
            } else {
                // 別平面・空間上の点は、作業平面へ法線投影した位置を参照できる
                // (Inventor の「ジオメトリを投影」に相当する暗黙投影)。
                consider(DrawingSnapKind::ProjectedPoint,
                    activePlane_->ToWorld(planeCoordinates.u, planeCoordinates.v),
                    pointRadius, namedPoint.point);
            }
        }

        struct PlaneSegment {
            double au = 0.0;
            double av = 0.0;
            double bu = 0.0;
            double bv = 0.0;
            int wireIndex = -1;
        };
        std::vector<PlaneSegment> nearbySegments;
        for (int wireIndex = 0; wireIndex < static_cast<int>(project_->Wires().size()); ++wireIndex) {
            const auto& namedWire = project_->Wires()[wireIndex];
            if (!ShouldDisplay(CadSelectionKind::Wire, wireIndex, namedWire.visible)) {
                continue;
            }
            std::vector<Vector3> snapPoints;
            if (namedWire.wire.Kind() == WireKind::Polyline) {
                snapPoints = namedWire.wire.ControlPoints();
            } else {
                snapPoints = {namedWire.wire.Start(), namedWire.wire.End()};
            }
            for (const Vector3& endpoint : snapPoints) {
                const auto planeCoordinates = activePlane_->Project(endpoint);
                if (std::abs(planeCoordinates.w) <= 1.0e-6) {
                    consider(DrawingSnapKind::Endpoint, endpoint, pointRadius);
                } else {
                    consider(DrawingSnapKind::ProjectedPoint,
                        activePlane_->ToWorld(planeCoordinates.u, planeCoordinates.v),
                        pointRadius - 2.0, endpoint);
                }
            }

            std::vector<Vector3> samples;
            if (namedWire.wire.Kind() == WireKind::Line
                || namedWire.wire.Kind() == WireKind::Polyline) {
                samples = namedWire.wire.ControlPoints();
            } else {
                constexpr int kCurveIntersectionSamples = 64;
                samples.reserve(kCurveIntersectionSamples + 1);
                for (int sample = 0; sample <= kCurveIntersectionSamples; ++sample) {
                    samples.push_back(namedWire.wire.Evaluate(
                        static_cast<double>(sample) / kCurveIntersectionSamples));
                }
            }
            for (std::size_t index = 1; index < samples.size(); ++index) {
                const auto first = activePlane_->Project(samples[index - 1]);
                const auto second = activePlane_->Project(samples[index]);
                if (std::abs(first.w) > 1.0e-6 || std::abs(second.w) > 1.0e-6
                    || DistanceToSegment(
                           screenPosition,
                           ProjectPoint(samples[index - 1]),
                           ProjectPoint(samples[index])) > segmentSearchRadius) {
                    continue;
                }
                nearbySegments.push_back({
                    first.u, first.v, second.u, second.v, wireIndex,
                });
            }
        }

        for (std::size_t firstIndex = 0; firstIndex < nearbySegments.size(); ++firstIndex) {
            const PlaneSegment& first = nearbySegments[firstIndex];
            const double firstU = first.bu - first.au;
            const double firstV = first.bv - first.av;
            for (std::size_t secondIndex = firstIndex + 1;
                 secondIndex < nearbySegments.size(); ++secondIndex) {
                const PlaneSegment& second = nearbySegments[secondIndex];
                if (first.wireIndex == second.wireIndex) {
                    continue;
                }
                const double secondU = second.bu - second.au;
                const double secondV = second.bv - second.av;
                const double denominator = firstU * secondV - firstV * secondU;
                if (std::abs(denominator) <= 1.0e-12) {
                    continue;
                }
                const double deltaU = second.au - first.au;
                const double deltaV = second.av - first.av;
                const double firstParameter = (deltaU * secondV - deltaV * secondU) / denominator;
                const double secondParameter = (deltaU * firstV - deltaV * firstU) / denominator;
                if (firstParameter < -1.0e-8 || firstParameter > 1.0 + 1.0e-8
                    || secondParameter < -1.0e-8 || secondParameter > 1.0 + 1.0e-8) {
                    continue;
                }
                consider(
                    DrawingSnapKind::Intersection,
                    activePlane_->ToWorld(
                        first.au + firstU * firstParameter,
                        first.av + firstV * firstParameter),
                    intersectionRadius);
            }
        }

        // 延長線の推測(作図補助): 既存の直線分の延長上へカーソルが来たら、
        // 破線ガイド付きでその延長線上にスナップする(同一直線の作図補助)。
        {
            constexpr double kExtensionPerpendicularPixels = 6.0;
            constexpr double kExtensionReachPixels = 480.0;
            for (int wireIndex = 0; wireIndex < static_cast<int>(project_->Wires().size()); ++wireIndex) {
                const auto& namedWire = project_->Wires()[wireIndex];
                if (!ShouldDisplay(CadSelectionKind::Wire, wireIndex, namedWire.visible)
                    || (namedWire.wire.Kind() != WireKind::Line
                        && namedWire.wire.Kind() != WireKind::Polyline)) {
                    continue;
                }
                const auto& points = namedWire.wire.ControlPoints();
                for (std::size_t index = 1; index < points.size(); ++index) {
                    const auto firstCoordinates = activePlane_->Project(points[index - 1]);
                    const auto secondCoordinates = activePlane_->Project(points[index]);
                    if (std::abs(firstCoordinates.w) > 1.0e-6
                        || std::abs(secondCoordinates.w) > 1.0e-6) {
                        continue;
                    }
                    const QPointF screenA = ProjectPoint(points[index - 1]);
                    const QPointF screenB = ProjectPoint(points[index]);
                    const QPointF direction = screenB - screenA;
                    const double lengthSquared = QPointF::dotProduct(direction, direction);
                    if (lengthSquared <= 1.0e-9) {
                        continue;
                    }
                    const double along = QPointF::dotProduct(
                        screenPosition - screenA, direction) / lengthSquared;
                    if (along >= -1.0e-9 && along <= 1.0 + 1.0e-9) {
                        continue; // 線分の内側は通常のスナップに任せる。
                    }
                    const QPointF footScreen = screenA + direction * along;
                    const double perpendicular = QLineF(screenPosition, footScreen).length();
                    if (perpendicular > kExtensionPerpendicularPixels) {
                        continue;
                    }
                    const QPointF nearestEnd = along < 0.0 ? screenA : screenB;
                    if (QLineF(footScreen, nearestEnd).length() > kExtensionReachPixels) {
                        continue;
                    }
                    const double deltaU = secondCoordinates.u - firstCoordinates.u;
                    const double deltaV = secondCoordinates.v - firstCoordinates.v;
                    const double planeLengthSquared = deltaU * deltaU + deltaV * deltaV;
                    if (planeLengthSquared <= 1.0e-18) {
                        continue;
                    }
                    const auto cursorCoordinates = activePlane_->Project(point);
                    const double planeAlong
                        = ((cursorCoordinates.u - firstCoordinates.u) * deltaU
                              + (cursorCoordinates.v - firstCoordinates.v) * deltaV)
                        / planeLengthSquared;
                    consider(DrawingSnapKind::Extension,
                        activePlane_->ToWorld(
                            firstCoordinates.u + deltaU * planeAlong,
                            firstCoordinates.v + deltaV * planeAlong),
                        kExtensionPerpendicularPixels + 2.0,
                        along < 0.0 ? points[index - 1] : points[index]);
                }
            }
        }
    }

    if (gridPointsVisible_ && !nearbyStructuralOnly) {
        const double gridStep = snapStep_ / static_cast<double>(gridSubdivision_);
        const auto coordinates = activePlane_->Project(point);
        const long long gridUIndex = std::llround((coordinates.u - gridOriginU_) / gridStep);
        const long long gridVIndex = std::llround((coordinates.v - gridOriginV_) / gridStep);
        const double snappedU = gridOriginU_ + static_cast<double>(gridUIndex) * gridStep;
        const double snappedV = gridOriginV_ + static_cast<double>(gridVIndex) * gridStep;
        const bool mainPoint = gridUIndex % gridSubdivision_ == 0
            && gridVIndex % gridSubdivision_ == 0;
        const double minorRadius = std::clamp(
            gridStep * pixelsPerMillimeter_ * 0.3,
            0.75,
            2.25);
        consider(
            DrawingSnapKind::Grid,
            activePlane_->ToWorld(snappedU, snappedV),
            mainPoint ? 5.5 : std::max(1.5, minorRadius));
    }
    return best;
}

Vector3 CadViewport::SnapPoint(
    Vector3 point,
    QPointF screenPosition,
    Qt::KeyboardModifiers modifiers)
{
    if (modifiers.testFlag(Qt::ControlModifier)) {
        drawingSnapHover_.reset();
        return point;
    }
    drawingSnapHover_ = FindDrawingSnap(point, screenPosition);
    return drawingSnapHover_.has_value() ? drawingSnapHover_->point : point;
}

Vector3 CadViewport::SnapGridAlignmentTarget(Vector3 point, QPointF screenPosition) const
{
    if (!activePlane_.has_value() || project_ == nullptr) {
        return point;
    }

    double bestDistance = 12.0;
    std::optional<Vector3> bestPoint;
    for (int wireIndex = 0; wireIndex < static_cast<int>(project_->Wires().size()); ++wireIndex) {
        const NamedWire& namedWire = project_->Wires()[wireIndex];
        if (!ShouldDisplay(CadSelectionKind::Wire, wireIndex, namedWire.visible)) {
            continue;
        }
        for (const Vector3& candidate : namedWire.wire.ControlPoints()) {
            if (std::abs(activePlane_->Project(candidate).w) > 1.0e-6) {
                continue;
            }
            const double distance = QLineF(screenPosition, ProjectPoint(candidate)).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = candidate;
            }
        }
    }
    return bestPoint.value_or(point);
}

Vector3 CadViewport::SnapDraggedControlPoint(Vector3 point, QPointF screenPosition) const
{
    if (!snapEnabled_ || !controlPointSnapPlane_.has_value()) {
        return point;
    }

    if (project_ != nullptr && controlPointDragPlane_.has_value()) {
        double bestDistance = 10.0;
        std::optional<Vector3> bestPoint;
        for (int wireIndex = 0; wireIndex < static_cast<int>(project_->Wires().size()); ++wireIndex) {
            const NamedWire& namedWire = project_->Wires()[wireIndex];
            if (!ShouldDisplay(CadSelectionKind::Wire, wireIndex, namedWire.visible)) {
                continue;
            }
            const auto& controls = namedWire.wire.ControlPoints();
            for (std::size_t pointIndex = 0; pointIndex < controls.size(); ++pointIndex) {
                if (draggedControlPoint_.has_value()
                    && draggedControlPoint_->wireIndex == wireIndex
                    && draggedControlPoint_->controlPointIndex == pointIndex) {
                    continue;
                }
                const Vector3 candidate = controls[pointIndex];
                if (draggedControlPoint_.has_value()
                    && draggedControlPoint_->wireIndex == wireIndex
                    && AlmostEqual(
                        candidate,
                        controls[draggedControlPoint_->controlPointIndex],
                        1.0e-9)) {
                    continue;
                }
                if (std::abs(controlPointDragPlane_->Project(candidate).w) > 1.0e-6) {
                    continue;
                }
                const double distance = QLineF(screenPosition, ProjectPoint(candidate)).length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestPoint = candidate;
                }
            }
        }
        if (bestPoint.has_value()) {
            return *bestPoint;
        }
    }

    const auto coordinates = controlPointSnapPlane_->Project(point);
    const double snappedU = gridOriginU_
        + std::round((coordinates.u - gridOriginU_) / snapStep_) * snapStep_;
    const double snappedV = gridOriginV_
        + std::round((coordinates.v - gridOriginV_) / snapStep_) * snapStep_;
    return controlPointSnapPlane_->ToWorld(snappedU, snappedV, coordinates.w);
}

Vector3 CadViewport::ApplyDrawingConstraint(Vector3 point, Qt::KeyboardModifiers modifiers) const
{
    if (!activePlane_.has_value() || drawingPoints_.empty()
        || !modifiers.testFlag(Qt::ShiftModifier)) {
        return point;
    }

    const Vector3 anchor = tool_ == ViewportTool::DrawPolyline || tool_ == ViewportTool::DrawSpline
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    auto target = activePlane_->Project(point);
    const double deltaU = target.u - start.u;
    const double deltaV = target.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline) {
        if (std::abs(deltaU) >= std::abs(deltaV)) {
            target.v = start.v;
        } else {
            target.u = start.u;
        }
        return activePlane_->ToWorld(target.u, target.v);
    }

    if (tool_ == ViewportTool::DrawRectangle) {
        const double side = std::max(std::abs(deltaU), std::abs(deltaV));
        target.u = start.u + (deltaU < 0.0 ? -side : side);
        target.v = start.v + (deltaV < 0.0 ? -side : side);
        return activePlane_->ToWorld(target.u, target.v);
    }
    return point;
}

void CadViewport::CommitDrawingPoint(Vector3 point)
{
    if (!activePlane_.has_value() || tool_ == ViewportTool::Select) {
        return;
    }
    if (!drawingPoints_.empty() && (point - drawingPoints_.back()).LengthSquared() <= 1.0e-18) {
        return;
    }

    if (tool_ == ViewportTool::DrawPoint) {
        if (pointCreated_) {
            pointCreated_(point);
        }
        hoverDrawingPoint_ = point;
        drawingSnapHover_.reset();
        NotifyDrawingState();
        update();
        return;
    }

    if (tool_ == ViewportTool::DrawRectangle && drawingPoints_.size() == 1) {
        const auto firstCoordinates = activePlane_->Project(drawingPoints_.front());
        const auto secondCoordinates = activePlane_->Project(point);
        if (std::abs(secondCoordinates.u - firstCoordinates.u) <= 1.0e-9
            || std::abs(secondCoordinates.v - firstCoordinates.v) <= 1.0e-9) {
            return;
        }
    }
    if (tool_ == ViewportTool::DrawArc
        && ((arcDrawingMode_ == ArcDrawingMode::EndpointsRadius && drawingPoints_.size() >= 2)
            || (arcDrawingMode_ == ArcDrawingMode::StartTangent && !drawingPoints_.empty()))) {
        return;
    }

    drawingPoints_.push_back(point);
    if ((tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection)
        && drawingPoints_.size() == 2) {
        const Vector3 delta = drawingPoints_[1] - drawingPoints_[0];
        const bool copy = tool_ == ViewportTool::CopySelection;
        drawingPoints_.clear();
        if (translationRequested_) {
            translationRequested_(delta, copy);
        }
    } else if (tool_ == ViewportTool::MirrorSelection && drawingPoints_.size() == 2) {
        const Vector3 linePoint = drawingPoints_[0];
        const Vector3 lineDirection = drawingPoints_[1] - drawingPoints_[0];
        const Vector3 planeNormal = activePlane_->Normal();
        drawingPoints_.clear();
        if (mirrorRequested_) {
            mirrorRequested_(linePoint, lineDirection, planeNormal);
        }
    } else if (tool_ == ViewportTool::RotateSelection && drawingPoints_.size() == 3) {
        const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
        const Vector3 to = drawingPoints_[2] - drawingPoints_[0];
        if (from.LengthSquared() <= 1.0e-18 || to.LengthSquared() <= 1.0e-18) {
            drawingPoints_.pop_back();
            return;
        }
        const Vector3 normal = activePlane_->Normal();
        const double angle = std::atan2(Dot(Cross(from, to), normal), Dot(from, to));
        const Vector3 axisPoint = drawingPoints_[0];
        drawingPoints_.clear();
        if (rotationRequested_) {
            rotationRequested_(axisPoint, normal, angle);
        }
    } else if (tool_ == ViewportTool::DrawLine && drawingPoints_.size() == 2) {
        const Vector3 start = drawingPoints_[0];
        const Vector3 end = drawingPoints_[1];
        drawingPoints_.clear();
        if (lineCreated_) {
            lineCreated_(start, end);
        }
    } else if (tool_ == ViewportTool::DrawRectangle && drawingPoints_.size() == 2) {
        const auto firstCoordinates = activePlane_->Project(drawingPoints_[0]);
        const auto secondCoordinates = activePlane_->Project(drawingPoints_[1]);
        const std::array<Vector3, 4> corners = {
            activePlane_->ToWorld(firstCoordinates.u, firstCoordinates.v),
            activePlane_->ToWorld(secondCoordinates.u, firstCoordinates.v),
            activePlane_->ToWorld(secondCoordinates.u, secondCoordinates.v),
            activePlane_->ToWorld(firstCoordinates.u, secondCoordinates.v),
        };
        drawingPoints_.clear();
        if (rectangleCreated_) {
            rectangleCreated_(corners);
        }
    } else if (tool_ == ViewportTool::DrawCircle && drawingPoints_.size() == 2) {
        const Vector3 center = drawingPoints_[0];
        const double radius = (drawingPoints_[1] - drawingPoints_[0]).Length();
        drawingPoints_.clear();
        if (circleCreated_) {
            circleCreated_(center, radius);
        }
    } else if (tool_ == ViewportTool::DrawArc
        && arcDrawingMode_ == ArcDrawingMode::ThreePoints
        && drawingPoints_.size() == 3) {
        const std::array<Vector3, 3> points = {drawingPoints_[0], drawingPoints_[1], drawingPoints_[2]};
        drawingPoints_.clear();
        if (arcCreated_) {
            arcCreated_(points[0], points[1], points[2]);
        }
    } else if (tool_ == ViewportTool::DrawBezier && drawingPoints_.size() == 4) {
        const std::array<Vector3, 4> points = {drawingPoints_[0], drawingPoints_[1], drawingPoints_[2], drawingPoints_[3]};
        drawingPoints_.clear();
        if (bezierCreated_) {
            bezierCreated_(points);
        }
    }
    hoverDrawingPoint_ = point;
    UpdateDynamicDimensionEditor();
    NotifyDrawingState();
    update();
}

void CadViewport::UpdateHover(QPointF position)
{
    hoverScreenPosition_ = position.toPoint();
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();
    hoveredControlPoint_.reset();

    CadSelection nearbyPoint;
    if (project_ != nullptr && tool_ == ViewportTool::Select) {
        double bestDistance = 7.0;
        for (int index = 0; index < static_cast<int>(project_->Points().size()); ++index) {
            const auto& point = project_->Points()[index];
            if (!ShouldDisplay(CadSelectionKind::Point, index, point.visible)) {
                continue;
            }
            const double distance = QLineF(position, ProjectPoint(point.point)).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                nearbyPoint = {CadSelectionKind::Point, index};
            }
        }
    }

    if (nearbyPoint.kind == CadSelectionKind::Point) {
        hoveredSelection_ = nearbyPoint;
    } else if (const auto controlPoint = NearestEditableControlPoint(position, 9.0);
        tool_ == ViewportTool::Select && controlPoint.has_value()) {
        hoveredControlPoint_ = controlPoint;
        hoveredSelection_ = {CadSelectionKind::Wire, controlPoint->wireIndex};
        hoveredWirePoint_ = project_->Wires()[controlPoint->wireIndex]
                                .wire.ControlPoints()[controlPoint->controlPointIndex];
    } else if (project_ != nullptr
        && (tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
            || tool_ == ViewportTool::Curvature)) {
        const std::optional<WireEndpointPick> endpoint = NearestWireEndpoint(position, 8.0);
        if (endpoint.has_value()) {
            hoveredSelection_ = {CadSelectionKind::Wire, endpoint->wireIndex};
            hoveredWirePoint_ = endpoint->point;
        }
    } else if (project_ != nullptr
        && (tool_ == ViewportTool::Select || tool_ == ViewportTool::Measure
            || tool_ == ViewportTool::SplitWire || tool_ == ViewportTool::TrimWire
            || tool_ == ViewportTool::ExtendWire)) {
        if (tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire) {
            const CadSelection hit = HitTestWire(position, 9.0);
            if (hit.kind == CadSelectionKind::Wire) {
                hoveredSelection_ = hit;
                hoveredWireParameter_ = NearestWireParameter(hit.index, position, 12.0, true);
            }
        } else {
            double bestPointDistance = 8.0;
            int bestPointWire = -1;
            Vector3 bestPoint;
            const auto considerPoint = [&](int wireIndex, Vector3 point) {
                const double distance = QLineF(position, ProjectPoint(point)).length();
                if (distance < bestPointDistance) {
                    bestPointDistance = distance;
                    bestPointWire = wireIndex;
                    bestPoint = point;
                }
            };

            for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
                const NamedWire& namedWire = project_->Wires()[index];
                if (!ShouldDisplay(CadSelectionKind::Wire, index, namedWire.visible)) {
                    continue;
                }
                considerPoint(index, namedWire.wire.Start());
                if (!namedWire.wire.IsClosed()) {
                    considerPoint(index, namedWire.wire.End());
                }
                if (IsSelected(CadSelectionKind::Wire, index)) {
                    for (const Vector3& point : namedWire.wire.ControlPoints()) {
                        considerPoint(index, point);
                    }
                }
            }

            if (bestPointWire >= 0) {
                hoveredSelection_ = {CadSelectionKind::Wire, bestPointWire};
                hoveredWirePoint_ = bestPoint;
            } else {
                const CadSelection hit = HitTestWire(position, 9.0);
                if (hit.kind == CadSelectionKind::Wire) {
                    hoveredSelection_ = hit;
                    hoveredWireParameter_ = NearestWireParameter(hit.index, position, 12.0, true);
                } else if (tool_ == ViewportTool::Select) {
                    hoveredSelection_ = HitTest(position);
                }
            }
        }
    }

    setCursor(hoveredControlPoint_.has_value() && tool_ == ViewportTool::Select
            ? Qt::SizeAllCursor
            : hoveredSelection_.kind == CadSelectionKind::Wire
            && (tool_ == ViewportTool::Select || tool_ == ViewportTool::Measure
                || tool_ == ViewportTool::SplitWire || tool_ == ViewportTool::TrimWire
                || tool_ == ViewportTool::ExtendWire || tool_ == ViewportTool::Coincident
                || tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature)
        ? Qt::PointingHandCursor
        : tool_ == ViewportTool::Select && hoveredSelection_.kind != CadSelectionKind::None
        ? Qt::PointingHandCursor
        : tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void CadViewport::UpdateDirectLineEditPreview()
{
    directLineEditPreviewWire_.reset();
    directLineEditPreviewIntersection_.reset();
    if (project_ == nullptr
        || (tool_ != ViewportTool::TrimWire && tool_ != ViewportTool::ExtendWire)
        || hoveredSelection_.kind != CadSelectionKind::Wire
        || !hoveredWireParameter_.has_value()
        || hoveredSelection_.index < 0
        || hoveredSelection_.index >= static_cast<int>(project_->Wires().size())) {
        update();
        return;
    }

    const int targetIndex = hoveredSelection_.index;
    const Wire& target = project_->Wires()[targetIndex].wire;
    std::vector<Wire> boundaries;
    boundaries.reserve(project_->Wires().size());
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const NamedWire& candidate = project_->Wires()[index];
        if (index == targetIndex
            || !ShouldDisplay(CadSelectionKind::Wire, index, candidate.visible)) {
            continue;
        }
        boundaries.push_back(candidate.wire);
    }

    try {
        if (tool_ == ViewportTool::TrimWire) {
            directLineEditPreviewWire_ = TrimWireAtBoundaries(
                target, *hoveredWireParameter_, boundaries).removed;
        } else {
            const auto result = ExtendWireToBoundary(
                target, *hoveredWireParameter_, boundaries);
            directLineEditPreviewWire_ = result.added;
            directLineEditPreviewIntersection_ = result.intersection;
        }
    } catch (const std::invalid_argument&) {
        // The line remains hoverable even when no visible boundary can edit it.
    }
    update();
}

void CadViewport::ClearHover()
{
    if (hoveredSelection_.kind == CadSelectionKind::None
        && !hoveredWirePoint_.has_value() && !hoveredWireParameter_.has_value()
        && !hoveredControlPoint_.has_value() && !drawingSnapHover_.has_value()
        && !directLineEditPreviewWire_.has_value()) {
        return;
    }
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();
    hoveredControlPoint_.reset();
    drawingSnapHover_.reset();
    directLineEditPreviewWire_.reset();
    directLineEditPreviewIntersection_.reset();
    update();
}

void CadViewport::CancelControlPointDrag()
{
    draggedControlPoint_.reset();
    draggedWirePreview_.reset();
    controlPointDragPlane_.reset();
    controlPointSnapPlane_.reset();
}

void CadViewport::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), backgroundColor_);

    if (displayMode_ == ViewportDisplayMode::Design
        && activePlane_.has_value() && gridPointsVisible_) {
        double displayOriginU = gridOriginU_;
        double displayOriginV = gridOriginV_;
        if (gridOriginDragSource_.has_value() && gridOriginDragTarget_.has_value()) {
            const auto source = activePlane_->Project(*gridOriginDragSource_);
            const auto target = activePlane_->Project(*gridOriginDragTarget_);
            displayOriginU = gridOriginDragBaseU_ + target.u - source.u;
            displayOriginV = gridOriginDragBaseV_ + target.v - source.v;
        }

        const double majorSpacing = snapStep_;
        const double minorSpacing = majorSpacing / static_cast<double>(gridSubdivision_);
        int majorStride = 1;
        while (majorSpacing * pixelsPerMillimeter_ * majorStride < 12.0) {
            majorStride *= 2;
        }

        const double extent = std::max(width(), height()) / std::max(pixelsPerMillimeter_, 0.1);
        double minimumU = displayOriginU - extent;
        double maximumU = displayOriginU + extent;
        double minimumV = displayOriginV - extent;
        double maximumV = displayOriginV + extent;
        bool foundBounds = false;
        for (const QPointF corner : {QPointF(0.0, 0.0), QPointF(width(), 0.0),
                 QPointF(width(), height()), QPointF(0.0, height())}) {
            const auto point = PointOnActivePlane(corner);
            if (!point.has_value()) {
                continue;
            }
            const auto coordinates = activePlane_->Project(*point);
            if (!foundBounds) {
                minimumU = maximumU = coordinates.u;
                minimumV = maximumV = coordinates.v;
                foundBounds = true;
            } else {
                minimumU = std::min(minimumU, coordinates.u);
                maximumU = std::max(maximumU, coordinates.u);
                minimumV = std::min(minimumV, coordinates.v);
                maximumV = std::max(maximumV, coordinates.v);
            }
        }

        const bool showMinorPoints = gridSubdivision_ > 1
            && minorSpacing * pixelsPerMillimeter_ >= 2.5 && majorStride == 1;
        const double drawSpacing = showMinorPoints ? minorSpacing : majorSpacing * majorStride;
        const int firstU = static_cast<int>(std::floor((minimumU - displayOriginU) / drawSpacing)) - 1;
        const int lastU = static_cast<int>(std::ceil((maximumU - displayOriginU) / drawSpacing)) + 1;
        const int firstV = static_cast<int>(std::floor((minimumV - displayOriginV) / drawSpacing)) - 1;
        const int lastV = static_cast<int>(std::ceil((maximumV - displayOriginV) / drawSpacing)) + 1;
        for (int uIndex = firstU; uIndex <= lastU; ++uIndex) {
            const double u = displayOriginU + uIndex * drawSpacing;
            for (int vIndex = firstV; vIndex <= lastV; ++vIndex) {
                const double v = displayOriginV + vIndex * drawSpacing;
                const QPointF point = ProjectPoint(activePlane_->ToWorld(u, v));
                if (rect().adjusted(-2, -2, 2, 2).contains(point.toPoint())) {
                    const bool majorPoint = !showMinorPoints
                        || (uIndex % gridSubdivision_ == 0 && vIndex % gridSubdivision_ == 0);
                    painter.setPen(QPen(
                        majorPoint ? majorGridColor_ : minorGridColor_,
                        majorPoint ? 3.2 : 1.25,
                        Qt::SolidLine,
                        Qt::RoundCap));
                    painter.drawPoint(point);
                }
            }
        }
    } else if (project_ == nullptr || project_->WorkPlanes().empty()) {
        painter.setPen(QPen(majorGridColor_, 1.0));
        for (int coordinate = -50; coordinate <= 50; coordinate += 5) {
            painter.drawLine(ProjectPoint({static_cast<double>(coordinate), -50.0, 0.0}), ProjectPoint({static_cast<double>(coordinate), 50.0, 0.0}));
            painter.drawLine(ProjectPoint({-50.0, static_cast<double>(coordinate), 0.0}), ProjectPoint({50.0, static_cast<double>(coordinate), 0.0}));
        }
    }

    if (project_ != nullptr) {
        for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
            const auto& namedPlane = project_->WorkPlanes()[index];
            if (!ShouldDisplay(CadSelectionKind::WorkPlane, index, namedPlane.visible)) {
                continue;
            }
            const auto& plane = namedPlane.plane;
            QPolygonF polygon;
            polygon << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, kPlaneHalfSize));
            const bool selected = IsSelected(CadSelectionKind::WorkPlane, index);
            const bool active = activePlane_.has_value()
                && AlmostEqual(activePlane_->Origin(), plane.Origin(), 1.0e-8)
                && AlmostEqual(activePlane_->Normal(), plane.Normal(), 1.0e-8)
                && AlmostEqual(activePlane_->UAxis(), plane.UAxis(), 1.0e-8);
            painter.setBrush(selected ? QColor(241, 178, 54, 52) : active ? QColor(0, 127, 120, 36) : QColor(69, 132, 142, 18));
            painter.setPen(QPen(selected ? QColor("#c47a13") : active ? QColor("#007f78") : QColor("#7d9aa0"), selected || active ? 2.2 : 1.0, Qt::DashLine));
            painter.drawPolygon(polygon);

            painter.setPen(QPen(QColor("#25747d"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(4.0, 0.0)));
            painter.setPen(QPen(QColor("#8b5a2b"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(0.0, 4.0)));
        }

        for (int index = 0; index < static_cast<int>(project_->Surfaces().size()); ++index) {
            const auto& namedSurface = project_->Surfaces()[index];
            if (!ShouldDisplay(CadSelectionKind::Surface, index, namedSurface.visible)) {
                continue;
            }
            const auto& surface = namedSurface.surface;
            const bool selected = IsSelected(CadSelectionKind::Surface, index);
            QColor fill = selected ? QColor(230, 159, 0, 90) : surfaceFillColor_;
            if (!selected) {
                fill.setAlphaF(static_cast<double>(surfaceOpacityPercent_) / 100.0);
            }
            const QColor edge = selected ? QColor("#c47a13") : surfaceEdgeColor_;
            const double edgeWidth = selected ? 2.5 : surfaceEdgeWidth_;
            const Qt::PenStyle edgeStyle = selected ? Qt::SolidLine : surfaceEdgeStyle_;
            if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
                QPainterPath boundary(ProjectPoint(surface.FirstBoundary().Evaluate(0.0)));
                for (int sample = 1; sample <= 128; ++sample) {
                    boundary.lineTo(ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0)));
                }
                boundary.closeSubpath();
                painter.setBrush(fill);
                painter.setPen(QPen(edge, edgeWidth, edgeStyle));
                painter.drawPath(boundary);
            } else {
                painter.setPen(QPen(edge, selected ? 1.8 : std::max(0.25, edgeWidth * 0.55), edgeStyle));
                double curvatureScale = 0.0;
                if (surfaceDiagnosticMode_ == SurfaceDiagnosticMode::GaussianCurvature) {
                    std::vector<double> sampledCurvatures;
                    sampledCurvatures.reserve(32 * 10);
                    for (int uIndex = 0; uIndex < 32; ++uIndex) {
                        for (int vIndex = 0; vIndex < 10; ++vIndex) {
                            try {
                                sampledCurvatures.push_back(std::abs(surface.Curvature(
                                    (static_cast<double>(uIndex) + 0.5) / 32.0,
                                    (static_cast<double>(vIndex) + 0.5) / 10.0).gaussian));
                            } catch (const std::exception&) {
                            }
                        }
                    }
                    if (!sampledCurvatures.empty()) {
                        std::sort(sampledCurvatures.begin(), sampledCurvatures.end());
                        curvatureScale = sampledCurvatures[
                            sampledCurvatures.size() * 9 / 10];
                    }
                    curvatureScale = std::max(curvatureScale, 1.0e-12);
                }
                for (int uIndex = 0; uIndex < 32; ++uIndex) {
                    for (int vIndex = 0; vIndex < 10; ++vIndex) {
                        const double u0 = static_cast<double>(uIndex) / 32.0;
                        const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                        const double v0 = static_cast<double>(vIndex) / 10.0;
                        const double v1 = static_cast<double>(vIndex + 1) / 10.0;
                        QPolygonF patch;
                        patch << ProjectPoint(surface.Evaluate(u0, v0))
                              << ProjectPoint(surface.Evaluate(u1, v0))
                              << ProjectPoint(surface.Evaluate(u1, v1))
                              << ProjectPoint(surface.Evaluate(u0, v1));
                        QColor patchFill = fill;
                        if (surfaceDiagnosticMode_ == SurfaceDiagnosticMode::Wireframe) {
                            patchFill = Qt::transparent;
                        } else if (surfaceDiagnosticMode_
                            == SurfaceDiagnosticMode::GaussianCurvature) {
                            double gaussian = 0.0;
                            try {
                                gaussian = surface.Curvature(
                                    (u0 + u1) * 0.5, (v0 + v1) * 0.5).gaussian;
                            } catch (const std::exception&) {
                            }
                            const double strength = std::sqrt(std::clamp(
                                std::abs(gaussian) / curvatureScale, 0.0, 1.0));
                            const QColor neutral("#2f8f78");
                            const QColor target = gaussian >= 0.0
                                ? QColor("#d94b37")
                                : QColor("#3568c0");
                            patchFill = QColor::fromRgbF(
                                neutral.redF() * (1.0 - strength) + target.redF() * strength,
                                neutral.greenF() * (1.0 - strength) + target.greenF() * strength,
                                neutral.blueF() * (1.0 - strength) + target.blueF() * strength,
                                0.88F);
                        } else if ((uIndex + vIndex) % 2 != 0) {
                            patchFill.setAlpha(std::max(16, patchFill.alpha() - 14));
                        }
                        painter.setBrush(patchFill);
                        painter.drawPolygon(patch);
                    }
                }
            }
        }

        for (int index = 0; index < static_cast<int>(project_->Plates().size()); ++index) {
            const auto& namedPlate = project_->Plates()[index];
            if (!ShouldDisplay(CadSelectionKind::Plate, index, namedPlate.visible)) {
                continue;
            }
            const auto& plate = namedPlate.plate;
            const auto& source = plate.SourceSurface();
            const bool selected = IsSelected(CadSelectionKind::Plate, index);
            QColor fill = plateFillColor_;
            fill.setAlphaF(static_cast<double>(plateOpacityPercent_) / 100.0);
            if (selected) {
                fill = QColor(230, 159, 0, 168);
            }
            const QColor edge = selected ? QColor("#b66700") : plateEdgeColor_;
            std::vector<QPainterPath> openingPaths;
            for (const std::string& openingName : namedPlate.openingWireNames) {
                const auto opening = std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const auto& wire) {
                    return wire.name == openingName;
                });
                if (opening == project_->Wires().end()) {
                    continue;
                }
                QPainterPath path(ProjectPoint(opening->wire.Evaluate(0.0)));
                for (int sample = 1; sample <= 128; ++sample) {
                    path.lineTo(ProjectPoint(opening->wire.Evaluate(static_cast<double>(sample) / 128.0)));
                }
                path.closeSubpath();
                openingPaths.push_back(std::move(path));
            }

            painter.save();
            if (!openingPaths.empty()) {
                QPainterPath clipPath;
                clipPath.addRect(QRectF(rect()));
                for (const QPainterPath& openingPath : openingPaths) {
                    clipPath.addPath(openingPath);
                }
                clipPath.setFillRule(Qt::OddEvenFill);
                painter.setClipPath(clipPath);
            }
            painter.setPen(QPen(
                edge,
                selected ? 2.2 : plateEdgeWidth_,
                selected ? Qt::SolidLine : plateEdgeStyle_));

            if (source.Kind() == kachakacha::model::SurfaceKind::Planar) {
                const Vector3 normal = source.Normal(0.5, 0.5);
                const auto drawLayer = [&](double offset, int alpha) {
                    QPainterPath path(ProjectPoint(source.FirstBoundary().Evaluate(0.0) + normal * offset));
                    for (int sample = 1; sample <= 128; ++sample) {
                        path.lineTo(ProjectPoint(source.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0) + normal * offset));
                    }
                    path.closeSubpath();
                    QColor layerFill = fill;
                    layerFill.setAlpha(alpha);
                    painter.setBrush(layerFill);
                    painter.drawPath(path);
                };
                drawLayer(plate.MinimumOffset(), std::max(40, fill.alpha() - 42));
                drawLayer(plate.MaximumOffset(), fill.alpha());
                painter.setBrush(fill.darker(108));
                for (int sample = 0; sample < 64; ++sample) {
                    const double t0 = static_cast<double>(sample) / 64.0;
                    const double t1 = static_cast<double>(sample + 1) / 64.0;
                    QPolygonF side;
                    side << ProjectPoint(source.FirstBoundary().Evaluate(t0) + normal * plate.MinimumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t1) + normal * plate.MinimumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t1) + normal * plate.MaximumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t0) + normal * plate.MaximumOffset());
                    painter.drawPolygon(side);
                }
            } else {
                for (int layer = 0; layer < 2; ++layer) {
                    QColor layerFill = fill;
                    if (layer == 0) {
                        layerFill.setAlpha(std::max(36, fill.alpha() - 50));
                    }
                    painter.setBrush(layerFill);
                    for (int uIndex = 0; uIndex < 32; ++uIndex) {
                        for (int vIndex = 0; vIndex < 10; ++vIndex) {
                            const double u0 = static_cast<double>(uIndex) / 32.0;
                            const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                            const double v0 = static_cast<double>(vIndex) / 10.0;
                            const double v1 = static_cast<double>(vIndex + 1) / 10.0;
                            QPolygonF patch;
                            patch << ProjectPoint(plate.Evaluate(u0, v0, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u1, v0, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u1, v1, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u0, v1, static_cast<double>(layer)));
                            painter.drawPolygon(patch);
                        }
                    }
                }
                painter.setBrush(fill.darker(108));
                for (int uIndex = 0; uIndex < 32; ++uIndex) {
                    const double u0 = static_cast<double>(uIndex) / 32.0;
                    const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                    for (double v : {0.0, 1.0}) {
                        QPolygonF side;
                        side << ProjectPoint(plate.Evaluate(u0, v, 0.0))
                             << ProjectPoint(plate.Evaluate(u1, v, 0.0))
                             << ProjectPoint(plate.Evaluate(u1, v, 1.0))
                             << ProjectPoint(plate.Evaluate(u0, v, 1.0));
                        painter.drawPolygon(side);
                    }
                }
                const auto& range = plate.Range();
                if (!source.FirstBoundary().IsClosed()
                    || range.minimumU > 1.0e-12 || range.maximumU < 1.0 - 1.0e-12) {
                    for (int vIndex = 0; vIndex < 16; ++vIndex) {
                        const double v0 = static_cast<double>(vIndex) / 16.0;
                        const double v1 = static_cast<double>(vIndex + 1) / 16.0;
                        for (double u : {0.0, 1.0}) {
                            QPolygonF side;
                            side << ProjectPoint(plate.Evaluate(u, v0, 0.0))
                                 << ProjectPoint(plate.Evaluate(u, v1, 0.0))
                                 << ProjectPoint(plate.Evaluate(u, v1, 1.0))
                                 << ProjectPoint(plate.Evaluate(u, v0, 1.0));
                            painter.drawPolygon(side);
                        }
                    }
                }
            }
            if (plateSplitPreviewAxis_.has_value()
                && selection_.kind == CadSelectionKind::Plate && selection_.index == index
                && source.Kind() != kachakacha::model::SurfaceKind::Planar) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor("#b23a48"), 3.0, Qt::DashLine));
                const bool splitU = *plateSplitPreviewAxis_ == kachakacha::model::PlateSplitAxis::U;
                const double firstU = splitU ? plateSplitPreviewParameter_ : 0.0;
                const double firstV = splitU ? 0.0 : plateSplitPreviewParameter_;
                QPainterPath splitPath(ProjectPoint(plate.Evaluate(firstU, firstV, 1.0)));
                for (int sample = 1; sample <= 96; ++sample) {
                    const double position = static_cast<double>(sample) / 96.0;
                    splitPath.lineTo(ProjectPoint(plate.Evaluate(
                        splitU ? plateSplitPreviewParameter_ : position,
                        splitU ? position : plateSplitPreviewParameter_,
                        1.0)));
                }
                painter.drawPath(splitPath);
            }
            painter.restore();
            if (plateAssemblyApproximationIndex_.has_value()
                && *plateAssemblyApproximationIndex_ == index) {
                painter.save();
                const double errorScale = std::max(
                    plateAssemblyApproximationMaximumDeviationMillimeters_, 1.0e-9);
                for (std::size_t panelIndex = 0;
                     panelIndex < plateAssemblyApproximationPanels_.size(); ++panelIndex) {
                    const auto& panel = plateAssemblyApproximationPanels_[panelIndex];
                    const int pieceIndex = panelIndex < plateAssemblyApproximationPieceIndices_.size()
                        ? plateAssemblyApproximationPieceIndices_[panelIndex]
                        : 0;
                    const double deviation = panelIndex < plateAssemblyApproximationDeviations_.size()
                        ? plateAssemblyApproximationDeviations_[panelIndex]
                        : 0.0;
                    const int warm = static_cast<int>(std::clamp(
                        deviation / errorScale, 0.0, 1.0) * 120.0);
                    QColor approximationFill = pieceIndex % 2 == 0
                        ? QColor(31 + warm, 139, 142 - warm / 3, 88)
                        : QColor(201, 161 - warm / 4, 67, 82);
                    const QColor approximationEdge = deviation > 0.1
                        ? QColor(173, 39, 52, 205)
                        : QColor(37, 76, 84, 175);
                    QPolygonF polygon;
                    polygon << ProjectPoint(panel[0])
                            << ProjectPoint(panel[1])
                            << ProjectPoint(panel[2]);
                    if (plateAssemblyApproximationSmoothPaper_) {
                        approximationFill = QColor(36, 143, 147, 105);
                    }
                    painter.setBrush(approximationFill);
                    painter.setPen(plateAssemblyApproximationSmoothPaper_
                        ? QPen(Qt::NoPen)
                        : QPen(approximationEdge, deviation > 0.1 ? 1.35 : 0.75));
                    painter.drawPolygon(polygon);
                }
                painter.restore();
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(edge, selected ? 2.8 : 1.6));
            for (const QPainterPath& openingPath : openingPaths) {
                painter.drawPath(openingPath);
            }
            if (plateAssemblyGuideIndex_.has_value()
                && *plateAssemblyGuideIndex_ == index) {
                const auto drawGuidePaths = [&](const auto& paths, const QPen& halo, const QPen& line) {
                    for (const auto& points : paths) {
                        if (points.size() < 2) {
                            continue;
                        }
                        QPainterPath path(ProjectPoint(points.front()));
                        for (std::size_t pointIndex = 1; pointIndex < points.size(); ++pointIndex) {
                            path.lineTo(ProjectPoint(points[pointIndex]));
                        }
                        painter.setPen(halo);
                        painter.drawPath(path);
                        painter.setPen(line);
                        painter.drawPath(path);
                    }
                };
                painter.save();
                painter.setBrush(Qt::NoBrush);
                drawGuidePaths(
                    plateAssemblyFoldLines_,
                    QPen(QColor(255, 255, 255, 190), 4.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin),
                    QPen(QColor("#1769aa"), 2.2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
                drawGuidePaths(
                    plateAssemblyReliefCuts_,
                    QPen(QColor(255, 255, 255, 205), 5.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
                    QPen(QColor("#c62838"), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.restore();
            }
        }

        // 部材近似モデルの曲げ状態プレビュー(部材タブのスライダー連動)。
        if (partFoldPreviewRails_.size() >= 2) {
            painter.save();
            painter.setBrush(Qt::NoBrush);
            // 素線(レール間のルールドのあたり)を薄く描く。
            painter.setPen(QPen(QColor(90, 140, 160, 110), 1.1,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (std::size_t row = 0; row + 1 < partFoldPreviewRails_.size(); ++row) {
                const auto& bottom = partFoldPreviewRails_[row];
                const auto& top = partFoldPreviewRails_[row + 1];
                const std::size_t count = std::min(bottom.size(), top.size());
                for (std::size_t column = 0; column < count; column += 8) {
                    painter.drawLine(
                        ProjectPoint(bottom[column]), ProjectPoint(top[column]));
                }
            }
            // レール。外縁は実線、内部の折り線は山(赤破線)/谷(青一点鎖線)。
            for (std::size_t row = 0; row < partFoldPreviewRails_.size(); ++row) {
                const auto& points = partFoldPreviewRails_[row];
                if (points.size() < 2) {
                    continue;
                }
                QPen pen(QColor("#1f5f4a"), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                if (row > 0 && row + 1 < partFoldPreviewRails_.size()) {
                    const std::size_t creaseIndex = row - 1;
                    const int crease = creaseIndex < partFoldPreviewCreases_.size()
                        ? partFoldPreviewCreases_[creaseIndex]
                        : 0;
                    if (crease > 0) {
                        pen = QPen(QColor("#c62838"), 2.2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
                    } else if (crease < 0) {
                        pen = QPen(QColor("#1769aa"), 2.2, Qt::DashDotLine, Qt::RoundCap, Qt::RoundJoin);
                    } else {
                        pen = QPen(QColor("#6a7680"), 1.8, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
                    }
                }
                QPainterPath path(ProjectPoint(points.front()));
                for (std::size_t index = 1; index < points.size(); ++index) {
                    path.lineTo(ProjectPoint(points[index]));
                }
                painter.setPen(QPen(QColor(255, 255, 255, 190), pen.widthF() + 2.2,
                    pen.style(), Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
                painter.setPen(pen);
                painter.drawPath(path);
            }
            painter.restore();
        }

        std::optional<Vector3> normalOrigin;
        std::optional<Vector3> normalDirection;
        if (selection_.kind == CadSelectionKind::Surface && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Surfaces().size())
            && ShouldDisplay(CadSelectionKind::Surface, selection_.index,
                project_->Surfaces()[selection_.index].visible)) {
            const auto& surface = project_->Surfaces()[selection_.index].surface;
            normalOrigin = surface.Evaluate(0.5, 0.5);
            normalDirection = surface.Normal(0.5, 0.5);
        } else if (selection_.kind == CadSelectionKind::Plate && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Plates().size())
            && ShouldDisplay(CadSelectionKind::Plate, selection_.index,
                project_->Plates()[selection_.index].visible)) {
            const auto& plate = project_->Plates()[selection_.index].plate;
            const double u = plate.SourceU(0.5);
            const double v = plate.SourceV(0.5);
            normalOrigin = plate.SourceSurface().Evaluate(u, v);
            normalDirection = plate.SourceSurface().Normal(u, v);
        }
        if (normalOrigin.has_value() && normalDirection.has_value()) {
            const double arrowLength = std::clamp(36.0 / pixelsPerMillimeter_, 2.0, 12.0);
            const QPointF start = ProjectPoint(*normalOrigin);
            const QPointF end = ProjectPoint(*normalOrigin + *normalDirection * arrowLength);
            painter.setBrush(QColor("#087780"));
            painter.setPen(QPen(QColor("#087780"), 2.5));
            if (QLineF(start, end).length() < 4.0) {
                painter.drawEllipse(start, 5.0, 5.0);
                painter.drawLine(start + QPointF(-8.0, 0.0), start + QPointF(8.0, 0.0));
                painter.drawLine(start + QPointF(0.0, -8.0), start + QPointF(0.0, 8.0));
                painter.drawText(start + QPointF(10.0, -7.0), QStringLiteral("+ 法線"));
            } else {
                painter.drawLine(start, end);
                const QPointF direction = (end - start) / QLineF(start, end).length();
                const QPointF side(-direction.y(), direction.x());
                QPolygonF head;
                head << end
                     << end - direction * 10.0 + side * 4.5
                     << end - direction * 10.0 - side * 4.5;
                painter.drawPolygon(head);
                painter.drawText(end + QPointF(7.0, -5.0), QStringLiteral("+ 法線"));
            }
        }

        for (int index = 0; index < static_cast<int>(project_->Bodies().size()); ++index) {
            const auto& namedBody = project_->Bodies()[index];
            if (!ShouldDisplay(CadSelectionKind::Body, index, namedBody.visible)) {
                continue;
            }
            const auto& body = namedBody.body;
            const bool selected = IsSelected(CadSelectionKind::Body, index);
            const bool hovered = hoveredSelection_.kind == CadSelectionKind::Body
                && hoveredSelection_.index == index;
            QColor fill = selected ? QColor(230, 159, 0, 185)
                : hovered ? QColor(49, 145, 135, 190)
                : QColor(72, 128, 111, 160);
            const QColor edge = selected ? QColor("#b56b00")
                : hovered ? QColor("#167c73") : QColor("#356f62");
            painter.setPen(QPen(edge, selected || hovered ? 2.2 : 1.0));

            for (int layer = 0; layer < 2; ++layer) {
                QColor layerFill = fill;
                if (layer == 0) {
                    layerFill.setAlpha(std::max(56, fill.alpha() - 60));
                }
                painter.setBrush(layerFill);
                for (int uIndex = 0; uIndex < 32; ++uIndex) {
                    for (int vIndex = 0; vIndex < 10; ++vIndex) {
                        const double u0 = static_cast<double>(uIndex) / 32.0;
                        const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                        const double v0 = static_cast<double>(vIndex) / 10.0;
                        const double v1 = static_cast<double>(vIndex + 1) / 10.0;
                        QPolygonF patch;
                        patch << ProjectPoint(body.Evaluate(u0, v0, static_cast<double>(layer)))
                              << ProjectPoint(body.Evaluate(u1, v0, static_cast<double>(layer)))
                              << ProjectPoint(body.Evaluate(u1, v1, static_cast<double>(layer)))
                              << ProjectPoint(body.Evaluate(u0, v1, static_cast<double>(layer)));
                        painter.drawPolygon(patch);
                    }
                }
            }

            painter.setBrush(fill.darker(112));
            for (int sample = 0; sample < 32; ++sample) {
                const double t0 = static_cast<double>(sample) / 32.0;
                const double t1 = static_cast<double>(sample + 1) / 32.0;
                for (double fixed : {0.0, 1.0}) {
                    QPolygonF uSide;
                    uSide << ProjectPoint(body.Evaluate(t0, fixed, 0.0))
                          << ProjectPoint(body.Evaluate(t1, fixed, 0.0))
                          << ProjectPoint(body.Evaluate(t1, fixed, 1.0))
                          << ProjectPoint(body.Evaluate(t0, fixed, 1.0));
                    painter.drawPolygon(uSide);

                    QPolygonF vSide;
                    vSide << ProjectPoint(body.Evaluate(fixed, t0, 0.0))
                          << ProjectPoint(body.Evaluate(fixed, t1, 0.0))
                          << ProjectPoint(body.Evaluate(fixed, t1, 1.0))
                          << ProjectPoint(body.Evaluate(fixed, t0, 1.0));
                    painter.drawPolygon(vSide);
                }
            }
        }

        for (int index = 0; index < static_cast<int>(project_->Points().size()); ++index) {
            const auto& namedPoint = project_->Points()[index];
            if (!ShouldDisplay(CadSelectionKind::Point, index, namedPoint.visible)) {
                continue;
            }
            const bool selected = IsSelected(CadSelectionKind::Point, index);
            const bool hovered = hoveredSelection_.kind == CadSelectionKind::Point
                && hoveredSelection_.index == index;
            const QPointF center = ProjectPoint(namedPoint.point);
            const QColor color = selected ? QColor("#e69200")
                : hovered ? QColor("#087f9c") : QColor("#54767a");
            painter.save();
            painter.setBrush(selected || hovered ? QColor(255, 255, 255, 235) : color);
            painter.setPen(QPen(color, selected || hovered ? 2.0 : 1.3));
            painter.drawEllipse(center, selected || hovered ? 4.0 : 2.6,
                selected || hovered ? 4.0 : 2.6);
            painter.drawLine(center + QPointF(-6.0, 0.0), center + QPointF(6.0, 0.0));
            painter.drawLine(center + QPointF(0.0, -6.0), center + QPointF(0.0, 6.0));
            painter.restore();
        }

        for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
            const NamedWire& namedWire = project_->Wires()[index];
            if (!ShouldDisplay(CadSelectionKind::Wire, index, namedWire.visible)) {
                continue;
            }
            const Wire& displayedWire = draggedControlPoint_.has_value()
                    && draggedControlPoint_->wireIndex == index
                    && draggedWirePreview_.has_value()
                ? *draggedWirePreview_
                : namedWire.wire;
            const bool selected = IsSelected(CadSelectionKind::Wire, index);
            const bool reference = reference_.kind == CadSelectionKind::Wire && reference_.index == index;
            const bool hovered = hoveredSelection_.kind == CadSelectionKind::Wire
                && hoveredSelection_.index == index;
            QPainterPath path(ProjectPoint(displayedWire.Evaluate(0.0)));
            const int samples = displayedWire.Kind() == WireKind::Line ? 1 : 64;
            for (int sample = 1; sample <= samples; ++sample) {
                path.lineTo(ProjectPoint(displayedWire.Evaluate(static_cast<double>(sample) / samples)));
            }

            if (hovered) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(17, 132, 160, 92), selected ? 7.5 : 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
            }
            const QColor wireColor = reference ? QColor("#007f78")
                : selected ? QColor("#e69200")
                : hovered ? QColor("#087f9c")
                : namedWire.partModelSourceName.has_value() ? QColor("#c2402a")
                : namedWire.metadata.construction ? constructionWireColor_
                : wireColor_;
            const Qt::PenStyle wireStyle = reference ? Qt::DashDotLine
                : namedWire.metadata.construction ? constructionWireStyle_
                : wireStyle_;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(
                wireColor,
                hovered ? (selected || reference ? 4.2 : 3.4)
                        : reference || selected ? 3.2
                        : namedWire.metadata.construction ? constructionWireWidth_ : wireWidth_,
                wireStyle,
                Qt::RoundCap,
                Qt::RoundJoin));
            painter.drawPath(path);

            if (selected) {
                const auto activeControlPoint = [&](std::size_t controlIndex) {
                    const auto& pick = draggedControlPoint_.has_value()
                        ? draggedControlPoint_
                        : hoveredControlPoint_;
                    return pick.has_value() && pick->wireIndex == index
                        && pick->controlPointIndex == controlIndex;
                };
                if (displayedWire.Kind() == WireKind::CubicBezier
                    || displayedWire.Kind() == WireKind::CubicBSpline) {
                    QPainterPath controlPath(ProjectPoint(displayedWire.ControlPoints().front()));
                    for (std::size_t controlIndex = 1;
                         controlIndex < displayedWire.ControlPoints().size(); ++controlIndex) {
                        controlPath.lineTo(ProjectPoint(displayedWire.ControlPoints()[controlIndex]));
                    }
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor("#79838a"), 1.2, Qt::DashLine));
                    painter.drawPath(controlPath);
                }
                painter.setPen(QPen(QColor("#e69f00"), 2.0));
                for (std::size_t controlIndex = 0;
                     controlIndex < displayedWire.ControlPoints().size(); ++controlIndex) {
                    if (displayedWire.Kind() == WireKind::Circle && controlIndex > 1) {
                        continue;
                    }
                    if (displayedWire.Kind() == WireKind::Circle && controlIndex == 1
                        && namedWire.metadata.curveConstraints.radiusMillimeters.has_value()) {
                        continue;
                    }
                    const bool active = activeControlPoint(controlIndex);
                    painter.setBrush(active ? QColor("#1184a0") : QColor("#ffffff"));
                    const double radius = active ? 5.5 : 4.0;
                    painter.drawEllipse(
                        ProjectPoint(displayedWire.ControlPoints()[controlIndex]), radius, radius);
                }
                const auto drawEndpoint = [&](Vector3 point, std::size_t controlIndex) {
                    const QPointF screenPoint = ProjectPoint(point);
                    painter.setBrush(activeControlPoint(controlIndex)
                            ? QColor("#1184a0") : QColor("#ffffff"));
                    painter.drawRect(QRectF(screenPoint - QPointF(4.5, 4.5), QSizeF(9.0, 9.0)));
                };
                if (displayedWire.Kind() == WireKind::CircularArc) {
                    drawEndpoint(displayedWire.Start(), 1);
                    drawEndpoint(displayedWire.End(), 2);
                } else if (displayedWire.Kind() == WireKind::Circle
                    && !namedWire.metadata.curveConstraints.radiusMillimeters.has_value()) {
                    drawEndpoint(displayedWire.Start(), 1);
                } else if (displayedWire.Kind() != WireKind::Circle) {
                    drawEndpoint(displayedWire.Start(), 0);
                    if (!displayedWire.IsClosed()) {
                        drawEndpoint(displayedWire.End(), displayedWire.ControlPoints().size() - 1);
                    }
                }
                if (displayedWire.Kind() == WireKind::Circle
                    || displayedWire.Kind() == WireKind::CircularArc) {
                    const QPointF center = ProjectPoint(displayedWire.ArcData().center);
                    painter.setPen(QPen(QColor("#e69f00"), 1.5));
                    painter.drawLine(center - QPointF(6.0, 0.0), center + QPointF(6.0, 0.0));
                    painter.drawLine(center - QPointF(0.0, 6.0), center + QPointF(0.0, 6.0));
                }
            }
            if (reference) {
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#007f78"), 2.0));
                painter.drawRect(QRectF(ProjectPoint(namedWire.wire.Start()) - QPointF(4.0, 4.0), QSizeF(8.0, 8.0)));
                painter.drawRect(QRectF(ProjectPoint(namedWire.wire.End()) - QPointF(4.0, 4.0), QSizeF(8.0, 8.0)));
                painter.drawText(
                    ProjectPoint(namedWire.wire.Evaluate(0.5)) + QPointF(6.0, -7.0),
                    QStringLiteral("基準"));
            }
        }

        painter.save();
        const auto findWire = [this](const std::string& name) {
            return std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const NamedWire& wire) {
                return wire.name == name;
            });
        };
        const auto endpointPoint = [](const NamedWire& wire, kachakacha::model::WireEndpoint endpoint) {
            return endpoint == kachakacha::model::WireEndpoint::Start
                ? wire.wire.Start()
                : wire.wire.End();
        };
        for (const auto& constraint : project_->CoincidentConstraints()) {
            const auto anchorWire = findWire(constraint.anchor.wireName);
            const auto followerWire = findWire(constraint.follower.wireName);
            if (anchorWire == project_->Wires().end() || followerWire == project_->Wires().end()) {
                continue;
            }
            const Vector3 point = endpointPoint(*anchorWire, constraint.anchor.endpoint);
            const QPointF screenPoint = ProjectPoint(point);
            const int anchorIndex = static_cast<int>(std::distance(project_->Wires().begin(), anchorWire));
            const int followerIndex = static_cast<int>(std::distance(project_->Wires().begin(), followerWire));
            if (!ShouldDisplay(CadSelectionKind::Wire, anchorIndex, anchorWire->visible)
                || !ShouldDisplay(CadSelectionKind::Wire, followerIndex, followerWire->visible)) {
                continue;
            }
            const auto smoothConstraint = std::find_if(
                project_->TangentConstraints().begin(), project_->TangentConstraints().end(),
                [&](const auto& candidate) {
                    return candidate.anchor.wireName == constraint.anchor.wireName
                        && candidate.anchor.endpoint == constraint.anchor.endpoint
                        && candidate.follower.wireName == constraint.follower.wireName
                        && candidate.follower.endpoint == constraint.follower.endpoint;
                });
            const bool tangent = smoothConstraint != project_->TangentConstraints().end();
            const bool curvature = tangent
                && smoothConstraint->continuity == kachakacha::model::WireContinuity::G2Curvature;
            const bool emphasized = tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
                || tool_ == ViewportTool::Curvature
                || IsSelected(CadSelectionKind::Wire, anchorIndex)
                || IsSelected(CadSelectionKind::Wire, followerIndex);
            const QColor markerColor(curvature ? "#7a4c9e" : tangent ? "#2f7d4a" : "#0b7f78");
            painter.setBrush(QColor(255, 255, 255, 235));
            painter.setPen(QPen(markerColor, emphasized ? 2.6 : 1.8));
            painter.drawEllipse(screenPoint + QPointF(-3.0, 0.0), 4.0, 4.0);
            painter.drawEllipse(screenPoint + QPointF(3.0, 0.0), 4.0, 4.0);
            if (tangent) {
                painter.drawLine(screenPoint + QPointF(-8.0, 6.0), screenPoint + QPointF(8.0, 6.0));
                if (curvature) {
                    painter.drawLine(screenPoint + QPointF(-8.0, 10.0), screenPoint + QPointF(8.0, 10.0));
                }
            }
            if (emphasized) {
                painter.setPen(markerColor.darker(125));
                painter.drawText(
                    screenPoint + QPointF(9.0, -8.0),
                    curvature ? QStringLiteral("G2")
                              : tangent ? QStringLiteral("G1") : QStringLiteral("一致"));
            }
        }
        if (!coincidencePicks_.empty()) {
            const QPointF screenPoint = ProjectPoint(coincidencePicks_.front().point);
            painter.setBrush(QColor("#ffffff"));
            painter.setPen(QPen(QColor("#0b7f78"), 2.8));
            painter.drawRect(QRectF(screenPoint - QPointF(6.0, 6.0), QSizeF(12.0, 12.0)));
            painter.drawText(screenPoint + QPointF(10.0, -9.0), QStringLiteral("固定側"));
        }
        painter.restore();

        if (hoveredSelection_.kind == CadSelectionKind::Wire
            && !((tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire)
                && directLineEditPreviewWire_.has_value())
            && hoveredSelection_.index >= 0
            && hoveredSelection_.index < static_cast<int>(project_->Wires().size())) {
            const NamedWire& hoveredWire = project_->Wires()[hoveredSelection_.index];
            Vector3 anchorPoint = hoveredWire.wire.Evaluate(0.5);
            QString hoverText = QString::fromUtf8(hoveredWire.name);
            if (hoveredWire.metadata.construction) {
                hoverText += QStringLiteral("  （補助）");
            }
            if (hoveredWirePoint_.has_value()) {
                anchorPoint = *hoveredWirePoint_;
                hoverText += hoveredWire.wire.IsClosed()
                    ? QStringLiteral("  基準点")
                    : AlmostEqual(anchorPoint, hoveredWire.wire.Start(), 1.0e-8)
                    ? QStringLiteral("  始点")
                    : AlmostEqual(anchorPoint, hoveredWire.wire.End(), 1.0e-8)
                    ? QStringLiteral("  終点")
                    : QStringLiteral("  制御点");
                const QPointF screenPoint = ProjectPoint(anchorPoint);
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#087f9c"), 2.4));
                painter.drawEllipse(screenPoint, 6.0, 6.0);
            } else if (hoveredWireParameter_.has_value()) {
                anchorPoint = hoveredWire.wire.Evaluate(*hoveredWireParameter_);
            }

            const QFontMetrics metrics = painter.fontMetrics();
            const QRect textBounds = metrics.boundingRect(hoverText);
            QPointF labelAnchor = ProjectPoint(anchorPoint) + QPointF(10.0, -10.0);
            if (!rect().contains(hoverScreenPosition_)) {
                labelAnchor = QPointF(hoverScreenPosition_) + QPointF(10.0, -10.0);
            }
            QRectF labelBox(
                labelAnchor.x(), labelAnchor.y() - textBounds.height() - 7.0,
                textBounds.width() + 14.0, textBounds.height() + 10.0);
            const QRectF viewportBounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
            if (labelBox.right() > viewportBounds.right()) {
                labelBox.moveRight(viewportBounds.right());
            }
            if (labelBox.left() < viewportBounds.left()) {
                labelBox.moveLeft(viewportBounds.left());
            }
            if (labelBox.top() < viewportBounds.top()) {
                labelBox.moveTop(viewportBounds.top());
            }
            if (labelBox.bottom() > viewportBounds.bottom()) {
                labelBox.moveBottom(viewportBounds.bottom());
            }
            painter.setPen(QPen(QColor("#087f9c"), 1.0));
            painter.setBrush(QColor(255, 255, 255, 238));
            painter.drawRoundedRect(labelBox, 3.0, 3.0);
            painter.setPen(QColor("#075f69"));
            painter.drawText(labelBox, Qt::AlignCenter, hoverText);
        }

        if (directLineEditPreviewWire_.has_value()) {
            painter.save();
            const bool trim = tool_ == ViewportTool::TrimWire;
            const QColor previewColor = trim ? QColor("#c0392b") : QColor("#27845c");
            const Wire& preview = *directLineEditPreviewWire_;
            QPainterPath previewPath(ProjectPoint(preview.Start()));
            const int previewSegments = preview.Kind() == WireKind::Line ? 1 : 96;
            for (int segment = 1; segment <= previewSegments; ++segment) {
                previewPath.lineTo(ProjectPoint(preview.Evaluate(
                    static_cast<double>(segment) / previewSegments)));
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 215), 8.0,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(previewPath);
            painter.setPen(QPen(previewColor, trim ? 5.0 : 3.8,
                trim ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(previewPath);

            const QPointF labelPoint = ProjectPoint(preview.Evaluate(0.5)) + QPointF(10.0, -12.0);
            const QString label = trim
                ? QStringLiteral("クリックで削除")
                : QStringLiteral("クリックでここまで延長");
            const QFontMetrics metrics = painter.fontMetrics();
            const QRect textBounds = metrics.boundingRect(label);
            QRectF labelBox(
                labelPoint.x(), labelPoint.y() - textBounds.height() - 7.0,
                textBounds.width() + 14.0, textBounds.height() + 10.0);
            const QRectF viewportBounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
            if (labelBox.right() > viewportBounds.right()) {
                labelBox.moveRight(viewportBounds.right());
            }
            if (labelBox.left() < viewportBounds.left()) {
                labelBox.moveLeft(viewportBounds.left());
            }
            if (labelBox.top() < viewportBounds.top()) {
                labelBox.moveTop(viewportBounds.top());
            }
            if (labelBox.bottom() > viewportBounds.bottom()) {
                labelBox.moveBottom(viewportBounds.bottom());
            }
            painter.setPen(QPen(previewColor, 1.0));
            painter.setBrush(QColor(255, 255, 255, 242));
            painter.drawRoundedRect(labelBox, 3.0, 3.0);
            painter.setPen(previewColor.darker(125));
            painter.drawText(labelBox, Qt::AlignCenter, label);

            if (directLineEditPreviewIntersection_.has_value()) {
                const QPointF intersection = ProjectPoint(*directLineEditPreviewIntersection_);
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(previewColor, 2.4));
                painter.drawEllipse(intersection, 5.0, 5.0);
            }
            painter.restore();
        }
    }

    if (!wireOffsetPreviews_.empty()) {
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#8b3fa7"), 2.4, Qt::DashLine));
        for (const Wire& wire : wireOffsetPreviews_) {
            QPainterPath path(ProjectPoint(wire.Evaluate(0.0)));
            const int samples = wire.Kind() == WireKind::Line ? 1 : 64;
            for (int sample = 1; sample <= samples; ++sample) {
                path.lineTo(ProjectPoint(wire.Evaluate(static_cast<double>(sample) / samples)));
            }
            painter.drawPath(path);
        }
        painter.restore();
    }

    if (activePlane_.has_value() && hoverDrawingPoint_.has_value() && tool_ != ViewportTool::Select) {
        painter.setPen(QPen(QColor("#d97706"), 2.0, Qt::DashLine));
        const auto drawPreviewWire = [&](const Wire& wire) {
            QPainterPath path(ProjectPoint(wire.Evaluate(0.0)));
            for (int sample = 1; sample <= 64; ++sample) {
                path.lineTo(ProjectPoint(wire.Evaluate(static_cast<double>(sample) / 64.0)));
            }
            painter.drawPath(path);
        };
        if (!drawingPoints_.empty()) {
            if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline) {
                QPainterPath path(ProjectPoint(drawingPoints_.front()));
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    path.lineTo(ProjectPoint(drawingPoints_[index]));
                }
                path.lineTo(ProjectPoint(*hoverDrawingPoint_));
                painter.drawPath(path);
            } else if (tool_ == ViewportTool::DrawRectangle) {
                const auto first = activePlane_->Project(drawingPoints_.front());
                const auto second = activePlane_->Project(*hoverDrawingPoint_);
                QPolygonF rectangle;
                rectangle << ProjectPoint(activePlane_->ToWorld(first.u, first.v))
                          << ProjectPoint(activePlane_->ToWorld(second.u, first.v))
                          << ProjectPoint(activePlane_->ToWorld(second.u, second.v))
                          << ProjectPoint(activePlane_->ToWorld(first.u, second.v));
                painter.drawPolygon(rectangle);
            } else if (tool_ == ViewportTool::DrawCircle) {
                const double radius = (*hoverDrawingPoint_ - drawingPoints_.front()).Length();
                QPainterPath circlePath(ProjectPoint(activePlane_->ToWorld(
                    activePlane_->Project(drawingPoints_.front()).u + radius,
                    activePlane_->Project(drawingPoints_.front()).v)));
                const auto center = activePlane_->Project(drawingPoints_.front());
                for (int sample = 1; sample <= 64; ++sample) {
                    const double angle = static_cast<double>(sample) / 64.0 * 6.28318530717958647692;
                    circlePath.lineTo(ProjectPoint(activePlane_->ToWorld(
                        center.u + std::cos(angle) * radius,
                        center.v + std::sin(angle) * radius)));
                }
                painter.drawPath(circlePath);
            } else if (tool_ == ViewportTool::DrawArc) {
                if (arcDrawingMode_ == ArcDrawingMode::ThreePoints) {
                    if (drawingPoints_.size() == 1) {
                        painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                    } else {
                        try {
                            drawPreviewWire(Wire::CircularArcThroughThreePoints(
                                drawingPoints_[0], drawingPoints_[1], *hoverDrawingPoint_));
                        } catch (const std::exception&) {
                            QPainterPath guide(ProjectPoint(drawingPoints_[0]));
                            guide.lineTo(ProjectPoint(drawingPoints_[1]));
                            guide.lineTo(ProjectPoint(*hoverDrawingPoint_));
                            painter.drawPath(guide);
                        }
                    }
                } else if (arcDrawingMode_ == ArcDrawingMode::EndpointsRadius) {
                    if (drawingPoints_.size() == 1) {
                        painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                    } else if (drawingPoints_.size() == 2) {
                        try {
                            drawPreviewWire(Wire::CircularArcFromEndpointsRadius(
                                drawingPoints_[0], drawingPoints_[1], activePlane_->Normal(),
                                configuredArcRadius_, configuredArcBulgeLeft_));
                        } catch (const std::exception&) {
                            painter.drawLine(
                                ProjectPoint(drawingPoints_[0]), ProjectPoint(drawingPoints_[1]));
                        }
                    }
                } else if (drawingPoints_.size() == 1) {
                    try {
                        const double direction = configuredArcTangentAngleDegrees_
                            * std::numbers::pi / 180.0;
                        const Vector3 tangent = activePlane_->UAxis() * std::cos(direction)
                            + activePlane_->VAxis() * std::sin(direction);
                        drawPreviewWire(Wire::CircularArcFromStartTangent(
                            drawingPoints_.front(), tangent, activePlane_->Normal(),
                            configuredArcRadius_, configuredArcSweepAngleDegrees_
                                * std::numbers::pi / 180.0));
                        painter.drawLine(
                            ProjectPoint(drawingPoints_.front()),
                            ProjectPoint(drawingPoints_.front()
                                + tangent * std::max(configuredArcRadius_ * 0.65, 2.0)));
                    } catch (const std::exception&) {
                    }
                }
            } else if (tool_ == ViewportTool::DrawBezier) {
                QPainterPath guide(ProjectPoint(drawingPoints_.front()));
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    guide.lineTo(ProjectPoint(drawingPoints_[index]));
                }
                guide.lineTo(ProjectPoint(*hoverDrawingPoint_));
                painter.drawPath(guide);
                if (drawingPoints_.size() == 3) {
                    try {
                        drawPreviewWire(Wire::CubicBezier(
                            drawingPoints_[0], drawingPoints_[1], drawingPoints_[2], *hoverDrawingPoint_));
                    } catch (const std::exception&) {
                    }
                }
            } else if (tool_ == ViewportTool::DrawSpline) {
                std::vector<Vector3> previewPoints = drawingPoints_;
                previewPoints.push_back(*hoverDrawingPoint_);
                QPainterPath guide(ProjectPoint(previewPoints.front()));
                for (std::size_t index = 1; index < previewPoints.size(); ++index) {
                    guide.lineTo(ProjectPoint(previewPoints[index]));
                }
                painter.drawPath(guide);
                if (previewPoints.size() >= 4) {
                    try {
                        drawPreviewWire(Wire::InterpolatingCubicBSpline(previewPoints));
                    } catch (const std::exception&) {
                    }
                }
            } else if ((tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection)
                && project_ != nullptr) {
                const Vector3 delta = *hoverDrawingPoint_ - drawingPoints_.front();
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                for (const CadSelection& selection : selections_) {
                    if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                        || selection.index >= static_cast<int>(project_->Wires().size())) {
                        continue;
                    }
                    drawPreviewWire(project_->Wires()[selection.index].wire.Translated(delta));
                }
            } else if (tool_ == ViewportTool::MirrorSelection && project_ != nullptr) {
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                const Vector3 direction = *hoverDrawingPoint_ - drawingPoints_.front();
                try {
                    for (const CadSelection& selection : selections_) {
                        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                            || selection.index >= static_cast<int>(project_->Wires().size())) {
                            continue;
                        }
                        drawPreviewWire(project_->Wires()[selection.index].wire.Mirrored(
                            drawingPoints_.front(), direction, activePlane_->Normal()));
                    }
                } catch (const std::exception&) {
                }
            } else if (tool_ == ViewportTool::RotateSelection && project_ != nullptr) {
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                if (drawingPoints_.size() == 2) {
                    painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(drawingPoints_[1]));
                    const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
                    const Vector3 to = *hoverDrawingPoint_ - drawingPoints_[0];
                    if (from.LengthSquared() > 1.0e-18 && to.LengthSquared() > 1.0e-18) {
                        const Vector3 normal = activePlane_->Normal();
                        const double angle = std::atan2(Dot(Cross(from, to), normal), Dot(from, to));
                        for (const CadSelection& selection : selections_) {
                            if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                                || selection.index >= static_cast<int>(project_->Wires().size())) {
                                continue;
                            }
                            drawPreviewWire(project_->Wires()[selection.index].wire.RotatedAroundAxis(
                                drawingPoints_[0], normal, angle));
                        }
                    }
                }
            }
        }
        painter.setBrush(QColor(255, 255, 255, 190));
        painter.setPen(QPen(QColor(217, 119, 6, 185), 1.3));
        painter.drawEllipse(ProjectPoint(*hoverDrawingPoint_), 2.7, 2.7);
        for (const Vector3& point : drawingPoints_) {
            painter.drawEllipse(ProjectPoint(point), 4.0, 4.0);
        }
        if (drawingSnapHover_.has_value()) {
            const QPointF center = ProjectPoint(drawingSnapHover_->point);
            painter.save();
            const bool structural = drawingSnapHover_->kind != DrawingSnapKind::Grid;
            painter.setBrush(QColor(255, 255, 255, structural ? 225 : 175));
            painter.setPen(QPen(
                structural ? QColor(0, 126, 138, 235) : QColor(8, 119, 128, 125),
                structural ? 2.2 : 1.0));
            if (structural) {
                painter.drawEllipse(center, 10.5, 10.5);
            }
            switch (drawingSnapHover_->kind) {
            case DrawingSnapKind::Intersection:
                painter.drawLine(center + QPointF(-5.5, -5.5), center + QPointF(5.5, 5.5));
                painter.drawLine(center + QPointF(-5.5, 5.5), center + QPointF(5.5, -5.5));
                break;
            case DrawingSnapKind::Point:
                painter.drawEllipse(center, 4.5, 4.5);
                break;
            case DrawingSnapKind::Endpoint:
                painter.drawRect(QRectF(center - QPointF(4.5, 4.5), QSizeF(9.0, 9.0)));
                break;
            case DrawingSnapKind::ProjectedPoint: {
                // 別平面の点の投影: ひし形マーカー+元の点への破線。
                painter.setPen(QPen(QColor(124, 58, 237, 235), 2.0));
                QPolygonF diamond;
                diamond << center + QPointF(0.0, -5.5) << center + QPointF(5.5, 0.0)
                        << center + QPointF(0.0, 5.5) << center + QPointF(-5.5, 0.0);
                painter.drawPolygon(diamond);
                if (drawingSnapHover_->guideAnchor.has_value()) {
                    painter.setPen(QPen(QColor(124, 58, 237, 130), 1.2, Qt::DashLine));
                    painter.drawLine(center, ProjectPoint(*drawingSnapHover_->guideAnchor));
                }
                break;
            }
            case DrawingSnapKind::Extension: {
                // 延長線の推測: 線分端からの破線ガイド。
                painter.setPen(QPen(QColor(217, 119, 6, 200), 1.6, Qt::DashLine));
                if (drawingSnapHover_->guideAnchor.has_value()) {
                    painter.drawLine(
                        ProjectPoint(*drawingSnapHover_->guideAnchor), center);
                }
                painter.setPen(QPen(QColor(217, 119, 6, 230), 2.0));
                painter.drawLine(center + QPointF(-5.0, 0.0), center + QPointF(5.0, 0.0));
                painter.drawLine(center + QPointF(0.0, -5.0), center + QPointF(0.0, 5.0));
                break;
            }
            case DrawingSnapKind::Grid:
                painter.setPen(QPen(QColor(8, 119, 128, 105), 1.0));
                painter.drawEllipse(center, 3.0, 3.0);
                break;
            case DrawingSnapKind::None:
                break;
            }
            painter.restore();
        }
    }

    if (tool_ == ViewportTool::MoveGridOrigin
        && gridOriginDragSource_.has_value() && gridOriginDragTarget_.has_value()) {
        const QPointF source = ProjectPoint(*gridOriginDragSource_);
        const QPointF target = ProjectPoint(*gridOriginDragTarget_);
        painter.setBrush(QColor(255, 255, 255, 225));
        painter.setPen(QPen(QColor("#d97706"), 2.2, Qt::DashLine));
        painter.drawLine(source, target);
        painter.drawEllipse(source, 5.5, 5.5);
        painter.setBrush(QColor("#d97706"));
        painter.drawEllipse(target, 5.0, 5.0);
    }

    if (tool_ == ViewportTool::SplitWire && splitPreviewParameter_.has_value() && project_ != nullptr) {
        const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
            return selection.kind == CadSelectionKind::Wire;
        });
        if (selectedWire != selections_.end() && selectedWire->index >= 0
            && selectedWire->index < static_cast<int>(project_->Wires().size())) {
            const QPointF splitPoint = ProjectPoint(project_->Wires()[selectedWire->index].wire.Evaluate(*splitPreviewParameter_));
            painter.setBrush(QColor("#ffffff"));
            painter.setPen(QPen(QColor("#c0392b"), 2.5));
            painter.drawEllipse(splitPoint, 6.0, 6.0);
            painter.drawLine(splitPoint + QPointF(-9.0, 0.0), splitPoint + QPointF(9.0, 0.0));
        }
    }

    if (displayMode_ == ViewportDisplayMode::Design
        && !referenceDimensionOverlays_.empty()) {
        painter.save();
        const QColor dimensionColor("#256b63");
        const QRectF viewportBounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
        for (std::size_t index = 0; index < referenceDimensionOverlays_.size(); ++index) {
            const ReferenceDimensionOverlay& overlay = referenceDimensionOverlays_[index];
            const QPointF first = ProjectPoint(overlay.firstPoint);
            const QPointF second = ProjectPoint(overlay.secondPoint);
            painter.setPen(QPen(dimensionColor, 1.6, Qt::DashLine));
            painter.drawLine(first, second);
            painter.setBrush(QColor("#ffffff"));
            painter.drawEllipse(first, 3.5, 3.5);
            painter.drawEllipse(second, 3.5, 3.5);

            const QFontMetrics metrics = painter.fontMetrics();
            const QRect textBounds = metrics.boundingRect(overlay.text);
            const QPointF midpoint = (first + second) * 0.5;
            const QPointF labelAnchor = midpoint
                + QPointF(8.0, -8.0 - static_cast<double>(index % 4) * 15.0);
            QRectF labelBox(
                labelAnchor.x(),
                labelAnchor.y() - textBounds.height() - 7.0,
                textBounds.width() + 14.0,
                textBounds.height() + 10.0);
            if (labelBox.right() > viewportBounds.right()) {
                labelBox.moveRight(viewportBounds.right());
            }
            if (labelBox.left() < viewportBounds.left()) {
                labelBox.moveLeft(viewportBounds.left());
            }
            if (labelBox.top() < viewportBounds.top()) {
                labelBox.moveTop(viewportBounds.top());
            }
            if (labelBox.bottom() > viewportBounds.bottom()) {
                labelBox.moveBottom(viewportBounds.bottom());
            }
            painter.setPen(QPen(dimensionColor, 1.0));
            painter.setBrush(QColor(248, 255, 253, 238));
            painter.drawRoundedRect(labelBox, 3.0, 3.0);
            painter.setPen(dimensionColor);
            painter.drawText(labelBox, Qt::AlignCenter, overlay.text);
        }
        painter.restore();
    }

    if (!measurementPicks_.empty() || measurementOverlayFirst_.has_value()) {
        painter.save();
        const QColor measurementColor("#8b3fb0");
        painter.setBrush(QColor("#ffffff"));
        painter.setPen(QPen(measurementColor, 2.2));
        for (const MeasurementPick& pick : measurementPicks_) {
            const QPointF point = ProjectPoint(pick.point);
            painter.drawEllipse(point, 5.0, 5.0);
            painter.drawLine(point + QPointF(-8.0, 0.0), point + QPointF(8.0, 0.0));
            painter.drawLine(point + QPointF(0.0, -8.0), point + QPointF(0.0, 8.0));
        }
        if (measurementOverlayFirst_.has_value()) {
            const QPointF first = ProjectPoint(*measurementOverlayFirst_);
            QPointF labelAnchor = first + QPointF(10.0, -10.0);
            if (measurementOverlaySecond_.has_value()) {
                const QPointF second = ProjectPoint(*measurementOverlaySecond_);
                if (measurementOverlayComponentTexts_.size() == 3) {
                    const Vector3& start = *measurementOverlayFirst_;
                    const Vector3& end = *measurementOverlaySecond_;
                    const std::array<Vector3, 4> componentPoints{{
                        start,
                        {end.x, start.y, start.z},
                        {end.x, end.y, start.z},
                        end,
                    }};
                    const std::array<QColor, 3> componentColors{{
                        QColor("#c73535"), QColor("#2d8a4a"), QColor("#2674c8")}};
                    const std::array<QPointF, 3> labelOffsets{{
                        {6.0, -8.0}, {6.0, 17.0}, {6.0, -8.0}}};
                    for (int axis = 0; axis < 3; ++axis) {
                        const QPointF componentStart = ProjectPoint(componentPoints[axis]);
                        const QPointF componentEnd = ProjectPoint(componentPoints[axis + 1]);
                        painter.setPen(QPen(componentColors[axis], 2.6));
                        if (QLineF(componentStart, componentEnd).length() > 1.0) {
                            painter.drawLine(componentStart, componentEnd);
                        } else {
                            painter.drawEllipse(componentStart, 3.0, 3.0);
                        }
                        const QString& componentText = measurementOverlayComponentTexts_[axis];
                        const QFontMetrics componentMetrics = painter.fontMetrics();
                        const QRect componentBounds = componentMetrics.boundingRect(componentText);
                        const QPointF componentAnchor
                            = (componentStart + componentEnd) * 0.5 + labelOffsets[axis];
                        QRectF componentBox(
                            componentAnchor.x(), componentAnchor.y() - componentBounds.height() - 5.0,
                            componentBounds.width() + 10.0, componentBounds.height() + 8.0);
                        const QRectF bounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
                        if (componentBox.left() < bounds.left()) {
                            componentBox.moveLeft(bounds.left());
                        }
                        if (componentBox.right() > bounds.right()) {
                            componentBox.moveRight(bounds.right());
                        }
                        if (componentBox.top() < bounds.top()) {
                            componentBox.moveTop(bounds.top());
                        }
                        if (componentBox.bottom() > bounds.bottom()) {
                            componentBox.moveBottom(bounds.bottom());
                        }
                        painter.setPen(QPen(componentColors[axis], 1.0));
                        painter.setBrush(QColor(255, 255, 255, 236));
                        painter.drawRoundedRect(componentBox, 3.0, 3.0);
                        painter.drawText(componentBox, Qt::AlignCenter, componentText);
                    }
                }
                painter.setPen(QPen(measurementColor, 2.0, Qt::DashLine));
                painter.drawLine(first, second);
                painter.setBrush(QColor("#ffffff"));
                painter.drawEllipse(first, 4.0, 4.0);
                painter.drawEllipse(second, 4.0, 4.0);
                labelAnchor = (first + second) * 0.5 + QPointF(8.0, -8.0);
                if (measurementOverlayThird_.has_value()) {
                    const QPointF third = ProjectPoint(*measurementOverlayThird_);
                    painter.drawLine(first, third);
                    painter.drawEllipse(third, 4.0, 4.0);
                    labelAnchor = (first + second + third) / 3.0 + QPointF(8.0, -8.0);
                }
            }
            if (!measurementOverlayText_.isEmpty()) {
                const QFontMetrics metrics = painter.fontMetrics();
                const QRect textBounds = metrics.boundingRect(measurementOverlayText_);
                QRectF labelBox(
                    labelAnchor.x(),
                    labelAnchor.y() - textBounds.height() - 7.0,
                    textBounds.width() + 14.0,
                    textBounds.height() + 10.0);
                labelBox = labelBox.intersected(QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0));
                painter.setPen(QPen(measurementColor, 1.0));
                painter.setBrush(QColor(255, 255, 255, 232));
                painter.drawRoundedRect(labelBox, 3.0, 3.0);
                painter.setPen(measurementColor);
                painter.drawText(labelBox, Qt::AlignCenter, measurementOverlayText_);
            }
        }
        painter.restore();
    }

    if (surfaceDiagnosticMode_ == SurfaceDiagnosticMode::GaussianCurvature) {
        painter.save();
        const QRectF legend(18.0, height() - 54.0, 300.0, 34.0);
        painter.setBrush(QColor(255, 255, 255, 224));
        painter.setPen(QPen(QColor("#829097"), 1.0));
        painter.drawRoundedRect(legend, 3.0, 3.0);
        const std::array<QColor, 3> colors{
            QColor("#3568c0"), QColor("#2f8f78"), QColor("#d94b37")};
        const std::array<QString, 3> texts{
            QStringLiteral("鞍状"), QStringLiteral("一方向"), QStringLiteral("二重曲率")};
        for (int index = 0; index < 3; ++index) {
            const QRectF swatch(
                legend.left() + 10.0 + index * 96.0, legend.top() + 8.0, 20.0, 18.0);
            painter.setBrush(colors[index]);
            painter.setPen(Qt::NoPen);
            painter.drawRect(swatch);
            painter.setPen(QColor("#2f3d43"));
            painter.drawText(QRectF(
                swatch.right() + 4.0, legend.top() + 5.0, 66.0, 24.0),
                Qt::AlignVCenter | Qt::AlignLeft, texts[index]);
        }
        painter.restore();
    }

    const std::array<std::pair<Vector3, QColor>, 3> axes = {{
        {{8.0, 0.0, 0.0}, QColor("#c33b3b")},
        {{0.0, 8.0, 0.0}, QColor("#32844b")},
        {{0.0, 0.0, 8.0}, QColor("#336fc2")},
    }};
    const std::array<QString, 3> labels = {"X", "Y", "Z"};
    for (int index = 0; index < 3; ++index) {
        painter.setPen(QPen(axes[index].second, 2.5));
        painter.drawLine(ProjectPoint({0.0, 0.0, 0.0}), ProjectPoint(axes[index].first));
        painter.drawText(ProjectPoint(axes[index].first) + QPointF(4.0, -4.0), labels[index]);
    }

    painter.setPen(QColor("#52606a"));
    QString modeText;
    switch (tool_) {
    case ViewportTool::Select:
        modeText = QStringLiteral("選択");
        break;
    case ViewportTool::MoveGridOrigin:
        modeText = QStringLiteral("点グリッド基準をドラッグ");
        break;
    case ViewportTool::DrawPoint:
        modeText = QStringLiteral("作図点");
        break;
    case ViewportTool::DrawLine:
        modeText = QStringLiteral("直線");
        break;
    case ViewportTool::DrawPolyline:
        modeText = QStringLiteral("ポリライン");
        break;
    case ViewportTool::DrawRectangle:
        modeText = QStringLiteral("矩形");
        break;
    case ViewportTool::DrawCircle:
        modeText = QStringLiteral("円");
        break;
    case ViewportTool::DrawArc:
        modeText = arcDrawingMode_ == ArcDrawingMode::ThreePoints
            ? QStringLiteral("3点円弧")
            : arcDrawingMode_ == ArcDrawingMode::EndpointsRadius
            ? QStringLiteral("両端＋半径 円弧")
            : QStringLiteral("始点＋方向 円弧");
        break;
    case ViewportTool::DrawBezier:
        modeText = QStringLiteral("ベジェ曲線");
        break;
    case ViewportTool::DrawSpline:
        modeText = QStringLiteral("通過点スプライン");
        break;
    case ViewportTool::MoveSelection:
        modeText = QStringLiteral("移動");
        break;
    case ViewportTool::CopySelection:
        modeText = QStringLiteral("コピー");
        break;
    case ViewportTool::MirrorSelection:
        modeText = QStringLiteral("ミラー複製");
        break;
    case ViewportTool::RotateSelection:
        modeText = QStringLiteral("回転");
        break;
    case ViewportTool::SplitWire:
        modeText = QStringLiteral("分割");
        break;
    case ViewportTool::TrimWire:
        modeText = QStringLiteral("トリム · 消す部分をクリック");
        break;
    case ViewportTool::ExtendWire:
        modeText = QStringLiteral("延長 · 伸ばす端側をクリック");
        break;
    case ViewportTool::Coincident:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("一致 · 固定側の端点")
            : QStringLiteral("一致 · 追従側の端点");
        break;
    case ViewportTool::Tangent:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("接線 · 固定側の端点")
            : QStringLiteral("接線 · 追従曲線の端点");
        break;
    case ViewportTool::Curvature:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("曲率 · 固定側の端点")
            : QStringLiteral("曲率 · 追従ベジェの端点");
        break;
    case ViewportTool::Measure:
        modeText = measurementMode_ == MeasurementMode::TwoPoints
            ? QStringLiteral("測定 · 2点間")
            : measurementMode_ == MeasurementMode::ThreePointsAngle
            ? QStringLiteral("測定 · 3点角度")
            : QStringLiteral("測定 · 要素");
        break;
    case ViewportTool::LineBetweenPoints:
        modeText = lineBetweenFirstPoint_.has_value()
            ? QStringLiteral("2点間線 · 2点目（点・端点・交点に吸着）")
            : QStringLiteral("2点間線 · 1点目（点・端点・交点に吸着）");
        break;
    }
    if (activePlane_.has_value() && hoverDrawingPoint_.has_value()) {
        const auto coordinates = activePlane_->Project(*hoverDrawingPoint_);
        modeText += QStringLiteral("   U %1 mm   V %2 mm").arg(coordinates.u, 0, 'f', 3).arg(coordinates.v, 0, 'f', 3);
        if (!drawingPoints_.empty() && tool_ != ViewportTool::Select) {
            const auto start = activePlane_->Project(drawingPoints_.front());
            if (tool_ == ViewportTool::DrawLine) {
                modeText += QStringLiteral("   長さ %1 mm").arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawPolyline) {
                double length = 0.0;
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    length += (drawingPoints_[index] - drawingPoints_[index - 1]).Length();
                }
                length += (*hoverDrawingPoint_ - drawingPoints_.back()).Length();
                modeText += QStringLiteral("   合計 %1 mm").arg(length, 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawSpline) {
                modeText += QStringLiteral("   通過点 %1").arg(drawingPoints_.size());
            } else if (tool_ == ViewportTool::DrawRectangle) {
                modeText += QStringLiteral("   幅 %1 mm   高さ %2 mm")
                    .arg(std::abs(coordinates.u - start.u), 0, 'f', 3)
                    .arg(std::abs(coordinates.v - start.v), 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawCircle) {
                modeText += QStringLiteral("   半径 %1 mm").arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection) {
                const Vector3 delta = *hoverDrawingPoint_ - drawingPoints_.front();
                modeText += QStringLiteral("   移動 X %1   Y %2   Z %3 mm")
                    .arg(delta.x, 0, 'f', 3)
                    .arg(delta.y, 0, 'f', 3)
                    .arg(delta.z, 0, 'f', 3);
            } else if (tool_ == ViewportTool::MirrorSelection) {
                modeText += QStringLiteral("   軸長 %1 mm")
                    .arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::RotateSelection && drawingPoints_.size() == 2) {
                const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
                const Vector3 to = *hoverDrawingPoint_ - drawingPoints_[0];
                if (from.LengthSquared() > 1.0e-18 && to.LengthSquared() > 1.0e-18) {
                    const double angle = std::atan2(
                        Dot(Cross(from, to), activePlane_->Normal()), Dot(from, to));
                    modeText += QStringLiteral("   角度 %1 °").arg(angle * 180.0 / 3.14159265358979323846, 0, 'f', 2);
                }
            }
        }
    }
    painter.drawText(QRect(14, 12, std::max(80, width() - 270), 24), Qt::AlignLeft, modeText);

    if (tool_ == ViewportTool::LineBetweenPoints) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (lineBetweenFirstPoint_.has_value()) {
            const QPointF first = ProjectPoint(*lineBetweenFirstPoint_);
            painter.setBrush(QColor("#087780"));
            painter.setPen(QPen(QColor("#075f69"), 1.5));
            painter.drawEllipse(first, 5.0, 5.0);
            if (lineBetweenHoverPoint_.has_value()) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor("#087780"), 1.5, Qt::DashLine));
                painter.drawLine(first, ProjectPoint(*lineBetweenHoverPoint_));
            }
        }
        if (lineBetweenHoverPoint_.has_value()) {
            const QPointF hover = ProjectPoint(*lineBetweenHoverPoint_);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor("#d17d00"), 2.0));
            painter.drawEllipse(hover, 7.0, 7.0);
            painter.drawLine(hover + QPointF(-10.0, 0.0), hover + QPointF(10.0, 0.0));
            painter.drawLine(hover + QPointF(0.0, -10.0), hover + QPointF(0.0, 10.0));
        }
        painter.restore();
    }

    const ViewCubeGeometry cube = MakeViewCubeGeometry(width(), CurrentViewBasis());
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool navigatorActive = navigatorHot_ || viewCubeInteraction_
        || hoveredViewCubeFace_ != static_cast<int>(ViewCubeFace::None);
    painter.setOpacity(navigatorActive ? 1.0 : 0.55);
    QFont cubeFont = painter.font();
    cubeFont.setPointSizeF(std::max(7.0, cubeFont.pointSizeF() - 1.0));
    painter.setFont(cubeFont);

    // モデル軸まわりの回転リング(キューブの後ろに描く。視点に追従する)。
    const auto drawRingArrowHead = [&](QPointF tip, QPointF direction, const QColor& color, bool hovered) {
        const QPointF perpendicular(-direction.y(), direction.x());
        const double size = hovered ? 1.35 : 1.0;
        QPolygonF head;
        head << tip + direction * (8.0 * size)
             << tip - direction * (3.0 * size) + perpendicular * (5.0 * size)
             << tip - direction * (3.0 * size) - perpendicular * (5.0 * size);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(head);
    };
    for (const ViewCubeRingGeometry& ring : cube.rings) {
        const bool plusHovered = hoveredViewCubeFace_ == static_cast<int>(ring.plusFace);
        const bool minusHovered = hoveredViewCubeFace_ == static_cast<int>(ring.minusFace);
        QColor ringColor = ring.color;
        ringColor.setAlpha(plusHovered || minusHovered ? 235 : 150);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ringColor, plusHovered || minusHovered ? 2.4 : 1.6));
        painter.drawPolygon(ring.polyline);
        drawRingArrowHead(ring.plusTip, ring.plusTangent, ringColor, plusHovered);
        drawRingArrowHead(ring.minusTip, ring.minusTangent, ringColor, minusHovered);
    }

    // キューブ本体(リングの上に重ねる)。
    for (const ViewCubeFaceGeometry& face : cube.faces) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(face.face);
        QColor fill = hovered ? face.color.lighter(118) : face.color;
        fill.setAlpha(242);
        painter.setBrush(fill);
        painter.setPen(QPen(QColor("#68747c"), hovered ? 2.0 : 1.2));
        painter.drawPolygon(face.polygon);
        painter.setPen(QColor("#1f2b33"));
        if (face.polygon.boundingRect().width() >= 16.0
            && face.polygon.boundingRect().height() >= 14.0) {
            painter.drawText(face.polygon.boundingRect(), Qt::AlignCenter, face.label);
        }
    }
    // 辺(2面の中間ビュー)。ホバーで辺そのものを光らせる。
    for (const ViewCubeEdgeGeometry& edge : cube.edges) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(ViewCubeFace::Edge)
            && (edge.direction - hoveredViewCubeDirection_).LengthSquared() < 1.0e-12;
        if (hovered) {
            painter.setPen(QPen(QColor("#087780"), 3.4, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(edge.lineStart, edge.lineEnd);
        } else if (navigatorActive) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(159, 179, 186, 170));
            painter.drawEllipse(edge.area.center(), 2.4, 2.4);
        }
    }
    for (const ViewCubeCornerGeometry& corner : cube.corners) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(corner.face);
        QPolygonF diamond;
        diamond << QPointF(corner.area.center().x(), corner.area.top())
                << QPointF(corner.area.right(), corner.area.center().y())
                << QPointF(corner.area.center().x(), corner.area.bottom())
                << QPointF(corner.area.left(), corner.area.center().y());
        painter.setBrush(hovered ? QColor("#f0b64a") : QColor(247, 220, 166, 200));
        painter.setPen(QPen(QColor("#8b6a2c"), hovered ? 2.0 : 1.0));
        painter.drawPolygon(diamond);
    }
    // 面に正対中のみ: 隣の面へ90°回る三角矢印。
    for (const auto& [area, adjacentFace] : cube.adjacentArrows) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(adjacentFace);
        QPolygonF triangle;
        const QPointF center = area.center();
        if (adjacentFace == ViewCubeFace::AdjacentLeft) {
            triangle << QPointF(area.left(), center.y())
                     << QPointF(area.right(), area.top())
                     << QPointF(area.right(), area.bottom());
        } else if (adjacentFace == ViewCubeFace::AdjacentRight) {
            triangle << QPointF(area.right(), center.y())
                     << QPointF(area.left(), area.top())
                     << QPointF(area.left(), area.bottom());
        } else if (adjacentFace == ViewCubeFace::AdjacentUp) {
            triangle << QPointF(center.x(), area.top())
                     << QPointF(area.left(), area.bottom())
                     << QPointF(area.right(), area.bottom());
        } else {
            triangle << QPointF(center.x(), area.bottom())
                     << QPointF(area.left(), area.top())
                     << QPointF(area.right(), area.top());
        }
        painter.setBrush(hovered ? QColor("#c9e8e5") : QColor(237, 242, 242, 220));
        painter.setPen(QPen(hovered ? QColor("#087780") : QColor("#68747c"), hovered ? 2.0 : 1.0));
        painter.drawPolygon(triangle);
    }

    // ロール(画面基準・位置固定)。
    const auto drawRollButton = [&](const QRectF& area, ViewCubeFace face, bool clockwise) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(face);
        const QColor stroke = hovered ? QColor("#075f69") : QColor("#34434b");
        painter.setBrush(hovered ? QColor("#c9e8e5") : QColor("#edf2f2"));
        painter.setPen(QPen(hovered ? QColor("#087780") : QColor("#68747c"), hovered ? 2.0 : 1.0));
        painter.drawEllipse(area);

        painter.setBrush(stroke);
        painter.setPen(QPen(stroke, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QRectF arc = area.adjusted(5.0, 4.0, -5.0, -4.0);
        painter.drawArc(arc, clockwise ? 35 * 16 : 145 * 16, clockwise ? -235 * 16 : 235 * 16);
        QPolygonF head;
        if (clockwise) {
            head << QPointF(area.right() - 4.0, area.bottom() - 5.0)
                 << QPointF(area.right() - 9.0, area.bottom() - 5.0)
                 << QPointF(area.right() - 5.0, area.bottom() - 10.0);
        } else {
            head << QPointF(area.left() + 4.0, area.bottom() - 5.0)
                 << QPointF(area.left() + 9.0, area.bottom() - 5.0)
                 << QPointF(area.left() + 5.0, area.bottom() - 10.0);
        }
        painter.drawPolygon(head);
    };
    drawRollButton(cube.rollLeft, ViewCubeFace::RollLeft, false);
    drawRollButton(cube.rollRight, ViewCubeFace::RollRight, true);

    // 画面基準の回転矢印(位置固定)。下=左右回し、右=上下回し。
    for (const ViewCubeScreenArrowGeometry& arrow : cube.screenArrows) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(arrow.face);
        const QColor stroke = hovered ? QColor("#087780") : QColor("#34434b");
        if (hovered) {
            painter.setBrush(QColor(201, 232, 229, 180));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(arrow.area, 5.0, 5.0);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(stroke, 2.0, Qt::SolidLine, Qt::RoundCap));
        const QRectF area = arrow.area;
        QPainterPath path;
        QPointF tip;
        QPointF tipDirection;
        if (arrow.face == ViewCubeFace::RelativeYPositive) {
            // 下段・左向き(モデルの前面が左へ流れる)。
            tip = QPointF(area.left() + 4.0, area.center().y());
            tipDirection = QPointF(-1.0, 0.0);
            path.moveTo(area.right() - 4.0, area.center().y());
            path.quadTo(area.center().x(), area.center().y() + 9.0, tip.x(), tip.y());
        } else if (arrow.face == ViewCubeFace::RelativeYNegative) {
            tip = QPointF(area.right() - 4.0, area.center().y());
            tipDirection = QPointF(1.0, 0.0);
            path.moveTo(area.left() + 4.0, area.center().y());
            path.quadTo(area.center().x(), area.center().y() + 9.0, tip.x(), tip.y());
        } else if (arrow.face == ViewCubeFace::RelativeXPositive) {
            // 右列・上向き(前面が上へ流れる)。
            tip = QPointF(area.center().x(), area.top() + 4.0);
            tipDirection = QPointF(0.0, -1.0);
            path.moveTo(area.center().x(), area.bottom() - 4.0);
            path.quadTo(area.center().x() + 9.0, area.center().y(), tip.x(), tip.y());
        } else {
            tip = QPointF(area.center().x(), area.bottom() - 4.0);
            tipDirection = QPointF(0.0, 1.0);
            path.moveTo(area.center().x(), area.top() + 4.0);
            path.quadTo(area.center().x() + 9.0, area.center().y(), tip.x(), tip.y());
        }
        painter.drawPath(path);
        const QPointF perpendicular(-tipDirection.y(), tipDirection.x());
        QPolygonF head;
        head << tip + tipDirection * 6.0
             << tip - tipDirection * 2.0 + perpendicular * 4.5
             << tip - tipDirection * 2.0 - perpendicular * 4.5;
        painter.setBrush(stroke);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(head);
    }

    // ホームボタン(家アイコン)。
    {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(ViewCubeFace::Home);
        painter.setBrush(hovered ? QColor("#c9e8e5") : QColor("#f4f7f7"));
        painter.setPen(QPen(hovered ? QColor("#087780") : QColor("#8d999f"), hovered ? 2.0 : 1.0));
        painter.drawRoundedRect(cube.home, 4.0, 4.0);
        const QPointF center = cube.home.center();
        const QColor icon = hovered ? QColor("#075f69") : QColor("#3b4a52");
        QPolygonF roof;
        roof << center + QPointF(0.0, -8.0)
             << center + QPointF(-8.0, -1.0)
             << center + QPointF(8.0, -1.0);
        painter.setBrush(icon);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(roof);
        painter.drawRect(QRectF(center.x() - 5.5, center.y() - 1.0, 11.0, 8.0));
        painter.setBrush(hovered ? QColor("#c9e8e5") : QColor("#f4f7f7"));
        painter.drawRect(QRectF(center.x() - 1.8, center.y() + 2.2, 3.6, 4.8));
    }

    // 選択に正対。
    const bool canAlignSelection = project_ != nullptr && selection_.kind != CadSelectionKind::None;
    const bool selectionHovered = hoveredViewCubeFace_ == static_cast<int>(ViewCubeFace::Selection);
    painter.setBrush(canAlignSelection
            ? (selectionHovered ? QColor("#c9e8e5") : QColor("#e3f1ef"))
            : QColor("#eceff0"));
    painter.setPen(QPen(
        canAlignSelection ? QColor("#39777a") : QColor("#aeb7bc"),
        selectionHovered ? 2.0 : 1.0));
    painter.drawRoundedRect(cube.selection, 3.0, 3.0);
    const QPointF targetCenter(cube.selection.left() + 14.0, cube.selection.center().y());
    painter.drawEllipse(targetCenter, 6.0, 6.0);
    painter.drawLine(targetCenter + QPointF(-9.0, 0.0), targetCenter + QPointF(9.0, 0.0));
    painter.drawLine(targetCenter + QPointF(0.0, -9.0), targetCenter + QPointF(0.0, 9.0));
    painter.setPen(canAlignSelection ? QColor("#174d50") : QColor("#8c969b"));
    painter.drawText(cube.selection.adjusted(27.0, 0.0, -4.0, 0.0), Qt::AlignCenter, QStringLiteral("選択に正対"));
    painter.restore();
}

CadSelection CadViewport::HitTestWire(QPointF position, double maximumDistance) const
{
    if (project_ == nullptr || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return {};
    }

    double bestDistance = maximumDistance;
    CadSelection best;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const auto& namedWire = project_->Wires()[index];
        if (!ShouldDisplay(CadSelectionKind::Wire, index, namedWire.visible)) {
            continue;
        }
        const auto& wire = namedWire.wire;
        QPointF previous = ProjectPoint(wire.Evaluate(0.0));
        const int samples = wire.Kind() == WireKind::Line ? 1 : 48;
        for (int sample = 1; sample <= samples; ++sample) {
            const QPointF current = ProjectPoint(wire.Evaluate(static_cast<double>(sample) / samples));
            const double distance = DistanceToSegment(position, previous, current);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {CadSelectionKind::Wire, index};
            }
            previous = current;
        }
    }
    return best;
}

CadSelection CadViewport::HitTest(QPointF position) const
{
    if (project_ == nullptr) {
        return {};
    }

    double pointDistance = 7.0;
    CadSelection pointSelection;
    for (int index = 0; index < static_cast<int>(project_->Points().size()); ++index) {
        const auto& point = project_->Points()[index];
        if (!ShouldDisplay(CadSelectionKind::Point, index, point.visible)) {
            continue;
        }
        const double distance = QLineF(position, ProjectPoint(point.point)).length();
        if (distance < pointDistance) {
            pointDistance = distance;
            pointSelection = {CadSelectionKind::Point, index};
        }
    }
    if (pointSelection.kind == CadSelectionKind::Point) {
        return pointSelection;
    }

    const CadSelection wire = HitTestWire(position);
    if (wire.kind != CadSelectionKind::None) {
        return wire;
    }

    for (int index = 0; index < static_cast<int>(project_->Bodies().size()); ++index) {
        const auto& namedBody = project_->Bodies()[index];
        if (!ShouldDisplay(CadSelectionKind::Body, index, namedBody.visible)) {
            continue;
        }
        const auto& body = namedBody.body;
        for (int uIndex = 0; uIndex < 24; ++uIndex) {
            for (int vIndex = 0; vIndex < 8; ++vIndex) {
                const double u0 = static_cast<double>(uIndex) / 24.0;
                const double u1 = static_cast<double>(uIndex + 1) / 24.0;
                const double v0 = static_cast<double>(vIndex) / 8.0;
                const double v1 = static_cast<double>(vIndex + 1) / 8.0;
                QPolygonF patch;
                patch << ProjectPoint(body.Evaluate(u0, v0, 1.0))
                      << ProjectPoint(body.Evaluate(u1, v0, 1.0))
                      << ProjectPoint(body.Evaluate(u1, v1, 1.0))
                      << ProjectPoint(body.Evaluate(u0, v1, 1.0));
                if (patch.containsPoint(position, Qt::OddEvenFill)) {
                    return {CadSelectionKind::Body, index};
                }
            }
        }
    }

    for (int index = 0; index < static_cast<int>(project_->Plates().size()); ++index) {
        const auto& namedPlate = project_->Plates()[index];
        if (!ShouldDisplay(CadSelectionKind::Plate, index, namedPlate.visible)) {
            continue;
        }
        const auto& plate = namedPlate.plate;
        const auto& surface = plate.SourceSurface();
        bool insideOpening = false;
        for (const std::string& openingName : namedPlate.openingWireNames) {
            const auto opening = std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const auto& wire) {
                return wire.name == openingName;
            });
            if (opening == project_->Wires().end()) {
                continue;
            }
            QPolygonF openingPolygon;
            for (int sample = 0; sample < 128; ++sample) {
                openingPolygon << ProjectPoint(opening->wire.Evaluate(static_cast<double>(sample) / 128.0));
            }
            if (openingPolygon.containsPoint(position, Qt::OddEvenFill)) {
                insideOpening = true;
                break;
            }
        }
        if (insideOpening) {
            continue;
        }
        if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
            const Vector3 normal = surface.Normal(0.5, 0.5);
            QPolygonF polygon;
            for (int sample = 0; sample < 128; ++sample) {
                polygon << ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0)
                    + normal * plate.MaximumOffset());
            }
            if (polygon.containsPoint(position, Qt::OddEvenFill)) {
                return {CadSelectionKind::Plate, index};
            }
            continue;
        }
        for (int uIndex = 0; uIndex < 24; ++uIndex) {
            for (int vIndex = 0; vIndex < 8; ++vIndex) {
                const double u0 = static_cast<double>(uIndex) / 24.0;
                const double u1 = static_cast<double>(uIndex + 1) / 24.0;
                const double v0 = static_cast<double>(vIndex) / 8.0;
                const double v1 = static_cast<double>(vIndex + 1) / 8.0;
                QPolygonF patch;
                patch << ProjectPoint(plate.Evaluate(u0, v0, 1.0))
                      << ProjectPoint(plate.Evaluate(u1, v0, 1.0))
                      << ProjectPoint(plate.Evaluate(u1, v1, 1.0))
                      << ProjectPoint(plate.Evaluate(u0, v1, 1.0));
                if (patch.containsPoint(position, Qt::OddEvenFill)) {
                    return {CadSelectionKind::Plate, index};
                }
            }
        }
    }

    for (int index = 0; index < static_cast<int>(project_->Surfaces().size()); ++index) {
        const auto& namedSurface = project_->Surfaces()[index];
        if (!ShouldDisplay(CadSelectionKind::Surface, index, namedSurface.visible)) {
            continue;
        }
        const auto& surface = namedSurface.surface;
        if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
            QPolygonF polygon;
            for (int sample = 0; sample < 128; ++sample) {
                polygon << ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0));
            }
            if (polygon.containsPoint(position, Qt::OddEvenFill)) {
                return {CadSelectionKind::Surface, index};
            }
            continue;
        }
        for (int uIndex = 0; uIndex < 24; ++uIndex) {
            for (int vIndex = 0; vIndex < 8; ++vIndex) {
                const double u0 = static_cast<double>(uIndex) / 24.0;
                const double u1 = static_cast<double>(uIndex + 1) / 24.0;
                const double v0 = static_cast<double>(vIndex) / 8.0;
                const double v1 = static_cast<double>(vIndex + 1) / 8.0;
                QPolygonF patch;
                patch << ProjectPoint(surface.Evaluate(u0, v0))
                      << ProjectPoint(surface.Evaluate(u1, v0))
                      << ProjectPoint(surface.Evaluate(u1, v1))
                      << ProjectPoint(surface.Evaluate(u0, v1));
                if (patch.containsPoint(position, Qt::OddEvenFill)) {
                    return {CadSelectionKind::Surface, index};
                }
            }
        }
    }

    for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
        const auto& namedPlane = project_->WorkPlanes()[index];
        if (!ShouldDisplay(CadSelectionKind::WorkPlane, index, namedPlane.visible)) {
            continue;
        }
        const auto& plane = namedPlane.plane;
        const std::array<QPointF, 4> corners = {
            ProjectPoint(plane.ToWorld(-kPlaneHalfSize, -kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(kPlaneHalfSize, -kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(kPlaneHalfSize, kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(-kPlaneHalfSize, kPlaneHalfSize)),
        };
        for (int edge = 0; edge < 4; ++edge) {
            if (DistanceToSegment(position, corners[edge], corners[(edge + 1) % 4]) < 7.0) {
                return {CadSelectionKind::WorkPlane, index};
            }
        }
    }
    return {};
}

void CadViewport::mousePressEvent(QMouseEvent* event)
{
    mousePressPosition_ = event->position().toPoint();
    lastMousePosition_ = event->position().toPoint();
    dragButton_ = event->button();
    mouseMoved_ = false;
    orbitInteraction_ = event->button() == Qt::MiddleButton
        && event->modifiers().testFlag(Qt::ShiftModifier);
    setFocus();
    const ViewCubeHit pressedCubeHit = event->button() == Qt::LeftButton
        ? HitViewCube(MakeViewCubeGeometry(width(), CurrentViewBasis()), event->position())
        : ViewCubeHit{};
    if (pressedCubeHit.face != ViewCubeFace::None) {
        viewCubeInteraction_ = true;
        viewCubeDragActive_ = false;
        pressedViewCubeFace_ = static_cast<int>(pressedCubeHit.face);
        pressedViewCubeDirection_ = pressedCubeHit.direction;
        const auto pressedRotation = ViewCubeRotation(pressedCubeHit.face);
        setCursor(CanDragViewCube(pressedCubeHit.face) || pressedRotation.has_value()
                ? Qt::OpenHandCursor : Qt::PointingHandCursor);
        event->accept();
        return;
    }
    if (orbitInteraction_) {
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton
        && (tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire)) {
        UpdateHover(event->position());
        UpdateDirectLineEditPreview();
    }
    if (event->button() == Qt::LeftButton && tool_ == ViewportTool::MoveGridOrigin
        && activePlane_.has_value() && gridPointsVisible_) {
        const auto point = PointOnActivePlane(event->position());
        if (point.has_value()) {
            const auto coordinates = activePlane_->Project(*point);
            const double step = snapStep_ / static_cast<double>(gridSubdivision_);
            const double gridU = gridOriginU_
                + std::round((coordinates.u - gridOriginU_) / step) * step;
            const double gridV = gridOriginV_
                + std::round((coordinates.v - gridOriginV_) / step) * step;
            const Vector3 gridPoint = activePlane_->ToWorld(gridU, gridV);
            if (QLineF(event->position(), ProjectPoint(gridPoint)).length() <= 14.0) {
                gridOriginDragSource_ = gridPoint;
                gridOriginDragTarget_ = gridPoint;
                gridOriginDragBaseU_ = gridOriginU_;
                gridOriginDragBaseV_ = gridOriginV_;
                setCursor(Qt::ClosedHandCursor);
                update();
                event->accept();
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton && tool_ == ViewportTool::Select) {
        const auto controlPoint = NearestEditableControlPoint(event->position(), 9.0);
        if (controlPoint.has_value()) {
            const NamedWire& namedWire = project_->Wires()[controlPoint->wireIndex];
            const Vector3 point = namedWire.wire.ControlPoints()[controlPoint->controlPointIndex];
            std::optional<kachakacha::model::WorkPlane> sourcePlane;
            if (namedWire.metadata.sourcePlaneName.has_value()) {
                sourcePlane = project_->FindWorkPlane(*namedWire.metadata.sourcePlaneName);
            }

            draggedControlPoint_ = controlPoint;
            draggedWirePreview_ = namedWire.wire;
            if (namedWire.metadata.planePolicy == kachakacha::model::WirePlanePolicy::LockedToPlane
                && sourcePlane.has_value()) {
                controlPointDragPlane_ = sourcePlane;
            } else {
                const auto basis = CurrentViewBasis();
                controlPointDragPlane_ = kachakacha::model::WorkPlane::FromPointNormal(
                    point, basis[0], basis[1]);
            }
            controlPointSnapPlane_ = sourcePlane.has_value()
                ? sourcePlane
                : activePlane_.has_value() ? activePlane_ : controlPointDragPlane_;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    if (tool_ != ViewportTool::Select
        && tool_ != ViewportTool::SplitWire
        && tool_ != ViewportTool::TrimWire
        && tool_ != ViewportTool::ExtendWire
        && tool_ != ViewportTool::Measure
        && tool_ != ViewportTool::Coincident
        && tool_ != ViewportTool::Tangent
        && tool_ != ViewportTool::Curvature
        && tool_ != ViewportTool::MoveGridOrigin
        && tool_ != ViewportTool::DrawPoint
        && event->button() == Qt::LeftButton
        && activePlane_.has_value()
        && drawingPoints_.empty()) {
        const auto point = PointOnActivePlane(event->position());
        if (point.has_value()) {
            CommitDrawingPoint(SnapPoint(*point, event->position(), event->modifiers()));
        }
    }
}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (viewCubeInteraction_ && dragButton_ == Qt::LeftButton) {
        const ViewCubeFace pressedFace = static_cast<ViewCubeFace>(pressedViewCubeFace_);
        // リング(モデル軸まわり)はドラッグで連続回転する。マウスの角度変化に
        // 追従させる: カメラ回転 = -sign(Dot(軸, 視線)) × 画面上の角度変化。
        if (const auto ringRotation = ViewCubeRotation(pressedFace);
            ringRotation.has_value() && ringRotation->relative) {
            // 画面固定矢印も「つかんで回す」: 下段はマウスの左右、右列は上下に追従する。
            const QPoint totalDelta = event->position().toPoint() - mousePressPosition_;
            if (!viewCubeDragActive_ && std::hypot(totalDelta.x(), totalDelta.y()) <= 3.0) {
                event->accept();
                return;
            }
            viewCubeDragActive_ = true;
            mouseMoved_ = true;
            setCursor(Qt::ClosedHandCursor);
            const QPoint delta = event->position().toPoint() - lastMousePosition_;
            if (ringRotation->axis == ViewRotationAxis::Y) {
                RotateViewAroundRelativeAxis(ViewRotationAxis::Y, -delta.x() * 0.008);
            } else {
                RotateViewAroundRelativeAxis(ViewRotationAxis::X, -delta.y() * 0.008);
            }
            lastMousePosition_ = event->position().toPoint();
            event->accept();
            return;
        }
        if (const auto ringRotation = ViewCubeRotation(pressedFace);
            ringRotation.has_value() && !ringRotation->relative) {
            const QPoint totalDelta = event->position().toPoint() - mousePressPosition_;
            if (!viewCubeDragActive_ && std::hypot(totalDelta.x(), totalDelta.y()) <= 3.0) {
                event->accept();
                return;
            }
            viewCubeDragActive_ = true;
            mouseMoved_ = true;
            setCursor(Qt::ClosedHandCursor);
            const QPointF ringCenter(
                width() - kViewCubeCenterFromRight, kViewCubeCenterY);
            const QPointF previous = QPointF(lastMousePosition_) - ringCenter;
            const QPointF current = event->position() - ringCenter;
            if (std::hypot(previous.x(), previous.y()) > 4.0
                && std::hypot(current.x(), current.y()) > 4.0) {
                const double angleDelta = std::atan2(-current.y(), current.x())
                    - std::atan2(-previous.y(), previous.x());
                double wrapped = angleDelta;
                while (wrapped > std::numbers::pi) {
                    wrapped -= 2.0 * std::numbers::pi;
                }
                while (wrapped < -std::numbers::pi) {
                    wrapped += 2.0 * std::numbers::pi;
                }
                const Vector3 axis = ringRotation->axis == ViewRotationAxis::X
                    ? Vector3{1.0, 0.0, 0.0}
                    : ringRotation->axis == ViewRotationAxis::Y
                    ? Vector3{0.0, 1.0, 0.0}
                    : Vector3{0.0, 0.0, 1.0};
                const double orientation =
                    Dot(axis, CurrentViewBasis()[0]) >= 0.0 ? 1.0 : -1.0;
                RotateViewAroundWorldAxis(ringRotation->axis, -orientation * wrapped);
            }
            lastMousePosition_ = event->position().toPoint();
            event->accept();
            return;
        }
        if (!CanDragViewCube(pressedFace)) {
            event->accept();
            return;
        }
        if (!viewCubeDragActive_) {
            const QPoint totalDelta = event->position().toPoint() - mousePressPosition_;
            // クリック判定(リリース側)と同じ3pxを境にする。閾値がずれていると
            // 「クリックともドラッグとも扱われない不感帯」が生まれる。
            if (std::hypot(totalDelta.x(), totalDelta.y()) <= 3.0) {
                event->accept();
                return;
            }
            viewCubeDragActive_ = true;
            mouseMoved_ = true;
            setCursor(Qt::ClosedHandCursor);
        }
        const QPoint delta = event->position().toPoint() - lastMousePosition_;
        // キューブは「つかんで回す」向き: 上へドラッグ=キューブの上面が奥へ倒れる。
        // キャンバスのオービット(カメラを動かす向き)とは縦方向が逆になる。
        OrbitViewByPixels(delta.x(), -delta.y());
        lastMousePosition_ = event->position().toPoint();
        update();
        event->accept();
        return;
    }

    if (draggedControlPoint_.has_value() && dragButton_ == Qt::LeftButton
        && controlPointDragPlane_.has_value() && project_ != nullptr) {
        const QPoint delta = event->position().toPoint() - lastMousePosition_;
        if (delta.manhattanLength() > 1) {
            mouseMoved_ = true;
        }
        const auto point = PointOnPlane(event->position(), *controlPointDragPlane_);
        if (point.has_value()) {
            try {
                const NamedWire& source = project_->Wires()[draggedControlPoint_->wireIndex];
                draggedWirePreview_ = source.wire.WithMovedControlPoint(
                    draggedControlPoint_->controlPointIndex,
                    SnapDraggedControlPoint(*point, event->position()));
            } catch (const std::exception&) {
                draggedWirePreview_.reset();
            }
        }
        lastMousePosition_ = event->position().toPoint();
        update();
        event->accept();
        return;
    }

    if (tool_ == ViewportTool::MoveGridOrigin && dragButton_ == Qt::LeftButton
        && gridOriginDragSource_.has_value() && activePlane_.has_value()) {
        const QPoint totalDelta = event->position().toPoint() - mousePressPosition_;
        if (std::hypot(totalDelta.x(), totalDelta.y()) > 1.5) {
            mouseMoved_ = true;
        }
        const auto point = PointOnActivePlane(event->position());
        if (point.has_value()) {
            gridOriginDragTarget_ = SnapGridAlignmentTarget(*point, event->position());
        }
        lastMousePosition_ = event->position().toPoint();
        update();
        event->accept();
        return;
    }

    const ViewCubeGeometry hoverCube = MakeViewCubeGeometry(width(), CurrentViewBasis());
    const ViewCubeHit hoveredHit = HitViewCube(hoverCube, event->position());
    const int hoveredFace = static_cast<int>(hoveredHit.face);
    const bool navigatorHot =
        hoverCube.bounds.adjusted(-26.0, -26.0, 26.0, 26.0).contains(event->position());
    if (navigatorHot != navigatorHot_) {
        navigatorHot_ = navigatorHot;
        update();
    }
    if (hoveredFace != hoveredViewCubeFace_
        || (hoveredHit.direction - hoveredViewCubeDirection_).LengthSquared() > 1.0e-12) {
        hoveredViewCubeFace_ = hoveredFace;
        hoveredViewCubeDirection_ = hoveredHit.direction;
        const bool unavailableSelection = hoveredFace == static_cast<int>(ViewCubeFace::Selection)
            && selection_.kind == CadSelectionKind::None;
        const ViewCubeFace hoveredCubeFace = static_cast<ViewCubeFace>(hoveredFace);
        setCursor(hoveredCubeFace == ViewCubeFace::None
                ? (tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor)
                : unavailableSelection ? Qt::ForbiddenCursor
                : CanDragViewCube(hoveredCubeFace) ? Qt::OpenHandCursor
                : Qt::PointingHandCursor);
        QString tooltip;
        if (const auto rotation = ViewCubeRotation(hoveredCubeFace); rotation.has_value()) {
            tooltip = rotation->relative
                ? QStringLiteral("画面基準で回転: クリック15°（Shiftで5°）、つかんでドラッグで連続")
                : QStringLiteral("モデルの%1軸まわりに回転: クリック15°（Shiftで5°）、ドラッグで連続")
                      .arg(RotationAxisName(rotation->axis));
        } else {
            switch (hoveredCubeFace) {
        case ViewCubeFace::Top:
            tooltip = QStringLiteral("上面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Bottom:
            tooltip = QStringLiteral("下面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Front:
            tooltip = QStringLiteral("前面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Back:
            tooltip = QStringLiteral("後面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Right:
            tooltip = QStringLiteral("右面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Left:
            tooltip = QStringLiteral("左面へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::Home:
            tooltip = QStringLiteral("ホーム（標準の3D表示へ戻す）");
            break;
        case ViewCubeFace::Selection:
            tooltip = unavailableSelection
                ? QStringLiteral("線・面・板材・作業平面を先に選択")
                : QStringLiteral("選択対象に正対して中央表示");
            break;
        case ViewCubeFace::Edge:
            tooltip = QStringLiteral("この辺の方向へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::AdjacentLeft:
        case ViewCubeFace::AdjacentRight:
        case ViewCubeFace::AdjacentUp:
        case ViewCubeFace::AdjacentDown:
            tooltip = QStringLiteral("隣の面へ90°回転");
            break;
        case ViewCubeFace::CornerPositivePositivePositive:
        case ViewCubeFace::CornerPositivePositiveNegative:
        case ViewCubeFace::CornerPositiveNegativePositive:
        case ViewCubeFace::CornerPositiveNegativeNegative:
        case ViewCubeFace::CornerNegativePositivePositive:
        case ViewCubeFace::CornerNegativePositiveNegative:
        case ViewCubeFace::CornerNegativeNegativePositive:
        case ViewCubeFace::CornerNegativeNegativeNegative:
            tooltip = QStringLiteral("角へ正対。ドラッグで自由回転");
            break;
        case ViewCubeFace::RollLeft:
            tooltip = QStringLiteral("画面上で左へ15°ロール（Shiftで5°）");
            break;
        case ViewCubeFace::RollRight:
            tooltip = QStringLiteral("画面上で右へ15°ロール（Shiftで5°）");
            break;
        case ViewCubeFace::None:
            break;
        default:
            break;
            }
        }
        setToolTip(tooltip);
        update();
    }

    if (dragButton_ == Qt::NoButton
        && hoveredFace == static_cast<int>(ViewCubeFace::None)) {
        UpdateHover(event->position());
    } else if (hoveredFace != static_cast<int>(ViewCubeFace::None)) {
        ClearHover();
    }

    if (tool_ == ViewportTool::LineBetweenPoints && dragButton_ == Qt::NoButton) {
        lineBetweenHoverPoint_ = FindConnectablePoint(event->position(), 14.0);
        update();
    }
    if (tool_ == ViewportTool::SplitWire) {
        splitPreviewParameter_.reset();
        const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
            return selection.kind == CadSelectionKind::Wire;
        });
        if (selectedWire != selections_.end()) {
            splitPreviewParameter_ = NearestWireParameter(selectedWire->index, event->position());
        }
        update();
    } else if (tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire) {
        UpdateDirectLineEditPreview();
    } else if (tool_ != ViewportTool::Select
        && tool_ != ViewportTool::Measure
        && tool_ != ViewportTool::Coincident
        && tool_ != ViewportTool::Tangent
        && tool_ != ViewportTool::Curvature
        && tool_ != ViewportTool::MoveGridOrigin
        && activePlane_.has_value()) {
        const auto point = PointOnActivePlane(event->position());
        hoverDrawingPoint_ = point.has_value()
            ? std::optional<Vector3>(ApplyDrawingConstraint(
                  SnapPoint(*point, event->position(), event->modifiers()), event->modifiers()))
            : std::nullopt;
        UpdateDynamicDimensionEditor();
        NotifyDrawingState();
        update();
    }

    if (dragButton_ == Qt::NoButton) {
        return;
    }
    const QPoint delta = event->position().toPoint() - lastMousePosition_;
    if (delta.manhattanLength() > 1) {
        mouseMoved_ = true;
    }
    if (orbitInteraction_) {
        OrbitViewByPixels(delta.x(), delta.y());
    } else if (dragButton_ == Qt::RightButton || dragButton_ == Qt::MiddleButton) {
        const auto basis = CurrentViewBasis();
        const Vector3& right = basis[1];
        const Vector3& up = basis[2];
        target_ = target_ - right * (delta.x() / pixelsPerMillimeter_) + up * (delta.y() / pixelsPerMillimeter_);
    }
    lastMousePosition_ = event->position().toPoint();
    update();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (viewCubeInteraction_) {
        const QPoint totalDelta = event->position().toPoint() - mousePressPosition_;
        const double totalDistance = std::hypot(totalDelta.x(), totalDelta.y());
        if (event->button() == Qt::LeftButton && !viewCubeDragActive_
            && totalDistance <= 3.0) {
            const ViewCubeFace face = static_cast<ViewCubeFace>(pressedViewCubeFace_);
            const ViewCubeHit releasedHit = HitViewCube(
                MakeViewCubeGeometry(width(), CurrentViewBasis()), event->position());
            if (releasedHit.face == face) {
                const double stepRadians = (event->modifiers().testFlag(Qt::ShiftModifier) ? 5.0 : 15.0)
                    * std::numbers::pi / 180.0;
                const auto basis = CurrentViewBasis();
                const auto snapToAxis = [](Vector3 value) {
                    const double absX = std::abs(value.x);
                    const double absY = std::abs(value.y);
                    const double absZ = std::abs(value.z);
                    if (absX >= absY && absX >= absZ) {
                        return Vector3{value.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0};
                    }
                    if (absY >= absZ) {
                        return Vector3{0.0, value.y >= 0.0 ? 1.0 : -1.0, 0.0};
                    }
                    return Vector3{0.0, 0.0, value.z >= 0.0 ? 1.0 : -1.0};
                };
                if (face == ViewCubeFace::Edge) {
                    SetDirectionView(pressedViewCubeDirection_);
                } else if (const auto direction = ViewCubeDirection(face); direction.has_value()) {
                    SetDirectionView(*direction);
                } else if (const auto rotation = ViewCubeRotation(face); rotation.has_value()) {
                    if (rotation->relative) {
                        RotateViewAroundRelativeAxis(
                            rotation->axis, rotation->direction * stepRadians);
                    } else {
                        RotateViewAroundWorldAxis(
                            rotation->axis, rotation->direction * stepRadians);
                    }
                } else {
                    switch (face) {
                    case ViewCubeFace::Home:
                        AnimateViewTo(IsometricViewBasis());
                        break;
                    case ViewCubeFace::Selection:
                        (void)AlignToSelection();
                        break;
                    case ViewCubeFace::AdjacentLeft:
                        SetDirectionView(snapToAxis(-basis[1]));
                        break;
                    case ViewCubeFace::AdjacentRight:
                        SetDirectionView(snapToAxis(basis[1]));
                        break;
                    case ViewCubeFace::AdjacentUp:
                        SetDirectionView(snapToAxis(basis[2]));
                        break;
                    case ViewCubeFace::AdjacentDown:
                        SetDirectionView(snapToAxis(-basis[2]));
                        break;
                    case ViewCubeFace::RollLeft:
                        RollView(-stepRadians);
                        break;
                    case ViewCubeFace::RollRight:
                        RollView(stepRadians);
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        viewCubeInteraction_ = false;
        viewCubeDragActive_ = false;
        pressedViewCubeFace_ = static_cast<int>(ViewCubeFace::None);
        dragButton_ = Qt::NoButton;
        const ViewCubeFace hoveredFace = static_cast<ViewCubeFace>(hoveredViewCubeFace_);
        setCursor(hoveredFace == ViewCubeFace::None
                ? (tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor)
                : CanDragViewCube(hoveredFace) ? Qt::OpenHandCursor
                : Qt::PointingHandCursor);
        event->accept();
        return;
    }

    if (orbitInteraction_) {
        setCursor(tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    }
    orbitInteraction_ = false;

    if (tool_ == ViewportTool::MoveGridOrigin && event->button() == Qt::LeftButton) {
        if (gridOriginDragSource_.has_value() && gridOriginDragTarget_.has_value()
            && activePlane_.has_value() && mouseMoved_) {
            const auto source = activePlane_->Project(*gridOriginDragSource_);
            const auto target = activePlane_->Project(*gridOriginDragTarget_);
            const double newU = gridOriginDragBaseU_ + target.u - source.u;
            const double newV = gridOriginDragBaseV_ + target.v - source.v;
            gridOriginDragSource_.reset();
            gridOriginDragTarget_.reset();
            SetGridOrigin(newU, newV);
            if (gridOriginChanged_) {
                gridOriginChanged_(newU, newV);
            }
        } else {
            gridOriginDragSource_.reset();
            gridOriginDragTarget_.reset();
            update();
        }
        dragButton_ = Qt::NoButton;
        setCursor(tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }

    if (draggedControlPoint_.has_value()) {
        const WireControlPointPick pick = *draggedControlPoint_;
        const std::optional<Wire> preview = draggedWirePreview_;
        const bool changed = event->button() == Qt::LeftButton && mouseMoved_
            && preview.has_value() && project_ != nullptr
            && pick.wireIndex >= 0 && pick.wireIndex < static_cast<int>(project_->Wires().size())
            && !AlmostEqual(
                preview->ControlPoints()[pick.controlPointIndex],
                project_->Wires()[pick.wireIndex].wire.ControlPoints()[pick.controlPointIndex],
                1.0e-9);
        CancelControlPointDrag();
        dragButton_ = Qt::NoButton;
        setCursor(Qt::SizeAllCursor);
        if (changed && wireControlPointMoved_) {
            wireControlPointMoved_(pick.wireIndex, *preview);
        }
        UpdateHover(event->position());
        event->accept();
        return;
    }

    if (tool_ == ViewportTool::Measure) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            CommitMeasurementPick(event->position());
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            ClearMeasurement();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ == ViewportTool::LineBetweenPoints) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            const auto picked = FindConnectablePoint(event->position(), 14.0);
            if (picked.has_value()) {
                if (!lineBetweenFirstPoint_.has_value()) {
                    lineBetweenFirstPoint_ = *picked;
                } else if ((*picked - *lineBetweenFirstPoint_).Length() > 1.0e-9) {
                    const Vector3 firstPoint = *lineBetweenFirstPoint_;
                    lineBetweenFirstPoint_.reset();
                    if (lineBetweenPicked_) {
                        lineBetweenPicked_(firstPoint, *picked);
                    }
                }
                update();
            }
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            lineBetweenFirstPoint_.reset();
            update();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
        || tool_ == ViewportTool::Curvature) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            CommitCoincidencePick(event->position());
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            ClearCoincidencePicks();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire) {
        if (event->button() == Qt::LeftButton && !mouseMoved_
            && hoveredSelection_.kind == CadSelectionKind::Wire
            && hoveredWireParameter_.has_value()) {
            const int wireIndex = hoveredSelection_.index;
            const double parameter = *hoveredWireParameter_;
            if (tool_ == ViewportTool::TrimWire && trimRequested_) {
                trimRequested_(wireIndex, parameter);
            } else if (tool_ == ViewportTool::ExtendWire && extendRequested_) {
                extendRequested_(wireIndex, parameter);
            }
            directLineEditPreviewWire_.reset();
            directLineEditPreviewIntersection_.reset();
        } else if (event->button() == Qt::RightButton && !mouseMoved_
            && toolExitRequested_) {
            toolExitRequested_();
        }
        dragButton_ = Qt::NoButton;
        event->accept();
        return;
    }

    if (tool_ == ViewportTool::SplitWire) {
        if (event->button() == Qt::LeftButton && !mouseMoved_ && splitPreviewParameter_.has_value()) {
            const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
                return selection.kind == CadSelectionKind::Wire;
            });
            if (selectedWire != selections_.end() && splitRequested_) {
                splitRequested_(selectedWire->index, *splitPreviewParameter_);
            }
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            CancelDrawing();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ != ViewportTool::Select) {
        if (event->button() == Qt::LeftButton && activePlane_.has_value()) {
            const auto point = PointOnActivePlane(event->position());
            if (point.has_value()) {
                CommitDrawingPoint(ApplyDrawingConstraint(
                    SnapPoint(*point, event->position(), event->modifiers()), event->modifiers()));
            }
        } else if (!mouseMoved_ && event->button() == Qt::RightButton) {
            const bool canStartFromNearbyPoint = drawingPoints_.empty()
                && (tool_ == ViewportTool::DrawLine
                    || tool_ == ViewportTool::DrawPolyline
                    || tool_ == ViewportTool::DrawRectangle
                    || tool_ == ViewportTool::DrawCircle
                    || tool_ == ViewportTool::DrawArc
                    || tool_ == ViewportTool::DrawBezier
                    || tool_ == ViewportTool::DrawSpline)
                && !event->modifiers().testFlag(Qt::ControlModifier)
                && activePlane_.has_value();
            if (canStartFromNearbyPoint) {
                const auto point = PointOnActivePlane(event->position());
                const auto candidate = point.has_value()
                    ? FindDrawingSnap(*point, event->position(), true)
                    : std::nullopt;
                if (candidate.has_value()) {
                    drawingSnapHover_ = candidate;
                    CommitDrawingPoint(candidate->point);
                    dragButton_ = Qt::NoButton;
                    event->accept();
                    return;
                }
            }
            if ((tool_ == ViewportTool::DrawPolyline && drawingPoints_.size() >= 2)
                || (tool_ == ViewportTool::DrawSpline && drawingPoints_.size() >= 4)) {
                FinishDrawing();
            } else {
                CancelDrawing();
            }
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (event->button() == Qt::LeftButton && !mouseMoved_) {
        const CadSelection hit = HitTest(event->position());
        const bool extendSelection = event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::ShiftModifier);
        if (!extendSelection) {
            selections_.clear();
            if (hit.kind != CadSelectionKind::None) {
                selections_.push_back(hit);
            }
        } else if (hit.kind != CadSelectionKind::None) {
            const auto existing = std::find_if(selections_.begin(), selections_.end(), [&](const CadSelection& selection) {
                return selection.kind == hit.kind && selection.index == hit.index;
            });
            if (existing == selections_.end()) {
                selections_.push_back(hit);
            } else {
                selections_.erase(existing);
            }
        }
        selection_ = selections_.empty() ? CadSelection{} : selections_.back();
        NotifySelection();
        update();
    }
    dragButton_ = Qt::NoButton;
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
    const QString text = event->text();
    const bool startsExpression = text.size() == 1
        && QStringLiteral("0123456789.+-(pP").contains(text.front());
    if (startsExpression && HasDynamicDimensions() && !drawingPoints_.empty()) {
        BeginDynamicDimensionInput(text);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (gridOriginDragSource_.has_value()) {
            gridOriginDragSource_.reset();
            gridOriginDragTarget_.reset();
            dragButton_ = Qt::NoButton;
            setCursor(Qt::CrossCursor);
            update();
        } else if (draggedControlPoint_.has_value()) {
            CancelControlPointDrag();
            dragButton_ = Qt::NoButton;
            UpdateHover(mapFromGlobal(QCursor::pos()));
        } else if (tool_ == ViewportTool::Measure) {
            ClearMeasurement();
        } else if (tool_ == ViewportTool::LineBetweenPoints) {
            lineBetweenFirstPoint_.reset();
        } else if (tool_ == ViewportTool::Coincident) {
            ClearCoincidencePicks();
        } else if (tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature) {
            ClearCoincidencePicks();
        } else if (tool_ == ViewportTool::TrimWire || tool_ == ViewportTool::ExtendWire) {
            if (toolExitRequested_) {
                toolExitRequested_();
            }
        } else {
            CancelDrawing();
        }
        // 他CAD同様、Escは常に「選択モード・何も選ばれていない」状態で終わる。
        if (escapeRequested_) {
            escapeRequested_();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        FinishDrawing();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool CadViewport::eventFilter(QObject* watched, QEvent* event)
{
    const bool isPrimary = watched == dynamicPrimaryField_;
    const bool isSecondary = watched == dynamicSecondaryField_;
    if ((isPrimary || isSecondary) && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        SetDynamicDimensionFieldError(static_cast<QLineEdit*>(watched), false);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            CommitDynamicDimensionInput();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            static_cast<QLineEdit*>(watched)->clearFocus();
            setFocus(Qt::OtherFocusReason);
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            const bool rectangle = tool_ == ViewportTool::DrawRectangle;
            const bool positiveOnly = isPrimary || rectangle;
            if (!ValidateDynamicDimensionField(static_cast<QLineEdit*>(watched), positiveOnly)) {
                keyEvent->accept();
                return true;
            }
            QLineEdit* target = isPrimary && dynamicSecondaryField_->isVisible()
                ? dynamicSecondaryField_
                : dynamicPrimaryField_;
            target->setFocus(Qt::TabFocusReason);
            target->selectAll();
            keyEvent->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CadViewport::contextMenuEvent(QContextMenuEvent* event)
{
    // 選択ツール中だけ右クリックメニューを出す。作図ツール中の右クリックは
    // 「近くの点から引き始める」操作に割り当て済みのため出さない。
    // 右ドラッグ(軌道回転)の後にも出さない。
    if (tool_ == ViewportTool::Select && !mouseMoved_ && onSelectContextMenu) {
        onSelectContextMenu(event->globalPos());
        event->accept();
        return;
    }
    event->ignore();
}

void CadViewport::leaveEvent(QEvent* event)
{
    ClearHover();
    QWidget::leaveEvent(event);
}

void CadViewport::wheelEvent(QWheelEvent* event)
{
    const double factor = std::pow(1.0015, event->angleDelta().y());
    pixelsPerMillimeter_ = std::clamp(pixelsPerMillimeter_ * factor, 0.25, 400.0);
    update();
}

void CadViewport::NotifySelection()
{
    if (selectionChanged_) {
        selectionChanged_(selections_);
    }
}

void CadViewport::NotifyDrawingState()
{
    if (drawingStateChanged_) {
        drawingStateChanged_(tool_, drawingPoints_.size());
    }
}
