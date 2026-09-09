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
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/geometry/Vector3.h"

#include <array>
#include <optional>
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

// ---- 回転矢印の並べ方(V1同等。ui-workflows §13.3)----
//
// V1 はビューキューブの下に、絶対回転と相対回転の矢印が並んでいた。
// 並べ方を画面に書くと、画面を出さないと位置を確かめられない。だからここに置く。
//
// 並びは上段が絶対(世界の X/Y/Z)、下段が相対(選んだ部品の X/Y/Z)。
// 各段に軸ごとの「戻す」「進める」が並ぶので、1段6個、2段で12個になる。

//! 矢印1つ。場所は画面の座標(px)。左上が原点。
struct AxisArrowButton {
    RotationAxis axis = RotationAxis::X;
    RotationAxisMode mode = RotationAxisMode::World;
    //! 進める向きなら true、戻す向きなら false。
    bool positive = true;
    double xPx = 0.0;
    double yPx = 0.0;
    double widthPx = 0.0;
    double heightPx = 0.0;
};

//! 矢印を並べる帯の幅と、間の広さ。キューブの大きさから決める。
//! 帯はキューブより広い。6個を1段に並べると、キューブ幅では押せない大きさになるためである。
//! 帯はキューブの右端にそろえて、左へ伸ばす。キューブは画面の右端にあるので、
//! 右へ伸ばすと画面の外へ出てしまう。
inline constexpr double kAxisArrowBlockWidthRatio = 2.1;
inline constexpr double kAxisArrowGapRatio = 0.05;
//! 1段に並ぶ数。軸3つ x 向き2つ。
inline constexpr int kAxisArrowColumns = 6;

//! 軸の名前。画面に出す。
[[nodiscard]] std::string_view RotationAxisName(RotationAxis axis) noexcept;
//! 軸の取り方の名前。絶対 / 相対。
[[nodiscard]] std::string_view RotationAxisModeNameJa(RotationAxisMode mode) noexcept;
//! 矢印1つの説明。押す前に何が起きるか分かるようにする。
[[nodiscard]] std::string AxisArrowTooltipJa(const AxisArrowButton& button);

//! キューブの下へ矢印を並べる。cubeLeftPx / cubeBottomPx はキューブの左と下。
//! 並びは決まった順で、いつも12個返す。相対軸が使えないときも消さない。
//! 消すと「無い」のか「使えない」のかが分からなくなる。
[[nodiscard]] std::vector<AxisArrowButton> BuildAxisArrowButtons(double cubeLeftPx,
    double cubeBottomPx, double cubeSizePx);

//! 並べた矢印がぜんぶ入る高さと幅。キューブの下にどれだけ空ければよいか。
[[nodiscard]] double AxisArrowBlockHeightPx(double cubeSizePx) noexcept;
[[nodiscard]] double AxisArrowBlockWidthPx(double cubeSizePx) noexcept;

//! 画面のその点にある矢印。無ければ値を持たない。
[[nodiscard]] std::optional<std::size_t> AxisArrowAtScreen(
    const std::vector<AxisArrowButton>& buttons, double xPx, double yPx);

// ---- 視点の操作板(V1同等。ui-workflows §13.3)----
//
// V1 はキューブのまわりに
//   - 世界の X/Y/Z ごとの色付きの輪(赤・緑・青)と、その両端の矢じり
//   - 画面の上下・左右へ回す弧の矢印
//   - 画面の奥行き軸まわりに回す2つの丸矢印
//   - 既定の視点へ戻す家の印
//   - 「選択に正対」
// が並んでいた。並べ方をここに置く。画面に書くと、画面を出さないと確かめられない。

//! 操作板の部品の種類。
enum class ViewGadgetKind {
    Home,           //!< 既定の視点へ戻す
    Roll,           //!< 画面の奥行き軸まわり
    Orbit,          //!< 画面の上下・左右
    AxisRing,       //!< 世界の軸まわり(輪の端の矢じり)
    AlignSelection, //!< 選んだものに正対
    ModeToggle,     //!< 輪を絶対で使うか、選んだ部品の軸で使うか
};

