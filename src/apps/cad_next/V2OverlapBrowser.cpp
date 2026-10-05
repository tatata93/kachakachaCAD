#include "V2OverlapBrowser.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/kernel/OcctContact.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <limits>

namespace {
using namespace kachakacha::v2;
struct OverlapRegion {
    int first=0, second=0, ordinal=0;
    double volume=0;
    modeling::ShapeMesh mesh;
    int removal=0;
};
// Child of the normal viewport: it shares its camera and does not intercept navigation.
class OverlapOverlay final : public QWidget {
public:
    explicit OverlapOverlay(V2Viewport& viewport):QWidget(&viewport),viewport_(viewport) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setObjectName("overlapOverlay");
        setGeometry(viewport.rect());show();raise();
    }
    std::vector<OverlapRegion> regions;
    std::vector<modeling::ShapeMesh> result;
    int selected=-1;
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto mapping=viewport_.Mapping();
        for(std::size_t i=0;i<regions.size();++i) {
            const bool chosen=static_cast<int>(i)==selected;
            painter.setPen(QPen(chosen ? QColor(255,215,65) : QColor(255,100,90),chosen ? 2:1));
            painter.setBrush(chosen ? QColor(255,180,35,110) : QColor(240,60,50,70));
            for(const auto& triangle:regions[i].mesh.triangles) {
                QPolygonF polygon;
                for(const auto& point:triangle.points) {
                    const auto s=mapping.Project(point);if(s)polygon<<QPointF(s->x,s->y);
                }
                painter.drawPolygon(polygon);
            }
        }
        painter.setBrush(Qt::NoBrush);
        for(const auto& region:regions) for(const auto& edge:region.mesh.edges) {
            painter.setPen(QPen(QColor(255,165,55),2));QPolygonF line;
            for(const auto& p:edge){const auto s=mapping.Project(p);if(s)line<<QPointF(s->x,s->y);}
            painter.drawPolyline(line);
        }
        painter.setPen(QPen(QColor(90,255,165),2));
        for(const auto& mesh:result) for(const auto& edge:mesh.edges) {
            QPolygonF line;
            for(const auto& p:edge){const auto s=mapping.Project(p);if(s)line<<QPointF(s->x,s->y);}
            painter.drawPolyline(line);
        }
    }
private: V2Viewport& viewport_;
};

