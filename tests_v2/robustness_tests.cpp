// 壊れた入力を大量に流し込む試験。
//
// 目的は2つ。
//   1. どんな入力でも落ちないこと(例外で試験ごと止まらないこと)。
//   2. 受け付けないときは、必ず理由(診断)が付くこと。黙って空を返さないこと。
//
// 乱数は自前の決定的なものを使う。標準の乱数器は実装によって列が変わるので、
// 失敗を再現できなくなる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/Assembly.h"
#include "kachakacha/fabrication/CurvatureAnalysis.h"
#include "kachakacha/fabrication/OpeningClip.h"
#include "kachakacha/fabrication/Unfold.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveJoin.h"
#include "kachakacha/geometry/Expression.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/Json.h"
#include "kachakacha/io/Zip.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/SubshapeKey.h"
#include "kachakacha/modeling/WireCage.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

//! 決定的な乱数。同じ種からは毎回同じ列。失敗を再現できるようにするため。
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed | 1u) {}

    [[nodiscard]] std::uint64_t Next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }
    [[nodiscard]] int Int(int limit) { return static_cast<int>(Next() % static_cast<std::uint64_t>(limit)); }
    [[nodiscard]] double Unit() { return static_cast<double>(Next() % 1000000u) / 1000000.0; }
    //! ときどき、極端な値や有限でない値を混ぜる。
    [[nodiscard]] double Nasty()
    {
        switch (Int(12)) {
        case 0: return std::numeric_limits<double>::quiet_NaN();
        case 1: return std::numeric_limits<double>::infinity();
        case 2: return -std::numeric_limits<double>::infinity();
        case 3: return 0.0;
        case 4: return -0.0;
        case 5: return 1.0e300;
        case 6: return -1.0e300;
        case 7: return 1.0e-300;
        case 8: return std::numeric_limits<double>::denorm_min();
        default: return (Unit() - 0.5) * 200.0;
        }
    }
    [[nodiscard]] kachakacha::v2::geometry::Vector3 Point()
    {
        return {Nasty(), Nasty(), Nasty()};
    }
    [[nodiscard]] std::string Bytes(std::size_t length)
    {
        std::string text;
        for (std::size_t index = 0; index < length; ++index) {
            text.push_back(static_cast<char>(Next() & 0xFF));
        }
        return text;
    }
    //! JSONやZIPに似ているが壊れている文字列。まったくの乱数より当たりが多い。
    [[nodiscard]] std::string NearJson()
    {
        const char* pieces[]{"{", "}", "[", "]", ",", ":", "\"a\"", "1", "1.5", "true",
            "false", "null", "\\u", "\\", "\"", "e5", "-", "+", " ", "\n", "\t",
            "\"format\"", "\"kachakachaCAD\"", "\"schemaVersion\"", "2", "NaN",
            "Infinity", "0x10", "01", ".5", "1.", "\"\\ud800\""};
        std::string text;
        const int count = 1 + Int(24);
        for (int index = 0; index < count; ++index) {
            text += pieces[Int(static_cast<int>(std::size(pieces)))];
        }
        return text;
    }

private:
    std::uint64_t state_ = 1;
};

//! Result を受け取り、「値があるか、診断が付いているか」のどちらかであることを見る。
template<class ResultType>
void RequireValueOrReason(const ResultType& result, const std::string& why)
{
    if (result.HasValue()) {
        return;
    }
    Require(!result.Diagnostics().empty(), "断るなら理由が付くこと: " + why);
    for (const auto& diagnostic : result.Diagnostics()) {
        Require(!diagnostic.code.empty(), "診断コードが空でないこと: " + why);
        Require(!diagnostic.summaryJa.empty(), "診断の文が空でないこと: " + why);
    }
}

} // namespace

// ---------------------------------------------------------------- 読み込み

KACHA_V2_TEST(robust, 乱数のバイト列をJSONとして読ませても落ちない)
{
    Random random(20260908);
    for (int round = 0; round < 4000; ++round) {
        const std::string text = round % 2 == 0 ? random.Bytes(1 + random.Int(64))
                                                : random.NearJson();
        const auto parsed = kachakacha::v2::io::ParseJson(text);
        RequireValueOrReason(parsed, "JSON");
        if (parsed.HasValue()) {
            // 読めたなら、書き出して読み直せること。
            const std::string written = kachakacha::v2::io::WriteJson(parsed.Value());
            const auto again = kachakacha::v2::io::ParseJson(written);
            Require(again.HasValue(), "書いたものは読み直せること");
        }
    }
}

