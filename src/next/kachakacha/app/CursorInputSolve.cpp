//! カーソル連動数値入力の「解く」側(ui-workflows §7.2 / §7.3)。
//!
//! ロックした欄から形を決める。入れた順ではなく、ロックの組合せで決める。
//! 決まらない組合せ、矛盾する組合せは値を返さない。近い形へ寄せない。
#include "kachakacha/app/CursorInput.h"

#include "kachakacha/geometry/Units.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kachakacha::v2::app {
namespace {

using base::Diagnostic;
using base::MakeError;
using base::Result;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTiny = 1.0e-12;

[[nodiscard]] std::optional<double> Locked(const CursorInputPanel& panel,
    std::string_view id)
{
    for (std::size_t index = 0; index < panel.fields.size(); ++index) {
        if (panel.fields[index].id == id && panel.states[index].locked
            && panel.states[index].hasValue) {
            return panel.states[index].value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Diagnostic Conflict(std::string labelJa)
{
    return MakeError("UI-C004", "入れた値どうしが合いません。",
        std::move(labelJa) + "が、ほかの欄から決まる値と違います。"
                             "どれかのロックを外してから入れ直してください。");
}

[[nodiscard]] Diagnostic TooShort(double neededMm)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", neededMm);
    return MakeError("UI-C005", "その長さでは形が決まりません。",
        std::string("少なくとも ") + buffer + " mm 必要です。");
}

//! 作業平面の上の直線。長さ・平面内角度・du・dv の4欄で、自由度は2。
[[nodiscard]] Result<Vector3> SolvePlanarLine(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    const std::optional<double> length = Locked(panel, "length");
    const std::optional<double> angle = Locked(panel, "angle");
    const std::optional<double> du = Locked(panel, "du");
    const std::optional<double> dv = Locked(panel, "dv");

    double u = pointerDelta.x;
    double v = pointerDelta.y;
    if (du.has_value() && dv.has_value()) {
        u = *du;
        v = *dv;
    } else if (length.has_value() && angle.has_value()) {
        u = *length * std::cos(*angle);
        v = *length * std::sin(*angle);
    } else if (length.has_value() && du.has_value()) {
        const double remaining = *length * *length - *du * *du;
        if (remaining < -kTiny) {
            return Result<Vector3>::Failure(TooShort(std::abs(*du)));
        }
        u = *du;
        v = std::copysign(std::sqrt(std::max(0.0, remaining)),
            pointerDelta.y == 0.0 ? 1.0 : pointerDelta.y);
    } else if (length.has_value() && dv.has_value()) {
        const double remaining = *length * *length - *dv * *dv;
        if (remaining < -kTiny) {
            return Result<Vector3>::Failure(TooShort(std::abs(*dv)));
        }
        v = *dv;
        u = std::copysign(std::sqrt(std::max(0.0, remaining)),
            pointerDelta.x == 0.0 ? 1.0 : pointerDelta.x);
    } else if (angle.has_value() && du.has_value()) {
        const double c = std::cos(*angle);
        if (std::abs(c) < 1.0e-9) {
            return Result<Vector3>::Failure(Conflict("du"));
        }
        const double solved = *du / c;
        u = *du;
        v = solved * std::sin(*angle);
    } else if (angle.has_value() && dv.has_value()) {
        const double s = std::sin(*angle);
        if (std::abs(s) < 1.0e-9) {
            return Result<Vector3>::Failure(Conflict("dv"));
        }
        const double solved = *dv / s;
        v = *dv;
        u = solved * std::cos(*angle);
    } else if (length.has_value()) {
        const double current = std::hypot(pointerDelta.x, pointerDelta.y);
        const double scale = current > kTiny ? *length / current : 0.0;
        u = current > kTiny ? pointerDelta.x * scale : *length;
        v = current > kTiny ? pointerDelta.y * scale : 0.0;
    } else if (angle.has_value()) {
        const double current = std::hypot(pointerDelta.x, pointerDelta.y);
        u = current * std::cos(*angle);
        v = current * std::sin(*angle);
    } else if (du.has_value()) {
        u = *du;
    } else if (dv.has_value()) {
        v = *dv;
    }
    return Result<Vector3>::Success(Vector3{u, v, 0.0});
}

//! 3Dの直線。長さ・dX・dY・dZ・各軸との角度の7欄で、自由度は3。
//! 軸との角度は「長さが決まれば成分が決まる」形で効く。
[[nodiscard]] Result<Vector3> SolveSpatialLine(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    std::optional<double> length = Locked(panel, "length");
    std::array<std::optional<double>, 3> component{Locked(panel, "dx"), Locked(panel, "dy"),
        Locked(panel, "dz")};
    const std::array<std::optional<double>, 3> axisAngle{Locked(panel, "angle_x"),
        Locked(panel, "angle_y"), Locked(panel, "angle_z")};

    // 決まるところまで繰り返し埋める。順番に依らないように、動かなくなるまで回す。
    for (int round = 0; round < 6; ++round) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!axisAngle[axis].has_value()) {
                continue;
            }
            const double c = std::cos(*axisAngle[axis]);
            if (length.has_value() && !component[axis].has_value()) {
                component[axis] = *length * c;
            } else if (component[axis].has_value() && !length.has_value()
                && std::abs(c) > 1.0e-9) {
                length = *component[axis] / c;
            }
        }
        const int known = static_cast<int>(component[0].has_value())
            + static_cast<int>(component[1].has_value())
            + static_cast<int>(component[2].has_value());
        if (known == 3 && !length.has_value()) {
            length = std::sqrt(*component[0] * *component[0] + *component[1] * *component[1]
                + *component[2] * *component[2]);
        }
        if (known == 2 && length.has_value()) {
            double sum = 0.0;
            std::size_t missing = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (component[axis].has_value()) {
                    sum += *component[axis] * *component[axis];
                } else {
                    missing = axis;
                }
            }
            const double remaining = *length * *length - sum;
            if (remaining < -kTiny) {
                return Result<Vector3>::Failure(TooShort(std::sqrt(sum)));
            }
            const double pointer =
                missing == 0 ? pointerDelta.x : (missing == 1 ? pointerDelta.y : pointerDelta.z);
            component[missing] = std::copysign(std::sqrt(std::max(0.0, remaining)),
                pointer == 0.0 ? 1.0 : pointer);
        }
    }

    Vector3 delta{pointerDelta};
    std::array<double*, 3> slot{&delta.x, &delta.y, &delta.z};
    const std::array<double, 3> pointer{pointerDelta.x, pointerDelta.y, pointerDelta.z};
    double fixedSquare = 0.0;
    double freeSquare = 0.0;
    int freeCount = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (component[axis].has_value()) {
            *slot[axis] = *component[axis];
            fixedSquare += *component[axis] * *component[axis];
        } else {
            freeSquare += pointer[axis] * pointer[axis];
            ++freeCount;
        }
    }
    // 長さがロックされているなら、決まっていない成分だけを伸び縮みさせて長さを合わせる。
    // 決まった成分は動かさない。動かすと、入れた値が黙って変わってしまう。
    if (length.has_value() && freeCount > 0) {
        const double remaining = *length * *length - fixedSquare;
        if (remaining < -kTiny) {
            return Result<Vector3>::Failure(TooShort(std::sqrt(fixedSquare)));
        }
        const double wanted = std::sqrt(std::max(0.0, remaining));
        if (freeSquare > kTiny) {
            const double scale = wanted / std::sqrt(freeSquare);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (!component[axis].has_value()) {
                    *slot[axis] = pointer[axis] * scale;
                }
            }
        } else {
            // カーソルが動いていないときは、最初の自由な軸へ全部を置く。
            bool placed = false;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (component[axis].has_value()) {
                    continue;
                }
                *slot[axis] = placed ? 0.0 : wanted;
                placed = true;
            }
        }
    }
    return Result<Vector3>::Success(delta);
}

