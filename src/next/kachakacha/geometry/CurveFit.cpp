#include "kachakacha/geometry/CurveFit.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::geometry {
namespace {

using base::MakeError;
using base::Result;

//! t での一様 3 次 B-spline の基底(どの区間の、どの 4 つの重みか)。
struct Basis {
    std::size_t first = 0;
    std::array<double, 4> weights{};
};

[[nodiscard]] Basis BasisAt(double t, std::size_t controlPoints)
{
    const std::size_t spans = controlPoints - 3;
    const double scaled = std::clamp(t, 0.0, 1.0) * static_cast<double>(spans);
    std::size_t span = static_cast<std::size_t>(std::floor(scaled));
    if (span >= spans) {
        span = spans - 1;
    }
    const double u = scaled - static_cast<double>(span);
    const double u2 = u * u;
    const double u3 = u2 * u;
    Basis basis;
    basis.first = span;
    basis.weights = {(1.0 - 3.0 * u + 3.0 * u2 - u3) / 6.0, (4.0 - 6.0 * u2 + 3.0 * u3) / 6.0,
        (1.0 + 3.0 * u + 3.0 * u2 - 3.0 * u3) / 6.0, u3 / 6.0};
    return basis;
}

//! 部分ピボット付きの消去で A x = b を解く(A は n×n、行優先)。解けなければ偽。
[[nodiscard]] bool Solve(std::vector<double>& a, std::vector<double>& b, std::size_t n)
{
    for (std::size_t column = 0; column < n; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < n; ++row) {
            if (std::abs(a[row * n + column]) > std::abs(a[pivot * n + column])) {
                pivot = row;
            }
        }
        if (!(std::abs(a[pivot * n + column]) > 1.0e-14)) {
            return false;
        }
        if (pivot != column) {
            for (std::size_t k = 0; k < n; ++k) {
                std::swap(a[column * n + k], a[pivot * n + k]);
            }
            std::swap(b[column], b[pivot]);
        }
        for (std::size_t row = column + 1; row < n; ++row) {
            const double factor = a[row * n + column] / a[column * n + column];
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t k = column; k < n; ++k) {
                a[row * n + k] -= factor * a[column * n + k];
            }
            b[row] -= factor * b[column];
        }
    }
    for (std::size_t row = n; row-- > 0;) {
        double sum = b[row];
        for (std::size_t k = row + 1; k < n; ++k) {
            sum -= a[row * n + k] * b[k];
        }
        b[row] = sum / a[row * n + row];
    }
    return true;
}

//! n 個の制御点で、両端を通す最小二乗。解けなければ空。
[[nodiscard]] std::vector<Vector3> FitWith(const std::vector<double>& parameters,
    const std::vector<Vector3>& points, std::size_t n)
{
    // 未知: 制御点 n 個 + 端の拘束の乗数 2 個(座標ごとに同じ行列)。
    const std::size_t size = n + 2;
    std::vector<double> matrix(size * size, 0.0);
    std::vector<std::array<double, 3>> rhs(size, {0.0, 0.0, 0.0});
    for (std::size_t k = 0; k < parameters.size(); ++k) {
        const Basis basis = BasisAt(parameters[k], n);
        for (std::size_t i = 0; i < 4; ++i) {
            const std::size_t row = basis.first + i;
            for (std::size_t j = 0; j < 4; ++j) {
                matrix[row * size + basis.first + j] += 2.0 * basis.weights[i] * basis.weights[j];
            }
            rhs[row][0] += 2.0 * basis.weights[i] * points[k].x;
            rhs[row][1] += 2.0 * basis.weights[i] * points[k].y;
            rhs[row][2] += 2.0 * basis.weights[i] * points[k].z;
        }
    }
    // 端の拘束: C(0) = 始点、C(1) = 終点。
    const Basis start = BasisAt(0.0, n);
    const Basis end = BasisAt(1.0, n);
    for (std::size_t i = 0; i < 4; ++i) {
        matrix[n * size + start.first + i] = start.weights[i];
        matrix[(start.first + i) * size + n] = start.weights[i];
        matrix[(n + 1) * size + end.first + i] = end.weights[i];
        matrix[(end.first + i) * size + n + 1] = end.weights[i];
    }
    rhs[n] = {points.front().x, points.front().y, points.front().z};
    rhs[n + 1] = {points.back().x, points.back().y, points.back().z};
    std::vector<Vector3> control(n);
    for (int axis = 0; axis < 3; ++axis) {
        std::vector<double> a = matrix;
        std::vector<double> b(size);
        for (std::size_t row = 0; row < size; ++row) {
            b[row] = rhs[row][static_cast<std::size_t>(axis)];
        }
        if (!Solve(a, b, size)) {
            return {};
        }
        for (std::size_t i = 0; i < n; ++i) {
            (axis == 0 ? control[i].x : axis == 1 ? control[i].y : control[i].z) = b[i];
        }
    }
    return control;
}

} // namespace

