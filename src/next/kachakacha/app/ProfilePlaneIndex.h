#pragma once

#include "kachakacha/geometry/Vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace kachakacha::v2::app::detail {

// A curve lying in a plane necessarily starts in its tolerance slab. Index the
// actual start points, then retain the existing full-curve planarity test.
// Nearby endpoint queries reuse the same point index. Both queries reject
// candidates only; no curve or topology is approximated here.
class ProfilePlaneIndex {
public:
    explicit ProfilePlaneIndex(std::vector<geometry::Vector3> starts)
        : starts_(std::move(starts)), order_(starts_.size())
    {
        std::iota(order_.begin(), order_.end(), 0);
        nodes_.reserve(starts_.size() * 2);
        if (!starts_.empty()) Build(0, starts_.size());
    }

    [[nodiscard]] std::vector<std::size_t> Candidates(const geometry::Vector3& origin,
        const geometry::Vector3& normal, double tolerance) const
    {
        std::vector<std::size_t> result;
        const double length = normal.Length();
        if (!(length > 0)) return result;
        if (!std::isfinite(length)) result = order_;
        else if (!nodes_.empty()) Visit(0, origin, normal * (1.0 / length), tolerance, result);
        // Preserve original source priority, including coincident curves.
        std::sort(result.begin(), result.end());
        return result;
    }


    [[nodiscard]] std::vector<std::size_t> Near(const geometry::Vector3& point,
        double tolerance) const
    {
        std::vector<std::size_t> result;
        if (!nodes_.empty()) VisitNear(0, point, tolerance, result);
        std::sort(result.begin(), result.end());
        return result;
    }

private:
    struct Node {
        geometry::Bounds3 bounds;
        std::size_t begin = 0, end = 0, left = 0, right = 0;
        bool leaf = true;
    };

    [[nodiscard]] static double Axis(const geometry::Vector3& point, int axis)
    {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    }

    std::size_t Build(std::size_t begin, std::size_t end)
    {
        Node node;
        node.begin = begin;
        node.end = end;
        for (auto i = begin; i < end; ++i) node.bounds.Add(starts_[order_[i]]);
        const auto at = nodes_.size();
        nodes_.push_back(node);
        if (end - begin <= 16) return at;
        const auto size = node.bounds.Size();
        const int axis = size.x >= size.y && size.x >= size.z ? 0 : size.y >= size.z ? 1 : 2;
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + end,
            [&](auto a, auto b) {
                const auto av = Axis(starts_[a], axis), bv = Axis(starts_[b], axis);
                return av < bv || (av == bv && a < b);
            });
        const auto left = Build(begin, middle), right = Build(middle, end);
        // Do not retain references across vector growth during recursion.
        nodes_[at].left = left;
        nodes_[at].right = right;
        nodes_[at].leaf = false;
        return at;
    }

    [[nodiscard]] static bool Touches(const geometry::Bounds3& bounds,
        const geometry::Vector3& origin, const geometry::Vector3& normal, double tolerance)
    {
        const auto half = bounds.Size() * .5, center = bounds.minimum + half;
        const double distance = geometry::Dot(center - origin, normal);
        const double radius = std::abs(normal.x) * half.x + std::abs(normal.y) * half.y
            + std::abs(normal.z) * half.z;
        const double scale = 1 + std::abs(origin.x) + std::abs(origin.y) + std::abs(origin.z)
            + std::abs(center.x) + std::abs(center.y) + std::abs(center.z) + radius;
        if (!std::isfinite(distance) || !std::isfinite(radius) || !std::isfinite(scale)) return true;
        const double margin = 64 * std::numeric_limits<double>::epsilon() * scale;
        return std::abs(distance) <= radius + tolerance + margin;
    }

    void Visit(std::size_t at, const geometry::Vector3& origin, const geometry::Vector3& normal,
        double tolerance, std::vector<std::size_t>& result) const
    {
        const auto& node = nodes_[at];
        if (!Touches(node.bounds, origin, normal, tolerance)) return;
        if (node.leaf) {
            result.insert(result.end(), order_.begin() + node.begin, order_.begin() + node.end);
        } else {
            Visit(node.left, origin, normal, tolerance, result);
            Visit(node.right, origin, normal, tolerance, result);
        }
    }


    void VisitNear(std::size_t at, const geometry::Vector3& point, double tolerance,
        std::vector<std::size_t>& result) const
    {
        const auto& node = nodes_[at];
        const auto& low = node.bounds.minimum;
        const auto& high = node.bounds.maximum;
        const double scale = 1 + std::abs(point.x) + std::abs(point.y) + std::abs(point.z)
            + std::abs(low.x) + std::abs(low.y) + std::abs(low.z)
            + std::abs(high.x) + std::abs(high.y) + std::abs(high.z);
        const double radius = tolerance + 64 * std::numeric_limits<double>::epsilon() * scale;
        if (std::isfinite(radius) && (point.x < low.x - radius || point.x > high.x + radius
            || point.y < low.y - radius || point.y > high.y + radius
            || point.z < low.z - radius || point.z > high.z + radius)) return;
        if (node.leaf) {
            result.insert(result.end(), order_.begin() + node.begin, order_.begin() + node.end);
        } else {
            VisitNear(node.left, point, tolerance, result);
            VisitNear(node.right, point, tolerance, result);
        }
    }

    std::vector<geometry::Vector3> starts_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
};

} // namespace kachakacha::v2::app::detail
