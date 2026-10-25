#include "V2GpuRenderer.h"
#include <QImage>
#include <QPainter>
#include <QPaintDevice>
#ifdef KACHACAD_GPU_VIEW
#include "V2GpuImages.h"
#include <QByteArray>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLShader>
#include <QSize>
#include <QSurfaceFormat>
#include <QVector3D>
#include <QOffscreenSurface>
#include <QOpenGLPaintDevice>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QGuiApplication>
#include <QDebug>
#include <cstdio>
#include <QElapsedTimer>
#include <limits>
#include <algorithm>
#include <cmath>

namespace {
struct Vertex { float p[3], n[3], face[3]; };
struct Batch { int triangles, triangleCount, edges, edgeCount; bool closed; };
const char* vertexShader = R"(
attribute vec3 position;
attribute vec3 normal;
attribute vec3 faceNormal;
uniform mat4 matrix;
varying vec3 n;
varying vec3 face;
void main() { gl_Position=matrix*vec4(position,1.0); n=normal; face=faceNormal; }
)";
const char* fragmentShader = R"(
uniform vec3 color;
uniform vec3 forward;
uniform bool closed;
uniform bool edges;
varying vec3 n;
varying vec3 face;
void main() {
    if (!edges && (dot(face,face)<1e-20 || (closed && dot(face,forward)>0.0))) discard;
    float shade=1.0;
    if (!edges) shade=0.45+0.55*(0.25+0.75*abs(dot(normalize(n),normalize(vec3(0.4,0.35,-0.85)))));
    gl_FragColor=vec4(color*shade,1.0);
}
)";
QVector3D Color(unsigned rgb) { return QVector3D(float((rgb>>16)&255)/255,
    float((rgb>>8)&255)/255,float(rgb&255)/255); }
