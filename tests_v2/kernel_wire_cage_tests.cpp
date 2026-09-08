// ワイヤーかごから立体を作る(AT-GEO-013 / GEO-S005 / GEO-S008)。
//
// 大事なのは1つ。2つの閉じた立体を同時に確定したら、
// 中身が2つ入った1つの部品ではなく、2つの部品が出ることである。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctWireCage.h"
#include "kachakacha/modeling/WireCage.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildWireCageParts;
using kachakacha::v2::modeling::AnalyzeWireCage;
using kachakacha::v2::modeling::CageEdgeInput;
using kachakacha::v2::modeling::PlanWireCageParts;
using kachakacha::v2::modeling::WireCageAnalysis;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

//! 断られた理由を、そのまま試験の失敗文へ出す。
//! 「作れる」とだけ出ても、PC の側でしか再現しないものは追えない。
[[nodiscard]] std::string Why(const std::vector<Diagnostic>& diagnostics)
{
    if (diagnostics.empty()) {
        return "(理由なし)";
    }
    return diagnostics.front().code + " " + diagnostics.front().summaryJa + " / "
        + diagnostics.front().detailsJa;
}

struct EdgeMaker {
    DeterministicIdGenerator ids{5};
    std::vector<CageEdgeInput> edges;

    void Add(Vector3 from, Vector3 to)
    {
        edges.push_back(CageEdgeInput{EntityId(ids.Next()), SegmentId(ids.Next()),
            CurveSegment::MakeLine(from, to).Value()});
    }

    //! 角柱1つ分の12辺。
    void AddBox(double x0, double y0, double z0, double sx, double sy, double sz)
    {
        const double x1 = x0 + sx;
        const double y1 = y0 + sy;
        const double z1 = z0 + sz;
        // 下の四角
        Add({x0, y0, z0}, {x1, y0, z0});
        Add({x1, y0, z0}, {x1, y1, z0});
        Add({x1, y1, z0}, {x0, y1, z0});
        Add({x0, y1, z0}, {x0, y0, z0});
        // 上の四角
        Add({x0, y0, z1}, {x1, y0, z1});
        Add({x1, y0, z1}, {x1, y1, z1});
        Add({x1, y1, z1}, {x0, y1, z1});
        Add({x0, y1, z1}, {x0, y0, z1});
        // 縦
        Add({x0, y0, z0}, {x0, y0, z1});
        Add({x1, y0, z0}, {x1, y0, z1});
        Add({x1, y1, z0}, {x1, y1, z1});
        Add({x0, y1, z0}, {x0, y1, z1});
    }
};

} // namespace

KACHA_V2_TEST(kernel_cage, 選んだ数だけ部品の計画が出る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    maker.AddBox(50, 0, 0, 10, 10, 10);
    const GeometryTolerance tolerance;
    const auto analysis = AnalyzeWireCage(maker.edges, tolerance);
    Require(analysis.HasValue(), "調べられる");
    RequireEqual(std::to_string(analysis.Value().shells.size()), "2", "立体は2つ");

    // 2つ同時に確定する。
    const auto plan = PlanWireCageParts(analysis.Value(), {0, 1});
    Require(plan.HasValue(), "計画できる");
    RequireEqual(std::to_string(plan.Value().size()), "2",
        "1つの部品ではなく2つの部品になる");
    Require(plan.Value()[0].shellIndex != plan.Value()[1].shellIndex, "別の立体");
    for (const auto& part : plan.Value()) {
        RequireEqual(std::to_string(part.faceKeys.size()), "6", "角柱は6面");
        RequireNear(part.volumeMm3, 1000.0, 1e-6, "体積");
    }
}

