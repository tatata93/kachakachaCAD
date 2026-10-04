#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <QImage>
#include <memory>
#include <vector>
#include <functional>
class QPainter;

// Display-only GPU cache. Exact CAD geometry and CPU picking stay unchanged.
class V2GpuRenderer {
public:
    struct Item {
        const kachakacha::v2::modeling::ShapeMesh* mesh;
        unsigned fill, edge;
        bool visible, emphasized;
    };
    V2GpuRenderer();
    ~V2GpuRenderer();
    QImage Render(const std::vector<Item>& items, std::uint64_t revision,
        const kachakacha::v2::geometry::ScreenMapping& mapping,
        const kachakacha::v2::geometry::Vector3& forward, int width, int height);
    const char* Backend() const;
    bool PaintFrame(QPainter& destination, int width, int height,
        const std::function<void(QPainter&)>& draw);
    bool PaintingFrame() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
