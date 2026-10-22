#include "V2NativeCanvas.h"
#include <QWidget>
#include <QPaintDevice>
#include <QVariant>
#ifdef KACHACAD_NATIVE_VIEWPORT
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QGuiApplication>
#include <QTimer>
#include <QSurfaceFormat>
#include <QShowEvent>
#include <QElapsedTimer>
#include <utility>
namespace {
class Canvas final : public QOpenGLWidget {
public:
    Canvas(QWidget* parent, std::function<bool(QPaintDevice&, double)> paint)
        : QOpenGLWidget(parent), paint_(std::move(paint)) {
        setObjectName("nativeViewportCanvas");
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QSurfaceFormat format;format.setVersion(2,1);format.setDepthBufferSize(24);
        setFormat(format);
        setGeometry(parent->rect());lower();
    }
    void Finish() {
        auto* previous=QOpenGLContext::currentContext();
        auto* surface=previous?previous->surface():nullptr;
        makeCurrent();
        if(context()&&QOpenGLContext::currentContext()==context())context()->functions()->glFinish();
        doneCurrent();
        if(previous&&surface)previous->makeCurrent(surface);
    }
protected:
    void showEvent(QShowEvent* event) override {
        QOpenGLWidget::showEvent(event);
        QTimer::singleShot(100,this,[this]{
            if(isVisible()&&!isValid()){setProperty("nativeReady",false);hide();parentWidget()->update();}
        });
    }
    void initializeGL() override {}
    void paintGL() override {
        const bool measure=qEnvironmentVariableIsSet("KACHACAD_VIEWPORT_PERF_FRAMES");
        QElapsedTimer timer;if(measure)timer.start();
        if(!paint_(*this,devicePixelRatioF())) {
            setProperty("nativeReady",false);
            QTimer::singleShot(0,this,[this]{hide();parentWidget()->update();});
            return;
        }
        setProperty("nativeReady",true);
        auto* parent=parentWidget();
        if(measure){context()->functions()->glFinish();parent->setProperty("presentationWorkMs",timer.nsecsElapsed()/1e6);}
        parent->setProperty("nativeViewportFrames",parent->property("nativeViewportFrames").toULongLong()+1);
    }
private:
    std::function<bool(QPaintDevice&, double)> paint_;
};
}
#endif
QWidget* V2CreateNativeCanvas(QWidget* parent,std::function<bool(QPaintDevice&, double)> paint) {
#ifdef KACHACAD_NATIVE_VIEWPORT
    if(QGuiApplication::platformName()=="windows"&&!qEnvironmentVariableIsSet("KACHACAD_SOFTWARE_RENDER")
        &&!qEnvironmentVariableIsSet("KACHACAD_LEGACY_VIEWPORT"))return new Canvas(parent,std::move(paint));
#else
    (void)parent;(void)paint;
#endif
    return nullptr;
}
bool V2NativeCanvasActive(const QWidget* canvas) {
    return canvas&&canvas->isVisible()&&canvas->property("nativeReady").toBool();
}
void V2CompleteNativeCanvas(QWidget* canvas) {
#ifdef KACHACAD_NATIVE_VIEWPORT
    if(V2NativeCanvasActive(canvas))static_cast<Canvas*>(canvas)->Finish();
#else
    (void)canvas;
#endif
}
