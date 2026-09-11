#include "kachakacha/modeling/GuideSurfaceTable.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kachakacha::v2::modeling {
namespace {

using base::Diagnostic;
using base::MakeError;
using base::Result;
using geometry::CurveKind;

constexpr double kTwoPi = 6.283185307179586;

[[nodiscard]] Diagnostic NoSuchRow(std::size_t rowIndex)
{
    return MakeError("UI-R001", "その行がありません。",
        "行番号 " + std::to_string(rowIndex + 1) + " は表にありません。");
}

//! 1本の曲線を、種類を保ったまま逆向きにする。実装は geometry に1つだけ置く。
//! ここに2つ目を書くと、片方だけ直したときに食い違う。
[[nodiscard]] Result<CurveSegment> ReverseSegment(const CurveSegment& segment)
{
    return geometry::ReverseCurve(segment);
}

[[nodiscard]] double DistanceTo(const std::vector<CurveSegment>& segments,
    const Vector3& point)
{
    double best = 1.0e300;
    for (const CurveSegment& segment : segments) {
        best = std::min(best, segment.ClosestPoint(point).distance);
    }
    return best;
}

//! 役割ごとに、表の並び順で 1 から振り直す。
[[nodiscard]] std::vector<int> NumberRows(const GuideTable& table)
{
    std::map<int, int> counters;
    std::vector<int> numbers;
    numbers.reserve(table.rows.size());
    for (const GuideTableRow& row : table.rows) {
        const int key = static_cast<int>(row.role);
        numbers.push_back(++counters[key]);
    }
    return numbers;
}

} // namespace

const std::vector<ChainRole>& RolesForMethod(GuideSurfaceMethod method)
{
    static const std::vector<ChainRole> planar{ChainRole::OuterBoundary,
        ChainRole::HoleBoundary};
    static const std::vector<ChainRole> sections{ChainRole::Section};
    static const std::vector<ChainRole> guided{ChainRole::Section, ChainRole::GuideU};
    static const std::vector<ChainRole> gordon{ChainRole::GuideU, ChainRole::GuideV};
    static const std::vector<ChainRole> fill{ChainRole::BoundarySide};
    static const std::vector<ChainRole> offset{ChainRole::SourceSurface};
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return planar;
    case GuideSurfaceMethod::RuledSections:  return sections;
    case GuideSurfaceMethod::LoftSections:   return sections;
    case GuideSurfaceMethod::GuidedLoft:     return guided;
    case GuideSurfaceMethod::GordonNetwork:  return gordon;
    case GuideSurfaceMethod::BoundaryFill:   return fill;
    case GuideSurfaceMethod::OffsetGuide:    return offset;
    case GuideSurfaceMethod::Revolve:        return sections;
    }
    return sections;
}

