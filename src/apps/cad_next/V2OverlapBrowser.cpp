#include <QColor>
#include <QEvent>
#include <QObject>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>
#include "V2OverlapBrowser.h"
#include "kachakacha/kernel/OcctContact.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
using namespace kachakacha::v2;
struct OverlapRegion { int first=0, second=0, ordinal=0; double volume=0; modeling::ShapeMesh mesh; };

class OverlapCanvas final : public QWidget {
public:
    explicit OverlapCanvas(QWidget* parent) : QWidget(parent) { setMinimumSize(420,360); setObjectName("overlapCanvas"); }
    std::vector<OverlapSource> sources;
    std::vector<OverlapRegion> regions;
    int selected=-1, view=0;
    double zoom=1;
    bool includeHidden=true;
    std::function<void(int)> choose;
    double yaw=-0.7853981634, pitch=0.6154797087;
    QPointF lastMouse;
    geometry::ScreenMapping mapping_;
    geometry::ScreenMapping Mapping() const {
        geometry::Vector3 low{}, high{}; bool first=true;
        for(const auto& source:sources) {
            if(!includeHidden && source.hidden) continue;
            if(first){low=source.mesh.minimum;high=source.mesh.maximum;first=false;}
            else {
                low={std::min(low.x,source.mesh.minimum.x),std::min(low.y,source.mesh.minimum.y),std::min(low.z,source.mesh.minimum.z)};
                high={std::max(high.x,source.mesh.maximum.x),std::max(high.y,source.mesh.maximum.y),std::max(high.z,source.mesh.maximum.z)};
            }
        }
        const geometry::Vector3 forward{-std::cos(pitch)*std::cos(yaw),-std::cos(pitch)*std::sin(yaw),-std::sin(pitch)};
        const geometry::Vector3 up{-std::sin(pitch)*std::cos(yaw),-std::sin(pitch)*std::sin(yaw),std::cos(pitch)};
        const double span=std::max(1.0,(high-low).Length())*1.15*std::max(1.0,static_cast<double>(width())/height())/zoom;
        return geometry::MakeOrthographicMapping((low+high)*0.5,forward,up,span,width(),height());
    }
    QPointF Map(const geometry::Vector3& p) const {
        const auto point=mapping_.Project(p);
        return point.has_value() ? QPointF(point->x,point->y) : QPointF();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        mapping_=Mapping();
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.fillRect(rect(),QColor(30,35,42));
        painter.setPen(QPen(QColor(135,150,160),1));
        for (const auto& s:sources) {
            if (!includeHidden && s.hidden) continue;
            for (const auto& edge:s.mesh.edges) for (std::size_t i=1;i<edge.size();++i) painter.drawLine(Map(edge[i-1]),Map(edge[i]));
        }
        for (std::size_t i=0;i<regions.size();++i) {
            const auto& r=regions[i]; const bool active=static_cast<int>(i)==selected;
            painter.setPen(Qt::NoPen); painter.setBrush(active ? QColor(255,195,30,125):QColor(255,65,65,65));
            for (const auto& t:r.mesh.triangles) {
                QPolygonF poly; for (const auto& p:t.points) poly<<Map(p); painter.drawPolygon(poly);
            }
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(active?QColor(255,220,50):QColor(255,85,75),active?3:1));
            for (const auto& edge:r.mesh.edges) for(std::size_t k=1;k<edge.size();++k) painter.drawLine(Map(edge[k-1]),Map(edge[k]));
            painter.drawText(Map((r.mesh.minimum+r.mesh.maximum)*0.5),QString::number(i+1));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        mapping_=Mapping();lastMouse=event->position();
        if(event->button()!=Qt::LeftButton) return;
        std::vector<int> hits;
        for(std::size_t i=0;i<regions.size();++i) for(const auto& t:regions[i].mesh.triangles) {
            QPolygonF poly; for(const auto& p:t.points) poly<<Map(p);
            if(poly.containsPoint(event->position(),Qt::OddEvenFill)) {hits.push_back(static_cast<int>(i));break;}
        }
        if(hits.empty()) return;
        auto found=std::find(hits.begin(),hits.end(),selected);
        const int next=found!=hits.end() && ++found!=hits.end() ? *found:hits.front();
        if(choose) choose(next);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!(event->buttons() & (Qt::RightButton | Qt::MiddleButton)))return;
        const auto delta=event->position()-lastMouse;lastMouse=event->position();
        yaw-=delta.x()*0.01;pitch=std::clamp(pitch+delta.y()*0.01,-1.55,1.55);update();
    }
    void wheelEvent(QWheelEvent* event) override { zoom=std::clamp(zoom*std::pow(1.15,event->angleDelta().y()/120.0),0.2,30.0);update(); }
};