class OverlapBrowser final : public QWidget {
public:
    OverlapBrowser(V2Viewport& viewport,V2OperationPanelHost& host,std::vector<OverlapSource> sources,
        double tolerance,QString missing,OverlapAction action)
        :QWidget(&host),viewport_(viewport),host_(host),sources_(std::move(sources)),
         tolerance_(tolerance),missing_(std::move(missing)),action_(std::move(action)) {
        setObjectName("overlapBrowser");
        overlay_=new OverlapOverlay(viewport_);
        auto* root=new QVBoxLayout(this);
        hidden_=new QCheckBox(QStringLiteral("非表示の履歴部品も検査する"),this);root->addWidget(hidden_);
        status_=new QLabel(this);status_->setWordWrap(true);root->addWidget(status_);
        list_=new QListWidget(this);list_->setObjectName("overlapList");root->addWidget(list_);
        auto* pick=new QPushButton(QStringLiteral("1 めり込み部分を選び直す"),this);root->addWidget(pick);
        mode_=new QComboBox(this);mode_->setObjectName("overlapDisposition");
        mode_->addItems({QStringLiteral("選んだ側を削る"),QStringLiteral("選んだ側を残す")});root->addWidget(mode_);
        for(int i=0;i<2;++i) {
            side_[i]=new QPushButton(this);side_[i]->setObjectName(i==0 ? "overlapSideA":"overlapSideB");
            side_[i]->setCheckable(true);root->addWidget(side_[i]);
            QObject::connect(side_[i],&QPushButton::clicked,this,[this,i]{SelectSide(i);});
        }
        auto* legend=new QLabel(QStringLiteral("黄: 選択領域 / 赤: 他のめり込み\n緑の輪郭: 加工後に残る形\n側は3D上の部品、または上の部品名で選べます。"),this);
        legend->setWordWrap(true);root->addWidget(legend);
        confirm_=new QPushButton(QStringLiteral("確定 Enter"),this);confirm_->setObjectName("overlapConfirm");root->addWidget(confirm_);
        wire_=new QPushButton(QStringLiteral("この組の交差ワイヤーだけ作る"),this);wire_->setObjectName("overlapWire");root->addWidget(wire_);
        auto* cancel=new QPushButton(QStringLiteral("取消 Esc"),this);root->addWidget(cancel);
        QObject::connect(cancel,&QPushButton::clicked,this,[this]{Restart();});
        QObject::connect(confirm_,&QPushButton::clicked,this,[this]{Apply(false);});
        QObject::connect(wire_,&QPushButton::clicked,this,[this]{Apply(true);});
        QObject::connect(hidden_,&QCheckBox::toggled,this,[this]{Restart();});
        QObject::connect(list_,&QListWidget::currentRowChanged,this,[this](int row){SelectRegion(row);});
        QObject::connect(pick,&QPushButton::clicked,this,[this]{choosingSide_=false;status_->setText(QStringLiteral("1 3Dビューのめり込み部分をクリックしてください。"));});
        QObject::connect(mode_,&QComboBox::currentIndexChanged,this,[this]{if(selectedSide_>=0)SelectSide(selectedSide_);});
        qApp->installEventFilter(this);
        Restart();
    }
    ~OverlapBrowser() override { Stop(); }
private:
    void Stop() {
        ++generation_;qApp->removeEventFilter(this);
        if(overlay_){delete overlay_.data();overlay_=nullptr;}
    }
    void closeEvent(QCloseEvent* event) override {Stop();QWidget::closeEvent(event);}
    bool eventFilter(QObject* object,QEvent* event) override {
        if(!isVisible() || !overlay_)return false;
        if(object==&viewport_) {
            if(event->type()==QEvent::Resize)overlay_->setGeometry(viewport_.rect());
            if(event->type()==QEvent::Paint)overlay_->update();
            if(event->type()==QEvent::MouseButtonPress) {
                auto* mouse=static_cast<QMouseEvent*>(event);
                if(mouse->button()==Qt::LeftButton && mouse->modifiers()==Qt::NoModifier) {
                    viewport_.setFocus();
                    if(viewport_.PressViewNavigator(mouse->position(),view::AxisArrowModifier::None)!=V2Viewport::ViewPress::None)return true;
                    return Pick(mouse->position());
                }
            }
        }
        if(event->type()==QEvent::KeyPress) {
            auto* target=qobject_cast<QWidget*>(object);
            if(!target || target->window()!=window())return false;
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Escape){Restart();return true;}
            if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter){Apply(false);return true;}
        }
        return false;
    }
    double Hit(const modeling::ShapeMesh& mesh,const QPointF& point) const {
        const auto ray=viewport_.Mapping().RayThrough({point.x(),point.y()});
        double best=std::numeric_limits<double>::max();if(!ray)return best;
        for(const auto& triangle:mesh.triangles) {
            const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle);
            if(hit && *hit<best)best=*hit;
        }
        return best;
    }
    bool Pick(const QPointF& point) {
        if(!done_)return true;
        const int row=list_->currentRow();
        if(choosingSide_ && row>=0) {
            const auto& r=overlay_->regions[row];
            const double a=Hit(sources_[r.first].mesh,point),b=Hit(sources_[r.second].mesh,point);
            if(std::min(a,b)<std::numeric_limits<double>::max())SelectSide(a<=b ? 0:1);
            else status_->setText(QStringLiteral("処理する部品をクリックするか、右の部品名を押してください。"));
            return true;
        }
        std::vector<int> hits;
        for(std::size_t i=0;i<overlay_->regions.size();++i)
            if(Hit(overlay_->regions[i].mesh,point)<std::numeric_limits<double>::max())hits.push_back(static_cast<int>(i));
        if(hits.empty())return true;
        auto found=std::find(hits.begin(),hits.end(),row);
        const int next=found!=hits.end() && ++found!=hits.end() ? *found:hits.front();
        list_->setCurrentRow(next);return true;
    }
    void SelectRegion(int row) {
        if(!overlay_)return;
        bool changedPair=false;
        if(row>=0 && overlay_->selected>=0) {
            const auto& previous=overlay_->regions[overlay_->selected];const auto& next=overlay_->regions[row];
            changedPair=previous.first!=next.first || previous.second!=next.second;
            if(changedPair)for(auto& r:overlay_->regions)r.removal=0;
        }
        overlay_->selected=row;overlay_->result.clear();selectedSide_=-1;ready_=false;
        choosingSide_=row>=0;
        for(int i=0;i<2;++i) {
            side_[i]->setEnabled(done_ && row>=0 && overlay_->regions[row].ordinal>=0);side_[i]->setChecked(false);
            if(row>=0) {const auto& r=overlay_->regions[row];side_[i]->setText(QStringLiteral("3 %1").arg(sources_[i==0 ? r.first:r.second].name));}
        }
        confirm_->setEnabled(false);wire_->setEnabled(done_ && row>=0);
        if(row>=0)status_->setText(QStringLiteral("2 残す／削るを選び、3 処理する側を3D上でクリックしてください。"));
        if(changedPair)status_->setText(QStringLiteral("別の組へ切り替えたため、前の未確定指定を解除しました。残す／削る側を選んでください。"));
        overlay_->update();
    }
    std::vector<int> Masks() const {
        const int row=list_->currentRow();if(row<0)return {};
        const auto& selected=overlay_->regions[row];std::vector<int> masks;
        for(const auto& r:overlay_->regions) if(r.first==selected.first && r.second==selected.second && r.ordinal>=0) {
            if(masks.size()<=static_cast<std::size_t>(r.ordinal))masks.resize(r.ordinal+1,0);
            masks[r.ordinal]=r.removal;
        }
        return masks;
    }
    void SelectSide(int side) {
        const int row=list_->currentRow();if(!done_ || row<0)return;
        auto& r=overlay_->regions[row];if(r.ordinal<0)return;
        selectedSide_=side;r.removal=(mode_->currentIndex()==0 ? side:1-side)==0 ? 1:2;
        side_[0]->setChecked(side==0);side_[1]->setChecked(side==1);
        overlay_->result.clear();ready_=false;
        const auto built=kernel::BuildLocalTrim(sources_[r.first].handle,sources_[r.second].handle,Masks(),tolerance_);
        if(!built.HasValue())status_->setText(QString::fromStdString(built.FirstSummaryJa()));
        else {
            ready_=true;
            for(const auto& piece:built.Value().pieces) {
                const auto mesh=kernel::BuildShapeMesh(piece.handle);
                if(mesh.HasValue())overlay_->result.push_back(mesh.Value());
                else {ready_=false;status_->setText(QString::fromStdString(mesh.FirstSummaryJa()));}
            }
            if(ready_)status_->setText(QStringLiteral("%1: %2。緑の輪郭を確認して Enter。別領域は一覧から追加指定できます。")
                .arg(sources_[side==0 ? r.first:r.second].name,mode_->currentText()));
        }
        confirm_->setEnabled(ready_);overlay_->update();
    }
    void Apply(bool wire) {
        const int row=list_->currentRow();if(!done_ || row<0 || (!wire && !ready_))return;
        const auto r=overlay_->regions[row];const auto a=sources_[r.first],b=sources_[r.second];
        const auto masks=Masks();const auto action=action_;
        host_.SetShelves({});
        action(a,b,r.ordinal,wire ? app::BooleanKind::ContactWire:app::BooleanKind::TrimOverlap,masks);
    }
    void Restart() {
        ++generation_;i_=0;j_=1;failures_=0;done_=false;ready_=false;choosingSide_=false;
        overlay_->regions.clear();overlay_->result.clear();list_->clear();SelectRegion(-1);Next(generation_);
    }
    bool BoundsOverlap(const modeling::ShapeMesh& a,const modeling::ShapeMesh& b) const {
        return a.maximum.x+tolerance_>=b.minimum.x && b.maximum.x+tolerance_>=a.minimum.x
            && a.maximum.y+tolerance_>=b.minimum.y && b.maximum.y+tolerance_>=a.minimum.y
            && a.maximum.z+tolerance_>=b.minimum.z && b.maximum.z+tolerance_>=a.minimum.z;
    }
    void Next(int generation) {
        QTimer::singleShot(0,this,[this,generation]{
            if(generation!=generation_ || !overlay_)return;
            const int n=static_cast<int>(sources_.size());
            if(i_>=n-1) {
                done_=true;status_->setText(QStringLiteral("検査完了: %1領域 / 失敗%2組。\n1 3Dビューのめり込み、または一覧を選んでください。\n%3")
                    .arg(overlay_->regions.size()).arg(failures_).arg(missing_));
                if(list_->currentRow()>=0)SelectRegion(list_->currentRow());return;
            }
            const auto& a=sources_[i_];const auto& b=sources_[j_];
            status_->setText(QStringLiteral("検査中: %1 / %2").arg(a.name,b.name));
            if((hidden_->isChecked() || (!a.hidden&&!b.hidden)) && BoundsOverlap(a.mesh,b.mesh))ScanPair(a,b);
            if(++j_>=n){++i_;j_=i_+1;}Next(generation);
        });
    }
    void ScanPair(const OverlapSource& a,const OverlapSource& b) {
        const auto built=kernel::BuildContact(a.handle,b.handle,false,false,true,tolerance_);
        if(!built.HasValue()) {
            if(built.FirstSummaryJa().find("重なり体積がありません")!=std::string::npos) {
                ScanTouch(a,b);return;
            }
            {
                ++failures_;missing_+=QStringLiteral("\n%1 / %2: %3").arg(a.name,b.name,QString::fromStdString(built.FirstSummaryJa()));
            }
            return;
        }
        for(const auto& piece:built.Value().pieces) {
            const auto mesh=kernel::BuildShapeMesh(piece.handle);
            if(!mesh.HasValue()){++failures_;missing_+=QString::fromStdString(mesh.FirstSummaryJa());continue;}
            overlay_->regions.push_back({i_,j_,piece.fragmentIndex,piece.volumeMm3,mesh.Value(),0});
            list_->addItem(QStringLiteral("%1: %2 / %3\n重なり %4 — %5 mm³").arg(overlay_->regions.size()).arg(a.name,b.name)
                .arg(piece.fragmentIndex+1).arg(piece.volumeMm3,0,'f',3));
        }
        overlay_->update();
    }
    void ScanTouch(const OverlapSource& a,const OverlapSource& b) {
        const auto contact=kernel::BuildContact(a.handle,b.handle,true,false,false,tolerance_);
        if(!contact.HasValue()) {
            if(contact.FirstSummaryJa().find("線になる接触・交差がありません")==std::string::npos) {
                ++failures_;missing_+=QStringLiteral("\n%1 / %2: %3").arg(a.name,b.name,QString::fromStdString(contact.FirstSummaryJa()));
            }
            return;
        }
        modeling::ShapeMesh mesh;
        for(const auto& wire:contact.Value().wires) for(const auto& curve:wire) {
            std::vector<geometry::Vector3> edge;
            const int steps=curve.Kind()==geometry::CurveKind::Line ? 1:64;
            for(int k=0;k<=steps;++k)edge.push_back(curve.Evaluate(static_cast<double>(k)/steps));
            mesh.edges.push_back(std::move(edge));
        }
        if(mesh.edges.empty())return;
        overlay_->regions.push_back({i_,j_,-1,0,mesh,0});
        list_->addItem(QStringLiteral("%1 / %2\n面接触: 交差ワイヤーのみ生成可能").arg(a.name,b.name));
        overlay_->update();
    }
    V2Viewport& viewport_;V2OperationPanelHost& host_;std::vector<OverlapSource> sources_;
    double tolerance_;QString missing_;OverlapAction action_;
    QPointer<OverlapOverlay> overlay_;QListWidget* list_=nullptr;QLabel* status_=nullptr;
    QCheckBox* hidden_=nullptr;QComboBox* mode_=nullptr;QPushButton* side_[2]{};
    QPushButton* confirm_=nullptr;QPushButton* wire_=nullptr;
    int i_=0,j_=1,generation_=0,failures_=0,selectedSide_=-1;bool done_=false,ready_=false,choosingSide_=false;
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
void OpenOverlapBrowser(V2Viewport& viewport,V2OperationPanelHost& host,std::vector<OverlapSource> sources,
    double tolerance,QString missing,OverlapAction action)
{
    auto* panel=new OverlapBrowser(viewport,host,std::move(sources),tolerance,std::move(missing),std::move(action));
    host.ShowTemporaryPage(panel,QStringLiteral("めり込みを処理"));
}
void InstallContactPicker(QWidget& viewport,std::function<bool(const QPointF&)> pick) {new ContactPicker(viewport,std::move(pick));}