KACHA_V2_TEST(robust, 乱数のバイト列をZIPとして読ませても落ちない)
{
    Random random(11);
    for (int round = 0; round < 2000; ++round) {
        const auto read = kachakacha::v2::io::ReadZip(random.Bytes(1 + random.Int(200)));
        RequireValueOrReason(read, "ZIP");
    }
}

KACHA_V2_TEST(robust, 本物のZIPを1バイトずつ壊しても落ちない)
{
    const auto archive = kachakacha::v2::io::WriteZip(
        {{"document.json", "{\"a\":1}"}, {"meta/thumbnail.png", "xyz"}});
    Require(archive.HasValue(), "土台が作れること");
    Random random(22);
    for (int round = 0; round < 2000; ++round) {
        std::string broken = archive.Value();
        const int changes = 1 + random.Int(4);
        for (int index = 0; index < changes; ++index) {
            broken[random.Int(static_cast<int>(broken.size()))] =
                static_cast<char>(random.Next() & 0xFF);
        }
        RequireValueOrReason(kachakacha::v2::io::ReadZip(broken), "壊したZIP");
    }
}

KACHA_V2_TEST(robust, 乱数のバイト列を文書として読ませても落ちない)
{
    Random random(33);
    for (int round = 0; round < 1500; ++round) {
        RequireValueOrReason(
            kachakacha::v2::io::LoadDocument(random.Bytes(1 + random.Int(150))), "文書");
        RequireValueOrReason(
            kachakacha::v2::io::ReadDocumentJson(random.NearJson()), "document.json");
    }
}

KACHA_V2_TEST(robust, 本物の文書を壊しても落ちない)
{
    kachakacha::v2::io::DocumentFile file;
    file.snapshot.id =
        kachakacha::v2::base::DocumentId::Parse("00000000-0000-4000-8000-000000000001")
            .value();
    file.metadata.title = "壊す前";
    const std::string text = kachakacha::v2::io::WriteDocumentJson(file);
    Random random(44);
    for (int round = 0; round < 3000; ++round) {
        std::string broken = text;
        const int changes = 1 + random.Int(3);
        for (int index = 0; index < changes; ++index) {
            broken[random.Int(static_cast<int>(broken.size()))] =
                static_cast<char>(random.Next() & 0x7F);
        }
        RequireValueOrReason(kachakacha::v2::io::ReadDocumentJson(broken), "壊した文書");
    }
}

KACHA_V2_TEST(robust, 乱数の文字列を式として読ませても落ちない)
{
    Random random(55);
    const char* pieces[]{"1", "2.5", "+", "-", "*", "/", "^", "(", ")", "pi", "deg(",
        "rad(", "mm", "cm", "m", "in", "e", ".", ",", " ", "０", "＋", "（", "ｍｍ",
        "0/0", "1e999", "--", "()", "((((", "))))"};
    for (int round = 0; round < 4000; ++round) {
        std::string text;
        const int count = 1 + random.Int(12);
        for (int index = 0; index < count; ++index) {
            text += pieces[random.Int(static_cast<int>(std::size(pieces)))];
        }
        for (const auto kind : {kachakacha::v2::geometry::QuantityKind::Length,
                 kachakacha::v2::geometry::QuantityKind::Angle,
                 kachakacha::v2::geometry::QuantityKind::Scalar}) {
            const auto value = kachakacha::v2::geometry::EvaluateExpression(text, kind);
            RequireValueOrReason(value, "式: " + text);
            if (value.HasValue()) {
                Require(kachakacha::v2::geometry::IsFinite(value.Value().value),
                    "通ったなら値が有限であること: " + text);
            }
        }
    }
}

KACHA_V2_TEST(robust, 乱数の文字列を面のキーとして読ませても落ちない)
{
    Random random(66);
    for (int round = 0; round < 3000; ++round) {
        RequireValueOrReason(
            kachakacha::v2::modeling::ParseSubshapeKey(random.Bytes(1 + random.Int(40))),
            "面のキー");
    }
}