//! 円と円弧。半径・直径・中心角・円弧長は互いに決まる。
[[nodiscard]] Result<Vector3> SolveRadial(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    std::optional<double> radius = Locked(panel, "radius");
    const std::optional<double> diameter = Locked(panel, "diameter");
    const std::optional<double> sweep = Locked(panel, "sweep");
    const std::optional<double> arcLength = Locked(panel, "arc_length");
    if (!radius.has_value() && diameter.has_value()) {
        radius = *diameter * 0.5;
    }
    if (!radius.has_value() && sweep.has_value() && arcLength.has_value()) {
        if (std::abs(*sweep) < 1.0e-9) {
            return Result<Vector3>::Failure(Conflict("中心角"));
        }
        radius = *arcLength / *sweep;
    }
    if (!radius.has_value()) {
        radius = std::hypot(pointerDelta.x, pointerDelta.y);
    }
    if (*radius <= 0.0) {
        return Result<Vector3>::Failure(MakeError("UI-C006",
            "半径が正の数ではありません。", "0 より大きい値を入れてください。"));
    }
    const double angle = sweep.has_value() ? *sweep
                                           : std::atan2(pointerDelta.y, pointerDelta.x);
    return Result<Vector3>::Success(
        Vector3{*radius * std::cos(angle), *radius * std::sin(angle), 0.0});
}

