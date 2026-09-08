#pragma once

//! 視点の姿勢(PRD-073 / 074 / 075、AT-UIX-008)。
//!
//! ここは「どこを向いているか」だけを持つ。Qt にも OpenGL にも OCCT にも依存しない。
//! 画面へ出す行列は ScreenMapping が作る。
//!
//! 決まりが4つある。契約書がわざわざ「しない」と書いているものばかりである。
//!   1. ドラッグ中に 90度へ吸着しない。指を離した角度がそのまま残る。
//!   2. 離した後に勝手に回らない。慣性も、地平線の自動補正もない。
//!   3. 離散の向きを使うのは「面/辺/角をクリックして正対する」ときだけ。
//!   4. 回すのはカメラだけで、模型の座標は動かさない。
//!
//! V1 は orbit のたびに up を世界Zへ引き戻していたので、真上を通ると絵が回った。
//! V2 は姿勢を四元数1個で持ち、引き戻しをしない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Vector3.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::view {

using geometry::Vector3;

//! 姿勢。単位四元数として持つ。
struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] double Norm() const noexcept;
    [[nodiscard]] bool IsFinite() const noexcept;
};

[[nodiscard]] Quaternion Normalized(const Quaternion& value) noexcept;
//! left を先に、right を後に掛ける(right * left の順で回る合成ではない)。
[[nodiscard]] Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept;
[[nodiscard]] Quaternion Conjugate(const Quaternion& value) noexcept;
//! 軸まわりに angleRad 回す四元数。軸の長さが0なら単位を返す。
[[nodiscard]] Quaternion FromAxisAngle(const Vector3& axis, double angleRad) noexcept;
//! ベクトルを回す。
[[nodiscard]] Vector3 Rotate(const Quaternion& rotation, const Vector3& value) noexcept;
//! 2つの姿勢の間の角度(rad)。0 から π。符号は持たない。
[[nodiscard]] double AngleBetween(const Quaternion& from, const Quaternion& to) noexcept;

//! 姿勢から視線と上向きを取り出す。カメラは -Z を見ていて、+Y が上、という約束。
[[nodiscard]] Vector3 ForwardOf(const Quaternion& orientation) noexcept;
[[nodiscard]] Vector3 UpOf(const Quaternion& orientation) noexcept;
[[nodiscard]] Vector3 RightOf(const Quaternion& orientation) noexcept;

//! ビューキューブのどこを指したか。面6・辺12・角8で26通り。
struct ViewCubeZone {
    //! -1 / 0 / +1 の3値。3つとも0にはならない。
    int x = 0;
    int y = 0;
    int z = 1;

    //! 0以外がいくつあるか。1=面、2=辺、3=角。
    [[nodiscard]] int NonZeroCount() const noexcept;
    [[nodiscard]] bool IsFace() const noexcept { return NonZeroCount() == 1; }
    [[nodiscard]] bool IsEdge() const noexcept { return NonZeroCount() == 2; }
    [[nodiscard]] bool IsCorner() const noexcept { return NonZeroCount() == 3; }

    friend bool operator==(const ViewCubeZone& l, const ViewCubeZone& r) noexcept
    {
        return l.x == r.x && l.y == r.y && l.z == r.z;
    }
    friend bool operator!=(const ViewCubeZone& l, const ViewCubeZone& r) noexcept
    {
        return !(l == r);
    }
};

//! 26通りを、決まった順で並べたもの。台帳と試験が同じ順を見る。
[[nodiscard]] const std::vector<ViewCubeZone>& AllViewCubeZones();

//! 画面に出す日本語。「正面」「右上」「右上手前」など。
[[nodiscard]] std::string ViewCubeZoneLabelJa(const ViewCubeZone& zone);

