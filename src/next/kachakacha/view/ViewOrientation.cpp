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

} // namespace kachakacha::v2::view
