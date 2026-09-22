// 部品の形状編集(シェル・分割)の層の試験(kernel/OcctShellSplit、matrix P-13)。
//
// シェル: 箱の上の面を抜いて肉厚 t を残すと、体積は 外 − 内((X−2t)(Y−2t)(Z−t))。
// 分割: 平面で 2 つに分けると、両側の体積は式どおりで、和は元と同じ。U 字を横に切ると
// 片側が 2 つの塊になる(数えて返す)。面は面の上の点で指し、辺の上(2 枚の面にまたがる点)は
// 取り違えるので断る。平面が部品を通らなければ断る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctShellSplit.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::kernel::NearestSolidFace;
using kachakacha::v2::kernel::ShellSolid;
using kachakacha::v2::kernel::SolidFaceAt;
using kachakacha::v2::kernel::SplitSolidByPlane;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[maybe_unused]] [[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! xy の閉じた折れ線を z = 0 から高さぶん押し出す。作れなければ空の番号。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Prism(const std::vector<Vector3>& corners,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    for (std::size_t index = 0; index < corners.size(); ++index) {
        profile.segments.push_back(Line(corners[index], corners[(index + 1) % corners.size()]));
    }
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    if (!analysis.HasValue()) {
        return {};
    }
    const auto built = BuildExtrude(request, analysis.Value(), Tolerance(), {});
    if (!built.HasValue() || built.Value().parts.empty()) {
        return {};
    }
    return built.Value().parts.front().handle;
}

[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x1, double y1, double height)
{
    return Prism({{0, 0, 0}, {x1, 0, 0}, {x1, y1, 0}, {0, y1, 0}}, height);
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_shell_split_absent, カーネルが無い版はシェルも分割も断る)
{
    const auto shell = ShellSolid({}, {Vector3{}}, 1.0, Tolerance());
    Require(!shell.HasValue(), "シェルを作れたことにしない");
    RequireEqual(FirstCode(shell.Diagnostics()), "KER-H004", "カーネル不在の診断コード");
    const auto split = SplitSolidByPlane({}, Vector3{}, Vector3{0, 0, 1}, Tolerance());
    Require(!split.HasValue(), "分割を作れたことにしない");
    RequireEqual(FirstCode(split.Diagnostics()), "KER-H004", "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_shell_split, 箱の上の面を抜いてシェルにすると体積が式どおり)
{
    // 40 × 20 × 30。上の面(z = 30)を抜いて肉厚 2 を残す。内側は 36 × 16 × 28。
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto top = NearestSolidFace(box, Vector3{20.0, 10.0, 30.3});
    Require(top.HasValue(), "押した点に近い面が拾える: " + top.FirstSummaryJa());
    RequireNear(top.Value().point.z, 30.0, 1.0e-6, "面の上へ落とした点");
    Require(!top.Value().outline.empty(), "面の縁(下見)がある");
    const auto shell = ShellSolid(box, {top.Value().point}, 2.0, Tolerance());
    Require(shell.HasValue(), "シェルにできる: " + shell.FirstSummaryJa());
    RequireNear(shell.Value().previousVolumeMm3, 24000.0, 1.0e-3, "前の体積");
    RequireNear(shell.Value().volumeMm3, 24000.0 - 36.0 * 16.0 * 28.0, 1.0e-2,
        "残る体積は 外 − (X−2t)(Y−2t)(Z−t)");
}

KACHA_V2_TEST(kernel_shell_split, 辺のそばを押しても面の内側の点を残す)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    // 上の面と手前の面(y = 0)の境のすぐそば。上の面のほうがわずかに近い。
    const auto near = NearestSolidFace(box, Vector3{20.0, 0.002, 29.999});
    Require(near.HasValue(), "面が拾える: " + near.FirstSummaryJa());
    RequireNear(near.Value().point.z, 30.0, 1.0e-6, "上の面の上");
    Require(near.Value().point.y > 0.4, "手前の面から 0.4 mm より離れた点へ寄せる(許容差を上限にしても取り違えない)");
    GeometryTolerance loose = Tolerance();
    loose.interactiveJoinMm = 0.1;   // 画面で選べる上限
    Require(SolidFaceAt(box, near.Value().point, loose).HasValue(), "許容差を上限にしても同じ面を選び直せる");
    const auto again = SolidFaceAt(box, near.Value().point, Tolerance());
    Require(again.HasValue(), "残した点で同じ面を選び直せる: " + again.FirstSummaryJa());
    Require(again.Value().faceIndex == near.Value().faceIndex && near.Value().faceIndex >= 0,
        "選び直した面は押した面と同じ番号");
    // 辺の上そのもの(2 枚の面から同じ距離)は、どちらの面か決まらないので断る。
    const auto onEdge = SolidFaceAt(box, Vector3{20.0, 0.0, 30.0}, Tolerance());
    Require(!onEdge.HasValue() && FirstCode(onEdge.Diagnostics()) == "KER-H002",
        "辺の上の点では面を決めない: " + onEdge.FirstSummaryJa());
}

KACHA_V2_TEST(kernel_shell_split, 無い面と厚すぎる肉厚のシェルは断る)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto missing = ShellSolid(box, {Vector3{20.0, 10.0, 15.0}}, 1.0, Tolerance());
    Require(!missing.HasValue() && FirstCode(missing.Diagnostics()) == "KER-H002",
        "箱の中の点には面が無いと言う: " + missing.FirstSummaryJa());
    const auto none = ShellSolid(box, {}, 1.0, Tolerance());
    Require(!none.HasValue() && FirstCode(none.Diagnostics()) == "KER-H002", "面が無ければ断る");
    const auto thick = ShellSolid(box, {Vector3{20.0, 10.0, 30.0}}, 15.0, Tolerance());
    Require(!thick.HasValue(), "幅の半分を超える肉厚は作れたことにしない: " + thick.FirstSummaryJa());
    const auto zero = ShellSolid(box, {Vector3{20.0, 10.0, 30.0}}, 0.0, Tolerance());
    Require(!zero.HasValue() && FirstCode(zero.Diagnostics()) == "KER-H001", "肉厚 0 は断る");
}

KACHA_V2_TEST(kernel_shell_split, 平面で2つに分けると両側の体積は式どおり)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto split = SplitSolidByPlane(box, Vector3{10.0, 0.0, 0.0}, Vector3{2.0, 0.0, 0.0},
        Tolerance());
    Require(split.HasValue(), "分けられる: " + split.FirstSummaryJa());
    RequireNear(split.Value().positiveVolumeMm3, 30.0 * 20.0 * 30.0, 1.0e-3, "法線の側(x > 10)");
    RequireNear(split.Value().negativeVolumeMm3, 10.0 * 20.0 * 30.0, 1.0e-3, "反対の側(x < 10)");
    RequireEqual(std::to_string(split.Value().positiveSolidCount), "1", "法線の側は 1 つ");
    RequireEqual(std::to_string(split.Value().negativeSolidCount), "1", "反対の側は 1 つ");
    Require(split.Value().positive.Valid() && split.Value().negative.Valid(), "両側の形が残る");
}

KACHA_V2_TEST(kernel_shell_split, U字を横に切ると片側が2つの塊になる)
{
    // 30 × 20 の U(真ん中の溝は x 10〜20、y 10〜20)。y = 15 で切ると上は 2 本の脚。
    const auto u = Prism({{0, 0, 0}, {30, 0, 0}, {30, 20, 0}, {20, 20, 0}, {20, 10, 0},
        {10, 10, 0}, {10, 20, 0}, {0, 20, 0}}, 5.0);
    Require(u.Valid(), "U 字が作れること");
    const auto split = SplitSolidByPlane(u, Vector3{0.0, 15.0, 0.0}, Vector3{0.0, 1.0, 0.0},
        Tolerance());
    Require(split.HasValue(), "分けられる: " + split.FirstSummaryJa());
    RequireEqual(std::to_string(split.Value().positiveSolidCount), "2", "脚の側は 2 つの塊");
    RequireEqual(std::to_string(split.Value().negativeSolidCount), "1", "底の側は 1 つ");
    RequireNear(split.Value().positiveVolumeMm3, 2.0 * 10.0 * 5.0 * 5.0, 1.0e-3, "脚の側の体積");
}

KACHA_V2_TEST(kernel_shell_split, 部品を通らない平面では分けない)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto outside = SplitSolidByPlane(box, Vector3{50.0, 0.0, 0.0}, Vector3{1.0, 0.0, 0.0},
        Tolerance());
    Require(!outside.HasValue() && FirstCode(outside.Diagnostics()) == "KER-H005",
        "外れた平面は断る: " + outside.FirstSummaryJa());
    const auto face = SplitSolidByPlane(box, Vector3{0.0, 0.0, 30.0}, Vector3{0.0, 0.0, 1.0},
        Tolerance());
    Require(!face.HasValue() && FirstCode(face.Diagnostics()) == "KER-H005",
        "面と重なる平面は分けたことにしない: " + face.FirstSummaryJa());
    const auto zero = SplitSolidByPlane(box, Vector3{10.0, 0.0, 0.0}, Vector3{}, Tolerance());
    Require(!zero.HasValue() && FirstCode(zero.Diagnostics()) == "KER-H001", "向きの無い平面は断る");
}

#endif

KACHA_V2_TEST_MAIN("kernel_shell_split_tests")