KACHA_V2_TEST(kernel_cage, 1つだけ選べば1つだけ出る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    maker.AddBox(50, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto plan = PlanWireCageParts(analysis.Value(), {1});
    Require(plan.HasValue(), "計画できる");
    RequireEqual(std::to_string(plan.Value().size()), "1", "1つ");
    RequireEqual(std::to_string(plan.Value()[0].shellIndex), "1", "選んだ方");
}

KACHA_V2_TEST(kernel_cage, 何も選ばなければ断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto refused = PlanWireCageParts(analysis.Value(), {});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S011", "選ばれていない");
}

KACHA_V2_TEST(kernel_cage, 同じ立体を2度選んだら断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto refused = PlanWireCageParts(analysis.Value(), {0, 0});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S010", "2度選んだ");
}

KACHA_V2_TEST(kernel_cage, 無い立体を選んだら断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto refused = PlanWireCageParts(analysis.Value(), {9});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S011", "候補にない");
}

#ifdef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_cage, 2つ同時に確定すると2つの立体が出来る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    maker.AddBox(50, 0, 0, 20, 10, 10);
    const GeometryTolerance tolerance;
    const auto analysis = AnalyzeWireCage(maker.edges, tolerance);
    Require(analysis.HasValue(), "調べられる");
    const auto plan = PlanWireCageParts(analysis.Value(), {0, 1});
    Require(plan.HasValue(), "計画できる");

    const auto built = BuildWireCageParts(maker.edges, analysis.Value(), plan.Value(),
        tolerance);
    Require(built.HasValue(), "作れる: " + Why(built.Diagnostics()));
    RequireEqual(std::to_string(built.Value().size()), "2", "2つの立体");
    // 別々の立体なので、番号も別。
    Require(built.Value()[0].handle.value != built.Value()[1].handle.value,
        "別の形として持たれる");
    for (const auto& part : built.Value()) {
        RequireEqual(std::to_string(part.faceCount), "6", "角柱は6面");
        Require(part.volumeMm3 > 0.0, "体積がある");
        RequireEqual(std::to_string(part.faceKeys.size()), "6", "面のキーも6つ");
    }
    // 体積はそれぞれの角柱のもの。合算した1つの立体になっていない。
    const double first = built.Value()[0].volumeMm3;
    const double second = built.Value()[1].volumeMm3;
    Require(std::abs(first - 1000.0) < 1e-6 || std::abs(first - 2000.0) < 1e-6,
        "1000 か 2000");
    Require(std::abs(second - 1000.0) < 1e-6 || std::abs(second - 2000.0) < 1e-6,
        "1000 か 2000");
    Require(std::abs(first - second) > 1.0, "2つは別の大きさ");
}

KACHA_V2_TEST(kernel_cage, 作った立体の面のキーがcoreの決めたものと同じ)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const GeometryTolerance tolerance;
    const auto analysis = AnalyzeWireCage(maker.edges, tolerance);
    const auto plan = PlanWireCageParts(analysis.Value(), {0});
    const auto built = BuildWireCageParts(maker.edges, analysis.Value(), plan.Value(),
        tolerance);
    Require(built.HasValue(), "作れる: " + Why(built.Diagnostics()));
    RequireEqual(std::to_string(built.Value().front().faceKeys.size()),
        std::to_string(plan.Value().front().faceKeys.size()), "数が同じ");
    for (std::size_t index = 0; index < plan.Value().front().faceKeys.size(); ++index) {
        RequireEqual(built.Value().front().faceKeys[index],
            plan.Value().front().faceKeys[index].ToString(), "同じキー");
    }
}

KACHA_V2_TEST(kernel_cage, 何も選ばずに作ろうとしたら断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto refused = BuildWireCageParts(maker.edges, analysis.Value(), {},
        GeometryTolerance{});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S008", "カーネル側で断る");
}

KACHA_V2_TEST(kernel_cage, 候補にない立体を作ろうとしたら断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    std::vector<kachakacha::v2::modeling::WireCagePart> bogus(1);
    bogus[0].shellIndex = 99;
    const auto refused = BuildWireCageParts(maker.edges, analysis.Value(), bogus,
        GeometryTolerance{});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S008", "候補にない");
}

#else

KACHA_V2_TEST(kernel_cage, カーネルが無いときは作らずに断る)
{
    EdgeMaker maker;
    maker.AddBox(0, 0, 0, 10, 10, 10);
    const auto analysis = AnalyzeWireCage(maker.edges, GeometryTolerance{});
    const auto plan = PlanWireCageParts(analysis.Value(), {0});
    const auto refused = BuildWireCageParts(maker.edges, analysis.Value(), plan.Value(),
        GeometryTolerance{});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-S008", "カーネルが無い");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_wire_cage_tests")
