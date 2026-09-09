#include "kachakacha/view/ViewOrientation.h"

#include "kachakacha/geometry/Units.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::view {
namespace {

using base::Diagnostic;
using base::MakeError;
using base::Result;

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double DegreesToRadians(double degrees) noexcept
{
    return degrees * kPi / 180.0;
}

[[nodiscard]] Diagnostic NotFinite(std::string details)
{
    return MakeError("UI-V001", "視点の値に数値でないものが入っています。", std::move(details));
}

[[nodiscard]] Vector3 AxisVector(RotationAxis axis) noexcept
{
    switch (axis) {
    case RotationAxis::X: return {1.0, 0.0, 0.0};
    case RotationAxis::Y: return {0.0, 1.0, 0.0};
    case RotationAxis::Z: return {0.0, 0.0, 1.0};
    }
    return {0.0, 0.0, 1.0};
}

[[nodiscard]] std::string SignLabel(int value, std::string_view positive,
    std::string_view negative)
{
    if (value > 0) {
        return std::string(positive);
    }
    if (value < 0) {
        return std::string(negative);
    }
    return {};
}

} // namespace

double Quaternion::Norm() const noexcept
{
    return std::sqrt(w * w + x * x + y * y + z * z);
}

bool Quaternion::IsFinite() const noexcept
{
    return geometry::IsFinite(w) && geometry::IsFinite(x) && geometry::IsFinite(y)
        && geometry::IsFinite(z);
}

Quaternion Normalized(const Quaternion& value) noexcept
{
    const double norm = value.Norm();
    if (!geometry::IsFinite(norm) || norm <= 0.0) {
        return Quaternion{};
    }
    return Quaternion{value.w / norm, value.x / norm, value.y / norm, value.z / norm};
}

Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept
{
    return Quaternion{
        right.w * left.w - right.x * left.x - right.y * left.y - right.z * left.z,
        right.w * left.x + right.x * left.w + right.y * left.z - right.z * left.y,
        right.w * left.y - right.x * left.z + right.y * left.w + right.z * left.x,
        right.w * left.z + right.x * left.y - right.y * left.x + right.z * left.w};
}

Quaternion Conjugate(const Quaternion& value) noexcept
{
    return Quaternion{value.w, -value.x, -value.y, -value.z};
}

Quaternion FromAxisAngle(const Vector3& axis, double angleRad) noexcept
{
    const double length = axis.Length();
    if (!geometry::IsFinite(length) || length <= 0.0 || !geometry::IsFinite(angleRad)) {
        return Quaternion{};
    }
    const double half = angleRad * 0.5;
    const double s = std::sin(half) / length;
    return Quaternion{std::cos(half), axis.x * s, axis.y * s, axis.z * s};
}

Vector3 Rotate(const Quaternion& rotation, const Vector3& value) noexcept
{
    const Quaternion unit = Normalized(rotation);
    const Vector3 u{unit.x, unit.y, unit.z};
    const double s = unit.w;
    const double dot = u.x * value.x + u.y * value.y + u.z * value.z;
    const Vector3 cross{u.y * value.z - u.z * value.y, u.z * value.x - u.x * value.z,
        u.x * value.y - u.y * value.x};
    return u * (2.0 * dot) + value * (s * s - u.LengthSquared()) + cross * (2.0 * s);
}

double AngleBetween(const Quaternion& from, const Quaternion& to) noexcept
{
    const Quaternion a = Normalized(from);
    const Quaternion b = Normalized(to);
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    dot = std::clamp(std::abs(dot), 0.0, 1.0);
    return 2.0 * std::acos(dot);
}

Vector3 ForwardOf(const Quaternion& orientation) noexcept
{
    return Rotate(orientation, Vector3{0.0, 0.0, -1.0});
}

Vector3 UpOf(const Quaternion& orientation) noexcept
{
    return Rotate(orientation, Vector3{0.0, 1.0, 0.0});
}