//! 距離1つだけの道具。
[[nodiscard]] Result<Vector3> SolveDistance(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    const std::optional<double> distance = Locked(panel, "distance");
    if (!distance.has_value()) {
        return Result<Vector3>::Success(pointerDelta);
    }
    const double current = pointerDelta.Length();
    if (current <= kTiny) {
        return Result<Vector3>::Success(Vector3{*distance, 0.0, 0.0});
    }
    return Result<Vector3>::Success(pointerDelta * (*distance / current));
}

//! 決めた形が、ロックした欄すべてと合っているか。合わなければ、その欄の名前で断る。
[[nodiscard]] Result<Vector3> VerifyLocks(const CursorInputPanel& panel, const Vector3& delta)
{
    const double planarLength = std::hypot(delta.x, delta.y);
    for (std::size_t index = 0; index < panel.fields.size(); ++index) {
        if (!panel.states[index].locked || !panel.states[index].hasValue) {
            continue;
        }
        const std::string& id = panel.fields[index].id;
        const double wanted = panel.states[index].value;
        double actual = wanted;
        const bool planar = panel.onWorkPlane;
        if (id == "length") {
            actual = planar ? planarLength : delta.Length();
        } else if (id == "angle") {
            actual = std::atan2(delta.y, delta.x);
        } else if (id == "du" || id == "dx") {
            actual = delta.x;
        } else if (id == "dv" || id == "dy") {
            actual = delta.y;
        } else if (id == "dz") {
            actual = delta.z;
        } else if (id == "angle_x" || id == "angle_y" || id == "angle_z") {
            const double total = delta.Length();
            if (total <= kTiny) {
                continue;
            }
            const double along = id == "angle_x" ? delta.x
                                                 : (id == "angle_y" ? delta.y : delta.z);
            actual = std::acos(std::clamp(along / total, -1.0, 1.0));
        } else {
            continue;   // 半径系はここでは見ない(SolveRadial が満たす)。
        }
        const bool isAngle = panel.fields[index].kind == QuantityKind::Angle;
        double difference = std::abs(actual - wanted);
        if (isAngle) {
            while (difference > kPi) {
                difference = std::abs(difference - 2.0 * kPi);
            }
        }
        if (difference > (isAngle ? 1.0e-7 : 1.0e-6)) {
            return Result<Vector3>::Failure(Conflict(panel.fields[index].labelJa));
        }
    }
    return Result<Vector3>::Success(delta);
}

} // namespace

Result<Vector3> SolveDelta(const CursorInputPanel& panel, const Vector3& pointerDelta)
{
    if (!panel.active) {
        return Result<Vector3>::Failure(MakeError("UI-C002",
            "数値入力が出ていません。", "先に点を置いてください。"));
    }
    if (!pointerDelta.IsFinite()) {
        return Result<Vector3>::Failure(MakeError("UI-C007",
            "カーソルの位置に数値でない値が入っています。", "画面の写し方を確かめます。"));
    }
    Result<Vector3> solved = Result<Vector3>::Success(pointerDelta);
    switch (panel.tool) {
    case DrawingTool::Line:
    case DrawingTool::Polyline:
        solved = panel.onWorkPlane ? SolvePlanarLine(panel, pointerDelta)
                                   : SolveSpatialLine(panel, pointerDelta);
        break;
    case DrawingTool::Circle:
    case DrawingTool::Arc:
        solved = SolveRadial(panel, pointerDelta);
        break;
    case DrawingTool::Move:
        solved = SolveDistance(panel, pointerDelta);
        break;
    default:
        return Result<Vector3>::Failure(MakeError("UI-C001",
            "この道具では数値入力を使いません。", "点を置いて形を決める道具ではありません。"));
    }
    if (!solved.HasValue()) {
        return solved;
    }
    return VerifyLocks(panel, solved.Value());
}