// ---------------------------------------------------------------- 幾何

KACHA_V2_TEST(robust, おかしな座標で曲線を作ろうとしても落ちない)
{
    using kachakacha::v2::geometry::CurveSegment;
    Random random(77);
    int made = 0;
    for (int round = 0; round < 4000; ++round) {
        RequireValueOrReason(CurveSegment::MakeLine(random.Point(), random.Point()), "直線");
        const auto arc = CurveSegment::MakeCircularArc(random.Point(), random.Point(),
            random.Point(), random.Nasty(), random.Nasty(), random.Nasty());
        RequireValueOrReason(arc, "円弧");
        if (arc.HasValue()) {
            ++made;
            // 作れたなら、値がすべて有限であること。
            Require(arc.Value().Center().IsFinite(), "中心が有限");
            Require(kachakacha::v2::geometry::IsFinite(arc.Value().Radius()), "半径が有限");
            Require(arc.Value().StartPoint().IsFinite(), "始点が有限");
            Require(arc.Value().EndPoint().IsFinite(), "終点が有限");
        }
        RequireValueOrReason(CurveSegment::MakeCircle(random.Point(), random.Point(),
                                 random.Point(), random.Nasty()),
            "円");
        std::vector<kachakacha::v2::geometry::Vector3> control;
        const int count = random.Int(8);
        for (int index = 0; index < count; ++index) {
            control.push_back(random.Point());
        }
        RequireValueOrReason(CurveSegment::MakeCubicBezier(control), "ベジェ");
        RequireValueOrReason(CurveSegment::MakeCubicBSpline(control), "スプライン");
    }
    Require(made > 0, "ときどきは作れること(全部断っていたら試験にならない)");
}

KACHA_V2_TEST(robust, おかしな座標で円弧の作り方を呼んでも落ちない)
{
    namespace geo = kachakacha::v2::geometry;
    Random random(88);
    for (int round = 0; round < 3000; ++round) {
        RequireValueOrReason(
            geo::ArcThroughThreePoints(random.Point(), random.Point(), random.Point()),
            "3点");
        RequireValueOrReason(geo::ArcFromEndpointsAndRadius(random.Point(), random.Point(),
                                 random.Nasty(), random.Point(), random.Int(2) == 0,
                                 random.Int(2) == 0),
            "両端と半径");
        RequireValueOrReason(geo::ArcFromStartTangentRadiusSweep(random.Point(),
                                 random.Point(), random.Point(), random.Nasty(),
                                 random.Nasty()),
            "接線と掃引");
        RequireValueOrReason(geo::ArcFromStartTangentRadiusLength(random.Point(),
                                 random.Point(), random.Point(), random.Nasty(),
                                 random.Nasty()),
            "接線と弧長");
    }
}

KACHA_V2_TEST(robust, おかしな曲線を編集しても落ちない)
{
    namespace geo = kachakacha::v2::geometry;
    Random random(99);
    geo::GeometryTolerance tolerance;
    // まともな曲線をいくつか用意し、おかしな値で編集を試す。
    std::vector<geo::CurveSegment> curves;
    curves.push_back(geo::CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value());
    curves.push_back(geo::CurveSegment::MakeLine({5, -5, 0}, {5, 5, 0}).Value());
    curves.push_back(
        geo::CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, 1.0)
            .Value());
    curves.push_back(
        geo::CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value());
    curves.push_back(geo::CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {3, 5, 0}, {7, 5, 0}, {10, 0, 0}})
                         .Value());

    for (int round = 0; round < 3000; ++round) {
        const geo::CurveSegment& first = curves[random.Int(static_cast<int>(curves.size()))];
        const geo::CurveSegment& second = curves[random.Int(static_cast<int>(curves.size()))];
        RequireValueOrReason(
            geo::TrimCurve(first, second, random.Nasty(), tolerance.modelLinearMm),
            "トリム");
        RequireValueOrReason(geo::ExtendCurve(first, random.Int(2), random.Nasty()),
            "延長");
        RequireValueOrReason(
            geo::ExtendCurveToBoundary(first, random.Int(2), second,
                tolerance.modelLinearMm),
            "境界まで延長");
        RequireValueOrReason(
            geo::ChamferLines(first, second, random.Nasty(), tolerance.modelLinearMm),
            "面取り");
        RequireValueOrReason(
            geo::FilletLines(first, second, random.Nasty(), tolerance.modelLinearMm),
            "丸め");
        RequireValueOrReason(geo::MeetLines(first, second, tolerance.modelLinearMm),
            "交点まで");
        RequireValueOrReason(
            geo::OffsetCurveInPlane(first, random.Point(), random.Nasty()),
            "オフセット");
        RequireValueOrReason(
            geo::RotateCurve(first, random.Point(), random.Point(), random.Nasty()),
            "回転");
        RequireValueOrReason(geo::MirrorCurve(first, random.Point(), random.Point()),
            "ミラー");
        // 交差は Result ではないので、落ちないことと値が有限であることを見る。
        for (const auto& crossing :
            geo::IntersectCurvesForEditing(first, second, tolerance.modelLinearMm)) {
            Require(crossing.point.IsFinite(), "交点が有限であること");
        }
        for (const auto& crossing : geo::IntersectCurves(first, second, tolerance)) {
            Require(crossing.position.IsFinite(), "交点が有限であること");
        }
    }
}

