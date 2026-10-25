#include "V2GpuImages.h"
#ifdef KACHACAD_GPU_VIEW
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QImage>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>

namespace {
const char* vertexShader=R"(
attribute vec3 position;
attribute vec2 pixel;
uniform mat4 matrix;
varying vec2 uv;
void main(){gl_Position=matrix*vec4(position,1.0);uv=pixel;}
)";
const char* fragmentShader=R"(
uniform sampler2D image;
uniform float opacity;
varying vec2 uv;
void main(){
    if(uv.x<0.0||uv.y<0.0||uv.x>=1.0||uv.y>=1.0)discard;
    vec4 value=texture2D(image,uv);value.a*=opacity;
    if(value.a<=0.0)discard;
    gl_FragColor=value;
}
)";
struct Vertex {float x,y,z,u,v;};
}

void V2GpuImages::Release() {
    auto* context=QOpenGLContext::currentContext();
    if(context&&!textures_.empty())context->functions()->glDeleteTextures(int(textures_.size()),textures_.data());
    textures_.clear();layers_.clear();sources_.clear();
    revision_=std::numeric_limits<std::uint64_t>::max();
}

bool V2GpuImages::Upload(const std::vector<const V2ImageView*>& images,std::uint64_t revision,
    const kachakacha::v2::geometry::Vector3& origin) {
    if(revision_==revision&&sources_==images&&origin_==origin)return true;
    Release();extent_=1;
    if(images.empty()){revision_=revision;origin_=origin;return true;}
    if(!initialized_) {
        initialized_=shader_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexShader)
            &&shader_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentShader)&&shader_.link()&&buffer_.create();
        if(!initialized_)return false;
    }
    auto* gl=QOpenGLContext::currentContext()->functions();GLint limit=0;
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    std::vector<Vertex> vertices;
    for(const auto* source:images) {
        const auto& image=source->image;
        if(image.width<=0||image.height<=0||image.width>limit||image.height>limit
            ||image.pixels.size()!=std::size_t(image.width)*image.height){Release();return false;}
        QImage pixels(reinterpret_cast<const uchar*>(image.pixels.data()),image.width,image.height,QImage::Format_ARGB32);
        pixels=pixels.convertToFormat(QImage::Format_RGBA8888);
        if(pixels.isNull()){Release();return false;}
        unsigned texture=0;gl->glGenTextures(1,&texture);textures_.push_back(texture);
        gl->glActiveTexture(GL_TEXTURE0);gl->glBindTexture(GL_TEXTURE_2D,texture);
        gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        gl->glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,image.width,image.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.constBits());
        if(gl->glGetError()!=GL_NO_ERROR){Release();return false;}
        ++uploads_;
        for(const auto& triangle:source->triangles) {
            const auto center=(triangle.mesh.points[0]+triangle.mesh.points[1]+triangle.mesh.points[2])*(1./3);
            layers_.push_back({int(vertices.size()),texture,float(image.opacity),center});
            for(int i=0;i<3;++i) {
                const auto p=triangle.mesh.points[i]-origin;const auto uv=triangle.pixels[i];
                extent_=std::max(extent_,p.Length());
                vertices.push_back({float(p.x),float(p.y),float(p.z),float(uv.x/image.width),float(uv.y/image.height)});
            }
        }
    }
    buffer_.bind();buffer_.allocate(vertices.data(),int(vertices.size()*sizeof(Vertex)));buffer_.release();
    gl->glBindTexture(GL_TEXTURE_2D,0);revision_=revision;sources_=images;origin_=origin;
    return true;
}

void V2GpuImages::Draw(const QMatrix4x4& matrix,const kachakacha::v2::geometry::Vector3& forward) {
    if(layers_.empty())return;
    std::vector<const Layer*> order;order.reserve(layers_.size());
    for(const auto& layer:layers_)order.push_back(&layer);
    std::stable_sort(order.begin(),order.end(),[&](const auto* a,const auto* b){
        return kachakacha::v2::geometry::Dot(a->center,forward)>kachakacha::v2::geometry::Dot(b->center,forward);});
    auto* gl=QOpenGLContext::currentContext()->functions();
    shader_.bind();buffer_.bind();shader_.setUniformValue("matrix",matrix);shader_.setUniformValue("image",0);
    shader_.enableAttributeArray("position");shader_.setAttributeBuffer("position",GL_FLOAT,0,3,sizeof(Vertex));
    shader_.enableAttributeArray("pixel");shader_.setAttributeBuffer("pixel",GL_FLOAT,3*sizeof(float),2,sizeof(Vertex));
    gl->glEnable(GL_BLEND);gl->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    gl->glDepthMask(GL_TRUE);gl->glDepthFunc(GL_LEQUAL);gl->glActiveTexture(GL_TEXTURE0);
    for(const auto* layer:order) {
        shader_.setUniformValue("opacity",layer->opacity);gl->glBindTexture(GL_TEXTURE_2D,layer->texture);
        gl->glDrawArrays(GL_TRIANGLES,layer->first,3);
    }
    gl->glBindTexture(GL_TEXTURE_2D,0);gl->glDisable(GL_BLEND);gl->glDepthFunc(GL_LESS);
    shader_.disableAttributeArray("position");shader_.disableAttributeArray("pixel");buffer_.release();shader_.release();
}
#endif
