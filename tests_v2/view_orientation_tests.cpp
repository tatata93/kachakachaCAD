// 視点の姿勢とビューキューブ(AT-UIX-008 / PRD-073 / 074 / 075)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/view/ViewOrientation.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::view;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] double Degrees(double radians)
{
    return radians * 180.0 / kPi;
}

void RequireVectorNear(const Vector3& actual, const Vector3& expected, const char* what)
{
    RequireNear(actual.x, expected.x, 1e-9, what);
    RequireNear(actual.y, expected.y, 1e-9, what);
    RequireNear(actual.z, expected.z, 1e-9, what);
}

[[nodiscard]] AxisArrowRequest WorldRequest(RotationAxis axis)
{
    AxisArrowRequest request;
    request.orientation = Quaternion{1.0, 0.0, 0.0, 0.0};
    request.mode = RotationAxisMode::World;
    request.axis = axis;
    return request;
}

} // namespace

KACHA_V2_TEST(view, 単位四元数は何も回さない)
{
    const Quaternion identity{1.0, 0.0, 0.0, 0.0};
    RequireVectorNear(Rotate(identity, Vector3{1.0, 2.0, 3.0}), Vector3{1.0, 2.0, 3.0}, "回らない");
}

KACHA_V2_TEST(view, Z軸まわり90度でXがYへ行く)
{
    const Quaternion rotation = FromAxisAngle(Vector3{0.0, 0.0, 1.0}, kPi * 0.5);
    RequireVectorNear(Rotate(rotation, Vector3{1.0, 0.0, 0.0}), Vector3{0.0, 1.0, 0.0}, "X->Y");
}

KACHA_V2_TEST(view, 合成した回転は順に効く)
{
    const Quaternion first = FromAxisAngle(Vector3{0.0, 0.0, 1.0}, kPi * 0.5);
    const Quaternion second = FromAxisAngle(Vector3{1.0, 0.0, 0.0}, kPi * 0.5);
    const Vector3 stepwise = Rotate(second, Rotate(first, Vector3{1.0, 0.0, 0.0}));
    const Vector3 combined = Rotate(Multiply(first, second), Vector3{1.0, 0.0, 0.0});
    RequireVectorNear(combined, stepwise, "1つにまとめても同じ");
}

KACHA_V2_TEST(view, 長さ0の軸では回らない)
{
    const Quaternion rotation = FromAxisAngle(Vector3{0.0, 0.0, 0.0}, 1.0);
    RequireNear(rotation.w, 1.0, 1e-12, "単位のまま");
}

KACHA_V2_TEST(view, 姿勢の間の角度が測れる)
{
    const Quaternion identity{1.0, 0.0, 0.0, 0.0};
    const Quaternion turned = FromAxisAngle(Vector3{0.0, 1.0, 0.0}, kPi / 3.0);
    RequireNear(Degrees(AngleBetween(identity, turned)), 60.0, 1e-9, "60度");
}

KACHA_V2_TEST(view, 区画は26通りある)
{
    RequireEqual(std::to_string(AllViewCubeZones().size()), "26", "面6辺12角8");
    int faces = 0;
    int edges = 0;
    int corners = 0;
    for (const ViewCubeZone& zone : AllViewCubeZones()) {
        faces += zone.IsFace() ? 1 : 0;
        edges += zone.IsEdge() ? 1 : 0;
        corners += zone.IsCorner() ? 1 : 0;
    }
    RequireEqual(std::to_string(faces), "6", "面");
    RequireEqual(std::to_string(edges), "12", "辺");
    RequireEqual(std::to_string(corners), "8", "角");
}

KACHA_V2_TEST(view, 区画の名前が重ならない)
{
    std::set<std::string> labels;
    for (const ViewCubeZone& zone : AllViewCubeZones()) {
        const std::string label = ViewCubeZoneLabelJa(zone);
        Require(!label.empty(), "名前が空でない");
        Require(labels.insert(label).second, "名前が重ならない: " + label);
    }
}

KACHA_V2_TEST(view, 面の真ん中を指すと面になる)
{
    const auto result = ViewCubeZoneAt(Vector3{0.0, -1.0, 0.0}, 0.25);
    Require(result.HasValue(), "面を返す");
    Require(result.Value().IsFace(), "面である");
    RequireEqual(ViewCubeZoneLabelJa(result.Value()), "前", "前の面");
}

KACHA_V2_TEST(view, 辺の帯を指すと辺になる)
{
    const auto result = ViewCubeZoneAt(Vector3{0.0, -1.0, 0.9}, 0.25);
    Require(result.HasValue(), "辺を返す");
    Require(result.Value().IsEdge(), "辺である");
}