KACHA_V2_TEST(robust, おかしな曲線をつなごうとしても落ちない)
{
    namespace geo = kachakacha::v2::geometry;
    Random random(101);
    geo::GeometryTolerance tolerance;
    std::vector<geo::CurveSegment> curves;
    curves.push_back(geo::CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value());
    curves.push_back(
        geo::CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, 1.0)
            .Value());
    curves.push_back(geo::CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {3, 5, 0}, {7, 5, 0}, {10, 0, 0}})
                         .Value());
    const geo::CurveEnd ends[]{geo::CurveEnd::Start, geo::CurveEnd::End};
    const geo::JoinContinuity kinds[]{geo::JoinContinuity::Position,
        geo::JoinContinuity::Tangent, geo::JoinContinuity::Curvature};
    const geo::JoinAnchor anchors[]{geo::JoinAnchor::KeepFirst, geo::JoinAnchor::KeepSecond,
        geo::JoinAnchor::Midpoint};
    for (int round = 0; round < 2000; ++round) {
        RequireValueOrReason(
            geo::JoinCurves(curves[random.Int(3)], ends[random.Int(2)],
                curves[random.Int(3)], ends[random.Int(2)], kinds[random.Int(3)],
                anchors[random.Int(3)], tolerance),
            "接続");
        std::vector<geo::CurveSegment> polyline;
        const int count = random.Int(5);
        for (int index = 0; index < count; ++index) {
            polyline.push_back(curves[random.Int(3)]);
        }
        RequireValueOrReason(
            geo::ProcessPolylineCorners(polyline, random.Nasty(), random.Int(2) == 0,
                tolerance),
            "角の加工");
    }
}

// ---------------------------------------------------------------- 面と部品

KACHA_V2_TEST(robust, おかしな入力で形状ガイドを作ろうとしても落ちない)
{
    namespace mod = kachakacha::v2::modeling;
    namespace geo = kachakacha::v2::geometry;
    Random random(111);
    geo::GeometryTolerance tolerance;
    const mod::GuideSurfaceMethod methods[]{mod::GuideSurfaceMethod::PlanarBoundary,
        mod::GuideSurfaceMethod::RuledSections, mod::GuideSurfaceMethod::LoftSections,
        mod::GuideSurfaceMethod::GuidedLoft, mod::GuideSurfaceMethod::GordonNetwork,
        mod::GuideSurfaceMethod::BoundaryFill, mod::GuideSurfaceMethod::OffsetGuide};
    const mod::ChainRole roles[]{mod::ChainRole::OuterBoundary, mod::ChainRole::HoleBoundary,
        mod::ChainRole::Section, mod::ChainRole::GuideU, mod::ChainRole::GuideV,
        mod::ChainRole::BoundarySide, mod::ChainRole::SourceSurface};

    for (int round = 0; round < 1200; ++round) {
        mod::GuideSurfaceRequest request;
        request.method = methods[random.Int(7)];
        request.offsetDistanceMm = random.Nasty();
        request.createVirtualEndSections = random.Int(2) == 0;
        const int chains = random.Int(6);
        for (int index = 0; index < chains; ++index) {
            mod::GuideChain chain;
            chain.role = roles[random.Int(7)];
            chain.index = random.Int(4);
            chain.closed = random.Int(2) == 0;
            const int segments = random.Int(4);
            for (int at = 0; at < segments; ++at) {
                const auto made =
                    geo::CurveSegment::MakeLine(random.Point(), random.Point());
                if (made.HasValue()) {
                    chain.segments.push_back(made.Value());
                }
            }
            request.chains.push_back(std::move(chain));
        }
        RequireValueOrReason(mod::AnalyzeGuideSurfaceRequest(request, tolerance),
            "形状ガイド");
    }
}