bool RoleUsedByMethod(GuideSurfaceMethod method, ChainRole role)
{
    const std::vector<ChainRole>& roles = RolesForMethod(method);
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

std::string ChainRoleLabelJa(ChainRole role)
{
    switch (role) {
    case ChainRole::OuterBoundary: return "外形";
    case ChainRole::HoleBoundary:  return "穴";
    case ChainRole::Section:       return "断面";
    case ChainRole::GuideU:        return "外形U";
    case ChainRole::GuideV:        return "外形V";
    case ChainRole::BoundarySide:  return "境界辺";
    case ChainRole::SourceSurface: return "元の面";
    }
    return "不明";
}

bool IsBoundaryRole(ChainRole role) noexcept
{
    return role == ChainRole::OuterBoundary || role == ChainRole::HoleBoundary
        || role == ChainRole::GuideU || role == ChainRole::GuideV
        || role == ChainRole::BoundarySide;
}

RowColor ColorForRow(ChainRole role, int number) noexcept
{
    // 役割で色相の基準を決め、番号でずらす。表と3Dが同じ式を使うので必ず一致する。
    static const RowColor kBase[] = {
        {0xE0, 0x60, 0x40}, {0xC0, 0x40, 0xA0}, {0x40, 0xA0, 0xE0}, {0x40, 0xC0, 0x70},
        {0xE0, 0xB0, 0x40}, {0x90, 0x70, 0xE0}, {0x80, 0x80, 0x90}};
    const std::size_t index = static_cast<std::size_t>(role)
        % (sizeof(kBase) / sizeof(kBase[0]));
    const RowColor base = kBase[index];
    const int shift = ((number - 1) % 4) * 24;
    const auto mix = [shift](std::uint8_t value) {
        const int mixed = static_cast<int>(value) + shift;
        return static_cast<std::uint8_t>(mixed > 255 ? 255 - (mixed - 255) : mixed);
    };
    return RowColor{mix(base.red), mix(base.green), mix(base.blue)};
}

std::vector<GuideTableRowView> BuildGuideTableView(const GuideTable& table,
    const GeometryTolerance& tolerance)
{
    const std::vector<int> numbers = NumberRows(table);
    std::vector<GuideTableRowView> views;
    views.reserve(table.rows.size());
    for (std::size_t index = 0; index < table.rows.size(); ++index) {
        const GuideTableRow& row = table.rows[index];
        GuideTableRowView view;
        view.rowIndex = index;
        view.role = row.role;
        view.roleLabelJa = ChainRoleLabelJa(row.role);
        view.number = numbers[index];
        view.segmentCount = row.segments.size();
        view.reversed = row.reversed;
        view.directionLabelJa = row.reversed ? "逆" : "正";
        view.color = ColorForRow(row.role, view.number);
        for (std::size_t part = 0; part < row.sourceLabels.size(); ++part) {
            if (part != 0) {
                view.sourceLabelJa += " + ";
            }
            view.sourceLabelJa += row.sourceLabels[part];
        }
        if (!row.segments.empty()) {
            view.startPoint = row.segments.front().StartPoint();
            view.endPoint = row.segments.back().EndPoint();
        }
        views.push_back(std::move(view));
    }

    // 接続列。断面などの両端が、外形側の行へ届いているかを見る。
    for (GuideTableRowView& view : views) {
        if (view.segmentCount == 0) {
            view.connectionLabelJa = "線なし";
            continue;
        }
        if (IsBoundaryRole(view.role)) {
            const bool closed = (view.startPoint - view.endPoint).Length()
                <= tolerance.interactiveJoinMm;
            view.startConnected = closed;
            view.endConnected = closed;
            view.connectionLabelJa = closed ? "閉じている" : "開いている";
            continue;
        }
        for (std::size_t other = 0; other < table.rows.size(); ++other) {
            if (other == view.rowIndex || !IsBoundaryRole(table.rows[other].role)) {
                continue;
            }
            const std::vector<CurveSegment>& segments = table.rows[other].segments;
            view.startConnected = view.startConnected
                || DistanceTo(segments, view.startPoint) <= tolerance.interactiveJoinMm;
            view.endConnected = view.endConnected
                || DistanceTo(segments, view.endPoint) <= tolerance.interactiveJoinMm;
        }
        if (view.startConnected && view.endConnected) {
            view.connectionLabelJa = "有効";
        } else if (view.startConnected || view.endConnected) {
            view.connectionLabelJa = "片側だけ";
        } else {
            view.connectionLabelJa = "未接続";
        }
    }
    return views;
}

std::vector<std::string> MissingRoleGuidanceJa(const GuideTable& table)
{
    std::vector<std::string> guidance;
    for (ChainRole role : RolesForMethod(table.method)) {
        if (role == ChainRole::HoleBoundary) {
            continue;   // 穴は無くてよい。
        }
        const bool present = std::any_of(table.rows.begin(), table.rows.end(),
            [role](const GuideTableRow& row) { return row.role == role; });
        if (!present) {
            guidance.push_back(ChainRoleLabelJa(role) + "が1つも入っていません。");
        }
    }
    return guidance;
}

Result<GuideTable> AddSelectionAsNewRow(const GuideTable& table, ChainRole role,
    const GuideTableSelection& selection)
{
    if (!RoleUsedByMethod(table.method, role)) {
        return Result<GuideTable>::Failure(MakeError("UI-R003",
            "その作り方では、この役割を使いません。",
            std::string(GuideSurfaceMethodName(table.method)) + " に "
                + ChainRoleLabelJa(role) + " はありません。"));
    }
    if (selection.segments.empty()) {
        return Result<GuideTable>::Failure(MakeError("UI-R004",
            "選んだ線がありません。", "先に線を選んでください。"));
    }
    for (const GuideTableRow& row : table.rows) {
        if (std::find(row.sourceWireIds.begin(), row.sourceWireIds.end(),
                selection.sourceWireId)
            != row.sourceWireIds.end()) {
            return Result<GuideTable>::Failure(MakeError("UI-R006",
                "そのワイヤーは、すでに別の行に入っています。",
                selection.label + " は1つの行にしか入れられません。"));
        }
    }
    GuideTable next = table;
    GuideTableRow row;
    row.role = role;
    row.sourceWireIds.push_back(selection.sourceWireId);
    row.sourceLabels.push_back(selection.label);
    row.segments = selection.segments;
    next.rows.push_back(std::move(row));
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> AddSourceSurfaceRow(const GuideTable& table, const EntityId& surfaceId,
    const std::string& label)
{
    if (!RoleUsedByMethod(table.method, ChainRole::SourceSurface)) {
        return Result<GuideTable>::Failure(MakeError("UI-R003",
            "その作り方では、この役割を使いません。",
            std::string(GuideSurfaceMethodName(table.method)) + " に "
                + ChainRoleLabelJa(ChainRole::SourceSurface) + " はありません。"));
    }
    for (const GuideTableRow& row : table.rows) {
        if (row.role == ChainRole::SourceSurface) {
            return Result<GuideTable>::Failure(MakeError("UI-R010",
                "元の面はすでに入っています。",
                "離した面は1枚の面からしか作れません。先に元の面の行を消してください。"));
        }
    }
    GuideTable next = table;
    GuideTableRow row;
    row.role = ChainRole::SourceSurface;
    row.sourceWireIds.push_back(surfaceId);
    row.sourceLabels.push_back(label);
    next.rows.push_back(std::move(row));
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> AddSelectionToRow(const GuideTable& table, std::size_t rowIndex,
    const GuideTableSelection& selection, const GeometryTolerance& tolerance)
{
    if (rowIndex >= table.rows.size()) {
        return Result<GuideTable>::Failure(NoSuchRow(rowIndex));
    }
    if (selection.segments.empty()) {
        return Result<GuideTable>::Failure(MakeError("UI-R004",
            "選んだ線がありません。", "先に線を選んでください。"));
    }
    for (const GuideTableRow& row : table.rows) {
        if (std::find(row.sourceWireIds.begin(), row.sourceWireIds.end(),
                selection.sourceWireId)
            != row.sourceWireIds.end()) {
            return Result<GuideTable>::Failure(MakeError("UI-R006",
                "そのワイヤーは、すでに別の行に入っています。",
                selection.label + " は1つの行にしか入れられません。"));
        }
    }
    const GuideTableRow& target = table.rows[rowIndex];
    if (!target.segments.empty()) {
        const Vector3 tail = target.segments.back().EndPoint();
        const double gap = (selection.segments.front().StartPoint() - tail).Length();
        if (gap > tolerance.interactiveJoinMm) {
            return Result<GuideTable>::Failure(MakeError("UI-R005",
                "その線は、行の端につながりません。",
                "行の終点と " + std::to_string(gap) + " mm 離れています。"
                    + "つながる線を選ぶか、向きを変えてください。"));
        }
    }
    GuideTable next = table;
    GuideTableRow& row = next.rows[rowIndex];
    row.sourceWireIds.push_back(selection.sourceWireId);
    row.sourceLabels.push_back(selection.label);
    row.segments.insert(row.segments.end(), selection.segments.begin(),
        selection.segments.end());
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> MoveRow(const GuideTable& table, std::size_t rowIndex, int delta)
{
    if (rowIndex >= table.rows.size()) {
        return Result<GuideTable>::Failure(NoSuchRow(rowIndex));
    }
    if (delta != 1 && delta != -1) {
        return Result<GuideTable>::Failure(MakeError("UI-R007",
            "行は1つずつしか動かせません。", "上へ、または下へです。"));
    }
    // 同じ役割の行だけを見て、その中での隣と入れ替える。
    const ChainRole role = table.rows[rowIndex].role;
    std::vector<std::size_t> sameRole;
    for (std::size_t index = 0; index < table.rows.size(); ++index) {
        if (table.rows[index].role == role) {
            sameRole.push_back(index);
        }
    }
    const auto here = std::find(sameRole.begin(), sameRole.end(), rowIndex);
    const std::ptrdiff_t position = std::distance(sameRole.begin(), here) + delta;
    if (position < 0 || position >= static_cast<std::ptrdiff_t>(sameRole.size())) {
        return Result<GuideTable>::Failure(MakeError("UI-R008",
            "これ以上その向きへは動かせません。",
            ChainRoleLabelJa(role) + "の端の行です。"));
    }
    GuideTable next = table;
    std::swap(next.rows[rowIndex], next.rows[sameRole[static_cast<std::size_t>(position)]]);
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> RemoveRow(const GuideTable& table, std::size_t rowIndex)
{
    if (rowIndex >= table.rows.size()) {
        return Result<GuideTable>::Failure(NoSuchRow(rowIndex));
    }
    GuideTable next = table;
    next.rows.erase(next.rows.begin() + static_cast<std::ptrdiff_t>(rowIndex));
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> ReverseRow(const GuideTable& table, std::size_t rowIndex)
{
    if (rowIndex >= table.rows.size()) {
        return Result<GuideTable>::Failure(NoSuchRow(rowIndex));
    }
    GuideTable next = table;
    GuideTableRow& row = next.rows[rowIndex];
    std::vector<CurveSegment> reversed;
    reversed.reserve(row.segments.size());
    for (auto item = row.segments.rbegin(); item != row.segments.rend(); ++item) {
        Result<CurveSegment> flipped = ReverseSegment(*item);
        if (!flipped.HasValue()) {
            return Result<GuideTable>::Failure(flipped.Diagnostics());
        }
        reversed.push_back(flipped.Value());
    }
    row.segments = std::move(reversed);
    std::reverse(row.sourceWireIds.begin(), row.sourceWireIds.end());
    std::reverse(row.sourceLabels.begin(), row.sourceLabels.end());
    row.reversed = !row.reversed;
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideTable> SetGuideTableMethod(const GuideTable& table, GuideSurfaceMethod method)
{
    for (const GuideTableRow& row : table.rows) {
        if (!RoleUsedByMethod(method, row.role)) {
            return Result<GuideTable>::Failure(MakeError("UI-R003",
                "その作り方では、この役割を使いません。",
                ChainRoleLabelJa(row.role) + "の行が残っています。"
                    + "先に消してから作り方を変えてください。"));
        }
    }
    GuideTable next = table;
    next.method = method;
    return Result<GuideTable>::Success(std::move(next));
}

Result<GuideSurfaceRequest> ToGuideSurfaceRequest(const GuideTable& table,
    const GeometryTolerance& tolerance)
{
    const std::vector<std::string> missing = MissingRoleGuidanceJa(table);
    if (!missing.empty()) {
        std::string details;
        for (const std::string& line : missing) {
            details += line;
        }
        return Result<GuideSurfaceRequest>::Failure(MakeError("UI-R009",
            "面を作るための行がそろっていません。", details));
    }
    const std::vector<int> numbers = NumberRows(table);
    GuideSurfaceRequest request;
    request.method = table.method;
    request.offsetDistanceMm = table.offsetDistanceMm;
    request.revolveAxisPoint = table.revolveAxisPoint;
    request.revolveAxisDirection = table.revolveAxisDirection;
    request.revolveAngleRad = table.revolveAngleRad;
    for (std::size_t index = 0; index < table.rows.size(); ++index) {
        const GuideTableRow& row = table.rows[index];
        if (row.role == ChainRole::SourceSurface) {
            // 元の面は線を持たない。指す先だけを渡す。
            GuideChain chain;
            chain.role = row.role;
            chain.index = numbers[index];
            chain.sourceEntityId = row.sourceWireIds.front();
            request.chains.push_back(std::move(chain));
            continue;
        }
        if (row.segments.empty()) {
            return Result<GuideSurfaceRequest>::Failure(MakeError("UI-R004",
                "選んだ線がありません。",
                std::to_string(index + 1) + "行目に線が入っていません。"));
        }
        GuideChain chain;
        chain.role = row.role;
        chain.index = numbers[index];
        chain.sourceEntityId = row.sourceWireIds.front();
        chain.segments = row.segments;
        chain.closed = (row.segments.front().StartPoint() - row.segments.back().EndPoint())
                .Length()
            <= tolerance.interactiveJoinMm;
        request.chains.push_back(std::move(chain));
    }
    return Result<GuideSurfaceRequest>::Success(std::move(request));
}

} // namespace kachakacha::v2::modeling