Vertex MakeVertex(const kachakacha::v2::geometry::Vector3& p,
    const kachakacha::v2::geometry::Vector3& n, const kachakacha::v2::geometry::Vector3& f) {
    return {{float(p.x),float(p.y),float(p.z)}, {float(n.x),float(n.y),float(n.z)},
        {float(f.x),float(f.y),float(f.z)}};
}
}
struct V2GpuRenderer::State {
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLShaderProgram shader;
    QOpenGLBuffer buffer;
    V2GpuImages images;
    std::unique_ptr<QOpenGLFramebufferObject> frame;
    std::vector<Batch> batches;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    bool attempted = false, ready = false, paintingFrame = false, directFrame = false;
    double pixelRatio=1;
    std::uint64_t readbacks=0;
    kachakacha::v2::geometry::Vector3 origin{};
    double extent=1;
    QByteArray renderer;
    QImage cachedImage;
    std::array<double,16> cachedMatrix{};
    std::vector<std::uint64_t> cachedStyle;
    std::vector<const V2ImageView*> cachedImages;
    std::uint64_t cachedImageRevision=0;
    bool renderSucceeded=false;
    std::uint64_t cachedRevision=std::numeric_limits<std::uint64_t>::max();
    ~State() {
        if (ready && context.makeCurrent(&surface)) {
            images.Release();frame.reset(); buffer.destroy(); shader.removeAllShaders(); context.doneCurrent();
        }
    }
    bool Initialize() {
        if (attempted) return ready;
        attempted=true;
        if (qEnvironmentVariableIsSet("KACHACAD_SOFTWARE_RENDER")
            || QGuiApplication::platformName()=="offscreen") return false;
        QSurfaceFormat format; format.setVersion(2,1); format.setDepthBufferSize(24);
        context.setFormat(format);
        context.setShareContext(QOpenGLContext::globalShareContext());
        if (!context.create()) return false;
        surface.setFormat(context.format()); surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
        auto* gl=context.functions(); gl->initializeOpenGLFunctions();
        renderer=reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));
        ready=shader.addShaderFromSourceCode(QOpenGLShader::Vertex,vertexShader)
            && shader.addShaderFromSourceCode(QOpenGLShader::Fragment,fragmentShader)
            && shader.link() && buffer.create();
        if (!ready) qWarning()<<"GPU renderer unavailable:"<<shader.log();
        context.doneCurrent();
        return ready;
    }
    void Upload(const std::vector<Item>& items, std::uint64_t version,
        const kachakacha::v2::geometry::Vector3& imageOrigin) {
        auto nextOrigin=imageOrigin;
        for(const auto& item:items)if(!item.mesh->triangles.empty()){nextOrigin=item.mesh->triangles.front().points[0];break;}
        if (revision==version&&origin==nextOrigin) return;
        std::vector<Vertex> vertices; batches.clear();
        origin=nextOrigin;
        for (const auto& item : items) {
            Batch batch{int(vertices.size()),0,0,0,item.mesh->closed};
            for (const auto& t : item.mesh->triangles) for (int i=0;i<3;++i) {
                const auto n=t.vertexNormals[i]==kachakacha::v2::geometry::Vector3{} ? t.normal : t.vertexNormals[i];
                vertices.push_back(MakeVertex(t.points[i]-origin,n,t.normal));
            }
            batch.triangleCount=int(vertices.size())-batch.triangles;
            batches.push_back(batch);
        }
        for(std::size_t at=0;at<items.size();++at) {
            auto& batch=batches[at];const auto& item=items[at];
            batch.edges=int(vertices.size());
            for (const auto& line:item.mesh->edges) for(std::size_t i=1;i<line.size();++i) {
                vertices.push_back(MakeVertex(line[i-1]-origin,{},{}));
                vertices.push_back(MakeVertex(line[i]-origin,{},{}));
            }
            batch.edgeCount=int(vertices.size())-batch.edges;
        }
        extent=1;
        for(const auto& v:vertices)extent=std::max(extent,std::sqrt(double(v.p[0])*v.p[0]
            +double(v.p[1])*v.p[1]+double(v.p[2])*v.p[2]));
        buffer.bind(); buffer.allocate(vertices.data(),int(vertices.size()*sizeof(Vertex)));
        revision=version;
    }
    void Draw(const std::vector<Item>& items, bool edges) {
        auto* gl=QOpenGLContext::currentContext()->functions(); shader.setUniformValue("edges",edges);
        if(edges) { gl->glDepthMask(GL_FALSE); gl->glDepthFunc(GL_LEQUAL); }
        else { gl->glEnable(GL_POLYGON_OFFSET_FILL); gl->glPolygonOffset(1,1); }
        for(std::size_t i=0;i<items.size();) {
            if(!items[i].visible){++i;continue;}
            const auto& b=batches[i];
            const auto color=edges?items[i].edge:items[i].fill;
            int count=edges?b.edgeCount:b.triangleCount;
            std::size_t end=i+1;
            for(;end<items.size();++end) {
                if(!items[end].visible || (edges?items[end].edge:items[end].fill)!=color
                    || (!edges && batches[end].closed!=b.closed)
                    || (edges && items[end].emphasized!=items[i].emphasized))break;
                count+=edges?batches[end].edgeCount:batches[end].triangleCount;
            }
            shader.setUniformValue("closed",b.closed);
            shader.setUniformValue("color",Color(color));
            if(edges)gl->glLineWidth(float((items[i].emphasized?3.:1.)*pixelRatio));
            gl->glDrawArrays(edges?GL_LINES:GL_TRIANGLES,edges?b.edges:b.triangles,count);
            i=end;
        }
        gl->glDisable(GL_POLYGON_OFFSET_FILL); gl->glDepthMask(GL_TRUE); gl->glDepthFunc(GL_LESS);
    }
};
V2GpuRenderer::V2GpuRenderer():state_(std::make_unique<State>()){}
V2GpuRenderer::~V2GpuRenderer()=default;
const char* V2GpuRenderer::Backend() const { return state_->ready?state_->renderer.constData():"CPU"; }
QImage V2GpuRenderer::Render(const std::vector<Item>& items, std::uint64_t revision,
    const kachakacha::v2::geometry::ScreenMapping& mapping,
    const kachakacha::v2::geometry::Vector3& forward, int width, int height,
    const std::vector<const V2ImageView*>& images,std::uint64_t imageRevision) {
    auto& s=*state_;
    s.renderSucceeded=false;
    if(width<=0||height<=0||!s.Initialize())return {};
    std::vector<std::uint64_t> style;
    for(const auto& item:items)style.push_back(std::uint64_t(item.fill)|(std::uint64_t(item.edge)<<24)
        |(std::uint64_t(item.visible)<<48)|(std::uint64_t(item.emphasized)<<49));
    if(!s.paintingFrame && s.cachedRevision==revision && s.cachedMatrix==mapping.matrix && s.cachedStyle==style
        && s.cachedImageRevision==imageRevision&&s.cachedImages==images
        && s.cachedImage.size()==QSize(width,height)){s.renderSucceeded=true;return s.cachedImage;}
    if(!s.directFrame&&!s.context.makeCurrent(&s.surface))return {};
    if(!s.directFrame&&(!s.frame||s.frame->size()!=QSize(width,height))) {
        QOpenGLFramebufferObjectFormat format; format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        s.frame=std::make_unique<QOpenGLFramebufferObject>(width,height,format);
    }
    if(!s.directFrame&&!s.frame->isValid()){s.context.doneCurrent();return {};}
    QElapsedTimer gpuTimer; gpuTimer.start();
    if(!s.directFrame)s.frame->bind();
    s.Upload(items,revision,images.empty()?kachakacha::v2::geometry::Vector3{}:images.front()->triangles.front().mesh.points[0]);
    if(!s.images.Upload(images,imageRevision,s.origin)) {
        // A frame owned by QPainter must remain current for the CPU-image fallback.
        if(!s.paintingFrame){s.frame->release();s.context.doneCurrent();}return {};
    }
    s.buffer.bind(); s.shader.bind();
    auto* gl=QOpenGLContext::currentContext()->functions();
    gl->glViewport(0,0,int(std::round(width*s.pixelRatio)),int(std::round(height*s.pixelRatio)));
    gl->glDepthMask(GL_TRUE);gl->glClearColor(0,0,0,0);gl->glClear(s.paintingFrame?GL_DEPTH_BUFFER_BIT:GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl->glEnable(GL_DEPTH_TEST);gl->glDisable(GL_BLEND);gl->glDisable(GL_CULL_FACE);
    QMatrix4x4 matrix;
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)matrix(r,c)=float(mapping.matrix[r*4+c]);
    for(int r=0;r<4;++r)matrix(r,3)=float(mapping.matrix[r*4+3]+mapping.matrix[r*4]*s.origin.x
        +mapping.matrix[r*4+1]*s.origin.y+mapping.matrix[r*4+2]*s.origin.z);
    if(mapping.matrix[12]==0 && mapping.matrix[13]==0 && mapping.matrix[14]==0) {
        // CPU orthographic display does not clip depth when zooming. Fit the GPU
        // depth range to the cached geometry, independently of the zoom width.
        const double depth=1+std::abs(double(matrix(2,3)))+std::max(s.extent,s.images.Extent())*std::sqrt(
            mapping.matrix[8]*mapping.matrix[8]+mapping.matrix[9]*mapping.matrix[9]+mapping.matrix[10]*mapping.matrix[10]);
        for(int c=0;c<4;++c)matrix(2,c)=float(matrix(2,c)/depth);
    }
    s.shader.setUniformValue("matrix",matrix);
    s.shader.setUniformValue("forward",QVector3D(float(forward.x),float(forward.y),float(forward.z)));
    const char* names[]={"position","normal","faceNormal"};
    for(int i=0;i<3;++i){s.shader.enableAttributeArray(names[i]);
        s.shader.setAttributeBuffer(names[i],GL_FLOAT,i*3*sizeof(float),3,sizeof(Vertex));}
    s.Draw(items,false);
    s.images.Draw(matrix,forward);
    s.buffer.bind();s.shader.bind();
    for(int i=0;i<3;++i){s.shader.enableAttributeArray(names[i]);
        s.shader.setAttributeBuffer(names[i],GL_FLOAT,i*3*sizeof(float),3,sizeof(Vertex));}
    s.Draw(items,true);
    gl->glDisable(GL_DEPTH_TEST);gl->glLineWidth(1.f);
    for(const auto* name:names)s.shader.disableAttributeArray(name);
    s.shader.release();s.buffer.release();
    s.renderSucceeded=true;
    if(s.paintingFrame)return {};
    const auto submitTime=gpuTimer.nsecsElapsed();
    QImage image=s.frame->toImage();++s.readbacks;s.frame->release();s.context.doneCurrent();
    if(qEnvironmentVariableIsSet("KACHACAD_TRACE_RENDER"))std::fprintf(stderr,"gpu stages ms %.3f %.3f\n",submitTime/1e6,(gpuTimer.nsecsElapsed()-submitTime)/1e6);
    s.cachedImage=image; s.cachedMatrix=mapping.matrix; s.cachedStyle=std::move(style);s.cachedRevision=revision;
    s.cachedImageRevision=imageRevision;s.cachedImages=images;
    return image;
}
bool V2GpuRenderer::PaintingFrame() const {return state_->paintingFrame;}
bool V2GpuRenderer::PaintFrame(QPainter& destination,int width,int height,
    const std::function<void(QPainter&)>& draw) {
    auto& s=*state_;
    if(width<=0||height<=0||!s.Initialize()||!s.context.makeCurrent(&s.surface))return false;
    if(!s.frame||s.frame->size()!=QSize(width,height)) {
        QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        s.frame=std::make_unique<QOpenGLFramebufferObject>(width,height,format);
    }
    if(!s.frame->isValid()){s.context.doneCurrent();return false;}
    s.frame->bind();s.paintingFrame=true;
    QElapsedTimer timer;timer.start();
    QOpenGLPaintDevice device(width,height);
    {QPainter painter(&device);draw(painter);}
    s.paintingFrame=false;
    const auto painted=timer.nsecsElapsed();
    const QImage image=s.frame->toImage();++s.readbacks;
    const auto read=timer.nsecsElapsed();
    s.cachedImage={};s.frame->release();s.context.doneCurrent();
    if(image.isNull())return false;
    destination.drawImage(0,0,image);
    if(qEnvironmentVariableIsSet("KACHACAD_TRACE_RENDER"))std::fprintf(stderr,"gpu frame ms draw %.3f readback %.3f copy %.3f\n",painted/1e6,(read-painted)/1e6,(timer.nsecsElapsed()-read)/1e6);
    return true;
}