class OverlapBrowser final : public QDialog {
public:
    OverlapBrowser(QWidget* parent,std::vector<OverlapSource> sources,double tolerance,QString missing,OverlapAction action)
        : QDialog(parent),tolerance_(tolerance),missing_(std::move(missing)),action_(std::move(action)) {
        setObjectName("overlapBrowser"); setWindowTitle(QStringLiteral("KCDのめり込み一覧 — 3Dビュー"));
        setAttribute(Qt::WA_DeleteOnClose); resize(1050,660);
        auto* root=new QVBoxLayout(this); auto* top=new QHBoxLayout();
        auto* view=new QComboBox(this); view->setObjectName("overlapView"); view->addItems({QStringLiteral("斜め"),QStringLiteral("正面 XZ"),QStringLiteral("上面 XY"),QStringLiteral("側面 YZ")});
        hidden_=new QCheckBox(QStringLiteral("非表示部品も検査（履歴の元形状を含む）"),this);hidden_->setChecked(true);
        auto* fit=new QPushButton(QStringLiteral("全体表示"),this);
        top->addWidget(view);top->addWidget(hidden_);top->addWidget(fit);root->addLayout(top);
        status_=new QLabel(this);status_->setWordWrap(true);root->addWidget(status_);
        auto* main=new QHBoxLayout();canvas_=new OverlapCanvas(this);canvas_->sources=std::move(sources);
        list_=new QListWidget(this);list_->setObjectName("overlapList");list_->setMinimumWidth(330);
        main->addWidget(canvas_,1);main->addWidget(list_);root->addLayout(main,1);
        root->addWidget(new QLabel(QStringLiteral("赤=めり込み、黄=選択領域。クリックで選択。同じ位置を再クリックすると奥の候補へ。右／中ドラッグで回転、ホイールで拡大。"),this));
        BuildActions(root);
        QObject::connect(view,&QComboBox::currentIndexChanged,this,[this](int i){canvas_->yaw=i==1 ? -1.5707963268 : i==3 ? 0 : -0.7853981634;canvas_->pitch=i==2 ? 1.5707963268 : i==0 ? 0.6154797087 : 0;canvas_->update();});
        QObject::connect(fit,&QPushButton::clicked,this,[this]{canvas_->zoom=1;canvas_->update();});
        QObject::connect(hidden_,&QCheckBox::toggled,this,[this]{Restart();});
        QObject::connect(list_,&QListWidget::currentRowChanged,this,[this](int row){canvas_->selected=row;canvas_->update();EnableActions();});
        canvas_->choose=[this](int row){list_->setCurrentRow(row);};
        Restart();
    }
private:
    void BuildActions(QVBoxLayout* root) {
        auto* row=new QHBoxLayout();
        const std::pair<QString,app::BooleanKind> choices[]{
            {QStringLiteral("この組の境界ワイヤーを作る"),app::BooleanKind::ContactWire},
            {QStringLiteral("削る側を指定する"),app::BooleanKind::TrimOverlap},
            {QStringLiteral("分割して残す領域を選ぶ"),app::BooleanKind::SplitOverlap}};
        for(const auto& entry:choices) {
            auto* button=new QPushButton(entry.first,this);actions_.push_back(button);row->addWidget(button);
            QObject::connect(button,&QPushButton::clicked,this,[this,kind=entry.second]{
                const int i=list_->currentRow();if(!done_ || i<0 || i>=static_cast<int>(canvas_->regions.size()))return;
                const auto region=canvas_->regions[i];
                action_(canvas_->sources[region.first],canvas_->sources[region.second],region.ordinal,kind);close();
            });
        }
        auto* closeButton=new QPushButton(QStringLiteral("閉じる／検査を中止"),this);row->addWidget(closeButton);
        QObject::connect(closeButton,&QPushButton::clicked,this,[this]{close();});root->addLayout(row);
    }
    void EnableActions() { for(auto* b:actions_) b->setEnabled(done_ && list_->currentRow()>=0); }
    void Restart() {
        ++generation_;i_=0;j_=1;failures_=0;done_=false;canvas_->regions.clear();list_->clear();
        canvas_->includeHidden=hidden_->isChecked();canvas_->selected=-1;canvas_->update();EnableActions();
        Next(generation_);
    }
    void Next(int generation) {
        QTimer::singleShot(0,this,[this,generation]{
            if(generation!=generation_)return;
            const int n=static_cast<int>(canvas_->sources.size());
            if(i_>=n-1){done_=true;status_->setText(QStringLiteral("検査完了: %1 領域 / 計算失敗 %2 組。%3")
                .arg(canvas_->regions.size()).arg(failures_).arg(missing_));EnableActions();return;}
            const auto& a=canvas_->sources[i_];const auto& b=canvas_->sources[j_];
            status_->setText(QStringLiteral("全体を検査中: %1 / %2 …（途中結果）").arg(a.name,b.name));
            if((canvas_->includeHidden || (!a.hidden&&!b.hidden)) && BoundsOverlap(a.mesh,b.mesh)) ScanPair(a,b);
            if(++j_>=n){++i_;j_=i_+1;} Next(generation);
        });
    }
    bool BoundsOverlap(const modeling::ShapeMesh& a,const modeling::ShapeMesh& b) const {
        return a.maximum.x>=b.minimum.x && b.maximum.x>=a.minimum.x && a.maximum.y>=b.minimum.y
            && b.maximum.y>=a.minimum.y && a.maximum.z>=b.minimum.z && b.maximum.z>=a.minimum.z;
    }
    void ScanPair(const OverlapSource& a,const OverlapSource& b) {
        const auto built=kernel::BuildContact(a.handle,b.handle,false,false,true,tolerance_);
        if(!built.HasValue()) {
            if(built.FirstSummaryJa().find("重なり体積がありません") == std::string::npos) {++failures_;missing_+=QStringLiteral("\n%1 / %2: %3").arg(a.name,b.name,QString::fromStdString(built.FirstSummaryJa()));}
            return;
        }
        for(const auto& piece:built.Value().pieces) {
            const auto mesh=kernel::BuildShapeMesh(piece.handle);if(!mesh.HasValue()){++failures_;continue;}
            canvas_->regions.push_back({i_,j_,piece.fragmentIndex,piece.volumeMm3,mesh.Value()});
            list_->addItem(QStringLiteral("%1: A %2 / B %3\n重なり %4 — %5 mm³").arg(canvas_->regions.size()).arg(a.name,b.name)
                .arg(piece.fragmentIndex+1).arg(piece.volumeMm3,0,'f',3));
        }
        canvas_->update();
    }
    double tolerance_;QString missing_;OverlapAction action_;
    OverlapCanvas* canvas_=nullptr;QListWidget* list_=nullptr;QLabel* status_=nullptr;QCheckBox* hidden_=nullptr;
    std::vector<QPushButton*> actions_;int i_=0,j_=1,generation_=0,failures_=0;bool done_=false;
};

class ContactPicker final : public QObject {
public:
    ContactPicker(QWidget& parent,std::function<bool(const QPointF&)> pick):QObject(&parent),pick_(std::move(pick)){parent.installEventFilter(this);}
    bool eventFilter(QObject*,QEvent* event) override {
        if(event->type()!=QEvent::MouseButtonPress)return false;
        auto* mouse=static_cast<QMouseEvent*>(event);
        return mouse->button()==Qt::LeftButton && mouse->modifiers()==Qt::NoModifier && pick_(mouse->position());
    }
private: std::function<bool(const QPointF&)> pick_;
};
}
void OpenOverlapBrowser(QWidget* parent,std::vector<OverlapSource> sources,double tolerance,QString missing,OverlapAction action)
{
    auto* dialog=new OverlapBrowser(parent,std::move(sources),tolerance,std::move(missing),std::move(action));dialog->show();
}
void InstallContactPicker(QWidget& viewport,std::function<bool(const QPointF&)> pick) { new ContactPicker(viewport,std::move(pick)); }
