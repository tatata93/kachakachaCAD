#include "kachakacha/fabrication/ClosedLoop.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

constexpr const char* kClosureFailed = "FAB-A001";
constexpr const char* kBadInput = "FAB-A004";

[[nodiscard]] double Length(const Point2& from, const Point2& to)
{
    const double du = to.u - from.u;
    const double dv = to.v - from.v;
    return std::sqrt(du * du + dv * dv);
}

[[nodiscard]] bool VertexExists(const AssemblyPanel& panel, std::size_t index)
{
    return index < panel.flatOutline.size();
}

//! 置いた板から、型紙の頂点に対応する3次元の位置を取り出す。
[[nodiscard]] bool PlacedVertex(const AssemblyResult& placed, std::size_t panelIndex,
    std::size_t vertexIndex, Vector3& out)
{
    if (panelIndex >= placed.panels.size()) {
        return false;
    }
    const PlacedPanel& panel = placed.panels[panelIndex];
    if (vertexIndex >= panel.outline.size()) {
        return false;
    }
    out = panel.outline[vertexIndex];
    return true;
}

//! いまの折り角で、貼り合わせがどれだけ開いているか。
//! 返す値は残差の並び(組ごとに始点と終点の差、xyz)。
[[nodiscard]] bool EvaluateResiduals(const std::vector<AssemblyPanel>& panels,
    const std::vector<AssemblyFold>& folds, const std::vector<MatePair>& mates,
    std::vector<double>& out)
{
    AssemblyState state;
    state.masterPercent = 100.0;
    auto placed = EvaluateAssembly(panels, folds, state);
    if (!placed.HasValue()) {
        return false;
    }
    out.clear();
    out.reserve(mates.size() * 6);
    for (const MatePair& mate : mates) {
        Vector3 firstFrom{};
        Vector3 firstTo{};
        Vector3 secondFrom{};
        Vector3 secondTo{};
        if (!PlacedVertex(placed.Value(), mate.firstPanelIndex, mate.firstFromVertex,
                firstFrom)
            || !PlacedVertex(placed.Value(), mate.firstPanelIndex, mate.firstToVertex,
                firstTo)
            || !PlacedVertex(placed.Value(), mate.secondPanelIndex,
                mate.secondFromVertex, secondFrom)
            || !PlacedVertex(placed.Value(), mate.secondPanelIndex, mate.secondToVertex,
                secondTo)) {
            return false;
        }
        const Vector3 startGap = firstFrom - secondFrom;
        const Vector3 endGap = firstTo - secondTo;
        out.push_back(startGap.x);
        out.push_back(startGap.y);
        out.push_back(startGap.z);
        out.push_back(endGap.x);
        out.push_back(endGap.y);
        out.push_back(endGap.z);
    }
    return true;
}

[[nodiscard]] double MaximumGap(const std::vector<double>& residuals)
{
    double worst = 0.0;
    for (std::size_t index = 0; index + 2 < residuals.size(); index += 3) {
        const double gap = std::sqrt(residuals[index] * residuals[index]
            + residuals[index + 1] * residuals[index + 1]
            + residuals[index + 2] * residuals[index + 2]);
        worst = std::max(worst, gap);
    }
    return worst;
}

[[nodiscard]] double RootMeanSquare(const std::vector<double>& residuals)
{
    if (residuals.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : residuals) {
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(residuals.size()));
}

//! 小さな連立一次方程式を解く。部分ピボットつきのガウス消去。
//! 解けなければ false。
[[nodiscard]] bool SolveLinearSystem(std::vector<std::vector<double>>& matrix,
    std::vector<double>& rightHand, std::vector<double>& out)
{
    const std::size_t size = rightHand.size();
    for (std::size_t column = 0; column < size; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < size; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) < 1.0e-14) {
            return false;
        }
        std::swap(matrix[column], matrix[pivot]);
        std::swap(rightHand[column], rightHand[pivot]);
        for (std::size_t row = column + 1; row < size; ++row) {
            const double factor = matrix[row][column] / matrix[column][column];
            for (std::size_t at = column; at < size; ++at) {
                matrix[row][at] -= factor * matrix[column][at];
            }
            rightHand[row] -= factor * rightHand[column];
        }
    }
    out.assign(size, 0.0);
    for (std::size_t index = size; index-- > 0;) {
        double value = rightHand[index];
        for (std::size_t at = index + 1; at < size; ++at) {
            value -= matrix[index][at] * out[at];
        }
        out[index] = value / matrix[index][index];
    }
    return true;
}

} // namespace