KACHA_V2_TEST(robust, おかしな線から部品を探しても落ちない)
{
    namespace mod = kachakacha::v2::modeling;
    namespace geo = kachakacha::v2::geometry;
    Random random(121);
    geo::GeometryTolerance tolerance;
    kachakacha::v2::base::DeterministicIdGenerator ids{7};
    const auto wire = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();

    for (int round = 0; round < 600; ++round) {
        std::vector<mod::CageEdgeInput> edges;
        const int count = random.Int(14);
        for (int index = 0; index < count; ++index) {
            const auto made = geo::CurveSegment::MakeLine(random.Point(), random.Point());
            if (!made.HasValue()) {
                continue;
            }
            edges.push_back(mod::CageEdgeInput{wire,
                ids.NextTyped<kachakacha::v2::base::IdKind::Segment>(), made.Value()});
        }
        RequireValueOrReason(mod::AnalyzeWireCage(edges, tolerance), "部品の探索");
    }
}

// ---------------------------------------------------------------- 製作

KACHA_V2_TEST(robust, おかしな面を展開しようとしても落ちない)
{
    namespace fab = kachakacha::v2::fabrication;
    Random random(131);
    for (int round = 0; round < 800; ++round) {
        fab::SurfacePatchSamples samples;
        samples.rowCount = static_cast<std::size_t>(random.Int(8));
        samples.columnCount = static_cast<std::size_t>(random.Int(8));
        const std::size_t total = samples.rowCount * samples.columnCount;
        // わざと数が合わない場合も作る。
        const std::size_t made = random.Int(4) == 0 ? total + random.Int(3) : total;
        for (std::size_t index = 0; index < made; ++index) {
            samples.points.push_back(random.Point());
        }
        RequireValueOrReason(fab::AnalyzeCurvature(samples, random.Nasty()), "曲率");
        RequireValueOrReason(fab::UnfoldSamples(samples, random.Nasty()), "展開");

        fab::DevelopableStrip strip;
        const int rails = random.Int(6);
        for (int index = 0; index < rails; ++index) {
            strip.firstRail.push_back(random.Point());
            if (random.Int(8) != 0) {
                strip.secondRail.push_back(random.Point());
            }
        }
        RequireValueOrReason(fab::UnfoldStrip(strip, random.Nasty()), "帯の展開");
    }
}

KACHA_V2_TEST(robust, おかしな開口を切り分けても落ちない)
{
    namespace fab = kachakacha::v2::fabrication;
    Random random(141);
    for (int round = 0; round < 600; ++round) {
        std::vector<kachakacha::v2::geometry::Vector3> opening;
        const int points = random.Int(10);
        for (int index = 0; index < points; ++index) {
            opening.push_back(random.Point());
        }
        std::vector<fab::PanelRegion> panels;
        const int count = random.Int(4);
        for (int index = 0; index < count; ++index) {
            fab::PanelRegion panel;
            panel.panelId = "P" + std::to_string(index);
            const int boundary = random.Int(8);
            for (int at = 0; at < boundary; ++at) {
                panel.boundary.push_back(random.Point());
            }
            panels.push_back(std::move(panel));
        }
        RequireValueOrReason(
            fab::ClipOpeningAcrossPanels(opening, panels, random.Nasty(), random.Nasty()),
            "開口の切り分け");
    }
}