//! 向き。種類ごとに意味が変わる。
enum class ViewGadgetDirection {
    Positive,
    Negative,
    Up,
    Down,
    Left,
    Right,
};

[[nodiscard]] std::string_view ViewGadgetDirectionNameJa(ViewGadgetDirection direction) noexcept;

//! 操作板の部品1つ。場所は画面の座標(px)。左上が原点。
struct ViewGadget {
    ViewGadgetKind kind = ViewGadgetKind::Home;
    RotationAxis axis = RotationAxis::X;          //!< AxisRing のときだけ意味がある
    ViewGadgetDirection direction = ViewGadgetDirection::Positive;
    double xPx = 0.0;
    double yPx = 0.0;
    double widthPx = 0.0;
    double heightPx = 0.0;

    [[nodiscard]] double CenterXPx() const noexcept { return xPx + widthPx * 0.5; }
    [[nodiscard]] double CenterYPx() const noexcept { return yPx + heightPx * 0.5; }
};

//! 輪1本ぶんの描き方。画面に落とした点列と、両端の矢じりの向き。
struct ViewAxisRing {
    RotationAxis axis = RotationAxis::X;
    //! 閉じた点列。最後の点は最初の点と同じにしない(閉じるのは描く側)。
    std::vector<geometry::ScreenPoint> points;
    //! 矢じりを置く点と、そこでの進む向き(単位ベクトル)。
    geometry::ScreenPoint positiveHead{};
    geometry::ScreenPoint positiveTangent{};
    geometry::ScreenPoint negativeHead{};
    geometry::ScreenPoint negativeTangent{};
};

//! 操作板ぜんぶ。
struct ViewGadgetLayout {
    std::vector<ViewGadget> gadgets;
    std::vector<ViewAxisRing> rings;
    //! 操作板が占める四角。画面に入るかを見るのに使う。
    double xPx = 0.0;
    double yPx = 0.0;
    double widthPx = 0.0;
    double heightPx = 0.0;
};

//! 輪の大きさ。キューブの外側へどれだけ出るか。
inline constexpr double kViewRingRadiusRatio = 0.86;
//! 矢じりや丸ボタンの一辺。キューブの大きさに対する割合。
inline constexpr double kViewGadgetButtonRatio = 0.26;
//! 輪を何点で描くか。多すぎても目には変わらない。
inline constexpr int kViewRingSampleCount = 72;
//! 操作板がキューブの外へ出るぶん。キューブの大きさに対する割合。
//! キューブを置くときに、この分だけ余白を空けておけば、操作板がはみ出さない。
inline constexpr double kViewGadgetOuterMarginRatio = 0.45;

//! キューブのまわりに操作板を並べる。輪は姿勢によって形が変わるので姿勢を渡す。
[[nodiscard]] ViewGadgetLayout BuildViewGadgets(double cubeLeftPx, double cubeTopPx,
    double cubeSizePx, const Quaternion& orientation);

//! 操作板を画面の中へ寄せる。はみ出していたら、はみ出したぶんだけ全体を動かす。
//! 動かしても入らない(画面より操作板が大きい)ときは false を返し、何も変えない。
//! 出さない判断は呼び手がする。ここで勝手に縮めると、押せない大きさになる。
[[nodiscard]] bool FitViewGadgetsIntoScreen(ViewGadgetLayout& layout, double widthPx,
    double heightPx);

//! 画面のその点にある部品。無ければ値を持たない。近いものを1つだけ返す。
[[nodiscard]] std::optional<std::size_t> ViewGadgetAtScreen(
    const ViewGadgetLayout& layout, double xPx, double yPx);

//! その部品の説明。押す前に何が起きるか分かるようにする。
[[nodiscard]] std::string ViewGadgetTooltipJa(const ViewGadget& gadget,
    RotationAxisMode ringMode);

//! 画面の軸で回す。上下は画面の横軸、左右は画面の縦軸、rollは視線の軸。
//! 世界の軸ではないので、AxisArrowRequest は使わない。
[[nodiscard]] base::Result<Quaternion> RotateByScreenAxis(const Quaternion& orientation,
    ViewGadgetDirection direction, double degrees);

} // namespace kachakacha::v2::view