Result<bool> CheckMateEdgeLengths(const std::vector<AssemblyPanel>& panels,
    const std::vector<MatePair>& mates, double toleranceMm)
{
    std::vector<Diagnostic> errors;
    for (const MatePair& mate : mates) {
        if (mate.firstPanelIndex >= panels.size()
            || mate.secondPanelIndex >= panels.size()) {
            errors.push_back(MakeError(kBadInput, "貼り合わせが板を指していません。",
                mate.matePairId));
            continue;
        }
        const AssemblyPanel& first = panels[mate.firstPanelIndex];
        const AssemblyPanel& second = panels[mate.secondPanelIndex];
        if (!VertexExists(first, mate.firstFromVertex)
            || !VertexExists(first, mate.firstToVertex)
            || !VertexExists(second, mate.secondFromVertex)
            || !VertexExists(second, mate.secondToVertex)) {
            errors.push_back(MakeError(kBadInput, "貼り合わせが頂点を指していません。",
                mate.matePairId));
            continue;
        }
        const double firstLength = Length(first.flatOutline[mate.firstFromVertex],
            first.flatOutline[mate.firstToVertex]);
        const double secondLength = Length(second.flatOutline[mate.secondFromVertex],
            second.flatOutline[mate.secondToVertex]);
        if (std::abs(firstLength - secondLength) > std::max(toleranceMm, 1.0e-9)) {
            // 長さが違う辺は、どんな角度にしても合わない。
            // 伸ばして合わせることはしない。
            errors.push_back(MakeError(kBadInput,
                "貼り合わせる辺の長さが違います。",
                mate.matePairId + ": " + std::to_string(firstLength) + " mm と "
                    + std::to_string(secondLength) + " mm。"
                    + "型紙のほうを直してください。伸ばして合わせることはしません。"));
        }
    }
    if (!errors.empty()) {
        return Result<bool>::Failure(std::move(errors));
    }
    return Result<bool>::Success(true);
}