bool V2GpuRenderer::PaintDirect(QPaintDevice& target,int width,int height,double ratio,
    const std::function<void(QPainter&)>& draw) {
    (void)target;
    auto* current=QOpenGLContext::currentContext();
    if(!current||width<=0||height<=0)return false;
    auto* surface=current->surface();auto* gl=current->functions();
    GLint framebuffer=0;gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    auto& s=*state_;const bool ready=s.Initialize();
    // Restore the widget's context and its nonzero default FBO after creating
    // shared resources in the owned context, before starting QPainter.
    if(!current->makeCurrent(surface))return false;
    gl->glBindFramebuffer(GL_FRAMEBUFFER,GLuint(framebuffer));
    if(!ready||!QOpenGLContext::areSharing(current,&s.context))return false;
    s.paintingFrame=true;s.directFrame=true;s.pixelRatio=ratio;
    // QWidget::render redirects a widget's paint engine to its capture image.
    // An explicit GL device keeps every layer on the current widget FBO even
    // during grabFramebuffer; Qt then copies the complete, fresh frame once.
    QOpenGLPaintDevice device(int(std::round(width*ratio)),int(std::round(height*ratio)));
    device.setDevicePixelRatio(ratio);
    {QPainter painter(&device);draw(painter);}
    s.paintingFrame=false;s.directFrame=false;s.pixelRatio=1;
    return true;
}
std::uint64_t V2GpuRenderer::Readbacks() const {return state_->readbacks;}
bool V2GpuRenderer::RenderSucceeded() const {return state_->renderSucceeded;}
std::uint64_t V2GpuRenderer::ImageUploads() const {return state_->images.Uploads();}
#else
struct V2GpuRenderer::State {};
V2GpuRenderer::V2GpuRenderer()=default;
V2GpuRenderer::~V2GpuRenderer()=default;
const char* V2GpuRenderer::Backend() const { return "CPU"; }
bool V2GpuRenderer::PaintingFrame() const {return false;}
bool V2GpuRenderer::PaintDirect(QPaintDevice&,int,int,double,const std::function<void(QPainter&)>&){return false;}
std::uint64_t V2GpuRenderer::Readbacks() const {return 0;}
bool V2GpuRenderer::RenderSucceeded() const {return false;}
std::uint64_t V2GpuRenderer::ImageUploads() const {return 0;}
bool V2GpuRenderer::PaintFrame(QPainter&,int,int,const std::function<void(QPainter&)>&){return false;}
QImage V2GpuRenderer::Render(const std::vector<Item>&,std::uint64_t,
    const kachakacha::v2::geometry::ScreenMapping&,const kachakacha::v2::geometry::Vector3&,int,int,
    const std::vector<const V2ImageView*>&,std::uint64_t){return {};}
#endif
