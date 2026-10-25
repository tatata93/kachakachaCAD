#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <QImage>
#include <memory>
#include <vector>
#include <functional>
struct V2ImageView;
class QPainter;
class QPaintDevice;

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
        const kachakacha::v2::geometry::Vector3& forward, int width, int height,
        const std::vector<const V2ImageView*>& images={}, std::uint64_t imageRevision=0);
    const char* Backend() const;
    bool PaintFrame(QPainter& destination, int width, int height,
        const std::function<void(QPainter&)>& draw);
    bool PaintingFrame() const;
    bool PaintDirect(QPaintDevice& target, int width, int height, double devicePixelRatio,
        const std::function<void(QPainter&)>& draw);
    std::uint64_t Readbacks() const;
    bool RenderSucceeded() const;
    std::uint64_t ImageUploads() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