Result<CurveFitResult> FitUniformCubicBSpline(const std::function<Vector3(double)>& curve,
    double toleranceMm, std::size_t preferredControlPoints, std::size_t maximumControlPoints)
{
    std::vector<std::size_t> counts;
    if (preferredControlPoints >= 4 && preferredControlPoints <= maximumControlPoints) {
        counts.push_back(preferredControlPoints);
    }
    for (std::size_t n = 4; n <= maximumControlPoints; n = std::max(n + 1, n * 3 / 2)) {
        counts.push_back(n);
    }
    if (counts.empty() || counts.back() != maximumControlPoints) {
        counts.push_back(maximumControlPoints);
    }
    double best = -1.0;
    for (const std::size_t n : counts) {
        if (n < 4) {
            continue;
        }
        const std::size_t samples = std::max<std::size_t>(64, n * 4);
        std::vector<double> parameters(samples);
        std::vector<Vector3> points(samples);
        for (std::size_t k = 0; k < samples; ++k) {
            parameters[k] = static_cast<double>(k) / static_cast<double>(samples - 1);
            points[k] = curve(parameters[k]);
        }
        const std::vector<Vector3> control = FitWith(parameters, points, n);
        if (control.size() != n) {
            continue;
        }
        auto made = CurveSegment::MakeCubicBSpline(control);
        if (!made.HasValue()) {
            continue;
        }
        // 合わせに使っていない t(間)でも測る。
        double worst = 0.0;
        const std::size_t checks = samples * 4;
        for (std::size_t k = 0; k <= checks; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(checks);
            worst = std::max(worst, (made.Value().Evaluate(t) - curve(t)).Length());
        }
        best = best < 0.0 ? worst : std::min(best, worst);
        if (worst <= toleranceMm) {
            return Result<CurveFitResult>::Success(CurveFitResult{made.Value(), worst, n});
        }
    }
    return Result<CurveFitResult>::Failure(MakeError(kCurveFitFailed,
        "曲線を、形を保ったまま core の B-spline へ写せませんでした。",
        "制御点 " + std::to_string(maximumControlPoints) + " 個でも最大 "
            + std::to_string(best) + " mm 外れます(許容 " + std::to_string(toleranceMm)
            + " mm)。折れ線へ落として返すことはしません。"));
}

std::vector<std::array<Vector3, 4>> UniformBSplineBezierSpans(
    const std::vector<Vector3>& q)
{
    std::vector<std::array<Vector3, 4>> spans;
    if (q.size() < 4) {
        return spans;
    }
    for (std::size_t j = 0; j + 3 < q.size(); ++j) {
        spans.push_back({(q[j] + q[j + 1] * 4.0 + q[j + 2]) * (1.0 / 6.0),
            (q[j + 1] * 4.0 + q[j + 2] * 2.0) * (1.0 / 6.0),
            (q[j + 1] * 2.0 + q[j + 2] * 4.0) * (1.0 / 6.0),
            (q[j + 1] + q[j + 2] * 4.0 + q[j + 3]) * (1.0 / 6.0)});
    }
    return spans;
}

} // namespace kachakacha::v2::geometry