//! キューブの表面で指した位置(立方体のローカル座標、各軸 -1..+1)から区画を決める。
//! 境目の帯の幅は edgeBandRatio。0.5 なら面が消えて辺と角だけになる。
[[nodiscard]] base::Result<ViewCubeZone> ViewCubeZoneAt(const Vector3& cubeLocalPoint,
    double edgeBandRatio);

//! その区画へ正対する姿勢。ここだけが離散の向きを使う。
[[nodiscard]] base::Result<Quaternion> OrientationForZone(const ViewCubeZone& zone);

//! ドラッグ1回分の状態。押した瞬間に作り、離すまで持ち回る。
struct ViewCubeDrag {
    bool active = false;
    Quaternion orientationAtPress{};
    double totalDxPx = 0.0;
    double totalDyPx = 0.0;
};

//! 感度。契約が数値まで決めている(ui-workflows §13.3)。
inline constexpr double kAxisArrowDegreesPerPixel = 0.25;
inline constexpr double kAxisArrowDegreesPerPixelFine = 0.05;
inline constexpr double kAxisArrowDegreesPerPixelCoarse = 1.0;
inline constexpr double kAxisArrowClickDegrees = 15.0;
inline constexpr double kViewCubeDegreesPerPixel = 0.5;

//! 押した。
[[nodiscard]] base::Result<ViewCubeDrag> BeginViewCubeDrag(const Quaternion& orientation);

//! 動かした。押した位置からの合計移動量で決めるので、途中の丸めが溜まらない。
[[nodiscard]] base::Result<Quaternion> UpdateViewCubeDrag(ViewCubeDrag& drag,
    double totalDxPx, double totalDyPx, double degreesPerPixel);

//! 離した。ここで姿勢は変わらない。慣性も吸着もないことを、この形で示す。
[[nodiscard]] base::Result<Quaternion> EndViewCubeDrag(ViewCubeDrag& drag,
    const Quaternion& orientationAtRelease);

//! 離した後、時間が経っても姿勢が変わらないこと(AT-UIX-008 の「1秒後に変化0」)。
[[nodiscard]] Quaternion SettleAfterRelease(const Quaternion& orientation,
    double elapsedSeconds) noexcept;

//! 回転矢印の軸の取り方。
enum class RotationAxisMode {
    World,     //!< 世界の X/Y/Z
    Relative,  //!< 選んだ部品のローカル X/Y/Z
};

enum class RotationAxis {
    X,
    Y,
    Z,
};

//! 押しているキー。感度が変わる。
enum class AxisArrowModifier {
    None,
    Fine,    //!< Shift
    Coarse,  //!< Ctrl
};

[[nodiscard]] double AxisArrowDegreesPerPixel(AxisArrowModifier modifier) noexcept;

//! 相対軸のとき、部品のローカル姿勢が要る。世界軸のときは渡さなくてよい。
struct AxisArrowRequest {
    Quaternion orientation{};
    RotationAxisMode mode = RotationAxisMode::World;
    RotationAxis axis = RotationAxis::Z;
    AxisArrowModifier modifier = AxisArrowModifier::None;
    //! 相対軸で使う。部品のローカル→世界の姿勢。
    bool hasSelectionFrame = false;
    Quaternion selectionFrame{};
};

//! 矢印をドラッグした。dragPx は矢印に沿った移動量。
[[nodiscard]] base::Result<Quaternion> RotateByAxisArrowDrag(const AxisArrowRequest& request,
    double dragPx);

//! 矢印をクリックした。15度だけ回る。90度ではない。
[[nodiscard]] base::Result<Quaternion> RotateByAxisArrowClick(const AxisArrowRequest& request,
    bool reverse);

//! 数値欄へ角度を入れた。
[[nodiscard]] base::Result<Quaternion> RotateByAxisAngle(const AxisArrowRequest& request,
    double degrees);

//! その要求で回せるか。相対軸で選択が無いときは断る理由を返す。
[[nodiscard]] base::Result<Vector3> ResolveRotationAxis(const AxisArrowRequest& request);

} // namespace kachakacha::v2::view