Vector3 RightOf(const Quaternion& orientation) noexcept
{
    return Rotate(orientation, Vector3{1.0, 0.0, 0.0});
}

int ViewCubeZone::NonZeroCount() const noexcept
{
    return (x != 0 ? 1 : 0) + (y != 0 ? 1 : 0) + (z != 0 ? 1 : 0);
}

const std::vector<ViewCubeZone>& AllViewCubeZones()
{
    static const std::vector<ViewCubeZone> zones = [] {
        std::vector<ViewCubeZone> built;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    if (x == 0 && y == 0 && z == 0) {
                        continue;
                    }
                    built.push_back(ViewCubeZone{x, y, z});
                }
            }
        }
        return built;
    }();
    return zones;
}

std::string ViewCubeZoneLabelJa(const ViewCubeZone& zone)
{
    // 模型は +Y を奥、+Z を上として置く。前は -Y。
    std::string label;
    label += SignLabel(zone.z, "上", "下");
    label += SignLabel(zone.y, "背", "正");
    label += SignLabel(zone.x, "右", "左");
    if (label.empty()) {
        return "正面";
    }
    if (zone.IsFace()) {
        if (zone.y != 0) {
            return zone.y > 0 ? "背面" : "正面";
        }
        return label + "面";
    }
    return label;
}

Result<ViewCubeZone> ViewCubeZoneAt(const Vector3& cubeLocalPoint, double edgeBandRatio)
{
    if (!cubeLocalPoint.IsFinite()) {
        return Result<ViewCubeZone>::Failure(NotFinite("キューブ上の位置です。"));
    }
    if (!geometry::IsFinite(edgeBandRatio) || edgeBandRatio <= 0.0 || edgeBandRatio >= 0.5) {
        return Result<ViewCubeZone>::Failure(MakeError("UI-V002",
            "ビューキューブの辺の帯幅が範囲外です。", "0 より大きく 0.5 より小さい値です。"));
    }
    const std::array<double, 3> values{cubeLocalPoint.x, cubeLocalPoint.y, cubeLocalPoint.z};
    double maxAbs = 0.0;
    for (double value : values) {
        maxAbs = std::max(maxAbs, std::abs(value));
    }
    if (maxAbs < 1.0 - 1e-9) {
        return Result<ViewCubeZone>::Failure(MakeError("UI-V003",
            "ビューキューブの表面ではない場所を指しています。",
            "立方体の面に載っていません。"));
    }
    const double threshold = 1.0 - edgeBandRatio;
    ViewCubeZone zone{0, 0, 0};
    std::array<int*, 3> zoneSlots{&zone.x, &zone.y, &zone.z};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (std::abs(values[index]) >= threshold) {
            *zoneSlots[index] = values[index] >= 0.0 ? 1 : -1;
        }
    }
    if (zone.NonZeroCount() == 0) {
        return Result<ViewCubeZone>::Failure(MakeError("UI-V003",
            "ビューキューブの表面ではない場所を指しています。",
            "どの面にも届いていません。"));
    }
    return Result<ViewCubeZone>::Success(zone);
}