Result<CursorInputPanel> UpdateFromPointer(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    if (!panel.active) {
        return Result<CursorInputPanel>::Failure(MakeError("UI-C002",
            "数値入力が出ていません。", "先に点を置いてください。"));
    }
    const Result<Vector3> solved = SolveDelta(panel, pointerDelta);
    if (!solved.HasValue()) {
        return Result<CursorInputPanel>::Failure(solved.Diagnostics());
    }
    const Vector3 delta = solved.Value();
    CursorInputPanel next = panel;
    for (std::size_t index = 0; index < next.fields.size(); ++index) {
        if (next.states[index].locked) {
            continue;   // ロックした欄はマウスで動かさない。
        }
        const std::string& id = next.fields[index].id;
        std::optional<double> value;
        const double planarLength = std::hypot(delta.x, delta.y);
        const double total = delta.Length();
        if (id == "length") {
            value = next.onWorkPlane ? planarLength : total;
        } else if (id == "angle") {
            value = std::atan2(delta.y, delta.x);
        } else if (id == "du" || id == "dx") {
            value = delta.x;
        } else if (id == "dv" || id == "dy") {
            value = delta.y;
        } else if (id == "dz") {
            value = delta.z;
        } else if (id == "angle_x" || id == "angle_y" || id == "angle_z") {
            if (total > 1.0e-12) {
                const double along = id == "angle_x"
                    ? delta.x
                    : (id == "angle_y" ? delta.y : delta.z);
                value = std::acos(std::clamp(along / total, -1.0, 1.0));
            }
        } else if (id == "radius") {
            value = planarLength;
        } else if (id == "diameter") {
            value = planarLength * 2.0;
        } else if (id == "sweep") {
            value = std::atan2(delta.y, delta.x);
        } else if (id == "arc_length") {
            value = planarLength * std::atan2(delta.y, delta.x);
        } else if (id == "distance") {
            value = total;
        }
        if (value.has_value()) {
            next.states[index].value = *value;
            next.states[index].hasValue = true;
            next.states[index].text.clear();   // 式は打った人のもの。動かすときは消す。
            next.states[index].error = false;
            next.states[index].messageJa.clear();
        }
    }
    return Result<CursorInputPanel>::Success(std::move(next));
}

Result<CursorCommitResult> CommitFocusedField(const CursorInputPanel& panel,
    const Vector3& pointerDelta)
{
    if (!panel.active || panel.focusedIndex >= panel.fields.size()) {
        return Result<CursorCommitResult>::Failure(MakeError("UI-C002",
            "数値入力が出ていません。", "先に点を置いてください。"));
    }
    const std::size_t index = panel.focusedIndex;
    const CursorField& field = panel.fields[index];
    CursorInputPanel next = panel;

    if (!panel.states[index].text.empty()) {
        const auto evaluated = geometry::EvaluateExpression(panel.states[index].text,
            field.kind);
        if (!evaluated.HasValue()) {
            // 赤くして、その欄だけ確定しない。ほかの欄は触らない。
            next.states[index].error = true;
            next.states[index].messageJa = evaluated.Diagnostics().front().summaryJa;
            CursorCommitResult failed{std::move(next), false};
            return Result<CursorCommitResult>::Failure(evaluated.Diagnostics());
        }
        next.states[index].value = evaluated.Value().value;
        next.states[index].hasValue = true;
    }
    if (!next.states[index].hasValue) {
        return Result<CursorCommitResult>::Failure(MakeError("UI-C008",
            "その欄がまだ空です。", field.labelJa + "に値を入れてください。"));
    }
    next.states[index].locked = true;
    next.states[index].error = false;
    next.states[index].messageJa.clear();

    const Result<Vector3> solved = SolveDelta(next, pointerDelta);
    if (!solved.HasValue()) {
        // 矛盾。最後の変更だけを取り消し、その欄を赤くする。
        CursorInputPanel reverted = panel;
        reverted.states[index].error = true;
        reverted.states[index].messageJa = solved.Diagnostics().front().summaryJa;
        return Result<CursorCommitResult>::Failure(solved.Diagnostics());
    }
    const auto refreshed = UpdateFromPointer(next, pointerDelta);
    if (refreshed.HasValue()) {
        next = refreshed.Value();
    }

    // 必要な欄がそろったか。主要欄がロックされていれば形は決まる。
    bool ready = false;
    for (std::size_t at = 0; at < next.fields.size(); ++at) {
        if (next.fields[at].primary && next.states[at].locked) {
            ready = true;
        }
    }
    CursorCommitResult result{std::move(next), ready};
    return Result<CursorCommitResult>::Success(std::move(result));
}

} // namespace kachakacha::v2::app