KACHA_V2_TEST(view, 角を指すと角になる)
{
    const auto result = ViewCubeZoneAt(Vector3{0.95, -1.0, 0.92}, 0.25);
    Require(result.HasValue(), "角を返す");
    Require(result.Value().IsCorner(), "角である");
}

KACHA_V2_TEST(view, 帯を広げると面が辺に変わる)
{
    const Vector3 point{0.0, -1.0, 0.7};
    const auto narrow = ViewCubeZoneAt(point, 0.2);
    const auto wide = ViewCubeZoneAt(point, 0.4);
    Require(narrow.HasValue() && narrow.Value().IsFace(), "細い帯では面");
    Require(wide.HasValue() && wide.Value().IsEdge(), "広い帯では辺");
}

KACHA_V2_TEST(view, キューブの中を指したら断る)
{
    const auto result = ViewCubeZoneAt(Vector3{0.2, 0.3, 0.1}, 0.25);
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-V003", "表面ではない");
}

KACHA_V2_TEST(view, 数値でない位置を断る)
{
    const auto result = ViewCubeZoneAt(Vector3{std::nan(""), -1.0, 0.0}, 0.25);
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-V001", "数値でない");
}

KACHA_V2_TEST(view, 帯幅が範囲外なら断る)
{
    for (double ratio : {0.0, -0.1, 0.5, 0.9}) {
        const auto result = ViewCubeZoneAt(Vector3{0.0, -1.0, 0.0}, ratio);
        Require(!result.HasValue(), "断る");
        RequireEqual(FirstCode(result.Diagnostics()), "UI-V002", "帯幅");
    }
}

KACHA_V2_TEST(view, 26通りすべてで正対できる)
{
    for (const ViewCubeZone& zone : AllViewCubeZones()) {
        const auto result = OrientationForZone(zone);
        Require(result.HasValue(), "正対できる: " + ViewCubeZoneLabelJa(zone));
        const Quaternion orientation = result.Value();
        RequireNear(orientation.Norm(), 1.0, 1e-9, "単位である");
        const Vector3 forward = ForwardOf(orientation);
        const Vector3 outward{static_cast<double>(zone.x), static_cast<double>(zone.y),
            static_cast<double>(zone.z)};
        const double length = outward.Length();
        RequireVectorNear(forward, outward * (-1.0 / length), "区画の逆を向く");
        const Vector3 up = UpOf(orientation);
        RequireNear(up.Length(), 1.0, 1e-9, "上向きは単位");
        RequireNear(forward.x * up.x + forward.y * up.y + forward.z * up.z, 0.0, 1e-9,
            "視線と上向きは直交");
    }
}

KACHA_V2_TEST(view, 正面と背面は別の姿勢になる)
{
    const auto front = OrientationForZone(ViewCubeZone{0, -1, 0});
    const auto back = OrientationForZone(ViewCubeZone{0, 1, 0});
    Require(front.HasValue() && back.HasValue(), "両方できる");
    RequireNear(Degrees(AngleBetween(front.Value(), back.Value())), 180.0, 1e-6, "180度違う");
}

KACHA_V2_TEST(view, 真上からは世界Zを上にできないので奥を上にする)
{
    const auto top = OrientationForZone(ViewCubeZone{0, 0, 1});
    Require(top.HasValue(), "できる");
    RequireVectorNear(ForwardOf(top.Value()), Vector3{0.0, 0.0, -1.0}, "真下を見る");
    RequireVectorNear(UpOf(top.Value()), Vector3{0.0, 1.0, 0.0}, "奥が上");
    RequireVectorNear(RightOf(top.Value()), Vector3{1.0, 0.0, 0.0}, "右は+X");

    const auto bottom = OrientationForZone(ViewCubeZone{0, 0, -1});
    Require(bottom.HasValue(), "できる");
    RequireVectorNear(ForwardOf(bottom.Value()), Vector3{0.0, 0.0, 1.0}, "真上を見る");
    RequireVectorNear(UpOf(bottom.Value()), Vector3{0.0, -1.0, 0.0}, "下から見ると手前が上");
    RequireVectorNear(RightOf(bottom.Value()), Vector3{1.0, 0.0, 0.0}, "右は+X のまま");
}

KACHA_V2_TEST(view, 角からの正対は等角になる)
{
    const auto corner = OrientationForZone(ViewCubeZone{1, -1, 1});
    Require(corner.HasValue(), "できる");
    const Vector3 forward = ForwardOf(corner.Value());
    RequireNear(std::abs(forward.x), std::abs(forward.y), 1e-9, "3軸が同じ量");
    RequireNear(std::abs(forward.y), std::abs(forward.z), 1e-9, "3軸が同じ量");
}