Result<Quaternion> OrientationForZone(const ViewCubeZone& zone)
{
    if (zone.NonZeroCount() == 0) {
        return Result<Quaternion>::Failure(MakeError("UI-V004",
            "その向きへは正対できません。", "面も辺も角も指していません。"));
    }
    // 目からモデルへ向かう向き。区画の外向きの逆。
    const Vector3 outward{static_cast<double>(zone.x), static_cast<double>(zone.y),
        static_cast<double>(zone.z)};
    const double outwardLength = outward.Length();
    const Vector3 forward = outward * (-1.0 / outwardLength);

    // 上向きは世界Zを使う。真上・真下だけは世界Zが使えないので、-Y を上にする。
    Vector3 upHint{0.0, 0.0, 1.0};
    if (zone.x == 0 && zone.y == 0) {
        // V1 と同じで、上から見たときは奥(+Y)が画面の上になる。
        upHint = Vector3{0.0, static_cast<double>(zone.z), 0.0};
    }
    const double along = forward.x * upHint.x + forward.y * upHint.y + forward.z * upHint.z;
    Vector3 up = upHint - forward * along;
    const double upLength = up.Length();
    if (upLength <= 0.0) {
        return Result<Quaternion>::Failure(MakeError("UI-V004",
            "その向きへは正対できません。", "上向きを決められません。"));
    }
    up = up * (1.0 / upLength);
    // 右手系にする。right = forward x up。逆にすると鏡になり、四元数へ直せない。
    const Vector3 right{forward.y * up.z - forward.z * up.y,
        forward.z * up.x - forward.x * up.z, forward.x * up.y - forward.y * up.x};

    // 列が right / up / -forward の回転行列を四元数へ。
    const double m00 = right.x;
    const double m10 = right.y;
    const double m20 = right.z;
    const double m01 = up.x;
    const double m11 = up.y;
    const double m21 = up.z;
    const double m02 = -forward.x;
    const double m12 = -forward.y;
    const double m22 = -forward.z;
    const double trace = m00 + m11 + m22;
    Quaternion result;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        result = Quaternion{0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        result = Quaternion{(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        result = Quaternion{(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        result = Quaternion{(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
    }
    return Result<Quaternion>::Success(Normalized(result));
}

Result<ViewCubeDrag> BeginViewCubeDrag(const Quaternion& orientation)
{
    if (!orientation.IsFinite() || orientation.Norm() <= 0.0) {
        return Result<ViewCubeDrag>::Failure(NotFinite("いまの姿勢です。"));
    }
    ViewCubeDrag drag;
    drag.active = true;
    drag.orientationAtPress = Normalized(orientation);
    return Result<ViewCubeDrag>::Success(drag);
}

Result<Quaternion> UpdateViewCubeDrag(ViewCubeDrag& drag, double totalDxPx, double totalDyPx,
    double degreesPerPixel)
{
    if (!drag.active) {
        return Result<Quaternion>::Failure(MakeError("UI-V005",
            "ビューキューブを押していません。", "押してから動かしてください。"));
    }
    if (!geometry::IsFinite(totalDxPx) || !geometry::IsFinite(totalDyPx)) {
        return Result<Quaternion>::Failure(NotFinite("ドラッグの移動量です。"));
    }
    if (!geometry::IsFinite(degreesPerPixel) || degreesPerPixel <= 0.0) {
        return Result<Quaternion>::Failure(MakeError("UI-V006",
            "回す速さが正の数ではありません。", "1px あたりの角度です。"));
    }
    drag.totalDxPx = totalDxPx;
    drag.totalDyPx = totalDyPx;
    // 横は画面の上向き、縦は画面の右向きのまわり。押した瞬間の姿勢から積む。
    const Quaternion base = drag.orientationAtPress;
    const Quaternion yaw = FromAxisAngle(UpOf(base), DegreesToRadians(-totalDxPx * degreesPerPixel));
    const Quaternion pitch =
        FromAxisAngle(RightOf(base), DegreesToRadians(-totalDyPx * degreesPerPixel));
    return Result<Quaternion>::Success(Normalized(Multiply(base, Multiply(pitch, yaw))));
}

Result<Quaternion> EndViewCubeDrag(ViewCubeDrag& drag, const Quaternion& orientationAtRelease)
{
    if (!drag.active) {
        return Result<Quaternion>::Failure(MakeError("UI-V005",
            "ビューキューブを押していません。", "押していないので離せません。"));
    }
    if (!orientationAtRelease.IsFinite() || orientationAtRelease.Norm() <= 0.0) {
        return Result<Quaternion>::Failure(NotFinite("離した瞬間の姿勢です。"));
    }
    drag.active = false;
    drag.totalDxPx = 0.0;
    drag.totalDyPx = 0.0;
    // 吸着も慣性もしない。離した姿勢を、そのまま返す。
    return Result<Quaternion>::Success(Normalized(orientationAtRelease));
}

Quaternion SettleAfterRelease(const Quaternion& orientation, double elapsedSeconds) noexcept
{
    (void)elapsedSeconds;
    return Normalized(orientation);
}

double AxisArrowDegreesPerPixel(AxisArrowModifier modifier) noexcept
{
    switch (modifier) {
    case AxisArrowModifier::None:   return kAxisArrowDegreesPerPixel;
    case AxisArrowModifier::Fine:   return kAxisArrowDegreesPerPixelFine;
    case AxisArrowModifier::Coarse: return kAxisArrowDegreesPerPixelCoarse;
    }
    return kAxisArrowDegreesPerPixel;
}

Result<Vector3> ResolveRotationAxis(const AxisArrowRequest& request)
{
    if (!request.orientation.IsFinite() || request.orientation.Norm() <= 0.0) {
        return Result<Vector3>::Failure(NotFinite("いまの姿勢です。"));
    }
    const Vector3 local = AxisVector(request.axis);
    if (request.mode == RotationAxisMode::World) {
        return Result<Vector3>::Success(local);
    }
    if (!request.hasSelectionFrame) {
        return Result<Vector3>::Failure(MakeError("UI-V007",
            "相対軸で回すには、部品を1つ選んでください。",
            "選んだ部品のローカル軸を使うためです。"));
    }
    if (!request.selectionFrame.IsFinite() || request.selectionFrame.Norm() <= 0.0) {
        return Result<Vector3>::Failure(NotFinite("選んだ部品の姿勢です。"));
    }
    return Result<Vector3>::Success(Rotate(request.selectionFrame, local));
}

Result<Quaternion> RotateByAxisAngle(const AxisArrowRequest& request, double degrees)
{
    if (!geometry::IsFinite(degrees)) {
        return Result<Quaternion>::Failure(NotFinite("回す角度です。"));
    }
    const Result<Vector3> axis = ResolveRotationAxis(request);
    if (!axis.HasValue()) {
        return Result<Quaternion>::Failure(axis.Diagnostics());
    }
    const Quaternion delta = FromAxisAngle(axis.Value(), DegreesToRadians(degrees));
    return Result<Quaternion>::Success(
        Normalized(Multiply(Normalized(request.orientation), delta)));
}

Result<Quaternion> RotateByAxisArrowDrag(const AxisArrowRequest& request, double dragPx)
{
    if (!geometry::IsFinite(dragPx)) {
        return Result<Quaternion>::Failure(NotFinite("ドラッグの移動量です。"));
    }
    return RotateByAxisAngle(request, dragPx * AxisArrowDegreesPerPixel(request.modifier));
}

Result<Quaternion> RotateByAxisArrowClick(const AxisArrowRequest& request, bool reverse)
{
    const double degrees = reverse ? -kAxisArrowClickDegrees : kAxisArrowClickDegrees;
    return RotateByAxisAngle(request, degrees);
}

std::string_view RotationAxisName(RotationAxis axis) noexcept
{
    switch (axis) {
    case RotationAxis::X: return "X";
    case RotationAxis::Y: return "Y";
    case RotationAxis::Z: return "Z";
    }
    return "?";
}

std::string_view RotationAxisModeNameJa(RotationAxisMode mode) noexcept
{
    switch (mode) {
    case RotationAxisMode::World:    return "絶対";
    case RotationAxisMode::Relative: return "相対";
    }
    return "不明";
}

std::string AxisArrowTooltipJa(const AxisArrowButton& button)
{
    return std::string(RotationAxisModeNameJa(button.mode)) + " "
        + std::string(RotationAxisName(button.axis)) + "軸まわりに"
        + (button.positive ? "進める" : "戻す")
        + "(押すと15度、引きずると連続。Shiftで細かく、Ctrlで粗く)";
}

namespace {

//! 矢印1つの一辺。帯の幅から、間を引いて6等分する。
[[nodiscard]] double AxisArrowSizePx(double cubeSizePx) noexcept
{
    const double gap = cubeSizePx * kAxisArrowGapRatio;
    const double width = AxisArrowBlockWidthPx(cubeSizePx);
    return (width - gap * static_cast<double>(kAxisArrowColumns - 1))
        / static_cast<double>(kAxisArrowColumns);
}

} // namespace

double AxisArrowBlockWidthPx(double cubeSizePx) noexcept
{
    return cubeSizePx * kAxisArrowBlockWidthRatio;
}

double AxisArrowBlockHeightPx(double cubeSizePx) noexcept
{
    const double gap = cubeSizePx * kAxisArrowGapRatio;
    // 2段ぶん。段と段のあいだにも同じ間を空ける。
    return AxisArrowSizePx(cubeSizePx) * 2.0 + gap * 3.0;
}

std::vector<AxisArrowButton> BuildAxisArrowButtons(double cubeLeftPx, double cubeBottomPx,
    double cubeSizePx)
{
    std::vector<AxisArrowButton> buttons;
    if (!(cubeSizePx > 0.0)) {
        return buttons;
    }
    const double size = AxisArrowSizePx(cubeSizePx);
    const double gap = cubeSizePx * kAxisArrowGapRatio;
    // 帯はキューブの右端にそろえて、左へ伸ばす。
    const double blockLeft = cubeLeftPx + cubeSizePx - AxisArrowBlockWidthPx(cubeSizePx);
    const RotationAxis axes[] = {RotationAxis::X, RotationAxis::Y, RotationAxis::Z};
    const RotationAxisMode modes[] = {RotationAxisMode::World, RotationAxisMode::Relative};
    // 1段に「軸3つ x 向き2つ」を並べる。幅はキューブに収める。
    const double stride = size + gap;
    buttons.reserve(12);
    for (std::size_t row = 0; row < std::size(modes); ++row) {
        const double top = cubeBottomPx + gap + static_cast<double>(row) * (size + gap);
        std::size_t column = 0;
        for (const RotationAxis axis : axes) {
            for (const bool positive : {false, true}) {
                AxisArrowButton button;
                button.axis = axis;
                button.mode = modes[row];
                button.positive = positive;
                button.xPx = blockLeft + stride * static_cast<double>(column);
                button.yPx = top;
                button.widthPx = size;
                button.heightPx = size;
                buttons.push_back(button);
                ++column;
            }
        }
    }
    return buttons;
}

std::optional<std::size_t> AxisArrowAtScreen(const std::vector<AxisArrowButton>& buttons,
    double xPx, double yPx)
{
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        const AxisArrowButton& button = buttons[index];
        if (xPx >= button.xPx && xPx <= button.xPx + button.widthPx && yPx >= button.yPx
            && yPx <= button.yPx + button.heightPx) {
            return index;
        }
    }
    return std::nullopt;
}

// ---- 視点の操作板 ----
namespace {

//! その軸に垂直な平面を張る2本。輪はこの平面の上に描く。
void RingBasis(RotationAxis axis, Vector3& first, Vector3& second) noexcept
{
    switch (axis) {
    case RotationAxis::X:
        first = Vector3{0.0, 1.0, 0.0};
        second = Vector3{0.0, 0.0, 1.0};
        return;
    case RotationAxis::Y:
        first = Vector3{0.0, 0.0, 1.0};
        second = Vector3{1.0, 0.0, 0.0};
        return;
    case RotationAxis::Z:
        first = Vector3{1.0, 0.0, 0.0};
        second = Vector3{0.0, 1.0, 0.0};
        return;
    }
    first = Vector3{1.0, 0.0, 0.0};
    second = Vector3{0.0, 1.0, 0.0};
}

//! 世界の点を、キューブと同じ写し方で画面へ落とす。
[[nodiscard]] geometry::ScreenPoint ToRingScreen(const Vector3& world,
    const Vector3& right, const Vector3& up, double centerXPx, double centerYPx,
    double scalePx) noexcept
{
    const double sx = world.x * right.x + world.y * right.y + world.z * right.z;
    const double sy = world.x * up.x + world.y * up.y + world.z * up.z;
    // 画面のyは下向きなので、上向き成分は引く。
    return geometry::ScreenPoint{centerXPx + sx * scalePx, centerYPx - sy * scalePx};
}

//! 中心からいちばん遠い点。輪のどこに矢じりを置くかを決める。
//! いちばん遠いところは輪が真横を向いている場所なので、矢じりが潰れて見えない、が起きにくい。
[[nodiscard]] std::size_t FarthestPoint(const std::vector<geometry::ScreenPoint>& points,
    double centerXPx, double centerYPx) noexcept
{
    std::size_t best = 0;
    double bestDistance = -1.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double dx = points[index].x - centerXPx;
        const double dy = points[index].y - centerYPx;
        const double distance = dx * dx + dy * dy;
        if (distance > bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

//! その点での進む向き。となりの点との差から取る。長さは1に均す。
[[nodiscard]] geometry::ScreenPoint TangentAt(
    const std::vector<geometry::ScreenPoint>& points, std::size_t index) noexcept
{
    if (points.size() < 2) {
        return geometry::ScreenPoint{1.0, 0.0};
    }
    const std::size_t next = (index + 1) % points.size();
    const std::size_t previous = (index + points.size() - 1) % points.size();
    const double dx = points[next].x - points[previous].x;
    const double dy = points[next].y - points[previous].y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.0)) {
        return geometry::ScreenPoint{1.0, 0.0};
    }
    return geometry::ScreenPoint{dx / length, dy / length};
}

//! 四角を1つ足す。中心と一辺で置く。
void AddGadget(std::vector<ViewGadget>& gadgets, ViewGadgetKind kind,
    ViewGadgetDirection direction, RotationAxis axis, double centerXPx, double centerYPx,
    double sizePx)
{
    ViewGadget gadget;
    gadget.kind = kind;
    gadget.direction = direction;
    gadget.axis = axis;
    gadget.xPx = centerXPx - sizePx * 0.5;
    gadget.yPx = centerYPx - sizePx * 0.5;
    gadget.widthPx = sizePx;
    gadget.heightPx = sizePx;
    gadgets.push_back(gadget);
}

} // namespace

std::string_view ViewGadgetDirectionNameJa(ViewGadgetDirection direction) noexcept
{
    switch (direction) {
    case ViewGadgetDirection::Positive: return "進める";
    case ViewGadgetDirection::Negative: return "戻す";
    case ViewGadgetDirection::Up:       return "上へ";
    case ViewGadgetDirection::Down:     return "下へ";
    case ViewGadgetDirection::Left:     return "左へ";
    case ViewGadgetDirection::Right:    return "右へ";
    }
    return "不明";
}

std::string ViewGadgetTooltipJa(const ViewGadget& gadget, RotationAxisMode ringMode)
{
    switch (gadget.kind) {
    case ViewGadgetKind::Home:
        return "既定の視点へ戻す(斜め上から)";
    case ViewGadgetKind::Roll:
        return std::string("画面のまま回す(")
            + (gadget.direction == ViewGadgetDirection::Positive ? "左回り" : "右回り")
            + ")";
    case ViewGadgetKind::Orbit:
        return std::string("視点を")
            + std::string(ViewGadgetDirectionNameJa(gadget.direction)) + "回す";
    case ViewGadgetKind::AxisRing:
        return std::string(RotationAxisModeNameJa(ringMode)) + " "
            + std::string(RotationAxisName(gadget.axis)) + "軸まわりに"
            + std::string(ViewGadgetDirectionNameJa(gadget.direction))
            + "(押すと15度、引きずると連続。Shiftで細かく、Ctrlで粗く)";
    case ViewGadgetKind::AlignSelection:
        return "選んだものに正対する";
    case ViewGadgetKind::ModeToggle:
        return std::string("輪の軸の取り方を切り替える(いまは ")
            + std::string(RotationAxisModeNameJa(ringMode)) + ")";
    }
    return {};
}

ViewGadgetLayout BuildViewGadgets(double cubeLeftPx, double cubeTopPx, double cubeSizePx,
    const Quaternion& orientation)
{
    ViewGadgetLayout layout;
    if (!(cubeSizePx > 0.0) || !orientation.IsFinite() || orientation.Norm() <= 0.0) {
        return layout;
    }
    const double button = cubeSizePx * kViewGadgetButtonRatio;
    const double centerX = cubeLeftPx + cubeSizePx * 0.5;
    const double centerY = cubeTopPx + cubeSizePx * 0.5;
    // 立方体は一辺2(-1..+1)。外接球の半径 sqrt(3) が入る大きさで写す。
    const double scale = cubeSizePx * 0.5 / 1.7320508075688772;
    const double ringRadius = cubeSizePx * kViewRingRadiusRatio * 0.5 / scale;
    const Vector3 right = RightOf(orientation);
    const Vector3 up = UpOf(orientation);

    // 軸ごとの輪。決まった順(X→Y→Z)で作る。
    for (const RotationAxis axis : {RotationAxis::X, RotationAxis::Y, RotationAxis::Z}) {
        Vector3 first{};
        Vector3 second{};
        RingBasis(axis, first, second);
        ViewAxisRing ring;
        ring.axis = axis;
        ring.points.reserve(static_cast<std::size_t>(kViewRingSampleCount));
        for (int step = 0; step < kViewRingSampleCount; ++step) {
            const double angle = 6.283185307179586 * static_cast<double>(step)
                / static_cast<double>(kViewRingSampleCount);
            const Vector3 point = first * (ringRadius * std::cos(angle))
                + second * (ringRadius * std::sin(angle));
            ring.points.push_back(ToRingScreen(point, right, up, centerX, centerY, scale));
        }
        const std::size_t farthest = FarthestPoint(ring.points, centerX, centerY);
        const std::size_t opposite = (farthest + ring.points.size() / 2) % ring.points.size();
        ring.positiveHead = ring.points[farthest];
        ring.positiveTangent = TangentAt(ring.points, farthest);
        ring.negativeHead = ring.points[opposite];
        ring.negativeTangent = TangentAt(ring.points, opposite);
        // 戻す側は、進む向きの逆を向く。
        ring.negativeTangent.x = -ring.negativeTangent.x;
        ring.negativeTangent.y = -ring.negativeTangent.y;
        layout.rings.push_back(std::move(ring));
    }
    for (const ViewAxisRing& ring : layout.rings) {
        AddGadget(layout.gadgets, ViewGadgetKind::AxisRing, ViewGadgetDirection::Positive,
            ring.axis, ring.positiveHead.x, ring.positiveHead.y, button);
        AddGadget(layout.gadgets, ViewGadgetKind::AxisRing, ViewGadgetDirection::Negative,
            ring.axis, ring.negativeHead.x, ring.negativeHead.y, button);
    }

    // キューブの外側。上に家と2つの丸矢印、右に上下、下に左右。
    const double outer = cubeSizePx * 0.5 + button * 0.9;
    AddGadget(layout.gadgets, ViewGadgetKind::Home, ViewGadgetDirection::Positive,
        RotationAxis::X, cubeLeftPx - button * 0.2, cubeTopPx - button * 0.6, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Roll, ViewGadgetDirection::Positive,
        RotationAxis::Z, centerX - button * 0.8, cubeTopPx - button * 0.6, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Roll, ViewGadgetDirection::Negative,
        RotationAxis::Z, centerX + button * 0.8, cubeTopPx - button * 0.6, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Orbit, ViewGadgetDirection::Up,
        RotationAxis::X, centerX + outer, centerY - button * 0.9, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Orbit, ViewGadgetDirection::Down,
        RotationAxis::X, centerX + outer, centerY + button * 0.9, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Orbit, ViewGadgetDirection::Left,
        RotationAxis::Y, centerX - button * 1.1, centerY + outer, button);
    AddGadget(layout.gadgets, ViewGadgetKind::Orbit, ViewGadgetDirection::Right,
        RotationAxis::Y, centerX + button * 1.1, centerY + outer, button);
    AddGadget(layout.gadgets, ViewGadgetKind::ModeToggle, ViewGadgetDirection::Positive,
        RotationAxis::X, cubeLeftPx + cubeSizePx + button * 0.2, cubeTopPx - button * 0.6,
        button);
    // 「選択に正対」は横に長い。四角を作ってから幅だけ広げる。
    AddGadget(layout.gadgets, ViewGadgetKind::AlignSelection, ViewGadgetDirection::Positive,
        RotationAxis::X, centerX, centerY + outer + button * 1.3, button);
    ViewGadget& align = layout.gadgets.back();
    align.xPx = cubeLeftPx - button * 0.6;
    align.widthPx = cubeSizePx + button * 1.2;

    double minX = layout.gadgets.front().xPx;
    double minY = layout.gadgets.front().yPx;
    double maxX = minX;
    double maxY = minY;
    for (const ViewGadget& gadget : layout.gadgets) {
        minX = std::min(minX, gadget.xPx);
        minY = std::min(minY, gadget.yPx);
        maxX = std::max(maxX, gadget.xPx + gadget.widthPx);
        maxY = std::max(maxY, gadget.yPx + gadget.heightPx);
    }
    layout.xPx = minX;
    layout.yPx = minY;
    layout.widthPx = maxX - minX;
    layout.heightPx = maxY - minY;
    return layout;
}

std::optional<std::size_t> ViewGadgetAtScreen(const ViewGadgetLayout& layout, double xPx,
    double yPx)
{
    // 重なっているときは、中心がいちばん近いものを返す。毎回同じ結果になる。
    std::optional<std::size_t> best;
    double bestDistance = 0.0;
    for (std::size_t index = 0; index < layout.gadgets.size(); ++index) {
        const ViewGadget& gadget = layout.gadgets[index];
        if (xPx < gadget.xPx || xPx > gadget.xPx + gadget.widthPx || yPx < gadget.yPx
            || yPx > gadget.yPx + gadget.heightPx) {
            continue;
        }
        const double dx = xPx - gadget.CenterXPx();
        const double dy = yPx - gadget.CenterYPx();
        const double distance = dx * dx + dy * dy;
        if (!best.has_value() || distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
    }
    return best;
}

Result<Quaternion> RotateByScreenAxis(const Quaternion& orientation,
    ViewGadgetDirection direction, double degrees)
{
    if (!orientation.IsFinite() || orientation.Norm() <= 0.0) {
        return Result<Quaternion>::Failure(NotFinite("いまの姿勢です。"));
    }
    if (!geometry::IsFinite(degrees)) {
        return Result<Quaternion>::Failure(NotFinite("回す角度です。"));
    }
    // 画面の軸で回す。カメラの姿勢から取るので、世界のどこを向いていても同じ操作感になる。
    const Quaternion unit = Normalized(orientation);
    Vector3 axis{};
    double sign = 1.0;
    switch (direction) {
    case ViewGadgetDirection::Up:
        axis = RightOf(unit);
        sign = 1.0;
        break;
    case ViewGadgetDirection::Down:
        axis = RightOf(unit);
        sign = -1.0;
        break;
    case ViewGadgetDirection::Left:
        axis = UpOf(unit);
        sign = 1.0;
        break;
    case ViewGadgetDirection::Right:
        axis = UpOf(unit);
        sign = -1.0;
        break;
    case ViewGadgetDirection::Positive:
        axis = ForwardOf(unit);
        sign = 1.0;
        break;
    case ViewGadgetDirection::Negative:
        axis = ForwardOf(unit);
        sign = -1.0;
        break;
    }
    const Quaternion delta = FromAxisAngle(axis, DegreesToRadians(degrees * sign));
    return Result<Quaternion>::Success(Normalized(Multiply(unit, delta)));
}

} // namespace kachakacha::v2::view
