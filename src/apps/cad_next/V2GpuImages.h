#pragma once
#ifdef KACHACAD_GPU_VIEW
#include "V2ImageViews.h"
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <cstdint>
#include <limits>
#include <vector>

// Display textures only. Image placement and exact picking remain in the model.
// Owned by the renderer and released with its shared GL context current.
class V2GpuImages {
public:
    bool Upload(const std::vector<const V2ImageView*>& images, std::uint64_t revision,
        const kachakacha::v2::geometry::Vector3& origin);
    void Draw(const QMatrix4x4& matrix, const kachakacha::v2::geometry::Vector3& forward);
    void Release();
    double Extent() const {return extent_;}
    std::uint64_t Uploads() const {return uploads_;}
private:
    struct Layer {int first;unsigned texture;float opacity;kachakacha::v2::geometry::Vector3 center;};
    QOpenGLShaderProgram shader_;
    QOpenGLBuffer buffer_;
    std::vector<unsigned> textures_;
    std::vector<Layer> layers_;
    std::vector<const V2ImageView*> sources_;
    std::uint64_t revision_=std::numeric_limits<std::uint64_t>::max(),uploads_=0;
    kachakacha::v2::geometry::Vector3 origin_{};
    double extent_=1;
    bool initialized_=false;
};
#endif
