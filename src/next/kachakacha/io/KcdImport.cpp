#include "kachakacha/io/KcdImport.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace kachakacha::v2::io {

using base::Diagnostic;
using base::EntityId;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using document::AddFeatureCommand;
using document::Document;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using geometry::CurveSegment;
using geometry::Vector3;
using modeling::WorkPlaneFrame;

namespace {

constexpr const char* kBroken = "KCD1-E001";
constexpr const char* kSkipped = "KCD1-I002";
constexpr double kPi = 3.14159265358979323846;

//! 1行ぶんの命令。空白区切り。
struct Line {
    int number = 0;
    std::string command;
    std::vector<std::string> args;
};

[[nodiscard]] std::vector<Line> Tokenize(std::string_view text)
{
    std::vector<Line> lines;
    std::istringstream stream{std::string(text)};
    std::string raw;
    int number = 0;
    while (std::getline(stream, raw)) {
        ++number;
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        std::istringstream words(raw);
        Line line;
        line.number = number;
        if (!(words >> line.command) || line.command.empty() || line.command[0] == '#') {
            continue;
        }
        std::string word;
        while (words >> word) {
            line.args.push_back(word);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

//! 読み込みの途中の状態。名前 → id の対応と、文書。
struct Reader {
    Document document;
    base::IdGenerator& ids;
    std::map<std::string, EntityId> planes;
    std::map<std::string, EntityId> wires;
    std::map<std::string, EntityId> points;
    std::map<std::string, EntityId> surfaces;
    std::map<std::string, WorkPlaneFrame> planeFrames;
    std::map<std::string, std::vector<CurveSegment>> wireSegments;
    std::vector<Diagnostic> notes;
    int read = 0;
    int skipped = 0;

    Reader(base::IdGenerator& generator)
        : document(generator.NextTyped<base::IdKind::Document>())
        , ids(generator)
    {
    }
};

[[nodiscard]] Diagnostic Broken(const Line& line, const std::string& why)
{
    return MakeError(kBroken, "V1 の .kcd を読めません。",
        std::to_string(line.number) + " 行目 " + line.command + ": " + why);
}

[[nodiscard]] Result<double> Number(const Line& line, std::size_t index)
{
    if (index >= line.args.size()) {
        return Result<double>::Failure(Broken(line, "数が足りません。"));
    }
    char* end = nullptr;
    const double value = std::strtod(line.args[index].c_str(), &end);
    if (end == line.args[index].c_str() || *end != '\0' || !std::isfinite(value)) {
        return Result<double>::Failure(Broken(line, "「" + line.args[index] + "」は数ではありません。"));
    }
    return Result<double>::Success(value);
}

[[nodiscard]] Result<Vector3> Point(const Line& line, std::size_t index)
{
    const auto x = Number(line, index);
    const auto y = Number(line, index + 1);
    const auto z = Number(line, index + 2);
    if (!x.HasValue()) {
        return Result<Vector3>::Failure(x.Diagnostics());
    }
    if (!y.HasValue()) {
        return Result<Vector3>::Failure(y.Diagnostics());
    }
    if (!z.HasValue()) {
        return Result<Vector3>::Failure(z.Diagnostics());
    }
    return Result<Vector3>::Success(Vector3{x.Value(), y.Value(), z.Value()});
}

//! Feature と Entity を1組で文書へ入れる。
[[nodiscard]] Result<EntityId> AddOne(Reader& reader, Feature feature, EntityKind kind,
    const std::string& name, const char* outputKey)
{
    feature.id = reader.ids.NextTyped<base::IdKind::Feature>();
    feature.displayName = name;
    Entity entity;
    entity.id = reader.ids.NextTyped<base::IdKind::Entity>();
    entity.kind = kind;
    entity.displayName = name;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{outputKey, entity.id, kind});
    const auto added = reader.document.Run(AddFeatureCommand(feature, {entity}, name));
    if (!added.committed) {
        return Result<EntityId>::Failure(added.diagnostics);
    }
    return Result<EntityId>::Success(entity.id);
}

[[nodiscard]] Result<EntityId> AddPlane(Reader& reader, const std::string& name,
    const WorkPlaneFrame& frame, modeling::WorkPlaneMethod method,
    std::vector<EntityId> inputs)
{
    Feature feature;
    feature.type = FeatureType::CreateWorkPlane;
    feature.inputEntityIds = inputs;
    domain::CreateWorkPlaneDefinition definition;
    definition.method = static_cast<int>(method);
    definition.inputs = std::move(inputs);
    definition.origin = frame.origin;
    definition.normal = frame.normal;
    definition.uDirection = frame.uAxis;
    feature.definition = std::move(definition);
    const auto id = AddOne(reader, std::move(feature), EntityKind::WorkPlane, name, "plane");
    if (id.HasValue()) {
        reader.planes[name] = id.Value();
        reader.planeFrames[name] = frame;
    }
    return id;
}

[[nodiscard]] Result<EntityId> AddWire(Reader& reader, const std::string& name,
    std::vector<CurveSegment> segments)
{
    Feature feature;
    feature.type = FeatureType::CreateWire;
    domain::CreateWireDefinition definition;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        definition.segmentIds.push_back(reader.ids.NextTyped<base::IdKind::Segment>());
    }
    definition.segments = segments;
    feature.definition = std::move(definition);
    const auto id = AddOne(reader, std::move(feature), EntityKind::Wire, name, "wire");
    if (id.HasValue()) {
        reader.wires[name] = id.Value();
        reader.wireSegments[name] = std::move(segments);
    }
    return id;
}

[[nodiscard]] Result<WorkPlaneFrame> PlaneNamed(Reader& reader, const Line& line,
    const std::string& name)
{
    const auto found = reader.planeFrames.find(name);
    if (found == reader.planeFrames.end()) {
        return Result<WorkPlaneFrame>::Failure(Broken(line, "平面「" + name + "」がありません。"));
    }
    return Result<WorkPlaneFrame>::Success(found->second);
}

//! 軸まわりに向きを回す(Rodrigues)。
[[nodiscard]] Vector3 Rotate(const Vector3& value, const Vector3& axis, double angleRad)
{
    const Vector3 k = geometry::Normalized(axis);
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    return value * c + Cross(k, value) * s + k * (Dot(k, value) * (1.0 - c));
}

// ---- 平面 ----

[[nodiscard]] std::optional<Diagnostic> ReadPlane(Reader& reader, const Line& line)
{
    using modeling::WorkPlaneMethod;
    const auto& tolerance = reader.document.Snapshot().settings.tolerance;
    if (line.args.empty()) {
        return Broken(line, "名前がありません。");
    }
    const std::string& name = line.args[0];
    if (line.command == "plane_point_normal") {
        const auto origin = Point(line, 1);
        const auto normal = Point(line, 4);
        const auto hint = Point(line, 7);
        if (!origin.HasValue() || !normal.HasValue() || !hint.HasValue()) {
            return Broken(line, "原点・法線・横方向の9つの数が要ります。");
        }
        const auto frame = modeling::FrameFromNormalAndU(origin.Value(), normal.Value(),
            hint.Value(), tolerance);
        if (!frame.HasValue()) {
            return frame.Diagnostics().front();
        }
        const auto added = AddPlane(reader, name, frame.Value(), WorkPlaneMethod::PointNormal, {});
        return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
    }
    if (line.command == "plane_offset") {
        if (line.args.size() < 3) {
            return Broken(line, "元の平面と距離が要ります。");
        }
        const auto source = PlaneNamed(reader, line, line.args[1]);
        const auto distance = Number(line, 2);
        if (!source.HasValue() || !distance.HasValue()) {
            return source.HasValue() ? distance.Diagnostics().front()
                                     : source.Diagnostics().front();
        }
        WorkPlaneFrame frame = source.Value();
        frame.origin = frame.origin + frame.normal * distance.Value();
        const auto added = AddPlane(reader, name, frame, WorkPlaneMethod::OffsetFromPlane,
            {reader.planes[line.args[1]]});
        return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
    }
    if (line.command == "plane_rotate") {
        const auto source = PlaneNamed(reader, line, line.args.size() > 1 ? line.args[1] : "");
        const auto axisPoint = Point(line, 2);
        const auto axisDirection = Point(line, 5);
        const auto degrees = Number(line, 8);
        if (!source.HasValue() || !axisPoint.HasValue() || !axisDirection.HasValue()
            || !degrees.HasValue()) {
            return Broken(line, "元の平面・軸の点・軸の向き・角度が要ります。");
        }
        const double angle = degrees.Value() * kPi / 180.0;
        const WorkPlaneFrame& base = source.Value();
        const Vector3 origin = axisPoint.Value()
            + Rotate(base.origin - axisPoint.Value(), axisDirection.Value(), angle);
        const auto frame = modeling::FrameFromNormalAndU(origin,
            Rotate(base.normal, axisDirection.Value(), angle),
            Rotate(base.uAxis, axisDirection.Value(), angle), tolerance);
        if (!frame.HasValue()) {
            return frame.Diagnostics().front();
        }
        const auto added = AddPlane(reader, name, frame.Value(), WorkPlaneMethod::AngleAboutEdge,
            {reader.planes[line.args[1]]});
        return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
    }
    // plane_three
    modeling::WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    for (std::size_t index = 1; index + 2 < line.args.size() && request.points.size() < 3;
         index += 3) {
        const auto point = Point(line, index);
        if (!point.HasValue()) {
            return point.Diagnostics().front();
        }
        request.points.push_back(point.Value());
    }
    const auto frame = modeling::BuildWorkPlane(request, tolerance);
    if (!frame.HasValue()) {
        return frame.Diagnostics().front();
    }
    const auto added = AddPlane(reader, name, frame.Value(), WorkPlaneMethod::ThreePoints, {});
    return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
}

// ---- 線 ----

[[nodiscard]] Result<std::vector<Vector3>> CountedPoints(const Line& line, std::size_t from,
    std::size_t count)
{
    std::vector<Vector3> points;
    for (std::size_t index = 0; index < count; ++index) {
        const auto point = Point(line, from + index * 3);
        if (!point.HasValue()) {
            return Result<std::vector<Vector3>>::Failure(point.Diagnostics());
        }
        points.push_back(point.Value());
    }
    return Result<std::vector<Vector3>>::Success(std::move(points));
}

//! 端を4重にした一様な節か。V2 の B-spline はこれしか持てない。
[[nodiscard]] bool UniformClampedKnots(const std::vector<double>& knots, std::size_t controls)
{
    if (knots.size() != controls + 4 || controls < 4) {
        return false;
    }
    const std::size_t spans = controls - 3;
    for (std::size_t index = 0; index < knots.size(); ++index) {
        double expected = 0.0;
        if (index >= 4 && index < knots.size() - 4) {
            expected = static_cast<double>(index - 3) / static_cast<double>(spans);
        } else if (index >= knots.size() - 4) {
            expected = 1.0;
        }
        if (std::abs(knots[index] - expected) > 1.0e-9) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<std::vector<CurveSegment>> CurveOf(const Line& line)
{
    using Out = Result<std::vector<CurveSegment>>;
    const auto one = [](Result<CurveSegment> made) {
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        return Out::Success({made.Value()});
    };
    if (line.command == "line3d") {
        const auto points = CountedPoints(line, 1, 2);
        if (!points.HasValue()) {
            return Out::Failure(points.Diagnostics());
        }
        return one(CurveSegment::MakeLine(points.Value()[0], points.Value()[1]));
    }
    if (line.command == "bezier3d") {
        const auto points = CountedPoints(line, 1, 4);
        if (!points.HasValue()) {
            return Out::Failure(points.Diagnostics());
        }
        return one(CurveSegment::MakeCubicBezier(points.Value()));
    }
    if (line.command == "polyline3d" || line.command == "bspline3d"
        || line.command == "bspline3d_knots") {
        // polyline3d と bspline3d は行の終わりまで点。bspline3d_knots だけ先頭に点の数。
        std::size_t from = 1;
        std::size_t count = (line.args.size() - 1) / 3;
        if (line.command == "bspline3d_knots") {
            const auto declared = Number(line, 1);
            if (!declared.HasValue()) {
                return Out::Failure(declared.Diagnostics());
            }
            from = 2;
            count = static_cast<std::size_t>(declared.Value());
        } else if ((line.args.size() - 1) % 3 != 0 || count < 2) {
            return Out::Failure(Broken(line, "点は x y z の組で2つ以上要ります。"));
        }
        const auto points = CountedPoints(line, from, count);
        if (!points.HasValue()) {
            return Out::Failure(points.Diagnostics());
        }
        if (line.command == "polyline3d") {
            std::vector<CurveSegment> segments;
            for (std::size_t index = 0; index + 1 < points.Value().size(); ++index) {
                const auto made = CurveSegment::MakeLine(points.Value()[index],
                    points.Value()[index + 1]);
                if (!made.HasValue()) {
                    return Out::Failure(made.Diagnostics());
                }
                segments.push_back(made.Value());
            }
            return Out::Success(std::move(segments));
        }
        if (line.command == "bspline3d_knots") {
            std::vector<double> knots;
            for (std::size_t index = 2 + points.Value().size() * 3; index < line.args.size();
                 ++index) {
                const auto knot = Number(line, index);
                if (!knot.HasValue()) {
                    return Out::Failure(knot.Diagnostics());
                }
                knots.push_back(knot.Value());
            }
            if (!UniformClampedKnots(knots, points.Value().size())) {
                return Out::Failure(Broken(line,
                    "V2 の B-spline は端を4重にした一様な節だけを持てます。この節は違います。"));
            }
        }
        return one(CurveSegment::MakeCubicBSpline(points.Value()));
    }
    // circle3d / arc3d: 中心・u軸・v軸・半径(・開始角・掃引角)。
    const auto center = Point(line, 1);
    const auto uAxis = Point(line, 4);
    const auto vAxis = Point(line, 7);
    const auto radius = Number(line, 10);
    if (!center.HasValue() || !uAxis.HasValue() || !vAxis.HasValue() || !radius.HasValue()) {
        return Out::Failure(Broken(line, "中心・u軸・v軸・半径が要ります。"));
    }
    const Vector3 normal = Cross(uAxis.Value(), vAxis.Value());
    if (line.command == "circle3d") {
        return one(CurveSegment::MakeCircle(center.Value(), normal, uAxis.Value(),
            radius.Value()));
    }
    const auto start = Number(line, 11);
    const auto sweep = Number(line, 12);
    if (!start.HasValue() || !sweep.HasValue()) {
        return Out::Failure(Broken(line, "開始角と掃引角(度)が要ります。"));
    }
    return one(CurveSegment::MakeCircularArc(center.Value(), normal, uAxis.Value(),
        radius.Value(), start.Value() * kPi / 180.0, sweep.Value() * kPi / 180.0));
}

[[nodiscard]] std::optional<Diagnostic> ReadWire(Reader& reader, const Line& line)
{
    if (line.args.empty()) {
        return Broken(line, "名前がありません。");
    }
    const auto curve = CurveOf(line);
    if (!curve.HasValue()) {
        return curve.Diagnostics().front();
    }
    const auto added = AddWire(reader, line.args[0], curve.Value());
    return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
}

[[nodiscard]] std::optional<Diagnostic> ReadPoint(Reader& reader, const Line& line)
{
    if (line.args.empty()) {
        return Broken(line, "名前がありません。");
    }
    const auto position = Point(line, 1);
    if (!position.HasValue()) {
        return position.Diagnostics().front();
    }
    Feature feature;
    feature.type = FeatureType::CreatePoint;
    domain::CreatePointDefinition definition;
    definition.positionMm = position.Value();
    definition.xExpression = {"", position.Value().x, geometry::QuantityKind::Length};
    definition.yExpression = {"", position.Value().y, geometry::QuantityKind::Length};
    definition.zExpression = {"", position.Value().z, geometry::QuantityKind::Length};
    feature.definition = std::move(definition);
    const auto added = AddOne(reader, std::move(feature), EntityKind::Point, line.args[0], "point");
    if (added.HasValue()) {
        reader.points[line.args[0]] = added.Value();
    }
    return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
}

// ---- 紐づけ・表示 ----

//! wire_meta 名前 平面 方針。線の作り方に「元の平面」を持たせる。
[[nodiscard]] std::optional<Diagnostic> ReadWireMeta(Reader& reader, const Line& line)
{
    if (line.args.size() < 2) {
        return Broken(line, "線と平面の名前が要ります。");
    }
    const auto wire = reader.wires.find(line.args[0]);
    const auto plane = reader.planes.find(line.args[1]);
    if (wire == reader.wires.end()) {
        return Broken(line, "線「" + line.args[0] + "」がありません。");
    }
    if (plane == reader.planes.end()) {
        return Broken(line, "平面「" + line.args[1] + "」がありません。");
    }
    const auto* entity = reader.document.FindEntity(wire->second);
    const auto* feature = entity == nullptr ? nullptr
                                            : reader.document.FindFeature(entity->createdBy);
    if (feature == nullptr) {
        return Broken(line, "線「" + line.args[0] + "」の作り方がありません。");
    }
    auto definition = std::get<domain::CreateWireDefinition>(feature->definition);
    definition.sourcePlaneId = plane->second;
    const auto changed = reader.document.Run(document::UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, "平面へ紐づける"));
    if (!changed.committed) {
        return changed.diagnostics.front();
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Diagnostic> ReadVisibility(Reader& reader, const Line& line)
{
    if (line.args.size() < 3) {
        return Broken(line, "種類・名前・visible/hidden が要ります。");
    }
    const std::string& kind = line.args[0];
    const std::string& name = line.args[1];
    const std::map<std::string, EntityId>* table = nullptr;
    if (kind == "workplane" || kind == "plane") {
        table = &reader.planes;
    } else if (kind == "wire") {
        table = &reader.wires;
    } else if (kind == "point") {
        table = &reader.points;
    } else if (kind == "surface" || kind == "plate") {
        table = &reader.surfaces;
    }
    const auto found = table == nullptr ? reader.wires.end() : table->find(name);
    if (table == nullptr || found == table->end()) {
        // 読み飛ばしたもの(治具など)の表示指定は、知らせるだけ。
        reader.notes.push_back(MakeWarning(kSkipped, "V1 の命令を読み飛ばしました。",
            std::to_string(line.number) + " 行目 visibility " + kind + " " + name
                + ": その種類・名前は読み込んでいません。"));
        ++reader.skipped;
        return std::nullopt;
    }
    const auto changed = reader.document.Run(document::SetVisibilityCommand({found->second},
        line.args[2] == "hidden" ? domain::Visibility::Hidden : domain::Visibility::Visible));
    if (!changed.committed) {
        return changed.diagnostics.front();
    }
    return std::nullopt;
}

// ---- 面・板 ----

//! surface_loft 名前 断面1 断面2 ... → 形状ガイド(断面のロフト)。
[[nodiscard]] std::optional<Diagnostic> ReadLoft(Reader& reader, const Line& line)
{
    if (line.args.size() < 3) {
        return Broken(line, "面の名前と断面が2つ以上要ります。");
    }
    modeling::GuideTable table;
    table.method = modeling::GuideSurfaceMethod::LoftSections;
    std::vector<EntityId> inputs;
    for (std::size_t index = 1; index < line.args.size(); ++index) {
        const auto wire = reader.wires.find(line.args[index]);
        if (wire == reader.wires.end()) {
            return Broken(line, "断面「" + line.args[index] + "」がありません。");
        }
        modeling::GuideTableRow row;
        row.role = modeling::ChainRole::Section;
        row.sourceWireIds.push_back(wire->second);
        row.sourceLabels.push_back(line.args[index]);
        row.segments = reader.wireSegments[line.args[index]];
        table.rows.push_back(std::move(row));
        inputs.push_back(wire->second);
    }
    Feature feature;
    feature.type = FeatureType::CreateGuideSurface;
    feature.inputEntityIds = inputs;
    feature.definition = app::DefinitionFromGuideTable(table);
    const auto added = AddOne(reader, std::move(feature), EntityKind::GuideSurface, line.args[0],
        "surface");
    if (added.HasValue()) {
        reader.surfaces[line.args[0]] = added.Value();
    }
    return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
}

//! plate 名前 面 厚み 向き 材料 → 面に厚み(ThickenSurface)。材料は持たない。
[[nodiscard]] std::optional<Diagnostic> ReadPlate(Reader& reader, const Line& line)
{
    if (line.args.size() < 4) {
        return Broken(line, "板の名前・面・厚み・向きが要ります。");
    }
    const auto surface = reader.surfaces.find(line.args[1]);
    if (surface == reader.surfaces.end()) {
        return Broken(line, "面「" + line.args[1] + "」がありません。");
    }
    const auto thickness = Number(line, 2);
    if (!thickness.HasValue()) {
        return thickness.Diagnostics().front();
    }
    Feature feature;
    feature.type = FeatureType::ThickenSurface;
    feature.inputEntityIds = {surface->second};
    domain::ThickenSurfaceDefinition definition;
    definition.surface = surface->second;
    definition.thickness = {"", thickness.Value(), geometry::QuantityKind::Length};
    const std::string& side = line.args[3];
    definition.placement = side == "outside" ? 0 : (side == "inside" ? 2 : 1);
    feature.definition = std::move(definition);
    const auto added = AddOne(reader, std::move(feature), EntityKind::Part, line.args[0], "part");
    if (added.HasValue()) {
        reader.surfaces[line.args[0]] = added.Value();
    }
    return added.HasValue() ? std::nullopt : std::optional<Diagnostic>(added.Diagnostics().front());
}

[[nodiscard]] std::optional<Diagnostic> ReadLine(Reader& reader, const Line& line)
{
    const std::string& c = line.command;
    if (c == "format_version") {
        return std::nullopt;
    }
    if (c == "plane_point_normal" || c == "plane_offset" || c == "plane_rotate"
        || c == "plane_three") {
        return ReadPlane(reader, line);
    }
    if (c == "line3d" || c == "polyline3d" || c == "bezier3d" || c == "bspline3d"
        || c == "bspline3d_knots" || c == "circle3d" || c == "arc3d") {
        return ReadWire(reader, line);
    }
    if (c == "point3d") {
        return ReadPoint(reader, line);
    }
    if (c == "wire_meta") {
        return ReadWireMeta(reader, line);
    }
    if (c == "visibility") {
        return ReadVisibility(reader, line);
    }
    if (c == "surface_loft") {
        return ReadLoft(reader, line);
    }
    if (c == "plate") {
        return ReadPlate(reader, line);
    }
    // V2 に同じ意味のものが無い、または核が要るもの。名前を挙げて読み飛ばす。
    const std::string hint = c == "wire_project"
        ? " 開いてから、線と面を選んで「曲面へ投影」で作り直してください。"
        : (c.rfind("plate_", 0) == 0 ? " 板の範囲・開口・分割は V2 では製作モデルで扱います。"
                                     : "");
    reader.notes.push_back(MakeWarning(kSkipped, "V1 の命令を読み飛ばしました。",
        std::to_string(line.number) + " 行目 " + c
            + (line.args.empty() ? "" : " " + line.args[0]) + ": V2 に同じものがありません。" + hint));
    ++reader.skipped;
    return std::nullopt;
}

} // namespace

Result<KcdImportResult> ImportKcdScript(std::string_view text, base::IdGenerator& ids)
{
    using Out = Result<KcdImportResult>;
    Reader reader(ids);
    const auto lines = Tokenize(text);
    if (lines.empty()) {
        return Out::Failure(MakeError(kBroken, "V1 の .kcd を読めません。",
            "命令が1つもありません。"));
    }
    for (const Line& line : lines) {
        const auto problem = ReadLine(reader, line);
        if (problem.has_value()) {
            return Out::Failure({*problem});
        }
        ++reader.read;
    }
    KcdImportResult result;
    result.snapshot = reader.document.Snapshot();
    result.notes = std::move(reader.notes);
    result.readCommands = reader.read - reader.skipped;
    result.skippedCommands = reader.skipped;
    return Out::Success(std::move(result));
}

bool LooksLikeKcdPath(std::string_view path) noexcept
{
    if (path.size() < 4) {
        return false;
    }
    std::string tail(path.substr(path.size() - 4));
    for (char& c : tail) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return tail == ".kcd";
}

} // namespace kachakacha::v2::io