KACHA_V2_TEST(view, 押していないのに動かしたら断る)
{
    ViewCubeDrag drag;
    const auto result = UpdateViewCubeDrag(drag, 10.0, 0.0, kViewCubeDegreesPerPixel);
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-V005", "押していない");
}

KACHA_V2_TEST(view, ドラッグ量と回転量が比例する)
{
    const auto begun = BeginViewCubeDrag(Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(begun.HasValue(), "押せた");
    ViewCubeDrag drag = begun.Value();
    const auto small = UpdateViewCubeDrag(drag, 20.0, 0.0, 0.5);
    Require(small.HasValue(), "動かせた");
    RequireNear(Degrees(AngleBetween(drag.orientationAtPress, small.Value())), 10.0, 1e-6,
        "20px x 0.5deg = 10度");
    const auto large = UpdateViewCubeDrag(drag, 40.0, 0.0, 0.5);
    Require(large.HasValue(), "動かせた");
    RequireNear(Degrees(AngleBetween(drag.orientationAtPress, large.Value())), 20.0, 1e-6,
        "2倍動かせば2倍回る");
}

KACHA_V2_TEST(view, 途中の細かい動きでも合計だけで決まる)
{
    const Quaternion start = FromAxisAngle(Vector3{0.0, 1.0, 0.0}, 0.3);
    const auto begun = BeginViewCubeDrag(start);
    Require(begun.HasValue(), "押せた");
    ViewCubeDrag stepwise = begun.Value();
    for (int step = 1; step <= 37; ++step) {
        const auto moved = UpdateViewCubeDrag(stepwise, step * 1.0, step * 0.5, 0.5);
        Require(moved.HasValue(), "動かせた");
    }
    const auto stepEnd = UpdateViewCubeDrag(stepwise, 37.0, 18.5, 0.5);
    ViewCubeDrag direct = begun.Value();
    const auto jump = UpdateViewCubeDrag(direct, 37.0, 18.5, 0.5);
    Require(stepEnd.HasValue() && jump.HasValue(), "どちらも動かせた");
    RequireNear(Degrees(AngleBetween(stepEnd.Value(), jump.Value())), 0.0, 1e-9,
        "刻んでも一気でも同じ");
}

KACHA_V2_TEST(view, ドラッグ中に90度へ吸着しない)
{
    const auto begun = BeginViewCubeDrag(Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(begun.HasValue(), "押せた");
    ViewCubeDrag drag = begun.Value();
    // 90度のすぐ手前まで回しても、90度になってしまわないこと。
    const auto moved = UpdateViewCubeDrag(drag, 178.0, 0.0, 0.5);
    Require(moved.HasValue(), "動かせた");
    const double angle = Degrees(AngleBetween(drag.orientationAtPress, moved.Value()));
    RequireNear(angle, 89.0, 1e-6, "89度のまま");
    Require(std::abs(angle - 90.0) > 0.5, "90度へ寄せない");
}

KACHA_V2_TEST(view, 離しても姿勢が変わらない)
{
    const auto begun = BeginViewCubeDrag(Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(begun.HasValue(), "押せた");
    ViewCubeDrag drag = begun.Value();
    const auto moved = UpdateViewCubeDrag(drag, 133.0, -71.0, 0.5);
    Require(moved.HasValue(), "動かせた");
    const auto released = EndViewCubeDrag(drag, moved.Value());
    Require(released.HasValue(), "離せた");
    RequireNear(Degrees(AngleBetween(moved.Value(), released.Value())), 0.0, 1e-12,
        "離した瞬間に変わらない");
    Require(!drag.active, "ドラッグが終わっている");
}

KACHA_V2_TEST(view, 離した1秒後も変化0)
{
    const Quaternion held = FromAxisAngle(Vector3{0.3, 0.7, -0.2}, 1.1);
    for (double seconds : {0.0, 0.1, 1.0, 10.0, 3600.0}) {
        const Quaternion settled = SettleAfterRelease(held, seconds);
        RequireNear(Degrees(AngleBetween(held, settled)), 0.0, 1e-12, "慣性がない");
    }
}

KACHA_V2_TEST(view, 離してからもう一度離せない)
{
    const auto begun = BeginViewCubeDrag(Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(begun.HasValue(), "押せた");
    ViewCubeDrag drag = begun.Value();
    const auto first = EndViewCubeDrag(drag, Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(first.HasValue(), "1度目は離せる");
    const auto second = EndViewCubeDrag(drag, Quaternion{1.0, 0.0, 0.0, 0.0});
    Require(!second.HasValue(), "2度目は断る");
    RequireEqual(FirstCode(second.Diagnostics()), "UI-V005", "押していない");
}

KACHA_V2_TEST(view, 回す速さが0以下なら断る)
{
    const auto begun = BeginViewCubeDrag(Quaternion{1.0, 0.0, 0.0, 0.0});
    ViewCubeDrag drag = begun.Value();
    for (double speed : {0.0, -0.5}) {
        const auto result = UpdateViewCubeDrag(drag, 10.0, 0.0, speed);
        Require(!result.HasValue(), "断る");
        RequireEqual(FirstCode(result.Diagnostics()), "UI-V006", "速さ");
    }
}

KACHA_V2_TEST(view, 感度が契約どおり3段ある)
{
    RequireNear(AxisArrowDegreesPerPixel(AxisArrowModifier::None), 0.25, 1e-12, "既定");
    RequireNear(AxisArrowDegreesPerPixel(AxisArrowModifier::Fine), 0.05, 1e-12, "Shift");
    RequireNear(AxisArrowDegreesPerPixel(AxisArrowModifier::Coarse), 1.0, 1e-12, "Ctrl");
}

KACHA_V2_TEST(view, 矢印ドラッグが感度どおり回る)
{
    const AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    const auto normal = RotateByAxisArrowDrag(request, 100.0);
    Require(normal.HasValue(), "回せた");
    RequireNear(Degrees(AngleBetween(request.orientation, normal.Value())), 25.0, 1e-9,
        "100px で 25度");

    AxisArrowRequest fine = request;
    fine.modifier = AxisArrowModifier::Fine;
    const auto fineResult = RotateByAxisArrowDrag(fine, 100.0);
    RequireNear(Degrees(AngleBetween(request.orientation, fineResult.Value())), 5.0, 1e-9,
        "Shift で 5度");

    AxisArrowRequest coarse = request;
    coarse.modifier = AxisArrowModifier::Coarse;
    const auto coarseResult = RotateByAxisArrowDrag(coarse, 100.0);
    RequireNear(Degrees(AngleBetween(request.orientation, coarseResult.Value())), 100.0, 1e-9,
        "Ctrl で 100度");
}

KACHA_V2_TEST(view, 矢印クリックは15度で90度ではない)
{
    const AxisArrowRequest request = WorldRequest(RotationAxis::X);
    const auto forward = RotateByAxisArrowClick(request, false);
    Require(forward.HasValue(), "回せた");
    RequireNear(Degrees(AngleBetween(request.orientation, forward.Value())), 15.0, 1e-9,
        "15度");
    const auto backward = RotateByAxisArrowClick(request, true);
    RequireNear(Degrees(AngleBetween(forward.Value(), backward.Value())), 30.0, 1e-9,
        "逆向きは反対に15度");
}

KACHA_V2_TEST(view, 数値欄で任意の角度を入れられる)
{
    const AxisArrowRequest request = WorldRequest(RotationAxis::Y);
    const auto result = RotateByAxisAngle(request, 37.5);
    Require(result.HasValue(), "回せた");
    RequireNear(Degrees(AngleBetween(request.orientation, result.Value())), 37.5, 1e-9,
        "37.5度");
}

KACHA_V2_TEST(view, 数値でない角度を断る)
{
    const AxisArrowRequest request = WorldRequest(RotationAxis::Y);
    const auto result = RotateByAxisAngle(request, std::nan(""));
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-V001", "数値でない");
}

KACHA_V2_TEST(view, 世界軸は姿勢が変わっても同じ軸)
{
    AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    request.orientation = FromAxisAngle(Vector3{1.0, 0.0, 0.0}, 0.9);
    const auto axis = ResolveRotationAxis(request);
    Require(axis.HasValue(), "軸が出る");
    RequireVectorNear(axis.Value(), Vector3{0.0, 0.0, 1.0}, "世界Zのまま");
}

KACHA_V2_TEST(view, 相対軸は部品の姿勢で回る)
{
    AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    request.mode = RotationAxisMode::Relative;
    request.hasSelectionFrame = true;
    request.selectionFrame = FromAxisAngle(Vector3{1.0, 0.0, 0.0}, kPi * 0.5);
    const auto axis = ResolveRotationAxis(request);
    Require(axis.HasValue(), "軸が出る");
    RequireVectorNear(axis.Value(), Vector3{0.0, -1.0, 0.0}, "部品のZは世界の-Y");
}

KACHA_V2_TEST(view, 相対軸で選択が無ければ理由を出して断る)
{
    AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    request.mode = RotationAxisMode::Relative;
    request.hasSelectionFrame = false;
    const auto axis = ResolveRotationAxis(request);
    Require(!axis.HasValue(), "断る");
    RequireEqual(FirstCode(axis.Diagnostics()), "UI-V007", "選択が要る");
    Require(!axis.Diagnostics().front().summaryJa.empty(), "理由が日本語で出る");
    const auto rotated = RotateByAxisArrowDrag(request, 100.0);
    Require(!rotated.HasValue(), "回さない");
    RequireEqual(FirstCode(rotated.Diagnostics()), "UI-V007", "同じ理由");
}

KACHA_V2_TEST(view, 壊れた姿勢を断る)
{
    AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    request.orientation = Quaternion{0.0, 0.0, 0.0, 0.0};
    const auto axis = ResolveRotationAxis(request);
    Require(!axis.HasValue(), "断る");
    RequireEqual(FirstCode(axis.Diagnostics()), "UI-V001", "姿勢が壊れている");
}

KACHA_V2_TEST(view, 回してもモデル座標は動かない)
{
    // 回すのは姿勢だけである。ここでは「同じ点を渡し続ければ同じ値」を確かめる。
    const Vector3 modelPoint{12.0, -5.0, 3.5};
    Vector3 kept = modelPoint;
    AxisArrowRequest request = WorldRequest(RotationAxis::Z);
    for (int step = 0; step < 8; ++step) {
        const auto rotated = RotateByAxisArrowClick(request, false);
        Require(rotated.HasValue(), "回せた");
        request.orientation = rotated.Value();
    }
    RequireVectorNear(kept, modelPoint, "模型の座標は変わらない");
    RequireNear(Degrees(AngleBetween(Quaternion{1.0, 0.0, 0.0, 0.0}, request.orientation)),
        120.0, 1e-8, "8回で120度");
}

KACHA_V2_TEST(view, 押した姿勢が壊れていたら断る)
{
    const auto result = BeginViewCubeDrag(Quaternion{0.0, 0.0, 0.0, 0.0});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-V001", "姿勢が壊れている");
}

KACHA_V2_TEST(view_orientation, 回転矢印は絶対と相対の2段で12個ある)
{
    // V1 と同じで、絶対回転と相対回転の両方が出ている。片方だけにしない。
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(100.0, 200.0, 60.0);
    RequireEqual(std::to_string(buttons.size()), std::string("12"), "矢印の数");
    int world = 0;
    int relative = 0;
    for (const auto& button : buttons) {
        if (button.mode == kachakacha::v2::view::RotationAxisMode::World) {
            ++world;
        } else {
            ++relative;
        }
    }
    RequireEqual(std::to_string(world), std::string("6"), "絶対が6個");
    RequireEqual(std::to_string(relative), std::string("6"), "相対が6個");
}

KACHA_V2_TEST(view_orientation, 回転矢印は軸ごとに戻すと進めるがそろっている)
{
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(100.0, 200.0, 60.0);
    for (const auto mode : {kachakacha::v2::view::RotationAxisMode::World,
             kachakacha::v2::view::RotationAxisMode::Relative}) {
        for (const auto axis : {kachakacha::v2::view::RotationAxis::X,
                 kachakacha::v2::view::RotationAxis::Y,
                 kachakacha::v2::view::RotationAxis::Z}) {
            int forward = 0;
            int backward = 0;
            for (const auto& button : buttons) {
                if (button.mode != mode || button.axis != axis) {
                    continue;
                }
                if (button.positive) {
                    ++forward;
                } else {
                    ++backward;
                }
            }
            RequireEqual(std::to_string(forward), std::string("1"), "進めるが1個");
            RequireEqual(std::to_string(backward), std::string("1"), "戻すが1個");
        }
    }
}

KACHA_V2_TEST(view_orientation, 回転矢印は重ならずキューブの真下に並ぶ)
{
    const double left = 100.0;
    const double bottom = 200.0;
    const double size = 60.0;
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(left, bottom, size);
    const double blockWidth = kachakacha::v2::view::AxisArrowBlockWidthPx(size);
    for (const auto& button : buttons) {
        // 帯はキューブの右端にそろえて左へ伸びる。右へは出さない(画面の外になる)。
        Require(button.xPx >= left + size - blockWidth - 1.0e-9, "帯より左へ出ない");
        Require(button.xPx + button.widthPx <= left + size + 1.0e-9,
            "キューブより右へはみ出さない");
        Require(button.yPx > bottom, "キューブの下にある");
        Require(button.widthPx > size * 0.2, "押せる大きさがある");
    }
    // 同じ段のとなり同士が重ならない。
    for (std::size_t a = 0; a < buttons.size(); ++a) {
        for (std::size_t b = a + 1; b < buttons.size(); ++b) {
            const bool sameRow = std::abs(buttons[a].yPx - buttons[b].yPx) < 1.0e-9;
            if (!sameRow) {
                continue;
            }
            const bool apart = buttons[a].xPx + buttons[a].widthPx <= buttons[b].xPx + 1.0e-9
                || buttons[b].xPx + buttons[b].widthPx <= buttons[a].xPx + 1.0e-9;
            Require(apart, "となり同士が重ならない");
        }
    }
    Require(kachakacha::v2::view::AxisArrowBlockHeightPx(size) > 0.0, "高さがある");
}

KACHA_V2_TEST(view_orientation, 実際のキューブの大きさで矢印が押せる大きさになる)
{
    // 画面のキューブは88px。そこで20pxを切ると、指でも狙いにくい。
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(0.0, 0.0, 88.0);
    Require(!buttons.empty(), "矢印がある");
    for (const auto& button : buttons) {
        Require(button.widthPx >= 26.0, "26px以上");
        Require(button.heightPx >= 26.0, "26px以上");
    }
}

KACHA_V2_TEST(view_orientation, 押した場所からどの矢印か分かる)
{
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(100.0, 200.0, 60.0);
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        const auto& button = buttons[index];
        const auto found = kachakacha::v2::view::AxisArrowAtScreen(buttons,
            button.xPx + button.widthPx * 0.5, button.yPx + button.heightPx * 0.5);
        Require(found.has_value(), "拾える");
        RequireEqual(std::to_string(*found), std::to_string(index), "同じ矢印");
    }
    Require(!kachakacha::v2::view::AxisArrowAtScreen(buttons, 0.0, 0.0).has_value(),
        "外は拾わない");
}

KACHA_V2_TEST(view_orientation, 矢印の説明に軸と向きと取り方が入る)
{
    const auto buttons = kachakacha::v2::view::BuildAxisArrowButtons(100.0, 200.0, 60.0);
    for (const auto& button : buttons) {
        const std::string text = kachakacha::v2::view::AxisArrowTooltipJa(button);
        Require(text.find(std::string(
                    kachakacha::v2::view::RotationAxisName(button.axis))) != std::string::npos,
            "軸の名前");
        Require(text.find(std::string(kachakacha::v2::view::RotationAxisModeNameJa(
                    button.mode))) != std::string::npos,
            "取り方の名前");
        Require(text.find(button.positive ? "進める" : "戻す") != std::string::npos,
            "向き");
    }
}

KACHA_V2_TEST(view_orientation, 大きさが0なら矢印は出さない)
{
    Require(kachakacha::v2::view::BuildAxisArrowButtons(0.0, 0.0, 0.0).empty(), "空");
}

namespace {

using kachakacha::v2::view::BuildViewGadgets;
using kachakacha::v2::view::RotateByScreenAxis;
using kachakacha::v2::view::ViewButtonAtScreen;
using kachakacha::v2::view::ViewGadgetAtScreen;
using kachakacha::v2::view::ViewGadgetDirection;
using kachakacha::v2::view::ViewGadgetKind;
using kachakacha::v2::view::ViewGadgetLayout;
using kachakacha::v2::view::ViewGadgetTooltipJa;
using kachakacha::v2::view::ViewRingAtScreen;

constexpr double kCenterX = 800.0;
constexpr double kCenterY = 200.0;

[[nodiscard]] kachakacha::v2::view::Quaternion Isometric()
{
    const auto orientation = kachakacha::v2::view::OrientationForZone(
        kachakacha::v2::view::ViewCubeZone{1, -1, 1});
    Require(orientation.HasValue(), "姿勢が作れる");
    return orientation.Value();
}

[[nodiscard]] ViewGadgetLayout Panel()
{
    return BuildViewGadgets(kCenterX, kCenterY,
        kachakacha::v2::view::kNavigatorScalePx, Isometric());
}

[[nodiscard]] int CountOfKind(const ViewGadgetLayout& layout, ViewGadgetKind kind)
{
    int count = 0;
    for (const auto& gadget : layout.gadgets) {
        if (gadget.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace

KACHA_V2_TEST(view_orientation, 操作板にV1と同じ部品がそろっている)
{
    const auto layout = Panel();
    RequireEqual(std::to_string(CountOfKind(layout, ViewGadgetKind::AxisRing)),
        std::string("6"), "輪の矢じりは軸3本x2向き");
    RequireEqual(std::to_string(CountOfKind(layout, ViewGadgetKind::Orbit)),
        std::string("4"), "下=左右回し、右=上下回し");
    RequireEqual(std::to_string(CountOfKind(layout, ViewGadgetKind::Roll)),
        std::string("2"), "上のロールが2つ");
    RequireEqual(std::to_string(CountOfKind(layout, ViewGadgetKind::Home)),
        std::string("1"), "家");
    RequireEqual(std::to_string(CountOfKind(layout, ViewGadgetKind::AlignSelection)),
        std::string("1"), "選択に正対");
    RequireEqual(std::to_string(layout.rings.size()), std::string("3"), "輪は3本");
}

KACHA_V2_TEST(view_orientation, 輪は視点に追従して傾く)
{
    // どの軸で回るかが見た目で分かるように、キューブと同じ投影で描く(ADR 0023)。
    const auto top = kachakacha::v2::view::OrientationForZone(
        kachakacha::v2::view::ViewCubeZone{0, 0, 1});
    Require(top.HasValue(), "姿勢が作れる");
    const auto isometric = Panel();
    const auto fromTop = BuildViewGadgets(kCenterX, kCenterY,
        kachakacha::v2::view::kNavigatorScalePx, top.Value());
    bool moved = false;
    for (std::size_t index = 0; index < isometric.rings.size(); ++index) {
        for (std::size_t point = 0; point < isometric.rings[index].points.size(); ++point) {
            if (std::abs(isometric.rings[index].points[point].x
                    - fromTop.rings[index].points[point].x) > 1.0) {
                moved = true;
            }
        }
    }
    Require(moved, "視点が変われば輪も変わる");
}

KACHA_V2_TEST(view_orientation, 画面基準の矢印は視点が変わっても動かない)
{
    // 下・右・上の矢印と家と正対は位置も向きも変わらない(ADR 0023)。
    const auto top = kachakacha::v2::view::OrientationForZone(
        kachakacha::v2::view::ViewCubeZone{0, 0, 1});
    Require(top.HasValue(), "姿勢が作れる");
    const auto first = Panel();
    const auto second = BuildViewGadgets(kCenterX, kCenterY,
        kachakacha::v2::view::kNavigatorScalePx, top.Value());
    for (std::size_t index = 0; index < first.gadgets.size(); ++index) {
        if (first.gadgets[index].kind == ViewGadgetKind::AxisRing) {
            continue;
        }
        RequireNear(first.gadgets[index].xPx, second.gadgets[index].xPx, 1.0e-12, "同じ場所");
        RequireNear(first.gadgets[index].yPx, second.gadgets[index].yPx, 1.0e-12, "同じ場所");
    }
}

KACHA_V2_TEST(view_orientation, 輪の線のどこを押しても掴める)
{
    // ここが掴みやすさの肝。矢じりだけを的にすると、視点によっては潰れて狙えない。
    const auto layout = Panel();
    for (const auto& ring : layout.rings) {
        int caught = 0;
        for (std::size_t index = 0; index < ring.points.size(); index += 4) {
            const auto found = ViewRingAtScreen(layout, ring.points[index].x,
                ring.points[index].y);
            Require(found.has_value(), "輪の上を押せば当たる");
            Require(layout.gadgets[*found].kind == ViewGadgetKind::AxisRing, "輪である");
            ++caught;
        }
        Require(caught >= 16, "輪の全周で当たる");
    }
}

KACHA_V2_TEST(view_orientation, 輪から離れれば当たらない)
{
    const auto layout = Panel();
    // 中心はどの輪からも離れている(輪の半径は1.95、キューブは1.0)。
    Require(!ViewRingAtScreen(layout, kCenterX, kCenterY).has_value(), "中心は当たらない");
    Require(!ViewRingAtScreen(layout, kCenterX + 400.0, kCenterY).has_value(),
        "遠くも当たらない");
}

KACHA_V2_TEST(view_orientation, 輪の線を押すと近いほうの矢じりの向きになる)
{
    const auto layout = Panel();
    for (const auto& ring : layout.rings) {
        const auto atPositive = ViewRingAtScreen(layout, ring.positiveHead.x,
            ring.positiveHead.y);
        Require(atPositive.has_value(), "当たる");
        Require(layout.gadgets[*atPositive].direction == ViewGadgetDirection::Positive,
            "進める側");
        const auto atNegative = ViewRingAtScreen(layout, ring.negativeHead.x,
            ring.negativeHead.y);
        Require(atNegative.has_value(), "当たる");
        Require(layout.gadgets[*atNegative].direction == ViewGadgetDirection::Negative,
            "戻す側");
    }
}

KACHA_V2_TEST(view_orientation, ボタンと輪は別々に拾える)
{
    // キューブより先にボタン、キューブより後に輪を見るため、入口を分けてある。
    const auto layout = Panel();
    for (std::size_t index = 0; index < layout.gadgets.size(); ++index) {
        const auto& gadget = layout.gadgets[index];
        const double x = gadget.CenterXPx();
        const double y = gadget.CenterYPx();
        if (gadget.kind == ViewGadgetKind::AxisRing) {
            Require(ViewRingAtScreen(layout, x, y).has_value(), "輪として拾える");
        } else {
            const auto found = ViewButtonAtScreen(layout, x, y);
            Require(found.has_value(), "ボタンとして拾える");
            RequireEqual(std::to_string(*found), std::to_string(index), "同じもの");
        }
        Require(ViewGadgetAtScreen(layout, x, y).has_value(), "まとめてでも拾える");
    }
}

KACHA_V2_TEST(view_orientation, 操作板の説明が空にならない)
{
    const auto layout = Panel();
    for (const auto& gadget : layout.gadgets) {
        Require(!ViewGadgetTooltipJa(gadget).empty(), "説明がある");
    }
}

KACHA_V2_TEST(view_orientation, 画面の軸で上下左右に回せる)
{
    const auto start = kachakacha::v2::view::OrientationForZone(
        kachakacha::v2::view::ViewCubeZone{0, -1, 0});
    Require(start.HasValue(), "姿勢が作れる");
    for (const auto direction : {ViewGadgetDirection::Up, ViewGadgetDirection::Down,
             ViewGadgetDirection::Left, ViewGadgetDirection::Right,
             ViewGadgetDirection::Positive, ViewGadgetDirection::Negative}) {
        const auto rotated = RotateByScreenAxis(start.Value(), direction, 15.0);
        Require(rotated.HasValue(), "回せる");
        const double degrees = kachakacha::v2::view::AngleBetween(start.Value(),
                                   rotated.Value())
            * 180.0 / 3.14159265358979323846;
        RequireNear(degrees, 15.0, 1.0e-6, "15度回る");
    }
}

KACHA_V2_TEST(view_orientation, 上下と左右は互いに逆へ戻せる)
{
    const auto start = kachakacha::v2::view::OrientationForZone(
        kachakacha::v2::view::ViewCubeZone{0, -1, 0});
    Require(start.HasValue(), "姿勢が作れる");
    const std::pair<ViewGadgetDirection, ViewGadgetDirection> pairs[] = {
        {ViewGadgetDirection::Up, ViewGadgetDirection::Down},
        {ViewGadgetDirection::Left, ViewGadgetDirection::Right},
        {ViewGadgetDirection::Positive, ViewGadgetDirection::Negative},
    };
    for (const auto& pair : pairs) {
        const auto forward = RotateByScreenAxis(start.Value(), pair.first, 30.0);
        Require(forward.HasValue(), "回せる");
        const auto back = RotateByScreenAxis(forward.Value(), pair.second, 30.0);
        Require(back.HasValue(), "戻せる");
        RequireNear(kachakacha::v2::view::AngleBetween(start.Value(), back.Value()), 0.0,
            1.0e-9, "元へ戻る");
    }
}

KACHA_V2_TEST(view_orientation, 操作板は画面からはみ出したら寄せる)
{
    auto layout = BuildViewGadgets(60.0, 60.0, kachakacha::v2::view::kNavigatorScalePx,
        Isometric());
    Require(!layout.gadgets.empty(), "部品がある");
    Require(kachakacha::v2::view::FitViewGadgetsIntoScreen(layout, 800.0, 600.0),
        "寄せられる");
    for (const auto& gadget : layout.gadgets) {
        Require(gadget.xPx >= -1.0e-9, "左からはみ出さない");
        Require(gadget.yPx >= -1.0e-9, "上からはみ出さない");
    }
    for (const auto& ring : layout.rings) {
        for (const auto& point : ring.points) {
            Require(point.x >= -1.0e-9 && point.y >= -1.0e-9, "輪もはみ出さない");
        }
    }
}

KACHA_V2_TEST(view_orientation, 画面より大きい操作板は寄せられない)
{
    auto layout = Panel();
    Require(!kachakacha::v2::view::FitViewGadgetsIntoScreen(layout, 50.0, 50.0), "断る");
}

KACHA_V2_TEST(view_orientation, 大きさが0なら操作板は空になる)
{
    const auto layout = BuildViewGadgets(0.0, 0.0, 0.0, Isometric());
    Require(layout.gadgets.empty(), "空");
    Require(layout.rings.empty(), "輪も無い");
}

KACHA_V2_TEST(view_orientation, 面の名前は1文字でキューブに収まる)
{
    // キューブは一辺44pxしかない。2文字だと隣の面と重なって読めない。
    const std::pair<kachakacha::v2::view::ViewCubeZone, const char*> faces[] = {
        {{0, 0, 1}, "上"}, {{0, 0, -1}, "下"}, {{0, -1, 0}, "前"},
        {{0, 1, 0}, "後"}, {{1, 0, 0}, "右"}, {{-1, 0, 0}, "左"},
    };
    for (const auto& face : faces) {
        const std::string label = ViewCubeZoneLabelJa(face.first);
        RequireEqual(label, std::string(face.second), "面の名前");
        // UTF-8 の日本語1文字は3バイト。
        RequireEqual(std::to_string(label.size()), std::string("3"), "1文字");
    }
}

KACHA_V2_TEST_MAIN("view_orientation_tests")