Result<ClosedLoopSolution> SolveClosedLoop(const std::vector<AssemblyPanel>& panels,
    const std::vector<AssemblyFold>& folds, const std::vector<MatePair>& mates,
    double toleranceMm, int maximumIterations)
{
    using Out = Result<ClosedLoopSolution>;
    if (folds.empty()) {
        return Out::Failure(MakeError(kBadInput, "折りがありません。", {}));
    }
    if (mates.empty()) {
        return Out::Failure(MakeError(kBadInput, "貼り合わせがありません。", {}));
    }
    auto lengths = CheckMateEdgeLengths(panels, mates, toleranceMm);
    if (!lengths.HasValue()) {
        return Out::Failure(lengths.Diagnostics());
    }

    std::vector<AssemblyFold> current = folds;
    std::vector<double> residuals;
    if (!EvaluateResiduals(panels, current, mates, residuals)) {
        return Out::Failure(MakeError(kBadInput, "組み立てを評価できませんでした。", {}));
    }

    const double limit = std::max(toleranceMm, 1.0e-9);
    const std::size_t unknowns = current.size();
    double damping = 1.0e-3;
    int iteration = 0;
    for (; iteration < maximumIterations; ++iteration) {
        if (MaximumGap(residuals) <= limit) {
            break;
        }
        // 数値微分でヤコビ行列を作る。折り角は角度なので、刻みは一定でよい。
        const double step = 1.0e-6;
        std::vector<std::vector<double>> jacobian(residuals.size(),
            std::vector<double>(unknowns, 0.0));
        for (std::size_t column = 0; column < unknowns; ++column) {
            std::vector<AssemblyFold> nudged = current;
            nudged[column].targetAngleRad += step;
            std::vector<double> shifted;
            if (!EvaluateResiduals(panels, nudged, mates, shifted)
                || shifted.size() != residuals.size()) {
                return Out::Failure(MakeError(kBadInput,
                    "組み立てを評価できませんでした。", {}));
            }
            for (std::size_t row = 0; row < residuals.size(); ++row) {
                jacobian[row][column] = (shifted[row] - residuals[row]) / step;
            }
        }

        // (JtJ + λI) Δ = -Jt r
        std::vector<std::vector<double>> normal(unknowns,
            std::vector<double>(unknowns, 0.0));
        std::vector<double> gradient(unknowns, 0.0);
        for (std::size_t row = 0; row < unknowns; ++row) {
            for (std::size_t column = 0; column < unknowns; ++column) {
                double sum = 0.0;
                for (std::size_t at = 0; at < residuals.size(); ++at) {
                    sum += jacobian[at][row] * jacobian[at][column];
                }
                normal[row][column] = sum;
            }
            double sum = 0.0;
            for (std::size_t at = 0; at < residuals.size(); ++at) {
                sum += jacobian[at][row] * residuals[at];
            }
            gradient[row] = -sum;
            normal[row][row] += damping;
        }

        std::vector<double> delta;
        if (!SolveLinearSystem(normal, gradient, delta)) {
            damping *= 10.0;
            if (damping > 1.0e12) {
                break;
            }
            continue;
        }

        std::vector<AssemblyFold> candidate = current;
        for (std::size_t index = 0; index < unknowns; ++index) {
            candidate[index].targetAngleRad += delta[index];
        }
        std::vector<double> next;
        if (!EvaluateResiduals(panels, candidate, mates, next)) {
            damping *= 10.0;
            continue;
        }
        if (RootMeanSquare(next) < RootMeanSquare(residuals)) {
            current = std::move(candidate);
            residuals = std::move(next);
            damping = std::max(damping * 0.3, 1.0e-9);
        } else {
            damping *= 10.0;
            if (damping > 1.0e12) {
                break;
            }
        }
    }

    ClosedLoopSolution solution;
    solution.folds = current;
    solution.iterations = iteration;
    solution.maximumGapMm = MaximumGap(residuals);
    solution.rootMeanSquareGapMm = RootMeanSquare(residuals);
    for (std::size_t index = 0; index < mates.size(); ++index) {
        ClosureResidual entry;
        entry.matePairId = mates[index].matePairId;
        double worst = 0.0;
        for (int half = 0; half < 2; ++half) {
            const std::size_t base = index * 6 + static_cast<std::size_t>(half) * 3;
            if (base + 2 >= residuals.size()) {
                break;
            }
            worst = std::max(worst,
                std::sqrt(residuals[base] * residuals[base]
                    + residuals[base + 1] * residuals[base + 1]
                    + residuals[base + 2] * residuals[base + 2]));
        }
        entry.gapMm = worst;
        solution.residuals.push_back(std::move(entry));
    }
    solution.converged = solution.maximumGapMm <= limit;

    if (!solution.converged) {
        // 閉じなかった。板を歪めて閉じたことにしない。
        // いちばん開いている貼り合わせを名指しして断る。
        std::string worstId;
        double worstGap = 0.0;
        for (const ClosureResidual& entry : solution.residuals) {
            if (entry.gapMm >= worstGap) {
                worstGap = entry.gapMm;
                worstId = entry.matePairId;
            }
        }
        return Out::Failure(MakeError(kClosureFailed,
            "貼り合わせが閉じません。",
            "いちばん開いているのは " + worstId + " で " + std::to_string(worstGap)
                + " mm(許容 " + std::to_string(limit) + " mm)。"
                + "板を歪めて閉じたことにはしません。"
                + "型紙の寸法か、折りのつながりを見直してください。"));
    }

    // 解けたあとも、板が伸び縮みしていないことを必ず確かめる。
    AssemblyState state;
    state.masterPercent = 100.0;
    auto placed = EvaluateAssembly(panels, solution.folds, state);
    std::vector<Diagnostic> warnings;
    if (placed.HasValue()) {
        const AssemblyMetricCheck metric = CheckAssemblyMetric(panels, placed.Value());
        if (!metric.withinTolerance) {
            return Out::Failure(metric.diagnostics);
        }
    }
    return Out::Success(std::move(solution), std::move(warnings));
}

} // namespace kachakacha::v2::fabrication