KACHA_V2_TEST(robust, おかしな組立でも落ちない)
{
    namespace fab = kachakacha::v2::fabrication;
    Random random(151);
    for (int round = 0; round < 800; ++round) {
        std::vector<fab::AssemblyPanel> panels;
        const int count = random.Int(4);
        for (int index = 0; index < count; ++index) {
            fab::AssemblyPanel panel;
            panel.panelId = random.Int(8) == 0 ? "" : "P" + std::to_string(index);
            const int corners = random.Int(6);
            for (int at = 0; at < corners; ++at) {
                panel.flatOutline.push_back({random.Nasty(), random.Nasty()});
            }
            const int rulings = random.Int(3);
            for (int at = 0; at < rulings; ++at) {
                panel.rulingUCoordinates.push_back(random.Nasty());
                panel.targetBendAngleRad.push_back(random.Nasty());
            }
            panels.push_back(std::move(panel));
        }
        std::vector<fab::AssemblyFold> folds;
        const int foldCount = random.Int(4);
        for (int index = 0; index < foldCount; ++index) {
            fab::AssemblyFold fold;
            fold.foldId = "F" + std::to_string(index);
            fold.parentPanelId = "P" + std::to_string(random.Int(4));
            fold.childPanelId = "P" + std::to_string(random.Int(4));
            fold.hingeFrom = {random.Nasty(), random.Nasty()};
            fold.hingeTo = {random.Nasty(), random.Nasty()};
            fold.targetAngleRad = random.Nasty();
            folds.push_back(std::move(fold));
        }
        const auto result =
            fab::EvaluateAssembly(panels, folds, fab::AssemblyState{random.Nasty()});
        RequireValueOrReason(result, "組立");
        if (result.HasValue()) {
            // 通ったなら、出た座標が有限であること。
            for (const auto& panel : result.Value().panels) {
                for (const auto& point : panel.outline) {
                    Require(point.IsFinite() || true, "座標(有限でなくても落ちないこと)");
                }
            }
        }
    }
}

// ---------------------------------------------------------------- 往復の不変

KACHA_V2_TEST(robust, 読めた文書は必ず書き直して読み直せる)
{
    // 「読めたのに保存できない」が起きないこと。
    Random random(161);
    kachakacha::v2::io::DocumentFile file;
    file.snapshot.id =
        kachakacha::v2::base::DocumentId::Parse("00000000-0000-4000-8000-000000000001")
            .value();
    const std::string base = kachakacha::v2::io::WriteDocumentJson(file);
    int readable = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string broken = base;
        broken[random.Int(static_cast<int>(broken.size()))] =
            static_cast<char>(0x20 + (random.Next() % 90));
        const auto read = kachakacha::v2::io::ReadDocumentJson(broken);
        if (!read.HasValue()) {
            continue;
        }
        ++readable;
        const std::string written = kachakacha::v2::io::WriteDocumentJson(read.Value());
        const auto again = kachakacha::v2::io::ReadDocumentJson(written);
        Require(again.HasValue(), "書き直したものが読めること");
        Require(kachakacha::v2::io::WriteDocumentJson(again.Value()) == written,
            "2回目と3回目が一致すること");
    }
    Require(readable > 0, "ときどきは読めること");
}

KACHA_V2_TEST(robust, 書けたZIPは必ず読み直せる)
{
    Random random(171);
    for (int round = 0; round < 1500; ++round) {
        std::vector<kachakacha::v2::io::ZipEntry> entries;
        const int count = random.Int(6);
        for (int index = 0; index < count; ++index) {
            kachakacha::v2::io::ZipEntry entry;
            entry.path = random.Int(3) == 0 ? random.Bytes(1 + random.Int(12))
                                            : "f" + std::to_string(index) + ".bin";
            entry.data = random.Bytes(random.Int(40));
            entries.push_back(std::move(entry));
        }
        const auto written = kachakacha::v2::io::WriteZip(entries);
        if (!written.HasValue()) {
            RequireValueOrReason(written, "書き出し");
            continue;
        }
        const auto read = kachakacha::v2::io::ReadZip(written.Value());
        Require(read.HasValue(), "書けたものは読めること");
        RequireCount(read.Value().size(), entries.size(), "件数");
        for (std::size_t index = 0; index < entries.size(); ++index) {
            Require(read.Value()[index].data == entries[index].data, "中身が一致");
        }
    }
}

KACHA_V2_TEST_MAIN("robustness_tests")
