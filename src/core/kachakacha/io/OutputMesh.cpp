#include "kachakacha/io/OutputMesh.h"

#include "kachakacha/model/AutoSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace kachakacha::io {

namespace {

using geometry::Vector3;
using model::Plate;
using model::Project;
using model::ProjectObjectKind;
using model::Surface;
using model::Wire;

//! 同じ位置の頂点をまとめる格子(溶接)。STLの縁判定は頂点一致が前提。
class VertexWelder {
public:
    explicit VertexWelder(std::vector<Vector3>& vertices)
        : vertices_(vertices)
    {
    }

    [[nodiscard]] int Add(const Vector3& point)
    {
        const Key key{Quantize(point.x), Quantize(point.y), Quantize(point.z)};
        const auto found = index_.find(key);
        if (found != index_.end()) {
            return found->second;
        }
        const int index = static_cast<int>(vertices_.size());
        vertices_.push_back(point);
        index_.emplace(key, index);
        return index;
    }

private:
    using Key = std::array<long long, 3>;

    [[nodiscard]] static long long Quantize(double value)
    {
        // 1/1000 mm 単位で丸める(モデルの寸法はmm)。
        return static_cast<long long>(std::llround(value * 1000.0));
    }

    std::vector<Vector3>& vertices_;
    std::map<Key, int> index_;
};

void AddTriangle(OutputMesh& mesh, int a, int b, int c, bool fill)
{
    if (a == b || b == c || a == c) {
        return; // 潰れた三角形は捨てる。
    }
    const Vector3 normal = Cross(
        mesh.vertices[static_cast<std::size_t>(b)] - mesh.vertices[static_cast<std::size_t>(a)],
        mesh.vertices[static_cast<std::size_t>(c)] - mesh.vertices[static_cast<std::size_t>(a)]);
    if (normal.LengthSquared() <= 1.0e-18) {
        return;
    }
    mesh.triangles.push_back({a, b, c, fill});
}

//! 面(u,v)を格子状に三角形化して、指定の厚みぶん膨らませた薄板にする。
void AddThickSurface(
    OutputMesh& mesh,
    VertexWelder& welder,
    const Surface& surface,
    double thickness,
    int samples,
    bool fill)
{
    const int steps = std::max(2, samples);
    const double half = std::abs(thickness) * 0.5;
    std::vector<std::vector<int>> lower(
        static_cast<std::size_t>(steps) + 1, std::vector<int>(static_cast<std::size_t>(steps) + 1, 0));
    std::vector<std::vector<int>> upper = lower;
    for (int uIndex = 0; uIndex <= steps; ++uIndex) {
        for (int vIndex = 0; vIndex <= steps; ++vIndex) {
            const double u = static_cast<double>(uIndex) / steps;
            const double v = static_cast<double>(vIndex) / steps;
            Vector3 point = surface.Evaluate(u, v);
            Vector3 normal = surface.Normal(u, v);
            if (!normal.IsFinite() || normal.LengthSquared() <= 1.0e-18) {
                normal = Vector3{0.0, 0.0, 1.0};
            } else {
                normal = normal.Normalized();
            }
            lower[static_cast<std::size_t>(uIndex)][static_cast<std::size_t>(vIndex)]
                = welder.Add(point - normal * half);
            upper[static_cast<std::size_t>(uIndex)][static_cast<std::size_t>(vIndex)]
                = welder.Add(point + normal * half);
        }
    }
    const auto at = [](const std::vector<std::vector<int>>& grid, int u, int v) {
        return grid[static_cast<std::size_t>(u)][static_cast<std::size_t>(v)];
    };
    for (int uIndex = 0; uIndex < steps; ++uIndex) {
        for (int vIndex = 0; vIndex < steps; ++vIndex) {
            // 上面(法線側)と下面(裏)。裏は向きを反転して外向きに保つ。
            AddTriangle(mesh, at(upper, uIndex, vIndex), at(upper, uIndex + 1, vIndex),
                at(upper, uIndex + 1, vIndex + 1), fill);
            AddTriangle(mesh, at(upper, uIndex, vIndex), at(upper, uIndex + 1, vIndex + 1),
                at(upper, uIndex, vIndex + 1), fill);
            AddTriangle(mesh, at(lower, uIndex, vIndex), at(lower, uIndex + 1, vIndex + 1),
                at(lower, uIndex + 1, vIndex), fill);
            AddTriangle(mesh, at(lower, uIndex, vIndex), at(lower, uIndex, vIndex + 1),
                at(lower, uIndex + 1, vIndex + 1), fill);
        }
    }
    // 4辺の側面(板の小口)。
    for (int index = 0; index < steps; ++index) {
        AddTriangle(mesh, at(lower, index, 0), at(lower, index + 1, 0), at(upper, index + 1, 0), fill);
        AddTriangle(mesh, at(lower, index, 0), at(upper, index + 1, 0), at(upper, index, 0), fill);
        AddTriangle(mesh, at(lower, index + 1, steps), at(lower, index, steps), at(upper, index, steps), fill);
        AddTriangle(mesh, at(lower, index + 1, steps), at(upper, index, steps), at(upper, index + 1, steps), fill);
        AddTriangle(mesh, at(lower, 0, index + 1), at(lower, 0, index), at(upper, 0, index), fill);
        AddTriangle(mesh, at(lower, 0, index + 1), at(upper, 0, index), at(upper, 0, index + 1), fill);
        AddTriangle(mesh, at(lower, steps, index), at(lower, steps, index + 1), at(upper, steps, index + 1), fill);
        AddTriangle(mesh, at(lower, steps, index), at(upper, steps, index + 1), at(upper, steps, index), fill);
    }
}

//! 板材・治具など「厚みを持つもの」を格子状に三角形化する。
template <typename Solid>
void AddSolid(OutputMesh& mesh, VertexWelder& welder, const Solid& solid, int samples)
{
    const int steps = std::max(2, samples);
    std::vector<std::vector<int>> lower(
        static_cast<std::size_t>(steps) + 1, std::vector<int>(static_cast<std::size_t>(steps) + 1, 0));
    std::vector<std::vector<int>> upper = lower;
    for (int uIndex = 0; uIndex <= steps; ++uIndex) {
        for (int vIndex = 0; vIndex <= steps; ++vIndex) {
            const double u = static_cast<double>(uIndex) / steps;
            const double v = static_cast<double>(vIndex) / steps;
            lower[static_cast<std::size_t>(uIndex)][static_cast<std::size_t>(vIndex)]
                = welder.Add(solid.Evaluate(u, v, 0.0));
            upper[static_cast<std::size_t>(uIndex)][static_cast<std::size_t>(vIndex)]
                = welder.Add(solid.Evaluate(u, v, 1.0));
        }
    }
    const auto at = [](const std::vector<std::vector<int>>& grid, int u, int v) {
        return grid[static_cast<std::size_t>(u)][static_cast<std::size_t>(v)];
    };
    for (int uIndex = 0; uIndex < steps; ++uIndex) {
        for (int vIndex = 0; vIndex < steps; ++vIndex) {
            AddTriangle(mesh, at(upper, uIndex, vIndex), at(upper, uIndex + 1, vIndex),
                at(upper, uIndex + 1, vIndex + 1), false);
            AddTriangle(mesh, at(upper, uIndex, vIndex), at(upper, uIndex + 1, vIndex + 1),
                at(upper, uIndex, vIndex + 1), false);
            AddTriangle(mesh, at(lower, uIndex, vIndex), at(lower, uIndex + 1, vIndex + 1),
                at(lower, uIndex + 1, vIndex), false);
            AddTriangle(mesh, at(lower, uIndex, vIndex), at(lower, uIndex, vIndex + 1),
                at(lower, uIndex + 1, vIndex + 1), false);
        }
    }
    for (int index = 0; index < steps; ++index) {
        AddTriangle(mesh, at(lower, index, 0), at(lower, index + 1, 0), at(upper, index + 1, 0), false);
        AddTriangle(mesh, at(lower, index, 0), at(upper, index + 1, 0), at(upper, index, 0), false);
        AddTriangle(mesh, at(lower, index + 1, steps), at(lower, index, steps), at(upper, index, steps), false);
        AddTriangle(mesh, at(lower, index + 1, steps), at(upper, index, steps), at(upper, index + 1, steps), false);
        AddTriangle(mesh, at(lower, 0, index + 1), at(lower, 0, index), at(upper, 0, index), false);
        AddTriangle(mesh, at(lower, 0, index + 1), at(upper, 0, index), at(upper, 0, index + 1), false);
        AddTriangle(mesh, at(lower, steps, index), at(lower, steps, index + 1), at(upper, steps, index + 1), false);
        AddTriangle(mesh, at(lower, steps, index), at(upper, steps, index + 1), at(upper, steps, index), false);
    }
}

//! 境界の縁(1つの三角形にしか使われていない辺)をつなげて輪に戻す。
[[nodiscard]] std::vector<std::vector<int>> FindOpenLoops(const OutputMesh& mesh)
{
    std::map<std::pair<int, int>, int> edgeUse;
    for (const OutputMesh::Triangle& triangle : mesh.triangles) {
        const std::array<std::pair<int, int>, 3> edges = {
            std::pair<int, int>{triangle.a, triangle.b},
            std::pair<int, int>{triangle.b, triangle.c},
            std::pair<int, int>{triangle.c, triangle.a},
        };
        for (const auto& edge : edges) {
            const std::pair<int, int> key = edge.first < edge.second
                ? edge
                : std::pair<int, int>{edge.second, edge.first};
            ++edgeUse[key];
        }
    }
    std::multimap<int, int> openNeighbours;
    for (const auto& [edge, count] : edgeUse) {
        if (count != 1) {
            continue;
        }
        openNeighbours.emplace(edge.first, edge.second);
        openNeighbours.emplace(edge.second, edge.first);
    }
    std::set<std::pair<int, int>> visited;
    std::vector<std::vector<int>> loops;
    for (const auto& [start, unusedNeighbour] : openNeighbours) {
        static_cast<void>(unusedNeighbour);
        std::vector<int> loop;
        int current = start;
        int previous = -1;
        while (true) {
            loop.push_back(current);
            bool advanced = false;
            const auto range = openNeighbours.equal_range(current);
            for (auto iterator = range.first; iterator != range.second; ++iterator) {
                const int next = iterator->second;
                if (next == previous) {
                    continue;
                }
                const std::pair<int, int> key = current < next
                    ? std::pair<int, int>{current, next}
                    : std::pair<int, int>{next, current};
                if (visited.count(key) != 0) {
                    continue;
                }
                visited.insert(key);
                previous = current;
                current = next;
                advanced = true;
                break;
            }
            if (!advanced) {
                break;
            }
            if (current == start) {
                break; // 一周した。
            }
        }
        if (loop.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

//! 輪の重心へ扇状に三角形を張って穴をふさぐ。
void FillLoop(OutputMesh& mesh, VertexWelder& welder, const std::vector<int>& loop)
{
    Vector3 centroid{0.0, 0.0, 0.0};
    for (const int index : loop) {
        centroid = centroid + mesh.vertices[static_cast<std::size_t>(index)];
    }
    centroid = centroid * (1.0 / static_cast<double>(loop.size()));
    const int center = welder.Add(centroid);
    for (std::size_t index = 0; index < loop.size(); ++index) {
        const int a = loop[index];
        const int b = loop[(index + 1) % loop.size()];
        AddTriangle(mesh, center, a, b, true);
    }
}

[[nodiscard]] int CountOpenEdges(const OutputMesh& mesh)
{
    std::map<std::pair<int, int>, int> edgeUse;
    for (const OutputMesh::Triangle& triangle : mesh.triangles) {
        const std::array<std::pair<int, int>, 3> edges = {
            std::pair<int, int>{triangle.a, triangle.b},
            std::pair<int, int>{triangle.b, triangle.c},
            std::pair<int, int>{triangle.c, triangle.a},
        };
        for (const auto& edge : edges) {
            const std::pair<int, int> key = edge.first < edge.second
                ? edge
                : std::pair<int, int>{edge.second, edge.first};
            ++edgeUse[key];
        }
    }
    int open = 0;
    for (const auto& [edge, count] : edgeUse) {
        static_cast<void>(edge);
        if (count == 1) {
            ++open;
        }
    }
    return open;
}

} // namespace

OutputMesh BuildOutputMesh(
    const Project& project,
    const std::vector<OutputItem>& items,
    const OutputMeshOptions& options)
{
    OutputMesh mesh;
    VertexWelder welder(mesh.vertices);
    const double thickness = std::max(0.01, options.surfaceThicknessMillimeters);

    int surfaceCount = 0;
    int plateCount = 0;
    int bodyCount = 0;
    std::vector<Wire> loopWires;
    for (const OutputItem& item : items) {
        switch (item.kind) {
        case ProjectObjectKind::Surface: {
            const auto surface = project.FindSurface(item.name);
            if (!surface.has_value()) {
                continue;
            }
            AddThickSurface(mesh, welder, *surface, thickness, options.surfaceSamples, false);
            ++surfaceCount;
            break;
        }
        case ProjectObjectKind::Plate: {
            const auto plate = project.FindPlate(item.name);
            if (!plate.has_value()) {
                continue;
            }
            AddSolid(mesh, welder, *plate, options.surfaceSamples);
            ++plateCount;
            break;
        }
        case ProjectObjectKind::Body: {
            const auto body = project.FindBody(item.name);
            if (!body.has_value()) {
                continue;
            }
            AddSolid(mesh, welder, *body, options.surfaceSamples);
            ++bodyCount;
            break;
        }
        case ProjectObjectKind::Wire: {
            for (const model::NamedWire& wire : project.Wires()) {
                if (wire.name == item.name) {
                    loopWires.push_back(wire.wire);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    // ワイヤは「囲んでいる形」を面にしてから薄板にする(閉じていなければ自動連結)。
    if (!loopWires.empty()) {
        try {
            model::AutoSurfaceResult built = model::BuildAutoSurface(loopWires);
            AddThickSurface(mesh, welder, built.surface, thickness, options.surfaceSamples, true);
            mesh.notes.push_back(
                "選んだ線から面を作って厚み" + std::to_string(thickness) + "mmで出力します");
        } catch (const std::exception& error) {
            mesh.notes.push_back(
                std::string("線から面を作れませんでした: ") + error.what());
        }
    }

    if (surfaceCount > 0) {
        mesh.notes.push_back("面は厚み"
            + std::to_string(thickness) + "mmの板として出力します");
    }
    static_cast<void>(plateCount);
    static_cast<void>(bodyCount);

    if (options.fillOpenBoundaries && !mesh.triangles.empty()) {
        const std::vector<std::vector<int>> loops = FindOpenLoops(mesh);
        for (const std::vector<int>& loop : loops) {
            FillLoop(mesh, welder, loop);
            ++mesh.filledLoopCount;
        }
        if (mesh.filledLoopCount > 0) {
            mesh.notes.push_back("開いていた縁を"
                + std::to_string(mesh.filledLoopCount) + "か所、自動でふさぎました(橙色)");
        }
    }
    mesh.openEdgeCount = CountOpenEdges(mesh);
    if (mesh.openEdgeCount > 0) {
        mesh.notes.push_back("まだ閉じていない縁が"
            + std::to_string(mesh.openEdgeCount) + "本あります(3Dプリントでは要注意)");
    }
    return mesh;
}

void WriteOutputMeshStl(const std::string& path, const OutputMesh& mesh)
{
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("出力する形がありません。");
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("STLを書き出せませんでした: " + path);
    }
    char header[80] = {};
    const std::string title = "kachakachaCAD output";
    std::copy(title.begin(), title.end(), header);
    output.write(header, sizeof(header));
    const auto count = static_cast<std::uint32_t>(mesh.triangles.size());
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    const auto writeVector = [&output](const Vector3& value) {
        const float data[3] = {
            static_cast<float>(value.x),
            static_cast<float>(value.y),
            static_cast<float>(value.z),
        };
        output.write(reinterpret_cast<const char*>(data), sizeof(data));
    };
    for (const OutputMesh::Triangle& triangle : mesh.triangles) {
        const Vector3& a = mesh.vertices[static_cast<std::size_t>(triangle.a)];
        const Vector3& b = mesh.vertices[static_cast<std::size_t>(triangle.b)];
        const Vector3& c = mesh.vertices[static_cast<std::size_t>(triangle.c)];
        Vector3 normal = Cross(b - a, c - a);
        normal = normal.LengthSquared() > 1.0e-18 ? normal.Normalized() : Vector3{0.0, 0.0, 1.0};
        writeVector(normal);
        writeVector(a);
        writeVector(b);
        writeVector(c);
        const std::uint16_t attribute = 0;
        output.write(reinterpret_cast<const char*>(&attribute), sizeof(attribute));
    }
}

} // namespace kachakacha::io
